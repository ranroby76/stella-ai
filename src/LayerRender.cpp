// C:\workspace\Stella AI Studio\src\LayerRender.cpp
// From KnobMaker (C:\workspace\knobmaker); in Stella an Image layer's picture is found
// through LayerImages and read from disk once (cachedLayerImage).

/*
    LayerRender.cpp
*/

#include "LayerRender.h"
#include "BmpImage.h"
#include "ShapeDoc.h"
#include "TextureLibrary.h"

#include <algorithm>

#include <cmath>
#include <map>
#include <vector>

namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi;

    /** Stella: an Image layer's picture, read once and kept until the file changes. */
    juce::Image cachedLayerImage (const juce::File& file)
    {
        struct Entry
        {
            juce::Time modified;
            juce::int64 size = 0;
            juce::Image image;
        };

        static juce::CriticalSection lock;
        static std::map<juce::String, Entry> cache;

        if (! file.existsAsFile())
            return {};

        const auto modified = file.getLastModificationTime();
        const auto size = file.getSize();
        const juce::ScopedLock sl (lock);
        auto& entry = cache[file.getFullPathName()];

        if (! entry.image.isValid() || entry.modified != modified || entry.size != size)
        {
            entry.image = loadImageFile (file);
            entry.modified = modified;
            entry.size = size;
        }

        return entry.image;
    }

    inline float deg2rad (float d) noexcept { return d * kPi / 180.0f; }

    /** Walks `distance` along a closed polyline from vertex `from`, in the given
        direction, and returns the point it lands on. */
    juce::Point<float> walkAlong (const std::vector<juce::Point<float>>& pts,
                                  int from, int step, float distance)
    {
        const int n = (int) pts.size();

        if (n < 2)
            return pts.empty() ? juce::Point<float>() : pts.front();

        auto at = [&pts, n] (int i) { return pts[(size_t) (((i % n) + n) % n)]; };

        auto current = at (from);
        int  index = from;

        for (int guard = 0; guard < n && distance > 0.0f; ++guard)
        {
            const auto next = at (index + step);
            const float segment = current.getDistanceFrom (next);

            if (segment > 1.0e-6f)
            {
                if (segment >= distance)
                    return current + (next - current) * (distance / segment);

                distance -= segment;
            }

            current = next;
            index += step;
        }

        return current;
    }

    /** Rounds a closed polygon at NAMED corners only, filleting each by walking
        along the outline rather than along a single edge.

        Path::createPathWithRoundedCorners cannot do this job: it treats every
        vertex as a corner and clamps each fillet to the adjoining segment. On a
        shape approximated by a strip of segments — a bent spoke, say — that
        caps the radius at the real corners to one segment's worth, which is why
        the roundness barely showed. Filleting by arc length lets a corner eat
        as far back along a curved side as it needs to. */
    juce::Path roundPolygonCorners (const std::vector<juce::Point<float>>& pts,
                                    const std::vector<int>& corners, float radius)
    {
        juce::Path path;

        const int n = (int) pts.size();

        if (n < 3 || corners.size() < 2)
        {
            for (int i = 0; i < n; ++i)
                (i == 0 ? path.startNewSubPath (pts[0]) : path.lineTo (pts[(size_t) i]));

            if (n >= 3)
                path.closeSubPath();

            return path;
        }

        auto at = [&pts, n] (int i) { return pts[(size_t) (((i % n) + n) % n)]; };

        // Steps forward from one index to another around the ring. Raw index
        // comparison would never terminate on the run that wraps past the end.
        auto stepsBetween = [n] (int from, int to)
        {
            return (((to - from) % n) + n) % n;
        };

        // Arc length from one corner to the next, so a fillet never eats more
        // than half the run it sits on and two neighbours cannot overlap.
        auto runLength = [&at, &stepsBetween] (int from, int to)
        {
            float total = 0.0f;
            const int steps = stepsBetween (from, to);

            for (int k = 0; k < steps; ++k)
                total += at (from + k).getDistanceFrom (at (from + k + 1));

            return total;
        };

        const int count = (int) corners.size();

        std::vector<juce::Point<float>> entry ((size_t) count), exit ((size_t) count);
        std::vector<float> used ((size_t) count);

        for (int c = 0; c < count; ++c)
        {
            const int here = corners[(size_t) c];
            const int prev = corners[(size_t) ((c - 1 + count) % count)];
            const int next = corners[(size_t) ((c + 1) % count)];

            const float r = juce::jmin (radius,
                                        runLength (prev, here) * 0.5f,
                                        runLength (here, next) * 0.5f);

            used[(size_t) c]  = r;
            entry[(size_t) c] = walkAlong (pts, here, -1, r);
            exit[(size_t) c]  = walkAlong (pts, here, +1, r);
        }

        for (int c = 0; c < count; ++c)
        {
            const int here = corners[(size_t) c];
            const int next = corners[(size_t) ((c + 1) % count)];

            if (c == 0) path.startNewSubPath (entry[0]);
            else        path.lineTo (entry[(size_t) c]);

            if (used[(size_t) c] > 0.01f)
                path.quadraticTo (at (here), exit[(size_t) c]);
            else
                path.lineTo (at (here));

            // The stretch on to the next corner, vertex by vertex, so a curved
            // side stays curved instead of collapsing into one straight run.
            const int steps = stepsBetween (here, next);

            for (int k = 1; k < steps; ++k)
                path.lineTo (at (here + k));
        }

        path.closeSubPath();
        return path;
    }


    /** Direct access to a premultiplied ARGB pixel. Much faster than
        getPixelColour/setPixelColour, which matters once we render at 3-4x for
        the preview. Scaling R,G,B is valid in premultiplied space so long as
        the result is clamped to the alpha, not to 255. */
    inline juce::PixelARGB* pixelAt (juce::Image::BitmapData& d, int x, int y) noexcept
    {
        return reinterpret_cast<juce::PixelARGB*> (d.getLinePointer (y) + x * d.pixelStride);
    }

    inline juce::uint8 scaleComponent (juce::uint8 value, float mul, juce::uint8 alpha) noexcept
    {
        return (juce::uint8) juce::jlimit (0, (int) alpha, (int) std::lround (value * mul));
    }

    /** Separable box blur on a float field — softens the bevel height map so
        the bevel has width instead of being a one-pixel step, and is what
        Soften and Chisel Soft are built from. */
    void boxBlur (std::vector<float>& field, int w, int h, int radius, int passes = 1)
    {
        if (radius < 1 || passes < 1)
            return;

        std::vector<float> temp ((size_t) (w * h), 0.0f);

        for (int pass = 0; pass < passes; ++pass)
        {

            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    float sum = 0.0f;
                    int   n   = 0;

                    for (int i = -radius; i <= radius; ++i)
                    {
                        const int xx = x + i;

                        if (xx >= 0 && xx < w)
                        {
                            sum += field[(size_t) (y * w + xx)];
                            ++n;
                        }
                    }

                    temp[(size_t) (y * w + x)] = (n > 0) ? sum / (float) n : 0.0f;
                }
            }

            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    float sum = 0.0f;
                    int   n   = 0;

                    for (int i = -radius; i <= radius; ++i)
                    {
                        const int yy = y + i;

                        if (yy >= 0 && yy < h)
                        {
                            sum += temp[(size_t) (yy * w + x)];
                            ++n;
                        }
                    }

                    field[(size_t) (y * w + x)] = (n > 0) ? sum / (float) n : 0.0f;
                }
            }
        }
    }
}


//==============================================================================
bool LayerRender::hasOwnShading (PrimType t) noexcept
{
    return t == PrimType::Circle || t == PrimType::CircleFill
        || t == PrimType::MetalCircle || t == PrimType::Sphere;
}

namespace
{
    /** Unit vector pointing from the surface toward the light, in screen space
        (y grows downward). lightDir is degrees, 135 = upper-left. */
    inline void lightVector (float lightDirDegrees, double& lx, double& ly) noexcept
    {
        const double rad = lightDirDegrees * juce::MathConstants<double>::pi / 180.0;
        lx =  std::cos (rad);
        ly = -std::sin (rad);
    }

    inline juce::uint8 addBrightness (int component, int delta) noexcept
    {
        return (juce::uint8) juce::jlimit (0, 255, component + delta);
    }
}

//==============================================================================
juce::Image LayerRender::renderCircleFamily (const PrimitiveSpec& p, int w, int h, int spectype)
{
    juce::Image img (juce::Image::ARGB, w, h, true);

    {
        juce::Image::BitmapData data (img, juce::Image::BitmapData::readWrite);

        const double cx = w * 0.5, cy = h * 0.5;
        double ax = cx, ay = cy;

        if (p.aspect > 0.0f) ax = (100.0 - juce::jmin (p.aspect,  99.0f)) / 100.0 * cx;
        if (p.aspect < 0.0f) ay = (100.0 + juce::jmax (p.aspect, -99.0f)) / 100.0 * cy;

        // Emboss is the rim bevel here, consumed internally rather than by the
        // generic emboss pass.
        double rthick = juce::jmin (0.99, std::abs ((double) p.emboss) / 100.0);
        const double embossEdge = juce::jlimit (0.0, 1.0, (100.0 - p.embossDiffuse) / 100.0);

        // Diffuse fades the outer edge; it is not a lighting term.
        const double rD  = 1.0 - juce::jlimit (0.0, 100.0, (double) p.diffuse) / 100.0;
        const double rD2 = rD * rD;

        const double rMin = juce::jmin (ax, ay);

        double lx, ly;
        lightVector (p.lightDir, lx, ly);

        // Elevation of the key light above the plane.
        const double lz  = 0.72;
        const double ln  = std::sqrt (lx * lx + ly * ly + lz * lz);
        const double Lx = lx / ln, Ly = ly / ln, Lz = lz / ln;

        // Bevel light: same direction, mirrored to match the tangent frame.
        const double bn = std::sqrt (lx * lx + ly * ly + 1.0);
        const double Bx = -lx / bn, By = -ly / bn, Bz = 1.0 / bn;
        const double rTZ = 1.0 / std::sqrt (2.0);

        const int ambient  = (int) (juce::jlimit (0.0f, 100.0f, p.ambient)  * 2.55f);
        const int specular = (int) (juce::jlimit (0.0f, 100.0f, p.specular) * 2.55f);

        const double metalWidth = (p.specularWidth <= 0.0f)
                                    ? 0.0
                                    : std::pow (1.0 / (p.specularWidth * 0.01), 3.0);
        const double phongPower = juce::jmax (0.5, (110.0 - p.specularWidth) / 10.0);
        const double lightRad   = p.lightDir * juce::MathConstants<double>::pi / 180.0;

        for (int y = 0; y < h; ++y)
        {
            const double py   = (double) y - cy + 0.5;
            const double ry   = py / (ay + 0.5);
            const double rym  = py / juce::jmax (0.1, ay - 0.5);
            const double rye  = py / juce::jmax (0.1, ay + 0.5 - rMin * rthick * embossEdge);
            const double ryem = py / juce::jmax (0.1, ay - rMin * rthick);

            for (int x = 0; x < w; ++x)
            {
                const double px   = (double) x - cx + 0.5;
                const double rx   = px / (ax + 0.5);
                const double rxm  = px / juce::jmax (0.1, ax - 0.5);
                const double rxe  = px / juce::jmax (0.1, ax + 0.5 - rMin * rthick * embossEdge);
                const double rxem = px / juce::jmax (0.1, ax - rMin * rthick);

                const double rxy   = rx * rx + ry * ry;
                const double rxym  = rxm * rxm + rym * rym;
                const double rxye  = rxe * rxe + rye * rye;
                const double rxyem = rxem * rxem + ryem * ryem;

                if (rxy > 1.0)
                    continue;

                int cr = p.colour.getRed(), cg = p.colour.getGreen(), cb = p.colour.getBlue();

                // ---- face shading, only inside the bevel -------------------
                if (rxye < 1.0)
                {
                    if (spectype == 0)
                    {
                        const int delta = (int) (181.0 * (rx * Lx + ry * Ly)
                                                   * p.specular * 0.01);
                        cr = addBrightness (cr, delta);
                        cg = addBrightness (cg, delta);
                        cb = addBrightness (cb, delta);
                    }
                    else if (spectype == 1)
                    {
                        // Anisotropic brushed sheen: four lobes around the disc.
                        const double th = std::atan2 (ry, rx) - lightRad;
                        const double d  = std::sin (th * 2.0);
                        const double d2 = std::pow (juce::jmax (0.0, (d + 1.0) * 0.5), metalWidth);
                        const double a  = (d + 1.0) * 0.5 * (255.0 - ambient) + ambient
                                            + d2 * specular;

                        const int delta = (int) (a - 256.0);
                        cr = addBrightness (cr, delta);
                        cg = addBrightness (cg, delta);
                        cb = addBrightness (cb, delta);
                    }
                    else
                    {
                        // True sphere: normal (rx, ry, rz), Phong highlight.
                        const double rz = std::sqrt (juce::jmax (0.0, 1.0 - rxy));
                        const double d  = rx * Lx + ry * Ly + rz * Lz;

                        double a = ambient + (255.0 - ambient) * juce::jmax (0.0, d);

                        cr = (int) juce::jlimit (0.0, 255.0, cr * a / 255.0);
                        cg = (int) juce::jlimit (0.0, 255.0, cg * a / 255.0);
                        cb = (int) juce::jlimit (0.0, 255.0, cb * a / 255.0);

                        const double refl = 2.0 * rz * d - Lz;
                        const double s = refl <= 0.0 ? 0.0 : std::pow (refl, phongPower);

                        const int delta = (int) (specular * s);
                        cr = addBrightness (cr, delta);
                        cg = addBrightness (cg, delta);
                        cb = addBrightness (cb, delta);
                    }
                }

                // ---- rim bevel ---------------------------------------------
                if (rthick > 0.0 && rxyem >= 1.0)
                {
                    const double rr = std::sqrt (juce::jmax (1.0e-9, 2.0 * rxy));
                    const double tx = -rx / rr, ty = -ry / rr;

                    const double dot = (p.emboss >= 0.0f)
                                         ? ( tx * Bx +  ty * By + rTZ * Bz)
                                         : (-tx * Bx + -ty * By + rTZ * Bz);

                    const int delta = (int) (dot * 255.0 - 128.0);

                    const int er = addBrightness (p.colour.getRed(),   delta);
                    const int eg = addBrightness (p.colour.getGreen(), delta);
                    const int eb = addBrightness (p.colour.getBlue(),  delta);

                    if (rxye < 1.0)
                    {
                        const double denom = rxye - rxyem;
                        const int blend = (int) juce::jlimit (0.0, 255.0,
                            255.0 * std::abs (std::abs (denom) < 1.0e-9
                                                ? 1.0 : (1.0 - rxyem) / denom));

                        cr = (cr * (256 - blend) + er * blend) / 256;
                        cg = (cg * (256 - blend) + eg * blend) / 256;
                        cb = (cb * (256 - blend) + eb * blend) / 256;
                    }
                    else
                    {
                        cr = er; cg = eg; cb = eb;
                    }
                }

                // ---- alpha: edge fade then analytic antialiasing -----------
                double alpha = 255.0 * p.colour.getFloatAlpha();

                if (rD2 < 1.0 && rxy > rD2)
                    alpha *= juce::jmax (0.0, 1.0 - (rxy - rD2) / (1.0 - rD2));

                if (rxym >= 1.0)
                {
                    const double denom = rxym - rxy;
                    alpha *= (denom > 1.0e-9) ? juce::jlimit (0.0, 1.0, (1.0 - rxy) / denom) : 1.0;
                }

                const int a8 = juce::jlimit (0, 255, (int) std::lround (alpha));

                if (a8 == 0)
                    continue;

                // Store premultiplied.
                auto* dst = pixelAt (data, x, y);
                dst->setARGB ((juce::uint8) a8,
                              (juce::uint8) (cr * a8 / 255),
                              (juce::uint8) (cg * a8 / 255),
                              (juce::uint8) (cb * a8 / 255));
            }
        }
    }

    return img;
}

