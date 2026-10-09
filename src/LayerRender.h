// C:\workspace\Stella AI Studio\src\LayerRender.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps
// (LayerRender.cpp finds Image layers' pictures through LayerImages).

/*
    LayerRender.h

    Rasterises a LayerDoc. Each layer draws its primitive into its own ARGB
    buffer, gets embossed from its own alpha, then has colour adjustments and
    masks applied before being composited onto the accumulator under its
    transform.

    Emboss is the piece that does the heavy lifting: it derives a height field
    from the primitive's alpha, takes the gradient of that field as a surface
    normal, and shades it against lightDir. That is how an arbitrary shape gets
    a bevel without anyone writing shape-specific code.
*/

#pragma once

#include "LayerDoc.h"

//==============================================================================
/** What the renderer noticed while drawing, for the UI to report. Optional
    everywhere — pass nullptr and nothing is collected. */
struct RenderNotes
{
    juce::StringArray messages;
};

class LayerRender
{
public:
    /** Renders the document at normalised time t (0..1).

        scale > 1 renders above the document's own pixel size. The engine is
        resolution independent — every primitive is described in fractions of
        the canvas — so this yields a genuinely sharper image rather than an
        upscaled one. Use it for on-screen preview; export stays at 1.0. */
    static juce::Image renderFrame (const LayerDoc&, float t, float scale = 1.0f,
                                    RenderNotes* notes = nullptr);

    /** Renders every frame into one strip. */
    static juce::Image renderFilmstrip (const LayerDoc&, bool vertical, float scale = 1.0f);

    /** Renders a single layer in isolation. */
    static juce::Image renderLayer (const LayerDoc&, int layerIndex, float t, float scale = 1.0f);

private:
    /** `t` is the frame position, 0..1. Only the LED reads it — everything else
        is shaped by the effect chain rather than by where it sits in the strip. */
    static juce::Image rasterisePrimitive (const PrimitiveSpec&, int w, int h, float scale,
                                           float t = 0.0f);

    /** CircleFill, MetalCircle and Sphere are one analytic renderer with three
        specular models, exactly as KnobMan does it: a per-pixel distance field
        with its own antialiasing, its own rim bevel driven by Emboss, and an
        edge fade driven by Diffuse. spectype 0 = flat tint, 1 = brushed metal
        (anisotropic sin(2t) sheen), 2 = true Phong sphere. */
    static juce::Image renderCircleFamily (const PrimitiveSpec&, int w, int h, int spectype);

    /** Circle: an analytic ring whose thickness is Width as a percentage of the
        radius, with a tube highlight running across the ring. */
    static juce::Image renderRing (const PrimitiveSpec&, int w, int h);

    /** True when the primitive shades itself and must skip the generic emboss
        pass, which would otherwise double-shade it. */
    static bool hasOwnShading (PrimType) noexcept;

    /** Photoshop's Bevel & Emboss. Every style is a band on the signed distance
        to the layer's own edge, turned into a height field, shaded against a
        light with both an azimuth and an altitude, then split into a highlight
        pass and a shadow pass with their own blend modes.

        This replaced the old applyEmboss. The circle family still builds its
        rim inside renderCircleFamily from PrimitiveSpec::emboss — that one is
        analytic and has nothing to do with this. */
    static void applyBevel (juce::Image&, const PrimitiveSpec&, float scale,
                            float lightAngleDeg, float lightAltitudeDeg);
    static void applyGloss         (juce::Image&, const PrimitiveSpec&);

    /** Bump or Fill, from TextureLibrary. Runs after emboss on purpose: in
        KnobMan the embossed bevel deliberately ignores the texture, and doing
        it in this order reproduces that. */
    static void applyTexture       (juce::Image&, const PrimitiveSpec&);

    /** Photoshop's Stroke: an outline hugging the shape's silhouette, placed
        outside it, centred on it, or inside it. Driven by a signed distance
        field built from the layer's own alpha, so it follows any shape without
        knowing what that shape is. */
    /** Photoshop's Stroke, applied to the layer AS PLACED ON THE CANVAS —
        same reason as the shadows. A CircleFill or a Sphere is rasterised
        inscribed in its own layer image, so measured in layer space an Outside
        stroke has nowhere to go but the four corners, and the layer's zoom then
        shrinks those into a square frame.

        Working in canvas space also matches Photoshop, where a layer style is
        applied after the transform: an outline keeps its width in screen pixels
        instead of thinning out as the layer is scaled down. */
    static void applyStroke (juce::Image& placed, const PrimitiveSpec&, float scale,
                             bool* wasClipped = nullptr);

    /** The drop shadow, the inner shadow and the outer glow, which are one
        construction with two switches.

        Outward (`inward` false) is a shadow or a glow: the coverage starts at
        the silhouette and fades outward over `size`. Inward is an inner shadow:
        the same ramp read from inside, clipped to the layer's own alpha, so it
        darkens the inside of the edge instead of the outside. An outer glow is
        just an outward pass with no offset.

        Pass the layer AS PLACED ON THE CANVAS — pass the image
        compositeLayer has already drawn into, not the raw layer image.

        That distinction is the whole feature. A Sphere is rasterised inscribed
        in its own layer image, touching all four edges, so measured in layer
        space it has no room around it for a shadow and the only empty space is
        the four corners. Scale that down by the layer's zoom and those corners
        arrive as a square block behind the knob.

        Working from the placed image also makes the offset plain screen pixels,
        with no zoom to divide out and no rotation to undo. */
    static juce::Image makeEdgeEffect (juce::Image& placed, const ShadowSpec&,
                                       float scale, bool inward, bool* wasClipped = nullptr);

    /** Composites one image onto another through a blend mode. Normal takes the
        fast path through juce::Graphics; the rest go per-pixel, which is what
        makes a Screen-blended glow add light rather than lay a coloured disc
        over the top. */
    static void blendOnto (juce::Image& canvas, const juce::Image& src, BlendMode);

    /** Places the layer, then stacks its edge effects around it. Takes the
        canvas image rather than a Graphics, because a blend mode other than
        Normal has to reach the pixels directly. */
    static void compositeWithEdgeEffects (juce::Image& canvas, const DocLayer&, juce::Image&,
                                          int canvasW, int canvasH, float t, float scale,
                                          RenderNotes* notes);
    static void applyColourAdjust  (juce::Image&, const EffectSpec&, float t);
    static void applyMasks         (juce::Image&, const EffectSpec&, float t);
    static void compositeLayer     (juce::Graphics&, const DocLayer&, juce::Image&,
                                    int canvasW, int canvasH, float t);
};
