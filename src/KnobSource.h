// C:\workspace\Stella AI Studio\src\KnobSource.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knob editor in both apps.

/*
    KnobSource.h

    Where a widget's pixels come from. The plugin asks a source to draw a value
    and never learns which kind answered, so a procedural knob and an imported
    filmstrip can sit in the same panel and be swapped without touching the
    code that uses them.

    ProceduralSource is the only implementation today. ImageSource (PNG
    filmstrip) and CompositeSource (texture under procedural lighting) become
    additional implementations of this interface — not a parallel code path.
*/

#pragma once

#include "KnobRenderer.h"

//==============================================================================
class KnobSource
{
public:
    virtual ~KnobSource() = default;

    virtual void draw (juce::Graphics&,
                       juce::Rectangle<float> area,
                       float value01,
                       float rotaryStartAngle,
                       float rotaryEndAngle,
                       bool hover,
                       bool pressed,
                       bool enabled) = 0;

    virtual juce::String getTypeName() const = 0;
};

//==============================================================================
class ProceduralSource final : public KnobSource
{
public:
    ProceduralSource() = default;
    explicit ProceduralSource (const KnobStyle& s) : renderer (s) {}

    void setStyle (const KnobStyle& s)     { renderer.setStyle (s); }
    const KnobStyle& getStyle() const      { return renderer.getStyle(); }
    KnobRenderer& getRenderer() noexcept   { return renderer; }

    void draw (juce::Graphics& g,
               juce::Rectangle<float> area,
               float value01,
               float rotaryStartAngle,
               float rotaryEndAngle,
               bool hover, bool pressed, bool enabled) override
    {
        renderer.draw (g, area, value01, rotaryStartAngle, rotaryEndAngle,
                       hover, pressed, enabled);
    }

    juce::String getTypeName() const override { return "Procedural"; }

private:
    KnobRenderer renderer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProceduralSource)
};

//==============================================================================
/** Bridges any KnobSource onto a juce::Slider, which is how the preview gets
    real drag behaviour for free — and proves the source works as a LookAndFeel
    exactly the way it will inside a plugin. */
class KnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit KnobLookAndFeel (KnobSource& s) : source (s) {}

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        source.draw (g,
                     juce::Rectangle<int> (x, y, width, height).toFloat(),
                     sliderPos,
                     rotaryStartAngle, rotaryEndAngle,
                     slider.isMouseOverOrDragging(),
                     slider.isMouseButtonDown(),
                     slider.isEnabled());
    }

private:
    KnobSource& source;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobLookAndFeel)
};
