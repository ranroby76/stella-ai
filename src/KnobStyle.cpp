// C:\workspace\Stella AI Studio\src\KnobStyle.cpp
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knobs in both apps.

/*
    KnobStyle.cpp
*/

#include "KnobStyle.h"

//==============================================================================
KnobStyle KnobStyle::cream()
{
    KnobStyle s;
    s.body       = juce::Colour (0xffd7d2c8);
    s.cap        = juce::Colour (0xffe6e2d9);
    s.pointer    = juce::Colour (0xff2a2724);
    s.bezel      = juce::Colour (0x00000000);
    s.tickCount  = 0;
    s.fluteCount = 30;
    return s;
}

KnobStyle KnobStyle::black()
{
    KnobStyle s;
    s.body    = juce::Colour (0xff313134);
    s.cap     = juce::Colour (0xff1d1d20);
    s.pointer = juce::Colour (0xffe8e2d2);
    s.bezel   = juce::Colour (0xff96793d);
    s.tick    = juce::Colour (0xffe8dcc0);
    s.shadow  = juce::Colour (0x99000000);

    s.tickCount         = 11;
    s.fluteCount        = 26;
    s.ambient           = 0.26f;
    s.specularStrength  = 0.85f;
    s.specularTightness = 14.0f;
    return s;
}

KnobStyle KnobStyle::brushedMetal()
{
    KnobStyle s;
    s.body    = juce::Colour (0xff8d9096);
    s.cap     = juce::Colour (0xff9ba0a7);
    s.pointer = juce::Colour (0xff17191c);
    s.bezel   = juce::Colour (0x00000000);

    s.fluteCount        = 36;
    s.ambient           = 0.24f;
    s.specularStrength  = 1.0f;
    s.specularTightness = 20.0f;
    return s;
}

juce::StringArray KnobStyle::presetNames()
{
    return { "Cream", "Black + Gold Bezel", "Brushed Metal" };
}

KnobStyle KnobStyle::presetByIndex (int index)
{
    switch (index)
    {
        case 1:  return black();
        case 2:  return brushedMetal();
        default: return cream();
    }
}

//==============================================================================
const std::vector<KnobStyle::FloatParam>& KnobStyle::floatParams()
{
    static const std::vector<FloatParam> params
    {
        { "lightAngle",        "Light Angle",       "Lighting", -3.1416f, 3.1416f, 0.01f,  &KnobStyle::lightAngle },
        { "ambient",           "Ambient",           "Lighting",  0.0f,    0.9f,    0.01f,  &KnobStyle::ambient },
        { "specularStrength",  "Specular Strength", "Lighting",  0.0f,    1.5f,    0.01f,  &KnobStyle::specularStrength },
        { "specularTightness", "Specular Tightness","Lighting",  1.0f,   60.0f,    0.5f,   &KnobStyle::specularTightness },

        { "capRadius",         "Cap Radius",        "Geometry",  0.20f,   0.94f,   0.01f,  &KnobStyle::capRadius },
        { "shadowRadius",      "Shadow Blur",       "Geometry",  0.0f,    0.30f,   0.005f, &KnobStyle::shadowRadius },
        { "shadowOffset",      "Shadow Drop",       "Geometry",  0.0f,    0.20f,   0.005f, &KnobStyle::shadowOffset },

        { "fluteInner",        "Flute Inner",       "Flutes",    0.10f,   0.95f,   0.01f,  &KnobStyle::fluteInner },
        { "fluteOuter",        "Flute Outer",       "Flutes",    0.20f,   1.00f,   0.01f,  &KnobStyle::fluteOuter },
        { "fluteDuty",         "Flute Duty",        "Flutes",    0.10f,   0.96f,   0.01f,  &KnobStyle::fluteDuty },

        { "pointerInner",      "Pointer Inner",     "Pointer",   0.0f,    0.90f,   0.01f,  &KnobStyle::pointerInner },
        { "pointerOuter",      "Pointer Outer",     "Pointer",   0.15f,   1.00f,   0.01f,  &KnobStyle::pointerOuter },
        { "pointerWidth",      "Pointer Width",     "Pointer",   0.01f,   0.30f,   0.005f, &KnobStyle::pointerWidth },

        { "bezelWidth",        "Bezel Width",       "Bezel",     0.03f,   0.40f,   0.01f,  &KnobStyle::bezelWidth },
    };

    return params;
}

const std::vector<KnobStyle::IntParam>& KnobStyle::intParams()
{
    static const std::vector<IntParam> params
    {
        { "fluteCount", "Flute Count", "Flutes", 6, 64, &KnobStyle::fluteCount },
        { "tickCount",  "Tick Count",  "Bezel",  0, 41, &KnobStyle::tickCount },
    };

    return params;
}

