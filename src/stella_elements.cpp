// C:\workspace\Stella AI Studio\src\stella_elements.cpp
//
// The programmed elements' runtime (see stella_elements.h): a small anti-aliased software
// renderer with text (stb_truetype and the Inter font), the elements' registry, and what
// an element asks of its host. Compiled the same way into the studio's WebAssembly module
// and into exported plugins, so both show the same pixels.

#include "stella_elements.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

#include "stella_fonts.h"   // generated: the Inter font, regular and bold, as byte arrays

namespace stella::ui
{
    //==========================================================================
    // Colours
    namespace
    {
        constexpr float pi = 3.14159265358979f;

        float clamp01 (float v) noexcept    { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        int channel (Colour c, int shift) noexcept   { return (int) ((c >> shift) & 0xffu); }

        Colour make (int a, int r, int g, int b) noexcept
        {
            auto limit = [] (int v) { return (std::uint32_t) (v < 0 ? 0 : (v > 255 ? 255 : v)); };
            return (limit (a) << 24) | (limit (r) << 16) | (limit (g) << 8) | limit (b);
        }

        /** Straight colour and coverage to premultiplied. */
        std::uint32_t premultiplied (Colour c, float alpha) noexcept
        {
            const auto a = (float) channel (c, 24) * clamp01 (alpha) / 255.0f;

            if (a <= 0.0f)
                return 0;

            const auto r = (std::uint32_t) ((float) channel (c, 16) * a + 0.5f);
            const auto g = (std::uint32_t) ((float) channel (c, 8) * a + 0.5f);
            const auto b = (std::uint32_t) ((float) channel (c, 0) * a + 0.5f);
            return ((std::uint32_t) (a * 255.0f + 0.5f) << 24) | (r << 16) | (g << 8) | b;
        }

        void blend (std::uint32_t& dst, std::uint32_t src) noexcept
        {
            const auto sa = src >> 24;

            if (sa == 0)
                return;

            if (sa == 255)
            {
                dst = src;
                return;
            }

            const auto inverse = 255 - sa;
            const auto rb = (((dst & 0x00ff00ffu) * inverse) >> 8) & 0x00ff00ffu;
            const auto ag = (((dst >> 8) & 0x00ff00ffu) * inverse) & 0xff00ff00u;
            const auto under = rb | ag;
            std::uint32_t out = 0;

            for (int shift = 0; shift < 32; shift += 8)
            {
                const auto sum = ((under >> shift) & 0xffu) + ((src >> shift) & 0xffu);
                out |= (sum > 255 ? 255u : sum) << shift;
            }

            dst = out;
        }
    }

    Colour withAlpha (Colour c, float alpha) noexcept
    {
        return make ((int) ((float) channel (c, 24) * clamp01 (alpha) + 0.5f), channel (c, 16), channel (c, 8), channel (c, 0));
    }

    Colour mix (Colour a, Colour b, float t) noexcept
    {
        t = clamp01 (t);
        auto lerp = [t] (int x, int y) { return (int) ((float) x + (float) (y - x) * t + 0.5f); };
        return make (lerp (channel (a, 24), channel (b, 24)), lerp (channel (a, 16), channel (b, 16)),
                     lerp (channel (a, 8), channel (b, 8)), lerp (channel (a, 0), channel (b, 0)));
    }

    Colour brighter (Colour c, float amount) noexcept
    {
        return (c & 0xff000000u) | (mix (c, 0xffffffffu, amount) & 0x00ffffffu);
    }

    Colour darker (Colour c, float amount) noexcept
    {
        return (c & 0xff000000u) | (mix (c, 0xff000000u, amount) & 0x00ffffffu);
    }

    //==========================================================================
    // The renderer: shapes are polygons (curves as short straight pieces), filled with
    // exact area coverage, accumulated per pixel and summed along each row.
    namespace
    {
        struct Paint
        {
            enum Kind { solid, vertical, radial } kind = solid;
            Colour c1 = 0, c2 = 0;
            float a = 0.0f, b = 0.0f, cx = 0.0f, cy = 0.0f;   // vertical: top a, bottom b; radial: centre, radius a

            Colour at (float x, float y) const noexcept
            {
                if (kind == vertical)
                    return mix (c1, c2, b > a ? (y - a) / (b - a) : 0.0f);

                if (kind == radial)
                    return mix (c1, c2, a > 0.0f ? std::sqrt ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / a : 0.0f);

                return c1;
            }
        };

        struct Glyph
        {
            int x0 = 0, y0 = 0, w = 0, h = 0;
            std::vector<unsigned char> alpha;
        };

        struct Font
        {
            stbtt_fontinfo info {};
            bool ok = false;
            int ascent = 0, descent = 0, lineGap = 0;
        };

        int segmentsFor (float radius, float angle) noexcept
        {
            // Pieces short enough that the curve is never off by more than a tenth of a pixel.
            const auto r = std::max (0.5f, radius);
            const auto step = std::acos (std::max (-1.0f, 1.0f - 0.1f / r)) * 2.0f;
            const auto n = (int) std::ceil (std::fabs (angle) / std::max (0.01f, step));
            return std::max (2, std::min (720, n));
        }
    }

    struct Painter
    {
        Painter()
        {
            const unsigned char* data[] { fonts::regular, fonts::bold };

            for (int i = 0; i < 2; ++i)
            {
                auto& f = font[i];
                f.ok = stbtt_InitFont (&f.info, data[i], stbtt_GetFontOffsetForIndex (data[i], 0)) != 0;

                if (f.ok)
                    stbtt_GetFontVMetrics (&f.info, &f.ascent, &f.descent, &f.lineGap);
            }
        }

        //======================================================================
        // Building a shape
        void begin()
        {
            points.clear();
            starts.clear();
        }

        void moveTo (float x, float y)
        {
            starts.push_back ((int) points.size() / 2);
            points.push_back (x);
            points.push_back (y);
        }

        void lineTo (float x, float y)
        {
            if (starts.empty())
                starts.push_back (0);

            points.push_back (x);
            points.push_back (y);
        }

        /** A point at an angle (0 at 12 o'clock, clockwise) on an ellipse. */
        static void onEllipse (float cx, float cy, float rx, float ry, float angle, float& x, float& y)
        {
            x = cx + rx * std::sin (angle);
            y = cy - ry * std::cos (angle);
        }

        /** An arc's points from one angle to another (either way round); a new contour if asked. */
        void arc (float cx, float cy, float rx, float ry, float from, float to, bool startContour)
        {
            const auto n = segmentsFor (std::max (rx, ry), to - from);

            for (int i = 0; i <= n; ++i)
            {
                float x, y;
                onEllipse (cx, cy, rx, ry, from + (to - from) * (float) i / (float) n, x, y);

                if (i == 0 && startContour) moveTo (x, y);
                else                        lineTo (x, y);
            }
        }

