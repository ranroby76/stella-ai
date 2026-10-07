// C:\workspace\Stella AI Studio\src\WasmCompiler.h

#pragma once

#include <juce_core/juce_core.h>

#include "Project.h"

//==============================================================================
/**
    Turns a project into a plugin the studio can play live: the project's modules plus
    the Stella runtime, compiled to WebAssembly by the compiler that ships with the
    studio (wasi-sdk). Nothing has to be installed on the user's computer.

    A project holds:
        graph.json   which modules the plugin has and how they're wired
        modules/     one .cpp file per module type, written against stella_api.h
*/
class WasmCompiler final
{
public:
    struct Result
    {
        bool ok = false;
        juce::String log;        // the compiler's messages (errors, warnings)
        juce::File wasm;         // the compiled plugin, when ok
        double seconds = 0.0;
    };

    /** Compiles the project. Takes a few seconds: call it on a worker thread. */
    static Result compile (const juce::File& projectFolder, const juce::String& projectId);

    /** A new project starts with a working demo (a small synth, or a delay for effects),
        so it plays at once. Does nothing if the project already has a graph. */
    static juce::Result writeStarter (const juce::File& projectFolder, PluginKind kind);

    /** The bundled compiler's folder, or an empty File if it's missing. */
    static juce::File findCompiler();

    static constexpr const char* graphFileName = "graph.json";

    /** graph.json as C++ (stella_graph.cpp), for the live build and for exports. */
    static juce::Result generateGraphSource (const juce::File& graphFile, juce::String& source);

    /** Writes stella_api.h, stella_runtime.h and stella_runtime.cpp into a build folder. */
    static bool writeRuntime (const juce::File& folder);
};
