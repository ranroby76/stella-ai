// C:\workspace\Stella AI Studio\src\Workspace.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobStudio.h"
#include "Looks.h"
#include "PluginCanvas.h"
#include "SchematicView.h"
#include "Toolbox.h"

#include <functional>

//==============================================================================
/**
    The window below the top bar, in four tabs (picked in the top bar):

        Build with AI  the conversation with Stella AI (the main window's chat panel)
        Edit UI        the plugin, playing live, on a grid you zoom and move around, in two
                       tabs of its own: Edit, where its GUI is reshaped (with the primitives
                       toolbox on the left), and Play, where the GUI is locked and plays like
                       the finished plugin. Empty (just the grid) until a project is open
        Schematic      its DSP blocks and wires; click a block to instruct the AI
        Knob Studio    KnobMaker's layer builder, for the looks of its knobs, sliders and
                       switches (double-click one in Edit UI to open its look)
*/
class Workspace final : public juce::Component
{
public:
    enum TabIndex
    {
        aiTab = 0,
        pluginTab,      // Edit UI
        schematicTab,
        studioTab
    };

    enum class BuildState { idle, building, playing, failed };

    using ParamControl = PluginCanvas::Param;

    Workspace();

    /** The Build with AI tab's page: the main window owns it. */
    void setAiPage (juce::Component& page);

    /** name empty: no project is open. */
    void setProject (const juce::String& name, const juce::String& kindName);

    /** The open project's gui folder (pictures, looks, the Edit UI view); none: no project. */
    void setGuiFolder (const juce::File& folder);

    /** The looks the plugin's controls wear, and the Knob Studio edits. */
    Looks& getLooks() noexcept                    { return looks; }

    /** The Edit UI banner: Stella AI at work on a request from an edit menu, then its answer. */
    void showAiWorking (const juce::String& what);
    void showAiReply (const juce::String& reply);

    /** The Edit UI tab's own tabs: Edit (false) or Play (true). */
    void setPlayMode (bool playMode);

    void showTab (TabIndex index);
    TabIndex getCurrentTab() const noexcept       { return currentTab; }

    void setBuild (BuildState state, const juce::String& status);
    void setExporting (bool exporting);
    void setParameters (const juce::Array<ParamControl>& params);
    void setLayout (const GuiLayout& layout);
    const GuiLayout& getLayout() const noexcept   { return pluginView.canvas.getLayout(); }

    /** The GUI's programmed elements from the latest build (nullptr: none). */
    void setElements (std::shared_ptr<WasmElements> elements)   { pluginView.canvas.setElements (std::move (elements)); }

    /** The presets row: names, the chosen one (-1: none), and which of A and B is on. */
    void setPresets (const juce::StringArray& names, int selected);
    void setAbSlot (int slot);

    /** What meters, lamps and scopes can watch (for the design panel's lists). */
    void setSources (const juce::StringArray& signals, const juce::StringArray& displays);

    SchematicView& getSchematic() noexcept        { return schematicView; }

    /** For Export: the GUI baked into images. */
    PluginCanvas::Bake bakeGui()                  { return pluginView.canvas.bake(); }

    std::function<void()> onBuildRequested;
    std::function<void()> onShowLogRequested;
    std::function<void()> onExportRequested;
    std::function<void()> onPanicRequested;   // the Panic button: every note off
    std::function<void()> onAutoLayoutRequested;
    std::function<void()> onLayoutEdited;
    std::function<void (int index, float value)> onParameterChanged;
    std::function<void (int note, float velocity)> onNote;   // a keyboard element's key: down (velocity 0..1) or up (0)
    std::function<bool (int note)> isNoteDown;               // whether a note is sounding, for keyboards to show
    std::function<void (bool playMode)> onModeChanged;   // the user picked Edit or Play
    std::function<void (int tabIndex)> onTabChanged;     // whichever way the tab changed

    /** An edit menu's instruction for Stella AI, about one element of the GUI (-1: the window). */
    std::function<void (int widgetIndex, const juce::String& instruction)> onAskAiAboutGui;

    std::function<void (int presetIndex)> onPresetChosen;
    std::function<void (const juce::String& why)> onElementsFailed;   // the GUI's programmed elements stopped
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
    class PluginView final : public juce::Component,
                             public juce::DragAndDropContainer
    {
    public:
        PluginView();

        void setProject (const juce::String& name, const juce::String& kindName);
        void setPlayMode (bool playMode);
        void setBuild (BuildState state, const juce::String& status);
        void setExporting (bool exporting);

        void setPresets (const juce::StringArray& names, int selected);
        void setAbSlot (int slot);

        std::function<void()> onBuild, onShowLog, onExport, onAutoLayout, onSavePreset, onAbCopy, onPanic;
        std::function<void (int)> onPresetChosen, onDeletePreset, onAbChosen;
        std::function<void (bool playMode)> onMode;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int barHeight = 48, rowHeight = 40;

        PluginCanvas canvas;
        Toolbox toolbox;

    private:
        void chooseMode (bool play);

        // Two tabs: Edit (reshape the panel) and Play (the panel locked, its controls live).
        juce::TextButton editTab { "Edit" }, playTab { "Play" };
        juce::TextButton rebuildButton { "Rebuild" }, logButton { "Log" }, exportButton { "Export" };
        juce::TextButton panicButton { "Panic" };   // every note off: stuck notes stop

        // The Edit tab's row: start over with an automatic panel.
        juce::TextButton autoButton { "Auto layout" };

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
    void showPages();

    Looks looks;   // before the canvas and the studio, which both use it
    PluginView pluginView;
    SchematicView schematicView;
    KnobStudio studio { looks };
    bool openingStudioForControl = false;
    juce::Component* aiPage = nullptr;
    TabIndex currentTab = aiTab;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Workspace)
};