        void ellipse (float x, float y, float w, float h, bool reverse)
        {
            const auto rx = w * 0.5f, ry = h * 0.5f;
            arc (x + rx, y + ry, rx, ry, 0.0f, reverse ? -2.0f * pi : 2.0f * pi, true);
        }

        void roundedRect (float x, float y, float w, float h, float radius, bool reverse)
        {
            radius = std::max (0.0f, std::min (radius, std::min (w, h) * 0.5f));

            if (radius < 0.05f)
            {
                moveTo (x, y);

                if (reverse) { lineTo (x, y + h); lineTo (x + w, y + h); lineTo (x + w, y); }
                else         { lineTo (x + w, y); lineTo (x + w, y + h); lineTo (x, y + h); }
                return;
            }

            // Corners clockwise from the top-right; the other way round for a hole.
            const float cxs[] { x + w - radius, x + w - radius, x + radius, x + radius };
            const float cys[] { y + radius, y + h - radius, y + h - radius, y + radius };
            const float froms[] { 0.0f, 0.5f * pi, pi, 1.5f * pi };

            if (! reverse)
            {
                for (int i = 0; i < 4; ++i)
                    arc (cxs[i], cys[i], radius, radius, froms[i], froms[i] + 0.5f * pi, i == 0);
            }
            else
            {
                for (int i = 3; i >= 0; --i)
                    arc (cxs[i], cys[i], radius, radius, froms[i] + 0.5f * pi, froms[i], i == 3);
            }
        }

        /** A line with round ends: a capsule. */
        void capsule (float x0, float y0, float x1, float y1, float half)
        {
            const auto direction = std::atan2 (x1 - x0, -(y1 - y0));   // 0 is up, clockwise
            arc (x1, y1, half, half, direction - 0.5f * pi, direction + 0.5f * pi, true);
            arc (x0, y0, half, half, direction + 0.5f * pi, direction + 1.5f * pi, false);
        }

        //======================================================================
        // Filling it
        void fill (Canvas& canvas, std::uint32_t* pixels, const int* clip, const Paint& paint)
        {
            if (points.size() < 6)
                return;

            float minX = points[0], maxX = points[0], minY = points[1], maxY = points[1];

            for (size_t i = 0; i < points.size(); i += 2)
            {
                minX = std::min (minX, points[i]);
                maxX = std::max (maxX, points[i]);
                minY = std::min (minY, points[i + 1]);
                maxY = std::max (maxY, points[i + 1]);
            }

            if (! std::isfinite (minX) || ! std::isfinite (maxX) || ! std::isfinite (minY) || ! std::isfinite (maxY))
                return;

            const auto bx0 = std::max (clip[0], (int) std::floor (minX)), bx1 = std::min (clip[2], (int) std::ceil (maxX));
            const auto by0 = std::max (clip[1], (int) std::floor (minY)), by1 = std::min (clip[3], (int) std::ceil (maxY));

            if (bx1 <= bx0 || by1 <= by0)
                return;

            width = bx1 - bx0;
            height = by1 - by0;
            stride = width + 2;
            accumulator.assign ((size_t) stride * (size_t) height, 0.0f);

            const auto numPoints = (int) points.size() / 2;

            for (size_t c = 0; c < starts.size(); ++c)
            {
                const auto first = starts[c];
                const auto last = (c + 1 < starts.size() ? starts[c + 1] : numPoints) - 1;

                for (int i = first; i <= last; ++i)
                {
                    const auto j = i < last ? i + 1 : first;   // closed: the last point joins the first
                    addEdge (points[(size_t) i * 2] - (float) bx0, points[(size_t) i * 2 + 1] - (float) by0,
                             points[(size_t) j * 2] - (float) bx0, points[(size_t) j * 2 + 1] - (float) by0);
                }
            }

            const auto canvasWidth = (size_t) canvas.width();

            for (int y = 0; y < height; ++y)
            {
                const auto* row = accumulator.data() + (size_t) y * (size_t) stride;
                auto* target = pixels + (size_t) (by0 + y) * canvasWidth + (size_t) bx0;
                float sum = 0.0f;

                for (int x = 0; x < width; ++x)
                {
                    sum += row[x];
                    const auto coverage = std::min (1.0f, std::fabs (sum));

                    if (coverage > 0.002f)
                        blend (target[x], premultiplied (paint.kind == Paint::solid ? paint.c1
                                                                                    : paint.at ((float) (bx0 + x) + 0.5f, (float) (by0 + y) + 0.5f),
                                                         coverage));
                }
            }
        }

        /** An edge, split where it crosses the box's sides so its x can be held inside the box
            without changing which pixels it covers. */
        void addEdge (float x0, float y0, float x1, float y1)
        {
            if (y0 == y1)
                return;

            float cuts[4] { 0.0f, 1.0f, 1.0f, 1.0f };
            int numCuts = 1;

            for (const auto side : { 0.0f, (float) width })
                if ((x0 < side) != (x1 < side))
                    cuts[numCuts++] = (side - x0) / (x1 - x0);

            cuts[numCuts++] = 1.0f;

            for (int i = 1; i < numCuts; ++i)   // in order (at most four)
                for (int j = i; j > 0 && cuts[j] < cuts[j - 1]; --j)
                    std::swap (cuts[j], cuts[j - 1]);

            auto held = [this] (float x) { return std::max (0.0f, std::min ((float) width, x)); };

            for (int i = 0; i + 1 < numCuts; ++i)
            {
                const auto ta = cuts[i], tb = cuts[i + 1];

                if (tb <= ta)
                    continue;

                accumulate (held (x0 + (x1 - x0) * ta), y0 + (y1 - y0) * ta, held (x0 + (x1 - x0) * tb), y0 + (y1 - y0) * tb);
            }
        }

