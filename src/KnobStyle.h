// C:\workspace\Stella AI Studio\src\KnobStyle.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same knobs in both apps.

/*
    KnobStyle.h

    The document. Every parameter that defines a knob's appearance, plus the
    reflection tables the editor panel builds itself from.

    Adding a new parameter means adding a field and one line to the matching
    table. The editor UI, serialisation and C++ export all follow automatically.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

//==============================================================================
struct KnobStyle
{
    // ---- colours -----------------------------------------------------------
    juce::Colour body    { 0xffd7d2c8 };
    juce::Colour cap     { 0xffe6e2d9 };
    juce::Colour pointer { 0xff2a2724 };
    juce::Colour bezel   { 0x00000000 };
    juce::Colour tick    { 0xff9a8f74 };
    juce::Colour shadow  { 0x88000000 };

    // ---- lighting ----------------------------------------------------------
    float lightAngle        = -0.42f;   // radians clockwise from 12 o'clock
    float ambient           =  0.20f;
    float specularStrength  =  0.60f;
    float specularTightness = 10.0f;

    // ---- geometry (fractions of the knob radius) ---------------------------
    float capRadius    = 0.64f;
    float fluteInner   = 0.60f;
    float fluteOuter   = 0.97f;
    float fluteDuty    = 0.56f;
    float pointerInner = 0.12f;
    float pointerOuter = 0.54f;
    float pointerWidth = 0.085f;
    float shadowRadius = 0.11f;
    float shadowOffset = 0.055f;
    float bezelWidth   = 0.11f;

    // ---- counts ------------------------------------------------------------
    int fluteCount = 30;
    int tickCount  = 0;

    // ---- flags -------------------------------------------------------------
    bool drawFlutes = true;
    bool rotateBody = true;

    //==========================================================================
    // Presets

    static KnobStyle cream();
    static KnobStyle black();
    static KnobStyle brushedMetal();

    static juce::StringArray presetNames();
    static KnobStyle          presetByIndex (int index);

    //==========================================================================
    // Reflection tables — the editor panel is generated from these

    struct FloatParam
    {
        const char* id;
        const char* name;
        const char* group;
        float min, max, interval;
        float KnobStyle::* member;
    };

    struct IntParam
    {
        const char* id;
        const char* name;
        const char* group;
        int min, max;
        int KnobStyle::* member;
    };

    struct BoolParam
    {
        const char* id;
        const char* name;
        const char* group;
        bool KnobStyle::* member;
    };

    struct ColourParam
    {
        const char* id;
        const char* name;
        const char* group;
        juce::Colour KnobStyle::* member;
    };

    static const std::vector<FloatParam>&  floatParams();
    static const std::vector<IntParam>&    intParams();
    static const std::vector<BoolParam>&   boolParams();
    static const std::vector<ColourParam>& colourParams();

    /** Group names in the order the editor should show them. */
    static juce::StringArray groupOrder();

    //==========================================================================
    // Serialisation

    juce::ValueTree toValueTree() const;
    static KnobStyle fromValueTree (const juce::ValueTree&);

    /** Emits a C++ initialiser the user can paste into a plugin. */
    juce::String toCppSource (const juce::String& variableName = "knobStyle") const;

    bool operator== (const KnobStyle&) const;
    bool operator!= (const KnobStyle& other) const { return ! operator== (other); }
};
