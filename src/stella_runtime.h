// C:\workspace\Stella AI Studio\src\stella_runtime.h
//
// The Stella runtime: creates the modules a plugin's graph names, wires them, and runs
// them block by block. One Runtime is one plugin instance. It's compiled into every
// plugin: the studio's live WebAssembly build (see the exports at the end of
// stella_runtime.cpp) and the exported CLAP and VST3 plugins (stella_clap.cpp).
//
// Whoever hosts it talks to it through one Shared block: audio in and out, MIDI events,
// parameter values, bypassed modules, the signals the GUI watches, and module displays.

#pragma once

#include "stella_api.h"

#include <cstdint>
#include <string>
#include <vector>

namespace stella
{
    //==========================================================================
    // The graph, generated from graph.json by the studio (stella_graph.cpp).
    struct GraphModule { const char* id; const char* type; };
    struct GraphWire   { const char* fromModule; const char* fromPort; const char* toModule; const char* toPort; };

    extern const GraphModule graphModules[];
    extern const int numGraphModules;
    extern const GraphWire graphWires[];
    extern const int numGraphWires;

    //==========================================================================
    constexpr int maxEvents   = 512;
    constexpr int maxParams   = 1024;
    constexpr int maxNodes    = 256;
    constexpr int maxTaps     = 32;
    constexpr int maxScopes   = 4;
    constexpr int maxDisplays = 256;

    // Signal sources: a module's output (node >= 0), the plugin's input (-1) or output (-2).
    constexpr int pluginInput = -1, pluginOutput = -2;

    struct Event
    {
        std::uint32_t frame;
        std::uint8_t status, data1, data2, unused;
    };

    struct Tap
    {
        std::int32_t node, port;
        float peak, rms;   // of the last block
    };

    // Every field is 4 bytes (pointers too, in WebAssembly), so the layout is fixed.
    struct Shared
    {
        std::uint32_t maxFrames;
        std::uint32_t numEvents;
        std::uint32_t numParams;
        std::uint32_t numNodes;
        float* in[2];
        float* out[2];
        std::uint32_t numTaps;
        std::uint32_t numDisplays;
        std::uint32_t scopeFrames;               // valid samples in each scope this block
        std::uint32_t reserved;
        Tap taps[maxTaps];
        std::int32_t scopeNode[maxScopes];       // anything else than a source: off
        std::int32_t scopePort[maxScopes];
        float* scope[maxScopes];                 // maxFrames samples each, copied every block
        float displays[maxDisplays];
        std::uint8_t bypass[maxNodes];           // 1: the module is skipped (its first input passes through)
        float params[maxParams];
        Event events[maxEvents];
    };

    /** A parameter as the plugin's host sees it. */
    struct ParamInfo
    {
        std::string id, name, module, unit;
        float min = 0.0f, max = 1.0f, def = 0.0f, skew = 1.0f;
    };

    //==========================================================================
    class Runtime
    {
    public:
        Runtime() = default;
        ~Runtime();

        Runtime (const Runtime&) = delete;
        Runtime& operator= (const Runtime&) = delete;

        /** Creates the modules and wires them. Parameters are known from here on.
            false: something is wrong (getError says what). */
        bool build();

        /** Before processing, and again whenever the sample rate or block size change. */
        bool prepare (double sampleRate, int maxFrames);

        /** Runs one block: reads Shared's inputs, events and parameters, writes its outputs. */
        void process (int numFrames);

        /** Clears every module's state (voices, delay lines). */
        void reset();

        Shared& getShared() noexcept                    { return shared; }
        const std::string& getError() const noexcept    { return error; }
        const std::string& getDescription();            // JSON: modules, parameters, displays

        int getNumParams() const noexcept               { return (int) paramRefs.size(); }
        ParamInfo getParam (int index) const;

        /** A signal by name ("voices.out", "plugin.out L", "plugin.in R"), for taps and scopes. */
        bool findSignal (const std::string& name, std::int32_t& node, std::int32_t& port) const;

        /** A module display by id ("lfo.position"): its index in Shared::displays, or -1. */
        int findDisplay (const std::string& id) const;

    private:
        struct Source { int node; int port; };   // node -1: the plugin's inputs

        struct Instance
        {
            const ModuleType* type = nullptr;
            std::string id;
            Module* module = nullptr;
            std::vector<float> params;
            std::vector<float> displays;
            int firstDisplay = 0;                            // where its displays start in Shared::displays
            std::vector<std::vector<float>> outputs;         // one buffer per output port
            std::vector<std::vector<Source>> inputSources;   // per input port
            std::vector<std::vector<float>> mixBuffers;      // per input port with several sources
            std::vector<const float*> inputPointers;
            std::vector<float*> outputPointers;
        };

        struct ParamRef { int node; int index; };

        bool fail (const std::string& message);
        void describe();
        const float* sourceBuffer (const Source& source) const;
        const float* watched (std::int32_t node, std::int32_t port) const;

        Shared shared {};
        std::vector<Instance> nodes;
        std::vector<int> order;
        std::vector<ParamRef> paramRefs;
        std::vector<Source> pluginOutSources[2];
        std::vector<float> silence, inputBuffers[2], outputBuffers[2], scopeBuffers[maxScopes];
        std::vector<Note> notes;
        std::string description, error;
        double sampleRate = 48000.0;
        float pitchBend = 0.0f, modWheel = 0.0f;
        int numDisplays = 0;
        bool built = false, prepared = false;
    };
}