//==============================================================================
juce::Image LayerRender::renderRing (const PrimitiveSpec& p, int w, int h)
{
    juce::Image img (juce::Image::ARGB, w, h, true);

    {
        juce::Image::BitmapData data (img, juce::Image::BitmapData::readWrite);

        const double cx = w * 0.5, cy = h * 0.5;
        double ax = 1.0, ay = 1.0;

        if (p.aspect > 0.0f) ax = 100.0 / (100.0 - juce::jmin (p.aspect,  99.0f));
        if (p.aspect < 0.0f) ay = 100.0 / (100.0 + juce::jmax (p.aspect, -99.0f));

        // Width is the ring thickness as a percentage of the radius.
        const double thickness = juce::jlimit (1.0, 100.0, (double) p.width) * 0.01;
        const double rMinSize  = juce::jmin (cx, cy) * thickness;

        const double inner  = 1.0 - thickness;
        const double innerA = inner * inner;

        const double fade = juce::jlimit (0.0, 100.0, (double) p.diffuse) * 0.01;

        for (int y = 0; y < h; ++y)
        {
            const double py  = -((double) y + 0.5 - cy) * ay;
            const double rY  = py / cy;
            const double rYE = py / juce::jmax (0.1, cy - rMinSize * ay);

            for (int x = 0; x < w; ++x)
            {
                const double pxv = -((double) x + 0.5 - cx) * ax;
                const double rX  = pxv / cx;
                const double rXE = pxv / juce::jmax (0.1, cx - rMinSize * ax);

                const double r  = rX * rX + rY * rY;
                const double rE = rXE * rXE + rYE * rYE;

                if (r > 1.0 || rE < 1.0)
                    continue;

                double alpha = 255.0 * p.colour.getFloatAlpha();

                // One-pixel analytic feather on both edges of the ring.
                const double px1 = 2.0 / juce::jmax (1.0, juce::jmin (cx, cy));
                alpha *= juce::jlimit (0.0, 1.0, (1.0 - r) / px1);
                alpha *= juce::jlimit (0.0, 1.0, (rE - 1.0) / px1);

                if (fade > 0.0)
                {
                    // Soften across the ring so it reads as a tube rather than a band.
                    const double across = juce::jlimit (0.0, 1.0,
                                            (std::sqrt (r) - inner) / juce::jmax (1.0e-6, thickness));
                    alpha *= 1.0 - fade * std::abs (across * 2.0 - 1.0);
                }

                int cr = p.colour.getRed(), cg = p.colour.getGreen(), cb = p.colour.getBlue();

                if (p.specular > 0.0f)
                {
                    // Highlight running across the ring's thickness.
                    const double v1 = 1.0 / std::sqrt (juce::jmax (1.0e-9, r));
                    const double v2 = 1.0 / std::sqrt (juce::jmax (1.0e-9, rE));
                    double v = 2.0 * (1.0 - v2) / juce::jmax (1.0e-9, v1 - v2);

                    if (v > 1.0)
                        v = 2.0 - v;

                    const int delta = (int) (v * p.specular * 2.55);
                    cr = addBrightness (cr, delta);
                    cg = addBrightness (cg, delta);
                    cb = addBrightness (cb, delta);
                }

                const int a8 = juce::jlimit (0, 255, (int) std::lround (alpha));

                if (a8 == 0)
                    continue;

                auto* dst = pixelAt (data, x, y);
                dst->setARGB ((juce::uint8) a8,
                              (juce::uint8) (cr * a8 / 255),
                              (juce::uint8) (cg * a8 / 255),
                              (juce::uint8) (cb * a8 / 255));
            }
        }
    }

    return img;
}

