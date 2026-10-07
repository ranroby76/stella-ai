// C:\workspace\Stella AI Studio\src\MainComponent.cpp

#include "MainComponent.h"
#include "GuiLayout.h"
#include "StellaLookAndFeel.h"
#include "WasmCompiler.h"

namespace
{
    //==============================================================================
    class AudioSettingsWindow final : public juce::DocumentWindow
    {
    public:
        explicit AudioSettingsWindow (juce::AudioDeviceManager& deviceManager)
            : DocumentWindow ("Audio and MIDI settings", Theme::panel, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);

            auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager,
                                                                                  0, 2,      // audio inputs (for effects)
                                                                                  1, 2,      // audio outputs
                                                                                  true,      // MIDI inputs
                                                                                  false,     // MIDI output
                                                                                  true,      // stereo pairs
                                                                                  false);    // hide advanced options
            selector->setSize (560, 480);

            setContentOwned (selector.release(), true);
            setResizable (false, false);
        }

        void closeButtonPressed() override
        {
            setVisible (false);
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsWindow)
    };

    //==============================================================================
    /** The compiler's messages from the last build, in a window of their own. */
    class LogWindow final : public juce::DocumentWindow
    {
    public:
        LogWindow()
            : DocumentWindow ("Build log", Theme::panel, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);

            text.setMultiLine (true, false);
            text.setReadOnly (true);
            text.setScrollbarsShown (true);
            text.setFont (Theme::monoFont (14.0f));
            text.setSize (760, 420);

            setContentNonOwned (&text, true);
            setResizable (true, false);
        }

        void setLog (const juce::String& log)
        {
            text.setText (log.isNotEmpty() ? log : juce::String ("No messages: the last build was clean."), false);
        }

        void setTitle (const juce::String& title)
        {
            setName (title);
        }

        ~LogWindow() override
        {
            clearContentComponent();   // the editor is a member: let go of it before it goes
        }

        void closeButtonPressed() override    { setVisible (false); }

    private:
        juce::TextEditor text;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogWindow)
    };

    const juce::StringArray kindNames { "Instrument", "Audio effect", "MIDI effect" };
    const PluginKind kinds[] { PluginKind::instrument, PluginKind::effect, PluginKind::noteEffect };
}

