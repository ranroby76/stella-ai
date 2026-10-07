// C:\workspace\Stella AI Studio\src\TopBar.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

//==============================================================================
/**
    The strip across the top: the File menu, Undo and Redo on the left; the studio's four
    tabs in the middle (Build with AI, Edit UI, Schematic, Knob Studio); the Fanan logo on
    the right. The open project's name is in the window's title bar, as in Windows apps.
*/
class TopBar final : public juce::Component
{
public:
    TopBar();

    void setProjectName (const juce::String& name);   // empty: no project open

    /** Which of the four tabs is showing, in the order of tabNames. */
    void setCurrentTab (int index);

    static constexpr int numTabs = 4;
    static constexpr const char* tabNames[numTabs] { "Build with AI", "Edit UI", "Schematic", "Knob Studio" };

    std::function<void (int index)> onTabChosen;

    std::function<void()> onNew;
    std::function<void()> onOpen;
    std::function<void()> onRevealProject;
    std::function<void()> onCloseProject;
    std::function<void (const juce::File& projectFile)> onOpenRecent;

    /** What Undo and Redo would do ("Stella AI: add a chorus"), or empty when they can't. */
    void setUndoState (const juce::String& undoLabel, const juce::String& redoLabel);

    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void()> onSaveToCloud;
    std::function<void()> onOpenFromCloud;
    std::function<juce::String()> cloudStatus;      // shown beside "Save to the cloud now"
    std::function<bool()> cloudAvailable;           // signed in

    /** Asked for each time the File menu opens: recent project files, newest first. */
    std::function<juce::StringArray()> recentProjects;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int height = 46;

private:
    /** "File", in plain text like a Windows menu bar: it lights up under the mouse and
        while its menu is open. */
    class MenuTitle final : public juce::Button
    {
    public:
        explicit MenuTitle (const juce::String& title) : juce::Button (title) {}
        void setOpen (bool shouldBeOpen)    { open = shouldBeOpen; repaint(); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        bool open = false;
    };

    /** One of the main tabs: its name, underlined in the accent while it's showing. */
    class TabButton final : public juce::Button
    {
    public:
        explicit TabButton (const juce::String& name) : juce::Button (name) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    void showFileMenu();

    MenuTitle fileMenu { "File" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::OwnedArray<TabButton> tabs;

    bool hasProject = false;
    juce::Rectangle<int> logoArea, fananArea;
    juce::Image fananLogo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
