// C:\workspace\Stella AI Studio\src\KnobRenderer.cpp
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knobs in both apps.

/*
    KnobRenderer.cpp
*/

#include "KnobRenderer.h"

#include <cmath>

namespace
{
    constexpr float kKnobScale = 0.90f;   // knob radius as a fraction of the half-extent
}

//==============================================================================
void KnobRenderer::setStyle (const KnobStyle& s)
{
    if (s == style)
        return;

    style = s;
    layerCache.clear();
}

//==============================================================================
KnobRenderer::Geometry KnobRenderer::geometry (float radius) const noexcept
{
    Geometry geo;
    geo.outerR = radius * 0.94f;

    const bool hasAnnulus = (! style.bezel.isTransparent()) || style.tickCount > 1;

    geo.knobR = hasAnnulus
                  ? geo.outerR * (1.0f - juce::jlimit (0.03f, 0.40f, style.bezelWidth))
                  : radius * kKnobScale;

    return geo;
}

//==============================================================================
const KnobRenderer::Layers& KnobRenderer::getLayers (float diameter, float renderScale)
{
    const int px = juce::jmax (6, (int) std::lround (diameter));
    const int sq = juce::jlimit (2, 12, (int) std::lround (renderScale * 4.0f));

    const std::int64_t key = ((std::int64_t) px << 8) | (std::int64_t) sq;

    if (auto it = layerCache.find (key); it != layerCache.end())
        return it->second;

    if (layerCache.size() >= 24)
        layerCache.clear();

    const float s   = (float) sq * 0.25f;
    const int   dim = juce::jmax (12, (int) std::lround ((float) px * s));

    Layers layers;
    layers.base  = renderBase  (dim);
    layers.gloss = renderGloss (dim);

    return layerCache.emplace (key, std::move (layers)).first->second;
}

//==============================================================================
juce::Image KnobRenderer::renderBase (int dim) const
{
    juce::Image img (juce::Image::ARGB, dim, dim, true);
    juce::Graphics g (img);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    const float R = (float) dim * 0.5f;
    const juce::Point<float> c { R, R };
    const auto  geo = geometry (R);
    const float kr  = geo.knobR;

    juce::Path knob;
    knob.addEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);

    // ---- cast shadow -------------------------------------------------------
    {
        const int blur = juce::jmax (1, (int) std::lround (kr * style.shadowRadius));
        const int drop = (int) std::lround (kr * style.shadowOffset);
        juce::DropShadow (style.shadow, blur, { 0, drop }).drawForPath (g, knob);
    }

    // ---- bezel ring --------------------------------------------------------
    if (! style.bezel.isTransparent() && geo.outerR > kr)
    {
        const float mid = (geo.outerR + kr) * 0.5f;
        const float w   = juce::jmax (1.0f, geo.outerR - kr);

        juce::ColourGradient bg (style.bezel.brighter (0.55f), c.x, c.y - geo.outerR,
                                 style.bezel.darker   (0.50f), c.x, c.y + geo.outerR, false);
        bg.addColour (0.42, style.bezel.brighter (0.05f));
        bg.addColour (0.66, style.bezel.darker   (0.22f));

        juce::Path ring;
        ring.addCentredArc (c.x, c.y, mid, mid, 0.0f,
                            0.0f, juce::MathConstants<float>::twoPi, true);

        g.setGradientFill (bg);
        g.strokePath (ring, juce::PathStrokeType (w));
    }

    // ---- body: the falling-light gradient ----------------------------------
    {
        const auto b = style.body;

        juce::ColourGradient grad (b.brighter (0.62f), c.x, c.y - kr,
                                   b.darker   (0.55f), c.x, c.y + kr, false);
        grad.addColour (0.16, b.brighter (0.30f));
        grad.addColour (0.38, b.brighter (0.04f));
        grad.addColour (0.54, b.darker   (0.22f));
        grad.addColour (0.70, b.darker   (0.06f));   // gloss-contour bounce
        grad.addColour (0.88, b.darker   (0.40f));

        g.setGradientFill (grad);
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
    }

    // ---- seated shadow along the lower rim ---------------------------------
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (knob);

        for (int i = 0; i < 5; ++i)
        {
            const float t = (float) i / 4.0f;
            const float w = juce::jmax (1.0f, kr * (0.03f + 0.10f * t));
            const float r = kr - w * 0.5f;

            juce::Path arc;
            arc.addCentredArc (c.x, c.y, r, r, 0.0f, 1.84f, 4.44f, true);

            g.setColour (juce::Colours::black.withAlpha (0.16f * (1.0f - t)));
            g.strokePath (arc, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded),
                          juce::AffineTransform::rotation (style.lightAngle, c.x, c.y));
        }
    }

    return img;
}

