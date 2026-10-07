// C:\workspace\Stella AI Studio\src\Workspace.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobStudio.h"
#include "PluginCanvas.h"
#include "SchematicView.h"

#include <functional>

//==============================================================================
/**
    The middle of the window, in three tabs:

        Plugin       the plugin, playing live, in two tabs of its own: Build, where its
                     GUI is reshaped, and Play, where the GUI is locked and plays like
                     the finished plugin
        Schematic    its DSP blocks and wires; click a block to instruct the AI
        Knob Studio  KnobMaker's tools, for shaping a knob or a panel
*/
class Workspace final : public juce::Component
{
public:
    enum TabIndex
    {
        pluginTab = 0,
        schematicTab,
        studioTab
    };

    enum class BuildState { idle, building, playing, failed };

    using ParamControl = PluginCanvas::Param;

    Workspace();
    ~Workspace() override;

    /** name empty: no project is open. */
    void setProject (const juce::String& name, const juce::String& kindName);

    /** The Plugin tab's own tabs: Build (false) or Play (true). */
    void setPlayMode (bool playMode);
    void showTab (TabIndex index);

    void setBuild (BuildState state, const juce::String& status);
    void setExporting (bool exporting);
    void setParameters (const juce::Array<ParamControl>& params);
    void setLayout (const GuiLayout& layout);
    const GuiLayout& getLayout() const noexcept   { return pluginView.canvas.getLayout(); }

    /** The presets row: names, the chosen one (-1: none), and which of A and B is on. */
    void setPresets (const juce::StringArray& names, int selected);
    void setAbSlot (int slot);

    /** What meters, lamps and scopes can watch (for the design panel's lists). */
    void setSources (const juce::StringArray& signals, const juce::StringArray& displays);

    SchematicView& getSchematic() noexcept        { return schematicView; }

    /** For Export: the GUI baked into images. */
    PluginCanvas::Bake bakeGui()                  { return pluginView.canvas.bake(); }

    std::function<void()> onNewRequested;
    std::function<void()> onOpenRequested;
    std::function<void()> onBuildRequested;
    std::function<void()> onShowLogRequested;
    std::function<void()> onExportRequested;
    std::function<void()> onAutoLayoutRequested;
    std::function<void()> onLayoutEdited;
    std::function<void (int index, float value)> onParameterChanged;
    std::function<void (bool playMode)> onModeChanged;   // the user picked Build or Play

    std::function<void (int presetIndex)> onPresetChosen;
    std::function<void()> onSavePreset;
    std::function<void (int presetIndex)> onDeletePreset;
    std::function<void (int slot)> onAbChosen;       // 0: A, 1: B
    std::function<void()> onAbCopy;                  // the slot that's on, copied to the other

    std::function<float (const juce::String& source, bool rms)> readLevel;
    std::function<void (const juce::String& source, float* destination, int numSamples)> readScope;
    std::function<void (const juce::StringArray& levels, const juce::StringArray& scopes)> onSourcesChanged;

    void resized() override;

private:
    //==============================================================================
    class PluginView final : public juce::Component
    {
    public:
        PluginView();

        void setProject (const juce::String& name, const juce::String& kindName);
        void setPlayMode (bool playMode);
        void setBuild (BuildState state, const juce::String& status);
        void setExporting (bool exporting);

        void setPresets (const juce::StringArray& names, int selected);
        void setAbSlot (int slot);

        std::function<void()> onNew, onOpen, onBuild, onShowLog, onExport, onAutoLayout, onSavePreset, onAbCopy;
        std::function<void (int)> onPresetChosen, onDeletePreset, onAbChosen;
        std::function<void (bool playMode)> onMode;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int barHeight = 48, rowHeight = 40;

        PluginCanvas canvas;

    private:
        void showAddMenu();
        void chooseMode (bool play);

        juce::TextButton newButton, openButton;

        // Two tabs: Build (reshape the panel) and Play (the panel locked, its controls live).
        juce::TextButton buildTab { "Build" }, playTab { "Play" };
        juce::TextButton rebuildButton { "Rebuild" }, logButton { "Log" }, exportButton { "Export" };

        // The Build tab's row: add parts, or start over with an automatic panel.
        juce::TextButton addButton { "+ Add" }, autoButton { "Auto layout" };

        // The Play tab's row: choose, save and delete presets; compare A and B.
        juce::ComboBox presetBox;
        juce::TextButton savePresetButton, deletePresetButton { "Delete" };
        juce::TextButton aButton { "A" }, bButton { "B" }, copyButton { "Copy A to B" };
        int abSlot = 0;

        juce::String projectName, kind, buildStatus;
        BuildState buildState = BuildState::idle;
        bool playMode = false;
        juce::Rectangle<int> statusArea, hintArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginView)
    };

    //==============================================================================
    class HintView final : public juce::Component
    {
    public:
        HintView (juce::String titleWithProject, juce::String detailWithProject, bool dotGrid);

        void setHasProject (bool hasProject);
        void paint (juce::Graphics&) override;

    private:
        juce::String title, detail;
        bool grid = false, project = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HintView)
    };

    //==============================================================================
    // The views are declared before the tabs, so the tabs let go of them first.
    PluginView pluginView;
    SchematicView schematicView;
    KnobStudio studio;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Workspace)
};
