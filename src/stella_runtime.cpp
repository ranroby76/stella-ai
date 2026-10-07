// C:\workspace\Stella AI Studio\src\stella_runtime.cpp
//
// The Stella runtime (see stella_runtime.h), and the exports the studio's live
// WebAssembly build is driven through.

#include "stella_runtime.h"

#include <cmath>
#include <cstdio>
#include <new>

namespace stella
{
    //==========================================================================
    static std::vector<ModuleType>& registry()
    {
        static std::vector<ModuleType> types;   // filled by each module's Registrar
        return types;
    }

    void registerModuleType (const ModuleType& type)
    {
        registry().push_back (type);
    }

    namespace
    {
        constexpr int pluginNode = -1;   // "plugin" in the graph: the plugin's own audio

        void appendEscaped (std::string& out, const char* text)
        {
            out += '"';

            for (const char* c = text; c != nullptr && *c != 0; ++c)
            {
                switch (*c)
                {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n";  break;
                    default:
                        if ((unsigned char) *c >= 32)
                            out += *c;
                        break;
                }
            }

            out += '"';
        }

        void appendNumber (std::string& out, float value)
        {
            char buffer[48];
            std::snprintf (buffer, sizeof (buffer), "%.9g", (double) value);
            out += buffer;
        }

        const ModuleType* findType (const char* name)
        {
            for (const auto& type : registry())
                if (std::strcmp (type.type, name) == 0)
                    return &type;

            return nullptr;
        }

        int findPort (const char* const* names, int count, const char* port)
        {
            for (int i = 0; i < count; ++i)
                if (std::strcmp (names[i], port) == 0)
                    return i;

            return -1;
        }
    }

    //==========================================================================
    Runtime::~Runtime()
    {
        for (auto& node : nodes)
            delete node.module;
    }

    bool Runtime::fail (const std::string& message)
    {
        error = message;
        built = prepared = false;
        description.clear();
        return false;
    }

    ParamInfo Runtime::getParam (int index) const
    {
        ParamInfo info;

        if (index < 0 || index >= (int) paramRefs.size())
            return info;

        const auto& ref = paramRefs[(size_t) index];
        const auto& node = nodes[(size_t) ref.node];
        const auto& p = node.type->params[ref.index];

        info.id = node.id + "." + p.id;
        info.name = p.name != nullptr ? p.name : p.id;
        info.module = node.id;
        info.unit = p.unit != nullptr ? p.unit : "";
        info.min = p.min;
        info.max = p.max;
        info.def = p.def;
        info.skew = p.skew > 0.0f ? p.skew : 1.0f;
        return info;
    }

