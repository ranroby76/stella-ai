// C:\workspace\Stella AI Studio\src\PluginExporter.h

#pragma once

#include <juce_core/juce_core.h>

#include "GuiLayout.h"
#include "PluginCanvas.h"
#include "PresetBank.h"
#include "Project.h"

#include <map>

//==============================================================================
/**
    Export: turns a project into plugin files a DAW loads, CLAP and VST3, and a standalone
    app that runs on its own, built on the user's own computer with the compiler that ships
    with the studio (llvm-mingw). They have no JUCE in them: the project's modules, the
    Stella runtime, the baked GUI and a wrapper per format (stella_clap.cpp,
    stella_vst3.cpp, stella_standalone.cpp), compiled natively and linked statically, so
    they need nothing installed beside them.

    They land in Documents\Stella AI Studio\Exports\<plugin name>: <name>.clap, <name>.vst3
    as a VST3 bundle folder, and <name>.exe.
*/
class PluginExporter final
{
public:
    struct Result
    {
        bool ok = false;
        juce::String log;        // the compiler's messages, or what went wrong
        juce::File plugin;       // the CLAP file, when ok
        juce::File vst3;         // the VST3 bundle, when ok
        juce::File app;          // the standalone app, when ok
        double seconds = 0.0;
    };

    /** The plugin's GUI, baked and ready to compile in: images as PNG, widgets as C++. */
    struct Gui
    {
        juce::MemoryBlock background;   // empty: the plugin has no GUI (hosts show their own controls)
        int width = 0, height = 0;

        struct Strip
        {
            juce::MemoryBlock png;
            int frameWidth = 0, frameHeight = 0, frames = 0;
        };

        std::vector<Strip> strips;
        juce::String widgetRows;
        int numWidgets = 0;

        juce::String presetArrays, presetRows;   // the presets, as C++
        int numPresets = 0;
    };

    /** On the message thread: encodes the canvas's bake and describes each widget. */
    static Gui prepareGui (const GuiLayout& layout, const PluginCanvas::Bake& bake,
                           const std::map<juce::String, juce::String>& unitsById, const juce::StringArray& displayIds,
                           const std::vector<Preset>& presets);

    /** Builds the CLAP and VST3 plugins and the standalone app (side by side). Takes a few
        seconds: call it on a worker thread. */
    static Result exportPlugins (const juce::File& projectFolder, const ProjectInfo& info, const juce::File& destinationFolder,
                                 const Gui& gui);

    static juce::File defaultExportFolder (const juce::String& pluginName);

    /** The bundled native compiler and the CLAP headers, or empty Files if missing. */
    static juce::File findCompiler();
    static juce::File findClapHeaders();
    static juce::File findVst3Headers();   // the folder holding pluginterfaces/
};
