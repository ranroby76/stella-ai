// C:\workspace\Stella AI Studio\src\KeyboardStrip.cpp

#include "KeyboardStrip.h"
#include "AudioEngine.h"
#include "StellaLookAndFeel.h"

//==============================================================================
KeyboardStrip::KeyboardStrip (AudioEngine& e)
    : keyboard (e.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    keyboard.setAvailableRange (21, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyWidth (22.0f);
    keyboard.setScrollButtonsVisible (true);
    keyboard.setKeyPressBaseOctave (4);
    keyboard.setWantsKeyboardFocus (true);

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffdcdcdf));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff1c1c20));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff8a8a92));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::accent.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.4f));
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, Theme::raised);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, Theme::muted);

    addAndMakeVisible (keyboard);
}

void KeyboardStrip::resized()
{
    const auto area = getLocalBounds();
    keyboard.setBounds (area);

    // The piano's 88 keys spread over the whole width; they scroll only if they'd be too narrow.
    constexpr int whiteKeys = 52;
    const auto keyWidth = (float) area.getWidth() / (float) whiteKeys;
    const bool fits = keyWidth >= 9.0f;

    keyboard.setScrollButtonsVisible (! fits);
    keyboard.setKeyWidth (fits ? keyWidth : 9.0f);

    if (fits)
        keyboard.setLowestVisibleKey (lowestKey);
}