    bool Runtime::findSignal (const std::string& name, std::int32_t& node, std::int32_t& port) const
    {
        const auto dot = name.find ('.');

        if (dot == std::string::npos)
            return false;

        const auto module = name.substr (0, dot), portName = name.substr (dot + 1);

        if (module == "plugin")
        {
            node = portName.rfind ("in", 0) == 0 ? pluginInput : pluginOutput;
            port = ! portName.empty() && portName.back() == 'R' ? 1 : 0;
            return true;
        }

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i].id != module)
                continue;

            for (int p = 0; p < nodes[i].type->numOutputs; ++p)
            {
                if (portName == nodes[i].type->outputs[p])
                {
                    node = (std::int32_t) i;
                    port = p;
                    return true;
                }
            }
        }

        return false;
    }

    int Runtime::findDisplay (const std::string& id) const
    {
        for (const auto& node : nodes)
            for (size_t i = 0; i < node.displays.size(); ++i)
                if (node.id + "." + node.type->displayNames[i] == id)
                    return node.firstDisplay + (int) i;

        return -1;
    }

    //==========================================================================
    void Runtime::describe()
    {
        std::string& d = description;
        d = "{\"api\":1,\"error\":";
        appendEscaped (d, error.c_str());
        d += ",\"modules\":[";

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& node = nodes[i];
            if (i > 0) d += ',';

            d += "{\"id\":";     appendEscaped (d, node.id.c_str());
            d += ",\"type\":";   appendEscaped (d, node.type->type);
            d += ",\"name\":";   appendEscaped (d, node.type->name);
            d += ",\"inputs\":[";

            for (int p = 0; p < node.type->numInputs; ++p)
            {
                if (p > 0) d += ',';
                appendEscaped (d, node.type->inputs[p]);
            }

            d += "],\"outputs\":[";

            for (int p = 0; p < node.type->numOutputs; ++p)
            {
                if (p > 0) d += ',';
                appendEscaped (d, node.type->outputs[p]);
            }

            d += "]}";
        }

        d += "],\"params\":[";

        for (size_t i = 0; i < paramRefs.size(); ++i)
        {
            const auto p = getParam ((int) i);

            if (i > 0) d += ',';

            d += "{\"index\":";   appendNumber (d, (float) i);
            d += ",\"id\":";      appendEscaped (d, p.id.c_str());
            d += ",\"module\":";  appendEscaped (d, p.module.c_str());
            d += ",\"name\":";    appendEscaped (d, p.name.c_str());
            d += ",\"min\":";     appendNumber (d, p.min);
            d += ",\"max\":";     appendNumber (d, p.max);
            d += ",\"def\":";     appendNumber (d, p.def);
            d += ",\"unit\":";    appendEscaped (d, p.unit.c_str());
            d += ",\"skew\":";    appendNumber (d, p.skew);
            d += '}';
        }

        d += "],\"displays\":[";

        bool first = true;

        for (const auto& node : nodes)
        {
            for (int i = 0; i < (int) node.displays.size(); ++i)
            {
                if (! first) d += ',';
                first = false;

                const auto fullId = node.id + "." + node.type->displayNames[i];
                d += "{\"index\":";   appendNumber (d, (float) (node.firstDisplay + i));
                d += ",\"id\":";      appendEscaped (d, fullId.c_str());
                d += ",\"module\":";  appendEscaped (d, node.id.c_str());
                d += ",\"name\":";    appendEscaped (d, node.type->displayNames[i]);
                d += '}';
            }
        }

        d += "]}";
    }

    const std::string& Runtime::getDescription()
    {
        if (description.empty())
            describe();

        return description;
    }

    //==========================================================================
    bool Runtime::build()
    {
        if (built || ! nodes.empty())
            return fail ("The plugin was built twice.");

        for (int i = 0; i < numGraphModules; ++i)
        {
            const auto* type = findType (graphModules[i].type);

            if (type == nullptr)
                return fail (std::string ("The graph uses a module type that doesn't exist: ") + graphModules[i].type);

            for (const auto& node : nodes)
                if (node.id == graphModules[i].id)
                    return fail (std::string ("Two modules share the id ") + graphModules[i].id);

            Instance node;
            node.type = type;
            node.id = graphModules[i].id;
            node.module = type->create();

            if (node.module == nullptr)
                return fail (std::string ("Couldn't create the module ") + node.id);

            node.params.resize ((size_t) type->numParams);

            for (int p = 0; p < type->numParams; ++p)
                node.params[(size_t) p] = type->params[p].def;

            node.displays.assign ((size_t) (type->displayNames != nullptr && type->numDisplays > 0 ? type->numDisplays : 0), 0.0f);
            node.inputSources.resize ((size_t) type->numInputs);
            node.mixBuffers.resize ((size_t) type->numInputs);
            node.inputPointers.resize ((size_t) type->numInputs, nullptr);
            node.outputs.resize ((size_t) type->numOutputs);
            node.outputPointers.resize ((size_t) type->numOutputs, nullptr);

            nodes.push_back (std::move (node));
            nodes.back().module->params = nodes.back().params.data();
            nodes.back().module->displays = nodes.back().displays.empty() ? nullptr : nodes.back().displays.data();
        }

        static const char* const pluginInputNames[]  { "in L", "in R" };
        static const char* const pluginOutputNames[] { "out L", "out R" };

        auto findNode = [this] (const char* id)
        {
            if (std::strcmp (id, "plugin") == 0)
                return pluginNode;

            for (size_t i = 0; i < nodes.size(); ++i)
                if (nodes[i].id == id)
                    return (int) i;

            return -2;
        };

        for (int w = 0; w < numGraphWires; ++w)
        {
            const auto& wire = graphWires[w];
            const auto from = findNode (wire.fromModule);
            const auto to = findNode (wire.toModule);

            if (from == -2 || to == -2)
                return fail (std::string ("A wire names a module that doesn't exist: ")
                             + (from == -2 ? wire.fromModule : wire.toModule));

            const auto fromPort = from == pluginNode ? findPort (pluginInputNames, 2, wire.fromPort)
                                                     : findPort (nodes[(size_t) from].type->outputs, nodes[(size_t) from].type->numOutputs, wire.fromPort);

            const auto toPort = to == pluginNode ? findPort (pluginOutputNames, 2, wire.toPort)
                                                 : findPort (nodes[(size_t) to].type->inputs, nodes[(size_t) to].type->numInputs, wire.toPort);

            if (fromPort < 0)
                return fail (std::string ("A wire starts at a port that doesn't exist: ") + wire.fromModule + "." + wire.fromPort);

            if (toPort < 0)
                return fail (std::string ("A wire ends at a port that doesn't exist: ") + wire.toModule + "." + wire.toPort);

            if (to == pluginNode)
                pluginOutSources[toPort].push_back ({ from, fromPort });
            else
                nodes[(size_t) to].inputSources[(size_t) toPort].push_back ({ from, fromPort });
        }

        // Processing order: every module after the modules feeding it.
        std::vector<int> pending (nodes.size(), 0);

        for (size_t i = 0; i < nodes.size(); ++i)
            for (const auto& sources : nodes[i].inputSources)
                for (const auto& source : sources)
                    if (source.node >= 0)
                        ++pending[i];

        std::vector<int> readyNodes;

        for (size_t i = 0; i < nodes.size(); ++i)
            if (pending[i] == 0)
                readyNodes.push_back ((int) i);

        while (! readyNodes.empty())
        {
            const auto current = readyNodes.back();
            readyNodes.pop_back();
            order.push_back (current);

            for (size_t i = 0; i < nodes.size(); ++i)
                for (const auto& sources : nodes[i].inputSources)
                    for (const auto& source : sources)
                        if (source.node == current && --pending[i] == 0)
                            readyNodes.push_back ((int) i);
        }

        if (order.size() != nodes.size())
            return fail ("The modules are wired in a loop. Feedback has to stay inside one module.");

        for (size_t i = 0; i < nodes.size(); ++i)
            for (int p = 0; p < nodes[i].type->numParams; ++p)
                paramRefs.push_back ({ (int) i, p });

        if ((int) paramRefs.size() > maxParams)
            return fail ("The plugin has more parameters than Stella allows (1024).");

        if ((int) nodes.size() > maxNodes)
            return fail ("The plugin has more modules than Stella allows (256).");

        for (auto& node : nodes)
        {
            node.firstDisplay = numDisplays;
            numDisplays += (int) node.displays.size();
        }

        if (numDisplays > maxDisplays)
            return fail ("The plugin's modules show more values than Stella allows (256).");

        shared.numParams = (std::uint32_t) paramRefs.size();
        shared.numNodes = (std::uint32_t) nodes.size();
        shared.numDisplays = (std::uint32_t) numDisplays;

        for (size_t i = 0; i < paramRefs.size(); ++i)
            shared.params[i] = nodes[(size_t) paramRefs[i].node].params[(size_t) paramRefs[i].index];

        for (int i = 0; i < maxScopes; ++i)
            shared.scopeNode[i] = -100;

        built = true;
        describe();
        return true;
    }

    bool Runtime::prepare (double newSampleRate, int maxFrames)
    {
        if (! built)
            return fail (error.empty() ? std::string ("The plugin wasn't built.") : error);

        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        maxFrames = maxFrames > 0 ? (maxFrames < 8192 ? maxFrames : 8192) : 512;

        silence.assign ((size_t) maxFrames, 0.0f);

        for (auto& buffer : scopeBuffers)
            buffer.assign ((size_t) maxFrames, 0.0f);

        for (int c = 0; c < 2; ++c)
        {
            inputBuffers[c].assign ((size_t) maxFrames, 0.0f);
            outputBuffers[c].assign ((size_t) maxFrames, 0.0f);
        }

        notes.reserve (maxEvents);

        for (auto& node : nodes)
        {
            for (size_t o = 0; o < node.outputs.size(); ++o)
            {
                node.outputs[o].assign ((size_t) maxFrames, 0.0f);
                node.outputPointers[o] = node.outputs[o].data();
            }

            for (size_t p = 0; p < node.inputSources.size(); ++p)
                if (node.inputSources[p].size() > 1)
                    node.mixBuffers[p].assign ((size_t) maxFrames, 0.0f);

            node.module->prepare (sampleRate, maxFrames);
        }

        shared.maxFrames = (std::uint32_t) maxFrames;
        shared.numEvents = 0;
        shared.scopeFrames = 0;

        for (int c = 0; c < 2; ++c)
        {
            shared.in[c] = inputBuffers[c].data();
            shared.out[c] = outputBuffers[c].data();
        }

        for (int i = 0; i < maxScopes; ++i)
            shared.scope[i] = scopeBuffers[i].data();

        prepared = true;
        return true;
    }

    void Runtime::reset()
    {
        for (auto& node : nodes)
            node.module->reset();
    }

    const float* Runtime::sourceBuffer (const Source& source) const
    {
        if (source.node == pluginNode)
            return inputBuffers[source.port].data();

        return nodes[(size_t) source.node].outputs[(size_t) source.port].data();
    }

    const float* Runtime::watched (std::int32_t node, std::int32_t port) const
    {
        if (node >= 0 && node < (std::int32_t) nodes.size() && port >= 0 && port < (std::int32_t) nodes[(size_t) node].outputs.size())
            return nodes[(size_t) node].outputs[(size_t) port].data();

        if (node == pluginInput && (port == 0 || port == 1))
            return inputBuffers[port].data();

        if (node == pluginOutput && (port == 0 || port == 1))
            return outputBuffers[port].data();

        return nullptr;
    }

    //==========================================================================
    void Runtime::process (int numFrames)
    {
        if (! prepared)
            return;

        const auto frames = numFrames < 0 ? 0 : (numFrames > (int) shared.maxFrames ? (int) shared.maxFrames : numFrames);

        // Parameters: the host writes them; out-of-range values are clamped.
        for (size_t i = 0; i < paramRefs.size(); ++i)
        {
            const auto& ref = paramRefs[i];
            const auto& p = nodes[(size_t) ref.node].type->params[ref.index];
            const auto value = shared.params[i];
            nodes[(size_t) ref.node].params[(size_t) ref.index] = value == value ? clamp (value, p.min, p.max) : p.def;
        }

        // MIDI: notes, pitch bend, mod wheel, all notes off.
        notes.clear();
        const auto numEvents = shared.numEvents < (std::uint32_t) maxEvents ? shared.numEvents : (std::uint32_t) maxEvents;

        for (std::uint32_t e = 0; e < numEvents; ++e)
        {
            const auto& event = shared.events[e];
            const auto kind = event.status & 0xf0;
            const int frame = event.frame < (std::uint32_t) frames ? (int) event.frame : (frames > 0 ? frames - 1 : 0);

            if (kind == 0x90 && event.data2 > 0)
                notes.push_back ({ frame, event.data1 & 0x7f, (float) event.data2 / 127.0f, true });
            else if (kind == 0x80 || (kind == 0x90 && event.data2 == 0))
                notes.push_back ({ frame, event.data1 & 0x7f, 0.0f, false });
            else if (kind == 0xe0)
                pitchBend = ((float) ((event.data2 << 7) | event.data1) - 8192.0f) / 8192.0f;
            else if (kind == 0xb0 && event.data1 == 1)
                modWheel = (float) event.data2 / 127.0f;
            else if (kind == 0xb0 && (event.data1 == 120 || event.data1 == 123))
                for (int n = 0; n < 128; ++n)
                    notes.push_back ({ frame, n, 0.0f, false });
        }

        shared.numEvents = 0;

        const Context context { sampleRate, frames, notes.data(), (int) notes.size(), pitchBend, modWheel };

        for (const auto index : order)
        {
            auto& node = nodes[(size_t) index];

            for (size_t p = 0; p < node.inputSources.size(); ++p)
            {
                const auto& sources = node.inputSources[p];

                if (sources.empty())
                {
                    node.inputPointers[p] = silence.data();
                }
                else if (sources.size() == 1)
                {
                    node.inputPointers[p] = sourceBuffer (sources[0]);
                }
                else
                {
                    auto* mix = node.mixBuffers[p].data();
                    std::memset (mix, 0, sizeof (float) * (size_t) frames);

                    for (const auto& source : sources)
                    {
                        const auto* from = sourceBuffer (source);

                        for (int i = 0; i < frames; ++i)
                            mix[i] += from[i];
                    }

                    node.inputPointers[p] = mix;
                }
            }

            if (index < maxNodes && shared.bypass[index] != 0)
            {
                // Bypassed: the first input goes straight to the first output; the rest is silent.
                for (size_t o = 0; o < node.outputPointers.size(); ++o)
                {
                    if (o == 0 && ! node.inputPointers.empty())
                        std::memcpy (node.outputPointers[0], node.inputPointers[0], sizeof (float) * (size_t) frames);
                    else
                        std::memset (node.outputPointers[o], 0, sizeof (float) * (size_t) frames);
                }

                continue;
            }

            node.module->process (context, node.inputPointers.data(), node.outputPointers.data());
        }

        // The plugin's outputs, made safe: no NaN or infinity, nothing beyond +-4.
        for (int c = 0; c < 2; ++c)
        {
            auto* out = outputBuffers[c].data();
            std::memset (out, 0, sizeof (float) * (size_t) frames);

            for (const auto& source : pluginOutSources[c])
            {
                const auto* from = sourceBuffer (source);

                for (int i = 0; i < frames; ++i)
                    out[i] += from[i];
            }

            for (int i = 0; i < frames; ++i)
                out[i] = out[i] == out[i] ? clamp (out[i], -4.0f, 4.0f) : 0.0f;
        }

        // What the studio watches: levels for meters, copies for scopes, module displays.
        const auto numTaps = shared.numTaps < (std::uint32_t) maxTaps ? shared.numTaps : (std::uint32_t) maxTaps;

        for (std::uint32_t t = 0; t < numTaps; ++t)
        {
            auto& tap = shared.taps[t];
            const auto* signal = watched (tap.node, tap.port);
            float peak = 0.0f, sum = 0.0f;

            if (signal != nullptr)
            {
                for (int i = 0; i < frames; ++i)
                {
                    const auto v = signal[i];
                    peak = v > peak ? v : (-v > peak ? -v : peak);
                    sum += v * v;
                }
            }

            tap.peak = peak;
            tap.rms = frames > 0 ? std::sqrt (sum / (float) frames) : 0.0f;
        }

        for (int s = 0; s < maxScopes; ++s)
        {
            if (const auto* signal = watched (shared.scopeNode[s], shared.scopePort[s]))
                std::memcpy (scopeBuffers[s].data(), signal, sizeof (float) * (size_t) frames);
            else
                std::memset (scopeBuffers[s].data(), 0, sizeof (float) * (size_t) frames);
        }

        shared.scopeFrames = (std::uint32_t) frames;

        for (const auto& node : nodes)
            for (size_t i = 0; i < node.displays.size(); ++i)
            {
                const auto v = node.displays[i];
                shared.displays[(size_t) node.firstDisplay + i] = v == v ? v : 0.0f;
            }
    }
}