        /** Raph Levien's signed-area accumulation (font-rs). */
        void accumulate (float x0, float y0, float x1, float y1)
        {
            if (y0 == y1)
                return;

            float direction = 1.0f;

            if (y0 > y1)
            {
                std::swap (x0, x1);
                std::swap (y0, y1);
                direction = -1.0f;
            }

            const auto dxdy = (x1 - x0) / (y1 - y0);
            auto x = x0;

            if (y0 < 0.0f)
            {
                x -= y0 * dxdy;
                y0 = 0.0f;
            }

            const auto end = std::min (height, (int) std::ceil (y1));

            for (int y = (int) y0; y < end; ++y)
            {
                auto* row = accumulator.data() + (size_t) y * (size_t) stride;
                const auto dy = std::min ((float) (y + 1), y1) - std::max ((float) y, y0);
                const auto next = x + dxdy * dy;
                const auto d = dy * direction;
                const auto xa = std::min (x, next), xb = std::max (x, next);
                const auto xaFloor = std::floor (xa);
                const auto xai = (int) xaFloor;
                const auto xbCeil = std::ceil (xb);
                const auto xbi = (int) xbCeil;

                if (xbi <= xai + 1)
                {
                    const auto middle = 0.5f * (x + next) - xaFloor;
                    row[xai] += d - d * middle;
                    row[xai + 1] += d * middle;
                }
                else
                {
                    const auto s = 1.0f / (xb - xa);
                    const auto xaf = xa - xaFloor;
                    const auto a0 = 0.5f * s * (1.0f - xaf) * (1.0f - xaf);
                    const auto xbf = xb - xbCeil + 1.0f;
                    const auto am = 0.5f * s * xbf * xbf;

                    row[xai] += d * a0;

                    if (xbi == xai + 2)
                    {
                        row[xai + 1] += d * (1.0f - a0 - am);
                    }
                    else
                    {
                        const auto a1 = s * (1.5f - xaf);
                        row[xai + 1] += d * (a1 - a0);

                        for (int xi = xai + 2; xi < xbi - 1; ++xi)
                            row[xi] += d * s;

                        const auto a2 = a1 + (float) (xbi - xai - 3) * s;
                        row[xbi - 1] += d * (1.0f - a2 - am);
                    }

                    row[xbi] += d * am;
                }

                x = next;
            }
        }

        //======================================================================
        // Text
        struct Laid
        {
            int glyph;
            float x;   // where its origin goes, from the start of the line
        };

        static std::vector<int> decode (const std::string& text)
        {
            std::vector<int> codepoints;
            const auto* s = reinterpret_cast<const unsigned char*> (text.data());
            const auto n = text.size();

            for (size_t i = 0; i < n;)
            {
                const auto c = s[i];
                int cp = '?', extra = 0;

                if (c < 0x80)                   { cp = c; }
                else if ((c & 0xe0) == 0xc0)    { cp = c & 0x1f; extra = 1; }
                else if ((c & 0xf0) == 0xe0)    { cp = c & 0x0f; extra = 2; }
                else if ((c & 0xf8) == 0xf0)    { cp = c & 0x07; extra = 3; }

                ++i;

                for (int k = 0; k < extra && i < n && (s[i] & 0xc0) == 0x80; ++k, ++i)
                    cp = (cp << 6) | (s[i] & 0x3f);

                if (cp != '\r' && cp != '\n')
                    codepoints.push_back (cp);
            }

            return codepoints;
        }

        float scaleFor (int f, float size) const
        {
            return font[f].ok ? stbtt_ScaleForPixelHeight (&font[f].info, size) : 0.0f;
        }

        int glyphFor (int f, int codepoint)
        {
            auto g = stbtt_FindGlyphIndex (&font[f].info, codepoint);
            return g != 0 ? g : stbtt_FindGlyphIndex (&font[f].info, '?');
        }

        /** The glyphs of a line and where they go; returns its width. */
        float layOut (int f, const std::vector<int>& codepoints, float scale, std::vector<Laid>& laid)
        {
            laid.clear();
            float x = 0.0f;
            int previous = 0;

            for (const auto cp : codepoints)
            {
                const auto g = glyphFor (f, cp);

                if (previous != 0)
                    x += (float) stbtt_GetGlyphKernAdvance (&font[f].info, previous, g) * scale;

                laid.push_back ({ g, x });

                int advance = 0, bearing = 0;
                stbtt_GetGlyphHMetrics (&font[f].info, g, &advance, &bearing);
                x += (float) advance * scale;
                previous = g;
            }

            return x;
        }

        const Glyph& glyphImage (int f, float size, int glyph, int subpixel)
        {
            const auto sizeKey = (std::uint64_t) std::lround (size * 4.0f);
            const auto key = ((std::uint64_t) f << 62) | (sizeKey << 40) | ((std::uint64_t) (std::uint32_t) glyph << 8) | (std::uint64_t) subpixel;

            if (const auto found = glyphs.find (key); found != glyphs.end())
                return found->second;

            if (glyphs.size() > 4000)
                glyphs.clear();

            auto& image = glyphs[key];
            const auto scale = scaleFor (f, size);
            const auto shift = (float) subpixel / 4.0f;
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetGlyphBitmapBoxSubpixel (&font[f].info, glyph, scale, scale, shift, 0.0f, &x0, &y0, &x1, &y1);

            image.x0 = x0;
            image.y0 = y0;
            image.w = std::max (0, x1 - x0);
            image.h = std::max (0, y1 - y0);
            image.alpha.assign ((size_t) image.w * (size_t) image.h, 0);

            if (image.w > 0 && image.h > 0)
                stbtt_MakeGlyphBitmapSubpixel (&font[f].info, image.alpha.data(), image.w, image.h, image.w, scale, scale, shift, 0.0f, glyph);

            return image;
        }

        float textWidth (const std::string& text, float size, bool bold)
        {
            const auto f = bold ? 1 : 0;

            if (! font[f].ok)
                return 0.0f;

            return layOut (f, decode (text), scaleFor (f, size), laid);
        }

        void drawText (Canvas& canvas, std::uint32_t* pixels, const int* clip, const std::string& text,
                       float x, float y, float w, float h, float size, Colour colour, Align align, bool bold)
        {
            const auto f = bold ? 1 : 0;

            if (! font[f].ok || text.empty() || channel (colour, 24) == 0)
                return;

            size = std::max (4.0f, std::min (200.0f, size));
            const auto scale = scaleFor (f, size);
            auto codepoints = decode (text);
            auto width = layOut (f, codepoints, scale, laid);

            // Too long: cut, and end in an ellipsis.
            if (width > w + 0.5f && ! codepoints.empty())
            {
                const auto ellipsis = stbtt_FindGlyphIndex (&font[f].info, 0x2026) != 0 ? std::vector<int> { 0x2026 } : std::vector<int> { '.', '.', '.' };

                while (! codepoints.empty())
                {
                    codepoints.pop_back();

                    while (! codepoints.empty() && codepoints.back() == ' ')
                        codepoints.pop_back();

                    auto shortened = codepoints;
                    shortened.insert (shortened.end(), ellipsis.begin(), ellipsis.end());

                    if ((width = layOut (f, shortened, scale, laid)) <= w + 0.5f || codepoints.empty())
                        break;
                }
            }

            const auto left = align == Align::left ? x : (align == Align::right ? x + w - width : x + (w - width) * 0.5f);
            const auto textHeight = (float) (font[f].ascent - font[f].descent) * scale;
            const auto baseline = (float) std::lround (y + (h - textHeight) * 0.5f + (float) font[f].ascent * scale);
            const auto canvasWidth = canvas.width();
            const auto alpha = (float) channel (colour, 24) / 255.0f;
            const auto solid = colour | 0xff000000u;

            for (const auto& item : laid)
            {
                const auto penX = left + item.x;
                auto ix = (int) std::floor (penX);
                auto subpixel = (int) std::lround ((penX - (float) ix) * 4.0f);

                if (subpixel >= 4)
                {
                    ++ix;
                    subpixel = 0;
                }

                const auto& image = glyphImage (f, size, item.glyph, subpixel);

                for (int row = 0; row < image.h; ++row)
                {
                    const auto py = (int) baseline + image.y0 + row;

                    if (py < clip[1] || py >= clip[3])
                        continue;

                    for (int col = 0; col < image.w; ++col)
                    {
                        const auto px = ix + image.x0 + col;
                        const auto coverage = image.alpha[(size_t) row * (size_t) image.w + (size_t) col];

                        if (px < clip[0] || px >= clip[2] || coverage == 0)
                            continue;

                        blend (pixels[(size_t) py * (size_t) canvasWidth + (size_t) px], premultiplied (solid, alpha * (float) coverage / 255.0f));
                    }
                }
            }
        }

