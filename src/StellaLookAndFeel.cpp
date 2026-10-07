// C:\workspace\Stella AI Studio\src\StellaLookAndFeel.cpp

#include "StellaLookAndFeel.h"

//==============================================================================
juce::Font Theme::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height).withStyle (bold ? "Bold" : "Regular"));
}

juce::Font Theme::monoFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

//==============================================================================
StellaLookAndFeel::StellaLookAndFeel()
    : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme { Theme::window,    // window background
                                                                 Theme::panel,     // widget background
                                                                 Theme::raised,    // menu background
                                                                 Theme::outline,   // outline
                                                                 Theme::text,      // default text
                                                                 Theme::raised,    // default fill
                                                                 juce::Colours::white,   // highlighted text
                                                                 Theme::accent,    // highlighted fill
                                                                 Theme::text })    // menu text
{
    setColour (juce::ResizableWindow::backgroundColourId, Theme::window);
    setColour (juce::DocumentWindow::textColourId, Theme::text);

    setColour (juce::TextButton::buttonColourId, Theme::raised);
    setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    setColour (juce::TextButton::textColourOffId, Theme::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);

    setColour (juce::ToggleButton::textColourId, Theme::text);
    setColour (juce::ToggleButton::tickColourId, Theme::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Theme::muted);

    setColour (juce::Label::textColourId, Theme::text);

    setColour (juce::TextEditor::backgroundColourId, Theme::inset);
    setColour (juce::TextEditor::textColourId, Theme::text);
    setColour (juce::TextEditor::outlineColourId, Theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
    setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    setColour (juce::CaretComponent::caretColourId, Theme::accent);

    setColour (juce::ComboBox::backgroundColourId, Theme::inset);
    setColour (juce::ComboBox::outlineColourId, Theme::outline);
    setColour (juce::ComboBox::textColourId, Theme::text);
    setColour (juce::ComboBox::arrowColourId, Theme::muted);
    setColour (juce::ComboBox::focusedOutlineColourId, Theme::accent);

    setColour (juce::PopupMenu::backgroundColourId, Theme::raised);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    setColour (juce::ListBox::backgroundColourId, Theme::inset);
    setColour (juce::ListBox::outlineColourId, Theme::outline);

    setColour (juce::ScrollBar::thumbColourId, Theme::muted.withAlpha (0.5f));

    setColour (juce::TabbedComponent::backgroundColourId, Theme::window);   // behind the tab bar
    setColour (juce::TabbedComponent::outlineColourId, Theme::outline);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::tabTextColourId, Theme::muted);
    setColour (juce::TabbedButtonBar::frontTextColourId, Theme::text);

    setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
    setColour (juce::AlertWindow::textColourId, Theme::text);
    setColour (juce::AlertWindow::outlineColourId, Theme::outline);

    setColour (juce::TooltipWindow::backgroundColourId, Theme::raised);
    setColour (juce::TooltipWindow::textColourId, Theme::text);
    setColour (juce::TooltipWindow::outlineColourId, Theme::outline);
}

//==============================================================================
void StellaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                              bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = backgroundColour;

    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha (0.45f);
    else if (isButtonDown)
        fill = fill.darker (0.25f);
    else if (isMouseOverButton)
        fill = fill.brighter (0.12f);

    // Buttons joined to a neighbour (the Design / Play pair) are square on that side.
    const bool flatLeft   = button.isConnectedOnLeft();
    const bool flatRight  = button.isConnectedOnRight();
    const bool flatTop    = button.isConnectedOnTop();
    const bool flatBottom = button.isConnectedOnBottom();

    juce::Path shape;
    shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), 5.0f, 5.0f,
                               ! (flatLeft || flatTop), ! (flatRight || flatTop),
                               ! (flatLeft || flatBottom), ! (flatRight || flatBottom));

    g.setColour (fill);
    g.fillPath (shape);

    if (! button.getToggleState())
    {
        g.setColour (Theme::outline);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }
}

juce::Font StellaLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return Theme::font (juce::jmin (15.0f, (float) buttonHeight * 0.5f));
}

//==============================================================================
void StellaLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
{
    const auto area = button.getActiveArea().toFloat();
    const bool front = button.isFrontTab();

    if (front)
    {
        g.setColour (Theme::panel);
        g.fillRect (area);

        // The selected tab carries the accent as an underline.
        g.setColour (Theme::accent);
        g.fillRect (area.withTop (area.getBottom() - 2.0f));
    }
    else if (isMouseOver)
    {
        g.setColour (Theme::raised.withAlpha (0.6f));
        g.fillRect (area);
    }

    g.setColour (front ? Theme::text : (isMouseOver ? Theme::text.withAlpha (0.85f) : Theme::muted));
    g.setFont (Theme::font (14.0f, front));
    g.drawText (button.getButtonText(), area.toNearestInt(), juce::Justification::centred, false);
}

int StellaLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (Theme::font (14.0f, true), button.getButtonText(), 0.0f, 0.0f);
    return juce::jmax (tabDepth * 2, (int) std::ceil (glyphs.getBoundingBox (0, glyphs.getNumGlyphs(), true).getWidth()) + 36);
}

void StellaLookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics& g, int w, int h)
{
    // This layer sits in front of the other tabs (only the front tab is above it),
    // so it must stay transparent apart from the baseline.
    g.setColour (Theme::outline);
    g.fillRect (0, h - 1, w, 1);
}

//==============================================================================
void StellaLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), 5.0f);
}

void StellaLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const bool focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
    g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), 5.0f, focused ? 1.5f : 1.0f);
}

//==============================================================================
void StellaLookAndFeel::drawStretchableLayoutResizerBar (juce::Graphics& g, int w, int h, bool isVerticalBar,
                                                         bool isMouseOver, bool isMouseDragging)
{
    g.setColour (Theme::window);
    g.fillRect (0, 0, w, h);

    g.setColour (isMouseDragging ? Theme::accent : (isMouseOver ? Theme::muted : Theme::outline));

    if (isVerticalBar)
        g.fillRect (w / 2, 0, 1, h);
    else
        g.fillRect (0, h / 2, w, 1);
}
