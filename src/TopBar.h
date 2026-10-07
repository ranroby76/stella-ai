// C:\workspace\Stella AI Studio\src\TopBar.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

//==============================================================================
/**
    The strip across the top: the project menu (New, Open, the cloud, recent projects),
    Undo and Redo, the Design / Play switch, and the Fanan logo on the right. The audio
    device lives in the bottom strip.
*/
class TopBar final : public juce::Component
{
public:
    TopBar();

    void setProjectName (const juce::String& name);   // empty: no project open
    void setPlayMode (bool playMode);

    std::function<void()> onNew;
    std::function<void()> onOpen;
    std::function<void()> onRevealProject;
    std::function<void()> onCloseProject;
    std::function<void (bool playMode)> onModeChanged;
    std::function<void (const juce::File& projectFile)> onOpenRecent;
    /** What Undo and Redo would do ("Stella AI: add a chorus"), or empty when they can't. */
    void setUndoState (const juce::String& undoLabel, const juce::String& redoLabel);

    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void()> onSaveToCloud;
    std::function<void()> onOpenFromCloud;
    std::function<juce::String()> cloudStatus;      // shown at the top of the project menu
    std::function<bool()> cloudAvailable;           // signed in

    /** Asked for each time the project menu opens: recent project files, newest first. */
    std::function<juce::StringArray()> recentProjects;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int height = 46;

private:
    void showProjectMenu();

    juce::TextButton projectButton;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton designButton { "Design" }, playButton { "Play" };

    bool hasProject = false;
    juce::Rectangle<int> logoArea, fananArea;
    juce::Image fananLogo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
