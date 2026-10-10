// C:\workspace\Stella AI Studio\src\ColourSwatch.cpp
// From KnobMaker's StyleEditorPanel; with the standard colour it stands for when see-through,
// and Copy and Paste on a right-click.

#include "ColourSwatch.h"

//==============================================================================
ColourSwatchButton::ColourSwatchButton (const juce::String& name)
    : juce::Button (name)
{
}

ColourSwatchButton::~ColourSwatchButton()
{
}

void ColourSwatchButton::setColourValue (juce::Colour c, juce::NotificationType notify)
{
    if (c == colour)
        return;

    colour = c;
    repaint();

    if (notify != juce::dontSendNotification && onColourChange != nullptr)
        onColourChange();
}

void ColourSwatchButton::setStandardColour (juce::Colour c)
{
    standard = c;
    repaint();
}

juce::Colour ColourSwatchButton::getShownColour() const noexcept
{
    return colour.isTransparent() && ! standard.isTransparent() ? standard : colour;
}

void ColourSwatchButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    // Checkerboard so alpha is visible.
    const int cell = 6;
    g.setColour (juce::Colour (0xff555555));
    g.fillRoundedRectangle (r, 3.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, 3.0f);
        g.reduceClipRegion (clip);

        g.setColour (juce::Colour (0xff777777));

        for (int y = 0; y < getHeight(); y += cell)
            for (int x = 0; x < getWidth(); x += cell)
                if (((x / cell) + (y / cell)) % 2 == 0)
                    g.fillRect (x, y, cell, cell);
    }

    g.setColour (getShownColour());
    g.fillRoundedRectangle (r, 3.0f);

    g.setColour (juce::Colours::black.withAlpha (down ? 0.8f : (highlighted ? 0.5f : 0.3f)));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void ColourSwatchButton::clicked (const juce::ModifierKeys& modifiers)
{
    if (modifiers.isPopupMenu())
        showCopyPasteMenu();
    else
        openPicker();
}

void ColourSwatchButton::openPicker()
{
    auto selector = std::make_unique<juce::ColourSelector> (
        juce::ColourSelector::showColourAtTop
      | juce::ColourSelector::showSliders
      | juce::ColourSelector::showColourspace
      | juce::ColourSelector::showAlphaChannel);

    // A see-through colour starts from the one it shows, solid: otherwise every colour picked
    // would stay see-through, and nothing would change.
    const auto start = colour.isTransparent() ? (standard.isTransparent() ? colour.withAlpha (1.0f) : standard.withAlpha (1.0f)) : colour;

    selector->setName (getName());
    selector->setCurrentColour (start, juce::dontSendNotification);
    selector->setSize (300, 380);
    selector->addChangeListener (this);

    juce::CallOutBox::launchAsynchronously (std::move (selector),
                                            getScreenBounds(),
                                            nullptr);
}

void ColourSwatchButton::showCopyPasteMenu()
{
    const auto pasted = parse (juce::SystemClipboard::getTextFromClipboard());

    juce::PopupMenu menu;
    menu.addItem (1, "Copy");
    menu.addItem (2, "Paste", pasted.has_value());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safeThis = juce::Component::SafePointer<ColourSwatchButton> (this), pasted] (int choice)
                        {
                            if (safeThis == nullptr)
                                return;

                            if (choice == 1)
                                juce::SystemClipboard::copyTextToClipboard ("#" + safeThis->getShownColour().toDisplayString (true));
                            else if (choice == 2 && pasted.has_value())
                                safeThis->setColourValue (*pasted, juce::sendNotification);
                        });
}

std::optional<juce::Colour> ColourSwatchButton::parse (const juce::String& text)
{
    auto hex = text.trim().trimCharactersAtStart ("#");

    if (hex.length() == 6)
        hex = "ff" + hex;

    if (hex.length() != 8 || ! hex.containsOnly ("0123456789abcdefABCDEF"))
        return std::nullopt;

    return juce::Colour ((juce::uint32) hex.getHexValue64());
}

void ColourSwatchButton::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (auto* selector = dynamic_cast<juce::ColourSelector*> (source))
        setColourValue (selector->getCurrentColour(), juce::sendNotification);
}
