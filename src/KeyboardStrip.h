// C:\workspace\Stella AI Studio\src\KeyboardStrip.h

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

class AudioEngine;

//==============================================================================
/**
    The on-screen keyboard: the piano's 88 keys spread over its width, played with the
    mouse or the computer keys (shortcuts such as Ctrl+Z stay with the studio).

    Not shown anywhere for now (2026-10-08): it comes back where Rob decides.
*/
class KeyboardStrip final : public juce::Component
{
public:
    explicit KeyboardStrip (AudioEngine& engine);

    void resized() override;

private:
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

    static constexpr int lowestKey = 21;     // A0: the piano's 88 keys, A0 to C8

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardStrip)
};