//==============================================================================
MainComponent::MainComponent (Settings& s)
    : settings (s)
{
    project.onChanged = [this] { projectChanged(); };

    topBar.onNew = [this] { newProject(); };
    topBar.onOpen = [this] { openProject(); };
    keyboardStrip.onDeviceClicked = [this] { showAudioSettings(); };
    topBar.onModeChanged = [this] (bool play) { setPlayMode (play); };
    topBar.onOpenRecent = [this] (const juce::File& file) { openProjectFile (file); };
    topBar.onRevealProject = [this] { if (project.isOpen()) project.getProjectFile().revealToUser(); };
    topBar.onCloseProject = [this] { project.close(); };

    // Cloud projects: the open project saves itself; any project opens from the cloud.
    topBar.cloudStatus = [this] { return cloud.getStatusText(); };
    topBar.cloudAvailable = [this] { return server.isSignedIn() && server.isReady(); };
    topBar.onSaveToCloud = [this] { cloud.saveNow(); };
    topBar.onOpenFromCloud = [this] { openFromCloud(); };
    cloud.onConflict = [this] (const juce::String& updated) { cloudConflict (updated); };
    topBar.recentProjects = [this] { return settings.getRecentProjects(); };

    workspace.onNewRequested = [this] { newProject(); };
    workspace.onOpenRequested = [this] { openProject(); };

    engine.onDeviceChanged = [this] { updateDeviceSummary(); preview.deviceChanged(); };

    preview.onChanged = [this] { previewChanged(); };
    workspace.onBuildRequested = [this] { buildPlugin(); };
    workspace.onShowLogRequested = [this] { showBuildLog(); };
    workspace.onExportRequested = [this] { exportPlugin(); };
    workspace.onParameterChanged = [this] (int index, float value) { preview.setParameterValue (index, value); };
    workspace.onLayoutEdited = [this] { saveLayoutSoon(); };

    // The GUI's meters, lamps and scopes read the plugin live.
    workspace.readLevel = [this] (const juce::String& source, bool rms) { return preview.takeLevel (source, rms); };
    workspace.readScope = [this] (const juce::String& source, float* destination, int numSamples) { preview.readScope (source, destination, numSamples); };
    workspace.onSourcesChanged = [this] (const juce::StringArray& levels, const juce::StringArray& scopes) { preview.setGuiSources (levels, scopes); };
    workspace.onAutoLayoutRequested = [this] { autoLayout(); };
    projectTools.onLayoutChanged = [this] { loadLayout (false); };
    projectTools.onPresetsChanged = [this] { presets.load (project.getFolder()); pushPresets(); };
    projectTools.onCreateProject = [this] (const juce::String& name, PluginKind kind)
    {
        ProjectInfo info;
        info.name = name;
        info.vendor = settings.getVendor().isNotEmpty() ? settings.getVendor() : juce::String ("Fanan");
        info.kind = kind;

        const auto created = project.create (settings.getProjectsFolder(), info);

        if (created.wasOk())
            settings.addRecentProject (project.getProjectFile());

        return created;
    };

    // Undo: snapshots around each request to Stella AI, and after each edit.
    topBar.onUndo = [this] { undoOrRedo (false); };
    topBar.onRedo = [this] { undoOrRedo (true); };
    ai.onTurnStarted = [this] (const juce::String&) { recordHistory ("Changes"); };
    ai.onTurnFinished = [this] (const juce::String& request)
    {
        recordHistory ("Stella AI: " + (request.length() > 40 ? request.substring (0, 40) + juce::String::fromUTF8 ("\xe2\x80\xa6") : request));
    };

    // Presets and A/B.
    workspace.onPresetChosen = [this] (int index) { choosePreset (index); };
    workspace.onSavePreset = [this] { savePreset(); };
    workspace.onDeletePreset = [this] (int index) { deletePreset (index); };
    workspace.onAbChosen = [this] (int slot) { chooseAbSlot (slot); };
    workspace.onAbCopy = [this] { abValues[1 - abSlot] = currentValues(); };

    setWantsKeyboardFocus (true);

    // The schematic: bypass, probe, positions, and instructions for one block.
    auto& schematic = workspace.getSchematic();
    schematic.onBypass = [this] (const juce::String& id, bool off) { preview.setBypass (id, off); };
    schematic.onProbe = [this] (const juce::String& id, int port) { preview.setProbe (id, port); };
    schematic.readProbe = [this] (float* destination, int numSamples) { preview.readProbe (destination, numSamples); };
    schematic.onAskAi = [this] (const juce::String& id, const juce::String& type, const juce::String& instruction)
    {
        ai.send ("In the block \"" + id + "\" (" + type + ", modules/" + type + ".cpp): " + instruction);
    };
    schematic.onPositionsChanged = [this]
    {
        if (! project.isOpen())
            return;

        auto* object = new juce::DynamicObject();
        const juce::var file (object);
        object->setProperty ("positions", workspace.getSchematic().getPositions());
        project.getGuiFolder().getChildFile ("schematic.json").replaceWithText (juce::JSON::toString (file));
        recordHistory ("Moved blocks");
    };

    server.onStateChanged = [this] { connectionChanged(); };
    ai.onChanged = [this] { chat.setConversation (ai.getEntries(), ai.isBusy()); };

    chat.onSend = [this] (const juce::String& request, const juce::Array<juce::File>& files) { ai.send (request, files); };
    chat.onStop = [this] { ai.stop(); };
    chat.onRetry = [this] { server.checkNow(); };
    chat.onBuyCredits = [this] { buyCredits(); };
    chat.onSignIn = [this] (const juce::String& email) { signIn (false, email); };
    chat.onSignOut = [this] { signOut(); };

    // AI chat | divider | workspace
    columns.setItemLayout (0, ChatPanel::minimumWidth, 560, 340);
    columns.setItemLayout (1, 7, 7, 7);
    columns.setItemLayout (2, 420, -1.0, -0.75);

    addAndMakeVisible (topBar);
    addAndMakeVisible (chat);
    addAndMakeVisible (divider);
    addAndMakeVisible (workspace);
    addAndMakeVisible (keyboardStrip);

    projectChanged();
    updateDeviceSummary();
    connectionChanged();
    setSize (1360, 860);

    // Reopen the last project once the window is up, so any problem shows over it.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safeThis == nullptr)
            return;

        const auto recent = safeThis->settings.getRecentProjects();

        if (! recent.isEmpty() && ! safeThis->project.isOpen())
            safeThis->openProjectFile (juce::File (recent[0]));
    });
}

MainComponent::~MainComponent()
{
    project.onChanged = nullptr;
    engine.onDeviceChanged = nullptr;
    preview.onChanged = nullptr;
    logWindow = nullptr;
    server.onStateChanged = nullptr;
    ai.onChanged = nullptr;
    ai.onTurnStarted = nullptr;
    ai.onTurnFinished = nullptr;
    presetDialog = nullptr;
    audioSettingsWindow = nullptr;
    newProjectDialog = nullptr;
    signOutDialog = nullptr;
    autoLayoutDialog = nullptr;
    cloudDialog = nullptr;
    cloud.onConflict = nullptr;
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::window);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    topBar.setBounds (area.removeFromTop (TopBar::height));
    keyboardStrip.setBounds (area.removeFromBottom (KeyboardStrip::height));

    juce::Component* parts[] = { &chat, &divider, &workspace };
    columns.layOutComponents (parts, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);
}