//==============================================================================
juce::Image LayerRender::rasterisePrimitive (const PrimitiveSpec& p, int w, int h, float scale, float t)
{
    juce::Image img (juce::Image::ARGB, w, h, true);

    if (p.type == PrimType::None)
        return img;

    switch (p.type)
    {
        case PrimType::Circle:      return renderRing (p, w, h);
        case PrimType::CircleFill:  return renderCircleFamily (p, w, h, 0);
        case PrimType::MetalCircle: return renderCircleFamily (p, w, h, 1);
        case PrimType::Sphere:      return renderCircleFamily (p, w, h, 2);
        default: break;
    }

    juce::Graphics g (img);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setColour (p.colour);

    const auto full = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    const auto centre = full.getCentre();

    // Aspect squeezes one axis against the other.
    const float aspect = juce::jlimit (-99.0f, 99.0f, p.aspect);
    const float sx = (aspect >= 0.0f) ? 1.0f : (1.0f + aspect / 100.0f);
    const float sy = (aspect <= 0.0f) ? 1.0f : (1.0f - aspect / 100.0f);

    auto body = full.withSizeKeepingCentre (full.getWidth() * sx, full.getHeight() * sy);

    const float strokeW = juce::jmax (1.0f, juce::jmin (full.getWidth(), full.getHeight())
                                              * juce::jlimit (0.0f, 100.0f, p.width) * 0.005f);

    switch (p.type)
    {
        case PrimType::Image:
        {
            if (p.imageFile.isNotEmpty())
            {
                // Stella: a bare name is a picture in the project, and pictures are read
                // from disk once (a strip renders every layer once per frame).
                auto loaded = cachedLayerImage (LayerImages::resolve (p.imageFile));

                if (loaded.isValid())
                    g.drawImage (loaded, body, juce::RectanglePlacement::centred
                                             | juce::RectanglePlacement::onlyReduceInSize);
            }

            break;
        }

        case PrimType::Circle:
            g.drawEllipse (body.reduced (strokeW * 0.5f), strokeW);
            break;

        case PrimType::CircleFill:
            g.fillEllipse (body);
            break;

        case PrimType::MetalCircle:
        {
            // Radial brushed streaks. Seeded so the texture never boils between
            // frames — an unseeded generator is the classic bug here.
            g.fillEllipse (body);

            juce::Path clip;
            clip.addEllipse (body);

            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (clip);

            juce::Random rnd (0x5EEDBABE);
            const int   count = juce::jmax (48, p.step * 16);
            const float r     = juce::jmax (body.getWidth(), body.getHeight());
            const float light = deg2rad (p.lightDir);

            for (int i = 0; i < count; ++i)
            {
                const float a  = juce::MathConstants<float>::twoPi * (float) i / (float) count;
                const float lit = 0.5f + 0.5f * std::cos (a - light);
                const float amp = 0.03f + 0.06f * rnd.nextFloat();

                g.setColour ((lit > 0.5f ? juce::Colours::white : juce::Colours::black)
                                 .withAlpha (amp * (0.35f + 0.65f * std::abs (lit - 0.5f) * 2.0f)));

                g.drawLine (centre.x, centre.y,
                            centre.x + std::sin (a) * r,
                            centre.y - std::cos (a) * r,
                            juce::jmax (0.6f, r * 0.004f));
            }

            break;
        }

        case PrimType::WaveCircle:
        {
            // KnobMan takes the lobe count from AngleStep (180 / angleStep) and
            // the wave depth from Width — not from Step and Length.
            // Angle 0 means "unset" for the other shapes, so fall back to
            // KnobMan's default of 45, which gives four lobes.
            const float divisor = (std::abs (p.angleStep) < 1.0f) ? 45.0f
                                                                  : juce::jmax (3.0f, std::abs (p.angleStep));
            const int lobes = juce::jlimit (1, 64, (int) std::lround (180.0f / divisor));
            const float depth = juce::jlimit (0.0f, 100.0f, p.width) * 0.01f;
            const float baseR = juce::jmin (body.getWidth(), body.getHeight()) * 0.5f;
            const float phase = 0.0f;

            juce::Path path;

            for (int i = 0; i <= 720; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 720.0f;
                const float r = baseR * (1.0f - depth + depth * std::cos (a * (float) lobes + phase));
                const float x = centre.x + std::sin (a) * r * sx;
                const float y = centre.y - std::cos (a) * r * sy;

                if (i == 0) path.startNewSubPath (x, y);
                else        path.lineTo (x, y);
            }

            path.closeSubPath();

            if (p.fill) g.fillPath (path);
            else        g.strokePath (path, juce::PathStrokeType (strokeW));

            break;
        }

        case PrimType::Sphere:
        {
            // A lit ball: the one primitive whose shading is intrinsic rather
            // than derived from emboss.
            const float light = deg2rad (p.lightDir);
            const float rr    = juce::jmin (body.getWidth(), body.getHeight()) * 0.5f;
            const float amb   = juce::jlimit (0.0f, 1.0f, p.ambient * 0.01f);

            const float lx = centre.x + std::cos (light) * rr * 0.42f;
            const float ly = centre.y - std::sin (light) * rr * 0.42f;

            juce::ColourGradient grad (p.colour.brighter (0.55f), lx, ly,
                                       p.colour.darker (0.70f),
                                       centre.x - std::cos (light) * rr,
                                       centre.y + std::sin (light) * rr, true);
            grad.addColour (0.35, p.colour.brighter (0.10f));
            grad.addColour (0.70, p.colour.darker (0.30f * (1.0f - amb)));

            g.setGradientFill (grad);
            g.fillEllipse (body);

            const float specA = juce::jlimit (0.0f, 1.0f, p.specular * 0.01f);

            if (specA > 0.001f)
            {
                const float sr = rr * juce::jlimit (0.05f, 0.9f, 1.0f - p.specularWidth * 0.009f);

                juce::ColourGradient hot (juce::Colours::white.withAlpha (specA), lx, ly,
                                          juce::Colours::transparentWhite, lx + sr, ly, true);
                juce::Graphics::ScopedSaveState save (g);
                juce::Path clip;
                clip.addEllipse (body);
                g.reduceClipRegion (clip);
                g.setGradientFill (hot);
                g.fillEllipse (lx - sr, ly - sr, sr * 2.0f, sr * 2.0f);
            }

            break;
        }

        case PrimType::Rect:
        {
            const float corner = juce::jlimit (0.0f, 100.0f, p.round) * 0.005f
                                   * juce::jmin (body.getWidth(), body.getHeight());

            const auto outline = body.reduced (strokeW * 0.5f);

            juce::Path ring;

            // A zero-radius addRoundedRectangle is not the same shape to the
            // stroker as a plain rectangle: it leaves a degenerate cubic at
            // every corner, and the joint logic rounds those off. The fill path
            // does not care, which is why the hole came out sharp while the
            // outline did not. Below any rounding at all, hand it a genuine
            // four-point rectangle.
            if (corner > 0.01f)
                ring.addRoundedRectangle (outline, corner);
            else
                ring.addRectangle (outline);

            // Mitred explicitly rather than relying on a default, so the corners
            // stay sharp whichever rendering backend is in play.
            g.strokePath (ring, juce::PathStrokeType (strokeW, juce::PathStrokeType::mitered));
            break;
        }

        case PrimType::RectFill:
        {
            const float corner = juce::jlimit (0.0f, 100.0f, p.round) * 0.005f
                                   * juce::jmin (body.getWidth(), body.getHeight());

            if (corner > 0.01f)
                g.fillRoundedRectangle (body, corner);
            else
                g.fillRect (body);

            break;
        }

        case PrimType::Led:
        {
            // Brightness. Following the frame is what makes this useful in a
            // filmstrip: two frames give off-then-on, sixty-five give a meter,
            // and neither needs a second layer or a mask.
            const float drive = juce::jlimit (0.0f, 1.0f, p.ledIntensity * 0.01f);
            const float lit   = p.ledFollowFrame ? juce::jlimit (0.0f, 1.0f, t) * drive
                                                 : (p.ledOn ? drive : 0.0f);

            // An unlit LED is not a hole. The lens still catches ambient light,
            // which is what keeps off and on reading as the same component.
            const float off = juce::jlimit (0.0f, 1.0f, p.ledOffLevel * 0.01f);

            // Inset, so the halo has somewhere to fade out. A lens filling its
            // own layer leaves the bloom nowhere to go, and the rectangular edge
            // of the image shows through it as a hard border.
            const float sizePct = juce::jlimit (5.0f, 100.0f, p.ledSize) * 0.01f;

            auto lens = body.withSizeKeepingCentre (body.getWidth()  * sizePct,
                                                    body.getHeight() * sizePct);

            if (p.ledShape == LedShape::Circle)
            {
                const float d = juce::jmin (lens.getWidth(), lens.getHeight());
                lens = juce::Rectangle<float> (d, d).withCentre (centre);
            }

            const float minDim = juce::jmin (lens.getWidth(), lens.getHeight());
            const float corner = juce::jlimit (0.0f, 100.0f, p.round) * 0.005f * minDim;

            juce::Path shape;

            switch (p.ledShape)
            {
                case LedShape::Rect:
                    if (corner > 0.01f) shape.addRoundedRectangle (lens, corner);
                    else                shape.addRectangle (lens);
                    break;

                case LedShape::Triangle:
                {
                    const std::vector<juce::Point<float>> pts
                    {
                        { lens.getCentreX(), lens.getY() },
                        { lens.getRight(),   lens.getBottom() },
                        { lens.getX(),       lens.getBottom() }
                    };

                    const float shortest = juce::jmin (pts[0].getDistanceFrom (pts[1]),
                                                       pts[1].getDistanceFrom (pts[2]),
                                                       pts[2].getDistanceFrom (pts[0]));

                    shape = roundPolygonCorners (pts, { 0, 1, 2 },
                                                 juce::jlimit (0.0f, 100.0f, p.round)
                                                   * 0.01f * shortest * 0.5f);
                    break;
                }

                case LedShape::Circle:
                case LedShape::Ellipse:
                case LedShape::numShapes:
                default:
                    shape.addEllipse (lens);
                    break;
            }

            // The house LED stack, per juce-skeuomorph: bloom, lens dome, body
            // glow, lens highlight, bezel. The dome is drawn whether or not the
            // LED is lit — an off LED that vanishes takes the panel's
            // physicality with it.
            const float light = deg2rad (p.lightDir);
            const float rr    = minDim * 0.5f;

            // Both the die and the reflection move with the light, and they have
            // to: a hot centre that stays put while a spot slides around it
            // reads as two unrelated things. What keeps them from merging into
            // the blob they were before is the DISTANCE between them and the
            // difference in kind — the die is a broad soft glow, the reflection
            // a small hard spot three times further out.
            const float dieX = centre.x + std::cos (light) * rr * 0.11f;
            const float dieY = centre.y - std::sin (light) * rr * 0.11f;

            const float sx = centre.x + std::cos (light) * rr * 0.34f;
            const float sy = centre.y - std::sin (light) * rr * 0.34f;

            // --- 1. Bloom ---------------------------------------------------
            // Its RADIUS carries the brightness, not its alpha. A bloom that
            // only fades in opacity reads as a sticker being turned down.
            const float bloom = juce::jlimit (0.0f, 100.0f, p.ledGlow) * 0.01f;

            if (bloom > 0.01f && lit > 0.01f)
            {
                // However far it wants to reach, it has to finish inside the
                // image or the boundary becomes a visible edge. Nearest side,
                // not the corner: a gradient still above zero at ANY edge shows.
                const float room = juce::jmax (1.0f,
                                               juce::jmin (juce::jmin (centre.x, centre.y),
                                                           juce::jmin ((float) w - centre.x,
                                                                       (float) h - centre.y)));

                const float reach = juce::jmin (rr * (1.0f + bloom * 2.5f * lit), room);

                juce::ColourGradient halo (p.colour.withAlpha (0.50f), centre.x, centre.y,
                                           p.colour.withAlpha (0.0f),
                                           centre.x + reach, centre.y, true);
                halo.addColour (0.30, p.colour.withAlpha (0.26f));

                g.setGradientFill (halo);
                g.fillEllipse (juce::Rectangle<float> (reach * 2.0f, reach * 2.0f)
                                   .withCentre (centre));
            }

            {
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (shape);

                // --- 2. Lens dome -------------------------------------------
                // The physical object: a dark tinted dome, shaded from the light
                // direction. Present at every brightness, which is what keeps
                // off and on reading as the same component.
                const auto domeLit  = p.colour.withMultipliedBrightness (0.30f + 0.55f * off);
                const auto domeDark = p.colour.withMultipliedBrightness (0.10f + 0.20f * off)
                                              .darker (0.5f);

                juce::ColourGradient dome (domeLit, sx, sy, domeDark,
                                           centre.x - std::cos (light) * rr,
                                           centre.y + std::sin (light) * rr, true);
                g.setGradientFill (dome);
                g.fillPath (shape);

                // --- 3. Body glow -------------------------------------------
                // The emitter inside the lens: brightest at the die, transparent
                // at the rim. Every LED goes white at the die whatever colour
                // its lens is, so the core is derived rather than picked.
                if (lit > 0.005f)
                {
                    const float coreSize = juce::jlimit (0.02f, 1.0f, p.ledCore * 0.01f);

                    const auto core = p.colour.brighter (0.4f + 1.8f * lit)
                                              .interpolatedWith (juce::Colours::white, 0.7f * lit)
                                              .withAlpha (juce::jlimit (0.0f, 1.0f, 0.35f + 0.65f * lit));

                    juce::ColourGradient emitter (core, dieX, dieY,
                                                  p.colour.withAlpha (0.0f),
                                                  dieX + rr, dieY, true);
                    emitter.addColour (juce::jlimit (0.05, 0.9, (double) coreSize),
                                       p.colour.withAlpha (0.85f * lit));

                    g.setGradientFill (emitter);
                    g.fillPath (shape);
                }

                // --- 3b. Inner shadow ---------------------------------------
                // The lens rim absorbing light. Specular sets how deep it goes
                // and Spec Width how far in it reaches — without it the dome has
                // no thickness and the lens sits flat on the panel.
                const float shade = juce::jlimit (0.0f, 1.0f, p.specular * 0.01f);

                if (shade > 0.01f)
                {
                    const float start = juce::jlimit (0.10f, 0.95f,
                                                      1.0f - juce::jlimit (0.0f, 100.0f,
                                                                           p.specularWidth) * 0.009f);

                    juce::ColourGradient ring (juce::Colours::transparentBlack, centre.x, centre.y,
                                               juce::Colours::black.withAlpha (0.80f * shade),
                                               centre.x + rr, centre.y, true);
                    ring.addColour (start, juce::Colours::transparentBlack);

                    g.setGradientFill (ring);
                    g.fillPath (shape);
                }

                // --- 4. Lens highlight --------------------------------------
                // Glass is spec 1.00 at tightness 60: a small HARD spot, not a
                // soft pool. Broaden it and the lens turns to plastic.
                // Glass is spec 1.00 at tightness 60 — a small hard spot. Most of
                // its alpha holds flat and then falls off late; a smooth ramp
                // from the middle is what makes it read as a plastic ball.
                const float specR = rr * 0.13f;

                juce::ColourGradient spec (juce::Colours::white.withAlpha (0.85f),
                                           sx, sy, juce::Colours::transparentWhite,
                                           sx + specR, sy, true);
                spec.addColour (0.62, juce::Colours::white.withAlpha (0.72f));

                g.setGradientFill (spec);
                g.fillEllipse (sx - specR, sy - specR, specR * 2.0f, specR * 2.0f);

                // The thin bright arc along the rim facing the light. Stroked
                // with a gradient so it fades out around the shape, which makes
                // it work on a rect and a triangle as well as a circle.
                // Runs from the rim facing the light to the rim away from it.
                const juce::Point<float> lightRim (centre.x + std::cos (light) * rr,
                                                   centre.y - std::sin (light) * rr);
                const juce::Point<float> darkRim  (centre.x - std::cos (light) * rr,
                                                   centre.y + std::sin (light) * rr);

                juce::ColourGradient rimLight (juce::Colours::white.withAlpha (0.45f),
                                               lightRim,
                                               juce::Colours::transparentWhite,
                                               darkRim,
                                               false);
                rimLight.addColour (0.35, juce::Colours::white.withAlpha (0.10f));

                g.setGradientFill (rimLight);
                g.strokePath (shape, juce::PathStrokeType (juce::jmax (0.8f, rr * 0.06f)));
            }

            // --- 5. Bezel ---------------------------------------------------
            // Most panel LEDs sit in one, and it gives the off state something
            // to be.
            const float bezel = juce::jlimit (0.0f, 100.0f, p.ledBezel) * 0.01f * minDim * 0.25f;

            if (bezel > 0.3f)
            {
                g.setColour (p.ledBezelColour);
                g.strokePath (shape, juce::PathStrokeType (bezel, juce::PathStrokeType::curved));
            }

            break;
        }

        case PrimType::MeterCircle:
        case PrimType::MeterLines:
        {
            // A scale of tick marks: the ring around a Moog knob, or the same
            // stack beside a slider. Every Nth tick is drawn long, which is what
            // turns a row of marks into something you can read a value off.
            const int count = juce::jlimit (1, 512, p.step);
            const int every = juce::jlimit (1, 32, p.meterEvery);

            const float minDim = juce::jmin (body.getWidth(), body.getHeight());

            // Two reaches from a shared ring: inward toward the knob, outward
            // toward the boundary. A minor tick shrinks toward the ring from
            // both sides, so every tick stays centred on the same line.
            const float inLen  = minDim * 0.5f * juce::jlimit (0.0f, 100.0f, p.length) * 0.01f;
            const float outLen = minDim * 0.5f * juce::jlimit (0.0f, 100.0f, p.meterOuterLen) * 0.01f;

            const float minorScale = juce::jlimit (0.0f, 100.0f, p.meterMinorLen) * 0.01f;

            const float longLen  = juce::jmax (0.5f, inLen + outLen);
            const float shortLen = juce::jmax (0.5f, (inLen + outLen) * minorScale);

            const float thick = juce::jmax (0.6f, minDim
                                                    * juce::jlimit (0.0f, 100.0f, p.width) * 0.0006f);
            const float minorThick = thick * juce::jlimit (0.0f, 100.0f, p.meterMinorWidth) * 0.01f;

            // Round is a percentage of the tick's own half-thickness, so 100
            // caps the ends off instead of depending on how long it is.
            const float roundPct = juce::jlimit (0.0f, 100.0f, p.round) * 0.01f;

            // Labels, one per long tick, left to right. Empty tokens are kept so
            // a lone "@" holds its position and nothing after it shifts along.
            juce::StringArray labels;
            labels.addTokens (p.meterLabels, ",", "");

            for (auto& l : labels)
                l = l.trim();

            const bool hasLabels = ! labels.isEmpty() && p.meterLabels.isNotEmpty();

            const float labelGap  = minDim * 0.5f
                                      * juce::jlimit (0.0f, 100.0f, p.meterLabelGap) * 0.01f;
            const float labelSize = juce::jmax (4.0f, p.fontSize) * scale;

            int labelIndex = 0;

            if (hasLabels)
            {
                g.setColour (p.colour);
                g.setFont (juce::Font (juce::FontOptions (labelSize)));
            }

            // Drawn upright wherever it lands. A value that rotates with its
            // tick is unreadable at the bottom of the sweep, which is why no
            // hardware does it.
            auto drawLabel = [&] (juce::Point<float> at)
            {
                if (! hasLabels || labelIndex >= labels.size())
                {
                    ++labelIndex;
                    return;
                }

                const auto text = labels[labelIndex++];

                if (text.isEmpty() || text == "@")
                    return;

                const float boxW = labelSize * 4.0f;
                const float boxH = labelSize * 1.6f;

                g.drawText (text,
                            juce::Rectangle<float> (boxW, boxH).withCentre (at),
                            juce::Justification::centred, false);
            };

            // Takes plain width and height, and takes the radius from whichever
            // is smaller — a tick lying on its side has to round its ends, not
            // its long edges.
            auto tickPath = [roundPct] (float x, float y, float w, float h)
            {
                juce::Path t;
                const float r = roundPct * juce::jmin (w, h) * 0.5f;

                if (r > 0.01f) t.addRoundedRectangle (x, y, w, h, r);
                else           t.addRectangle (x, y, w, h);

                return t;
            };

            if (p.type == PrimType::MeterCircle)
            {
                // Start and end are the mask: the arc the scale occupies. The
                // classic knob sweep is -135 to +135, which is why that is the
                // default rather than a full circle.
                // The ring sits inward by the outward reach, so the far ends of
                // the longest ticks land exactly on the layer's edge however the
                // two lengths are split. Outward reach can never run off.
                const float ringR = minDim * 0.5f - outLen;

                const float start = p.meterStart;
                const float end   = p.meterEnd;

                for (int i = 0; i < count; ++i)
                {
                    const bool  major = (i % every) == 0;
                    const float w     = major ? thick : minorThick;

                    const float out = major ? outLen : outLen * minorScale;
                    const float in  = major ? inLen  : inLen  * minorScale;
                    const float len = juce::jmax (0.5f, in + out);

                    const float f = count > 1 ? (float) i / (float) (count - 1) : 0.0f;
                    const float a = deg2rad (start + (end - start) * f);

                    // Built pointing up, spanning from the outer end down past
                    // the ring, then swung round.
                    auto t = tickPath (-w * 0.5f, -(ringR + out), w, len);

                    g.fillPath (t, juce::AffineTransform::rotation (a)
                                       .translated (centre.x, centre.y));

                    if (major && hasLabels)
                    {
                        const float rad = p.meterLabelsInside ? ringR - in - labelGap
                                                              : ringR + out + labelGap;

                        drawLabel ({ centre.x + std::sin (a) * rad,
                                     centre.y - std::cos (a) * rad });
                    }
                }
            }
            else
            {
                // Stacked down the layer and growing from its left edge. Angle
                // turns the whole scale, so a horizontal slider is the same
                // primitive rotated.
                const auto transform = juce::AffineTransform::rotation (deg2rad (p.angleStep),
                                                                        centre.x, centre.y);

                for (int i = 0; i < count; ++i)
                {
                    const bool  major = (i % every) == 0;
                    const float len   = major ? longLen : shortLen;
                    const float w     = major ? thick   : minorThick;

                    const float f = count > 1 ? (float) i / (float) (count - 1) : 0.5f;
                    const float y = body.getY() + body.getHeight() * f;

                    auto t = tickPath (body.getX(), y - w * 0.5f, len, w);

                    g.fillPath (t, transform);

                    if (major && hasLabels)
                    {
                        const float lx = p.meterLabelsInside ? body.getX() - labelGap
                                                             : body.getX() + len + labelGap;

                        // Position follows the rotation, the text itself does not.
                        drawLabel (juce::Point<float> (lx, y).transformedBy (transform));
                    }
                }
            }

            break;
        }

        case PrimType::Triangle:
        {
            // Apex up, widening downward: Length is the height, Width the base,
            // both as percentages, matching KnobMan.
            const float halfBase = body.getWidth()  * juce::jlimit (0.0f, 100.0f, p.width)  * 0.005f;
            const float height   = body.getHeight() * juce::jlimit (0.0f, 100.0f, p.length) * 0.01f;
            const float halfH    = height * 0.5f;

            const std::vector<juce::Point<float>> points
            {
                { centre.x,            centre.y - halfH },
                { centre.x + halfBase, centre.y + halfH },
                { centre.x - halfBase, centre.y + halfH }
            };

            // Every vertex of a triangle is a corner, so all three are named.
            // Same filleting the spokes use: each radius is limited by half the
            // shorter edge it sits between, so two corners can never overlap.
            const float round = juce::jlimit (0.0f, 100.0f, p.round) * 0.01f;

            const float shortest = juce::jmin (points[0].getDistanceFrom (points[1]),
                                               points[1].getDistanceFrom (points[2]),
                                               points[2].getDistanceFrom (points[0]));

            auto tri = roundPolygonCorners (points, { 0, 1, 2 }, round * shortest * 0.5f);

            auto transform = juce::AffineTransform();

            if (std::abs (p.angleStep) > 0.01f)
            {
                // Rotating inside fixed bounds would shear the corners off, so
                // shrink to the inscribed circle first.
                const float circum = juce::jmax (halfH, std::sqrt (halfBase * halfBase
                                                                     + halfH * halfH));
                const float limit  = juce::jmin (body.getWidth(), body.getHeight()) * 0.5f;
                const float shrink = (circum > limit && circum > 0.0f) ? limit / circum : 1.0f;

                transform = juce::AffineTransform::translation (-centre.x, -centre.y)
                                .scaled (shrink, shrink)
                                .rotated (deg2rad (p.angleStep))
                                .translated (centre.x, centre.y);
            }

            if (p.fill) g.fillPath (tri, transform);
            else        g.strokePath (tri, juce::PathStrokeType (strokeW), transform);

            break;
        }

        case PrimType::Line:
        {
            const float len = juce::jlimit (0.0f, 100.0f, p.length) * 0.01f
                                * juce::jmin (body.getWidth(), body.getHeight());
            const float corner = juce::jlimit (0.0f, 100.0f, p.round) * 0.005f * strokeW;

            juce::Path line;

            // Same reasoning as Rect: no rounding means a real rectangle, not a
            // rounded one with the radius set to zero.
            if (corner > 0.01f)
                line.addRoundedRectangle (centre.x - strokeW * 0.5f, centre.y - len * 0.5f,
                                          strokeW, len, corner);
            else
                line.addRectangle (centre.x - strokeW * 0.5f, centre.y - len * 0.5f,
                                   strokeW, len);
            line.applyTransform (juce::AffineTransform::rotation (deg2rad (p.angleStep),
                                                                 centre.x, centre.y));
            g.fillPath (line);
            break;
        }

        case PrimType::RadiateLine:
        {
            const int   n     = juce::jlimit (1, 512, p.step);
            const float rOut  = juce::jmin (body.getWidth(), body.getHeight()) * 0.5f;
            const float len   = juce::jlimit (0.0f, 100.0f, p.length) * 0.01f * rOut;
            const float rIn   = juce::jmax (0.0f, rOut - len);
            const float pitch = juce::MathConstants<float>::twoPi / (float) n;
            const float halfW = pitch * juce::jlimit (0.02f, 0.49f,
                                                      juce::jlimit (0.0f, 100.0f, p.width) * 0.005f);
            const float base  = deg2rad (p.angleStep);

            // Skew tapers the outer end against the inner one. At +100 the tip
            // is a point and the spoke is a triangle; at -100 it is the root
            // that pinches and the spoke fans outward.
            const float skew   = juce::jlimit (-100.0f, 100.0f, p.raySkew) * 0.01f;
            const float wOuter = halfW * juce::jmax (0.0f, 1.0f - juce::jmax (0.0f,  skew));
            const float wInner = halfW * juce::jmax (0.0f, 1.0f - juce::jmax (0.0f, -skew));

            // Bend sweeps the spoke round as it travels out. Zero leaves it
            // radial; wind it up and the whole thing reads as a shuriken.
            const float bend = deg2rad (juce::jlimit (-90.0f, 90.0f, p.rayBend));

            // Built as a strip of quads along the spoke rather than one quad,
            // because a bent spoke is a curve and a four-point path cannot be.
            constexpr int kSegments = 16;

            auto pointAt = [rIn, rOut, bend, wInner, wOuter] (float t, float side)
            {
                const float r     = rIn + (rOut - rIn) * t;
                const float sweep = bend * t;
                const float half  = wInner + (wOuter - wInner) * t;
                const float a     = sweep + side * half;

                return juce::Point<float> (std::sin (a) * r, -std::cos (a) * r);
            };

            std::vector<juce::Point<float>> outline;
            outline.reserve ((size_t) (2 * kSegments + 2));

            for (int i = 0; i <= kSegments; ++i)
                outline.push_back (pointAt ((float) i / (float) kSegments, -1.0f));

            for (int i = kSegments; i >= 0; --i)
                outline.push_back (pointAt ((float) i / (float) kSegments, 1.0f));

            // The four corners a spoke actually has: root and tip on each side.
            // Everything between them is the curve of a bent side and must not
            // be treated as a corner.
            const std::vector<int> corners { 0, kSegments, kSegments + 1, 2 * kSegments + 1 };

            // Scaled to the spoke's WIDTH, not its length. Anything bigger and
            // every corner saturates against its own edge at a low percentage
            // and the rest of the slider does nothing. At 100% the fillet is a
            // full half-width, so the ends cap off into a stadium.
            const float round  = juce::jlimit (0.0f, 100.0f, p.rayRoundness) * 0.01f;
            const float radius = round * juce::jmax (wOuter * rOut, wInner * rIn);

            auto spoke = roundPolygonCorners (outline, corners, radius);

            for (int i = 0; i < n; ++i)
                g.fillPath (spoke, juce::AffineTransform::rotation (base + pitch * (float) i)
                                       .translated (centre.x, centre.y));

            break;
        }

        case PrimType::HLines:
        case PrimType::VLines:
        {
            const int   n    = juce::jlimit (1, 512, p.step);
            const bool  horz = (p.type == PrimType::HLines);
            const float span = horz ? body.getHeight() : body.getWidth();
            const float pitch = span / (float) n;
            const float thick = juce::jmax (1.0f, pitch * juce::jlimit (0.0f, 100.0f, p.width) * 0.01f);

            for (int i = 0; i < n; ++i)
            {
                const float pos = (horz ? body.getY() : body.getX()) + pitch * ((float) i + 0.5f);

                if (horz) g.fillRect (body.getX(), pos - thick * 0.5f, body.getWidth(), thick);
                else      g.fillRect (pos - thick * 0.5f, body.getY(), thick, body.getHeight());
            }

            break;
        }

        case PrimType::Text:
        {
            // Point size is absolute, unlike every other parameter here, so it
            // has to be scaled explicitly or text shrinks as resolution rises.
            auto options = juce::FontOptions (juce::jmax (4.0f, p.fontSize) * scale);

            if (p.bold && p.italic)  options = options.withStyle ("Bold Italic");
            else if (p.bold)         options = options.withStyle ("Bold");
            else if (p.italic)       options = options.withStyle ("Italic");

            g.setFont (juce::Font (options));

            const auto just = (p.textAlign == 1) ? juce::Justification::centredLeft
                            : (p.textAlign == 2) ? juce::Justification::centredRight
                                                 : juce::Justification::centred;

            g.drawText (p.text, body.toNearestInt(), just, false);
            break;
        }

        case PrimType::Shape:
        {
            juce::Path shape;

            // The inventory first. Falling back to the embedded points keeps
            // documents made before the Shapes tab existed — and imported .knob
            // shapes, which arrive as plain polygons — drawing as they did.
            if (p.shapeName.isNotEmpty())
            {
                auto doc = ShapeLibrary::load (p.shapeName);

                if (! doc.isEmpty())
                    shape = doc.buildPath();
            }

            if (shape.isEmpty() && p.shapePoints.size() >= 2)
            {
                for (size_t i = 0; i < p.shapePoints.size(); ++i)
                {
                    if (i == 0) shape.startNewSubPath (p.shapePoints[i]);
                    else        shape.lineTo (p.shapePoints[i]);
                }

                shape.closeSubPath();
            }

            if (! shape.isEmpty())
            {
                // Authored in 0..1, drawn into the layer's body, then turned
                // about that body's centre — the same pivot Triangle and Line
                // rotate around, so a shape behaves like the other primitives
                // rather than being the one that ignores Angle.
                shape.applyTransform (juce::AffineTransform::scale (body.getWidth(),
                                                                    body.getHeight())
                                          .translated (body.getX(), body.getY())
                                          .rotated (deg2rad (p.angleStep),
                                                    centre.x, centre.y));

                if (p.fill) g.fillPath (shape);
                else        g.strokePath (shape,
                                          juce::PathStrokeType (strokeW,
                                                                juce::PathStrokeType::curved));
            }

            break;
        }

        case PrimType::None:
        case PrimType::numTypes:
        default:
            break;
    }

    return img;
}