const std::vector<KnobStyle::BoolParam>& KnobStyle::boolParams()
{
    static const std::vector<BoolParam> params
    {
        { "drawFlutes", "Draw Flutes", "Flutes",  &KnobStyle::drawFlutes },
        { "rotateBody", "Body Rotates","Flutes",  &KnobStyle::rotateBody },
    };

    return params;
}

const std::vector<KnobStyle::ColourParam>& KnobStyle::colourParams()
{
    static const std::vector<ColourParam> params
    {
        { "body",    "Body",    "Colours", &KnobStyle::body },
        { "cap",     "Cap",     "Colours", &KnobStyle::cap },
        { "pointer", "Pointer", "Colours", &KnobStyle::pointer },
        { "bezel",   "Bezel",   "Colours", &KnobStyle::bezel },
        { "tick",    "Tick",    "Colours", &KnobStyle::tick },
        { "shadow",  "Shadow",  "Colours", &KnobStyle::shadow },
    };

    return params;
}

juce::StringArray KnobStyle::groupOrder()
{
    return { "Colours", "Lighting", "Geometry", "Flutes", "Pointer", "Bezel" };
}

//==============================================================================
juce::ValueTree KnobStyle::toValueTree() const
{
    juce::ValueTree tree ("KnobStyle");

    for (const auto& p : colourParams())
        tree.setProperty (p.id, (juce::int64) (this->*(p.member)).getARGB(), nullptr);

    for (const auto& p : floatParams())
        tree.setProperty (p.id, this->*(p.member), nullptr);

    for (const auto& p : intParams())
        tree.setProperty (p.id, this->*(p.member), nullptr);

    for (const auto& p : boolParams())
        tree.setProperty (p.id, this->*(p.member), nullptr);

    return tree;
}

KnobStyle KnobStyle::fromValueTree (const juce::ValueTree& tree)
{
    KnobStyle s;

    if (! tree.hasType ("KnobStyle"))
        return s;

    for (const auto& p : colourParams())
        if (tree.hasProperty (p.id))
            s.*(p.member) = juce::Colour ((juce::uint32) (juce::int64) tree[p.id]);

    for (const auto& p : floatParams())
        if (tree.hasProperty (p.id))
            s.*(p.member) = (float) tree[p.id];

    for (const auto& p : intParams())
        if (tree.hasProperty (p.id))
            s.*(p.member) = (int) tree[p.id];

    for (const auto& p : boolParams())
        if (tree.hasProperty (p.id))
            s.*(p.member) = (bool) tree[p.id];

    return s;
}

//==============================================================================
static juce::String colourLiteral (juce::Colour c)
{
    return "juce::Colour (0x" + juce::String::toHexString ((int) c.getARGB()).paddedLeft ('0', 8) + ")";
}

static juce::String floatLiteral (float v)
{
    auto s = juce::String (v, 4);

    while (s.endsWith ("0") && ! s.endsWith (".0"))
        s = s.dropLastCharacters (1);

    return s + "f";
}

juce::String KnobStyle::toCppSource (const juce::String& variableName) const
{
    juce::StringArray lines;

    lines.add ("// Generated by KnobMaker");
    lines.add ("fanan::VintageKnobLookAndFeel::Style " + variableName + ";");
    lines.add ({});

    auto emitGroup = [&] (const juce::String& group)
    {
        juce::StringArray groupLines;

        for (const auto& p : colourParams())
            if (group == p.group)
                groupLines.add (variableName + "." + p.id + " = " + colourLiteral (this->*(p.member)) + ";");

        for (const auto& p : floatParams())
            if (group == p.group)
                groupLines.add (variableName + "." + p.id + " = " + floatLiteral (this->*(p.member)) + ";");

        for (const auto& p : intParams())
            if (group == p.group)
                groupLines.add (variableName + "." + p.id + " = " + juce::String (this->*(p.member)) + ";");

        for (const auto& p : boolParams())
            if (group == p.group)
                groupLines.add (variableName + "." + p.id + " = " + ((this->*(p.member)) ? "true;" : "false;"));

        if (! groupLines.isEmpty())
        {
            lines.add ("// " + group);
            lines.addArray (groupLines);
            lines.add ({});
        }
    };

    for (const auto& group : groupOrder())
        emitGroup (group);

    return lines.joinIntoString ("\n");
}

//==============================================================================
bool KnobStyle::operator== (const KnobStyle& other) const
{
    for (const auto& p : colourParams())
        if ((this->*(p.member)) != (other.*(p.member)))
            return false;

    for (const auto& p : floatParams())
        if (! juce::approximatelyEqual (this->*(p.member), other.*(p.member)))
            return false;

    for (const auto& p : intParams())
        if ((this->*(p.member)) != (other.*(p.member)))
            return false;

    for (const auto& p : boolParams())
        if ((this->*(p.member)) != (other.*(p.member)))
            return false;

    return true;
}
