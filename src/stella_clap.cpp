// C:\workspace\Stella AI Studio\src\stella_clap.cpp
//
// The exported plugin's CLAP side: no JUCE, just the CLAP API around the Stella runtime.
// Built by the studio's Export with the bundled compiler, together with the project's
// modules, the runtime, the GUI (stella_gui.cpp, stella_gui_win32.cpp) and three generated
// files: stella_graph.cpp (the graph), stella_plugin_info.h (name, vendor, id, kind) and
// stella_gui_data.cpp (the baked GUI).
//
// The GUI changes parameters the CLAP way: gesture begin, values, gesture end, sent to the
// host from process() or flush().
//
// Parameters keep stable CLAP ids (a hash of "module.param"), so automation and saved
// projects survive rebuilds. State is saved as "id=value" lines, so it survives
// parameters being added or reordered.

#include "stella_runtime.h"
#include "stella_plugin_info.h"
#include "stella_gui.h"

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
 #include <xmmintrin.h>
 #define STELLA_HAS_SSE 1
#endif
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
    //==========================================================================
    clap_id idFor (const std::string& text)
    {
        // FNV-1a: the same id for the same parameter, build after build.
        std::uint32_t hash = 2166136261u;

        for (const auto c : text)
        {
            hash ^= (std::uint8_t) c;
            hash *= 16777619u;
        }

        return hash == CLAP_INVALID_ID ? 1u : hash;
    }

    void copyText (char* destination, size_t capacity, const std::string& text)
    {
        if (capacity == 0)
            return;

        std::strncpy (destination, text.c_str(), capacity - 1);
        destination[capacity - 1] = 0;
    }

    //==========================================================================
    constexpr int scopeSize = 4096;

    /** What one GUI widget watches: a tap (its level), a module display, or a scope slot. */
    struct LiveSource
    {
        int tap = -1, display = -1, scope = -1;
    };

    /** A parameter change made on the GUI, on its way to the host. */
    struct GuiEvent
    {
        int type = 0;   // 0 gesture begins, 1 value, 2 gesture ends
        int index = 0;
        float value = 0.0f;
    };

    struct Plugin final : public stella::gui::Host
    {
        clap_plugin_t clap {};
        const clap_host_t* host = nullptr;
        const clap_host_params_t* hostParams = nullptr;

        stella::Runtime runtime;
        bool built = false, active = false;
        std::vector<stella::ParamInfo> params;
        std::vector<clap_id> ids;
        std::unique_ptr<std::atomic<float>[]> values;
        std::uint32_t maxFrames = 512;

        // The GUI: its window, and the live data it shows.
        std::unique_ptr<stella::gui::Window> window;
        std::vector<LiveSource> live;
        int numTaps = 0, numScopes = 0;
        std::array<std::atomic<float>, stella::maxTaps> tapPeak {}, tapRms {};
        std::array<std::vector<float>, stella::maxScopes> scopeRing;
        std::array<std::atomic<std::uint32_t>, stella::maxScopes> scopeWrite {};
        std::array<std::atomic<float>, stella::maxDisplays> displayValues {};

        // GUI -> host parameter changes: one writer (the GUI), one reader (process or flush).
        std::array<GuiEvent, 1024> guiEvents {};
        std::atomic<std::uint32_t> guiHead { 0 }, guiTail { 0 };

        // The GUI keyboard's notes, and the notes sounding (for the keyboard to show).
        stella::gui::Notes guiNotes;

        int indexOf (clap_id id) const
        {
            for (size_t i = 0; i < ids.size(); ++i)
                if (ids[i] == id)
                    return (int) i;

            return -1;
        }

        //======================================================================
        void pushGuiEvent (int type, int index, float value)
        {
            const auto head = guiHead.load (std::memory_order_relaxed);
            const auto next = (head + 1) % (std::uint32_t) guiEvents.size();

            if (next == guiTail.load (std::memory_order_acquire))
                return;   // full: the host isn't listening; the value itself is already set

            guiEvents[head] = { type, index, value };
            guiHead.store (next, std::memory_order_release);

            if (hostParams != nullptr)
                hostParams->request_flush (host);
        }

        /** Hands the GUI's changes to the host (from process() or flush()). */
        void drainGuiEvents (const clap_output_events_t* out)
        {
            auto tail = guiTail.load (std::memory_order_relaxed);

            while (tail != guiHead.load (std::memory_order_acquire))
            {
                const auto event = guiEvents[tail];
                tail = (tail + 1) % (std::uint32_t) guiEvents.size();
                guiTail.store (tail, std::memory_order_release);

                if (event.index < 0 || event.index >= (int) ids.size() || out == nullptr)
                    continue;

                if (event.type == 1)
                {
                    clap_event_param_value_t value {};
                    value.header = { sizeof (value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                    value.param_id = ids[(size_t) event.index];
                    value.note_id = -1;
                    value.port_index = -1;
                    value.channel = -1;
                    value.key = -1;
                    value.value = event.value;
                    out->try_push (out, &value.header);
                }
                else
                {
                    clap_event_param_gesture_t gesture {};
                    gesture.header = { sizeof (gesture), 0, CLAP_CORE_EVENT_SPACE_ID,
                                       (std::uint16_t) (event.type == 0 ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END), 0 };
                    gesture.param_id = ids[(size_t) event.index];
                    out->try_push (out, &gesture.header);
                }
            }
        }

        /** After each block: the levels, samples and displays the GUI shows. */
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

        //======================================================================
        // What the GUI needs (stella::gui::Host), on the main thread.
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

        void beginEdit (int param) override    { pushGuiEvent (0, param, 0.0f); }
        void endEdit (int param) override      { pushGuiEvent (2, param, 0.0f); }

        void setValue (int param, float value) override
        {
            if (param < 0 || param >= (int) params.size())
                return;

            value = std::min (params[(size_t) param].max, std::max (params[(size_t) param].min, value));
            values[(size_t) param].store (value, std::memory_order_relaxed);
            pushGuiEvent (1, param, value);
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

        void applyParamEvent (const clap_event_header_t* header)
        {
            if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
                return;

            const auto* event = reinterpret_cast<const clap_event_param_value_t*> (header);
            const auto index = indexOf (event->param_id);

            if (index >= 0)
                values[(size_t) index].store ((float) event->value, std::memory_order_relaxed);
        }
    };

    Plugin* self (const clap_plugin_t* plugin)
    {
        return static_cast<Plugin*> (plugin->plugin_data);
    }

    //==========================================================================
    // Parameters
    uint32_t paramsCount (const clap_plugin_t* plugin)
    {
        return (uint32_t) self (plugin)->params.size();
    }

    bool paramsGetInfo (const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info)
    {
        auto* p = self (plugin);

        if (index >= p->params.size())
            return false;

        const auto& param = p->params[index];
        std::memset (info, 0, sizeof (*info));
        info->id = p->ids[index];
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        info->cookie = nullptr;
        copyText (info->name, sizeof (info->name), param.name);
        copyText (info->module, sizeof (info->module), param.module);
        info->min_value = param.min;
        info->max_value = param.max;
        info->default_value = param.def;
        return true;
    }

    bool paramsGetValue (const clap_plugin_t* plugin, clap_id id, double* value)
    {
        auto* p = self (plugin);
        const auto index = p->indexOf (id);

        if (index < 0)
            return false;

        *value = p->values[(size_t) index].load (std::memory_order_relaxed);
        return true;
    }

    bool paramsValueToText (const clap_plugin_t* plugin, clap_id id, double value, char* text, uint32_t capacity)
    {
        auto* p = self (plugin);
        const auto index = p->indexOf (id);

        if (index < 0 || capacity == 0)
            return false;

        const auto& param = p->params[(size_t) index];
        const auto decimals = (param.max - param.min) >= 100.0f ? 0 : 2;
        std::snprintf (text, capacity, "%.*f%s%s", decimals, value, param.unit.empty() ? "" : " ", param.unit.c_str());
        return true;
    }

    bool paramsTextToValue (const clap_plugin_t* plugin, clap_id id, const char* text, double* value)
    {
        if (self (plugin)->indexOf (id) < 0)
            return false;

        char* end = nullptr;
        const auto parsed = std::strtod (text, &end);

        if (end == text)
            return false;

        *value = parsed;
        return true;
    }

    void paramsFlush (const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out)
    {
        auto* p = self (plugin);

        for (uint32_t i = 0, n = in->size (in); i < n; ++i)
            p->applyParamEvent (in->get (in, i));

        p->drainGuiEvents (out);
    }

    const clap_plugin_params_t paramsExtension { paramsCount, paramsGetInfo, paramsGetValue,
                                                 paramsValueToText, paramsTextToValue, paramsFlush };

    //==========================================================================
    // Audio and note ports
    uint32_t audioPortsCount (const clap_plugin_t*, bool isInput)
    {
        return isInput ? (stella::info::isInstrument ? 0u : 1u) : 1u;
    }

    bool audioPortsGet (const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
    {
        if (index != 0 || (isInput && stella::info::isInstrument))
            return false;

        std::memset (info, 0, sizeof (*info));
        info->id = isInput ? 0 : 1;
        copyText (info->name, sizeof (info->name), isInput ? "Input" : "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }

    const clap_plugin_audio_ports_t audioPortsExtension { audioPortsCount, audioPortsGet };

    uint32_t notePortsCount (const clap_plugin_t*, bool isInput)
    {
        // Instruments take notes; effects take them too (a module may follow the keyboard).
        return isInput ? 1u : 0u;
    }

    bool notePortsGet (const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
    {
        if (index != 0 || ! isInput)
            return false;

        std::memset (info, 0, sizeof (*info));
        info->id = 0;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        copyText (info->name, sizeof (info->name), "Notes");
        return true;
    }

    const clap_plugin_note_ports_t notePortsExtension { notePortsCount, notePortsGet };

    //==========================================================================
    // State: one "id=value" line per parameter.
    bool stateSave (const clap_plugin_t* plugin, const clap_ostream_t* stream)
    {
        auto* p = self (plugin);
        std::string text = "stella-state 1\n";

        for (size_t i = 0; i < p->params.size(); ++i)
        {
            char value[64];
            std::snprintf (value, sizeof (value), "%.9g", (double) p->values[i].load (std::memory_order_relaxed));
            text += p->params[i].id + "=" + value + "\n";
        }

        size_t written = 0;

        while (written < text.size())
        {
            const auto n = stream->write (stream, text.data() + written, text.size() - written);

            if (n <= 0)
                return false;

            written += (size_t) n;
        }

        return true;
    }

    bool stateLoad (const clap_plugin_t* plugin, const clap_istream_t* stream)
    {
        auto* p = self (plugin);
        std::string text;
        char buffer[4096];

        for (;;)
        {
            const auto n = stream->read (stream, buffer, sizeof (buffer));

            if (n < 0)
                return false;

            if (n == 0)
                break;

            text.append (buffer, (size_t) n);
        }

        if (text.rfind ("stella-state", 0) != 0)
            return false;

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

                for (size_t i = 0; i < p->params.size(); ++i)
                    if (p->params[i].id == id)
                        p->values[i].store ((float) std::atof (line.c_str() + equals + 1), std::memory_order_relaxed);
            }

            start = end + 1;
        }

        // Let the host refresh its view of the values.
        if (p->host != nullptr)
            if (const auto* hostParams = static_cast<const clap_host_params_t*> (p->host->get_extension (p->host, CLAP_EXT_PARAMS)))
                hostParams->rescan (p->host, CLAP_PARAM_RESCAN_VALUES);

        return true;
    }

    const clap_plugin_state_t stateExtension { stateSave, stateLoad };

    //==========================================================================
    // The plugin
    bool pluginInit (const clap_plugin_t* plugin)
    {
        auto* p = self (plugin);
        p->built = p->runtime.build();

        if (! p->built)
            return false;

        const auto count = p->runtime.getNumParams();
        p->values = std::make_unique<std::atomic<float>[]> ((size_t) (count > 0 ? count : 1));

        for (int i = 0; i < count; ++i)
        {
            p->params.push_back (p->runtime.getParam (i));
            p->ids.push_back (idFor (p->params.back().id));
            p->values[(size_t) i].store (p->params.back().def);
        }

        if (p->host != nullptr)
            p->hostParams = static_cast<const clap_host_params_t*> (p->host->get_extension (p->host, CLAP_EXT_PARAMS));

        // What the GUI's meters, lamps and scopes watch.
        auto& shared = p->runtime.getShared();

        for (int i = 0; i < stella::gui::numWidgets; ++i)
        {
            const auto& w = stella::gui::widgets[i];
            LiveSource source;
            std::int32_t node = 0, port = 0;

            if ((w.kind == stella::gui::Kind::meter || w.kind == stella::gui::Kind::lamp || w.kind == stella::gui::Kind::custom) && w.source != nullptr)
            {
                if (w.isDisplay)
                {
                    source.display = p->runtime.findDisplay (w.source);
                }
                else if (p->numTaps < stella::maxTaps && p->runtime.findSignal (w.source, node, port))
                {
                    shared.taps[p->numTaps] = { node, port, 0.0f, 0.0f };
                    source.tap = p->numTaps++;
                }
            }

            // A programmed element may read both: its level and its samples.
            if ((w.kind == stella::gui::Kind::scope || (w.kind == stella::gui::Kind::custom && ! w.isDisplay)) && w.source != nullptr && p->numScopes < stella::maxScopes
                && p->runtime.findSignal (w.source, node, port))
            {
                shared.scopeNode[p->numScopes] = node;
                shared.scopePort[p->numScopes] = port;
                p->scopeRing[(size_t) p->numScopes].assign ((size_t) scopeSize, 0.0f);
                source.scope = p->numScopes++;
            }

            p->live.push_back (source);
        }

        shared.numTaps = (std::uint32_t) p->numTaps;
        return true;
    }

    void pluginDestroy (const clap_plugin_t* plugin)
    {
        delete self (plugin);
    }

    bool pluginActivate (const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
    {
        auto* p = self (plugin);
        p->maxFrames = maxFrames > 0 ? (maxFrames < 8192 ? maxFrames : 8192) : 512;
        p->active = p->built && p->runtime.prepare (sampleRate, (int) p->maxFrames);
        return p->active;
    }

    void pluginDeactivate (const clap_plugin_t* plugin)
    {
        self (plugin)->active = false;
        self (plugin)->guiNotes.clear();
    }

    bool pluginStartProcessing (const clap_plugin_t*)  { return true; }
    void pluginStopProcessing (const clap_plugin_t*)   {}

    void pluginReset (const clap_plugin_t* plugin)
    {
        self (plugin)->runtime.reset();
    }

    /** Tiny numbers (denormals) are treated as zero while the plugin runs: they'd make the
        processor crawl in decaying filters and reverbs. The host's setting comes back after. */
    struct NoDenormals
    {
       #if STELLA_HAS_SSE
        NoDenormals() : saved (_mm_getcsr())   { _mm_setcsr (saved | 0x8040); }
        ~NoDenormals()                         { _mm_setcsr (saved); }
        unsigned int saved;
       #endif
    };

    clap_process_status pluginProcess (const clap_plugin_t* plugin, const clap_process_t* process)
    {
        auto* p = self (plugin);

        if (! p->active || process->audio_outputs_count == 0)
            return CLAP_PROCESS_CONTINUE;

        const NoDenormals noDenormals;
        p->drainGuiEvents (process->out_events);

        auto& shared = p->runtime.getShared();
        const auto total = process->frames_count;
        const auto numEvents = process->in_events != nullptr ? process->in_events->size (process->in_events) : 0u;
        uint32_t nextEvent = 0;

        float* const* outputs = process->audio_outputs[0].data32;
        const auto outputChannels = process->audio_outputs[0].channel_count;

        const float* const* inputs = process->audio_inputs_count > 0 ? process->audio_inputs[0].data32 : nullptr;
        const auto inputChannels = process->audio_inputs_count > 0 ? process->audio_inputs[0].channel_count : 0u;

        for (uint32_t start = 0; start < total; start += p->maxFrames)
        {
            const auto frames = (total - start) < p->maxFrames ? (total - start) : p->maxFrames;
            uint32_t count = 0;

            // The GUI's keyboard: its notes come first, at the start of the block.
            if (start == 0)
            {
                int note = 0, velocity = 0;

                while (p->guiNotes.pop (note, velocity))
                {
                    if (count >= (uint32_t) stella::maxEvents)
                        continue;

                    shared.events[count++] = { 0, (std::uint8_t) (velocity > 0 ? 0x90 : 0x80), (std::uint8_t) note, (std::uint8_t) velocity, 0 };
                    p->guiNotes.sounding (note, velocity > 0);
                }
            }

            // This chunk's events: notes and MIDI to the runtime, parameter changes to the values.
            while (nextEvent < numEvents)
            {
                const auto* header = process->in_events->get (process->in_events, nextEvent);

                if (header->time >= start + frames)
                    break;

                ++nextEvent;

                if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
                    continue;

                const auto frame = header->time > start ? header->time - start : 0u;

                switch (header->type)
                {
                    case CLAP_EVENT_NOTE_ON:
                    case CLAP_EVENT_NOTE_OFF:
                    {
                        const auto* note = reinterpret_cast<const clap_event_note_t*> (header);

                        if (note->key < 0 || note->key > 127 || count >= (uint32_t) stella::maxEvents)
                            break;

                        const auto on = header->type == CLAP_EVENT_NOTE_ON;
                        auto velocity = (int) (note->velocity * 127.0 + 0.5);
                        velocity = velocity < 1 ? (on ? 1 : 0) : (velocity > 127 ? 127 : velocity);
                        const auto channel = note->channel >= 0 && note->channel < 16 ? note->channel : 0;

                        shared.events[count++] = { frame, (std::uint8_t) ((on ? 0x90 : 0x80) | channel), (std::uint8_t) note->key,
                                                   (std::uint8_t) (on ? velocity : 0), 0 };
                        p->guiNotes.sounding (note->key, on);
                        break;
                    }

                    case CLAP_EVENT_MIDI:
                    {
                        const auto* midi = reinterpret_cast<const clap_event_midi_t*> (header);

                        if (count < (uint32_t) stella::maxEvents && midi->data[0] < 0xf0)
                        {
                            shared.events[count++] = { frame, midi->data[0], midi->data[1], midi->data[2], 0 };

                            const auto type = midi->data[0] & 0xf0;

                            if (type == 0x90 || type == 0x80)
                                p->guiNotes.sounding (midi->data[1] & 0x7f, type == 0x90 && midi->data[2] > 0);
                        }
                        break;
                    }

                    case CLAP_EVENT_PARAM_VALUE:
                        p->applyParamEvent (header);
                        break;

                    default:
                        break;
                }
            }

            shared.numEvents = count;

            for (size_t i = 0; i < p->params.size(); ++i)
                shared.params[i] = p->values[i].load (std::memory_order_relaxed);

            for (int c = 0; c < 2; ++c)
            {
                const float* in = inputs != nullptr && inputChannels > 0 ? inputs[(uint32_t) c < inputChannels ? c : 0] : nullptr;

                if (in != nullptr)
                    std::memcpy (shared.in[c], in + start, sizeof (float) * frames);
                else
                    std::memset (shared.in[c], 0, sizeof (float) * frames);
            }

            p->runtime.process ((int) frames);
            p->captureLive (frames);

            for (uint32_t c = 0; c < outputChannels; ++c)
                std::memcpy (outputs[c] + start, shared.out[c < 2 ? c : 1], sizeof (float) * frames);
        }

        return CLAP_PROCESS_CONTINUE;
    }

    //==========================================================================
    // The GUI: a window inside the host's (Windows so far; elsewhere hosts show their own).
   #if defined (_WIN32)
    bool guiIsApiSupported (const clap_plugin_t*, const char* api, bool floating)
    {
        return stella::gui::backgroundPngSize > 0 && ! floating && std::strcmp (api, CLAP_WINDOW_API_WIN32) == 0;
    }

    bool guiGetPreferredApi (const clap_plugin_t*, const char** api, bool* floating)
    {
        *api = CLAP_WINDOW_API_WIN32;
        *floating = false;
        return true;
    }

    bool guiCreate (const clap_plugin_t* plugin, const char* api, bool floating)
    {
        if (! guiIsApiSupported (plugin, api, floating))
            return false;

        auto* p = self (plugin);
        p->window = stella::gui::Window::create (*p);
        return p->window != nullptr;
    }

    void guiDestroy (const clap_plugin_t* plugin)                          { self (plugin)->window.reset(); }
    bool guiSetScale (const clap_plugin_t*, double)                        { return false; }
    bool guiCanResize (const clap_plugin_t*)                               { return false; }
    bool guiGetResizeHints (const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
    bool guiSetTransient (const clap_plugin_t*, const clap_window_t*)      { return false; }
    void guiSuggestTitle (const clap_plugin_t*, const char*)               {}

    bool guiGetSize (const clap_plugin_t*, uint32_t* width, uint32_t* height)
    {
        *width = (uint32_t) stella::gui::width;
        *height = (uint32_t) stella::gui::height;
        return true;
    }

    bool guiAdjustSize (const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
    {
        return guiGetSize (plugin, width, height);
    }

    bool guiSetSize (const clap_plugin_t*, uint32_t width, uint32_t height)
    {
        return width == (uint32_t) stella::gui::width && height == (uint32_t) stella::gui::height;
    }

    bool guiSetParent (const clap_plugin_t* plugin, const clap_window_t* window)
    {
        auto* p = self (plugin);
        return p->window != nullptr && window != nullptr && p->window->attach (window->win32);
    }

    bool guiShow (const clap_plugin_t* plugin)
    {
        if (auto& w = self (plugin)->window) w->setVisible (true);
        return true;
    }

    bool guiHide (const clap_plugin_t* plugin)
    {
        if (auto& w = self (plugin)->window) w->setVisible (false);
        return true;
    }

    const clap_plugin_gui_t guiExtension { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
                                           guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
                                           guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
   #endif

    const void* pluginGetExtension (const clap_plugin_t*, const char* id)
    {
        if (std::strcmp (id, CLAP_EXT_PARAMS) == 0)       return &paramsExtension;
        if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0)  return &audioPortsExtension;
        if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0)   return &notePortsExtension;
        if (std::strcmp (id, CLAP_EXT_STATE) == 0)        return &stateExtension;

       #if defined (_WIN32)
        if (std::strcmp (id, CLAP_EXT_GUI) == 0 && stella::gui::backgroundPngSize > 0)
            return &guiExtension;
       #endif

        return nullptr;
    }

    void pluginOnMainThread (const clap_plugin_t*) {}

    //==========================================================================
    // The factory: one plugin.
    const char* const instrumentFeatures[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr };
    const char* const effectFeatures[]     { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr };

    const clap_plugin_descriptor_t descriptor {
        CLAP_VERSION_INIT,
        stella::info::id,
        stella::info::name,
        stella::info::vendor,
        "",   // url
        "",   // manual
        "",   // support
        stella::info::version,
        stella::info::description,
        stella::info::isInstrument ? instrumentFeatures : effectFeatures
    };

    uint32_t factoryCount (const clap_plugin_factory_t*)
    {
        return 1;
    }

    const clap_plugin_descriptor_t* factoryDescriptor (const clap_plugin_factory_t*, uint32_t index)
    {
        return index == 0 ? &descriptor : nullptr;
    }

    const clap_plugin_t* factoryCreate (const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
    {
        if (! clap_version_is_compatible (host->clap_version) || std::strcmp (pluginId, descriptor.id) != 0)
            return nullptr;

        auto* p = new Plugin();
        p->host = host;
        p->clap.desc = &descriptor;
        p->clap.plugin_data = p;
        p->clap.init = pluginInit;
        p->clap.destroy = pluginDestroy;
        p->clap.activate = pluginActivate;
        p->clap.deactivate = pluginDeactivate;
        p->clap.start_processing = pluginStartProcessing;
        p->clap.stop_processing = pluginStopProcessing;
        p->clap.reset = pluginReset;
        p->clap.process = pluginProcess;
        p->clap.get_extension = pluginGetExtension;
        p->clap.on_main_thread = pluginOnMainThread;
        return &p->clap;
    }

    const clap_plugin_factory_t factory { factoryCount, factoryDescriptor, factoryCreate };

    bool entryInit (const char*)    { return true; }
    void entryDeinit()              {}

    const void* entryGetFactory (const char* factoryId)
    {
        return std::strcmp (factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
    }
}

// Exported by clap/entry.h's declaration of clap_entry.
extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
