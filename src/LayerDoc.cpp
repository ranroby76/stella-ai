// C:\workspace\Stella AI Studio\src\LayerDoc.cpp
// From KnobMaker (C:\workspace\knobmaker); in Stella LayerImages is added, at the end.

/*
    LayerDoc.cpp
*/

#include "LayerDoc.h"

#include <cmath>

//==============================================================================
juce::StringArray animModeNames()
{
    return { "Fixed", "Linear", "Sine", "Reverse", "Triangle" };
}

juce::StringArray primTypeNames()
{
    return { "None", "Image", "Circle", "CircleFill", "MetalCircle", "WaveCircle",
             "Sphere", "Rect", "RectFill", "Triangle", "Line", "RadiateLine",
             "HLines", "VLines", "Text", "Shape",
             "MeterCircle", "MeterLines", "LED" };
}

juce::StringArray ledShapeNames()
{
    return { "Circle", "Ellipse", "Rect", "Triangle" };
}

juce::StringArray bevelStyleNames()
{
    return { "Outer Bevel", "Inner Bevel", "Emboss", "Pillow Emboss", "Stroke Emboss" };
}

juce::StringArray bevelTechniqueNames()
{
    return { "Smooth", "Chisel Hard", "Chisel Soft" };
}

juce::StringArray blendModeNames()
{
    return { "Normal", "Multiply", "Screen", "Overlay", "Linear Dodge (Add)", "Linear Burn" };
}

//==============================================================================
void Contour::sortPoints()
{
    std::sort (points.begin(), points.end(),
               [] (const Point& a, const Point& b) { return a.x < b.x; });
}

void Contour::addPoint (float x, float y)
{
    points.push_back ({ juce::jlimit (0.0f, 1.0f, x), juce::jlimit (0.0f, 1.0f, y), false });
    sortPoints();
}

void Contour::removePoint (int index)
{
    // The two ends anchor the curve; without them there is nothing to evaluate.
    if (points.size() <= 2 || index <= 0 || index >= (int) points.size() - 1)
        return;

    points.erase (points.begin() + index);
}

float Contour::at (float x) const noexcept
{
    if (points.empty())
        return x;

    x = juce::jlimit (0.0f, 1.0f, x);

    if (x <= points.front().x) return points.front().y;
    if (x >= points.back().x)  return points.back().y;

    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto& b = points[i];

        if (x > b.x)
            continue;

        const auto& a = points[i - 1];
        const float span = b.x - a.x;

        if (span <= 1.0e-6f)
            return b.y;

        float t = (x - a.x) / span;

        // A corner at either end keeps the segment straight; two smooth points
        // ease into each other. That is what the Corner toggle actually does.
        if (! a.corner && ! b.corner)
            t = t * t * (3.0f - 2.0f * t);

        return a.y + (b.y - a.y) * t;
    }

    return points.back().y;
}

juce::String Contour::toString() const
{
    juce::StringArray parts;

    for (const auto& p : points)
        parts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4)
                     + "," + (p.corner ? "1" : "0"));

    return parts.joinIntoString (";");
}

Contour Contour::fromString (const juce::String& text)
{
    Contour c;

    juce::StringArray parts;
    parts.addTokens (text, ";", {});
    parts.removeEmptyStrings();

    std::vector<Point> loaded;

    for (const auto& part : parts)
    {
        juce::StringArray f;
        f.addTokens (part, ",", {});

        if (f.size() >= 2)
            loaded.push_back ({ f[0].getFloatValue(), f[1].getFloatValue(),
                                f.size() > 2 && f[2].getIntValue() != 0 });
    }

    if (loaded.size() >= 2)
    {
        c.points = loaded;
        c.sortPoints();
    }

    return c;
}

juce::StringArray Contour::presetNames()
{
    // Twelve, so the picker fills a tidy 4 x 3 grid.
    return { "Linear", "Cone", "Cone - Inverted", "Half Round",
             "Rolling Slope", "Ring", "Gaussian",
             "Ring - Double", "Cove - Deep", "Sawtooth", "Steps", "Shallow Slope" };
}

