// C:\workspace\Stella AI Studio\src\ColourSwatch.cpp
// From KnobMaker's StyleEditorPanel (C:\workspace\knobmaker), unchanged.

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

void ColourSwatchButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    // Checkerboard so alpha is visible — the bezel colour defaults to transparent.
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

    g.setColour (colour);
    g.fillRoundedRectangle (r, 3.0f);

    g.setColour (juce::Colours::black.withAlpha (down ? 0.8f : (highlighted ? 0.5f : 0.3f)));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void ColourSwatchButton::clicked()
{
    auto selector = std::make_unique<juce::ColourSelector> (
        juce::ColourSelector::showColourAtTop
      | juce::ColourSelector::showSliders
      | juce::ColourSelector::showColourspace
      | juce::ColourSelector::showAlphaChannel);

    selector->setName (getName());
    selector->setCurrentColour (colour, juce::dontSendNotification);
    selector->setSize (300, 380);
    selector->addChangeListener (this);

    juce::CallOutBox::launchAsynchronously (std::move (selector),
                                            getScreenBounds(),
                                            nullptr);
}

void ColourSwatchButton::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (auto* selector = dynamic_cast<juce::ColourSelector*> (source))
        setColourValue (selector->getCurrentColour(), juce::sendNotification);
}