//==============================================================================
juce::Image KnobRenderer::renderGloss (int dim) const
{
    juce::Image img (juce::Image::ARGB, dim, dim, true);
    juce::Graphics g (img);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    const float R = (float) dim * 0.5f;
    const juce::Point<float> c { R, R };
    const auto  geo = geometry (R);
    const float kr  = geo.knobR;

    juce::Path knob;
    knob.addEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);

    // ---- specular pool, clipped to the body --------------------------------
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (knob);

        const float sx = c.x + std::sin (style.lightAngle) * kr * 0.34f;
        const float sy = c.y - std::cos (style.lightAngle) * kr * 0.34f;
        const float sr = juce::jmax (2.0f, kr * 0.98f);

        juce::ColourGradient spec (juce::Colours::white.withAlpha (0.32f * style.specularStrength),
                                   sx, sy,
                                   juce::Colours::transparentWhite, sx + sr, sy, true);
        spec.addColour (0.40, juce::Colours::white.withAlpha (0.11f * style.specularStrength));

        g.setGradientFill (spec);
        g.fillEllipse (sx - sr, sy - sr, sr * 2.0f, sr * 2.0f);
    }

    // ---- rim: lit above, dark below ----------------------------------------
    {
        const float w    = juce::jmax (1.0f, kr * 0.05f);
        const float rr   = kr - w * 0.5f;
        const auto  tilt = juce::AffineTransform::rotation (style.lightAngle, c.x, c.y);

        juce::Path lit;
        lit.addCentredArc (c.x, c.y, rr, rr, 0.0f, -1.15f, 1.15f, true);
        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.strokePath (lit, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded), tilt);

        juce::Path dark;
        dark.addCentredArc (c.x, c.y, rr, rr, 0.0f, 1.99f, 4.29f, true);
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.strokePath (dark, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded), tilt);
    }

    // ---- raised centre cap --------------------------------------------------
    {
        const float capR = kr * juce::jlimit (0.20f, 0.94f, style.capRadius);

        juce::Path capPath;
        capPath.addEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);

        juce::DropShadow (juce::Colours::black.withAlpha (0.50f),
                          juce::jmax (1, (int) std::lround (capR * 0.16f)),
                          { 0, (int) std::lround (capR * 0.09f) }).drawForPath (g, capPath);

        const auto k = style.cap;

        juce::ColourGradient capGrad (k.brighter (0.50f), c.x, c.y - capR,
                                      k.darker   (0.45f), c.x, c.y + capR, false);
        capGrad.addColour (0.22, k.brighter (0.20f));
        capGrad.addColour (0.50, k.brighter (0.01f));
        capGrad.addColour (0.68, k.darker   (0.20f));
        capGrad.addColour (0.84, k.darker   (0.08f));

        g.setGradientFill (capGrad);
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);

        const float cw   = juce::jmax (1.0f, capR * 0.06f);
        const float cr   = capR - cw * 0.5f;
        const auto  tilt = juce::AffineTransform::rotation (style.lightAngle, c.x, c.y);

        juce::Path capLit;
        capLit.addCentredArc (c.x, c.y, cr, cr, 0.0f, -1.05f, 1.05f, true);
        g.setColour (juce::Colours::white.withAlpha (0.26f));
        g.strokePath (capLit, juce::PathStrokeType (cw, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded), tilt);

        juce::Path capDark;
        capDark.addCentredArc (c.x, c.y, cr, cr, 0.0f, 2.09f, 4.19f, true);
        g.setColour (juce::Colours::black.withAlpha (0.24f));
        g.strokePath (capDark, juce::PathStrokeType (cw, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded), tilt);
    }

    return img;
}