        std::vector<float> points;      // x, y pairs
        std::vector<int> starts;        // where each contour starts, in points
        std::vector<float> accumulator;
        int width = 0, height = 0, stride = 0;

        Font font[2];
        std::unordered_map<std::uint64_t, Glyph> glyphs;
        std::vector<Laid> laid;
    };

    //==========================================================================
    struct CanvasAccess
    {
        static Canvas make (std::uint32_t* pixels, int w, int h, Painter& painter)
        {
            Canvas c;
            c.pixels_ = pixels;
            c.width_ = w;
            c.height_ = h;
            c.clip_[0] = 0;
            c.clip_[1] = 0;
            c.clip_[2] = w;
            c.clip_[3] = h;
            c.painter_ = &painter;
            return c;
        }

        static Painter& painter (Canvas& c)        { return *static_cast<Painter*> (c.painter_); }
        static std::uint32_t* pixels (Canvas& c)   { return c.pixels_; }
        static const int* clip (Canvas& c)         { return c.clip_; }
    };

    namespace
    {
        Paint solidPaint (Colour c)
        {
            Paint p;
            p.c1 = c;
            return p;
        }

        void fillWith (Canvas& c, const Paint& paint)
        {
            if (CanvasAccess::pixels (c) != nullptr)
                CanvasAccess::painter (c).fill (c, CanvasAccess::pixels (c), CanvasAccess::clip (c), paint);
        }
    }

    void Canvas::fillAll (Colour c)
    {
        fillRect ((float) clip_[0], (float) clip_[1], (float) (clip_[2] - clip_[0]), (float) (clip_[3] - clip_[1]), c);
    }

    void Canvas::fillRect (float x, float y, float w, float h, Colour c)
    {
        fillRoundedRect (x, y, w, h, 0.0f, c);
    }

    void Canvas::fillRoundedRect (float x, float y, float w, float h, float radius, Colour c)
    {
        if (w <= 0.0f || h <= 0.0f)
            return;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.roundedRect (x, y, w, h, radius, false);
        fillWith (*this, solidPaint (c));
    }

    void Canvas::fillEllipse (float x, float y, float w, float h, Colour c)
    {
        if (w <= 0.0f || h <= 0.0f)
            return;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.ellipse (x, y, w, h, false);
        fillWith (*this, solidPaint (c));
    }

    void Canvas::fillCircle (float cx, float cy, float radius, Colour c)
    {
        fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, c);
    }

    void Canvas::fillTriangle (float x0, float y0, float x1, float y1, float x2, float y2, Colour c)
    {
        const float xy[] { x0, y0, x1, y1, x2, y2 };
        fillPolygon (xy, 3, c);
    }

    void Canvas::fillPolygon (const float* xy, int numPoints, Colour c)
    {
        if (xy == nullptr || numPoints < 3)
            return;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.moveTo (xy[0], xy[1]);

        for (int i = 1; i < numPoints; ++i)
            p.lineTo (xy[i * 2], xy[i * 2 + 1]);

        fillWith (*this, solidPaint (c));
    }

    void Canvas::fillPie (float cx, float cy, float radius, float from, float to, Colour c)
    {
        if (radius <= 0.0f)
            return;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.moveTo (cx, cy);
        p.arc (cx, cy, radius, radius, from, to, false);
        fillWith (*this, solidPaint (c));
    }

    void Canvas::fillGradient (float x, float y, float w, float h, Colour top, Colour bottom, float radius)
    {
        if (w <= 0.0f || h <= 0.0f)
            return;

        Paint paint;
        paint.kind = Paint::vertical;
        paint.c1 = top;
        paint.c2 = bottom;
        paint.a = y;
        paint.b = y + h;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.roundedRect (x, y, w, h, radius, false);
        fillWith (*this, paint);
    }