//==============================================================================
void LayerRender::applyGloss (juce::Image& img, const PrimitiveSpec& p)
{
    const float strength = juce::jlimit (0.0f, 1.0f, p.gloss * 0.01f);

    if (strength < 0.005f)
        return;

    const int w = img.getWidth();
    const int h = img.getHeight();

    juce::Image::BitmapData data (img, juce::Image::BitmapData::readWrite);

    // Bounding box of the drawn pixels, so the ramp spans the shape rather
    // than the canvas.
    int minX = w, minY = h, maxX = -1, maxY = -1;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            if (pixelAt (data, x, y)->getAlpha() == 0)
                continue;

            minX = juce::jmin (minX, x); maxX = juce::jmax (maxX, x);
            minY = juce::jmin (minY, y); maxY = juce::jmax (maxY, y);
        }
    }

    if (maxX < minX || maxY < minY)
        return;

    const float cx = (float) (minX + maxX) * 0.5f;
    const float cy = (float) (minY + maxY) * 0.5f;
    const float bw = (float) (maxX - minX + 1);
    const float bh = (float) (maxY - minY + 1);

    // Ramp axis runs from the lit side toward the shadow side.
    const float a  = deg2rad (p.lightDir);
    const float ax = -std::sin (a);
    const float ay =  std::cos (a);

    const float extent = juce::jmax (1.0f, bw * std::abs (ax) + bh * std::abs (ay));

    // The curve. Note the lift at 0.70 — it is not monotonic, and that bounce
    // is the whole point: remove it and the surface reads as matte paper.
    struct Stop { float t, mul; };
    static const Stop curve[]
    {
        { 0.00f, 1.62f },
        { 0.16f, 1.30f },
        { 0.38f, 1.04f },
        { 0.54f, 0.78f },
        { 0.70f, 0.94f },
        { 0.88f, 0.60f },
        { 1.00f, 0.45f },
    };

    constexpr int numStops = (int) (sizeof (curve) / sizeof (Stop));

    auto sampleCurve = [] (float t) -> float
    {
        t = juce::jlimit (0.0f, 1.0f, t);

        for (int i = 1; i < numStops; ++i)
        {
            if (t > curve[i].t)
                continue;

            const float span = curve[i].t - curve[i - 1].t;
            const float f = (span > 0.0f) ? (t - curve[i - 1].t) / span : 0.0f;
            return curve[i - 1].mul + (curve[i].mul - curve[i - 1].mul) * f;
        }

        return curve[numStops - 1].mul;
    };

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            auto* px = pixelAt (data, x, y);
            const juce::uint8 alpha = px->getAlpha();

            if (alpha == 0)
                continue;

            const float proj = ((float) x - cx) * ax + ((float) y - cy) * ay;
            const float t    = 0.5f + proj / extent;

            const float mul = 1.0f + (sampleCurve (t) - 1.0f) * strength;

            px->setARGB (alpha,
                         scaleComponent (px->getRed(),   mul, alpha),
                         scaleComponent (px->getGreen(), mul, alpha),
                         scaleComponent (px->getBlue(),  mul, alpha));
        }
    }
}