//==============================================================================
void KnobRenderer::paintFlutes (juce::Graphics& g,
                                juce::Point<float> c, float kr, float rotation) const
{
    const int n = juce::jlimit (8, juce::jmax (8, style.fluteCount),
                                (int) std::lround (kr * 0.9f));

    const float rIn  = kr * juce::jlimit (0.10f, 0.95f, style.fluteInner);
    const float rOut = kr * juce::jlimit (0.20f, 1.00f, style.fluteOuter);

    if (rOut <= rIn)
        return;

    const float pitch = juce::MathConstants<float>::twoPi / (float) n;
    const float half  = pitch * juce::jlimit (0.05f, 0.48f, style.fluteDuty * 0.5f);

    const float wIn  = std::sin (half) * rIn;
    const float wOut = std::sin (half) * rOut;

    juce::Path flute;
    flute.startNewSubPath (-wIn,  -rIn);
    flute.lineTo          (-wOut, -rOut);
    flute.lineTo          ( wOut, -rOut);
    flute.lineTo          ( wIn,  -rIn);
    flute.closeSubPath();

    const auto darkFace  = style.body.darker   (0.62f);
    const auto lightFace = style.body.brighter (0.50f);
    const auto groove    = style.body.darker   (0.88f);

    const float grooveW = juce::jmax (0.5f, kr * 0.010f);
    const float ambient = juce::jlimit (0.0f, 0.9f, style.ambient);
    const bool  drawGrooves = kr > 20.0f;

    for (int i = 0; i < n; ++i)
    {
        const float theta = rotation + pitch * (float) i;

        // Lambertian term against a light that never rotates.
        const float d       = std::cos (theta - style.lightAngle);
        const float diffuse = 0.5f + 0.5f * d;
        const float shade   = juce::jlimit (0.0f, 1.0f, ambient + (1.0f - ambient) * diffuse);
        const float spec    = std::pow (juce::jmax (0.0f, d), style.specularTightness);

        const auto transform = juce::AffineTransform::rotation (theta).translated (c.x, c.y);

        auto face = darkFace.interpolatedWith (lightFace, shade);
        face = face.brighter (spec * style.specularStrength * 0.75f);

        g.setColour (face);
        g.fillPath (flute, transform);

        if (drawGrooves)
        {
            g.setColour (groove.withAlpha (0.20f + 0.30f * (1.0f - shade)));
            g.strokePath (flute, juce::PathStrokeType (grooveW), transform);
        }
    }
}

//==============================================================================
void KnobRenderer::paintPointer (juce::Graphics& g,
                                 juce::Point<float> c, float kr, float angle) const
{
    const float w  = juce::jmax (1.5f, kr * style.pointerWidth);
    const float y0 = -kr * juce::jlimit (0.15f, 1.0f,  style.pointerOuter);
    const float y1 = -kr * juce::jlimit (0.0f,  0.95f, style.pointerInner);

    if (y1 <= y0)
        return;

    juce::Path p;
    p.addRoundedRectangle (-w * 0.5f, y0, w, y1 - y0, w * 0.5f);

    const auto transform = juce::AffineTransform::rotation (angle).translated (c.x, c.y);

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillPath (p, transform.translated (0.0f, juce::jmax (0.5f, kr * 0.015f)));

    g.setColour (style.pointer);
    g.fillPath (p, transform);
}

