// C:\workspace\Stella AI Studio\src\MainComponent.h

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "AudioEngine.h"
#include "ChatPanel.h"
#include "FananServer.h"
#include "LivePreview.h"
#include "Project.h"
#include "CloudProjects.h"
#include "History.h"
#include "PluginExporter.h"
#include "PresetBank.h"
#include "ProjectTools.h"
#include "Settings.h"
#include "StellaAi.h"
#include "TopBar.h"
#include "Workspace.h"

//==============================================================================
/**
    The studio window's content:

        top bar (Options menu, device details, the four tabs, audio load, Audio / MIDI)
        the open tab (Build with AI / Edit UI / Schematic / Knob Studio)

    Owns the audio engine, the link to Fanan's server (Stella AI and credits) and the
    open project.
*/
class MainComponent final : public juce::Component
{
public:
    explicit MainComponent (Settings& settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    void newProject();
    void openProject();
    void openProjectFile (const juce::File& file);

private:
    void projectChanged();
    void connectionChanged();
    void previewChanged();
    void refreshSchematic();
    void loadLayout (bool makeOneIfMissing);
    void saveLayout();
    void saveLayoutSoon();
    void autoLayout();
    void buildPlugin();
    void showBuildLog();
    void showLog (const juce::String& title, const juce::String& text);
    void exportPlugin();
    void openFromCloud();

    // Undo, presets, A/B
    bool keyPressed (const juce::KeyPress& key) override;
    void undoOrRedo (bool redo);
    void recordHistory (const juce::String& label);
    void pushParameters();
    void pushPresets();
    std::map<juce::String, float> currentValues() const;
    void applyValues (const std::map<juce::String, float>& values);
    void choosePreset (int index);
    void savePreset();
    void deletePreset (int index);
    void chooseAbSlot (int slot);
    void cloudConflict (const juce::String& cloudUpdated);
    void downloadFromCloud (const CloudProjects::Entry& entry, const juce::File& folder);
    void exportFinished (const PluginExporter::Result& result);
    void buyCredits();
    void signIn (bool thenBuy, const juce::String& emailHint);
    void signOut();
    void setPlayMode (bool shouldPlay);
    void showAudioSettings();
    void showError (const juce::String& title, const juce::String& message);
    void updateWindowTitle();
    void updateDeviceSummary();

    Settings& settings;
    AudioEngine engine;
    LivePreview preview { engine };
    FananServer server { FananServer::loadSettings() };
    Project project;
    ProjectTools projectTools { project, preview };
    CloudProjects cloud { server, project };
    StellaAi ai { server, projectTools };

    TopBar topBar;
    ChatPanel chat;
    Workspace workspace;


    std::unique_ptr<juce::DocumentWindow> audioSettingsWindow, logWindow;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> newProjectDialog, signOutDialog, autoLayoutDialog, cloudDialog, presetDialog;

    History history;
    PresetBank presets;
    int currentPreset = -1;
    std::map<juce::String, float> abValues[2];
    int abSlot = 0;
    juce::TooltipWindow tooltips { this, 600 };

    bool playMode = false;
    bool waitingForSignIn = false;   // sent to the browser to sign in, not back yet
    int signInAttempt = 0;           // a wait ends by itself after a while
    juce::String builtProjectId;     // the project the preview was built for
    int shownParamsVersion = -1;
    bool layoutSaveScheduled = false;
    bool exporting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
