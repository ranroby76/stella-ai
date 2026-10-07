// C:\workspace\Stella AI Studio\src\KeyboardStrip.cpp

#include "KeyboardStrip.h"
#include "AudioEngine.h"
#include "StellaLookAndFeel.h"

//==============================================================================
float KeyboardStrip::Meter::toPosition (float gain) noexcept
{
    // -60 dB at the bottom, +6 dB at the top.
    const auto db = juce::Decibels::gainToDecibels (gain, -60.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
}

void KeyboardStrip::Meter::setLevels (float left, float right)
{
    const float incoming[2] { left, right };

    for (int i = 0; i < 2; ++i)
    {
        // Fast up, slow down; the peak line holds for about a second.
        levels[i] = incoming[i] > levels[i] ? incoming[i] : levels[i] * 0.86f;

        if (incoming[i] >= holds[i])
        {
            holds[i] = incoming[i];
            holdFrames[i] = 30;
        }
        else if (--holdFrames[i] <= 0)
        {
            holds[i] *= 0.9f;
        }
    }

    repaint();
}

void KeyboardStrip::Meter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto barWidth = (bounds.getWidth() - 3.0f) * 0.5f;

    for (int i = 0; i < 2; ++i)
    {
        const auto bar = juce::Rectangle<float> (bounds.getX() + (float) i * (barWidth + 3.0f), bounds.getY(),
                                                 barWidth, bounds.getHeight());

        g.setColour (Theme::inset);
        g.fillRect (bar);

        const auto top = bar.getBottom() - bar.getHeight() * toPosition (levels[i]);
        const auto zeroDb = bar.getBottom() - bar.getHeight() * toPosition (1.0f);
        const auto minus6 = bar.getBottom() - bar.getHeight() * toPosition (0.5f);

        // Green to -6 dB, amber to 0 dB, red above.
        g.setColour (Theme::safe);
        g.fillRect (bar.withTop (juce::jmax (top, minus6)));

        if (top < minus6)
        {
            g.setColour (Theme::hot);
            g.fillRect (bar.withTop (juce::jmax (top, zeroDb)).withBottom (minus6));
        }

        if (top < zeroDb)
        {
            g.setColour (Theme::clip);
            g.fillRect (bar.withTop (top).withBottom (zeroDb));
        }

        const auto holdY = bar.getBottom() - bar.getHeight() * toPosition (holds[i]);

        if (holds[i] > 0.001f)
        {
            g.setColour (holds[i] >= 1.0f ? Theme::clip : Theme::text.withAlpha (0.7f));
            g.fillRect (bar.withTop (holdY).withHeight (1.5f));
        }
    }
}

//==============================================================================
KeyboardStrip::KeyboardStrip (AudioEngine& e)
    : engine (e),
      keyboard (e.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
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

    testToneButton.setClickingTogglesState (true);
    testToneButton.setToggleState (engine.isTestToneEnabled(), juce::dontSendNotification);
    testToneButton.setTooltip ("A simple sine sound for the notes you play, to test the audio device until a plugin plays");
    testToneButton.onClick = [this] { engine.setTestToneEnabled (testToneButton.getToggleState()); };

    panicButton.setTooltip ("Stop every sounding note");
    panicButton.onClick = [this] { engine.allNotesOff(); };

    addChildComponent (keyboard);
    keyboard.setVisible (keyboardShown);
    addAndMakeVisible (testToneButton);
    addAndMakeVisible (panicButton);
    addAndMakeVisible (meter);

    deviceButton.onClick = [this] { if (onDeviceClicked != nullptr) onDeviceClicked(); };
    addAndMakeVisible (deviceButton);

    startTimerHz (30);
}

KeyboardStrip::~KeyboardStrip()
{
    stopTimer();
}

void KeyboardStrip::timerCallback()
{
    meter.setLevels (engine.takePeak (0), engine.takePeak (1));

    const auto load = juce::String (juce::roundToInt (engine.getCpuLoad() * 100.0)) + "%";

    if (load != loadText)
    {
        loadText = load;
        repaint (getLocalBounds().removeFromRight (150));
    }
}

//==============================================================================
void KeyboardStrip::DeviceButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (down ? Theme::raised.brighter (0.15f) : (highlighted ? Theme::raised.brighter (0.07f) : Theme::raised));
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (highlighted ? Theme::accent.withAlpha (0.7f) : Theme::outline);
    g.drawRoundedRectangle (box, 5.0f, 1.0f);

    auto area = getLocalBounds().reduced (8, 4);
    const auto lineHeight = area.getHeight() / 3;

    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i == 0 ? Theme::text : Theme::muted);
        g.setFont (Theme::font (i == 0 ? 12.5f : 11.5f, i == 0));
        g.drawText (lines[i], area.removeFromTop (lineHeight), juce::Justification::centredLeft, true);
    }
}

void KeyboardStrip::setDevice (const juce::StringArray& lines, const juce::String& tooltip)
{
    deviceButton.setLines (lines);
    deviceButton.setTooltip (tooltip + "\nClick for the audio settings");
}

void KeyboardStrip::paint (juce::Graphics& g)
{
    g.fillAll (Theme::window);

    g.setColour (Theme::outline);
    g.fillRect (0, 0, getWidth(), 1);

    auto right = getLocalBounds().reduced (12, 10).removeFromRight (rightWidth);
    right.removeFromLeft (30);   // the meter

    g.setColour (Theme::muted);
    g.setFont (Theme::font (12.0f));
    g.drawText ("AUDIO LOAD", right.removeFromTop (16).withTrimmedLeft (10), juce::Justification::centredLeft, false);

    g.setColour (Theme::text);
    g.setFont (Theme::monoFont (15.0f));
    g.drawText (loadText, right.removeFromTop (20).withTrimmedLeft (10), juce::Justification::centredLeft, false);
}

void KeyboardStrip::resized()
{
    auto area = getLocalBounds().reduced (12, 10);

    auto buttons = area.removeFromLeft (96);
    testToneButton.setBounds (buttons.removeFromTop (34));
    buttons.removeFromTop (8);
    panicButton.setBounds (buttons.removeFromTop (34));
    area.removeFromLeft (12);

    auto right = area.removeFromRight (rightWidth);
    meter.setBounds (right.removeFromLeft (24));
    right.removeFromLeft (6);
    right.removeFromTop (38);   // the audio load, painted
    deviceButton.setBounds (right);
    area.removeFromRight (12);

    if (! keyboardShown)
        return;

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