Contour Contour::preset (int index)
{
    Contour c;

    switch (index)
    {
        case 1:  c.points = { {0.0f,0.0f,true}, {0.5f,1.0f,true}, {1.0f,1.0f,true} };
                 break;                                                    // Cone

        case 2:  c.points = { {0.0f,0.0f,true}, {0.5f,0.0f,true}, {1.0f,1.0f,true} };
                 break;                                                    // Cone inverted

        case 3:  c.points = { {0.0f,0.0f,false}, {0.5f,0.92f,false}, {1.0f,1.0f,false} };
                 break;                                                    // Half round

        case 4:  c.points = { {0.0f,0.0f,false}, {0.35f,0.62f,false},
                              {0.70f,0.42f,false}, {1.0f,1.0f,false} };
                 break;                                                    // Rolling slope

        case 5:  c.points = { {0.0f,0.0f,true}, {0.35f,1.0f,false},
                              {0.65f,0.15f,false}, {1.0f,1.0f,true} };
                 break;                                                    // Ring

        case 6:  c.points = { {0.0f,0.0f,false}, {0.25f,0.10f,false},
                              {0.50f,0.50f,false}, {0.75f,0.90f,false}, {1.0f,1.0f,false} };
                 break;                                                    // Gaussian

        case 7:  c.points = { {0.0f,0.0f,true},  {0.22f,1.0f,false},
                              {0.42f,0.12f,false}, {0.62f,1.0f,false},
                              {0.82f,0.20f,false}, {1.0f,1.0f,true} };
                 break;                                                    // Ring - Double

        case 8:  c.points = { {0.0f,0.0f,true},  {0.30f,0.06f,false},
                              {0.72f,0.14f,false}, {1.0f,1.0f,true} };
                 break;                                                    // Cove - Deep

        case 9:  c.points = { {0.0f,0.0f,true},  {0.33f,1.0f,true},
                              {0.34f,0.0f,true}, {0.67f,1.0f,true},
                              {0.68f,0.0f,true}, {1.0f,1.0f,true} };
                 break;                                                    // Sawtooth

        case 10: c.points = { {0.0f,0.0f,true},  {0.25f,0.0f,true},
                              {0.26f,0.5f,true}, {0.60f,0.5f,true},
                              {0.61f,1.0f,true}, {1.0f,1.0f,true} };
                 break;                                                    // Steps

        case 11: c.points = { {0.0f,0.0f,true}, {0.55f,0.28f,false}, {1.0f,1.0f,true} };
                 break;                                                    // Shallow Slope

        case 0:
        default: c.points = { {0.0f,0.0f,true}, {1.0f,1.0f,true} };
                 break;                                                    // Linear
    }

    return c;
}

//==============================================================================
juce::StringArray strokePositionNames()
{
    return { "Outside", "Centre", "Inside" };
}

juce::StringArray textureModeNames()
{
    return { "Off", "Bump", "Fill" };
}

juce::StringArray maskTypeNames()
{
    return { "Rotate", "Radius", "Horizontal", "Vertical" };
}

//==============================================================================
float AnimVal::at (float t) const noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);

    switch (mode)
    {
        case AnimMode::Fixed:
            return from;

        case AnimMode::Linear:
            return from + (to - from) * t;

        case AnimMode::Sine:
        {
            const float e = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::pi);
            return from + (to - from) * e;
        }

        case AnimMode::Reverse:
            return to + (from - to) * t;

        case AnimMode::Triangle:
        {
            const float e = (t <= 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
            return from + (to - from) * e;
        }

        case AnimMode::numModes:
        default:
            return from;
    }
}

//==============================================================================
bool LayerDoc::hasSolo() const noexcept
{
    for (const auto& l : layers)
        if (l.solo)
            return true;

    return false;
}

void LayerDoc::addLayer (const DocLayer& l)
{
    layers.push_back (l);
}

void LayerDoc::removeLayer (int index)
{
    if (index >= 0 && index < (int) layers.size())
        layers.erase (layers.begin() + index);
}

void LayerDoc::moveLayer (int from, int to)
{
    const int n = (int) layers.size();

    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;

    auto layer = layers[(size_t) from];
    layers.erase (layers.begin() + from);
    layers.insert (layers.begin() + to, layer);
}

//==============================================================================
LayerDoc LayerDoc::emptyDocument()
{
    LayerDoc doc;

    DocLayer l;
    l.name = "Body";
    l.prim.type   = PrimType::CircleFill;
    l.prim.colour = juce::Colour (0xffb8b8b8);
    l.prim.emboss = 40.0f;
    l.eff.zoomX = l.eff.zoomY = AnimVal (85.0f);

    doc.addLayer (l);
    return doc;
}

//==============================================================================
LayerDoc LayerDoc::knobExample()
{
    LayerDoc doc;
    doc.canvasWidth = doc.canvasHeight = 128;
    doc.frames = 65;

    // 1. cast shadow
    {
        DocLayer l;
        l.name = "Shadow";
        l.prim.type   = PrimType::CircleFill;
        l.prim.colour = juce::Colour (0x66000000);
        l.eff.zoomX = l.eff.zoomY = AnimVal (92.0f);
        l.eff.offsetY = AnimVal (3.0f);
        doc.addLayer (l);
    }

    // 2. fluted skirt — rotates with the value
    {
        DocLayer l;
        l.name = "Flutes";
        l.prim.type      = PrimType::RadiateLine;
        l.prim.colour    = juce::Colour (0xffbcb7ad);
        l.prim.step      = 30;
        l.prim.width     = 26.0f;
        l.prim.length    = 38.0f;
        l.prim.emboss    = 55.0f;
        l.prim.gloss     = 55.0f;
        l.prim.lightDir  = 115.0f;
        l.prim.specular  = 55.0f;
        l.eff.zoomX = l.eff.zoomY = AnimVal (88.0f);
        l.eff.angle = AnimVal (-135.0f, 135.0f, AnimMode::Linear);
        doc.addLayer (l);
    }

    // 3. skirt body
    {
        DocLayer l;
        l.name = "Skirt";
        l.prim.type    = PrimType::CircleFill;
        l.prim.colour  = juce::Colour (0xffd7d2c8);
        l.prim.emboss  = 30.0f;
        l.prim.gloss   = 70.0f;
        l.prim.lightDir = 115.0f;
        l.eff.zoomX = l.eff.zoomY = AnimVal (60.0f);
        doc.addLayer (l);
    }

    // 4. centre cap
    {
        DocLayer l;
        l.name = "Cap";
        l.prim.type     = PrimType::Sphere;
        l.prim.colour   = juce::Colour (0xffe6e2d9);
        l.prim.lightDir = 115.0f;
        l.prim.specular = 45.0f;
        l.prim.ambient  = 45.0f;
        l.eff.zoomX = l.eff.zoomY = AnimVal (56.0f);
        doc.addLayer (l);
    }

    // 5. pointer — rotates with the value
    {
        DocLayer l;
        l.name = "Pointer";
        l.prim.type   = PrimType::Line;
        l.prim.colour = juce::Colour (0xff2a2724);
        l.prim.width  = 7.0f;
        l.prim.length = 26.0f;
        l.prim.round  = 100.0f;
        l.eff.offsetY = AnimVal (-14.0f);
        l.eff.angle   = AnimVal (-135.0f, 135.0f, AnimMode::Linear);
        l.eff.keepDir = false;
        doc.addLayer (l);
    }

    return doc;
}

