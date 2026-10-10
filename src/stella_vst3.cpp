// C:\workspace\Stella AI Studio\src\stella_vst3.cpp
//
// The exported plugin's VST3 side: no JUCE and no SDK helper classes, just the VST3
// interfaces (pluginterfaces, MIT licensed) around the Stella runtime. One object is both
// the processor and the edit controller ("single component"), so the GUI, the host and
// the audio all share one set of parameter values.
//
// Same as the CLAP side: parameter ids are stable (a hash of "module.param"), state is
// "id=value" lines. VST3 hosts send pitch bend and the mod wheel as hidden parameters
// (IMidiMapping); they reach the modules as MIDI again.

#include "stella_runtime.h"
#include "stella_plugin_info.h"
#include "stella_gui.h"

#define INIT_CLASS_IID   // this file defines the interface ids it uses
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
 #include <xmmintrin.h>
 #define STELLA_HAS_SSE 1
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
    //==========================================================================
    constexpr ParamID pitchBendId = 0x7ffffff0, modWheelId = 0x7ffffff1;
    constexpr int scopeSize = 4096;

    ParamID idFor (const std::string& text)
    {
        // FNV-1a, kept below 2^31 (higher ids are the host's).
        std::uint32_t hash = 2166136261u;

        for (const auto c : text)
        {
            hash ^= (std::uint8_t) c;
            hash *= 16777619u;
        }

        hash &= 0x7fffffffu;
        return hash == pitchBendId || hash == modWheelId ? hash - 2 : hash;
    }

    /** UTF-8 to UTF-16, for the host's strings. */
    void toUtf16 (const std::string& text, char16* destination, int capacity)
    {
        int out = 0;

        for (size_t i = 0; i < text.size() && out < capacity - 1;)
        {
            const auto c = (unsigned char) text[i];
            std::uint32_t code = c;
            int extra = 0;

            if (c >= 0xf0)      { code = c & 0x07; extra = 3; }
            else if (c >= 0xe0) { code = c & 0x0f; extra = 2; }
            else if (c >= 0xc0) { code = c & 0x1f; extra = 1; }

            ++i;

            for (int k = 0; k < extra && i < text.size(); ++k, ++i)
                code = (code << 6) | ((unsigned char) text[i] & 0x3f);

            if (code >= 0x10000 && out < capacity - 2)
            {
                code -= 0x10000;
                destination[out++] = (char16) (0xd800 + (code >> 10));
                destination[out++] = (char16) (0xdc00 + (code & 0x3ff));
            }
            else if (code < 0x10000)
            {
                destination[out++] = (char16) code;
            }
        }

        destination[out] = 0;
    }

    std::string fromUtf16 (const char16* text)
    {
        std::string result;

        for (int i = 0; text != nullptr && text[i] != 0 && i < 256; ++i)
            result += text[i] < 128 ? (char) text[i] : '?';

        return result;
    }

    void copyAscii (char8* destination, size_t capacity, const char* text)
    {
        std::strncpy (destination, text, capacity - 1);
        destination[capacity - 1] = 0;
    }

    struct NoDenormals
    {
       #if STELLA_HAS_SSE
        NoDenormals() : saved (_mm_getcsr())   { _mm_setcsr (saved | 0x8040); }
        ~NoDenormals()                         { _mm_setcsr (saved); }
        unsigned int saved;
       #endif
    };

    struct LiveSource
    {
        int tap = -1, display = -1, scope = -1;
    };

    class View;

    //==========================================================================
    class Processor final : public IComponent,
                            public IAudioProcessor,
                            public IProcessContextRequirements,
                            public IEditController,
                            public IMidiMapping,
                            public stella::gui::Host
    {
    public:
        Processor()
        {
            built = runtime.build();

            if (! built)
                return;

            const auto count = runtime.getNumParams();
            values = std::make_unique<std::atomic<float>[]> ((size_t) (count > 0 ? count : 1));

            for (int i = 0; i < count; ++i)
            {
                params.push_back (runtime.getParam (i));
                ids.push_back (idFor (params.back().id));
                values[(size_t) i].store (params.back().def);
            }

            setUpLive();
        }

        ~Processor()
        {
            if (handler != nullptr)
                handler->release();
        }

        bool isBuilt() const noexcept    { return built; }

        //======================================================================
        // FUnknown: one object, several interfaces.
        tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
        {
            QUERY_INTERFACE (iid, obj, FUnknown::iid, IComponent)
            QUERY_INTERFACE (iid, obj, IPluginBase::iid, IComponent)
            QUERY_INTERFACE (iid, obj, IComponent::iid, IComponent)
            QUERY_INTERFACE (iid, obj, IAudioProcessor::iid, IAudioProcessor)
            QUERY_INTERFACE (iid, obj, IProcessContextRequirements::iid, IProcessContextRequirements)
            QUERY_INTERFACE (iid, obj, IEditController::iid, IEditController)
            QUERY_INTERFACE (iid, obj, IMidiMapping::iid, IMidiMapping)
            *obj = nullptr;
            return kNoInterface;
        }

        uint32 PLUGIN_API addRef() override    { return ++refs; }

        uint32 PLUGIN_API release() override
        {
            const auto left = --refs;

            if (left == 0)
                delete this;

            return left;
        }

        //======================================================================
        // IPluginBase (shared by the component and the controller)
        tresult PLUGIN_API initialize (FUnknown*) override    { return built ? kResultOk : kResultFalse; }
        tresult PLUGIN_API terminate() override                { return kResultOk; }

        //======================================================================
        // IComponent
        tresult PLUGIN_API getControllerClassId (TUID) override    { return kNotImplemented; }   // single component
        tresult PLUGIN_API setIoMode (IoMode) override              { return kResultOk; }

        int32 PLUGIN_API getBusCount (MediaType type, BusDirection direction) override
        {
            if (type == kAudio)
                return direction == kInput ? (stella::info::isInstrument ? 0 : 1) : 1;

            return direction == kInput ? 1 : 0;   // notes in
        }

        tresult PLUGIN_API getBusInfo (MediaType type, BusDirection direction, int32 index, BusInfo& bus) override
        {
            if (index != 0 || index >= getBusCount (type, direction))
                return kInvalidArgument;

            bus.mediaType = type;
            bus.direction = direction;
            bus.channelCount = type == kAudio ? 2 : 16;
            toUtf16 (type == kAudio ? (direction == kInput ? "Input" : "Output") : "Notes", bus.name, 128);
            bus.busType = kMain;
            bus.flags = BusInfo::kDefaultActive;
            return kResultOk;
        }

        tresult PLUGIN_API getRoutingInfo (RoutingInfo&, RoutingInfo&) override    { return kNotImplemented; }

        tresult PLUGIN_API activateBus (MediaType type, BusDirection direction, int32 index, TBool) override
        {
            return index >= 0 && index < getBusCount (type, direction) ? kResultTrue : kInvalidArgument;
        }

        tresult PLUGIN_API setActive (TBool state) override
        {
            if (state && ! prepared)
                prepared = runtime.prepare (sampleRate, maxFrames);

            if (! state)
            {
                runtime.reset();
                guiNotes.clear();
            }

            return kResultOk;
        }

        // Shared by IComponent and IEditController: the same state either way.
        tresult PLUGIN_API setState (IBStream* stream) override
        {
            if (stream == nullptr)
                return kInvalidArgument;

            std::string text;
            char buffer[4096];

            for (;;)
            {
                int32 read = 0;

                if (stream->read (buffer, (int32) sizeof (buffer), &read) != kResultOk || read <= 0)
                    break;

                text.append (buffer, (size_t) read);
            }

            if (text.rfind ("stella-state", 0) != 0)
                return kResultFalse;

            size_t start = 0;

            while (start < text.size())
            {
                auto end = text.find ('\n', start);

                if (end == std::string::npos)
                    end = text.size();

                const auto line = text.substr (start, end - start);
                const auto equals = line.find ('=');

                if (equals != std::string::npos)
                {
                    const auto id = line.substr (0, equals);

                    for (size_t i = 0; i < params.size(); ++i)
                        if (params[i].id == id)
                            values[i].store (clampValue ((int) i, (float) std::atof (line.c_str() + equals + 1)), std::memory_order_relaxed);
                }

                start = end + 1;
            }

            return kResultOk;
        }

        tresult PLUGIN_API getState (IBStream* stream) override
        {
            if (stream == nullptr)
                return kInvalidArgument;

            std::string text = "stella-state 1\n";

            for (size_t i = 0; i < params.size(); ++i)
            {
                char value[64];
                std::snprintf (value, sizeof (value), "%.9g", (double) values[i].load (std::memory_order_relaxed));
                text += params[i].id + "=" + value + "\n";
            }

            int32 written = 0;
            return stream->write (text.data(), (int32) text.size(), &written) == kResultOk && written == (int32) text.size() ? kResultOk : kResultFalse;
        }

        //======================================================================
        // IAudioProcessor
        tresult PLUGIN_API setBusArrangements (SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts) override
        {
            const auto expectedIns = stella::info::isInstrument ? 0 : 1;

            if (numIns != expectedIns || numOuts != 1)
                return kResultFalse;

            for (int32 i = 0; i < numIns; ++i)
                if (inputs[i] != SpeakerArr::kStereo)
                    return kResultFalse;

            return outputs[0] == SpeakerArr::kStereo ? kResultTrue : kResultFalse;
        }

        tresult PLUGIN_API getBusArrangement (BusDirection direction, int32 index, SpeakerArrangement& arrangement) override
        {
            if (index != 0 || index >= getBusCount (kAudio, direction))
                return kInvalidArgument;

            arrangement = SpeakerArr::kStereo;
            return kResultOk;
        }

        tresult PLUGIN_API canProcessSampleSize (int32 size) override    { return size == kSample32 ? kResultTrue : kResultFalse; }
        uint32 PLUGIN_API getLatencySamples() override                    { return 0; }
        uint32 PLUGIN_API getTailSamples() override                       { return kInfiniteTail; }   // releases and echoes ring on

        tresult PLUGIN_API setupProcessing (ProcessSetup& setup) override
        {
            sampleRate = setup.sampleRate > 0.0 ? setup.sampleRate : 48000.0;
            maxFrames = setup.maxSamplesPerBlock > 0 ? std::min (setup.maxSamplesPerBlock, 8192) : 512;
            prepared = built && runtime.prepare (sampleRate, maxFrames);
            return prepared ? kResultOk : kResultFalse;
        }

        tresult PLUGIN_API setProcessing (TBool) override    { return kResultOk; }

        // The modules don't read the host's tempo or position (yet).
        uint32 PLUGIN_API getProcessContextRequirements() override    { return 0; }

        tresult PLUGIN_API process (ProcessData& data) override
        {
            const NoDenormals noDenormals;
            auto& shared = runtime.getShared();
            int numPending = 0;

            // Parameter changes: the last point of each queue; pitch bend and mod wheel become MIDI.
            if (data.inputParameterChanges != nullptr)
            {
                for (int32 q = 0; q < data.inputParameterChanges->getParameterCount(); ++q)
                {
                    auto* queue = data.inputParameterChanges->getParameterData (q);

                    if (queue == nullptr || queue->getPointCount() <= 0)
                        continue;

                    int32 offset = 0;
                    ParamValue value = 0.0;

                    if (queue->getPoint (queue->getPointCount() - 1, offset, value) != kResultOk)
                        continue;

                    const auto id = queue->getParameterId();

                    if (id == pitchBendId || id == modWheelId)
                    {
                        if (numPending >= (int) pending.size())
                            continue;

                        if (id == pitchBendId)
                        {
                            const auto bend = std::min (16383, std::max (0, (int) std::lround (value * 16383.0)));
                            pending[(size_t) numPending++] = { (std::uint32_t) std::max (0, offset), 0xe0, (std::uint8_t) (bend & 0x7f), (std::uint8_t) (bend >> 7), 0 };
                        }
                        else
                        {
                            pending[(size_t) numPending++] = { (std::uint32_t) std::max (0, offset), 0xb0, 1, (std::uint8_t) std::lround (value * 127.0), 0 };
                        }

                        continue;
                    }

                    const auto index = indexOf (id);

                    if (index >= 0)
                        values[(size_t) index].store (toPlain (index, value), std::memory_order_relaxed);
                }
            }

            // The GUI's keyboard: its notes come first, at the start of the block.
            {
                int note = 0, velocity = 0;

                while (guiNotes.pop (note, velocity))
                    if (numPending < (int) pending.size())
                        pending[(size_t) numPending++] = { 0, (std::uint8_t) (velocity > 0 ? 0x90 : 0x80), (std::uint8_t) note, (std::uint8_t) velocity, 0 };
            }

            // The host's notes.
            if (data.inputEvents != nullptr)
            {
                for (int32 e = 0; e < data.inputEvents->getEventCount() && numPending < (int) pending.size(); ++e)
                {
                    Event event {};

                    if (data.inputEvents->getEvent (e, event) != kResultOk)
                        continue;

                    if (event.type == Event::kNoteOnEvent && event.noteOn.pitch >= 0 && event.noteOn.pitch < 128)
                    {
                        const auto velocity = std::min (127, std::max (1, (int) std::lround (event.noteOn.velocity * 127.0f)));
                        pending[(size_t) numPending++] = { (std::uint32_t) std::max (0, event.sampleOffset), (std::uint8_t) (0x90 | (event.noteOn.channel & 15)),
                                                           (std::uint8_t) event.noteOn.pitch, (std::uint8_t) velocity, 0 };
                    }
                    else if (event.type == Event::kNoteOffEvent && event.noteOff.pitch >= 0 && event.noteOff.pitch < 128)
                    {
                        pending[(size_t) numPending++] = { (std::uint32_t) std::max (0, event.sampleOffset), (std::uint8_t) (0x80 | (event.noteOff.channel & 15)),
                                                           (std::uint8_t) event.noteOff.pitch, 0, 0 };
                    }
                }
            }

            // In time order, keeping the order of events at the same moment (a note's start
            // before its stop). Few events at a time, so a plain insertion sort.
            for (int i = 1; i < numPending; ++i)
                for (int j = i; j > 0 && pending[(size_t) j - 1].frame > pending[(size_t) j].frame; --j)
                    std::swap (pending[(size_t) j - 1], pending[(size_t) j]);

            // What the GUI's keyboard shows down.
            for (int i = 0; i < numPending; ++i)
            {
                const auto type = pending[(size_t) i].status & 0xf0;

                if (type == 0x90 || type == 0x80)
                    guiNotes.sounding (pending[(size_t) i].data1, type == 0x90 && pending[(size_t) i].data2 > 0);
            }

            if (data.numSamples <= 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 1 || ! prepared)
                return kResultOk;

            const auto total = (std::uint32_t) data.numSamples;
            float** outputs = data.outputs[0].channelBuffers32;
            const auto outputChannels = data.outputs[0].numChannels;
            float** inputs = data.numInputs > 0 ? data.inputs[0].channelBuffers32 : nullptr;
            const auto inputChannels = data.numInputs > 0 ? data.inputs[0].numChannels : 0;
            int nextEvent = 0;

            for (std::uint32_t start = 0; start < total; start += (std::uint32_t) maxFrames)
            {
                const auto frames = std::min (total - start, (std::uint32_t) maxFrames);
                std::uint32_t count = 0;

                while (nextEvent < numPending && pending[(size_t) nextEvent].frame < start + frames)
                {
                    auto event = pending[(size_t) nextEvent++];
                    event.frame = event.frame > start ? event.frame - start : 0;

                    if (count < (std::uint32_t) stella::maxEvents)
                        shared.events[count++] = event;
                }

                shared.numEvents = count;

                for (size_t i = 0; i < params.size(); ++i)
                    shared.params[i] = values[i].load (std::memory_order_relaxed);

                for (int c = 0; c < 2; ++c)
                {
                    const float* in = inputs != nullptr && inputChannels > 0 ? inputs[c < inputChannels ? c : 0] : nullptr;

                    if (in != nullptr)
                        std::memcpy (shared.in[c], in + start, sizeof (float) * frames);
                    else
                        std::memset (shared.in[c], 0, sizeof (float) * frames);
                }

                runtime.process ((int) frames);
                captureLive (frames);

                for (int32 c = 0; c < outputChannels; ++c)
                    std::memcpy (outputs[c] + start, shared.out[c < 2 ? c : 1], sizeof (float) * frames);
            }

            data.outputs[0].silenceFlags = 0;
            return kResultOk;
        }

        //======================================================================
        // IEditController
        tresult PLUGIN_API setComponentState (IBStream*) override    { return kResultOk; }   // the same object holds it

        int32 PLUGIN_API getParameterCount() override    { return (int32) params.size() + 2; }

        tresult PLUGIN_API getParameterInfo (int32 index, ParameterInfo& info) override
        {
            if (index < 0 || index >= getParameterCount())
                return kInvalidArgument;

            std::memset (&info, 0, sizeof (info));
            info.unitId = kRootUnitId;

            if (index >= (int32) params.size())
            {
                const bool bend = index == (int32) params.size();
                info.id = bend ? pitchBendId : modWheelId;
                toUtf16 (bend ? "Pitch Bend" : "Mod Wheel", info.title, 128);
                toUtf16 (bend ? "Bend" : "Mod", info.shortTitle, 128);
                info.defaultNormalizedValue = bend ? 0.5 : 0.0;
                info.flags = ParameterInfo::kIsHidden;
                return kResultOk;
            }

            const auto& p = params[(size_t) index];
            info.id = ids[(size_t) index];
            toUtf16 (p.name, info.title, 128);
            toUtf16 (p.name, info.shortTitle, 128);
            toUtf16 (p.unit, info.units, 128);
            info.stepCount = 0;
            info.defaultNormalizedValue = toNormalized (index, p.def);
            info.flags = ParameterInfo::kCanAutomate;
            return kResultOk;
        }

        tresult PLUGIN_API getParamStringByValue (ParamID id, ParamValue normalized, String128 text) override
        {
            const auto index = indexOf (id);
            char buffer[128];

            if (index < 0)
            {
                if (id != pitchBendId && id != modWheelId)
                    return kInvalidArgument;

                std::snprintf (buffer, sizeof (buffer), "%.2f", id == pitchBendId ? normalized * 2.0 - 1.0 : normalized);
            }
            else
            {
                const auto& p = params[(size_t) index];
                const auto decimals = (p.max - p.min) >= 100.0f ? 0 : 2;
                std::snprintf (buffer, sizeof (buffer), "%.*f%s%s", decimals, (double) toPlain (index, normalized),
                               p.unit.empty() ? "" : " ", p.unit.c_str());
            }

            toUtf16 (buffer, text, 128);
            return kResultOk;
        }

        tresult PLUGIN_API getParamValueByString (ParamID id, TChar* text, ParamValue& normalized) override
        {
            const auto index = indexOf (id);

            if (index < 0 && id != pitchBendId && id != modWheelId)
                return kInvalidArgument;

            const auto ascii = fromUtf16 (text);
            char* end = nullptr;
            const auto parsed = std::strtod (ascii.c_str(), &end);

            if (end == ascii.c_str())
                return kResultFalse;

            if (index >= 0)
                normalized = toNormalized (index, (float) parsed);
            else
                normalized = std::min (1.0, std::max (0.0, id == pitchBendId ? (parsed + 1.0) * 0.5 : parsed));

            return kResultOk;
        }

        ParamValue PLUGIN_API normalizedParamToPlain (ParamID id, ParamValue normalized) override
        {
            const auto index = indexOf (id);
            return index >= 0 ? (ParamValue) toPlain (index, normalized) : normalized;
        }

        ParamValue PLUGIN_API plainParamToNormalized (ParamID id, ParamValue plain) override
        {
            const auto index = indexOf (id);
            return index >= 0 ? toNormalized (index, (float) plain) : plain;
        }

        ParamValue PLUGIN_API getParamNormalized (ParamID id) override
        {
            const auto index = indexOf (id);

            if (index < 0)
                return id == pitchBendId ? 0.5 : 0.0;

            return toNormalized (index, values[(size_t) index].load (std::memory_order_relaxed));
        }

        tresult PLUGIN_API setParamNormalized (ParamID id, ParamValue value) override
        {
            const auto index = indexOf (id);

            if (index < 0)
                return id == pitchBendId || id == modWheelId ? kResultOk : kInvalidArgument;

            values[(size_t) index].store (toPlain (index, value), std::memory_order_relaxed);
            return kResultOk;
        }

        tresult PLUGIN_API setComponentHandler (IComponentHandler* newHandler) override
        {
            if (newHandler == handler)
                return kResultOk;

            if (handler != nullptr)
                handler->release();

            handler = newHandler;

            if (handler != nullptr)
                handler->addRef();

            return kResultOk;
        }

        IPlugView* PLUGIN_API createView (FIDString name) override;

        //======================================================================
        // IMidiMapping: pitch bend and the mod wheel arrive as these hidden parameters.
        tresult PLUGIN_API getMidiControllerAssignment (int32 busIndex, int16, CtrlNumber controller, ParamID& id) override
        {
            if (busIndex != 0)
                return kResultFalse;

            if (controller == kPitchBend)    { id = pitchBendId; return kResultTrue; }
            if (controller == kCtrlModWheel) { id = modWheelId; return kResultTrue; }
            return kResultFalse;
        }

        //======================================================================
        // What the GUI needs (stella::gui::Host), on the UI thread.
        int findParam (const char* id) override
        {
            for (size_t i = 0; i < params.size(); ++i)
                if (params[i].id == id)
                    return (int) i;

            return -1;
        }

        float getValue (int param) override
        {
            return param >= 0 && param < (int) params.size() ? values[(size_t) param].load (std::memory_order_relaxed) : 0.0f;
        }

        void getRange (int param, float& min, float& max, float& def, float& skew) override
        {
            if (param < 0 || param >= (int) params.size())
                return;

            min = params[(size_t) param].min;
            max = params[(size_t) param].max;
            def = params[(size_t) param].def;
            skew = params[(size_t) param].skew;
        }

        void beginEdit (int param) override
        {
            if (handler != nullptr && param >= 0 && param < (int) ids.size())
                handler->beginEdit (ids[(size_t) param]);
        }

        void setValue (int param, float value) override
        {
            if (param < 0 || param >= (int) params.size())
                return;

            value = clampValue (param, value);
            values[(size_t) param].store (value, std::memory_order_relaxed);

            if (handler != nullptr)
                handler->performEdit (ids[(size_t) param], toNormalized (param, value));
        }

        void endEdit (int param) override
        {
            if (handler != nullptr && param >= 0 && param < (int) ids.size())
                handler->endEdit (ids[(size_t) param]);
        }

        float takeLevel (int widget, bool rms) override
        {
            if (widget < 0 || widget >= (int) live.size())
                return 0.0f;

            const auto& source = live[(size_t) widget];

            if (source.display >= 0)
                return displayValues[(size_t) source.display].load (std::memory_order_relaxed);

            if (source.tap < 0)
                return 0.0f;

            return rms ? tapRms[(size_t) source.tap].load (std::memory_order_relaxed)
                       : tapPeak[(size_t) source.tap].exchange (0.0f, std::memory_order_relaxed);
        }

        void readScope (int widget, float* destination, int numSamples) override
        {
            const auto slot = widget >= 0 && widget < (int) live.size() ? live[(size_t) widget].scope : -1;
            const auto count = std::min (numSamples, scopeSize);
            const auto write = slot >= 0 ? scopeWrite[(size_t) slot].load (std::memory_order_acquire) : 0u;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto age = (std::uint32_t) (count - i);
                destination[i] = slot >= 0 && i < count && age <= write ? scopeRing[(size_t) slot][(write - age) % (std::uint32_t) scopeSize] : 0.0f;
            }
        }

        void playNote (int note, float velocity) override
        {
            guiNotes.push (note, velocity > 0.0f ? std::min (127, std::max (1, (int) std::lround (velocity * 127.0f))) : 0);
        }

        bool isNoteDown (int note) override    { return guiNotes.isDown (note); }

    private:
        int indexOf (ParamID id) const
        {
            for (size_t i = 0; i < ids.size(); ++i)
                if (ids[i] == id)
                    return (int) i;

            return -1;
        }

        float clampValue (int index, float value) const
        {
            return std::min (params[(size_t) index].max, std::max (params[(size_t) index].min, value));
        }

        float toPlain (int index, ParamValue normalized) const
        {
            const auto& p = params[(size_t) index];
            return clampValue (index, p.min + (p.max - p.min) * (float) std::min (1.0, std::max (0.0, normalized)));
        }

        ParamValue toNormalized (int index, float plain) const
        {
            const auto& p = params[(size_t) index];
            return p.max > p.min ? std::min (1.0, std::max (0.0, (double) (plain - p.min) / (double) (p.max - p.min))) : 0.0;
        }

        void setUpLive()
        {
            auto& shared = runtime.getShared();

            for (int i = 0; i < stella::gui::numWidgets; ++i)
            {
                const auto& w = stella::gui::widgets[i];
                LiveSource source;
                std::int32_t node = 0, port = 0;

                if ((w.kind == stella::gui::Kind::meter || w.kind == stella::gui::Kind::lamp || w.kind == stella::gui::Kind::custom) && w.source != nullptr)
                {
                    if (w.isDisplay)
                    {
                        source.display = runtime.findDisplay (w.source);
                    }
                    else if (numTaps < stella::maxTaps && runtime.findSignal (w.source, node, port))
                    {
                        shared.taps[numTaps] = { node, port, 0.0f, 0.0f };
                        source.tap = numTaps++;
                    }
                }

                // A programmed element may read both: its level and its samples.
                if ((w.kind == stella::gui::Kind::scope || (w.kind == stella::gui::Kind::custom && ! w.isDisplay)) && w.source != nullptr && numScopes < stella::maxScopes
                    && runtime.findSignal (w.source, node, port))
                {
                    shared.scopeNode[numScopes] = node;
                    shared.scopePort[numScopes] = port;
                    scopeRing[(size_t) numScopes].assign ((size_t) scopeSize, 0.0f);
                    source.scope = numScopes++;
                }

                live.push_back (source);
            }

            shared.numTaps = (std::uint32_t) numTaps;
        }

        void captureLive (std::uint32_t frames)
        {
            auto& shared = runtime.getShared();

            for (int t = 0; t < numTaps; ++t)
            {
                auto held = tapPeak[(size_t) t].load (std::memory_order_relaxed);
                const auto peak = shared.taps[t].peak;

                while (peak > held && ! tapPeak[(size_t) t].compare_exchange_weak (held, peak, std::memory_order_relaxed)) {}

                tapRms[(size_t) t].store (shared.taps[t].rms, std::memory_order_relaxed);
            }

            for (int s = 0; s < numScopes; ++s)
            {
                auto write = scopeWrite[(size_t) s].load (std::memory_order_relaxed);

                for (std::uint32_t i = 0; i < frames; ++i)
                    scopeRing[(size_t) s][(write + i) % (std::uint32_t) scopeSize] = shared.scope[s][i];

                scopeWrite[(size_t) s].store (write + frames, std::memory_order_release);
            }

            for (const auto& source : live)
                if (source.display >= 0)
                    displayValues[(size_t) source.display].store (shared.displays[source.display], std::memory_order_relaxed);
        }

        std::atomic<uint32> refs { 1 };
        stella::Runtime runtime;
        bool built = false, prepared = false;
        double sampleRate = 48000.0;
        int maxFrames = 512;

        std::vector<stella::ParamInfo> params;
        std::vector<ParamID> ids;
        std::unique_ptr<std::atomic<float>[]> values;
        std::array<stella::Event, 1024> pending {};
        stella::gui::Notes guiNotes;   // the GUI keyboard's notes, and the notes sounding
        IComponentHandler* handler = nullptr;

        std::vector<LiveSource> live;
        int numTaps = 0, numScopes = 0;
        std::array<std::atomic<float>, stella::maxTaps> tapPeak {}, tapRms {};
        std::array<std::vector<float>, stella::maxScopes> scopeRing;
        std::array<std::atomic<std::uint32_t>, stella::maxScopes> scopeWrite {};
        std::array<std::atomic<float>, stella::maxDisplays> displayValues {};
    };

    //==========================================================================
    /** The GUI, inside the host's window (Windows so far). */
    class View final : public IPlugView
    {
    public:
        explicit View (Processor& p) : processor (p)    { processor.addRef(); }
        ~View()                                          { window.reset(); processor.release(); }

        tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
        {
            QUERY_INTERFACE (iid, obj, FUnknown::iid, IPlugView)
            QUERY_INTERFACE (iid, obj, IPlugView::iid, IPlugView)
            *obj = nullptr;
            return kNoInterface;
        }

        uint32 PLUGIN_API addRef() override    { return ++refs; }

        uint32 PLUGIN_API release() override
        {
            const auto left = --refs;

            if (left == 0)
                delete this;

            return left;
        }

        tresult PLUGIN_API isPlatformTypeSupported (FIDString type) override
        {
           #if defined (_WIN32)
            return type != nullptr && std::strcmp (type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
           #else
            (void) type;
            return kResultFalse;
           #endif
        }

        tresult PLUGIN_API attached (void* parent, FIDString type) override
        {
            if (isPlatformTypeSupported (type) != kResultTrue)
                return kResultFalse;

            window = stella::gui::Window::create (processor);
            return window != nullptr && window->attach (parent) ? kResultOk : kResultFalse;
        }

        tresult PLUGIN_API removed() override
        {
            window.reset();
            return kResultOk;
        }

        tresult PLUGIN_API onWheel (float) override                    { return kResultFalse; }
        tresult PLUGIN_API onKeyDown (char16, int16, int16) override   { return kResultFalse; }
        tresult PLUGIN_API onKeyUp (char16, int16, int16) override     { return kResultFalse; }
        tresult PLUGIN_API onSize (ViewRect*) override                 { return kResultTrue; }
        tresult PLUGIN_API onFocus (TBool) override                    { return kResultTrue; }
        tresult PLUGIN_API setFrame (IPlugFrame*) override             { return kResultTrue; }
        tresult PLUGIN_API canResize() override                        { return kResultFalse; }

        tresult PLUGIN_API getSize (ViewRect* size) override
        {
            if (size == nullptr)
                return kInvalidArgument;

            *size = ViewRect (0, 0, stella::gui::width, stella::gui::height);
            return kResultOk;
        }

        tresult PLUGIN_API checkSizeConstraint (ViewRect* rect) override
        {
            if (rect == nullptr)
                return kInvalidArgument;

            rect->right = rect->left + stella::gui::width;
            rect->bottom = rect->top + stella::gui::height;
            return kResultTrue;
        }

    private:
        std::atomic<uint32> refs { 1 };
        Processor& processor;
        std::unique_ptr<stella::gui::Window> window;
    };

    IPlugView* PLUGIN_API Processor::createView (FIDString name)
    {
       #if defined (_WIN32)
        if (name != nullptr && std::strcmp (name, ViewType::kEditor) == 0 && stella::gui::backgroundPngSize > 0)
            return new View (*this);
       #else
        (void) name;
       #endif

        return nullptr;
    }

    //==========================================================================
    class Factory final : public IPluginFactory3
    {
    public:
        tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
        {
            QUERY_INTERFACE (iid, obj, FUnknown::iid, IPluginFactory)
            QUERY_INTERFACE (iid, obj, IPluginFactory::iid, IPluginFactory)
            QUERY_INTERFACE (iid, obj, IPluginFactory2::iid, IPluginFactory2)
            QUERY_INTERFACE (iid, obj, IPluginFactory3::iid, IPluginFactory3)
            *obj = nullptr;
            return kNoInterface;
        }

        uint32 PLUGIN_API addRef() override     { return 1; }   // one factory for the module's life
        uint32 PLUGIN_API release() override    { return 1; }

        tresult PLUGIN_API getFactoryInfo (PFactoryInfo* info) override
        {
            if (info == nullptr)
                return kInvalidArgument;

            *info = PFactoryInfo();
            copyAscii (info->vendor, sizeof (info->vendor), stella::info::vendor);
            info->flags = PFactoryInfo::kUnicode;
            return kResultOk;
        }

        int32 PLUGIN_API countClasses() override    { return 1; }

        tresult PLUGIN_API getClassInfo (int32 index, PClassInfo* info) override
        {
            if (index != 0 || info == nullptr)
                return kInvalidArgument;

            *info = PClassInfo();
            std::memcpy (info->cid, stella::info::vst3Id, sizeof (TUID));
            info->cardinality = PClassInfo::kManyInstances;
            copyAscii (info->category, sizeof (info->category), kVstAudioEffectClass);
            copyAscii (info->name, sizeof (info->name), stella::info::name);
            return kResultOk;
        }

        tresult PLUGIN_API getClassInfo2 (int32 index, PClassInfo2* info) override
        {
            if (index != 0 || info == nullptr)
                return kInvalidArgument;

            *info = PClassInfo2();
            std::memcpy (info->cid, stella::info::vst3Id, sizeof (TUID));
            info->cardinality = PClassInfo::kManyInstances;
            copyAscii (info->category, sizeof (info->category), kVstAudioEffectClass);
            copyAscii (info->name, sizeof (info->name), stella::info::name);
            info->classFlags = 0;   // one component, not distributable
            copyAscii (info->subCategories, sizeof (info->subCategories), stella::info::isInstrument ? "Instrument|Synth" : "Fx");
            copyAscii (info->vendor, sizeof (info->vendor), stella::info::vendor);
            copyAscii (info->version, sizeof (info->version), stella::info::version);
            copyAscii (info->sdkVersion, sizeof (info->sdkVersion), kVstVersionString);
            return kResultOk;
        }

        tresult PLUGIN_API getClassInfoUnicode (int32 index, PClassInfoW* info) override
        {
            if (index != 0 || info == nullptr)
                return kInvalidArgument;

            *info = PClassInfoW();
            std::memcpy (info->cid, stella::info::vst3Id, sizeof (TUID));
            info->cardinality = PClassInfo::kManyInstances;
            copyAscii (info->category, sizeof (info->category), kVstAudioEffectClass);
            toUtf16 (stella::info::name, info->name, 64);
            info->classFlags = 0;
            copyAscii (info->subCategories, sizeof (info->subCategories), stella::info::isInstrument ? "Instrument|Synth" : "Fx");
            toUtf16 (stella::info::vendor, info->vendor, 64);
            toUtf16 (stella::info::version, info->version, 64);
            toUtf16 (kVstVersionString, info->sdkVersion, 64);
            return kResultOk;
        }

        tresult PLUGIN_API createInstance (FIDString cid, FIDString iid, void** obj) override
        {
            if (obj == nullptr)
                return kInvalidArgument;

            *obj = nullptr;

            if (cid == nullptr || std::memcmp (cid, stella::info::vst3Id, sizeof (TUID)) != 0)
                return kNoInterface;

            auto* processor = new Processor();

            if (! processor->isBuilt())
            {
                processor->release();
                return kResultFalse;
            }

            TUID wanted;
            std::memcpy (wanted, iid, sizeof (TUID));
            const auto result = processor->queryInterface (wanted, obj);
            processor->release();   // the interface handed out holds the only reference now
            return result;
        }

        tresult PLUGIN_API setHostContext (FUnknown*) override    { return kResultOk; }
    };

    Factory factory;
}

//==============================================================================
extern "C"
{
    SMTG_EXPORT_SYMBOL IPluginFactory* PLUGIN_API GetPluginFactory()
    {
        return &factory;
    }

   #if defined (_WIN32)
    SMTG_EXPORT_SYMBOL bool InitDll()    { return true; }
    SMTG_EXPORT_SYMBOL bool ExitDll()    { return true; }
   #elif defined (__linux__)
    SMTG_EXPORT_SYMBOL bool ModuleEntry (void*)    { return true; }
    SMTG_EXPORT_SYMBOL bool ModuleExit()           { return true; }
   #endif
}
