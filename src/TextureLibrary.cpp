// C:\workspace\Stella AI Studio\src\TextureLibrary.cpp
// From KnobMaker (C:\workspace\knobmaker); in Stella the textures folder is Documents\Stella AI
// Studio\Textures and the folder choice is kept in the studio's own settings.

/*
    TextureLibrary.cpp
*/

#include "TextureLibrary.h"
#include "BmpImage.h"

#include <cmath>

namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi;

    //==========================================================================
    /** Deterministic per-lattice-point value. Not juce::Random: the same cell
        has to give the same number every time or the pattern would crawl as the
        preview re-renders at a different scale. */
    inline float hashCell (int x, int y) noexcept
    {
        auto h = (juce::uint32) (x * 374761393) + (juce::uint32) (y * 668265263);
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= (h >> 16);

        return (float) (h & 0xffffffu) / (float) 0xffffffu;
    }

    inline float smoothStep (float t) noexcept { return t * t * (3.0f - 2.0f * t); }

    /** Value noise that wraps at `period` cells, so the pattern tiles cleanly
        no matter where the rotation lands it. */
    float valueNoise (float u, float v, int period) noexcept
    {
        period = juce::jmax (1, period);

        const float x = u * (float) period;
        const float y = v * (float) period;

        const int x0 = (int) std::floor (x);
        const int y0 = (int) std::floor (y);

        const float fx = smoothStep (x - (float) x0);
        const float fy = smoothStep (y - (float) y0);

        auto wrap = [period] (int i) noexcept { return ((i % period) + period) % period; };

        const float a = hashCell (wrap (x0),     wrap (y0));
        const float b = hashCell (wrap (x0 + 1), wrap (y0));
        const float c = hashCell (wrap (x0),     wrap (y0 + 1));
        const float d = hashCell (wrap (x0 + 1), wrap (y0 + 1));

        return juce::jmap (fy, juce::jmap (fx, a, b), juce::jmap (fx, c, d));
    }

    //==========================================================================
    float patternCheckers (float u, float v, int n) noexcept
    {
        const int cx = (int) std::floor (u * (float) n);
        const int cy = (int) std::floor (v * (float) n);

        return ((cx + cy) & 1) ? 0.18f : 0.82f;
    }

    /** Streaks that run along one axis and vary across it — brushed metal.
        The x lattice coordinate is pinned so every row of the streak is
        identical; only the offsets change between octaves. */
    float patternHairline (float u, float v, int n) noexcept
    {
        juce::ignoreUnused (u);

        float value = 0.58f * valueNoise (0.00f, v, n * 8)
                    + 0.30f * valueNoise (0.37f, v, n * 23)
                    + 0.12f * valueNoise (0.71f, v, n * 61);

        return juce::jlimit (0.0f, 1.0f, value);
    }

    float patternSand (float u, float v, int n) noexcept
    {
        float value = 0.55f * valueNoise (u, v, n * 6)
                    + 0.30f * valueNoise (u, v, n * 13)
                    + 0.15f * valueNoise (u, v, n * 29);

        return juce::jlimit (0.0f, 1.0f, value);
    }

    /** A weave: alternate cells show the warp thread, the rest the weft, each
        shaded across its own width so the crossing reads as over-and-under. */
    float patternFabric (float u, float v, int n) noexcept
    {
        const float x = u * (float) n;
        const float y = v * (float) n;

        const int cx = (int) std::floor (x);
        const int cy = (int) std::floor (y);

        const float across = ((cx + cy) & 1) ? (y - (float) cy) : (x - (float) cx);

        return juce::jlimit (0.0f, 1.0f, 0.22f + 0.68f * std::sin (across * kPi));
    }

    //==========================================================================
    /** Scale is "percent of the canvas one repeat covers", so a smaller number
        means a finer pattern. Turned into a whole repeat count because the
        tileable noise needs an integer period. */
    int repeatsFor (float scalePercent) noexcept
    {
        const float s = juce::jlimit (2.0f, 200.0f, scalePercent);

        return juce::jlimit (1, 64, (int) std::lround (100.0f / s));
    }

    juce::Image generatePattern (const juce::String& name, int w, int h,
                                 float scalePercent, float angleDegrees)
    {
        juce::Image out (juce::Image::ARGB, w, h, false);
        juce::Image::BitmapData data (out, juce::Image::BitmapData::writeOnly);

        const int   n  = repeatsFor (scalePercent);
        const float a  = angleDegrees * kPi / 180.0f;
        const float ca = std::cos (a);
        const float sa = std::sin (a);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                // Rotate about the middle, then let the pattern wrap.
                const float u = (float) x / (float) w - 0.5f;
                const float v = (float) y / (float) h - 0.5f;

                const float ru = u * ca - v * sa + 0.5f;
                const float rv = u * sa + v * ca + 0.5f;

                float value = 0.5f;

                if      (name == "Checkers") value = patternCheckers (ru, rv, n);
                else if (name == "Fabric")   value = patternFabric   (ru, rv, n);
                else if (name == "Hairline") value = patternHairline (ru, rv, n);
                else if (name == "Sand")     value = patternSand     (ru, rv, n);

                const auto g = (juce::uint8) juce::jlimit (0, 255,
                                                           (int) std::lround (value * 255.0f));

                auto* px = reinterpret_cast<juce::PixelARGB*> (data.getLinePointer (y)
                                                                 + x * data.pixelStride);
                px->setARGB (255, g, g, g);
            }
        }

        return out;
    }

    //==========================================================================
    juce::File findUserFile (const juce::String& name)
    {
        auto folder = TextureLibrary::userFolder();

        if (! folder.isDirectory())
            return {};

        auto direct = folder.getChildFile (name);

        if (direct.existsAsFile())
            return direct;

        // A document may have stored the name without its extension — which is
        // exactly what an imported .knob does.
        juce::Array<juce::File> files;
        folder.findChildFiles (files, juce::File::findFiles, false,
                               "*.png;*.jpg;*.jpeg;*.bmp;*.gif");

        for (const auto& f : files)
            if (f.getFileNameWithoutExtension().equalsIgnoreCase (name))
                return f;

        return {};
    }

    juce::Image tileUserImage (const juce::String& name, int w, int h,
                               float scalePercent, float angleDegrees)
    {
        auto file = findUserFile (name);

        if (file == juce::File())
            return {};

        // Not ImageFileFormat::loadFrom: JUCE has no BMP decoder, and KnobMan's
        // own stock textures are bitmaps.
        auto source = loadImageFile (file);

        if (! source.isValid() || source.getWidth() < 1 || source.getHeight() < 1)
            return {};

        juce::Image out (juce::Image::ARGB, w, h, true);
        juce::Graphics g (out);

        // Neutral grey wherever the tile is transparent: no bump, no tint.
        g.fillAll (juce::Colour (0xff808080));

        const float tile = juce::jmax (4.0f, (float) juce::jmin (w, h)
                                               * juce::jlimit (2.0f, 200.0f, scalePercent) * 0.01f);

        auto transform = juce::AffineTransform::scale (tile / (float) source.getWidth(),
                                                       tile / (float) source.getHeight())
                            .rotated (angleDegrees * kPi / 180.0f,
                                      (float) w * 0.5f, (float) h * 0.5f);

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setFillType (juce::FillType (source, transform));
        g.fillAll();

        return out;
    }

    //==========================================================================
    juce::CriticalSection cacheLock;
    juce::HashMap<juce::String, juce::Image> cache;

    //==========================================================================
    juce::CriticalSection folderLock;
    juce::String chosenFolder;        // empty means "use the default"
    bool         folderLoaded = false;

    // Stella: the studio's own places, beside its primitives and looks.
    juce::File appDataRoot()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Fanan")
                   .getChildFile ("Stella AI Studio");
    }
}