//==============================================================================
LayerDoc LayerDoc::sliderExample()
{
    LayerDoc doc;
    doc.canvasWidth  = 48;
    doc.canvasHeight = 128;
    doc.frames       = 65;

    // 1. rail
    {
        DocLayer l;
        l.name = "Rail";
        l.prim.type   = PrimType::RectFill;
        l.prim.colour = juce::Colour (0xff1a1a1c);
        l.prim.round  = 100.0f;
        l.prim.emboss = -35.0f;          // negative carves it into the panel
        l.eff.zoomX = AnimVal (14.0f);
        l.eff.zoomY = AnimVal (96.0f);
        l.eff.zoomSeparate = true;
        doc.addLayer (l);
    }

    // 2. cap, travelling top to bottom
    {
        DocLayer l;
        l.name = "Cap";
        l.prim.type   = PrimType::RectFill;
        l.prim.colour = juce::Colour (0xffc9c4ba);
        l.prim.round  = 28.0f;
        l.prim.emboss = 55.0f;
        l.eff.zoomX = AnimVal (72.0f);
        l.eff.zoomY = AnimVal (22.0f);
        l.eff.zoomSeparate = true;
        l.eff.offsetY = AnimVal (-36.0f, 36.0f, AnimMode::Linear);
        doc.addLayer (l);
    }

    // 3. grip stripe on the cap
    {
        DocLayer l;
        l.name = "Grip";
        l.prim.type   = PrimType::RectFill;
        l.prim.colour = juce::Colour (0xff8a3b2e);
        l.eff.zoomX = AnimVal (66.0f);
        l.eff.zoomY = AnimVal (4.0f);
        l.eff.zoomSeparate = true;
        l.eff.offsetY = AnimVal (-36.0f, 36.0f, AnimMode::Linear);
        doc.addLayer (l);
    }

    return doc;
}

juce::StringArray LayerDoc::exampleNames()
{
    return { "Empty", "Fluted Knob", "Slider Cap" };
}

LayerDoc LayerDoc::exampleByIndex (int index)
{
    switch (index)
    {
        case 1:  return knobExample();
        case 2:  return sliderExample();
        default: return emptyDocument();
    }
}

//==============================================================================
namespace
{
    void writeAnim (juce::ValueTree& t, const juce::String& id, const AnimVal& v)
    {
        t.setProperty (id + "_f", v.from, nullptr);
        t.setProperty (id + "_t", v.to, nullptr);
        t.setProperty (id + "_m", (int) v.mode, nullptr);
    }

    AnimVal readAnim (const juce::ValueTree& t, const juce::String& id, const AnimVal& fallback)
    {
        if (! t.hasProperty (id + "_f"))
            return fallback;

        AnimVal v;
        v.from = (float) t[id + "_f"];
        v.to   = (float) t[id + "_t"];
        v.mode = (AnimMode) juce::jlimit (0, (int) AnimMode::numModes - 1, (int) t[id + "_m"]);
        return v;
    }

    void writeMask (juce::ValueTree& parent, const juce::String& id, const MaskSpec& m)
    {
        juce::ValueTree t (id);
        t.setProperty ("enabled", m.enabled, nullptr);
        t.setProperty ("type",    (int) m.type, nullptr);
        t.setProperty ("orOp",    m.orOp, nullptr);
        t.setProperty ("biDir",   m.biDir, nullptr);
        writeAnim (t, "start", m.start);
        writeAnim (t, "stop",  m.stop);
        parent.appendChild (t, nullptr);
    }

    MaskSpec readMask (const juce::ValueTree& parent, const juce::String& id)
    {
        MaskSpec m;
        auto t = parent.getChildWithName (id);

        if (! t.isValid())
            return m;

        m.enabled = (bool) t.getProperty ("enabled", false);
        m.type    = (MaskType) juce::jlimit (0, (int) MaskType::numTypes - 1,
                                             (int) t.getProperty ("type", 0));
        m.orOp    = (bool) t.getProperty ("orOp", false);
        m.biDir   = (bool) t.getProperty ("biDir", false);
        m.start   = readAnim (t, "start", m.start);
        m.stop    = readAnim (t, "stop",  m.stop);
        return m;
    }
}

