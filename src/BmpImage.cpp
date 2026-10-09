// C:\workspace\Stella AI Studio\src\BmpImage.cpp
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    BmpImage.cpp
*/

#include "BmpImage.h"

#include <cmath>

namespace
{
    /** One colour channel described by its bit mask, as BI_BITFIELDS gives it.
        The shift and scale are worked out once so the per-pixel path is a
        mask, a shift and a multiply. */
    struct Channel
    {
        juce::uint32 mask  = 0;
        int          shift = 0;
        float        scale = 0.0f;

        juce::uint8 extract (juce::uint32 value) const noexcept
        {
            if (mask == 0)
                return 255;

            const auto raw = (value & mask) >> shift;

            return (juce::uint8) juce::jlimit (0, 255, (int) std::lround ((float) raw * scale));
        }
    };

    Channel makeChannel (juce::uint32 mask) noexcept
    {
        Channel c;
        c.mask = mask;

        if (mask == 0)
            return c;

        while (((mask >> c.shift) & 1u) == 0u && c.shift < 31)
            ++c.shift;

        const auto range = mask >> c.shift;
        c.scale = range > 0 ? 255.0f / (float) range : 0.0f;

        return c;
    }
}

//==============================================================================
bool BmpImage::looksLikeBmp (const juce::File& file)
{
    juce::FileInputStream in (file);

    if (! in.openedOk())
        return false;

    char magic[2] = {};

    return in.read (magic, 2) == 2 && magic[0] == 'B' && magic[1] == 'M';
}

