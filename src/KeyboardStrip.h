// C:\workspace\Stella AI Studio\src\KeyboardStrip.h

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>

class AudioEngine;

//==============================================================================
/**
    The bottom strip: the test tone and All off, the output meter, the audio load and the
    audio device. Its on-screen keyboard (mouse or computer keys) is hidden for now: it
    stays in the code, ready to come back elsewhere (keyboardShown).
*/
class KeyboardStrip final : public juce::Component,
                            private juce::Timer
{
public:
    explicit KeyboardStrip (AudioEngine& engine);
    ~KeyboardStrip() override;

    /** The audio device in three lines: name, sample rate, buffer. */
    void setDevice (const juce::StringArray& lines, const juce::String& tooltip);

    /** The device button was pressed: open the audio settings. */
    std::function<void()> onDeviceClicked;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int height = 104;

    /** The on-screen keyboard is off for now (2026-10-08); it comes back somewhere else later. */
    static constexpr bool keyboardShown = false;

private:
    //==============================================================================
    class Meter final : public juce::Component
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

    AudioEngine& engine;
    /** The on-screen keyboard plays from the computer keyboard too, but leaves shortcuts
        (Ctrl+Z, Ctrl+Y...) to the studio. */
    class Keyboard final : public juce::MidiKeyboardComponent
    {
    public:
        using MidiKeyboardComponent::MidiKeyboardComponent;

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
                return false;

            return MidiKeyboardComponent::keyPressed (key);
        }
    };

    Keyboard keyboard;
    juce::TextButton testToneButton { "Test tone" }, panicButton { "All off" };

    /** A rectangle with three lines of text: the device, its sample rate and its buffer. */
    class DeviceButton final : public juce::Button
    {
    public:
        DeviceButton() : juce::Button ("Audio device") {}
        void setLines (const juce::StringArray& newLines)   { lines = newLines; repaint(); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::StringArray lines;
    };

    DeviceButton deviceButton;

    static constexpr int rightWidth = 196;   // the meter, the audio load and the device button
    static constexpr int lowestKey = 21;     // A0: the piano's 88 keys, A0 to C8
    Meter meter;
    juce::String loadText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardStrip)
};