//==============================================================================
juce::ValueTree LayerDoc::toValueTree() const
{
    juce::ValueTree doc ("LayerDoc");
    doc.setProperty ("globalLightAngle",    globalLightAngle,    nullptr);
    doc.setProperty ("globalLightAltitude", globalLightAltitude, nullptr);
    doc.setProperty ("canvasWidth",  canvasWidth,  nullptr);
    doc.setProperty ("canvasHeight", canvasHeight, nullptr);
    doc.setProperty ("frames",       frames,       nullptr);

    for (const auto& layer : layers)
    {
        juce::ValueTree lt ("Layer");
        lt.setProperty ("name",    layer.name,    nullptr);
        lt.setProperty ("visible", layer.visible, nullptr);
        lt.setProperty ("solo",    layer.solo,    nullptr);

        // ---- primitive -----------------------------------------------------
        juce::ValueTree pt ("Primitive");
        const auto& p = layer.prim;

        pt.setProperty ("type",          (int) p.type, nullptr);
        pt.setProperty ("colour",        (juce::int64) p.colour.getARGB(), nullptr);
        pt.setProperty ("aspect",        p.aspect,        nullptr);
        pt.setProperty ("round",         p.round,         nullptr);
        pt.setProperty ("width",         p.width,         nullptr);
        pt.setProperty ("length",        p.length,        nullptr);
        pt.setProperty ("step",          p.step,          nullptr);
        pt.setProperty ("angleStep",     p.angleStep,     nullptr);
        pt.setProperty ("emboss",        p.emboss,        nullptr);
        pt.setProperty ("embossDiffuse", p.embossDiffuse, nullptr);
        pt.setProperty ("ambient",       p.ambient,       nullptr);
        pt.setProperty ("lightDir",      p.lightDir,      nullptr);
        pt.setProperty ("specular",      p.specular,      nullptr);
        pt.setProperty ("specularWidth", p.specularWidth, nullptr);
        pt.setProperty ("diffuse",       p.diffuse,       nullptr);
        pt.setProperty ("gloss",         p.gloss,         nullptr);
        pt.setProperty ("text",          p.text,          nullptr);
        pt.setProperty ("fontSize",      p.fontSize,      nullptr);
        pt.setProperty ("bold",          p.bold,          nullptr);
        pt.setProperty ("italic",        p.italic,        nullptr);
        pt.setProperty ("textAlign",     p.textAlign,     nullptr);
        pt.setProperty ("imageFile",     p.imageFile,     nullptr);
        pt.setProperty ("fill",          p.fill,          nullptr);
        pt.setProperty ("ledShape",      (int) p.ledShape,  nullptr);
        pt.setProperty ("ledOn",         p.ledOn,           nullptr);
        pt.setProperty ("ledFollow",     p.ledFollowFrame,  nullptr);
        pt.setProperty ("ledSize",       p.ledSize,         nullptr);
        pt.setProperty ("ledIntensity",  p.ledIntensity,    nullptr);
        pt.setProperty ("ledCore",       p.ledCore,         nullptr);
        pt.setProperty ("ledOffLevel",   p.ledOffLevel,     nullptr);
        pt.setProperty ("ledGlow",       p.ledGlow,         nullptr);
        pt.setProperty ("ledBezel",      p.ledBezel,        nullptr);
        pt.setProperty ("ledBezelCol",   p.ledBezelColour.toString(), nullptr);
        pt.setProperty ("meterOuterLen", p.meterOuterLen,   nullptr);
        pt.setProperty ("meterLabels",   p.meterLabels,     nullptr);
        pt.setProperty ("meterLabelGap", p.meterLabelGap,   nullptr);
        pt.setProperty ("meterLabelsIn", p.meterLabelsInside, nullptr);
        pt.setProperty ("meterEvery",    p.meterEvery,      nullptr);
        pt.setProperty ("meterMinorLen", p.meterMinorLen,   nullptr);
        pt.setProperty ("meterMinorW",   p.meterMinorWidth, nullptr);
        pt.setProperty ("meterStart",    p.meterStart,      nullptr);
        pt.setProperty ("meterEnd",      p.meterEnd,        nullptr);
        pt.setProperty ("rayBend",       p.rayBend,      nullptr);
        pt.setProperty ("raySkew",       p.raySkew,      nullptr);
        pt.setProperty ("rayRound",      p.rayRoundness, nullptr);
        pt.setProperty ("shadOn",        p.shadow.enabled,  nullptr);
        pt.setProperty ("shadCol",       p.shadow.colour.toString(), nullptr);
        pt.setProperty ("shadOp",        p.shadow.opacity,  nullptr);
        pt.setProperty ("shadX",         p.shadow.offsetX,  nullptr);
        pt.setProperty ("shadY",         p.shadow.offsetY,  nullptr);
        pt.setProperty ("shadSize",      p.shadow.size,     nullptr);
        pt.setProperty ("shadFade",      p.shadow.fade,     nullptr);
        pt.setProperty ("shadKnock",     p.shadow.knockout, nullptr);
        pt.setProperty ("shadCurve",     p.shadow.curve,   nullptr);
        pt.setProperty ("shadHot",       p.shadow.hotCore, nullptr);
        pt.setProperty ("shadBlend",     (int) p.shadow.blend, nullptr);
        auto writeShadow = [&pt] (const juce::String& prefix, const ShadowSpec& v)
        {
            pt.setProperty (prefix + "On",    v.enabled,  nullptr);
            pt.setProperty (prefix + "Col",   v.colour.toString(), nullptr);
            pt.setProperty (prefix + "Op",    v.opacity,  nullptr);
            pt.setProperty (prefix + "X",     v.offsetX,  nullptr);
            pt.setProperty (prefix + "Y",     v.offsetY,  nullptr);
            pt.setProperty (prefix + "Size",  v.size,     nullptr);
            pt.setProperty (prefix + "Fade",  v.fade,     nullptr);
            pt.setProperty (prefix + "Knock", v.knockout, nullptr);
            pt.setProperty (prefix + "Curve", v.curve,   nullptr);
            pt.setProperty (prefix + "Hot",   v.hotCore, nullptr);
            pt.setProperty (prefix + "Blend", (int) v.blend, nullptr);
        };

        writeShadow ("inner", p.innerShadow);
        writeShadow ("glow",  p.outerGlow);

        pt.setProperty ("bevOn",         p.bevel.enabled, nullptr);
        pt.setProperty ("bevStyle",      (int) p.bevel.style, nullptr);
        pt.setProperty ("bevTech",       (int) p.bevel.technique, nullptr);
        pt.setProperty ("bevDepth",      p.bevel.depth,  nullptr);
        pt.setProperty ("bevUp",         p.bevel.up,     nullptr);
        pt.setProperty ("bevSize",       p.bevel.size,   nullptr);
        pt.setProperty ("bevSoften",     p.bevel.soften, nullptr);
        pt.setProperty ("bevAA",         p.bevel.antiAliased,    nullptr);
        pt.setProperty ("bevContourOn",  p.bevel.contourEnabled, nullptr);
        pt.setProperty ("bevBevContour", p.bevel.bevelContour.toString(), nullptr);
        pt.setProperty ("bevRange",      p.bevel.contourRange,   nullptr);
        pt.setProperty ("bevGlobal",     p.bevel.useGlobalLight, nullptr);
        pt.setProperty ("bevAngle",      p.bevel.angle,    nullptr);
        pt.setProperty ("bevAlt",        p.bevel.altitude, nullptr);
        pt.setProperty ("bevContour",    p.bevel.glossContour.toString(), nullptr);
        pt.setProperty ("bevHiMode",     (int) p.bevel.highlightMode, nullptr);
        pt.setProperty ("bevHiCol",      p.bevel.highlightColour.toString(), nullptr);
        pt.setProperty ("bevHiOp",       p.bevel.highlightOpacity, nullptr);
        pt.setProperty ("bevShMode",     (int) p.bevel.shadowMode, nullptr);
        pt.setProperty ("bevShCol",      p.bevel.shadowColour.toString(), nullptr);
        pt.setProperty ("bevShOp",       p.bevel.shadowOpacity, nullptr);
        pt.setProperty ("strokeOn",      p.strokeEnabled, nullptr);
        pt.setProperty ("strokeCol",     p.strokeColour.toString(), nullptr);
        pt.setProperty ("strokeW",       p.strokeWidth,   nullptr);
        pt.setProperty ("strokePos",     (int) p.strokePosition, nullptr);
        pt.setProperty ("texMode",       (int) p.textureMode, nullptr);
        pt.setProperty ("texName",       p.textureName,   nullptr);
        pt.setProperty ("texDepth",      p.textureDepth,  nullptr);
        pt.setProperty ("texScale",      p.textureScale,  nullptr);
        pt.setProperty ("texAngle",      p.textureAngle,  nullptr);

        pt.setProperty ("shapeName", p.shapeName, nullptr);

        if (! p.shapePoints.empty())
        {
            juce::StringArray pts;

            for (const auto& sp : p.shapePoints)
                pts.add (juce::String (sp.x, 4) + "," + juce::String (sp.y, 4));

            pt.setProperty ("shapePoints", pts.joinIntoString (" "), nullptr);
        }

        lt.appendChild (pt, nullptr);

        // ---- effect --------------------------------------------------------
        juce::ValueTree et ("Effect");
        const auto& e = layer.eff;

        et.setProperty ("antialias",    e.antialias,    nullptr);
        et.setProperty ("zoomSeparate", e.zoomSeparate, nullptr);
        et.setProperty ("keepDir",      e.keepDir,      nullptr);
        et.setProperty ("centreX",      e.centreX,      nullptr);
        et.setProperty ("centreY",      e.centreY,      nullptr);

        writeAnim (et, "zoomX",      e.zoomX);
        writeAnim (et, "zoomY",      e.zoomY);
        writeAnim (et, "offsetX",    e.offsetX);
        writeAnim (et, "offsetY",    e.offsetY);
        writeAnim (et, "angle",      e.angle);
        writeAnim (et, "alpha",      e.alpha);
        writeAnim (et, "brightness", e.brightness);
        writeAnim (et, "contrast",   e.contrast);
        writeAnim (et, "saturation", e.saturation);
        writeAnim (et, "hue",        e.hue);

        writeMask (et, "Mask1", e.mask1);
        writeMask (et, "Mask2", e.mask2);

        lt.appendChild (et, nullptr);

        doc.appendChild (lt, nullptr);
    }

    return doc;
}

