// C:\workspace\Stella AI Studio\src\KnobFile.cpp
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    KnobFile.cpp
*/

#include "KnobFile.h"
#include "TextureLibrary.h"

#include <cmath>

#include <map>

namespace
{
    //==========================================================================
    /** One [Section] of the INI body, as a key -> value map. */
    struct Section
    {
        std::map<juce::String, juce::String> values;

        const juce::String* find (const juce::String& key) const
        {
            auto it = values.find (key);
            return it == values.end() ? nullptr : &it->second;
        }

        juce::String getString (const juce::String& key, const juce::String& fallback) const
        {
            auto* v = find (key);
            return v != nullptr ? *v : fallback;
        }

        int getInt (const juce::String& key, int fallback) const
        {
            auto* v = find (key);
            return v != nullptr ? v->getIntValue() : fallback;
        }

        float getFloat (const juce::String& key, float fallback) const
        {
            auto* v = find (key);

            if (v == nullptr)
                return fallback;

            // KnobMan writes floats with the host locale's separator.
            return v->replaceCharacter (',', '.').getFloatValue();
        }

        bool has (const juce::String& key) const { return find (key) != nullptr; }
    };

    //==========================================================================
    class IniBody
    {
    public:
        explicit IniBody (const juce::String& text)
        {
            juce::StringArray lines;
            lines.addLines (text);

            juce::String current = "";

            for (auto raw : lines)
            {
                auto line = raw.trimEnd();

                if (line.startsWith ("[") && line.contains ("]"))
                {
                    current = line.fromFirstOccurrenceOf ("[", false, false)
                                  .upToFirstOccurrenceOf ("]", false, false);
                    sections[current];
                    continue;
                }

                const int eq = line.indexOfChar ('=');

                if (eq <= 0 || current.isEmpty())
                    continue;

                sections[current].values[line.substring (0, eq)] = line.substring (eq + 1);
            }
        }

        bool hasSection (const juce::String& name) const
        {
            return sections.find (name) != sections.end();
        }

        const Section& getSection (const juce::String& name) { return sections[name]; }

    private:
        std::map<juce::String, Section> sections;
    };

    //==========================================================================
    juce::uint32 readBigEndianInt (const juce::uint8* p) noexcept
    {
        return ((juce::uint32) p[0] << 24) | ((juce::uint32) p[1] << 16)
             | ((juce::uint32) p[2] << 8)  |  (juce::uint32) p[3];
    }

    /** Byte offset at which the INI text starts, or -1 if unrecognised. */
    int findBodyOffset (const juce::uint8* data, size_t size)
    {
        if (size < 4)
            return -1;

        const juce::uint16 id = (juce::uint16) (((juce::uint16) data[0] << 8) | data[1]);

        // 0xFFFE — UTF-16 byte order mark, body follows immediately.
        if (id == 0xFFFE)
            return 2;

        // 0x8950 — PNG signature with its first bytes rewritten. Walk chunks to
        // the tEXt payload; the keyword plus its null terminator is 8 bytes.
        if (id == 0x8950)
        {
            size_t offset = 8;

            while (offset + 8 <= size)
            {
                const juce::uint32 chunkLen = readBigEndianInt (data + offset);
                const juce::uint32 chunkId  = readBigEndianInt (data + offset + 4);

                if (chunkId == 0x74455874)          // 'tEXt'
                    return (int) (offset + 16);

                const size_t stride = (size_t) chunkLen + 12;

                if (stride == 0 || offset + stride <= offset)
                    break;

                offset += stride;
            }

            return -1;
        }

        // 0x424D — "BM": the BMP header's little-endian size field is where the
        // appended text begins.
        if (id == 0x424D && size >= 6)
        {
            const juce::uint32 len = (juce::uint32) data[2]
                                   | ((juce::uint32) data[3] << 8)
                                   | ((juce::uint32) data[4] << 16)
                                   | ((juce::uint32) data[5] << 24);

            return (len > 0 && len < size) ? (int) len : -1;
        }

        // 0x4B4D — "KM": KnobMan's own header, length stored byte-swapped.
        if (id == 0x4B4D && size >= 6)
        {
            const juce::uint32 raw = readBigEndianInt (data + 2);
            const juce::uint32 len = ((raw << 24) & 0xFF000000u) | ((raw << 8) & 0x00FF0000u)
                                   | ((raw >> 8) & 0x0000FF00u) | ((raw >> 24) & 0x000000FFu);

            return (len > 0 && len < size) ? (int) len : 0;
        }

        // Plain text.
        return 0;
    }