//==============================================================================
void LayerRender::applyTexture (juce::Image& img, const PrimitiveSpec& p)
{
    if (p.textureMode == TextureMode::Off || p.textureName.isEmpty() || ! img.isValid())
        return;

    const float depth = juce::jlimit (0.0f, 1.0f, p.textureDepth * 0.01f);

    if (depth < 0.005f)
        return;

    const int w = img.getWidth();
    const int h = img.getHeight();

    // Asked for at the layer's own render size, so the tile lines up 1:1 and
    // stays sharp when the preview renders at 3-4x.
    auto tex = TextureLibrary::image (p.textureName, w, h, p.textureScale, p.textureAngle);

    if (! tex.isValid())
        return;

    juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);
    juce::Image::BitmapData src (tex, juce::Image::BitmapData::readOnly);

    if (p.textureMode == TextureMode::Fill)
    {
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                auto c = dst.getPixelColour (x, y);

                if (c.getAlpha() == 0)
                    continue;

                const auto t = src.getPixelColour (x, y);

                // Blend toward the pattern but keep the layer's own alpha, so
                // the shape stays the shape. A greyscale pattern lands as tone
                // over the layer colour; a colour image lands as itself.
                dst.setPixelColour (x, y,
                                    juce::Colour::fromFloatRGBA (
                                        c.getFloatRed()   + (t.getFloatRed()   - c.getFloatRed())   * depth,
                                        c.getFloatGreen() + (t.getFloatGreen() - c.getFloatGreen()) * depth,
                                        c.getFloatBlue()  + (t.getFloatBlue()  - c.getFloatBlue())  * depth,
                                        c.getFloatAlpha()));
            }
        }

        return;
    }

    // Bump. The texture is a height field: take its gradient and shade against
    // the same light the emboss pass uses, so a textured face and an embossed
    // edge are lit from one place rather than two.
    const float light = deg2rad (p.lightDir);
    const float lx =  std::cos (light);
    const float ly = -std::sin (light);

    auto heightAt = [&src, w, h] (int x, int y) noexcept
    {
        const auto c = src.getPixelColour (juce::jlimit (0, w - 1, x),
                                           juce::jlimit (0, h - 1, y));

        return 0.299f * c.getFloatRed() + 0.587f * c.getFloatGreen() + 0.114f * c.getFloatBlue();
    };

    const float strength = depth * 6.0f;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            auto* px = pixelAt (dst, x, y);
            const auto alpha = px->getAlpha();

            if (alpha == 0)
                continue;

            const float gx = heightAt (x + 1, y) - heightAt (x - 1, y);
            const float gy = heightAt (x, y + 1) - heightAt (x, y - 1);

            const float shade = juce::jlimit (0.25f, 1.9f, 1.0f + strength * (gx * lx + gy * ly));

            px->setARGB (alpha,
                         scaleComponent (px->getRed(),   shade, alpha),
                         scaleComponent (px->getGreen(), shade, alpha),
                         scaleComponent (px->getBlue(),  shade, alpha));
        }
    }
}

//==============================================================================
namespace
{
    /** Exact Euclidean distance transform (Felzenszwalb & Huttenlocher).

        Contract is the same as before: cells already at 0 are the seeds, every
        other cell must start large; on return each cell holds its true distance
        to the nearest seed.

        This replaced a chamfer transform, and that swap is the fix for bevels
        coming out bent or out of proportion. A chamfer walks the grid with a
        step of 1 orthogonally and sqrt(2) diagonally, which is up to about 8%
        short of the truth at intermediate angles — so the band around a circle
        came out wider on the axes than on the diagonals and the bevel read as
        subtly octagonal. This one measures the real distance and is still
        O(pixels): two sweeps of a 1-D lower envelope of parabolas, columns then
        rows, on squared distances. */
    void euclideanDistance (std::vector<float>& field, int w, int h)
    {
        constexpr float kBig = 1.0e10f;

        const int maxDim = juce::jmax (w, h);

        std::vector<float> f ((size_t) maxDim), d ((size_t) maxDim), z ((size_t) maxDim + 2);
        std::vector<int>   v ((size_t) maxDim);

        auto transform1d = [&f, &d, &z, &v] (int n)
        {
            int k = 0;
            v[0] = 0;
            z[0] = -kBig;
            z[1] =  kBig;

            auto intersect = [&f, &v] (int q, int k2)
            {
                const int p = v[(size_t) k2];

                return ((f[(size_t) q] + (float) (q * q)) - (f[(size_t) p] + (float) (p * p)))
                         / (float) (2 * q - 2 * p);
            };

            for (int q = 1; q < n; ++q)
            {
                float s = intersect (q, k);

                while (k > 0 && s <= z[(size_t) k])
                    s = intersect (q, --k);

                ++k;
                v[(size_t) k] = q;
                z[(size_t) k] = s;
                z[(size_t) k + 1] = kBig;
            }

            k = 0;

            for (int q = 0; q < n; ++q)
            {
                while (z[(size_t) k + 1] < (float) q)
                    ++k;

                const float dx = (float) (q - v[(size_t) k]);
                d[(size_t) q] = dx * dx + f[(size_t) v[(size_t) k]];
            }
        };

        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < h; ++y)
                f[(size_t) y] = juce::jmin (kBig, field[(size_t) y * (size_t) w + (size_t) x]);

            transform1d (h);

            for (int y = 0; y < h; ++y)
                field[(size_t) y * (size_t) w + (size_t) x] = d[(size_t) y];
        }

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
                f[(size_t) x] = field[(size_t) y * (size_t) w + (size_t) x];

            transform1d (w);

            for (int x = 0; x < w; ++x)
                field[(size_t) y * (size_t) w + (size_t) x]
                    = std::sqrt (juce::jmax (0.0f, d[(size_t) x]));
        }
    }
}