//==============================================================================
// The studio's live WebAssembly build: one plugin instance per WebAssembly instance.
#if defined (__wasm__)

namespace
{
    stella::Runtime& instance()
    {
        static stella::Runtime runtime;
        return runtime;
    }
}

#define STELLA_EXPORT(name) __attribute__ ((export_name (name)))

extern "C"
{
    STELLA_EXPORT ("stella_api_version") int stella_api_version()
    {
        return 3;
    }

    /** Builds the graph. 1: ready; 0: something is wrong (stella_describe says what). */
    STELLA_EXPORT ("stella_create") int stella_create (float sampleRate, int maxFrames)
    {
        return instance().build() && instance().prepare ((double) sampleRate, maxFrames) ? 1 : 0;
    }

    STELLA_EXPORT ("stella_shared") stella::Shared* stella_shared()
    {
        return &instance().getShared();
    }

    STELLA_EXPORT ("stella_describe") const char* stella_describe()
    {
        static std::string text;
        text = instance().getDescription();

        if (text.empty())
        {
            // Nothing built: the description carries just the error.
            text = "{\"api\":1,\"error\":\"";

            for (const auto c : instance().getError())
                if (c != '"' && c != '\\' && (unsigned char) c >= 32)
                    text += c;

            text += "\",\"modules\":[],\"params\":[],\"displays\":[]}";
        }

        return text.c_str();
    }

    STELLA_EXPORT ("stella_process") void stella_process (int numFrames)
    {
        instance().process (numFrames);
    }
}

#endif
