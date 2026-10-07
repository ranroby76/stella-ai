// C:\workspace\Stella AI Studio\src\StyleEditorPanel.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knob editor in both apps.

/*
    StyleEditorPanel.h

    Builds itself from KnobStyle's reflection tables, so adding a parameter to
    the struct puts a control here with no extra UI code.
*/

#pragma once

#include "KnobStyle.h"

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
    void clicked() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::Colour colour { juce::Colours::white };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColourSwatchButton)
};

//==============================================================================
class StyleEditorPanel final : public juce::Component
{
public:
    StyleEditorPanel();

    void setStyle (const KnobStyle&);
    KnobStyle getStyle() const noexcept { return style; }

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Height the panel needs; the owner puts it in a Viewport. */
    int getRequiredHeight() const noexcept { return requiredHeight; }

    std::function<void (const KnobStyle&)> onChange;

private:
    struct Row
    {
        juce::String group;
        std::unique_ptr<juce::Label>     label;
        std::unique_ptr<juce::Slider>    slider;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<ColourSwatchButton> swatch;
    };

    void buildRows();
    void pushToControls();
    void pullFromControls();

    KnobStyle style;
    std::vector<Row> rows;
    juce::OwnedArray<juce::Label> headers;

    int requiredHeight = 0;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleEditorPanel)
};
