// C:\workspace\Stella AI Studio\src\stella_gui.cpp
//
// The exported plugin's GUI (see stella_gui.h): a small software renderer and the same
// control behaviour as the studio's Play mode.

#include "stella_gui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace stella::gui
{
    namespace
    {
        //======================================================================
        // Pixels are 0xAARRGGBB, premultiplied.
        std::uint32_t premultiplied (std::uint32_t argb, float alpha)
        {
            const auto a = (float) (argb >> 24) * std::min (1.0f, std::max (0.0f, alpha)) / 255.0f;
            const auto r = (std::uint32_t) ((float) ((argb >> 16) & 0xff) * a + 0.5f);
            const auto g = (std::uint32_t) ((float) ((argb >> 8) & 0xff) * a + 0.5f);
            const auto b = (std::uint32_t) ((float) (argb & 0xff) * a + 0.5f);
            return ((std::uint32_t) (a * 255.0f + 0.5f) << 24) | (r << 16) | (g << 8) | b;
        }

        void blend (std::uint32_t& dst, std::uint32_t src)
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

            // Saturating add of the premultiplied source.
            std::uint32_t out = 0;

            for (int shift = 0; shift < 32; shift += 8)
            {
                const auto sum = ((under >> shift) & 0xff) + ((src >> shift) & 0xff);
                out |= (sum > 255 ? 255u : sum) << shift;
            }

            dst = out;
        }

        struct Canvas
        {
            std::uint32_t* pixels;
            int width, height;

            void plot (int x, int y, std::uint32_t argb, float alpha)
            {
                if (x < 0 || y < 0 || x >= width || y >= height || alpha <= 0.0f)
                    return;

                blend (pixels[(size_t) y * (size_t) width + (size_t) x], premultiplied (argb, alpha));
            }

            void fillRect (float x0, float y0, float x1, float y1, std::uint32_t argb, float alpha)
            {
                const auto left = std::max (0, (int) std::floor (x0)), right = std::min (width, (int) std::ceil (x1));
                const auto top = std::max (0, (int) std::floor (y0)), bottom = std::min (height, (int) std::ceil (y1));

                for (int y = top; y < bottom; ++y)
                {
                    const auto cy = std::min (1.0f, std::min ((float) y + 1.0f, y1) - std::max ((float) y, y0));

                    for (int x = left; x < right; ++x)
                    {
                        const auto cx = std::min (1.0f, std::min ((float) x + 1.0f, x1) - std::max ((float) x, x0));
                        plot (x, y, argb, alpha * cx * cy);
                    }
                }
            }

            void line (float x0, float y0, float x1, float y1, float thickness, std::uint32_t argb, float alpha)
            {
                const auto half = thickness * 0.5f;
                const auto left = std::max (0, (int) std::floor (std::min (x0, x1) - half - 1.0f));
                const auto right = std::min (width - 1, (int) std::ceil (std::max (x0, x1) + half + 1.0f));
                const auto top = std::max (0, (int) std::floor (std::min (y0, y1) - half - 1.0f));
                const auto bottom = std::min (height - 1, (int) std::ceil (std::max (y0, y1) + half + 1.0f));

                const auto dx = x1 - x0, dy = y1 - y0;
                const auto lengthSquared = dx * dx + dy * dy;

                for (int y = top; y <= bottom; ++y)
                {
                    for (int x = left; x <= right; ++x)
                    {
                        const auto px = (float) x + 0.5f - x0, py = (float) y + 0.5f - y0;
                        const auto t = lengthSquared > 0.0f ? std::min (1.0f, std::max (0.0f, (px * dx + py * dy) / lengthSquared)) : 0.0f;
                        const auto ex = px - t * dx, ey = py - t * dy;
                        const auto coverage = half + 0.5f - std::sqrt (ex * ex + ey * ey);

                        if (coverage > 0.0f)
                            plot (x, y, argb, alpha * std::min (1.0f, coverage));
                    }
                }
            }

            void circle (float cx, float cy, float radius, std::uint32_t argb, float alpha)
            {
                for (int y = (int) std::floor (cy - radius - 1.0f); y <= (int) std::ceil (cy + radius + 1.0f); ++y)
                {
                    for (int x = (int) std::floor (cx - radius - 1.0f); x <= (int) std::ceil (cx + radius + 1.0f); ++x)
                    {
                        const auto dx = (float) x + 0.5f - cx, dy = (float) y + 0.5f - cy;
                        const auto coverage = radius + 0.5f - std::sqrt (dx * dx + dy * dy);

                        if (coverage > 0.0f)
                            plot (x, y, argb, alpha * std::min (1.0f, coverage));
                    }
                }
            }

            /** A polyline, and optionally the area under it down to bottom. */
            void curve (const std::vector<float>& xs, const std::vector<float>& ys, float bottom, std::uint32_t argb, float fillAlpha)
            {
                if (xs.size() < 2)
                    return;

                if (fillAlpha > 0.0f)
                {
                    // One pass over the columns, so each column is filled exactly once.
                    size_t segment = 0;

                    for (int x = (int) std::floor (xs.front()); x < (int) std::ceil (xs.back()); ++x)
                    {
                        const auto centre = (float) x + 0.5f;

                        while (segment + 2 < xs.size() && xs[segment + 1] < centre)
                            ++segment;

                        const auto span = xs[segment + 1] - xs[segment];
                        const auto t = span > 0.0f ? std::min (1.0f, std::max (0.0f, (centre - xs[segment]) / span)) : 0.0f;
                        const auto top = ys[segment] + (ys[segment + 1] - ys[segment]) * t;
                        fillRect ((float) x, top, (float) x + 1.0f, bottom, argb, fillAlpha);
                    }
                }

                for (size_t i = 0; i + 1 < xs.size(); ++i)
                    line (xs[i], ys[i], xs[i + 1], ys[i + 1], 2.0f, argb, 1.0f);
            }
        };

        std::uint32_t mix (std::uint32_t a, std::uint32_t b, float t)
        {
            std::uint32_t out = 0xff000000u;

            for (int shift = 0; shift < 24; shift += 8)
            {
                const auto ca = (float) ((a >> shift) & 0xff), cb = (float) ((b >> shift) & 0xff);
                out |= (std::uint32_t) (ca + (cb - ca) * t + 0.5f) << shift;
            }

            return out;
        }

        constexpr std::uint32_t green = 0xff00c853, amber = 0xffffb300, red = 0xffd50000;
    }

    //==========================================================================
    Editor::Image Editor::decode (const unsigned char* png, int size)
    {
        Image image;
        int w = 0, h = 0, channels = 0;
        auto* rgba = stbi_load_from_memory (png, size, &w, &h, &channels, 4);

        if (rgba == nullptr)
            return image;

        image.width = w;
        image.height = h;
        image.pixels.resize ((size_t) w * (size_t) h);

        for (size_t i = 0; i < image.pixels.size(); ++i)
        {
            const auto* p = rgba + i * 4;
            const auto a = (std::uint32_t) p[3];
            const auto r = (std::uint32_t) p[0] * a / 255, g = (std::uint32_t) p[1] * a / 255, b = (std::uint32_t) p[2] * a / 255;
            image.pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }

        stbi_image_free (rgba);
        return image;
    }

    Editor::Editor (Host& h)
        : host (h)
    {
        background = decode (backgroundPng, backgroundPngSize);

        for (int i = 0; i < numStrips; ++i)
            frames.push_back (decode (strips[i].png, strips[i].size));

        for (int i = 0; i < numWidgets; ++i)
        {
            const auto& w = widgets[i];
            Bound b;
            b.param = w.param != nullptr ? host.findParam (w.param) : -1;
            b.param2 = w.param2 != nullptr ? host.findParam (w.param2) : -1;
            b.param3 = w.param3 != nullptr ? host.findParam (w.param3) : -1;
            b.param4 = w.param4 != nullptr ? host.findParam (w.param4) : -1;
            bound.push_back (b);
        }

        meterLevels.assign ((size_t) numWidgets, 0.0f);
        scopeBuffer.assign (2048, 0.0f);
    }

    Editor::~Editor() = default;

    //==========================================================================
    float Editor::proportionOf (int param) const
    {
        if (param < 0)
            return 0.0f;

        float min = 0.0f, max = 1.0f, def = 0.0f, skew = 1.0f;
        host.getRange (param, min, max, def, skew);
        const auto t = std::min (1.0f, std::max (0.0f, (host.getValue (param) - min) / std::max (1.0e-9f, max - min)));
        return std::abs (skew - 1.0f) > 1.0e-4f && skew > 0.0f ? std::pow (t, skew) : t;
    }

    float Editor::valueAt (int param, float proportion) const
    {
        float min = 0.0f, max = 1.0f, def = 0.0f, skew = 1.0f;
        host.getRange (param, min, max, def, skew);
        auto t = std::min (1.0f, std::max (0.0f, proportion));

        if (std::abs (skew - 1.0f) > 1.0e-4f && skew > 0.0f)
            t = std::pow (t, 1.0f / skew);

        return min + (max - min) * t;
    }

    int Editor::widgetAt (int x, int y) const
    {
        for (int i = numWidgets; --i >= 0;)
        {
            const auto& w = widgets[i];
            const bool interactive = w.kind == Kind::knob || w.kind == Kind::slider || w.kind == Kind::toggle
                                  || w.kind == Kind::selector || w.kind == Kind::xy || w.kind == Kind::preset;

            if (interactive && x >= w.x && y >= w.y && x < w.x + w.w && y < w.y + w.h)
                return i;
        }

        return -1;
    }

    void Editor::setFromPointer (int index, int x, int y)
    {
        const auto& w = widgets[index];
        const auto& b = bound[(size_t) index];

        if (w.kind == Kind::slider && b.param >= 0)
        {
            const auto t = w.vertical ? ((float) (w.y + w.h - 4) - (float) y) / (float) std::max (1, w.h - 8)
                                      : ((float) x - (float) (w.x + 4)) / (float) std::max (1, w.w - 8);
            host.setValue (b.param, valueAt (b.param, t));
        }
        else if (w.kind == Kind::xy)
        {
            if (b.param >= 0)  host.setValue (b.param, valueAt (b.param, ((float) x - (float) (w.x + 2)) / (float) std::max (1, w.w - 4)));
            if (b.param2 >= 0) host.setValue (b.param2, valueAt (b.param2, ((float) (w.y + w.h - 2) - (float) y) / (float) std::max (1, w.h - 4)));
        }
    }

    void Editor::mouseDown (int x, int y, bool fine, bool doubleClick)
    {
        (void) fine;
        const auto index = widgetAt (x, y);

        if (index < 0)
            return;

        const auto& w = widgets[index];
        const auto& b = bound[(size_t) index];

        if (doubleClick && (w.kind == Kind::knob || w.kind == Kind::slider) && b.param >= 0)
        {
            float min = 0.0f, max = 1.0f, def = 0.0f, skew = 1.0f;
            host.getRange (b.param, min, max, def, skew);
            host.beginEdit (b.param);
            host.setValue (b.param, def);
            host.endEdit (b.param);
            return;
        }

        switch (w.kind)
        {
            case Kind::knob:
                if (b.param < 0) return;
                active = index;
                dragX = x;
                dragY = y;
                startProportion = proportionOf (b.param);
                host.beginEdit (b.param);
                break;

            case Kind::slider:
                if (b.param < 0) return;
                active = index;
                host.beginEdit (b.param);
                setFromPointer (index, x, y);
                break;

            case Kind::xy:
                active = index;
                if (b.param >= 0)  host.beginEdit (b.param);
                if (b.param2 >= 0) host.beginEdit (b.param2);
                setFromPointer (index, x, y);
                break;

            case Kind::toggle:
            {
                if (b.param < 0) return;
                float min = 0.0f, max = 1.0f, def = 0.0f, skew = 1.0f;
                host.getRange (b.param, min, max, def, skew);
                host.beginEdit (b.param);
                host.setValue (b.param, proportionOf (b.param) >= 0.5f ? min : max);
                host.endEdit (b.param);
                break;
            }

            case Kind::selector:
            {
                if (b.param < 0) return;
                float min = 0.0f, max = 1.0f, def = 0.0f, skew = 1.0f;
                host.getRange (b.param, min, max, def, skew);
                const auto count = std::max (1, w.count);
                const auto cell = std::min (count - 1, std::max (0, (x - w.x) * count / std::max (1, w.w)));
                host.beginEdit (b.param);
                host.setValue (b.param, min + (float) cell);
                host.endEdit (b.param);
                break;
            }

            case Kind::preset:
            {
                // The arrows step back and forth; anywhere else steps forward.
                if (numPresets <= 0)
                    return;

                const bool back = (x - w.x) * 100 < w.w * 22;
                applyPreset ((currentPreset + (back ? numPresets - 1 : 1)) % numPresets);
                break;
            }

            case Kind::meter: case Kind::lamp: case Kind::scope: case Kind::envelope: case Kind::filter:
                break;
        }
    }

    void Editor::applyPreset (int index)
    {
        if (index < 0 || index >= numPresets)
            return;

        currentPreset = index;
        const auto& preset = presets[index];

        for (int i = 0; i < preset.numValues; ++i)
        {
            if (preset.values[i].param == nullptr)
                continue;

            const auto param = host.findParam (preset.values[i].param);

            if (param >= 0)
            {
                host.beginEdit (param);
                host.setValue (param, preset.values[i].value);
                host.endEdit (param);
            }
        }
    }

    void Editor::mouseDrag (int x, int y, bool fine)
    {
        if (active < 0)
            return;

        const auto& w = widgets[active];
        const auto& b = bound[(size_t) active];

        if (w.kind == Kind::knob)
        {
            // Up or right turns it up; fine moves go slower.
            const auto travel = fine ? 900.0f : 200.0f;
            host.setValue (b.param, valueAt (b.param, startProportion + (float) ((x - dragX) - (y - dragY)) / travel));
        }
        else
        {
            setFromPointer (active, x, y);
        }
    }

    void Editor::mouseUp()
    {
        if (active < 0)
            return;

        const auto& b = bound[(size_t) active];

        if (b.param >= 0) host.endEdit (b.param);
        if (widgets[active].kind == Kind::xy && b.param2 >= 0) host.endEdit (b.param2);

        active = -1;
    }

    void Editor::mouseWheel (int x, int y, float notches)
    {
        const auto index = widgetAt (x, y);

        if (index < 0 || (widgets[index].kind != Kind::knob && widgets[index].kind != Kind::slider))
            return;

        const auto param = bound[(size_t) index].param;

        if (param < 0)
            return;

        host.beginEdit (param);
        host.setValue (param, valueAt (param, proportionOf (param) + notches * 0.04f));
        host.endEdit (param);
    }

    //==========================================================================
    void Editor::blitFrame (std::uint32_t* pixels, const Image& image, int frameHeight, int frame, int x, int y) const
    {
        if (image.pixels.empty() || frameHeight <= 0)
            return;

        for (int row = 0; row < frameHeight; ++row)
        {
            const auto ty = y + row;
            const auto sy = frame * frameHeight + row;

            if (ty < 0 || ty >= height || sy >= image.height)
                continue;

            for (int col = 0; col < image.width; ++col)
            {
                const auto tx = x + col;

                if (tx >= 0 && tx < width)
                    blend (pixels[(size_t) ty * (size_t) width + (size_t) tx], image.pixels[(size_t) sy * (size_t) image.width + (size_t) col]);
            }
        }
    }

    void Editor::drawMeter (std::uint32_t* pixels, const Widget& w, int index)
    {
        auto& shown = meterLevels[(size_t) index];
        const auto level = host.takeLevel (index, w.mode == 1);
        shown = w.isDisplay ? level : std::max (level, shown * 0.86f);

        const auto fraction = w.isDisplay ? std::min (1.0f, std::max (0.0f, shown))
                                          : std::min (1.0f, std::max (0.0f, (20.0f * std::log10 (std::max (shown, 1.0e-5f)) + 60.0f) / 66.0f));
        const auto t1 = w.isDisplay ? 0.75f : 48.0f / 66.0f, t2 = w.isDisplay ? 0.9f : 57.0f / 66.0f;

        auto colourAt = [&] (float t)
        {
            if (t < t1) return green;
            if (t < t2) return mix (green, amber, (t - t1) / (t2 - t1));
            return mix (amber, red, (t - t2) / (1.0f - t2));
        };

        Canvas canvas { pixels, width, height };
        const auto x0 = (float) w.x + 2.0f, y0 = (float) w.y + 2.0f, x1 = (float) (w.x + w.w) - 2.0f, y1 = (float) (w.y + w.h) - 2.0f;

        if (w.vertical)
        {
            const auto top = y1 - (y1 - y0) * fraction;

            for (int y = (int) std::floor (top); y < (int) std::ceil (y1); ++y)
                canvas.fillRect (x0, std::max ((float) y, top), x1, (float) y + 1.0f, colourAt ((y1 - (float) y) / (y1 - y0)), 1.0f);

            const auto zero = y1 - (y1 - y0) * (w.isDisplay ? 2.0f : 60.0f / 66.0f);
            canvas.fillRect (x0, zero, x1, zero + 1.0f, 0xffffffff, 0.35f);
        }
        else
        {
            const auto right = x0 + (x1 - x0) * fraction;

            for (int x = (int) std::floor (x0); x < (int) std::ceil (right); ++x)
                canvas.fillRect ((float) x, y0, std::min ((float) x + 1.0f, right), y1, colourAt (((float) x - x0) / (x1 - x0)), 1.0f);

            const auto zero = x0 + (x1 - x0) * (w.isDisplay ? 2.0f : 60.0f / 66.0f);
            canvas.fillRect (zero, y0, zero + 1.0f, y1, 0xffffffff, 0.35f);
        }
    }

    void Editor::drawScope (std::uint32_t* pixels, const Widget& w, int index)
    {
        host.readScope (index, scopeBuffer.data(), (int) scopeBuffer.size());

        constexpr int shown = 1024;
        int start = 0;

        for (int i = 1; i < (int) scopeBuffer.size() - shown; ++i)
        {
            if (scopeBuffer[(size_t) i - 1] < 0.0f && scopeBuffer[(size_t) i] >= 0.0f)
            {
                start = i;
                break;
            }
        }

        Canvas canvas { pixels, width, height };
        const auto x0 = (float) w.x + 3.0f, y0 = (float) w.y + 3.0f, x1 = (float) (w.x + w.w) - 3.0f, y1 = (float) (w.y + w.h) - 3.0f;
        const auto centre = (y0 + y1) * 0.5f, half = (y1 - y0) * 0.5f;

        float px = x0, py = centre - std::min (1.0f, std::max (-1.0f, scopeBuffer[(size_t) start])) * half;

        for (int i = 4; i < shown; i += 4)
        {
            const auto x = x0 + (x1 - x0) * (float) i / (float) (shown - 1);
            const auto y = centre - std::min (1.0f, std::max (-1.0f, scopeBuffer[(size_t) (start + i)])) * half;
            canvas.line (px, py, x, y, 1.5f, w.colour, 1.0f);
            px = x;
            py = y;
        }
    }

    void Editor::drawCurve (std::uint32_t* pixels, const Widget& w, const Bound& b)
    {
        Canvas canvas { pixels, width, height };
        const auto x0 = (float) w.x + 6.0f, y0 = (float) w.y + 6.0f, x1 = (float) (w.x + w.w) - 6.0f, y1 = (float) (w.y + w.h) - 6.0f;
        std::vector<float> xs, ys;

        if (w.kind == Kind::envelope)
        {
            // Each stage is as wide as its knob shows; the sustain plateau keeps a fixed width.
            const auto a = b.param >= 0 ? proportionOf (b.param) : 0.1f;
            const auto d = b.param2 >= 0 ? proportionOf (b.param2) : 0.3f;
            const auto s = b.param3 >= 0 ? proportionOf (b.param3) : 0.7f;
            const auto r = b.param4 >= 0 ? proportionOf (b.param4) : 0.4f;

            const float widths[] { 0.08f + 0.92f * a, 0.08f + 0.92f * d, 0.6f, 0.08f + 0.92f * r };
            const auto unit = (x1 - x0) / (widths[0] + widths[1] + widths[2] + widths[3]);
            const auto sustainY = y1 - (y1 - y0) * s;

            xs = { x0, x0 + widths[0] * unit, x0 + (widths[0] + widths[1]) * unit, x0 + (widths[0] + widths[1] + widths[2]) * unit, x1 };
            ys = { y1, y0, sustainY, sustainY, y1 };

            canvas.curve (xs, ys, y1, w.colour, 0.22f);

            for (int i = 1; i < 4; ++i)
                canvas.circle (xs[(size_t) i], ys[(size_t) i], 3.0f, w.colour, 1.0f);

            return;
        }

        // A filter's response over 20 Hz..20 kHz (log), -30..+24 dB.
        float cutoff = 1000.0f;

        if (b.param >= 0)
            cutoff = w.cutoffScale > 0.0f ? host.getValue (b.param) * w.cutoffScale : 20.0f * std::pow (1000.0f, proportionOf (b.param));

        cutoff = std::min (20000.0f, std::max (20.0f, cutoff));

        const auto resonance = b.param2 >= 0 ? proportionOf (b.param2) : 0.2f;
        const auto q = 0.707f + resonance * 9.0f;
        const auto steep = w.mode == 1 || w.mode == 3;
        const auto kind = w.mode == 2 || w.mode == 3 ? 1 : (w.mode == 4 ? 2 : 0);

        for (int i = 0; i <= 120; ++i)
        {
            const auto t = (float) i / 120.0f;
            const auto f = 20.0f * std::pow (1000.0f, t);
            const auto x = f / cutoff;
            const auto denominator = std::sqrt ((1.0f - x * x) * (1.0f - x * x) + (x / q) * (x / q));
            auto magnitude = kind == 1 ? (x * x) / denominator : (kind == 2 ? (x / q) / denominator : 1.0f / denominator);

            if (steep)
                magnitude *= kind == 2 ? 1.0f : magnitude / std::max (1.0f, q * 0.5f);

            const auto db = std::min (24.0f, std::max (-30.0f, 20.0f * std::log10 (std::max (magnitude, 1.0e-6f))));
            xs.push_back (x0 + (x1 - x0) * t);
            ys.push_back (y0 + (y1 - y0) * ((24.0f - db) / 54.0f));
        }

        canvas.curve (xs, ys, y1, w.colour, 0.22f);
    }

    void Editor::drawXy (std::uint32_t* pixels, const Widget& w, const Bound& b)
    {
        Canvas canvas { pixels, width, height };
        const auto x = b.param >= 0 ? proportionOf (b.param) : 0.5f;
        const auto y = b.param2 >= 0 ? proportionOf (b.param2) : 0.5f;
        const auto dx = (float) w.x + 2.0f + ((float) w.w - 4.0f) * x;
        const auto dy = (float) (w.y + w.h) - 2.0f - ((float) w.h - 4.0f) * y;

        canvas.fillRect (std::floor (dx), (float) w.y + 1.0f, std::floor (dx) + 1.0f, (float) (w.y + w.h) - 1.0f, w.colour, 0.4f);
        canvas.fillRect ((float) w.x + 1.0f, std::floor (dy), (float) (w.x + w.w) - 1.0f, std::floor (dy) + 1.0f, w.colour, 0.4f);
        canvas.circle (dx, dy, 11.0f, w.colour, 0.35f);
        canvas.circle (dx, dy, 5.5f, w.colour, 1.0f);
    }

    //==========================================================================
    void Editor::render (std::uint32_t* pixels)
    {
        if ((int) background.pixels.size() == width * height)
            std::memcpy (pixels, background.pixels.data(), background.pixels.size() * sizeof (std::uint32_t));
        else
            std::fill (pixels, pixels + (size_t) width * (size_t) height, 0xff202024u);

        for (int i = 0; i < numWidgets; ++i)
        {
            const auto& w = widgets[i];
            const auto& b = bound[(size_t) i];
            const auto* strip = w.strip >= 0 && w.strip < numStrips ? &strips[w.strip] : nullptr;
            const auto* image = strip != nullptr ? &frames[(size_t) w.strip] : nullptr;

            auto frameOf = [&] (float proportion)
            {
                return std::min (strip->frames - 1, std::max (0, (int) std::lround (proportion * (float) (strip->frames - 1))));
            };

            switch (w.kind)
            {
                case Kind::knob:
                case Kind::slider:
                    if (image != nullptr)
                        blitFrame (pixels, *image, strip->frameHeight, frameOf (proportionOf (b.param)), w.x, w.y);
                    break;

                case Kind::toggle:
                    if (image != nullptr)
                        blitFrame (pixels, *image, strip->frameHeight, proportionOf (b.param) >= 0.5f ? 1 : 0, w.x, w.y);
                    break;

                case Kind::selector:
                    if (image != nullptr && b.param >= 0)
                    {
                        float min = 0.0f, max = 1.0f, def = 0.0f, skew = 1.0f;
                        host.getRange (b.param, min, max, def, skew);
                        const auto chosen = (int) std::lround (host.getValue (b.param) - min);
                        blitFrame (pixels, *image, strip->frameHeight, std::min (strip->frames - 1, std::max (0, chosen)), w.x, w.y);
                    }
                    break;

                case Kind::lamp:
                    if (image != nullptr)
                        blitFrame (pixels, *image, strip->frameHeight, host.takeLevel (i, false) >= w.threshold ? 1 : 0, w.x, w.y);
                    break;

                case Kind::preset:
                    if (image != nullptr)
                        blitFrame (pixels, *image, strip->frameHeight, std::min (strip->frames - 1, std::max (0, currentPreset)), w.x, w.y);
                    break;

                case Kind::meter:     drawMeter (pixels, w, i); break;
                case Kind::scope:     drawScope (pixels, w, i); break;
                case Kind::envelope:
                case Kind::filter:    drawCurve (pixels, w, b); break;
                case Kind::xy:        drawXy (pixels, w, b); break;
            }
        }
    }
}