void MainComponent::parentHierarchyChanged()
{
    updateWindowTitle();
}

//==============================================================================
void MainComponent::projectChanged()
{
    if (project.isOpen())
    {
        const auto& info = project.getInfo();

        topBar.setProjectName (info.name);
        workspace.setProject (info.name, Project::kindDisplayName (info.kind));
        settings.addRecentProject (project.getProjectFile());

        // A newly opened project starts playing at once (a new one gets a demo to play).
        if (info.uuid != builtProjectId)
        {
            builtProjectId = info.uuid;

            if (const auto starter = WasmCompiler::writeStarter (project.getFolder(), info.kind); starter.failed())
                showError ("Couldn't set up the plugin", starter.getErrorMessage());

            workspace.getSchematic().setPositions (juce::JSON::parse (project.getGuiFolder().getChildFile ("schematic.json"))
                                                       .getProperty ("positions", {}));
            loadLayout (false);
            cloud.projectChanged();

            history.reset (project.getFolder());
            presets.load (project.getFolder());
            currentPreset = -1;
            abValues[0].clear();
            abValues[1].clear();
            abSlot = 0;
            workspace.setAbSlot (0);
            pushPresets();
            updateUndoButtons();
            refreshSchematic();
            buildPlugin();
        }
    }
    else
    {
        topBar.setProjectName ({});
        workspace.setProject ({}, {});

        builtProjectId.clear();
        preview.unload();
        refreshSchematic();
        workspace.setLayout ({});
        cloud.projectChanged();

        history.reset ({});
        presets.presets.clear();
        currentPreset = -1;
        pushPresets();
        updateUndoButtons();
    }

    updateWindowTitle();
}

void MainComponent::buildPlugin()
{
    if (project.isOpen())
        preview.build (project.getFolder(), project.getInfo().uuid);
}

void MainComponent::previewChanged()
{
    const auto state = preview.getState();

    workspace.setBuild (state == LivePreview::State::building ? Workspace::BuildState::building
                      : state == LivePreview::State::playing  ? Workspace::BuildState::playing
                      : state == LivePreview::State::failed   ? Workspace::BuildState::failed
                                                              : Workspace::BuildState::idle,
                        preview.getStatus());

    // The knobs are rebuilt only when the plugin's parameter list changed.
    if (preview.getParametersVersion() != shownParamsVersion)
    {
        shownParamsVersion = preview.getParametersVersion();

        pushParameters();

        // What the GUI can watch: every module output, the plugin's own audio, and displays.
        juce::StringArray signals { "plugin.out L", "plugin.out R" }, displays;

        if (project.isOpen() && project.getInfo().kind == PluginKind::effect)
            signals.addArray ({ "plugin.in L", "plugin.in R" });

        for (const auto& m : preview.getModules())
            for (const auto& out : m.outputs)
                signals.add (m.id + "." + out);

        for (const auto& d : preview.getDisplays())
            displays.add (d.id);

        workspace.setSources (signals, displays);

        // A plugin's first build gets a plain automatic GUI, for Stella AI or the user to reshape.
        loadLayout (state == LivePreview::State::playing);
    }

    if (logWindow != nullptr)
        static_cast<LogWindow*> (logWindow.get())->setLog (preview.getLog());

    refreshSchematic();
}

void MainComponent::loadLayout (bool makeOneIfMissing)
{
    if (! project.isOpen())
        return;

    const auto file = project.getGuiFolder().getChildFile (GuiLayout::fileName);
    GuiLayout layout;

    if (GuiLayout::load (file, layout).wasOk())
    {
        workspace.setLayout (layout);
        return;
    }

    if (! makeOneIfMissing || preview.getParameters().isEmpty())
    {
        workspace.setLayout ({});
        return;
    }

    juce::Array<GuiLayout::ParamInfo> infos;

    for (const auto& p : preview.getParameters())
        infos.add ({ p.id, p.module, p.name, p.unit, p.min, p.max, p.def });

    layout = GuiLayout::makeDefault (infos, project.getInfo().name);
    layout.save (file);
    workspace.setLayout (layout);
    history.absorb();   // the studio made it, not the user: nothing to undo
}

void MainComponent::saveLayout()
{
    if (! project.isOpen())
        return;

    if (const auto saved = workspace.getLayout().save (project.getGuiFolder().getChildFile (GuiLayout::fileName)); saved.failed())
        showError ("Couldn't save the GUI", saved.getErrorMessage());
    else
        recordHistory ("Changed the GUI");
}

