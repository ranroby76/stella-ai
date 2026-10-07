// C:\workspace\Stella AI Studio\src\GuiLayout.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobStyle.h"

#include <map>
#include <vector>

//==============================================================================
/** One element of the plugin's GUI. Controls are bound to a parameter by its id; meters,
    lamps and scopes watch a source (a signal or a module's display). */
struct GuiWidget
{
    enum class Type { knob, slider, toggle, selector, label, group,
                      meter, scope, lamp, envelope, filter, xy, shape, preset };

    Type type = Type::knob;
    juce::Rectangle<int> bounds;   // in the plugin window's pixels; controls include their caption
    juce::String param;            // "<moduleId>.<paramId>" for controls
    juce::String label;            // a control's caption, a group's title, or a label's text
    juce::String style;            // knobs: a style name from the layout's styles, or a preset
    juce::Colour colour;           // sliders, switches, labels: the accent (transparent: the default)
    float fontSize = 0.0f;         // labels and group titles (0: the default)
    bool bold = false;
    bool vertical = true;          // sliders
    juce::StringArray options;     // selectors: one per position

    juce::String source;           // meters, lamps, scopes: "voices.out", "plugin.out L", "lfo.position"
    juce::String mode;             // meters: "peak" or "rms"; filters: "lowpass", "highpass", "bandpass" (+ "24")
    std::map<juce::String, juce::String> roles;   // envelope: attack, decay, sustain, release;
                                                  // filter: cutoff, resonance; xy: x, y (parameter ids)
    float threshold = 0.5f;        // lamps: on from this value
    juce::String path;             // shapes: SVG path data, scaled to fit
    juce::Colour stroke;           // shapes: outline colour
    float strokeWidth = 0.0f;

    bool isControl() const noexcept
    {
        return type == Type::knob || type == Type::slider || type == Type::toggle || type == Type::selector;
    }

    bool isLive() const noexcept   { return type == Type::meter || type == Type::scope || type == Type::lamp; }

    /** The roles a type's parameters play, for the ones bound by role. */
    static juce::StringArray rolesOf (Type t)
    {
        if (t == Type::envelope) return { "attack", "decay", "sustain", "release" };
        if (t == Type::filter)   return { "cutoff", "resonance" };
        if (t == Type::xy)       return { "x", "y" };
        return {};
    }
};

//==============================================================================
/**
    The plugin's GUI as data, kept in the project as gui/layout.json. Stella AI drafts it,
    the user reshapes it in Design mode, and the studio draws it: knobs come from
    KnobMaker's renderer (KnobStyle), so they look the same in the Knob Studio, on the
    canvas and, later, in the exported plugin.
*/
class GuiLayout
{
public:
    int width = 720, height = 420;
    juce::Colour backgroundTop { 0xff2b2b30 }, backgroundBottom { 0xff17171a };
    std::map<juce::String, KnobStyle> styles;
    std::vector<GuiWidget> widgets;

    static constexpr const char* fileName = "layout.json";   // in the project's gui folder
    static constexpr int captionHeight = 20;                 // the caption under a control

    static GuiLayout fromVar (const juce::var& json);
    juce::var toVar() const;

    static juce::Result load (const juce::File& file, GuiLayout& result);
    juce::Result save (const juce::File& file) const;

    /** What a first GUI is made from: one group per module, a control per parameter. */
    struct ParamInfo
    {
        juce::String id, module, name, unit;
        float min = 0.0f, max = 1.0f, def = 0.0f;
    };

    static GuiLayout makeDefault (const juce::Array<ParamInfo>& params, const juce::String& title);

    /** A style by name: the layout's own, or one of KnobMaker's presets ("cream", "black",
        "metal"). Unknown names get the default. */
    KnobStyle styleFor (const juce::String& name) const;

    /** KnobStyle as JSON: every field by name, colours as "#AARRGGBB". Reading starts from
        the "preset" field (cream, black or metal) and applies the fields given. */
    static juce::var styleToVar (const KnobStyle& style);
    static KnobStyle styleFromVar (const juce::var& json);

    static juce::String typeName (GuiWidget::Type type);
    static GuiWidget::Type typeFromName (const juce::String& name);
    static juce::String colourToString (juce::Colour colour);
    static juce::Colour colourFromString (const juce::String& text, juce::Colour fallback);
};