    //==========================================================================
    PrimType primTypeFromName (const juce::String& name)
    {
        // Note the hyphens: the file spells these "H-Lines" and "V-Lines",
        // which do not match our enum names.
        static const std::pair<const char*, PrimType> table[]
        {
            { "None",        PrimType::None },
            { "Image",       PrimType::Image },
            { "Circle",      PrimType::Circle },
            { "CircleFill",  PrimType::CircleFill },
            { "MetalCircle", PrimType::MetalCircle },
            { "WaveCircle",  PrimType::WaveCircle },
            { "Sphere",      PrimType::Sphere },
            { "Rect",        PrimType::Rect },
            { "RectFill",    PrimType::RectFill },
            { "Triangle",    PrimType::Triangle },
            { "Line",        PrimType::Line },
            { "RadiateLine", PrimType::RadiateLine },
            { "H-Lines",     PrimType::HLines },
            { "V-Lines",     PrimType::VLines },
            { "Text",        PrimType::Text },
            { "Shape",       PrimType::Shape },
        };

        for (const auto& entry : table)
            if (name == entry.first)
                return entry.second;

        return PrimType::None;
    }

    /** KnobMan stores "animate on/off" plus a custom curve index. We have five
        fixed curves, so anything animated becomes Linear. */
    AnimMode readAnimMode (const Section& s, const juce::String& enableKey)
    {
        return s.getInt (enableKey, 0) == 0 ? AnimMode::Fixed : AnimMode::Linear;
    }

    AnimVal readAnim (const Section& s,
                      const juce::String& fromKey, const juce::String& toKey,
                      const juce::String& enableKey, float fallback)
    {
        AnimVal v;
        v.from = s.getFloat (fromKey, fallback);
        v.to   = s.getFloat (toKey, v.from);
        v.mode = readAnimMode (s, enableKey);
        return v;
    }
}

//==============================================================================
juce::String KnobFile::extractBody (const juce::MemoryBlock& raw)
{
    const auto* data = static_cast<const juce::uint8*> (raw.getData());
    const int offset = findBodyOffset (data, raw.getSize());

    if (offset < 0 || (size_t) offset >= raw.getSize())
        return {};

    return juce::String::fromUTF8 (reinterpret_cast<const char*> (data + offset),
                                   (int) (raw.getSize() - (size_t) offset));
}