//==============================================================================
void MainComponent::pushParameters()
{
    juce::Array<Workspace::ParamControl> controls;

    for (const auto& p : preview.getParameters())
    {
        Workspace::ParamControl control;
        control.index = p.index;
        control.id = p.id;
        control.name = p.name;
        control.unit = p.unit;
        control.min = p.min;
        control.max = p.max;
        control.def = p.def;
        control.skew = p.skew;
        control.value = preview.getParameterValue (p.index);
        controls.add (control);
    }

    workspace.setParameters (controls);
}

std::map<juce::String, float> MainComponent::currentValues() const
{
    std::map<juce::String, float> values;

    for (const auto& p : preview.getParameters())
        values[p.id] = preview.getParameterValue (p.index);

    return values;
}

void MainComponent::applyValues (const std::map<juce::String, float>& values)
{
    for (const auto& p : preview.getParameters())
        if (const auto found = values.find (p.id); found != values.end())
            preview.setParameterValue (p.index, juce::jlimit (p.min, p.max, found->second));

    pushParameters();
}

void MainComponent::pushPresets()
{
    if (currentPreset >= (int) presets.presets.size())
        currentPreset = -1;

    workspace.setPresets (presets.getNames(), currentPreset);
}

void MainComponent::choosePreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.presets.size()))
        return;

    currentPreset = index;
    applyValues (presets.presets[(size_t) index].values);
    pushPresets();
}

void MainComponent::savePreset()
{
    if (! project.isOpen() || preview.getParameters().isEmpty())
    {
        showError ("Nothing to save yet", "Build the plugin first: a preset holds its parameter values.");
        return;
    }

    const auto suggested = juce::isPositiveAndBelow (currentPreset, (int) presets.presets.size())
                               ? presets.presets[(size_t) currentPreset].name
                               : "Preset " + juce::String ((int) presets.presets.size() + 1);

    presetDialog = std::make_unique<juce::AlertWindow> ("Save preset", "A name for the current values (the same name replaces that preset):",
                                                        juce::MessageBoxIconType::NoIcon, this);
    presetDialog->addTextEditor ("name", suggested);
    presetDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* editor = presetDialog->getTextEditor ("name"))
        editor->selectAll();

    presetDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis = juce::Component::SafePointer<MainComponent> (this)] (int result)
        {
            if (safeThis == nullptr || safeThis->presetDialog == nullptr)
                return;

            const auto name = safeThis->presetDialog->getTextEditorContents ("name").trim().substring (0, 60);
            juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->presetDialog = nullptr; });

            if (result != 1 || name.isEmpty() || ! safeThis->project.isOpen())
                return;

            Preset preset;
            preset.name = name;
            preset.values = safeThis->currentValues();
            safeThis->currentPreset = safeThis->presets.put (preset);

            if (const auto saved = safeThis->presets.save (safeThis->project.getFolder()); saved.failed())
                safeThis->showError ("Couldn't save the preset", saved.getErrorMessage());

            safeThis->pushPresets();
            safeThis->recordHistory ("Saved the preset " + name);
        }), false);
}

void MainComponent::deletePreset (int index)
{
    if (! project.isOpen() || ! juce::isPositiveAndBelow (index, (int) presets.presets.size()))
        return;

    const auto name = presets.presets[(size_t) index].name;
    presets.presets.erase (presets.presets.begin() + index);
    presets.save (project.getFolder());
    currentPreset = -1;
    pushPresets();
    recordHistory ("Deleted the preset " + name);
}

void MainComponent::chooseAbSlot (int slot)
{
    if (slot == abSlot)
        return;

    // What's playing now belongs to the side being left; the other side comes back as it was.
    abValues[abSlot] = currentValues();
    abSlot = slot;

    if (! abValues[slot].empty())
        applyValues (abValues[slot]);

    workspace.setAbSlot (slot);
}

//==============================================================================
void MainComponent::recordHistory (const juce::String& label)
{
    if (project.isOpen() && history.record (label))
        updateUndoButtons();
}

void MainComponent::updateUndoButtons()
{
    topBar.setUndoState (history.getUndoLabel(), history.getRedoLabel());
}

void MainComponent::undoOrRedo (bool redo)
{
    if (! project.isOpen() || ai.isBusy())
        return;   // never under Stella AI's feet

    const auto changed = redo ? history.redo() : history.undo();
    updateUndoButtons();

    if (! changed.any())
        return;

    if (changed.layout)
        loadLayout (false);

    if (changed.schematic)
        workspace.getSchematic().setPositions (juce::JSON::parse (project.getGuiFolder().getChildFile ("schematic.json")).getProperty ("positions", {}));

    if (changed.presets)
    {
        presets.load (project.getFolder());
        pushPresets();
    }

    if (changed.code)
        buildPlugin();
    else
        refreshSchematic();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Text boxes keep their own undo.
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;

    const auto mods = key.getModifiers();

    if (mods.isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        undoOrRedo (mods.isShiftDown());
        return true;
    }

    if (mods.isCommandDown() && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y'))
    {
        undoOrRedo (true);
        return true;
    }

    return false;
}

void MainComponent::saveLayoutSoon()
{
    // Knob Studio edits arrive many times a second: the file is written once they pause.
    if (layoutSaveScheduled)
        return;

    layoutSaveScheduled = true;

    juce::Timer::callAfterDelay (400, [safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safeThis != nullptr)
        {
            safeThis->layoutSaveScheduled = false;
            safeThis->saveLayout();
        }
    });
}