//==============================================================================
juce::File TextureLibrary::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Stella AI Studio")
               .getChildFile ("Textures");
}

juce::File TextureLibrary::settingsFile()
{
    return appDataRoot().getChildFile ("textures.xml");
}

juce::File TextureLibrary::userFolder()
{
    const juce::ScopedLock sl (folderLock);

    if (! folderLoaded)
    {
        folderLoaded = true;

        if (auto xml = juce::XmlDocument::parse (settingsFile()))
            chosenFolder = xml->getStringAttribute ("textureFolder");
    }

    if (chosenFolder.isNotEmpty())
    {
        const juce::File chosen (chosenFolder);

        // If the folder has gone — renamed, or on a drive that is not plugged
        // in — fall back rather than showing an empty list with no explanation.
        if (chosen.isDirectory())
            return chosen;
    }

    return defaultFolder();
}

bool TextureLibrary::isUsingCustomFolder()
{
    return userFolder() != defaultFolder();
}

void TextureLibrary::setUserFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (folderLock);

        chosenFolder = folder.isDirectory() ? folder.getFullPathName() : juce::String();
        folderLoaded = true;

        auto file = settingsFile();
        file.getParentDirectory().createDirectory();

        // Merge into whatever is already in there — this file is shared with
        // anything else that wants to remember something later.
        auto xml = juce::XmlDocument::parse (file);

        if (xml == nullptr)
            xml = std::make_unique<juce::XmlElement> ("KnobMakerSettings");

        if (chosenFolder.isEmpty())
            xml->removeAttribute ("textureFolder");
        else
            xml->setAttribute ("textureFolder", chosenFolder);

        xml->writeTo (file);
    }

    rescan();
}

juce::StringArray TextureLibrary::proceduralNames()
{
    return { "Checkers", "Fabric", "Hairline", "Sand" };
}

bool TextureLibrary::isProcedural (const juce::String& name)
{
    return proceduralNames().contains (name);
}

juce::StringArray TextureLibrary::availableNames()
{
    auto names = proceduralNames();

    auto folder = userFolder();

    if (folder.isDirectory())
    {
        juce::Array<juce::File> files;
        folder.findChildFiles (files, juce::File::findFiles, false,
                               "*.png;*.jpg;*.jpeg;*.bmp;*.gif");

        juce::StringArray fromDisk;

        for (const auto& f : files)
            fromDisk.add (f.getFileName());

        fromDisk.sort (true);
        names.addArray (fromDisk);
    }

    return names;
}

void TextureLibrary::rescan()
{
    const juce::ScopedLock sl (cacheLock);
    cache.clear();
}

juce::Image TextureLibrary::image (const juce::String& name, int w, int h,
                                   float scalePercent, float angleDegrees)
{
    if (name.isEmpty() || w < 1 || h < 1)
        return {};

    const auto key = name + "|" + juce::String (w) + "x" + juce::String (h)
                       + "|" + juce::String (juce::roundToInt (scalePercent))
                       + "|" + juce::String (juce::roundToInt (angleDegrees));

    {
        const juce::ScopedLock sl (cacheLock);

        if (cache.contains (key))
            return cache[key];
    }

    auto result = isProcedural (name)
                    ? generatePattern (name, w, h, scalePercent, angleDegrees)
                    : tileUserImage   (name, w, h, scalePercent, angleDegrees);

    if (result.isValid())
    {
        const juce::ScopedLock sl (cacheLock);

        // The key carries the render size, and that changes every time the
        // preview is resized. Left alone this would grow without bound.
        if (cache.size() > 24)
            cache.clear();

        cache.set (key, result);
    }

    return result;
}