void LayerRender::applyStroke (juce::Image& img, const PrimitiveSpec& p, float scale,
                               bool* wasClipped)
{
    if (! p.strokeEnabled || ! img.isValid() || p.strokeColour.getAlpha() == 0)
        return;

    const float width = juce::jlimit (0.25f, 40.0f, p.strokeWidth) * juce::jmax (0.25f, scale);

    if (width < 0.2f)
        return;

    const int w = img.getWidth();
    const int h = img.getHeight();
    const size_t n = (size_t) w * (size_t) h;

    constexpr float kFar = 1.0e9f;

    std::vector<float> outside (n, kFar);   // distance to the nearest solid pixel
    std::vector<float> inside  (n, kFar);   // distance to the nearest empty pixel
    std::vector<juce::uint8> alpha (n, 0);

    bool anySolid = false, anyEmpty = false;

    {
        juce::Image::BitmapData data (img, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const auto a = pixelAt (data, x, y)->getAlpha();
                const size_t i = (size_t) y * (size_t) w + (size_t) x;

                alpha[i] = a;

                if (a >= 128) { outside[i] = 0.0f; anySolid = true; }
                else          { inside[i]  = 0.0f; anyEmpty = true; }
            }
        }
    }

    // Nothing drawn, or the shape fills the canvas edge to edge — either way
    // there is no silhouette to trace.
    if (! anySolid || ! anyEmpty)
        return;

    euclideanDistance (outside, w, h);
    euclideanDistance (inside,  w, h);

    // How much room the shape gives an outline in each direction: how far out
    // there is to go, and how deep the shape is at its thickest.
    float furthest = 0.0f, deepest = 0.0f;

    for (size_t i = 0; i < n; ++i)
    {
        const float v = outside[i] - inside[i];
        furthest = juce::jmax (furthest,  v);
        deepest  = juce::jmax (deepest,  -v);
    }

    // The band, as a signed distance: negative inside the shape, positive out.
    float lo = 0.0f, hi = 0.0f;

    switch (p.strokePosition)
    {
        case StrokePosition::Inside:  lo = -width;        hi = 0.0f;         break;
        case StrokePosition::Centre:  lo = -width * 0.5f; hi = width * 0.5f; break;
        case StrokePosition::Outside:
        default:                      lo = 0.0f;          hi = width;        break;
    }

    // An outline that eats almost all of a thin shape leaves a core a pixel or
    // so across, and a core that thin on a rotated bar breaks up into a ragged
    // diagonal — the outline looks like it corrupted the shape. Past that point
    // swallow the shape instead: a solid outline is what was asked for and it
    // is what it looks like.
    const bool swallowsShape = -lo > 0.0f && (deepest + lo) < 1.5f;

    if (swallowsShape)
        lo = -1.0e6f;

    // Not clamped the way a shadow's fade is — an outline is a band of a stated
    // width and silently thinning it would be worse. It says so instead.
    if (wasClipped != nullptr
          && (swallowsShape
                || (p.strokePosition != StrokePosition::Inside
                      && (p.strokePosition == StrokePosition::Centre ? width * 0.5f : width) > furthest)))
    {
        *wasClipped = true;
    }

    const float sr = p.strokeColour.getFloatRed();
    const float sg = p.strokeColour.getFloatGreen();
    const float sb = p.strokeColour.getFloatBlue();
    const float sa = p.strokeColour.getFloatAlpha();

    juce::Image::BitmapData data (img, juce::Image::BitmapData::readWrite);

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const size_t i = (size_t) y * (size_t) w + (size_t) x;

            // Signed distance to the edge. On a pixel the transform already puts
            // ON the boundary, the shape's own antialiasing is the better
            // estimate — half coverage means the edge runs through the middle
            // of this pixel.
            //
            // The proximity test is the whole point, and leaving it out is what
            // made every outline come out a rectangle. A CircleFill with any
            // rim fade has a wide annulus sitting at partial alpha; without the
            // test, every one of those pixels claimed to be half a pixel from
            // an edge, the band swallowed the lot, and the "outline" became a
            // solid blob clipped to the canvas — which is to say, a rectangle.
            float sd = outside[i] - inside[i];

            // Refine, never overrule. On a pixel the field already puts on the
            // boundary the shape's own antialiasing is the better estimate, but
            // it must not move the edge more than half a pixel from where the
            // field put it — on a thin shape every pixel is partly covered, and
            // letting the alpha win outright drags the whole interior onto the
            // boundary and lets the outline bleed through it.
            if (alpha[i] > 0 && alpha[i] < 255 && std::abs (sd) <= 1.0f)
                sd = juce::jlimit (sd - 0.5f, sd + 0.5f, 0.5f - (float) alpha[i] / 255.0f);

            // A soft step at each end of the band, multiplied together. One
            // pixel of falloff, which is what keeps the outline from stair-
            // stepping without blurring it.
            const float coverage = juce::jlimit (0.0f, 1.0f, 0.5f + (sd - lo))
                                 * juce::jlimit (0.0f, 1.0f, 0.5f + (hi - sd));

            if (coverage <= 0.002f)
                continue;

            const float ca = coverage * sa;

            const auto base = data.getPixelColour (x, y);

            // Source-over: the stroke sits on top of the fill, which is what
            // makes an Inside stroke read as an inset border rather than a tint.
            const float ba = base.getFloatAlpha();
            const float outA = ca + ba * (1.0f - ca);

            if (outA <= 0.0001f)
                continue;

            auto blend = [ca, ba, outA] (float strokeC, float baseC) noexcept
            {
                return (strokeC * ca + baseC * ba * (1.0f - ca)) / outA;
            };

            data.setPixelColour (x, y,
                                 juce::Colour::fromFloatRGBA (blend (sr, base.getFloatRed()),
                                                              blend (sg, base.getFloatGreen()),
                                                              blend (sb, base.getFloatBlue()),
                                                              outA));
        }
    }
}

//==============================================================================
namespace
{
    /** Signed distance to the layer's edge: negative inside, positive outside,
        zero on it. Same construction the outline uses — two chamfer passes,
        refined at partly covered pixels by the shape's own antialiasing, which
        is a better estimate of where the edge falls than the integer grid. */
    bool buildSignedDistance (juce::Image::BitmapData& data, int w, int h,
                              std::vector<float>& sd, std::vector<juce::uint8>& alpha)
    {
        constexpr float kFar = 1.0e9f;

        const size_t n = (size_t) w * (size_t) h;

        std::vector<float> outside (n, kFar), inside (n, kFar);
        alpha.assign (n, 0);

        bool anySolid = false, anyEmpty = false;

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const auto a = pixelAt (data, x, y)->getAlpha();
                const size_t i = (size_t) y * (size_t) w + (size_t) x;

                alpha[i] = a;

                if (a >= 128) { outside[i] = 0.0f; anySolid = true; }
                else          { inside[i]  = 0.0f; anyEmpty = true; }
            }
        }

        if (! anySolid || ! anyEmpty)
            return false;

        euclideanDistance (outside, w, h);
        euclideanDistance (inside,  w, h);

        sd.resize (n);

        for (size_t i = 0; i < n; ++i)
        {
            float v = outside[i] - inside[i];

            // Only refine pixels the transform already places on the boundary,
            // and only by half a pixel. Applied to every partly transparent
            // pixel this turns a soft rim into a field of false edges; allowed
            // to overrule the field, it drags the interior of a thin shape onto
            // the boundary too.
            if (alpha[i] > 0 && alpha[i] < 255 && std::abs (v) <= 1.0f)
                v = juce::jlimit (v - 0.5f, v + 0.5f, 0.5f - (float) alpha[i] / 255.0f);

            sd[i] = v;
        }

        return true;
    }

    inline float bevelProfile (float u, BevelTechnique technique) noexcept
    {
        u = juce::jlimit (0.0f, 1.0f, u);

        // Chisel is a straight ramp, which is what leaves a hard crease at the
        // top of the bevel. Smooth eases at both ends and reads as rounded.
        return technique == BevelTechnique::Smooth ? u * u * (3.0f - 2.0f * u) : u;
    }

    inline float blendChannel (BlendMode mode, float base, float src) noexcept
    {
        switch (mode)
        {
            case BlendMode::Multiply:    return base * src;
            case BlendMode::Screen:      return 1.0f - (1.0f - base) * (1.0f - src);
            case BlendMode::Overlay:     return base < 0.5f ? 2.0f * base * src
                                                            : 1.0f - 2.0f * (1.0f - base) * (1.0f - src);
            case BlendMode::LinearDodge: return juce::jmin (1.0f, base + src);
            case BlendMode::LinearBurn:  return juce::jmax (0.0f, base + src - 1.0f);
            case BlendMode::Normal:
            case BlendMode::numModes:
            default:                     return src;
        }
    }
}

void LayerRender::applyBevel (juce::Image& img, const PrimitiveSpec& p, float scale,
                              float lightAngleDeg, float lightAltitudeDeg)
{
    const auto& b = p.bevel;

    if (! b.enabled || ! img.isValid())
        return;

    const float size = juce::jmax (0.5f, b.size * juce::jmax (0.25f, scale));

    if (size < 0.5f)
        return;

    const int w = img.getWidth();
    const int h = img.getHeight();
    const size_t n = (size_t) w * (size_t) h;

    std::vector<float> sd;
    std::vector<juce::uint8> alpha;

    {
        juce::Image::BitmapData data (img, juce::Image::BitmapData::readOnly);

        if (! buildSignedDistance (data, w, h, sd, alpha))
            return;
    }

    // --- signed distance -> height field ------------------------------------
    // Every style is a different band on sd, which is why one piece of code
    // covers all five without knowing what shape it is looking at.
    std::vector<float> height (n, 0.0f);

    const float strokeW = juce::jmax (0.5f, p.strokeWidth * juce::jmax (0.25f, scale));

    // Both contours become 257-entry lookups. Contour::at walks its point list,
    // and this runs per pixel over an image that can be 400x400 at preview
    // scale — the table costs nothing and saves that walk 160,000 times.
    const bool  useContour   = b.contourEnabled;
    const float contourRange = juce::jlimit (0.01f, 1.0f, b.contourRange * 0.01f);

    std::vector<float> contourLut;

    if (useContour)
    {
        contourLut.resize (257);

        for (int i = 0; i <= 256; ++i)
            contourLut[(size_t) i] = b.bevelContour.at ((float) i / 256.0f);
    }

    for (size_t i = 0; i < n; ++i)
    {
        const float d = sd[i];
        float value = 0.0f;

        switch (b.style)
        {
            case BevelStyle::InnerBevel:
                value = d >= 0.0f ? 0.0f : bevelProfile (-d / size, b.technique);
                break;

            case BevelStyle::OuterBevel:
                value = d <= 0.0f ? 1.0f : bevelProfile (1.0f - d / size, b.technique);
                break;

            case BevelStyle::Emboss:
                value = bevelProfile ((size - d) / (2.0f * size), b.technique);
                break;

            case BevelStyle::PillowEmboss:
            {
                // A ridge just outside the edge and a groove just inside it —
                // the shape reads as pressed into the surface rather than out.
                const float outer = bevelProfile (1.0f - juce::jmax (0.0f, d) / size, b.technique);
                const float inner = bevelProfile (juce::jmax (0.0f, -d) / size, b.technique);
                value = 0.5f + 0.5f * (outer - inner);
                break;
            }

            case BevelStyle::StrokeEmboss:
            {
                // Bevels the outline instead of the fill, so it only says
                // anything when the outline is switched on.
                float lo = 0.0f, hi = strokeW;

                switch (p.strokePosition)
                {
                    case StrokePosition::Inside: lo = -strokeW;       hi = 0.0f;           break;
                    case StrokePosition::Centre: lo = -strokeW*0.5f;  hi = strokeW * 0.5f; break;
                    default: break;
                }

                const float mid  = (lo + hi) * 0.5f;
                const float half = juce::jmax (0.5f, (hi - lo) * 0.5f);

                value = bevelProfile (1.0f - std::abs (d - mid) / half, b.technique);
                break;
            }

            case BevelStyle::numStyles:
            default:
                break;
        }

        // Contour reshapes the profile across the band. Range spreads the curve
        // over a fraction of the bevel and holds its end value beyond that,
        // which is how a narrow bevel can still show a full ring.
        if (useContour)
        {
            const float u = juce::jlimit (0.0f, 1.0f, value / contourRange);
            value = contourLut[(size_t) juce::jlimit (0, 256, (int) std::lround (u * 256.0f))];
        }

        height[i] = juce::jlimit (0.0f, 1.0f, value);
    }

    // Chisel Soft is Chisel with the crease taken off; Soften rounds the whole
    // thing further. Both are blurs of the height field, before the normal.
    int blur = (int) std::lround (b.soften * juce::jmax (0.25f, scale));

    if (b.technique == BevelTechnique::ChiselSoft)
        blur = juce::jmax (blur, 1);

    if (b.technique == BevelTechnique::Smooth)
        blur = juce::jmax (blur, (int) std::lround (size * 0.25f));

    // Anti-aliased: a contour with corner points puts hard creases in the
    // height field, and a hard crease in a height field stair-steps once it is
    // shaded. One pixel of softening is enough to take that off without
    // rounding the bevel itself.
    if (b.antiAliased)
        blur = juce::jmax (blur, 1);

    // Three passes, not one. A single box blur has a square kernel and leaves
    // its own axis bias on the surface — the same class of error the distance
    // transform used to add. Three boxes converge on a Gaussian, which is
    // round, so the softening does not reshape the bevel.
    boxBlur (height, w, h, juce::jlimit (0, 24, blur), 3);

    // --- shade ---------------------------------------------------------------
    const float az  = deg2rad (lightAngleDeg);
    const float alt = deg2rad (juce::jlimit (0.0f, 90.0f, lightAltitudeDeg));

    // Screen space: y runs down, so the azimuth's y component is negated. This
    // is the same convention the rest of the renderer lights from.
    const float lx = std::cos (alt) * std::cos (az);
    const float ly = -std::cos (alt) * std::sin (az);
    const float lz = std::sin (alt);

    const float depth = juce::jlimit (1.0f, 1000.0f, b.depth) * 0.01f;
    const float dir   = b.up ? 1.0f : -1.0f;

    const float hiAmount = juce::jlimit (0.0f, 1.0f, b.highlightOpacity * 0.01f)
                             * b.highlightColour.getFloatAlpha();
    const float shAmount = juce::jlimit (0.0f, 1.0f, b.shadowOpacity * 0.01f)
                             * b.shadowColour.getFloatAlpha();

    std::vector<float> glossLut (257);

    for (int i = 0; i <= 256; ++i)
        glossLut[(size_t) i] = b.glossContour.at ((float) i / 256.0f);

    const bool paintsOutside = b.style == BevelStyle::OuterBevel
                            || b.style == BevelStyle::Emboss
                            || b.style == BevelStyle::PillowEmboss
                            || b.style == BevelStyle::StrokeEmboss;

    juce::Image::BitmapData data (img, juce::Image::BitmapData::readWrite);

    auto heightAt = [&height, w, h] (int x, int y) noexcept
    {
        return height[(size_t) juce::jlimit (0, h - 1, y) * (size_t) w
                        + (size_t) juce::jlimit (0, w - 1, x)];
    };

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const size_t i = (size_t) y * (size_t) w + (size_t) x;

            const bool onShape = alpha[i] > 0;

            if (! onShape && ! paintsOutside)
                continue;

            const float gx = (heightAt (x + 1, y) - heightAt (x - 1, y)) * 0.5f;
            const float gy = (heightAt (x, y + 1) - heightAt (x, y - 1)) * 0.5f;

            if (std::abs (gx) < 1.0e-5f && std::abs (gy) < 1.0e-5f)
                continue;

            // Surface normal from the height gradient, then a proper N.L. The
            // altitude is what lets the light rake: at 90 degrees it is straight
            // on and the bevel almost vanishes, at 10 it skims and every ripple
            // shows.
            const float nx = -gx * depth * 8.0f;
            const float ny = -gy * depth * 8.0f;
            const float len = std::sqrt (nx * nx + ny * ny + 1.0f);

            float lambert = (nx * lx + ny * ly + lz) / len - lz;
            lambert = juce::jlimit (-1.0f, 1.0f, lambert * dir * 2.0f);

            // Gloss contour: reshapes the lighting response, which is what makes
            // the same surface read as matte, metallic or glassy.
            const int gi = juce::jlimit (0, 256, (int) std::lround ((lambert * 0.5f + 0.5f) * 256.0f));
            const float shaped = glossLut[(size_t) gi] * 2.0f - 1.0f;

            const float hi = juce::jlimit (0.0f, 1.0f,  shaped) * hiAmount;
            const float sh = juce::jlimit (0.0f, 1.0f, -shaped) * shAmount;

            if (hi < 0.004f && sh < 0.004f)
                continue;

            auto c = data.getPixelColour (x, y);

            float r = c.getFloatRed(), g = c.getFloatGreen(), bl = c.getFloatBlue();
            float a = c.getFloatAlpha();

            if (hi > 0.0f)
            {
                r  += (blendChannel (b.highlightMode, r,  b.highlightColour.getFloatRed())   - r)  * hi;
                g  += (blendChannel (b.highlightMode, g,  b.highlightColour.getFloatGreen()) - g)  * hi;
                bl += (blendChannel (b.highlightMode, bl, b.highlightColour.getFloatBlue())  - bl) * hi;
            }

            if (sh > 0.0f)
            {
                r  += (blendChannel (b.shadowMode, r,  b.shadowColour.getFloatRed())   - r)  * sh;
                g  += (blendChannel (b.shadowMode, g,  b.shadowColour.getFloatGreen()) - g)  * sh;
                bl += (blendChannel (b.shadowMode, bl, b.shadowColour.getFloatBlue())  - bl) * sh;
            }

            // Outside the shape there is nothing to shade, so the bevel has to
            // supply its own coverage — that is what makes an Outer Bevel a
            // visible rim rather than a no-op on transparent pixels.
            if (! onShape)
                a = juce::jmax (a, juce::jmin (1.0f, hi + sh));

            if (a <= 0.0001f)
                continue;

            data.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, r),
                                                                    juce::jlimit (0.0f, 1.0f, g),
                                                                    juce::jlimit (0.0f, 1.0f, bl),
                                                                    a));
        }
    }
}

