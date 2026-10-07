// C:\workspace\Stella AI Studio\src\KnobRenderer.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knobs in both apps.

/*
    KnobRenderer.h

    Draws a knob from a KnobStyle. Static layers (shadow, body gradient, gloss,
    cap) are cached as images keyed by size and display scale; only the flutes
    and the pointer are drawn per frame.

    The light lives in screen space: each flute is shaded from the angle it ends
    up at after rotation, never from its position on the body. That is what keeps
    the highlight still while the knob turns.
*/

#pragma once

#include "KnobStyle.h"

#include <cstdint>
#include <unordered_map>

//==============================================================================
class KnobRenderer
{
public:
    KnobRenderer() = default;
    explicit KnobRenderer (const KnobStyle& s) : style (s) {}

    void setStyle (const KnobStyle& s);
    const KnobStyle& getStyle() const noexcept { return style; }

    /** Draws into an existing context. value01 is 0..1 across the rotary range. */
    void draw (juce::Graphics& g,
               juce::Rectangle<float> area,
               float value01,
               float rotaryStartAngle,
               float rotaryEndAngle,
               bool  hover    = false,
               bool  pressed  = false,
               bool  enabled  = true);

    /** Renders one frame to an ARGB image of the given pixel size. */
    juce::Image renderFrame (int pixelSize,
                             float value01,
                             float rotaryStartAngle,
                             float rotaryEndAngle);

    /** Renders a filmstrip: frames laid out vertically or horizontally. */
    juce::Image renderFilmstrip (int pixelSize,
                                 int numFrames,
                                 bool vertical,
                                 float rotaryStartAngle,
                                 float rotaryEndAngle);

    static constexpr float defaultStartAngle = 3.7699f;   // 7:30, 1.2 * pi
    static constexpr float defaultEndAngle   = 8.7965f;   // 4:30, 2.8 * pi

private:
    //==========================================================================
    struct Geometry
    {
        float outerR = 0.0f;
        float knobR  = 0.0f;
    };

    struct Layers
    {
        juce::Image base;
        juce::Image gloss;
    };

    Geometry geometry (float radius) const noexcept;

    const Layers& getLayers (float diameter, float renderScale);
    juce::Image   renderBase  (int dim) const;
    juce::Image   renderGloss (int dim) const;

    void paintFlutes  (juce::Graphics&, juce::Point<float> centre, float knobR, float rotation) const;
    void paintPointer (juce::Graphics&, juce::Point<float> centre, float knobR, float angle) const;
    void paintTicks   (juce::Graphics&, juce::Point<float> centre, const Geometry&,
                       float startAngle, float endAngle) const;

    KnobStyle style;
    std::unordered_map<std::int64_t, Layers> layerCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobRenderer)
};
