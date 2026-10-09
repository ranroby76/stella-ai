// C:\workspace\Stella AI Studio\src\ColourSwatch.h
// From KnobMaker's StyleEditorPanel (C:\workspace\knobmaker), unchanged: the colour button
// the Edit UI's menus use. It opens a colour picker in a call-out.

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

//==============================================================================
class ColourSwatchButton final : public juce::Button,
                                 private juce::ChangeListener
{
public:
    explicit ColourSwatchButton (const juce::String& name);
    ~ColourSwatchButton() override;

    void setColourValue (juce::Colour, juce::NotificationType);
    juce::Colour getColourValue() const noexcept { return colour; }

    std::function<void()> onColourChange;

private:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    using juce::Button::clicked;
    void clicked() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::Colour colour { juce::Colours::white };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColourSwatchButton)
};
