// C:\workspace\Stella AI Studio\src\TopBar.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

//==============================================================================
/**
    The strip across the top.

        left    the Options menu, and the audio device's details in a pill (a click opens
                the audio and MIDI settings)
        middle  the studio's four tabs: Build with AI, Edit UI, Schematic, Knob Studio
        right   the output meter with the audio load, and the Fanan logo

    The open project's name is in the window's title bar, as in Windows apps.
*/
class TopBar final : public juce::Component,
                     private juce::Timer
{
public:
    TopBar();
    ~TopBar() override;

    void setProjectName (const juce::String& name);   // empty: no project open

    /** Which of the four tabs is showing, in the order of tabNames. */
    void setCurrentTab (int index);

    static constexpr int numTabs = 4;
    static constexpr const char* tabNames[numTabs] { "Build with AI", "Edit UI", "Schematic", "Knob Studio" };

    std::function<void (int index)> onTabChosen;

    /** The audio device in short pieces (name, sample rate, buffer) for the pill, and a
        longer description for its tooltip. */
    void setDevice (const juce::StringArray& details, const juce::String& tooltip);

    std::function<void()> onAudioSettings;

    /** Read 30 times a second: a channel's peak since the last call, and the audio load (0..1). */
    std::function<float (int channel)> takePeak;
    std::function<double()> audioLoad;

    std::function<void()> onNew;
    std::function<void()> onOpen;
    std::function<void()> onRevealProject;
    std::function<void()> onCloseProject;
    std::function<void (const juce::File& projectFile)> onOpenRecent;

    /** Asked for each time the Options menu opens: recent project files, newest first. */
    std::function<juce::StringArray()> recentProjects;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int height = 46;

private:
    /** "Options", in plain text like a Windows menu bar: it lights up under the mouse and
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

    /** The audio device's details, in a rounded frame. A click opens the audio settings.
        When it's short of room it shows as many whole pieces as fit (name, then sample rate,
        then buffer); the tooltip always has everything. */
    class DevicePill final : public juce::Button
    {
    public:
        DevicePill() : juce::Button ("Audio device") {}
        void setPieces (const juce::StringArray& newPieces)    { pieces = newPieces; repaint(); }

        /** How wide the pill wants to be with at most this much room: just wide enough for
            the whole pieces that fit. */
        int getWidthFor (int room) const;

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::String textFor (int room) const;

        juce::StringArray pieces;
    };

    /** Left and right output levels: green to -6 dB, amber to 0 dB, red above. */
    class Meter final : public juce::Component,
                        public juce::SettableTooltipClient
    {
    public:
        void setLevels (float left, float right);
        void paint (juce::Graphics&) override;

    private:
        static float toPosition (float gain) noexcept;

        float levels[2] {}, holds[2] {};
        int holdFrames[2] {};
    };

    void timerCallback() override;
    void showOptionsMenu();

    MenuTitle optionsMenu { "Options" };
    DevicePill devicePill;
    juce::OwnedArray<TabButton> tabs;
    Meter meter;
    juce::String loadText { "0%" };

    bool hasProject = false;
    juce::Rectangle<int> wordmarkArea, fananArea, loadArea;   // empty when the window is too narrow for them
    juce::Image fananLogo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