    void Canvas::fillRadialGradient (float cx, float cy, float radius, Colour inside, Colour outside)
    {
        if (radius <= 0.0f)
            return;

        Paint paint;
        paint.kind = Paint::radial;
        paint.c1 = inside;
        paint.c2 = outside;
        paint.cx = cx;
        paint.cy = cy;
        paint.a = radius;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.ellipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, false);
        fillWith (*this, paint);
    }

    void Canvas::drawRect (float x, float y, float w, float h, float thickness, Colour c)
    {
        drawRoundedRect (x, y, w, h, 0.0f, thickness, c);
    }

    void Canvas::drawRoundedRect (float x, float y, float w, float h, float radius, float thickness, Colour c)
    {
        if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f)
            return;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.roundedRect (x, y, w, h, radius, false);

        if (thickness * 2.0f < std::min (w, h))
            p.roundedRect (x + thickness, y + thickness, w - thickness * 2.0f, h - thickness * 2.0f, std::max (0.0f, radius - thickness), true);

        fillWith (*this, solidPaint (c));
    }

    void Canvas::drawEllipse (float x, float y, float w, float h, float thickness, Colour c)
    {
        if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f)
            return;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.ellipse (x, y, w, h, false);

        if (thickness * 2.0f < std::min (w, h))
            p.ellipse (x + thickness, y + thickness, w - thickness * 2.0f, h - thickness * 2.0f, true);

        fillWith (*this, solidPaint (c));
    }

    void Canvas::drawLine (float x0, float y0, float x1, float y1, float thickness, Colour c)
    {
        if (thickness <= 0.0f)
            return;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.capsule (x0, y0, x1, y1, thickness * 0.5f);
        fillWith (*this, solidPaint (c));
    }

    void Canvas::drawPolyline (const float* xy, int numPoints, float thickness, Colour c)
    {
        if (xy == nullptr || numPoints < 2 || thickness <= 0.0f)
            return;

        // One capsule per piece, all in one fill: where they overlap, it's still covered once.
        auto& p = CanvasAccess::painter (*this);
        p.begin();

        for (int i = 0; i + 1 < numPoints; ++i)
            p.capsule (xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], thickness * 0.5f);

        fillWith (*this, solidPaint (c));
    }

    void Canvas::drawArc (float cx, float cy, float radius, float from, float to, float thickness, Colour c, bool roundEnds)
    {
        if (thickness <= 0.0f || radius <= 0.0f || std::fabs (to - from) < 1.0e-4f)
            return;

        const auto half = thickness * 0.5f;
        const auto outer = radius + half, inner = std::max (0.0f, radius - half);
        const auto sense = to > from ? 1.0f : -1.0f;

        auto& p = CanvasAccess::painter (*this);
        p.begin();
        p.arc (cx, cy, outer, outer, from, to, true);

        if (roundEnds)
        {
            float ex, ey;
            Painter::onEllipse (cx, cy, radius, radius, to, ex, ey);
            p.arc (ex, ey, half, half, to, to + sense * pi, false);
        }

        if (inner > 0.0f) p.arc (cx, cy, inner, inner, to, from, false);
        else              p.lineTo (cx, cy);

        if (roundEnds)
        {
            float sx, sy;
            Painter::onEllipse (cx, cy, radius, radius, from, sx, sy);
            p.arc (sx, sy, half, half, from + sense * pi, from + sense * 2.0f * pi, false);
        }

        fillWith (*this, solidPaint (c));
    }

    void Canvas::drawText (const std::string& text, float x, float y, float w, float h, float size, Colour c, Align align, bool bold)
    {
        if (pixels_ != nullptr)
            CanvasAccess::painter (*this).drawText (*this, pixels_, clip_, text, x, y, w, h, size, c, align, bold);
    }

    float Canvas::textWidth (const std::string& text, float size, bool bold)
    {
        return CanvasAccess::painter (*this).textWidth (text, std::max (4.0f, std::min (200.0f, size)), bold);
    }

    void Canvas::setClip (float x, float y, float w, float h)
    {
        clip_[0] = std::max (0, std::min (width_, (int) std::floor (x)));
        clip_[1] = std::max (0, std::min (height_, (int) std::floor (y)));
        clip_[2] = std::max (clip_[0], std::min (width_, (int) std::ceil (x + w)));
        clip_[3] = std::max (clip_[1], std::min (height_, (int) std::ceil (y + h)));
    }

    void Canvas::resetClip()
    {
        clip_[0] = clip_[1] = 0;
        clip_[2] = width_;
        clip_[3] = height_;
    }

    //==========================================================================
    // The registry
    namespace
    {
        struct TypeInfo
        {
            std::string name, description;
            Element* (*make)();
        };

        std::vector<TypeInfo>& registry()
        {
            static std::vector<TypeInfo> types;
            return types;
        }

        std::string& registryProblem()
        {
            static std::string problem;
            return problem;
        }
    }

    Registrar::Registrar (const char* name, const char* description, Element* (*make)())
    {
        const std::string n = name != nullptr ? name : "";

        for (const auto& t : registry())
        {
            if (t.name == n)
            {
                registryProblem() += "Two elements are called " + n + ": give each its own name. ";
                return;
            }
        }

        registry().push_back ({ n, description != nullptr ? description : "", make });
    }

    //==========================================================================
    // An element as the runtime keeps it
    struct Instance
    {
        ElementHost* host = nullptr;
        Painter* painter = nullptr;
        int id = -1;
        std::unique_ptr<Element> element;

        int width = 0, height = 0;
        std::vector<std::uint32_t> pixels, popupPixels;
        bool popupOpen = false;
        int popupX = 0, popupY = 0, popupW = 0, popupH = 0;

        // What the last render read: if any of it changes, it's drawn again.
        struct Read
        {
            int kind;     // 0: a parameter's value (a: its slot), 1: a note (a: which)
            int a;
            float value;
        };

        std::vector<Read> reads;
        bool dirty = true, animated = false, recording = false;

        std::map<std::string, int> slots;
        std::map<int, int> gestures;   // open edits, by slot
        std::set<int> notesOn;

        int slotOf (const char* role)
        {
            const std::string key = role != nullptr ? role : "";

            if (const auto found = slots.find (key); found != slots.end())
                return found->second;

            const auto slot = host->findSlot (id, key);
            slots[key] = slot;
            return slot;
        }

        bool info (const char* role, int what, float& result)
        {
            const auto slot = slotOf (role);

            if (slot < 0 || ! host->paramInfo (id, slot, what, result) || ! std::isfinite (result))
                return false;

            if (recording && what == ElementHost::valueInfo)
                reads.push_back ({ 0, slot, result });

            return true;
        }

        std::string text (int which, int index, const std::string& key = {})
        {
            std::string result;
            return host->text (id, which, index, key, result) ? result : std::string();
        }

        void endAllEdits()
        {
            for (const auto& [slot, depth] : gestures)
                if (depth > 0)
                    host->gesture (id, slot, false);

            gestures.clear();
        }

        void stopNotes()
        {
            for (const auto note : notesOn)
                host->playNote (id, note, 0.0f);

            notesOn.clear();
        }

        bool changed()
        {
            for (const auto& read : reads)
            {
                float now = 0.0f;

                if (read.kind == 0)
                {
                    if (! host->paramInfo (id, read.a, ElementHost::valueInfo, now))
                        return true;
                }
                else
                {
                    now = host->isNoteDown (read.a) ? 1.0f : 0.0f;
                }

                std::uint32_t a = 0, b = 0;
                std::memcpy (&a, &now, sizeof (a));
                std::memcpy (&b, &read.value, sizeof (b));

                if (a != b)
                    return true;
            }

            return false;
        }
    };

    struct ElementAccess
    {
        static Instance* of (const Element* e)   { return static_cast<Instance*> (e->instance_); }
        static void attach (Element& e, Instance* i)   { e.instance_ = i; }
    };

    namespace
    {
        Instance* instanceOf (const Element* e)   { return ElementAccess::of (e); }
    }

    //==========================================================================
    // What an element can ask
    int Element::width() const               { const auto* i = instanceOf (this); return i != nullptr ? i->width : 0; }
    int Element::height() const              { const auto* i = instanceOf (this); return i != nullptr ? i->height : 0; }

    bool Element::hasParam (const char* role) const
    {
        auto* i = instanceOf (this);
        float v = 0.0f;
        return i != nullptr && i->info (role, ElementHost::minInfo, v);
    }

    float Element::value (const char* role) const
    {
        auto* i = instanceOf (this);
        float v = 0.0f;
        return i != nullptr && i->info (role, ElementHost::valueInfo, v) ? v : 0.0f;
    }

    float Element::minimum (const char* role) const
    {
        auto* i = instanceOf (this);
        float v = 0.0f;
        return i != nullptr && i->info (role, ElementHost::minInfo, v) ? v : 0.0f;
    }

    float Element::maximum (const char* role) const
    {
        auto* i = instanceOf (this);
        float v = 1.0f;
        return i != nullptr && i->info (role, ElementHost::maxInfo, v) ? v : 1.0f;
    }

    float Element::defaultValue (const char* role) const
    {
        auto* i = instanceOf (this);
        float v = 0.0f;
        return i != nullptr && i->info (role, ElementHost::defaultInfo, v) ? v : 0.0f;
    }

    float Element::normalised (const char* role) const
    {
        auto* i = instanceOf (this);
        float v = 0.0f, low = 0.0f, high = 1.0f, skew = 1.0f;

        if (i == nullptr || ! i->info (role, ElementHost::valueInfo, v))
            return 0.0f;

        i->info (role, ElementHost::minInfo, low);
        i->info (role, ElementHost::maxInfo, high);
        i->info (role, ElementHost::skewInfo, skew);

        const auto t = clamp01 ((v - low) / std::max (1.0e-9f, high - low));
        return std::fabs (skew - 1.0f) > 1.0e-4f && skew > 0.0f ? std::pow (t, skew) : t;
    }

    int Element::choices (const char* role) const
    {
        if (! hasParam (role))
            return 0;

        return std::max (1, std::min (1000, (int) std::lround (maximum (role) - minimum (role)) + 1));
    }

    std::string Element::paramName (const char* role) const
    {
        auto* i = instanceOf (this);
        const auto slot = i != nullptr ? i->slotOf (role) : -1;
        return slot >= 0 ? i->text (ElementHost::paramNameText, slot) : std::string();
    }

    std::string Element::unit (const char* role) const
    {
        auto* i = instanceOf (this);
        const auto slot = i != nullptr ? i->slotOf (role) : -1;
        return slot >= 0 ? i->text (ElementHost::unitText, slot) : std::string();
    }

    std::string Element::valueText (const char* role) const
    {
        if (! hasParam (role))
            return {};

        char number[64];
        std::snprintf (number, sizeof (number), "%.*f", maximum (role) - minimum (role) >= 100.0f ? 0 : 2, (double) value (role));
        const auto u = unit (role);
        return u.empty() ? std::string (number) : std::string (number) + " " + u;
    }

    void Element::setValue (float newValue, const char* role)
    {
        auto* i = instanceOf (this);
        const auto slot = i != nullptr ? i->slotOf (role) : -1;
        float low = 0.0f, high = 1.0f;

        if (slot < 0 || ! i->info (role, ElementHost::minInfo, low) || ! i->info (role, ElementHost::maxInfo, high) || ! std::isfinite (newValue))
            return;

        newValue = std::max (low, std::min (high, newValue));
        const bool alone = i->gestures[slot] <= 0;

        if (alone) i->host->gesture (i->id, slot, true);
        i->host->setValue (i->id, slot, newValue);
        if (alone) i->host->gesture (i->id, slot, false);

        i->dirty = true;
    }

    void Element::setNormalised (float proportion, const char* role)
    {
        auto* i = instanceOf (this);
        float low = 0.0f, high = 1.0f, skew = 1.0f;

        if (i == nullptr || ! i->info (role, ElementHost::minInfo, low) || ! i->info (role, ElementHost::maxInfo, high))
            return;

        i->info (role, ElementHost::skewInfo, skew);
        auto t = clamp01 (proportion);

        if (std::fabs (skew - 1.0f) > 1.0e-4f && skew > 0.0f)
            t = std::pow (t, 1.0f / skew);

        setValue (low + (high - low) * t, role);
    }

    void Element::beginEdit (const char* role)
    {
        auto* i = instanceOf (this);
        const auto slot = i != nullptr ? i->slotOf (role) : -1;

        if (slot >= 0 && i->gestures[slot]++ == 0)
            i->host->gesture (i->id, slot, true);
    }

    void Element::endEdit (const char* role)
    {
        auto* i = instanceOf (this);
        const auto slot = i != nullptr ? i->slotOf (role) : -1;

        if (slot >= 0 && i->gestures[slot] > 0 && --i->gestures[slot] == 0)
            i->host->gesture (i->id, slot, false);
    }

    std::string Element::label() const
    {
        auto* i = instanceOf (this);
        return i != nullptr ? i->text (ElementHost::labelText, 0) : std::string();
    }

    int Element::numOptions() const
    {
        auto* i = instanceOf (this);
        return i != nullptr ? std::max (0, i->host->numOptions (i->id)) : 0;
    }

    std::string Element::option (int index) const
    {
        auto* i = instanceOf (this);
        return i != nullptr && index >= 0 ? i->text (ElementHost::optionText, index) : std::string();
    }

    Colour Element::colour (Colour fallback) const
    {
        auto* i = instanceOf (this);
        const auto c = i != nullptr ? i->host->colour (i->id) : 0u;
        return c != 0 ? c : fallback;
    }

    float Element::setting (const char* key, float fallback) const
    {
        const auto text = settingText (key);

        if (text.empty())
            return fallback;

        char* end = nullptr;
        const auto v = std::strtod (text.c_str(), &end);
        return end != text.c_str() && std::isfinite (v) ? (float) v : fallback;
    }

    std::string Element::settingText (const char* key, const std::string& fallback) const
    {
        auto* i = instanceOf (this);
        std::string result;

        if (i == nullptr || key == nullptr || ! i->host->text (i->id, ElementHost::settingText, 0, key, result))
            return fallback;

        return result;
    }

    float Element::level (bool rms) const
    {
        auto* i = instanceOf (this);

        if (i == nullptr)
            return 0.0f;

        if (i->recording)
            i->animated = true;

        return i->host->level (i->id, rms);
    }

    void Element::readScope (float* destination, int numSamples) const
    {
        auto* i = instanceOf (this);

        if (destination == nullptr || numSamples <= 0)
            return;

        if (i == nullptr)
        {
            std::fill (destination, destination + numSamples, 0.0f);
            return;
        }

        if (i->recording)
            i->animated = true;

        i->host->readScope (i->id, destination, numSamples);
    }

    double Element::seconds() const
    {
        auto* i = instanceOf (this);

        if (i == nullptr)
            return 0.0;

        if (i->recording)
            i->animated = true;

        return i->host->seconds();
    }

    void Element::playNote (int note, float velocity)
    {
        auto* i = instanceOf (this);

        if (i == nullptr || note < 0 || note > 127)
            return;

        if (velocity > 0.0f) i->notesOn.insert (note);
        else                 i->notesOn.erase (note);

        i->host->playNote (i->id, note, clamp01 (velocity));
    }

    bool Element::isNoteDown (int note) const
    {
        auto* i = instanceOf (this);

        if (i == nullptr || note < 0 || note > 127)
            return false;

        const auto down = i->host->isNoteDown (note);

        if (i->recording)
            i->reads.push_back ({ 1, note, down ? 1.0f : 0.0f });

        return down;
    }

    void Element::repaint()
    {
        if (auto* i = instanceOf (this))
            i->dirty = true;
    }

    void Element::openPopup (float x, float y, float w, float h)
    {
        auto* i = instanceOf (this);

        if (i == nullptr || ! std::isfinite (x) || ! std::isfinite (y) || ! std::isfinite (w) || ! std::isfinite (h))
            return;

        i->popupOpen = true;
        i->popupX = (int) std::floor (x);
        i->popupY = (int) std::floor (y);
        i->popupW = std::max (1, std::min (4096, (int) std::ceil (w)));
        i->popupH = std::max (1, std::min (4096, (int) std::ceil (h)));
        i->dirty = true;
    }

    void Element::closePopup()
    {
        if (auto* i = instanceOf (this))
        {
            i->popupOpen = false;
            i->dirty = true;
        }
    }

    bool Element::isPopupOpen() const
    {
        const auto* i = instanceOf (this);
        return i != nullptr && i->popupOpen;
    }

    //==========================================================================
    struct ElementRuntime::Impl
    {
        explicit Impl (ElementHost& h) : host (h) {}

        Instance* find (int element) const
        {
            const auto found = instances.find (element);
            return found != instances.end() ? found->second.get() : nullptr;
        }

        ElementHost& host;
        Painter painter;
        std::map<int, std::unique_ptr<Instance>> instances;
    };

    ElementRuntime::ElementRuntime (ElementHost& host)
        : impl (std::make_unique<Impl> (host))
    {
    }

    ElementRuntime::~ElementRuntime()
    {
        for (auto& [id, instance] : impl->instances)
        {
            instance->endAllEdits();
            instance->stopNotes();
        }
    }

    int ElementRuntime::numTypes()                       { return (int) registry().size(); }
    const char* ElementRuntime::problem()                { return registryProblem().c_str(); }

    const char* ElementRuntime::typeName (int index)
    {
        return index >= 0 && index < numTypes() ? registry()[(size_t) index].name.c_str() : "";
    }

    const char* ElementRuntime::typeDescription (int index)
    {
        return index >= 0 && index < numTypes() ? registry()[(size_t) index].description.c_str() : "";
    }

    bool ElementRuntime::create (int element, const char* type, int width, int height)
    {
        destroy (element);

        const std::string name = type != nullptr ? type : "";
        const TypeInfo* info = nullptr;

        for (const auto& t : registry())
            if (t.name == name)
                info = &t;

        if (info == nullptr || info->make == nullptr)
            return false;

        auto instance = std::make_unique<Instance>();
        instance->host = &impl->host;
        instance->painter = &impl->painter;
        instance->id = element;
        instance->width = std::max (0, std::min (4096, width));
        instance->height = std::max (0, std::min (4096, height));
        instance->element.reset (info->make());

        if (instance->element == nullptr)
            return false;

        ElementAccess::attach (*instance->element, instance.get());
        auto* raw = instance.get();
        impl->instances[element] = std::move (instance);
        raw->element->resized();
        raw->dirty = true;
        return true;
    }

    void ElementRuntime::destroy (int element)
    {
        const auto found = impl->instances.find (element);

        if (found == impl->instances.end())
            return;

        found->second->endAllEdits();
        found->second->stopNotes();
        impl->instances.erase (found);
    }

    void ElementRuntime::resize (int element, int width, int height)
    {
        auto* i = impl->find (element);
        width = std::max (0, std::min (4096, width));
        height = std::max (0, std::min (4096, height));

        if (i == nullptr || (i->width == width && i->height == height))
            return;

        i->width = width;
        i->height = height;
        i->element->resized();
        i->dirty = true;
    }

    void ElementRuntime::invalidate (int element)
    {
        if (auto* i = impl->find (element))
        {
            i->slots.clear();   // its roles may be bound differently now
            i->dirty = true;
        }
    }

    bool ElementRuntime::needsPaint (int element)
    {
        auto* i = impl->find (element);
        return i != nullptr && (i->dirty || i->animated || i->changed());
    }

    bool ElementRuntime::render (int element)
    {
        auto* i = impl->find (element);

        if (i == nullptr)
            return false;

        i->reads.clear();
        i->animated = false;
        i->dirty = false;
        i->recording = true;

        i->pixels.assign ((size_t) i->width * (size_t) i->height, 0u);

        if (i->width > 0 && i->height > 0)
        {
            auto canvas = CanvasAccess::make (i->pixels.data(), i->width, i->height, impl->painter);
            i->element->paint (canvas);
        }

        if (i->popupOpen)
        {
            i->popupPixels.assign ((size_t) i->popupW * (size_t) i->popupH, 0u);
            auto canvas = CanvasAccess::make (i->popupPixels.data(), i->popupW, i->popupH, impl->painter);
            i->element->paintPopup (canvas);
        }
        else
        {
            i->popupPixels.clear();
        }

        i->recording = false;
        return true;
    }

    const std::uint32_t* ElementRuntime::pixels (int element, bool popup) const
    {
        const auto* i = impl->find (element);

        if (i == nullptr)
            return nullptr;

        const auto& buffer = popup ? i->popupPixels : i->pixels;
        return buffer.empty() ? nullptr : buffer.data();
    }

    bool ElementRuntime::popupArea (int element, int& x, int& y, int& w, int& h) const
    {
        const auto* i = impl->find (element);

        if (i == nullptr || ! i->popupOpen)
            return false;

        x = i->popupX;
        y = i->popupY;
        w = i->popupW;
        h = i->popupH;
        return true;
    }

    bool ElementRuntime::mouse (int element, bool popup, int event, const Mouse& m, float notches)
    {
        auto* i = impl->find (element);

        if (i == nullptr || (popup && ! i->popupOpen))
            return false;

        auto& e = *i->element;
        i->dirty = true;

        switch (event)
        {
            case down:
                if (popup)
                {
                    e.popupMouseDown (m);
                    return true;
                }

                return e.mouseDown (m);

            case drag:   if (popup) e.popupMouseDrag (m); else e.mouseDrag (m); return true;
            case up:     if (popup) e.popupMouseUp (m); else e.mouseUp (m); i->endAllEdits(); return true;
            case move:   if (popup) e.popupMouseMove (m); else e.mouseMove (m); return true;
            case exit:   if (popup) e.popupMouseExit(); else e.mouseExit(); return true;
            case wheel:  if (popup) e.popupMouseWheel (m, notches); else e.mouseWheel (m, notches); return true;
            default:     return false;
        }
    }

    void ElementRuntime::closePopup (int element)
    {
        auto* i = impl->find (element);

        if (i == nullptr || ! i->popupOpen)
            return;

        i->popupOpen = false;
        i->dirty = true;
        i->element->popupClosed();
    }
}