//==============================================================================
juce::Image KnobFile::extractPreview (const juce::MemoryBlock& raw)
{
    if (raw.getSize() < 8)
        return {};

    const auto* data = static_cast<const juce::uint8*> (raw.getData());

    if (! (data[0] == 0x89 && data[1] == 0x50))
        return {};

    // Put the real PNG signature back and decode normally.
    juce::MemoryBlock png (raw);
    static const juce::uint8 signature[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    png.copyFrom (signature, 0, sizeof (signature));

    return juce::ImageFileFormat::loadFrom (png.getData(), png.getSize());
}

//==============================================================================
KnobFile::ImportResult KnobFile::parse (const juce::MemoryBlock& raw)
{
    ImportResult result;

    if (raw.getSize() < 16)
    {
        result.error = "File is too small to be a knob document.";
        return result;
    }

    auto body = extractBody (raw);

    if (body.isEmpty())
    {
        result.error = "Could not find the document text inside the file.";
        return result;
    }

    IniBody ini (body);

    if (! ini.hasSection ("Prefs"))
    {
        result.error = "No [Prefs] section — this does not look like a knob document.";
        return result;
    }

    const auto& prefs = ini.getSection ("Prefs");

    LayerDoc doc;
    doc.layers.clear();

    doc.canvasWidth  = juce::jlimit (8, 2048, prefs.getInt ("OutputSizeX", 32));
    doc.canvasHeight = juce::jlimit (8, 2048, prefs.getInt ("OutputSizeY", 32));

    int frames = prefs.getInt ("NumOfImage", 0);

    if (frames <= 0)
        frames = prefs.getInt ("RenderFrames", 31);

    doc.frames = juce::jlimit (1, 512, frames);

    if (prefs.getInt ("OverSampling", 0) != 0)
        result.warnings.add ("Oversampling is set in the source file; we always render 1:1.");

    if (prefs.getInt ("AlignHorizontal", 0) != 0)
        result.warnings.add ("Source strip is horizontal; export defaults to vertical.");

    const int layerCount = juce::jlimit (0, 256, prefs.getInt ("Layers", 1));

    bool sawTexture = false, sawShadow = false, sawCustomCurve = false;
    bool sawIntelliAlpha = false, sawFrameMask = false;

    for (int i = 0; i < layerCount; ++i)
    {
        const juce::String name = "Layer" + juce::String (i + 1);

        if (! ini.hasSection (name))
            continue;

        const auto& s = ini.getSection (name);

        DocLayer layer;
        layer.name    = s.getString ("Name", "Layer " + juce::String (i + 1));
        layer.visible = s.getInt ("Visible", 1) != 0;
        layer.solo    = s.getInt ("VisibleSolo", 0) != 0;

        // ---- primitive -----------------------------------------------------
        auto& p = layer.prim;

        p.type = primTypeFromName (s.getString ("Primitive", "None"));

        p.colour = juce::Colour ((juce::uint8) juce::jlimit (0, 255, s.getInt ("ColR", 255)),
                                 (juce::uint8) juce::jlimit (0, 255, s.getInt ("ColG", 0)),
                                 (juce::uint8) juce::jlimit (0, 255, s.getInt ("ColB", 0)));

        p.aspect        = s.getFloat ("PrimAspect", 0.0f);
        p.round         = s.getFloat ("PrimRound", 0.0f);
        p.width         = s.getFloat ("PrimWidth", 10.0f);
        p.length        = s.getFloat ("PrimLength", 50.0f);
        p.step          = juce::jlimit (1, 512, (int) s.getFloat ("PrimStep", 20.0f));
        p.angleStep     = s.getFloat ("PrimAngleStep", 45.0f);
        p.emboss        = s.getFloat ("PrimEmboss", 0.0f);
        p.embossDiffuse = s.getFloat ("PrimEmbossDiffuse", 0.0f);
        p.ambient       = s.getFloat ("PrimAmbient", 50.0f);
        p.specular      = s.getFloat ("PrimSpecular", 0.0f);
        p.specularWidth = s.getFloat ("PrimSpecWidth", 50.0f);
        p.diffuse       = s.getFloat ("PrimDiffuse", 0.0f);

        // KnobMan measures light anticlockwise from 3 o'clock and defaults to
        // -45; ours is clockwise from 12. Convert rather than copy.
        p.lightDir = 90.0f - s.getFloat ("PrimLightDir", -45.0f);

        p.text      = s.getString ("PrimText", "");
        p.fontSize  = s.getFloat ("PrimSize", 50.0f);
        p.bold      = s.getInt ("PrimBold", 0) != 0;
        p.italic    = s.getInt ("PrimItalic", 0) != 0;
        p.textAlign = s.getInt ("PrimTextAlign", 0);
        p.fill      = s.getInt ("PrimFill", 1) != 0;
        p.imageFile = s.getString ("PrimFile", "");

        // Shape points arrive as a flat list of numbers.
        {
            auto shapeText = s.getString ("PrimShape", "");

            if (shapeText.isNotEmpty())
            {
                juce::StringArray tokens;
                tokens.addTokens (shapeText.replaceCharacter (',', ' '), " ", {});
                tokens.removeEmptyStrings();

                for (int t = 0; t + 1 < tokens.size(); t += 2)
                    p.shapePoints.push_back ({ tokens[t].getFloatValue(),
                                               tokens[t + 1].getFloatValue() });
            }
        }

        // KnobMan's Emboss is a generic bevel for everything except the circle
        // family, which builds its rim analytically. Map it onto BevelSpec for
        // the former and leave the rim fields alone for the latter.
        {
            const bool circleFamily = p.type == PrimType::Circle
                                   || p.type == PrimType::CircleFill
                                   || p.type == PrimType::MetalCircle
                                   || p.type == PrimType::Sphere;

            if (! circleFamily && std::abs (p.emboss) > 0.5f)
            {
                auto& b = p.bevel;
                b.enabled   = true;
                b.style     = BevelStyle::InnerBevel;
                b.technique = BevelTechnique::Smooth;
                b.up        = p.emboss >= 0.0f;
                b.size      = juce::jlimit (0.5f, 100.0f, std::abs (p.emboss) * 0.12f);
                b.depth     = juce::jlimit (1.0f, 1000.0f, std::abs (p.emboss) * 4.0f);
                b.useGlobalLight = false;
                b.angle     = p.lightDir;
                b.altitude  = 30.0f;
                b.highlightOpacity = juce::jlimit (0.0f, 100.0f, p.embossDiffuse);
                b.shadowOpacity    = juce::jlimit (0.0f, 100.0f, p.embossDiffuse);
            }
        }

        // KnobMan's texture is a bump map named by file. Map the four stock
        // names onto our generated equivalents; anything else keeps its name
        // and is looked for in the user's texture folder at render time.
        {
            const auto textureFile = s.getString ("PrimTextureFile", "");
            const float depth = s.getFloat ("PrimTexture", 0.0f);

            if (textureFile.isNotEmpty() && std::abs (depth) > 0.01f)
            {
                // Split the path FIRST. createLegalFileName strips separators,
                // so legalising before splitting would fuse the whole path into
                // one name and the lookup would never match.
                auto name = textureFile.replaceCharacter ('\\', '/')
                                       .fromLastOccurrenceOf ("/", false, false)
                                       .trim();

                auto bare = name.contains (".") ? name.upToLastOccurrenceOf (".", false, false)
                                                : name;

                for (const auto& known : TextureLibrary::proceduralNames())
                    if (bare.equalsIgnoreCase (known))
                        bare = known;

                p.textureName  = bare.isNotEmpty() ? bare : name;
                p.textureDepth = juce::jlimit (0.0f, 100.0f, std::abs (depth));
                p.textureMode  = TextureMode::Bump;

                if (! TextureLibrary::isProcedural (p.textureName))
                    sawTexture = true;
            }
        }

        if (s.getInt ("IntelliAlpha", 0) != 0)
            sawIntelliAlpha = true;

        // ---- effect --------------------------------------------------------
        auto& e = layer.eff;

        e.antialias    = s.getInt ("Antialias", 1) != 0;
        e.zoomSeparate = s.getInt ("ZoomXYSepa", 0) != 0;
        e.keepDir      = s.getInt ("KeepDir", 0) != 0;

        e.zoomX   = readAnim (s, "Zoom1", "Zoom2", "AnimateZoom", 100.0f);
        e.zoomY   = readAnim (s, "ZoomY1", "ZoomY2", "AnimateZoomY", 100.0f);
        e.offsetX = readAnim (s, "LayerOffsetX1", "LayerOffsetX2", "AnimateLayerOffsetX", 0.0f);
        e.offsetY = readAnim (s, "LayerOffsetY1", "LayerOffsetY2", "AnimateLayerOffsetY", 0.0f);
        e.angle   = readAnim (s, "Angle1", "Angle2", "AnimateAngle", 0.0f);
        e.alpha   = readAnim (s, "Alpha1", "Alpha2", "AnimateAlpha", 100.0f);

        e.brightness = readAnim (s, "Brightness1", "Brightness2", "AnimateBrightness", 0.0f);
        e.contrast   = readAnim (s, "Contrast1", "Contrast2", "AnimateContrast", 0.0f);
        e.saturation = readAnim (s, "Saturation1", "Saturation2", "AnimateSaturation", 0.0f);
        e.hue        = readAnim (s, "Hue1", "Hue2", "AnimateHue", 0.0f);

        // KnobMan's rotation centre is an offset from the middle of the canvas;
        // ours is a percentage across it.
        e.centreX = juce::jlimit (0.0f, 100.0f, 50.0f + s.getFloat ("RotCenterX1", 0.0f) * 0.5f);
        e.centreY = juce::jlimit (0.0f, 100.0f, 50.0f + s.getFloat ("RotCenterY1", 0.0f) * 0.5f);

        auto readMask = [&s] (MaskSpec& m,
                              const juce::String& enable, const juce::String& type,
                              const juce::String& start1, const juce::String& start2,
                              const juce::String& startAnim,
                              const juce::String& stop1, const juce::String& stop2,
                              const juce::String& stopAnim)
        {
            m.enabled = s.getInt (enable, 0) != 0;
            m.type    = (MaskType) juce::jlimit (0, (int) MaskType::numTypes - 1,
                                                 s.getInt (type, 0));
            m.start   = readAnim (s, start1, start2, startAnim, -140.0f);
            m.stop    = readAnim (s, stop1,  stop2,  stopAnim,   140.0f);
        };

        readMask (e.mask1, "UseMask", "MaskType",
                  "MaskStart1", "MaskStart2", "AnimateMaskStart",
                  "MaskStop1",  "MaskStop2",  "AnimateMaskStop");

        readMask (e.mask2, "UseMask2", "Mask2Type",
                  "Mask2Start1", "Mask2Start2", "AnimateMask2Start",
                  "Mask2Stop1",  "Mask2Stop2",  "AnimateMask2Stop");

        e.mask2.orOp  = s.getInt ("Mask2Operation", 0) != 0;
        e.mask1.biDir = s.getInt ("MaskGradDir", 0) != 0;
        e.mask2.biDir = s.getInt ("Mask2GradDir", 0) != 0;

        if (s.getFloat ("ShadowDensity1", 0.0f) != 0.0f
            || s.getFloat ("IShadowDensity1", 0.0f) != 0.0f
            || s.getFloat ("HilightDensity1", 0.0f) != 0.0f)
            sawShadow = true;

        if (s.getInt ("UseFMask", 0) != 0)
            sawFrameMask = true;

        if (s.getInt ("AnimateZoom", 0) != 0 && s.has ("ZoomCurve"))
            sawCustomCurve = true;

        doc.layers.push_back (layer);
    }

    if (doc.layers.empty())
    {
        result.error = "The document contains no layers.";
        return result;
    }

    if (sawTexture)
        result.warnings.add ("Uses a texture image we do not ship. Drop a matching file into "
                             "the texture folder, or pick one of the built-in patterns.");

    if (sawShadow)
        result.warnings.add ("Drop, inner and highlight shadows are not implemented; those layers lose depth.");

    if (sawCustomCurve)
        result.warnings.add ("Custom animation curves became Linear.");

    if (sawIntelliAlpha)
        result.warnings.add ("IntelliAlpha edge handling is not implemented.");

    if (sawFrameMask)
        result.warnings.add ("Per-frame masks (FMask) are not implemented.");

    result.document = doc;
    result.preview  = extractPreview (raw);
    result.ok       = true;
    return result;
}

//==============================================================================
KnobFile::ImportResult KnobFile::parseFile (const juce::File& file)
{
    ImportResult result;

    if (! file.existsAsFile())
    {
        result.error = "File not found.";
        return result;
    }

    juce::MemoryBlock raw;

    if (! file.loadFileAsData (raw))
    {
        result.error = "Could not read the file.";
        return result;
    }

    return parse (raw);
}