void MainComponent::autoLayout()
{
    if (! project.isOpen() || preview.getParameters().isEmpty())
    {
        showError ("Nothing to lay out yet", "Build the plugin first: the automatic GUI is made from its parameters.");
        return;
    }

    autoLayoutDialog = std::make_unique<juce::AlertWindow> ("Replace the GUI?",
                                                            "This replaces the current GUI with a plain automatic one: a group per module, "
                                                            "a control per parameter.",
                                                            juce::MessageBoxIconType::NoIcon, this);
    autoLayoutDialog->addButton ("Replace", 1, juce::KeyPress (juce::KeyPress::returnKey));
    autoLayoutDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    autoLayoutDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis = juce::Component::SafePointer<MainComponent> (this)] (int result)
        {
            if (safeThis == nullptr || safeThis->autoLayoutDialog == nullptr)
                return;

            safeThis->autoLayoutDialog->setVisible (false);

            if (result == 1)
            {
                safeThis->project.getGuiFolder().getChildFile (GuiLayout::fileName).deleteFile();
                safeThis->loadLayout (true);
            }

            juce::MessageManager::callAsync ([safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->autoLayoutDialog = nullptr;
            });
        }), false);
}

void MainComponent::refreshSchematic()
{
    auto& schematic = workspace.getSchematic();

    if (! project.isOpen())
    {
        schematic.setGraph (false, false, {}, {});
        return;
    }

    // Blocks and wires from graph.json; ports from the plugin as built. If it hasn't built,
    // the ports are worked out from the wires.
    const auto graph = juce::JSON::parse (project.getFolder().getChildFile (WasmCompiler::graphFileName));

    juce::Array<SchematicView::Block> blocks;
    juce::Array<SchematicView::Wire> wires;

    if (const auto* list = graph.getProperty ("wires", {}).getArray())
    {
        for (const auto& item : *list)
        {
            const auto from = item.getProperty ("from", {}).toString();
            const auto to = item.getProperty ("to", {}).toString();

            if (from.containsChar ('.') && to.containsChar ('.'))
                wires.add ({ from.upToFirstOccurrenceOf (".", false, false), from.fromFirstOccurrenceOf (".", false, false),
                             to.upToFirstOccurrenceOf (".", false, false), to.fromFirstOccurrenceOf (".", false, false) });
        }
    }

    if (const auto* list = graph.getProperty ("modules", {}).getArray())
    {
        for (const auto& item : *list)
        {
            SchematicView::Block block;
            block.id = item.getProperty ("id", {}).toString();
            block.type = item.getProperty ("type", {}).toString();
            block.bypassed = preview.isBypassed (block.id);

            bool known = false;

            for (const auto& m : preview.getModules())
            {
                if (m.id == block.id && m.type == block.type)
                {
                    block.name = m.name;
                    block.inputs = m.inputs;
                    block.outputs = m.outputs;
                    known = true;
                }
            }

            if (! known)
            {
                for (const auto& w : wires)
                {
                    if (w.toModule == block.id)   block.inputs.addIfNotAlreadyThere (w.toPort);
                    if (w.fromModule == block.id) block.outputs.addIfNotAlreadyThere (w.fromPort);
                }
            }

            blocks.add (block);
        }
    }

    schematic.setGraph (true, project.getInfo().kind == PluginKind::effect, blocks, wires);
}

void MainComponent::showBuildLog()
{
    showLog ("Build log", preview.getLog());
}

void MainComponent::showLog (const juce::String& title, const juce::String& text)
{
    if (logWindow == nullptr)
        logWindow = std::make_unique<LogWindow>();

    auto* window = static_cast<LogWindow*> (logWindow.get());
    window->setTitle (title);
    window->setLog (text);
    logWindow->centreAroundComponent (this, logWindow->getWidth(), logWindow->getHeight());
    logWindow->setVisible (true);
    logWindow->toFront (true);
}