//==============================================================================
// The studio runs the elements as WebAssembly: what they ask goes out through these
// imports, and the studio drives them through these exports.
#if defined (__wasm__)

#define STELLA_IMPORT(name) __attribute__ ((import_module ("stella"), import_name (name)))
#define STELLA_UI_EXPORT(name) extern "C" __attribute__ ((export_name (name)))

extern "C"
{
    STELLA_IMPORT ("param_info")  float stella_host_param_info (int element, int slot, int info);      // NaN: none
    STELLA_IMPORT ("find_slot")   int stella_host_find_slot (int element, const char* role, int length);
    STELLA_IMPORT ("set_value")   void stella_host_set_value (int element, int slot, float value);
    STELLA_IMPORT ("gesture")     void stella_host_gesture (int element, int slot, int starts);
    STELLA_IMPORT ("text")        int stella_host_text (int element, int which, int index, const char* key, int keyLength, char* buffer, int capacity);
    STELLA_IMPORT ("num_options") int stella_host_num_options (int element);
    STELLA_IMPORT ("colour")      unsigned int stella_host_colour (int element);
    STELLA_IMPORT ("level")       float stella_host_level (int element, int rms);
    STELLA_IMPORT ("scope")       void stella_host_scope (int element, float* destination, int numSamples);
    STELLA_IMPORT ("note")        void stella_host_note (int element, int note, float velocity);
    STELLA_IMPORT ("note_down")   int stella_host_note_down (int note);
    STELLA_IMPORT ("seconds")     double stella_host_seconds();
}