//==============================================================================
void KnobRenderer::paintTicks (juce::Graphics& g,
                               juce::Point<float> c, const Geometry& geo,
                               float startAngle, float endAngle) const
{
    const int n = juce::jlimit (2, 64, style.tickCount);

    const float rIn  = geo.knobR  * 1.04f;
    const float rOut = geo.outerR * 0.99f;

    if (rOut <= rIn)
        return;

    const float w = juce::jmax (1.0f, geo.knobR * 0.035f);

    juce::Path tick;
    tick.addRoundedRectangle (-w * 0.5f, -rOut, w, rOut - rIn, w * 0.5f);

    g.setColour (style.tick);

    for (int i = 0; i < n; ++i)
    {
        const float t     = (n == 1) ? 0.0f : (float) i / (float) (n - 1);
        const float theta = startAngle + t * (endAngle - startAngle);

        g.fillPath (tick, juce::AffineTransform::rotation (theta).translated (c.x, c.y));
    }
}

//==============================================================================
void KnobRenderer::draw (juce::Graphics& g,
                         juce::Rectangle<float> area,
                         float value01,
                         float rotaryStartAngle,
                         float rotaryEndAngle,
                         bool hover, bool pressed, bool enabled)
{
    const float diameter = juce::jmin (area.getWidth(), area.getHeight());

    if (diameter < 6.0f)
        return;

    const auto  square = juce::Rectangle<float> (diameter, diameter).withCentre (area.getCentre());
    const auto  centre = square.getCentre();
    const float radius = diameter * 0.5f;
    const auto  geo    = geometry (radius);

    const float angle = rotaryStartAngle
                      + juce::jlimit (0.0f, 1.0f, value01) * (rotaryEndAngle - rotaryStartAngle);

    // Two-times supersampled cache; bounded so a big knob never eats memory.
    const auto& layers = getLayers (diameter, 2.0f);

    g.drawImage (layers.base, square, juce::RectanglePlacement::stretchToFit);

    // After the base image: the bezel lives in there, and ticks are printed on
    // top of a bezel rather than under it.
    if (style.tickCount > 1)
        paintTicks (g, centre, geo, rotaryStartAngle, rotaryEndAngle);

    if (style.drawFlutes)
        paintFlutes (g, centre, geo.knobR, style.rotateBody ? angle : 0.0f);

    g.drawImage (layers.gloss, square, juce::RectanglePlacement::stretchToFit);

    paintPointer (g, centre, geo.knobR, angle);

    const auto knobRect = juce::Rectangle<float> (geo.knobR * 2.0f, geo.knobR * 2.0f)
                              .withCentre (centre);

    if (! enabled)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (knobRect);
    }
    else if (hover || pressed)
    {
        g.setColour (juce::Colours::white.withAlpha (pressed ? 0.09f : 0.05f));
        g.fillEllipse (knobRect);
    }
}

//==============================================================================
juce::Image KnobRenderer::renderFrame (int pixelSize,
                                       float value01,
                                       float rotaryStartAngle,
                                       float rotaryEndAngle)
{
    const int dim = juce::jmax (8, pixelSize);

    juce::Image img (juce::Image::ARGB, dim, dim, true);
    juce::Graphics g (img);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    draw (g, juce::Rectangle<float> (0.0f, 0.0f, (float) dim, (float) dim),
          value01, rotaryStartAngle, rotaryEndAngle, false, false, true);

    return img;
}

//==============================================================================
juce::Image KnobRenderer::renderFilmstrip (int pixelSize,
                                           int numFrames,
                                           bool vertical,
                                           float rotaryStartAngle,
                                           float rotaryEndAngle)
{
    const int dim    = juce::jmax (8, pixelSize);
    const int frames = juce::jlimit (2, 512, numFrames);

    juce::Image strip (juce::Image::ARGB,
                       vertical ? dim : dim * frames,
                       vertical ? dim * frames : dim,
                       true);

    juce::Graphics g (strip);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    for (int i = 0; i < frames; ++i)
    {
        const float value01 = (frames == 1) ? 0.0f : (float) i / (float) (frames - 1);

        const juce::Rectangle<float> cell (vertical ? 0.0f : (float) (i * dim),
                                           vertical ? (float) (i * dim) : 0.0f,
                                           (float) dim, (float) dim);

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cell.toNearestInt());

        draw (g, cell, value01, rotaryStartAngle, rotaryEndAngle, false, false, true);
    }

    return strip;
}
