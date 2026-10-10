// C:\workspace\Stella AI Studio\src\ColourSwatch.h
// From KnobMaker's StyleEditorPanel (C:\workspace\knobmaker): the colour button the Edit UI's
// menus use. A click opens a colour picker in a call-out; a right-click copies the colour or
// pastes one (through the system clipboard, as "#AARRGGBB", so it goes from one button to another).

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <optional>

//==============================================================================
class ColourSwatchButton final : public juce::Button,
                                 private juce::ChangeListener
{
public:
    explicit ColourSwatchButton (const juce::String& name);
    ~ColourSwatchButton() override;

    void setColourValue (juce::Colour, juce::NotificationType);
    juce::Colour getColourValue() const noexcept { return colour; }

    /** What a see-through colour (the element's standard one) looks like: the button shows
        it, the picker starts from it (so what's picked shows), and Copy copies it. */
    void setStandardColour (juce::Colour c);

    /** The colour as it shows: the standard one when it's see-through. */
    juce::Colour getShownColour() const noexcept;

    std::function<void()> onColourChange;

private:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    using juce::Button::clicked;
    void clicked (const juce::ModifierKeys&) override;
    void openPicker();
    void showCopyPasteMenu();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** "#AARRGGBB", "#RRGGBB" or the same without "#"; nothing if it isn't one. */
    static std::optional<juce::Colour> parse (const juce::String& text);

    juce::Colour colour { juce::Colours::white };
    juce::Colour standard { juce::Colours::transparentBlack };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColourSwatchButton)
};