namespace
{
    using namespace stella::ui;

    struct ImportHost final : ElementHost
    {
        bool paramInfo (int element, int slot, int info, float& result) override
        {
            result = stella_host_param_info (element, slot, info);
            return std::isfinite (result);
        }

        int findSlot (int element, const std::string& role) override
        {
            return stella_host_find_slot (element, role.data(), (int) role.size());
        }

        void setValue (int element, int slot, float newValue) override   { stella_host_set_value (element, slot, newValue); }
        void gesture (int element, int slot, bool starts) override        { stella_host_gesture (element, slot, starts ? 1 : 0); }

        bool text (int element, int which, int index, const std::string& key, std::string& result) override
        {
            std::vector<char> buffer (1024);

            for (;;)
            {
                const auto length = stella_host_text (element, which, index, key.data(), (int) key.size(), buffer.data(), (int) buffer.size());

                if (length < 0)
                    return false;

                if (length <= (int) buffer.size() || buffer.size() >= 65536)
                {
                    result.assign (buffer.data(), (size_t) std::min (length, (int) buffer.size()));
                    return true;
                }

                buffer.resize ((size_t) length);
            }
        }

        int numOptions (int element) override                              { return stella_host_num_options (element); }
        std::uint32_t colour (int element) override                        { return stella_host_colour (element); }
        float level (int element, bool rms) override                       { return stella_host_level (element, rms ? 1 : 0); }
        void readScope (int element, float* destination, int n) override   { stella_host_scope (element, destination, n); }
        void playNote (int element, int note, float velocity) override    { stella_host_note (element, note, velocity); }
        bool isNoteDown (int note) override                                { return stella_host_note_down (note) != 0; }
        double seconds() override                                          { return stella_host_seconds(); }
    };