//==============================================================================
void MainComponent::openFromCloud()
{
    cloud.list ([safeThis = juce::Component::SafePointer<MainComponent> (this)] (const juce::Result& result, const juce::Array<CloudProjects::Entry>& entries)
    {
        if (safeThis == nullptr)
            return;

        if (result.failed())
        {
            safeThis->showError ("Couldn't reach the cloud", result.getErrorMessage());
            return;
        }

        if (entries.isEmpty())
        {
            safeThis->showError ("Nothing in the cloud yet", "Projects go to the cloud by themselves while you're signed in. Open one on this computer first.");
            return;
        }

        juce::PopupMenu menu;
        menu.addSectionHeader ("Your projects in the cloud");

        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries.getReference (i);
            const auto when = juce::Time::fromISO8601 (e.updated);
            menu.addItem (i + 1, e.name + juce::String::fromUTF8 ("  \xc2\xb7  ")
                                     + Project::kindDisplayName (Project::kindFromString (e.kind))
                                     + juce::String::fromUTF8 ("  \xc2\xb7  ") + when.toString (true, true, false, true));
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&safeThis->topBar),
                            [safeThis, entries] (int choice)
                            {
                                if (safeThis == nullptr || choice <= 0 || choice > entries.size())
                                    return;

                                const auto& entry = entries.getReference (choice - 1);
                                const auto projects = safeThis->settings.getProjectsFolder();
                                const auto legal = juce::File::createLegalFileName (entry.name);

                                // The same project on this computer: replace it (after asking). A different
                                // project with that name: a folder of its own.
                                auto folder = projects.getChildFile (legal);

                                for (int n = 2; folder.isDirectory(); ++n)
                                {
                                    const auto info = juce::JSON::parse (folder.getChildFile ("project.stella"));

                                    if (info.getProperty ("uuid", {}).toString() == entry.uuid || ! folder.getChildFile ("project.stella").existsAsFile())
                                        break;

                                    folder = projects.getChildFile (legal + " " + juce::String (n));
                                }

                                if (! folder.getChildFile ("project.stella").existsAsFile())
                                {
                                    safeThis->downloadFromCloud (entry, folder);
                                    return;
                                }

                                safeThis->cloudDialog = std::make_unique<juce::AlertWindow> ("Replace this computer's copy?",
                                    "\"" + entry.name + "\" is on this computer too. Replace it with the cloud copy?",
                                    juce::MessageBoxIconType::NoIcon, safeThis.getComponent());
                                safeThis->cloudDialog->addButton ("Replace", 1, juce::KeyPress (juce::KeyPress::returnKey));
                                safeThis->cloudDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                                safeThis->cloudDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, entry, folder] (int answer)
                                {
                                    if (safeThis == nullptr)
                                        return;

                                    juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->cloudDialog = nullptr; });

                                    if (answer == 1)
                                        safeThis->downloadFromCloud (entry, folder);
                                }), false);
                            });
    });
}

void MainComponent::downloadFromCloud (const CloudProjects::Entry& entry, const juce::File& folder)
{
    cloud.download (entry.uuid, folder, [safeThis = juce::Component::SafePointer<MainComponent> (this), folder] (const juce::Result& result)
    {
        if (safeThis == nullptr)
            return;

        if (result.failed())
        {
            safeThis->showError ("Couldn't open it from the cloud", result.getErrorMessage());
            return;
        }

        const bool same = safeThis->project.isOpen() && safeThis->project.getFolder() == folder;
        safeThis->openProjectFile (folder.getChildFile ("project.stella"));

        if (same)
        {
            // The same project with new files: rebuild what shows them.
            safeThis->loadLayout (false);
            safeThis->refreshSchematic();
            safeThis->buildPlugin();
        }

        safeThis->cloud.projectChanged();
    });
}

void MainComponent::cloudConflict (const juce::String& cloudUpdated)
{
    const auto when = juce::Time::fromISO8601 (cloudUpdated);

    cloudDialog = std::make_unique<juce::AlertWindow> ("Changed on another computer",
        "This project was saved to the cloud from another computer"
            + (cloudUpdated.isNotEmpty() ? " (" + when.toString (true, true, false, true) + ")" : juce::String())
            + " since this computer last saved it. Which copy do you want to keep?",
        juce::MessageBoxIconType::NoIcon, this);
    cloudDialog->addButton ("Keep this computer's copy", 1);
    cloudDialog->addButton ("Use the cloud copy", 2);
    cloudDialog->addButton ("Decide later", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    cloudDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis = juce::Component::SafePointer<MainComponent> (this)] (int answer)
        {
            if (safeThis == nullptr)
                return;

            juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->cloudDialog = nullptr; });

            if (! safeThis->project.isOpen())
                return;

            if (answer == 1)
            {
                safeThis->cloud.saveNow (true);
            }
            else if (answer == 2)
            {
                CloudProjects::Entry entry;
                entry.uuid = safeThis->project.getInfo().uuid;
                entry.name = safeThis->project.getInfo().name;
                safeThis->downloadFromCloud (entry, safeThis->project.getFolder());
            }
        }), false);
}

