// C:\workspace\Stella AI Studio\src\PreviewComponent.cpp
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knob editor in both apps.

/*
    PreviewComponent.cpp
*/

#include "PreviewComponent.h"

//==============================================================================
PreviewComponent::PreviewComponent()
{
    auto configure = [this] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (KnobRenderer::defaultStartAngle,
                               KnobRenderer::defaultEndAngle, true);
        s.setRange (0.0, 1.0, 0.0);
        s.setValue (0.5, juce::dontSendNotification);
        s.setLookAndFeel (&lookAndFeel);
        addAndMakeVisible (s);
    };

    configure (bigKnob);

    bigKnob.onValueChange = [this]
    {
        for (auto* k : sizeKnobs)
            k->setValue (bigKnob.getValue(), juce::dontSendNotification);
    };

    for (int i = 0; i < sizes.size(); ++i)
    {
        auto* s = sizeKnobs.add (new juce::Slider());
        configure (*s);
        s->setInterceptsMouseClicks (false, false);
    }

    sizeLabel.setJustificationType (juce::Justification::centred);
    sizeLabel.setText ("36 / 64 / 96 px", juce::dontSendNotification);
    sizeLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (sizeLabel);
}

PreviewComponent::~PreviewComponent()
{
    bigKnob.setLookAndFeel (nullptr);

    for (auto* s : sizeKnobs)
        s->setLookAndFeel (nullptr);
}

//==============================================================================
void PreviewComponent::setStyle (const KnobStyle& s)
{
    source.setStyle (s);
    repaint();
}

void PreviewComponent::setActualSize (bool shouldUseActualSize)
{
    if (actualSize == shouldUseActualSize)
        return;

    actualSize = shouldUseActualSize;
    resized();
    repaint();
}

void PreviewComponent::setActualSizePixels (int pixels)
{
    pixels = juce::jlimit (8, 4096, pixels);

    if (actualPixels == pixels)
        return;

    actualPixels = pixels;

    if (actualSize)
    {
        resized();
        repaint();
    }
}

void PreviewComponent::cycleBackground()
{
    backgroundMode = (backgroundMode + 1) % 3;
    repaint();
}

//==============================================================================
void PreviewComponent::paintBackgroundInto (juce::Graphics& g, juce::Rectangle<int> area) const
{
    switch (backgroundMode)
    {
        case 1:
            g.setColour (juce::Colour (0xffe8e8e8));
            g.fillRect (area);
            break;

        case 2:
        {
            const int cell = 12;
            g.setColour (juce::Colour (0xff909090));
            g.fillRect (area);
            g.setColour (juce::Colour (0xffb4b4b4));

            for (int y = area.getY(); y < area.getBottom(); y += cell)
                for (int x = area.getX(); x < area.getRight(); x += cell)
                    if ((((x - area.getX()) / cell) + ((y - area.getY()) / cell)) % 2 == 0)
                        g.fillRect (x, y, cell, cell);

            break;
        }

        default:
            g.setColour (juce::Colour (0xff1b1b1e));
            g.fillRect (area);
            break;
    }
}

void PreviewComponent::paint (juce::Graphics& g)
{
    paintBackgroundInto (g, getLocalBounds());
}

//==============================================================================
void PreviewComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    // At 1:1 the comparison strip goes away. The point of the mode is to judge
    // one knob at its real size, and the strip is the largest thing competing
    // for the room that knob needs.
    for (auto* k : sizeKnobs)
        k->setVisible (! actualSize);

    if (! actualSize)
    {
        auto strip = area.removeFromBottom (120);
        sizeLabel.setBounds (strip.removeFromBottom (20));
        sizeLabel.setText ("36 / 64 / 96 px", juce::dontSendNotification);

        const int total = 36 + 64 + 96 + 64;
        int x = strip.getCentreX() - total / 2;

        for (int i = 0; i < sizeKnobs.size(); ++i)
        {
            const int d = sizes[i];
            sizeKnobs[i]->setBounds (x, strip.getCentreY() - d / 2, d, d);
            x += d + 32;
        }
    }
    else
    {
        sizeLabel.setBounds (area.removeFromBottom (20));
    }

    const int fit = juce::jmin (area.getWidth(), area.getHeight());
    const int d   = actualSize ? juce::jmin (actualPixels, fit) : fit;

    if (actualSize)
        sizeLabel.setText (d < actualPixels
                             ? juce::String (actualPixels) + " px  -  panel only fits "
                                 + juce::String (fit) + " px"
                             : juce::String (actualPixels) + " px  (1:1)",
                           juce::dontSendNotification);

    bigKnob.setBounds (juce::Rectangle<int> (d, d).withCentre (area.getCentre()));
}
