// C:\workspace\Stella AI Studio\src\StellaLookAndFeel.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/** The studio's colours: near-black greys and one personality colour, Stella red. */
namespace Theme
{
    inline const juce::Colour window     { 0xff121214 };
    inline const juce::Colour panel      { 0xff1a1a1e };
    inline const juce::Colour raised     { 0xff24242a };
    inline const juce::Colour inset      { 0xff0d0d0f };
    inline const juce::Colour outline    { 0xff2f2f37 };
    inline const juce::Colour text       { 0xffe9e9ec };
    inline const juce::Colour muted      { 0xff8b8b96 };
    inline const juce::Colour accent     { 0xffe5484d };   // Stella red
    inline const juce::Colour accentDeep { 0xff481c1c };

    // Meter colours are a convention, not a style choice.
    inline const juce::Colour safe       { 0xff00c853 };
    inline const juce::Colour hot        { 0xffffb300 };
    inline const juce::Colour clip       { 0xffd50000 };

    juce::Font font (float height, bool bold = false);
    juce::Font monoFont (float height);
}

//==============================================================================
class StellaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StellaLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawStretchableLayoutResizerBar (juce::Graphics&, int w, int h, bool isVerticalBar,
                                          bool isMouseOver, bool isMouseDragging) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StellaLookAndFeel)
};