//==============================================================================
juce::Image BmpImage::load (const void* rawData, size_t numBytes)
{
    const auto* data = static_cast<const juce::uint8*> (rawData);

    // 14-byte file header plus the smallest DIB header there is.
    if (data == nullptr || numBytes < 26)
        return {};

    if (data[0] != 'B' || data[1] != 'M')
        return {};

    auto u16 = [data] (size_t o) noexcept -> juce::uint32
    {
        return (juce::uint32) data[o] | ((juce::uint32) data[o + 1] << 8);
    };

    auto u32 = [data] (size_t o) noexcept -> juce::uint32
    {
        return  (juce::uint32) data[o]
             | ((juce::uint32) data[o + 1] << 8)
             | ((juce::uint32) data[o + 2] << 16)
             | ((juce::uint32) data[o + 3] << 24);
    };

    const size_t       pixelOffset = (size_t) u32 (10);
    const juce::uint32 dibSize     = u32 (14);

    if (dibSize < 12 || (size_t) 14 + dibSize > numBytes)
        return {};

    int          width = 0, height = 0, bitCount = 0;
    juce::uint32 compression = 0, paletteCount = 0;
    size_t       paletteEntrySize = 4;

    if (dibSize == 12)
    {
        // BITMAPCOREHEADER: 16-bit dimensions, 3-byte palette entries.
        width    = (int) (juce::int16) (juce::uint16) u16 (18);
        height   = (int) (juce::int16) (juce::uint16) u16 (20);
        bitCount = (int) u16 (24);
        paletteEntrySize = 3;
    }
    else
    {
        width        = (int) (juce::int32) u32 (18);
        height       = (int) (juce::int32) u32 (22);
        bitCount     = (int) u16 (28);
        compression  = u32 (30);
        paletteCount = u32 (46);
    }

    // A negative height means the rows are stored top-down instead of the usual
    // bottom-up. Getting this wrong flips the texture, which on a symmetrical
    // pattern is invisible and on anything else is obvious.
    const bool topDown = height < 0;
    height = std::abs (height);

    if (width <= 0 || height <= 0 || width > 20000 || height > 20000)
        return {};

    // BI_RGB and BI_BITFIELDS only. An RLE bitmap decoded as if it were raw
    // would produce confident garbage, which is worse than refusing it.
    if (compression != 0 && compression != 3)
        return {};

    //--------------------------------------------------------------------------
    Channel cr, cg, cb, ca;

    if (compression == 3)
    {
        // The masks sit straight after a 40-byte header, and at the same file
        // offsets inside a V4 or V5 header.
        // Three masks live at 54, 58 and 62, so 66 bytes have to be there.
        if (numBytes < 66)
            return {};

        cr = makeChannel (u32 (54));
        cg = makeChannel (u32 (58));
        cb = makeChannel (u32 (62));

        // The alpha mask arrives with BITMAPV3INFOHEADER at 56 bytes, not with
        // V4 at 108 — requiring 108 would drop the alpha on every V3 file.
        if (dibSize >= 56 && numBytes >= 70)
            ca = makeChannel (u32 (66));
    }
    else if (bitCount == 32)
    {
        cb = makeChannel (0x000000ffu);
        cg = makeChannel (0x0000ff00u);
        cr = makeChannel (0x00ff0000u);
        ca = makeChannel (0xff000000u);
    }
    else if (bitCount == 16)
    {
        // BI_RGB at 16bpp is X1R5G5B5, not R5G6B5.
        cb = makeChannel (0x001fu);
        cg = makeChannel (0x03e0u);
        cr = makeChannel (0x7c00u);
    }

    //--------------------------------------------------------------------------
    const juce::uint8* palette = nullptr;

    if (bitCount <= 8)
    {
        if (paletteCount == 0)
            paletteCount = 1u << bitCount;

        const size_t paletteOffset = (size_t) 14 + dibSize;

        if (paletteOffset + paletteCount * paletteEntrySize > numBytes)
            return {};

        palette = data + paletteOffset;
    }

    const size_t stride = (size_t) (((width * bitCount + 31) / 32) * 4);

    if (pixelOffset + stride * (size_t) height > numBytes)
        return {};

    //--------------------------------------------------------------------------
    juce::Image image (juce::Image::ARGB, width, height, false);
    juce::Image::BitmapData dest (image, juce::Image::BitmapData::readWrite);

    auto fromPalette = [palette, paletteCount, paletteEntrySize]
                       (juce::uint32 index, juce::uint8& r, juce::uint8& g, juce::uint8& b) noexcept
    {
        if (palette == nullptr || index >= paletteCount)
        {
            r = g = b = 0;
            return;
        }

        const auto* entry = palette + (size_t) index * paletteEntrySize;
        b = entry[0];
        g = entry[1];
        r = entry[2];
    };

    bool sawAlpha = false;

    for (int y = 0; y < height; ++y)
    {
        const int sourceRow = topDown ? y : (height - 1 - y);
        const juce::uint8* row = data + pixelOffset + (size_t) sourceRow * stride;

        for (int x = 0; x < width; ++x)
        {
            juce::uint8 r = 0, g = 0, b = 0, a = 255;

            switch (bitCount)
            {
                case 32:
                {
                    const juce::uint32 v =  (juce::uint32) row[x * 4]
                                         | ((juce::uint32) row[x * 4 + 1] << 8)
                                         | ((juce::uint32) row[x * 4 + 2] << 16)
                                         | ((juce::uint32) row[x * 4 + 3] << 24);

                    r = cr.extract (v);
                    g = cg.extract (v);
                    b = cb.extract (v);

                    if (ca.mask != 0)
                    {
                        a = ca.extract (v);

                        if (a != 0)
                            sawAlpha = true;
                    }

                    break;
                }

                case 24:
                    b = row[x * 3];
                    g = row[x * 3 + 1];
                    r = row[x * 3 + 2];
                    break;

                case 16:
                {
                    const juce::uint32 v = (juce::uint32) row[x * 2]
                                         | ((juce::uint32) row[x * 2 + 1] << 8);

                    r = cr.extract (v);
                    g = cg.extract (v);
                    b = cb.extract (v);
                    break;
                }

                case 8:
                    fromPalette (row[x], r, g, b);
                    break;

                case 4:
                {
                    const auto byte = row[x >> 1];
                    fromPalette ((x & 1) ? (juce::uint32) (byte & 0x0f)
                                         : (juce::uint32) (byte >> 4), r, g, b);
                    break;
                }

                case 1:
                {
                    const auto byte = row[x >> 3];
                    fromPalette ((juce::uint32) ((byte >> (7 - (x & 7))) & 1), r, g, b);
                    break;
                }

                default:
                    return {};
            }

            dest.setPixelColour (x, y, juce::Colour (r, g, b, a));
        }
    }

    // A 32-bit BMP is very often written with its alpha channel left at zero.
    // Taken at face value that is a fully invisible image, so read an all-zero
    // alpha channel as "this file has no alpha" rather than "erase everything".
    if (bitCount == 32 && ca.mask != 0 && ! sawAlpha)
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                dest.setPixelColour (x, y, dest.getPixelColour (x, y).withAlpha (1.0f));

    return image;
}

//==============================================================================
juce::Image BmpImage::loadFrom (const juce::File& file)
{
    juce::MemoryBlock block;

    if (! file.loadFileAsData (block) || block.getSize() == 0)
        return {};

    return load (block.getData(), block.getSize());
}

//==============================================================================
juce::Image loadImageFile (const juce::File& file)
{
    // JUCE sniffs the content rather than trusting the extension, so this
    // handles a PNG named .bmp correctly and falls through cleanly on a real
    // BMP, which it has no decoder for.
    auto image = juce::ImageFileFormat::loadFrom (file);

    if (image.isValid())
        return image;

    return BmpImage::loadFrom (file);
}