//==============================================================================
juce::Image LayerRender::makeEdgeEffect (juce::Image& placed, const ShadowSpec& s,
                                         float scale, bool inward, bool* wasClipped)
{
    if (! s.enabled || ! placed.isValid() || s.colour.getAlpha() == 0)
        return {};

    const float opacity = juce::jlimit (0.0f, 1.0f, s.opacity * 0.01f)
                            * s.colour.getFloatAlpha();

    if (opacity < 0.004f)
        return {};

    const int w = placed.getWidth();
    const int h = placed.getHeight();

    std::vector<float> sd;
    std::vector<juce::uint8> alpha;

    {
        juce::Image::BitmapData data (placed, juce::Image::BitmapData::readOnly);

        if (! buildSignedDistance (data, w, h, sd, alpha))
            return {};
    }

    // How far there is to go before the effect runs out of anywhere to be:
    // outward, the furthest point from the shape; inward, the deepest point
    // inside it. Capping the reach there means the ramp always finishes rather
    // than stopping abruptly at full strength, so an oversized Size stops
    // mattering instead of turning into a solid block.
    const float reach = sd.empty()
                          ? 1.0f
                          : (inward ? -*std::min_element (sd.begin(), sd.end())
                                    :  *std::max_element (sd.begin(), sd.end()));

    const float requested = juce::jmax (0.0f, s.size * juce::jmax (0.25f, scale));
    const float limit     = juce::jmax (1.0f, reach);
    const float size      = juce::jmin (requested, limit);

    // Past this the Size control does nothing, which is worth saying out loud
    // rather than leaving the user to wonder why the slider stopped working.
    if (wasClipped != nullptr && requested > limit * 1.02f)
        *wasClipped = true;

    // Fade splits the reach into a solid part and a gradient. A floor of about
    // a pixel on the gradient keeps a hard edge antialiased rather than
    // stair-stepped.
    const float fade    = juce::jlimit (0.0f, 1.0f, s.fade * 0.01f);
    const float falloff = juce::jmax (0.75f, size * fade);
    const float inner   = size - falloff;

    // Screen pixels, straight through: the layer is already placed, so there is
    // no zoom to divide out and no rotation to undo.
    const float ox = s.offsetX * juce::jmax (0.25f, scale);
    const float oy = s.offsetY * juce::jmax (0.25f, scale);

    const float cr = s.colour.getFloatRed();
    const float cg = s.colour.getFloatGreen();
    const float cb = s.colour.getFloatBlue();

    const float curve = juce::jlimit (0.0f, 1.0f, s.curve   * 0.01f);
    const float heat  = juce::jlimit (0.0f, 1.0f, s.hotCore * 0.01f);

    // Inverse-square, renormalised so it still reaches zero at the end of the
    // band rather than stopping on a step. k sets how hard the near field is;
    // 14 lands close to how a real point source reads at these sizes.
    constexpr float kFalloff = 14.0f;
    const float tail = 1.0f / (1.0f + kFalloff);

    juce::Image out (juce::Image::ARGB, w, h, true);

    {
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const juce::uint8 a = alpha[(size_t) y * (size_t) w + (size_t) x];

                // An inner shadow only exists on the layer; a drop shadow only
                // exists off it, unless knockout is switched off.
                float clip = 1.0f;

                if (inward)
                {
                    if (a == 0)
                        continue;

                    clip = (float) a / 255.0f;
                }
                else if (s.knockout && a >= 250)
                {
                    continue;
                }

                // Where this pixel reads from in the silhouette. Off the image
                // counts as outside the shape, which for an inner shadow is a
                // hole casting at full strength and for an outward one is
                // nothing at all — the sign flip below handles both.
                const int fx = (int) std::lround ((float) x - ox);
                const int fy = (int) std::lround ((float) y - oy);

                const float d = (fx < 0 || fx >= w || fy < 0 || fy >= h)
                                  ? 1.0e6f
                                  : sd[(size_t) fy * (size_t) w + (size_t) fx];

                // Outward measures distance out of the shape; inward measures
                // depth into it. One sign, two effects.
                const float q = inward ? -d : d;

                if (q > size)
                    continue;

                const float t = juce::jlimit (0.0f, 1.0f, (q - inner) / falloff);

                // The eased ramp is what everything else uses. Curve bends it
                // toward inverse-square, which is what light actually does.
                const float eased = 1.0f - t * t * (3.0f - 2.0f * t);

                const float physical = (1.0f / (1.0f + kFalloff * t * t) - tail) / (1.0f - tail);

                const float falloffValue = eased + (physical - eased) * curve;
                const float coverage = falloffValue * clip;

                if (coverage <= 0.002f)
                    continue;

                // Hot core: the dense end clips toward white and the tail keeps
                // its colour. Cubed so only the brightest part blows out —
                // ramping it linearly just washes the whole glow pale.
                float r = cr, g = cg, b = cb;

                if (heat > 0.0f)
                {
                    const float hot = juce::jlimit (0.0f, 1.0f,
                                                    falloffValue * falloffValue * falloffValue * heat);

                    r += (1.0f - cr) * hot;
                    g += (1.0f - cg) * hot;
                    b += (1.0f - cb) * hot;
                }

                dst.setPixelColour (x, y,
                                    juce::Colour::fromFloatRGBA (r, g, b, coverage * opacity));
            }
        }
    }

    return out;
}

//==============================================================================
void LayerRender::applyColourAdjust (juce::Image& img, const EffectSpec& e, float t)
{
    const float bright = e.brightness.at (t) * 0.01f;
    const float contr  = e.contrast.at (t)   * 0.01f;
    const float sat    = e.saturation.at (t) * 0.01f;
    const float hue    = e.hue.at (t) / 360.0f;

    if (std::abs (bright) < 0.001f && std::abs (contr) < 0.001f
        && std::abs (sat) < 0.001f && std::abs (hue) < 0.0001f)
        return;

    juce::Image::BitmapData data (img, juce::Image::BitmapData::readWrite);

    for (int y = 0; y < img.getHeight(); ++y)
    {
        for (int x = 0; x < img.getWidth(); ++x)
        {
            auto c = data.getPixelColour (x, y);

            if (c.getAlpha() == 0)
                continue;

            if (std::abs (hue) > 0.0001f)
                c = c.withRotatedHue (hue);

            if (std::abs (sat) > 0.001f)
                c = (sat > 0.0f) ? c.withMultipliedSaturation (1.0f + sat * 2.0f)
                                 : c.withMultipliedSaturation (juce::jmax (0.0f, 1.0f + sat));

            float r = c.getFloatRed(), gr = c.getFloatGreen(), b = c.getFloatBlue();

            if (std::abs (contr) > 0.001f)
            {
                const float k = 1.0f + contr;
                r  = juce::jlimit (0.0f, 1.0f, (r  - 0.5f) * k + 0.5f);
                gr = juce::jlimit (0.0f, 1.0f, (gr - 0.5f) * k + 0.5f);
                b  = juce::jlimit (0.0f, 1.0f, (b  - 0.5f) * k + 0.5f);
            }

            if (std::abs (bright) > 0.001f)
            {
                r  = juce::jlimit (0.0f, 1.0f, r  + bright);
                gr = juce::jlimit (0.0f, 1.0f, gr + bright);
                b  = juce::jlimit (0.0f, 1.0f, b  + bright);
            }

            data.setPixelColour (x, y, juce::Colour::fromFloatRGBA (r, gr, b, c.getFloatAlpha()));
        }
    }
}