    ElementRuntime& runtime()
    {
        static ImportHost host;
        static ElementRuntime instance (host);
        return instance;
    }

    std::vector<char>& scratch()
    {
        static std::vector<char> buffer (256);
        return buffer;
    }
}

STELLA_UI_EXPORT ("stella_ui_api_version") int stellaUiApiVersion()                  { return 1; }
STELLA_UI_EXPORT ("stella_ui_type_count") int stellaUiTypeCount()                    { return ElementRuntime::numTypes(); }
STELLA_UI_EXPORT ("stella_ui_type_name") const char* stellaUiTypeName (int i)        { return ElementRuntime::typeName (i); }
STELLA_UI_EXPORT ("stella_ui_type_description") const char* stellaUiTypeDescription (int i) { return ElementRuntime::typeDescription (i); }
STELLA_UI_EXPORT ("stella_ui_problem") const char* stellaUiProblem()                 { return ElementRuntime::problem(); }

STELLA_UI_EXPORT ("stella_ui_scratch") char* stellaUiScratch (int size)
{
    auto& buffer = scratch();

    if (size > 0 && (size_t) size + 1 > buffer.size())
        buffer.resize ((size_t) size + 1);

    return buffer.data();
}

STELLA_UI_EXPORT ("stella_ui_create") int stellaUiCreate (int element, const char* type, int length, int width, int height)
{
    const std::string name (type, (size_t) std::max (0, length));
    return runtime().create (element, name.c_str(), width, height) ? 1 : 0;
}

STELLA_UI_EXPORT ("stella_ui_destroy") void stellaUiDestroy (int element)                     { runtime().destroy (element); }
STELLA_UI_EXPORT ("stella_ui_resize") void stellaUiResize (int element, int w, int h)         { runtime().resize (element, w, h); }
STELLA_UI_EXPORT ("stella_ui_invalidate") void stellaUiInvalidate (int element)               { runtime().invalidate (element); }
STELLA_UI_EXPORT ("stella_ui_needs_paint") int stellaUiNeedsPaint (int element)               { return runtime().needsPaint (element) ? 1 : 0; }
STELLA_UI_EXPORT ("stella_ui_render") int stellaUiRender (int element)                        { return runtime().render (element) ? 1 : 0; }
STELLA_UI_EXPORT ("stella_ui_pixels") const std::uint32_t* stellaUiPixels (int element, int popup) { return runtime().pixels (element, popup != 0); }
STELLA_UI_EXPORT ("stella_ui_close_popup") void stellaUiClosePopup (int element)              { runtime().closePopup (element); }

STELLA_UI_EXPORT ("stella_ui_popup") int stellaUiPopup (int element, int what)
{
    int x = 0, y = 0, w = 0, h = 0;

    if (! runtime().popupArea (element, x, y, w, h))
        return 0;

    switch (what)
    {
        case 0:  return 1;
        case 1:  return x;
        case 2:  return y;
        case 3:  return w;
        case 4:  return h;
        default: return 0;
    }
}

STELLA_UI_EXPORT ("stella_ui_mouse") int stellaUiMouse (int element, int popup, int event, float x, float y, int flags, float notches)
{
    Mouse m;
    m.x = x;
    m.y = y;
    m.shift = (flags & 1) != 0;
    m.doubleClick = (flags & 2) != 0;
    return runtime().mouse (element, popup != 0, event, m, notches) ? 1 : 0;
}

#endif