//==============================================================================
void MainComponent::exportPlugin()
{
    if (! project.isOpen() || exporting)
        return;

    exporting = true;
    workspace.setExporting (true);

    const auto folder = project.getFolder();
    const auto info = project.getInfo();
    const auto destination = PluginExporter::defaultExportFolder (info.name);

    // The GUI is baked here, on the message thread; the compiling happens in the background.
    std::map<juce::String, juce::String> units;

    for (const auto& p : preview.getParameters())
        units[p.id] = p.unit;

    juce::StringArray displays;

    for (const auto& d : preview.getDisplays())
        displays.add (d.id);

    const auto gui = std::make_shared<PluginExporter::Gui> (PluginExporter::prepareGui (workspace.getLayout(), workspace.bakeGui(), units, displays,
                                                                                         presets.presets));

    juce::Thread::launch ([safeThis = juce::Component::SafePointer<MainComponent> (this), folder, info, destination, gui]
    {
        const auto result = PluginExporter::exportPlugins (folder, info, destination, *gui);

        juce::MessageManager::callAsync ([safeThis, result]
        {
            if (safeThis == nullptr)
                return;

            safeThis->exporting = false;
            safeThis->workspace.setExporting (false);
            safeThis->exportFinished (result);
        });
    });
}

void MainComponent::exportFinished (const PluginExporter::Result& result)
{
    if (! result.ok)
    {
        showLog ("Export log", "The export didn't work.\n\n" + result.log);
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withTitle ("Exported")
                       .withMessage (result.plugin.getFileName() + " and " + result.vst3.getFileName() + " are ready (Windows 64-bit), built in "
                                     + juce::String (result.seconds, 1) + " s:\n" + result.plugin.getParentDirectory().getFullPathName()
                                     + "\n\nCopy them into the folders your DAW scans, then rescan plugins:\n"
                                       "CLAP: %LOCALAPPDATA%\\Programs\\Common\\CLAP or C:\\Program Files\\Common Files\\CLAP\n"
                                       "VST3 (the whole folder): %LOCALAPPDATA%\\Programs\\Common\\VST3 or C:\\Program Files\\Common Files\\VST3\n"
                                       "The folders under %LOCALAPPDATA% need no admin rights.")
                       .withButton ("Show in folder")
                       .withButton ("OK")
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [file = result.plugin] (int button)
    {
        if (button == 1)
            file.revealToUser();
    });
}

void MainComponent::connectionChanged()
{
    if (server.isSignedIn())
        waitingForSignIn = false;

    ChatPanel::Account account;
    account.status = server.getStatus();
    account.siteAvailable = server.hasSite();
    account.signedIn = server.isSignedIn();
    account.waitingForBrowser = waitingForSignIn;
    account.credits = server.getCredits();
    account.email = server.getEmail();
    account.computerCode = server.getComputerCode();
    account.lastEmail = server.getLastEmail();

    chat.setAccount (account);
}

void MainComponent::buyCredits()
{
    if (! server.hasSite())
    {
        showError ("The store isn't open yet", "Buying Stella AI credits isn't set up in this build yet.");
        return;
    }

    // Credits belong to an account, so they work on every computer signed in to it:
    // a computer that isn't signed in signs in first, and the site goes on to the store.
    if (! server.isSignedIn())
    {
        signIn (true, server.getLastEmail());
        return;
    }

    server.getStoreUrl().launchInDefaultBrowser();
    server.watchForChanges();   // the new credits show up here within seconds of paying
}

void MainComponent::signIn (bool thenBuy, const juce::String& emailHint)
{
    if (! server.isReady())
    {
        showError ("Not connected", "Signing in needs Fanan's server. Check the internet connection and try again.");
        server.checkNow();
        return;
    }

    server.signIn (thenBuy, emailHint, [safeThis = juce::Component::SafePointer<MainComponent> (this)] (const juce::Result& result, const juce::var&)
    {
        if (safeThis == nullptr)
            return;

        if (result.failed())
        {
            safeThis->showError ("Couldn't start signing in", result.getErrorMessage());
            return;
        }

        safeThis->waitingForSignIn = true;
        safeThis->connectionChanged();

        // If the browser is abandoned, the box goes back to normal after ten minutes.
        const auto attempt = ++safeThis->signInAttempt;

        juce::Timer::callAfterDelay (10 * 60 * 1000, [safeThis, attempt]
        {
            if (safeThis != nullptr && safeThis->signInAttempt == attempt && safeThis->waitingForSignIn)
            {
                safeThis->waitingForSignIn = false;
                safeThis->connectionChanged();
            }
        });
    });
}