//==============================================================================
void LayerRender::applyMasks (juce::Image& img, const EffectSpec& e, float t)
{
    if (! e.mask1.enabled && ! e.mask2.enabled)
        return;

    const int w = img.getWidth();
    const int h = img.getHeight();

    const float cx = w * e.centreX * 0.01f;
    const float cy = h * e.centreY * 0.01f;
    const float maxR = std::sqrt ((float) (w * w + h * h)) * 0.5f;

    auto testMask = [&] (const MaskSpec& m, int x, int y) -> bool
    {
        const float start = m.start.at (t);
        const float stop  = m.stop.at (t);
        const float lo    = juce::jmin (start, stop);
        const float hi    = juce::jmax (start, stop);

        float value = 0.0f;

        switch (m.type)
        {
            case MaskType::Rotate:
            {
                // Degrees clockwise from 12 o'clock, in -180..180.
                const float a = std::atan2 ((float) x - cx, cy - (float) y) * 180.0f / kPi;
                value = m.biDir ? std::abs (a) : a;
                break;
            }

            case MaskType::Radius:
            {
                const float dx = (float) x - cx;
                const float dy = (float) y - cy;
                value = (std::sqrt (dx * dx + dy * dy) / maxR) * 100.0f;
                break;
            }

            case MaskType::Horizontal:
                value = ((float) x / (float) juce::jmax (1, w)) * 200.0f - 100.0f;
                if (m.biDir) value = std::abs (value);
                break;

            case MaskType::Vertical:
                value = ((float) y / (float) juce::jmax (1, h)) * 200.0f - 100.0f;
                if (m.biDir) value = std::abs (value);
                break;

            case MaskType::numTypes:
            default:
                return true;
        }

        return value >= lo && value <= hi;
    };

    juce::Image::BitmapData data (img, juce::Image::BitmapData::readWrite);

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            auto* px = pixelAt (data, x, y);

            if (px->getAlpha() == 0)
                continue;

            bool keep = true;

            if (e.mask1.enabled)
                keep = testMask (e.mask1, x, y);

            if (e.mask2.enabled)
            {
                const bool m2 = testMask (e.mask2, x, y);
                keep = e.mask2.orOp ? (keep || m2) : (keep && m2);
            }

            if (! keep)
                px->setARGB (0, 0, 0, 0);
        }
    }
}

//==============================================================================
void LayerRender::compositeLayer (juce::Graphics& g, const DocLayer& layer,
                                  juce::Image& layerImage,
                                  int canvasW, int canvasH, float t)
{
    const auto& e = layer.eff;

    const float alpha = juce::jlimit (0.0f, 1.0f, e.alpha.at (t) * 0.01f);

    if (alpha <= 0.001f)
        return;

    const float zx = juce::jmax (0.001f, e.zoomX.at (t) * 0.01f);
    const float zy = juce::jmax (0.001f, (e.zoomSeparate ? e.zoomY.at (t)
                                                         : e.zoomX.at (t)) * 0.01f);

    // The pivot. Everything a layer does spatially happens around this point,
    // so putting it on the knob's centre is what ties a pointer to the knob.
    const float cx = canvasW * e.centreX * 0.01f;
    const float cy = canvasH * e.centreY * 0.01f;

    const float ox = canvasW * e.offsetX.at (t) * 0.01f;
    const float oy = canvasH * e.offsetY.at (t) * 0.01f;

    const float angle = deg2rad (e.angle.at (t));

    // Order matters, and this is the whole fix: zoom about the pivot, THEN
    // offset, THEN rotate about the pivot.
    //
    // Offsetting after the rotation — which is what this used to do — spins the
    // layer where it stands and only afterwards slides it out, so a pointer
    // pivots on its own middle at a fixed spot instead of swinging round the
    // knob. Offsetting first turns the offset into a radius: the layer is
    // carried out to that distance and the rotation then sweeps it round the
    // pivot, which is how a pointer, an indicator dot or a scale label tracks
    // the value. It is also KnobMan's own order, so imported .knob documents
    // now animate the way they do in the original editor.
    auto transform = juce::AffineTransform::translation (-cx, -cy)
                        .scaled (zx, zy)
                        .translated (ox, oy)
                        .rotated (angle)
                        .translated (cx, cy);

    if (e.keepDir)
    {
        // Keep direction: the layer still travels the same arc, but its own
        // content stays upright — a numeral, a badge or a dot that orbits
        // without turning over. Take where the rotation puts the layer's
        // centre, then place the unrotated content there.
        const juce::Point<float> home (canvasW * 0.5f, canvasH * 0.5f);
        const auto carried = home.transformedBy (transform);

        transform = juce::AffineTransform::translation (-home.x, -home.y)
                        .scaled (zx, zy)
                        .translated (carried.x, carried.y);
    }

    juce::Graphics::ScopedSaveState save (g);
    g.setOpacity (alpha);
    g.setImageResamplingQuality (e.antialias ? juce::Graphics::highResamplingQuality
                                             : juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed (layerImage, transform, false);
}

//==============================================================================
void LayerRender::blendOnto (juce::Image& canvas, const juce::Image& src, BlendMode mode)
{
    if (! src.isValid() || ! canvas.isValid())
        return;

    if (mode == BlendMode::Normal)
    {
        juce::Graphics g (canvas);
        g.drawImageAt (src, 0, 0);
        return;
    }

    const int w = juce::jmin (canvas.getWidth(),  src.getWidth());
    const int h = juce::jmin (canvas.getHeight(), src.getHeight());

    juce::Image source (src);

    juce::Image::BitmapData dst (canvas, juce::Image::BitmapData::readWrite);
    juce::Image::BitmapData s   (source, juce::Image::BitmapData::readOnly);

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const auto sc = s.getPixelColour (x, y);
            const float sa = sc.getFloatAlpha();

            if (sa <= 0.002f)
                continue;

            const auto bc = dst.getPixelColour (x, y);
            const float ba = bc.getFloatAlpha();

            // Blend the colours, then composite the result by the source alpha.
            // Over empty canvas Screen returns the source unchanged, so a glow
            // on its own still looks like the colour that was picked.
            const float r = blendChannel (mode, bc.getFloatRed(),   sc.getFloatRed());
            const float g = blendChannel (mode, bc.getFloatGreen(), sc.getFloatGreen());
            const float b = blendChannel (mode, bc.getFloatBlue(),  sc.getFloatBlue());

            const float outA = sa + ba * (1.0f - sa);

            if (outA <= 0.0001f)
                continue;

            auto mix = [sa, ba, outA] (float top, float bottom)
            {
                return (top * sa + bottom * ba * (1.0f - sa)) / outA;
            };

            dst.setPixelColour (x, y,
                                juce::Colour::fromFloatRGBA (mix (r, bc.getFloatRed()),
                                                             mix (g, bc.getFloatGreen()),
                                                             mix (b, bc.getFloatBlue()),
                                                             outA));
        }
    }
}

//==============================================================================
namespace
{
    bool needsPlacement (const PrimitiveSpec& p) noexcept
    {
        return p.strokeEnabled || p.shadow.enabled
            || p.innerShadow.enabled || p.outerGlow.enabled;
    }
}

void LayerRender::compositeWithEdgeEffects (juce::Image& canvas, const DocLayer& layer,
                                            juce::Image& img, int w, int h, float t, float scale,
                                            RenderNotes* notes)
{
    // Place the layer first, then cast from what actually landed. Measured in
    // layer space a Sphere has no room around it at all — it is rasterised
    // inscribed in its own image — and its corners arrive as a square block.
    juce::Image placed (juce::Image::ARGB, w, h, true);

    {
        juce::Graphics pg (placed);
        compositeLayer (pg, layer, img, w, h, t);
    }

    auto report = [notes, &layer] (bool clipped, const char* what)
    {
        if (clipped && notes != nullptr)
            notes->messages.add (layer.name + ": " + what
                                   + " is past the room around the shape — "
                                     "raise the canvas or lower the layer's zoom");
    };

    bool clipped = false;

    // The outline first, so everything downstream sees the silhouette it
    // produces. A drop shadow is cast by the shape AND its outline, which is
    // what Photoshop does and what looks right.
    applyStroke (placed, layer.prim, scale, &clipped);

    if (clipped && notes != nullptr)
        notes->messages.add (layer.name + ": outline is as thick as the shape it is "
                                          "tracing — reduce Outline W, or use Outside");

    // Then back to front, as Photoshop stacks them: drop shadow, glow, the
    // layer, then the inner shadow on top because it belongs to the surface.
    clipped = false;

    auto drop = makeEdgeEffect (placed, layer.prim.shadow, scale, false, &clipped);
    report (clipped, "drop shadow");
    blendOnto (canvas, drop, layer.prim.shadow.blend);

    clipped = false;
    auto glow = makeEdgeEffect (placed, layer.prim.outerGlow, scale, false, &clipped);
    report (clipped, "outer glow");
    blendOnto (canvas, glow, layer.prim.outerGlow.blend);

    {
        juce::Graphics g (canvas);
        g.drawImageAt (placed, 0, 0);
    }

    clipped = false;
    auto innerShadow = makeEdgeEffect (placed, layer.prim.innerShadow, scale, true, &clipped);
    report (clipped, "inner shadow");
    blendOnto (canvas, innerShadow, layer.prim.innerShadow.blend);
}

//==============================================================================
juce::Image LayerRender::renderFrame (const LayerDoc& doc, float t, float scale, RenderNotes* notes)
{
    scale = juce::jlimit (0.25f, 8.0f, scale);

    const int w = juce::jmax (8, (int) std::lround (doc.canvasWidth  * scale));
    const int h = juce::jmax (8, (int) std::lround (doc.canvasHeight * scale));

    juce::Image canvas (juce::Image::ARGB, w, h, true);

    // No long-lived Graphics: a layer with a non-Normal blend has to touch the
    // canvas pixels directly, and it cannot do that while one is open.

    const bool solo = doc.hasSolo();

    for (const auto& layer : doc.layers)
    {
        if (solo ? ! layer.solo : ! layer.visible)
            continue;

        auto img = rasterisePrimitive (layer.prim, w, h, scale, t);

        applyBevel (img, layer.prim, scale,
                    layer.prim.bevel.useGlobalLight ? doc.globalLightAngle    : layer.prim.bevel.angle,
                    layer.prim.bevel.useGlobalLight ? doc.globalLightAltitude : layer.prim.bevel.altitude);

        applyTexture      (img, layer.prim);
        applyGloss        (img, layer.prim);
        applyColourAdjust (img, layer.eff, t);
        applyMasks        (img, layer.eff, t);

        if (needsPlacement (layer.prim))
        {
            compositeWithEdgeEffects (canvas, layer, img, w, h, t, scale, notes);
        }
        else
        {
            juce::Graphics g (canvas);
            compositeLayer (g, layer, img, w, h, t);
        }
    }

    return canvas;
}

//==============================================================================
juce::Image LayerRender::renderLayer (const LayerDoc& doc, int layerIndex, float t, float scale)
{
    scale = juce::jlimit (0.25f, 8.0f, scale);

    const int w = juce::jmax (8, (int) std::lround (doc.canvasWidth  * scale));
    const int h = juce::jmax (8, (int) std::lround (doc.canvasHeight * scale));

    juce::Image canvas (juce::Image::ARGB, w, h, true);

    if (layerIndex < 0 || layerIndex >= (int) doc.layers.size())
        return canvas;

    const auto& layer = doc.layers[(size_t) layerIndex];

    auto img = rasterisePrimitive (layer.prim, w, h, scale, t);

    applyBevel (img, layer.prim, scale,
                layer.prim.bevel.useGlobalLight ? doc.globalLightAngle    : layer.prim.bevel.angle,
                layer.prim.bevel.useGlobalLight ? doc.globalLightAltitude : layer.prim.bevel.altitude);

    applyTexture      (img, layer.prim);
    applyGloss        (img, layer.prim);
    applyColourAdjust (img, layer.eff, t);
    applyMasks        (img, layer.eff, t);

    if (needsPlacement (layer.prim))
    {
        compositeWithEdgeEffects (canvas, layer, img, w, h, t, scale, nullptr);
    }
    else
    {
        juce::Graphics g (canvas);
        compositeLayer (g, layer, img, w, h, t);
    }

    return canvas;
}

//==============================================================================
juce::Image LayerRender::renderFilmstrip (const LayerDoc& doc, bool vertical, float scale)
{
    scale = juce::jlimit (0.25f, 8.0f, scale);

    const int w = juce::jmax (8, (int) std::lround (doc.canvasWidth  * scale));
    const int h = juce::jmax (8, (int) std::lround (doc.canvasHeight * scale));
    const int n = juce::jlimit (1, 512, doc.frames);

    juce::Image strip (juce::Image::ARGB,
                       vertical ? w : w * n,
                       vertical ? h * n : h,
                       true);

    juce::Graphics g (strip);

    for (int i = 0; i < n; ++i)
    {
        const float t = (n == 1) ? 0.0f : (float) i / (float) (n - 1);

        auto frame = renderFrame (doc, t, scale);

        g.drawImageAt (frame,
                       vertical ? 0 : i * w,
                       vertical ? i * h : 0);
    }

    return strip;
}
