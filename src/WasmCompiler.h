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
        elements/    programmed GUI elements, one .cpp file each, written against
                     stella_element_api.h: compiled into a module of their own (the GUI's)
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

    /** The bundled compiler's folder, or an empty File if it's missing. */
    static juce::File findCompiler();

    static constexpr const char* graphFileName = "graph.json";

    /** graph.json as C++ (stella_graph.cpp), for the live build and for exports. */
    static juce::Result generateGraphSource (const juce::File& graphFile, juce::String& source);

    /** Writes stella_api.h, stella_runtime.h and stella_runtime.cpp into a build folder. */
    static bool writeRuntime (const juce::File& folder);

    //==========================================================================
    static constexpr const char* elementsFolderName = "elements";

    /** A programmed element as its file declares it (STELLA_ELEMENT), before any build. */
    struct ElementSource
    {
        juce::String name, description, file;
    };

    static juce::Array<ElementSource> findElements (const juce::File& projectFolder);

    /** Compiles the elements folder's .cpp files into the GUI's module. With no element
        files, ok with no wasm. */
    static Result compileElements (const juce::File& projectFolder, const juce::String& projectId);

    /** Writes the elements' runtime (stella_element_api.h and the rest, and the font as
        stella_fonts.h) into a build folder, for the live build and for exports. */
    static bool writeElementRuntime (const juce::File& folder);
};