void MainComponent::signOut()
{
    signOutDialog = std::make_unique<juce::AlertWindow> ("Sign out?",
                                                         "This computer stops using " + server.getEmail()
                                                             + ". Your credits stay with your account, and signing in again brings them back.",
                                                         juce::MessageBoxIconType::NoIcon, this);

    signOutDialog->addButton ("Sign out", 1, juce::KeyPress (juce::KeyPress::returnKey));
    signOutDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    signOutDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis = juce::Component::SafePointer<MainComponent> (this)] (int result)
        {
            if (safeThis == nullptr || safeThis->signOutDialog == nullptr)
                return;

            safeThis->signOutDialog->setVisible (false);

            if (result == 1)
            {
                safeThis->server.signOut ([safeThis] (const juce::Result& outcome, const juce::var&)
                {
                    if (safeThis != nullptr && outcome.failed())
                        safeThis->showError ("Couldn't sign out", outcome.getErrorMessage());
                });
            }

            // Let the modal machinery finish with the dialog before it goes.
            juce::MessageManager::callAsync ([safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->signOutDialog = nullptr;
            });
        }), false);
}

void MainComponent::updateWindowTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (project.isOpen() ? project.getInfo().name + juce::String::fromUTF8 (" \xe2\x80\x94 Stella AI Studio")
                                          : juce::String ("Stella AI Studio"));
}

void MainComponent::updateDeviceSummary()
{
    keyboardStrip.setDevice (engine.describeDeviceLines(), engine.describeDevice());
}

void MainComponent::setPlayMode (bool shouldPlay)
{
    playMode = shouldPlay;
    topBar.setPlayMode (playMode);
    workspace.setPlayMode (playMode);
}

//==============================================================================
void MainComponent::newProject()
{
    newProjectDialog = std::make_unique<juce::AlertWindow> ("New plugin",
                                                            "What are you building? You can change all of this later.",
                                                            juce::MessageBoxIconType::NoIcon, this);

    newProjectDialog->addTextEditor ("name", "My Plugin", "Name");
    newProjectDialog->addComboBox ("kind", kindNames, "Type");
    newProjectDialog->addTextEditor ("vendor", settings.getVendor(), "Company");
    newProjectDialog->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    newProjectDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* combo = newProjectDialog->getComboBoxComponent ("kind"))
        combo->setSelectedItemIndex (0, juce::dontSendNotification);

    newProjectDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis = juce::Component::SafePointer<MainComponent> (this)] (int result)
        {
            if (safeThis == nullptr || safeThis->newProjectDialog == nullptr)
                return;

            auto& dialog = *safeThis->newProjectDialog;
            dialog.setVisible (false);

            if (result == 1)
            {
                ProjectInfo info;
                info.name = dialog.getTextEditorContents ("name").trim();
                info.vendor = dialog.getTextEditorContents ("vendor").trim();

                if (auto* combo = dialog.getComboBoxComponent ("kind"))
                    info.kind = kinds[juce::jlimit (0, 2, combo->getSelectedItemIndex())];

                const auto created = safeThis->project.create (safeThis->settings.getProjectsFolder(), info);

                if (created.failed())
                    safeThis->showError ("Couldn't create the plugin", created.getErrorMessage());
                else
                    safeThis->settings.setVendor (info.vendor);
            }

            // Let the modal machinery finish with the dialog before it goes.
            juce::MessageManager::callAsync ([safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->newProjectDialog = nullptr;
            });
        }), false);

    // Typing goes straight into the name, ready to be replaced. Done once the window is
    // on screen: a window that isn't showing yet can't take the keyboard.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safeThis == nullptr || safeThis->newProjectDialog == nullptr)
            return;

        if (auto* nameEditor = safeThis->newProjectDialog->getTextEditor ("name"))
        {
            nameEditor->grabKeyboardFocus();
            nameEditor->selectAll();
        }
    });
}

void MainComponent::openProject()
{
    const auto start = project.isOpen() ? project.getFolder().getParentDirectory() : settings.getProjectsFolder();

    chooser = std::make_unique<juce::FileChooser> ("Open a Stella project", start.isDirectory() ? start : juce::File(),
                                                   "*.stella");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (safeThis != nullptr && file != juce::File())
                                  safeThis->openProjectFile (file);
                          });
}

void MainComponent::openProjectFile (const juce::File& file)
{
    const auto result = project.open (file);

    if (result.failed())
    {
        settings.removeRecentProject (file);
        showError ("Couldn't open the project", result.getErrorMessage());
    }
}

//==============================================================================
void MainComponent::showAudioSettings()
{
    if (audioSettingsWindow == nullptr)
        audioSettingsWindow = std::make_unique<AudioSettingsWindow> (engine.getDeviceManager());

    audioSettingsWindow->centreAroundComponent (this, audioSettingsWindow->getWidth(), audioSettingsWindow->getHeight());
    audioSettingsWindow->setVisible (true);
    audioSettingsWindow->toFront (true);
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message, "OK", this);
}
