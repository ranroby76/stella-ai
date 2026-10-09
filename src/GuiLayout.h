// C:\workspace\Stella AI Studio\src\GuiLayout.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <vector>

//==============================================================================
/** One element of the plugin's GUI. Controls are bound to a parameter by its id; meters,
    lamps and scopes watch a source (a signal or a module's display); pictures show a file
    from the project's gui/images folder; a keyboard plays notes into the plugin. */
struct GuiWidget
{
    enum class Type { knob, slider, toggle, selector, label, group,
                      meter, scope, lamp, envelope, filter, xy, shape, preset, image, keyboard };

    Type type = Type::knob;
    juce::Rectangle<int> bounds;   // in the plugin window's pixels; controls include their caption
    juce::String param;            // "<moduleId>.<paramId>" for controls
    juce::String label;            // a control's caption, a group's title, or a label's text
    juce::String style;            // knobs, sliders, switches: the name of a look (see Looks)
    juce::Colour colour;           // labels, frames, selectors and the like: their colour (transparent: the default)
    float fontSize = 0.0f;         // labels and group titles (0: the default)
    bool bold = false;
    bool vertical = true;          // sliders
    juce::StringArray options;     // selectors: one per position

    juce::String source;           // meters, lamps, scopes: "voices.out", "plugin.out L", "lfo.position"
    juce::String mode;             // meters: "peak" or "rms"; filters: "lowpass", "highpass", "bandpass" (+ "24");
                                   // pictures: how they fill their box (see GuiLayout::pictureModes)
    juce::String image;            // pictures: a file name in the project's gui/images folder
    std::map<juce::String, juce::String> roles;   // envelope: attack, decay, sustain, release;
                                                  // filter: cutoff, resonance; xy: x, y (parameter ids)
    float threshold = 0.5f;        // lamps: on from this value
    juce::String path;             // shapes: SVG path data, scaled to fit
    juce::Colour stroke;           // shapes: outline colour
    float strokeWidth = 0.0f;
    int lowNote = 48, highNote = 84;   // keyboards: the lowest and highest key (MIDI notes; C3 to C6),
                                       // white keys both (see stella::keys::normalise)

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
    the user reshapes it in the Edit UI tab, and the studio draws it. Knobs, sliders and
    switches name a look: KnobMaker layer documents, made in the Knob Studio (see Looks),
    so they look the same there, on the canvas and in the exported plugin. Pictures (a
    background, or picture elements) are files in the project's gui/images folder.
*/
class GuiLayout
{
public:
    int width = 720, height = 420;
    juce::Colour backgroundTop { 0xff2b2b30 }, backgroundBottom { 0xff17171a };
    juce::String backgroundImage;              // a file in gui/images, over the colours (empty: none)
    juce::String backgroundMode { "fill" };    // how it covers the window (see pictureModes)
    std::vector<GuiWidget> widgets;

    static constexpr const char* fileName = "layout.json";   // in the project's gui folder
    static constexpr const char* imagesFolder = "images";    // the pictures, in the gui folder
    static constexpr int captionHeight = 20;                 // the caption under a control
    static constexpr int minWidth = 240, maxWidth = 2400, minHeight = 160, maxHeight = 1600;

    /** How a picture fills its box: "fill" (covers it, cropping the edges), "fit" (whole,
        keeping its shape), "stretch", "centre" (its own size) or "tile". With names for menus. */
    static juce::StringArray pictureModes()       { return { "fill", "fit", "stretch", "centre", "tile" }; }
    static juce::StringArray pictureModeNames()   { return { "Fill (crop the edges)", "Fit (whole picture)", "Stretch", "Centre (own size)", "Tile" }; }

    /** The picture types the studio reads. */
    static juce::String pictureFiles()            { return "*.png;*.jpg;*.jpeg;*.gif"; }
    static bool isPicture (const juce::File& file);

    /** Copies a picture into the images folder (named after it; a different picture with
        that name gets a number) and returns its file name there, or empty if it failed.
        The same picture again reuses its copy. */
    static juce::String importPicture (const juce::File& picture, const juce::File& imagesFolderFile);

    static GuiLayout fromVar (const juce::var& json);
    juce::var toVar() const;

    /** One element as JSON (the same fields as in layout.json), and back. */
    static juce::var widgetToVar (const GuiWidget& widget);
    static GuiWidget widgetFromVar (const juce::var& json);

    static juce::Result load (const juce::File& file, GuiLayout& result);
    juce::Result save (const juce::File& file) const;

    /** What a first GUI is made from: one group per module, a control per parameter. */
    struct ParamInfo
    {
        juce::String id, module, name, unit;
        float min = 0.0f, max = 1.0f, def = 0.0f;
    };

    static GuiLayout makeDefault (const juce::Array<ParamInfo>& params, const juce::String& title);

    static juce::String typeName (GuiWidget::Type type);
    static GuiWidget::Type typeFromName (const juce::String& name);

    /** Keyboards: a MIDI note's name ("C4" is middle C, 60), and a note from JSON: a name
        ("F#2", "Bb3") or a number. */
    static juce::String noteName (int note);
    static int noteFromVar (const juce::var& value, int fallback);
    static juce::String colourToString (juce::Colour colour);
    static juce::Colour colourFromString (const juce::String& text, juce::Colour fallback);
};