//==============================================================================
LayerDoc LayerDoc::fromValueTree (const juce::ValueTree& tree)
{
    LayerDoc doc;

    if (! tree.hasType ("LayerDoc"))
        return emptyDocument();

    doc.layers.clear();
    doc.globalLightAngle    = (float) tree.getProperty ("globalLightAngle",    120.0f);
    doc.globalLightAltitude = (float) tree.getProperty ("globalLightAltitude",  30.0f);
    doc.canvasWidth  = juce::jlimit (8, 2048, (int) tree.getProperty ("canvasWidth",  128));
    doc.canvasHeight = juce::jlimit (8, 2048, (int) tree.getProperty ("canvasHeight", 128));
    doc.frames       = juce::jlimit (1, 512,  (int) tree.getProperty ("frames",        65));

    for (const auto& lt : tree)
    {
        if (! lt.hasType ("Layer"))
            continue;

        DocLayer layer;
        layer.name    = lt.getProperty ("name", "Layer").toString();
        layer.visible = (bool) lt.getProperty ("visible", true);
        layer.solo    = (bool) lt.getProperty ("solo", false);

        if (auto pt = lt.getChildWithName ("Primitive"); pt.isValid())
        {
            auto& p = layer.prim;

            p.type   = (PrimType) juce::jlimit (0, (int) PrimType::numTypes - 1,
                                                (int) pt.getProperty ("type", 3));
            p.colour = juce::Colour ((juce::uint32) (juce::int64) pt.getProperty ("colour",
                                                     (juce::int64) 0xffb8b8b8));

            p.aspect        = (float) pt.getProperty ("aspect",        p.aspect);
            p.round         = (float) pt.getProperty ("round",         p.round);
            p.width         = (float) pt.getProperty ("width",         p.width);
            p.length        = (float) pt.getProperty ("length",        p.length);
            p.step          = (int)   pt.getProperty ("step",          p.step);
            p.angleStep     = (float) pt.getProperty ("angleStep",     p.angleStep);
            p.emboss        = (float) pt.getProperty ("emboss",        p.emboss);
            p.embossDiffuse = (float) pt.getProperty ("embossDiffuse", p.embossDiffuse);
            p.ambient       = (float) pt.getProperty ("ambient",       p.ambient);
            p.lightDir      = (float) pt.getProperty ("lightDir",      p.lightDir);
            p.specular      = (float) pt.getProperty ("specular",      p.specular);
            p.specularWidth = (float) pt.getProperty ("specularWidth", p.specularWidth);
            p.diffuse       = (float) pt.getProperty ("diffuse",       p.diffuse);
            p.gloss         = (float) pt.getProperty ("gloss",         p.gloss);
            p.text          = pt.getProperty ("text", p.text).toString();
            p.fontSize      = (float) pt.getProperty ("fontSize",      p.fontSize);
            p.bold          = (bool)  pt.getProperty ("bold",          p.bold);
            p.italic        = (bool)  pt.getProperty ("italic",        p.italic);
            p.textAlign     = (int)   pt.getProperty ("textAlign",     p.textAlign);
            p.imageFile     = pt.getProperty ("imageFile", juce::String()).toString();
            p.fill          = (bool)  pt.getProperty ("fill",          p.fill);
            p.ledShape       = (LedShape) juce::jlimit (0, (int) LedShape::numShapes - 1,
                                                       (int) pt.getProperty ("ledShape", 0));
            p.ledOn          = (bool)  pt.getProperty ("ledOn",        p.ledOn);
            p.ledFollowFrame = (bool)  pt.getProperty ("ledFollow",    p.ledFollowFrame);
            p.ledSize        = (float) pt.getProperty ("ledSize",      p.ledSize);
            p.ledIntensity   = (float) pt.getProperty ("ledIntensity", p.ledIntensity);
            p.ledCore        = (float) pt.getProperty ("ledCore",      p.ledCore);
            p.ledOffLevel    = (float) pt.getProperty ("ledOffLevel",  p.ledOffLevel);
            p.ledGlow        = (float) pt.getProperty ("ledGlow",      p.ledGlow);
            p.ledBezel       = (float) pt.getProperty ("ledBezel",     p.ledBezel);

            if (pt.hasProperty ("ledBezelCol"))
                p.ledBezelColour = juce::Colour::fromString (pt.getProperty ("ledBezelCol").toString());

            p.meterOuterLen   = (float) pt.getProperty ("meterOuterLen", p.meterOuterLen);
            p.meterLabels       = pt.getProperty ("meterLabels", p.meterLabels).toString();
            p.meterLabelGap     = (float) pt.getProperty ("meterLabelGap", p.meterLabelGap);
            p.meterLabelsInside = (bool)  pt.getProperty ("meterLabelsIn", p.meterLabelsInside);
            p.meterEvery      = (int)   pt.getProperty ("meterEvery",    p.meterEvery);
            p.meterMinorLen   = (float) pt.getProperty ("meterMinorLen", p.meterMinorLen);
            p.meterMinorWidth = (float) pt.getProperty ("meterMinorW",   p.meterMinorWidth);
            p.meterStart      = (float) pt.getProperty ("meterStart",    p.meterStart);
            p.meterEnd        = (float) pt.getProperty ("meterEnd",      p.meterEnd);
            p.rayBend      = (float) pt.getProperty ("rayBend",  p.rayBend);
            p.raySkew      = (float) pt.getProperty ("raySkew",  p.raySkew);
            p.rayRoundness = (float) pt.getProperty ("rayRound", p.rayRoundness);

            auto& sh = p.shadow;
            sh.enabled  = (bool)  pt.getProperty ("shadOn",    sh.enabled);
            sh.opacity  = (float) pt.getProperty ("shadOp",    sh.opacity);
            sh.offsetX  = (float) pt.getProperty ("shadX",     sh.offsetX);
            sh.offsetY  = (float) pt.getProperty ("shadY",     sh.offsetY);
            sh.size     = (float) pt.getProperty ("shadSize",  sh.size);
            sh.fade     = (float) pt.getProperty ("shadFade",  sh.fade);
            sh.knockout = (bool)  pt.getProperty ("shadKnock", sh.knockout);
            sh.curve    = (float) pt.getProperty ("shadCurve", sh.curve);
            sh.hotCore  = (float) pt.getProperty ("shadHot",   sh.hotCore);
            sh.blend    = (BlendMode) juce::jlimit (0, (int) BlendMode::numModes - 1,
                                                    (int) pt.getProperty ("shadBlend", (int) sh.blend));

            if (pt.hasProperty ("shadCol"))
                sh.colour = juce::Colour::fromString (pt.getProperty ("shadCol").toString());

            auto readShadow = [&pt] (const juce::String& prefix, ShadowSpec& v)
            {
                v.enabled  = (bool)  pt.getProperty (prefix + "On",    v.enabled);
                v.opacity  = (float) pt.getProperty (prefix + "Op",    v.opacity);
                v.offsetX  = (float) pt.getProperty (prefix + "X",     v.offsetX);
                v.offsetY  = (float) pt.getProperty (prefix + "Y",     v.offsetY);
                v.size     = (float) pt.getProperty (prefix + "Size",  v.size);
                v.fade     = (float) pt.getProperty (prefix + "Fade",  v.fade);
                v.knockout = (bool)  pt.getProperty (prefix + "Knock", v.knockout);
                v.curve    = (float) pt.getProperty (prefix + "Curve", v.curve);
                v.hotCore  = (float) pt.getProperty (prefix + "Hot",   v.hotCore);
                v.blend    = (BlendMode) juce::jlimit (0, (int) BlendMode::numModes - 1,
                                                       (int) pt.getProperty (prefix + "Blend", (int) v.blend));

                if (pt.hasProperty (prefix + "Col"))
                    v.colour = juce::Colour::fromString (pt.getProperty (prefix + "Col").toString());
            };

            readShadow ("inner", p.innerShadow);
            readShadow ("glow",  p.outerGlow);

            auto& b = p.bevel;
            b.enabled   = (bool)  pt.getProperty ("bevOn",     b.enabled);
            b.style     = (BevelStyle) juce::jlimit (0, (int) BevelStyle::numStyles - 1,
                                                     (int) pt.getProperty ("bevStyle", (int) b.style));
            b.technique = (BevelTechnique) juce::jlimit (0, (int) BevelTechnique::numTechniques - 1,
                                                         (int) pt.getProperty ("bevTech", (int) b.technique));
            b.depth     = (float) pt.getProperty ("bevDepth",  b.depth);
            b.up        = (bool)  pt.getProperty ("bevUp",     b.up);
            b.size      = (float) pt.getProperty ("bevSize",   b.size);
            b.soften    = (float) pt.getProperty ("bevSoften", b.soften);
            b.antiAliased    = (bool)  pt.getProperty ("bevAA",        b.antiAliased);
            b.contourEnabled = (bool)  pt.getProperty ("bevContourOn", b.contourEnabled);
            b.contourRange   = (float) pt.getProperty ("bevRange",     b.contourRange);
            b.useGlobalLight = (bool) pt.getProperty ("bevGlobal", b.useGlobalLight);

            if (pt.hasProperty ("bevBevContour"))
                b.bevelContour = Contour::fromString (pt.getProperty ("bevBevContour").toString());
            b.angle     = (float) pt.getProperty ("bevAngle",  b.angle);
            b.altitude  = (float) pt.getProperty ("bevAlt",    b.altitude);
            b.highlightOpacity = (float) pt.getProperty ("bevHiOp", b.highlightOpacity);
            b.shadowOpacity    = (float) pt.getProperty ("bevShOp", b.shadowOpacity);
            b.highlightMode = (BlendMode) juce::jlimit (0, (int) BlendMode::numModes - 1,
                                                        (int) pt.getProperty ("bevHiMode", (int) b.highlightMode));
            b.shadowMode    = (BlendMode) juce::jlimit (0, (int) BlendMode::numModes - 1,
                                                        (int) pt.getProperty ("bevShMode", (int) b.shadowMode));

            if (pt.hasProperty ("bevContour"))
                b.glossContour = Contour::fromString (pt.getProperty ("bevContour").toString());

            if (pt.hasProperty ("bevHiCol"))
                b.highlightColour = juce::Colour::fromString (pt.getProperty ("bevHiCol").toString());

            if (pt.hasProperty ("bevShCol"))
                b.shadowColour = juce::Colour::fromString (pt.getProperty ("bevShCol").toString());

            p.strokeEnabled = (bool)  pt.getProperty ("strokeOn", p.strokeEnabled);
            p.strokeWidth   = (float) pt.getProperty ("strokeW",  p.strokeWidth);
            p.strokePosition = (StrokePosition) juce::jlimit (0, (int) StrokePosition::numPositions - 1,
                                                              (int) pt.getProperty ("strokePos", 0));

            if (pt.hasProperty ("strokeCol"))
                p.strokeColour = juce::Colour::fromString (pt.getProperty ("strokeCol").toString());

            p.textureMode   = (TextureMode) juce::jlimit (0, (int) TextureMode::numModes - 1,
                                                          (int) pt.getProperty ("texMode", 0));
            p.textureName   = pt.getProperty ("texName", p.textureName).toString();
            p.textureDepth  = (float) pt.getProperty ("texDepth", p.textureDepth);
            p.textureScale  = (float) pt.getProperty ("texScale", p.textureScale);
            p.textureAngle  = (float) pt.getProperty ("texAngle", p.textureAngle);

            p.shapeName = pt.getProperty ("shapeName", p.shapeName).toString();

            if (pt.hasProperty ("shapePoints"))
            {
                juce::StringArray pts;
                pts.addTokens (pt.getProperty ("shapePoints").toString(), " ", {});

                for (const auto& s : pts)
                {
                    if (! s.contains (","))
                        continue;

                    p.shapePoints.push_back ({ s.upToFirstOccurrenceOf (",", false, false).getFloatValue(),
                                               s.fromFirstOccurrenceOf (",", false, false).getFloatValue() });
                }
            }
        }

        if (auto et = lt.getChildWithName ("Effect"); et.isValid())
        {
            auto& e = layer.eff;

            e.antialias    = (bool)  et.getProperty ("antialias",    e.antialias);
            e.zoomSeparate = (bool)  et.getProperty ("zoomSeparate", e.zoomSeparate);
            e.keepDir      = (bool)  et.getProperty ("keepDir",      e.keepDir);
            e.centreX      = (float) et.getProperty ("centreX",      e.centreX);
            e.centreY      = (float) et.getProperty ("centreY",      e.centreY);

            e.zoomX      = readAnim (et, "zoomX",      e.zoomX);
            e.zoomY      = readAnim (et, "zoomY",      e.zoomY);
            e.offsetX    = readAnim (et, "offsetX",    e.offsetX);
            e.offsetY    = readAnim (et, "offsetY",    e.offsetY);
            e.angle      = readAnim (et, "angle",      e.angle);
            e.alpha      = readAnim (et, "alpha",      e.alpha);
            e.brightness = readAnim (et, "brightness", e.brightness);
            e.contrast   = readAnim (et, "contrast",   e.contrast);
            e.saturation = readAnim (et, "saturation", e.saturation);
            e.hue        = readAnim (et, "hue",        e.hue);

            e.mask1 = readMask (et, "Mask1");
            e.mask2 = readMask (et, "Mask2");
        }

        doc.layers.push_back (layer);
    }

    if (doc.layers.empty())
        doc.layers.push_back (DocLayer());

    return doc;
}

//==============================================================================
namespace
{
    juce::File& imagesFolder()
    {
        static juce::File folder;
        return folder;
    }

    std::function<juce::String (const juce::File&)>& imageAdopter()
    {
        static std::function<juce::String (const juce::File&)> adopter;
        return adopter;
    }
}

void LayerImages::setFolder (const juce::File& folder)   { imagesFolder() = folder; }
juce::File LayerImages::getFolder()                       { return imagesFolder(); }

juce::File LayerImages::resolve (const juce::String& imageFile)
{
    const auto name = imageFile.trim();

    if (name.isEmpty())
        return {};

    if (juce::File::isAbsolutePath (name))
        return juce::File (name);

    return imagesFolder() != juce::File() ? imagesFolder().getChildFile (name) : juce::File();
}

void LayerImages::setAdopter (std::function<juce::String (const juce::File&)> adopter)
{
    imageAdopter() = std::move (adopter);
}

juce::String LayerImages::adopt (const juce::File& chosen)
{
    if (imageAdopter() != nullptr)
        if (const auto name = imageAdopter() (chosen); name.isNotEmpty())
            return name;

    return chosen.getFullPathName();
}
