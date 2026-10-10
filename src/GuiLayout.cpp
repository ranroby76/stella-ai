// C:\workspace\Stella AI Studio\src\GuiLayout.cpp

#include "GuiLayout.h"
#include "stella_keys.h"

namespace
{
    juce::var object()
    {
        return juce::var (new juce::DynamicObject());
    }

    void set (juce::var& target, const juce::Identifier& key, const juce::var& value)
    {
        if (auto* o = target.getDynamicObject())
            o->setProperty (key, value);
    }

    int intOf (const juce::var& v, const char* key, int fallback)
    {
        return v.hasProperty (key) ? (int) v.getProperty (key, fallback) : fallback;
    }
}

//==============================================================================
juce::String GuiLayout::colourToString (juce::Colour colour)
{
    return "#" + colour.toDisplayString (true);
}

juce::Colour GuiLayout::colourFromString (const juce::String& text, juce::Colour fallback)
{
    auto hex = text.trim().trimCharactersAtStart ("#");

    if (hex.length() == 6)
        hex = "ff" + hex;

    if (hex.length() != 8 || ! hex.containsOnly ("0123456789abcdefABCDEF"))
        return fallback;

    return juce::Colour ((juce::uint32) hex.getHexValue64());
}

juce::String GuiLayout::typeName (GuiWidget::Type type)
{
    switch (type)
    {
        case GuiWidget::Type::knob:      return "knob";
        case GuiWidget::Type::slider:    return "slider";
        case GuiWidget::Type::toggle:    return "switch";
        case GuiWidget::Type::selector:  return "selector";
        case GuiWidget::Type::label:     return "label";
        case GuiWidget::Type::group:     return "group";
        case GuiWidget::Type::meter:     return "meter";
        case GuiWidget::Type::scope:     return "scope";
        case GuiWidget::Type::lamp:      return "lamp";
        case GuiWidget::Type::envelope:  return "envelope";
        case GuiWidget::Type::filter:    return "filter";
        case GuiWidget::Type::xy:        return "xy";
        case GuiWidget::Type::shape:     return "shape";
        case GuiWidget::Type::preset:    return "preset";
        case GuiWidget::Type::image:     return "image";
        case GuiWidget::Type::keyboard:  return "keyboard";
    }

    return "knob";
}

GuiWidget::Type GuiLayout::typeFromName (const juce::String& name)
{
    const auto n = name.trim().toLowerCase();

    if (n == "slider" || n == "fader")               return GuiWidget::Type::slider;
    if (n == "switch" || n == "toggle" || n == "button") return GuiWidget::Type::toggle;
    if (n == "selector" || n == "choice")            return GuiWidget::Type::selector;
    if (n == "label" || n == "text")                 return GuiWidget::Type::label;
    if (n == "group" || n == "section" || n == "frame") return GuiWidget::Type::group;
    if (n == "meter" || n == "vu" || n == "level")   return GuiWidget::Type::meter;
    if (n == "scope" || n == "oscilloscope")         return GuiWidget::Type::scope;
    if (n == "lamp" || n == "led" || n == "light")   return GuiWidget::Type::lamp;
    if (n == "envelope" || n == "adsr")              return GuiWidget::Type::envelope;
    if (n == "filter" || n == "curve")               return GuiWidget::Type::filter;
    if (n == "xy" || n == "pad" || n == "xypad")     return GuiWidget::Type::xy;
    if (n == "shape" || n == "path" || n == "drawing") return GuiWidget::Type::shape;
    if (n == "preset" || n == "presets" || n == "program") return GuiWidget::Type::preset;
    if (n == "image" || n == "picture" || n == "bitmap" || n == "photo") return GuiWidget::Type::image;
    if (n == "keyboard" || n == "keys" || n == "piano" || n == "pianokeyboard" || n == "virtualkeyboard" || n == "midikeyboard")
        return GuiWidget::Type::keyboard;
    return GuiWidget::Type::knob;
}

juce::String GuiLayout::noteName (int note)
{
    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    note = juce::jlimit (0, 127, note);
    return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
}

int GuiLayout::noteFromVar (const juce::var& value, int fallback)
{
    if (value.isInt() || value.isInt64() || value.isDouble())
        return juce::jlimit (0, 127, juce::roundToInt ((double) value));

    const auto text = value.toString().trim().toUpperCase().replace (juce::String::fromUTF8 ("\xe2\x99\xaf"), "#")
                                                            .replace (juce::String::fromUTF8 ("\xe2\x99\xad"), "B");

    if (text.isEmpty())
        return fallback;

    if (text.containsOnly ("0123456789"))
        return juce::jlimit (0, 127, text.getIntValue());

    // A letter, then sharps or flats, then the octave (C4 = 60; "C-1" = 0).
    static const int letters[] { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const auto letter = text[0];

    if (letter < 'A' || letter > 'G')
        return fallback;

    int note = letters[letter - 'A'];
    int i = 1;

    for (; i < text.length(); ++i)
    {
        if (text[i] == '#')       ++note;
        else if (text[i] == 'B')  --note;
        else                      break;
    }

    const auto octave = text.substring (i);

    if (octave.isEmpty() || ! octave.trimCharactersAtStart ("-").containsOnly ("0123456789"))
        return fallback;

    return juce::jlimit (0, 127, note + (octave.getIntValue() + 1) * 12);
}

//==============================================================================
GuiLayout GuiLayout::fromVar (const juce::var& json)
{
    GuiLayout layout;

    layout.width = juce::jlimit (minWidth, maxWidth, intOf (json, "width", layout.width));
    layout.height = juce::jlimit (minHeight, maxHeight, intOf (json, "height", layout.height));

    const auto background = json.getProperty ("background", {});
    layout.backgroundTop = colourFromString (background.getProperty ("top", {}).toString(), layout.backgroundTop);
    layout.backgroundBottom = colourFromString (background.getProperty ("bottom", {}).toString(), layout.backgroundBottom);
    layout.backgroundImage = background.getProperty ("image", {}).toString().trim();

    if (const auto mode = background.getProperty ("mode", {}).toString().trim().toLowerCase(); pictureModes().contains (mode))
        layout.backgroundMode = mode;

    if (const auto* list = json.getProperty ("widgets", {}).getArray())
        for (const auto& item : *list)
            layout.widgets.push_back (widgetFromVar (item));

    // A background picture is a picture element covering the window, at the back (layouts
    // from before, or Stella AI's "background": { "image" }, become one), so it can be
    // stretched and layered like any other. One that's there already takes the new picture.
    if (layout.backgroundImage.isNotEmpty())
    {
        const juce::Rectangle<int> window (0, 0, layout.width, layout.height);
        auto& widgets = layout.widgets;

        if (! widgets.empty() && widgets.front().type == GuiWidget::Type::image && widgets.front().bounds.contains (window))
        {
            widgets.front().image = layout.backgroundImage;
            widgets.front().mode = layout.backgroundMode;
        }
        else
        {
            GuiWidget backdrop;
            backdrop.type = GuiWidget::Type::image;
            backdrop.image = layout.backgroundImage;
            backdrop.mode = layout.backgroundMode;
            backdrop.bounds = window;
            widgets.insert (widgets.begin(), backdrop);
        }

        layout.backgroundImage.clear();
    }

    return layout;
}

juce::String GuiLayout::backgroundPicture() const
{
    for (const auto& w : widgets)
        if (w.type == GuiWidget::Type::image && w.image.isNotEmpty() && w.bounds.contains (juce::Rectangle<int> (0, 0, width, height)))
            return w.image;

    return {};
}

juce::var GuiLayout::toVar() const
{
    auto json = object();
    set (json, "format", 1);
    set (json, "width", width);
    set (json, "height", height);

    auto background = object();
    set (background, "top", colourToString (backgroundTop));
    set (background, "bottom", colourToString (backgroundBottom));

    if (backgroundImage.isNotEmpty())
    {
        set (background, "image", backgroundImage);
        set (background, "mode", backgroundMode);
    }

    set (json, "background", background);

    juce::Array<juce::var> list;

    for (const auto& w : widgets)
        list.add (widgetToVar (w));

    set (json, "widgets", list);
    return json;
}

GuiWidget GuiLayout::widgetFromVar (const juce::var& item)
{
    GuiWidget w;
    w.type = typeFromName (item.getProperty ("type", {}).toString());
    w.param = item.getProperty ("param", {}).toString().trim();
    w.label = item.getProperty (w.type == GuiWidget::Type::label || w.type == GuiWidget::Type::group ? "text" : "label", {}).toString();

    if (w.label.isEmpty())
        w.label = item.getProperty ("label", item.getProperty ("text", {})).toString();

    w.style = item.getProperty ("style", item.getProperty ("look", {})).toString().trim();
    w.colour = colourFromString (item.getProperty ("color", item.getProperty ("colour", {})).toString(), juce::Colours::transparentBlack);
    w.fontSize = (float) (double) item.getProperty ("size", 0.0);
    w.bold = (bool) item.getProperty ("bold", false);
    w.vertical = item.getProperty ("orientation", "vertical").toString() != "horizontal";

    if (const auto* options = item.getProperty ("options", {}).getArray())
        for (const auto& option : *options)
            w.options.add (option.toString());

    w.source = item.getProperty ("source", {}).toString().trim();
    w.mode = item.getProperty ("mode", {}).toString().trim();
    w.threshold = (float) (double) item.getProperty ("threshold", 0.5);
    w.path = item.getProperty ("path", {}).toString();
    w.stroke = colourFromString (item.getProperty ("stroke", {}).toString(), juce::Colours::transparentBlack);
    w.strokeWidth = (float) (double) item.getProperty ("strokeWidth", 0.0);
    w.image = item.getProperty ("image", {}).toString().trim();

    if (auto* roles = item.getProperty ("params", {}).getDynamicObject())
        for (const auto& role : roles->getProperties())
            w.roles[role.name.toString()] = role.value.toString();

    if (w.type == GuiWidget::Type::keyboard)
    {
        w.lowNote = noteFromVar (item.getProperty ("low", item.getProperty ("from", {})), w.lowNote);
        w.highNote = noteFromVar (item.getProperty ("high", item.getProperty ("to", {})), w.highNote);
        stella::keys::normalise (w.lowNote, w.highNote);
    }

    const auto x = intOf (item, "x", 0), y = intOf (item, "y", 0);

    if (w.type == GuiWidget::Type::knob)
    {
        // A knob is given by its diameter; its caption sits under it.
        const auto size = juce::jlimit (16, 400, intOf (item, "size", intOf (item, "w", 56)));
        w.bounds = { x, y, size, size + captionHeight };
        w.fontSize = 0.0f;
    }
    else
    {
        if (w.type != GuiWidget::Type::label && w.type != GuiWidget::Type::group)
            w.fontSize = 0.0f;

        const auto t = w.type;
        const auto defaultW = t == GuiWidget::Type::slider ? 32 : t == GuiWidget::Type::toggle ? 48 : t == GuiWidget::Type::meter ? 18
                            : t == GuiWidget::Type::lamp ? 30 : t == GuiWidget::Type::xy ? 160 : t == GuiWidget::Type::label ? 160
                            : t == GuiWidget::Type::image ? 160 : t == GuiWidget::Type::keyboard ? 600 : 200;
        const auto defaultH = t == GuiWidget::Type::slider ? 140 : t == GuiWidget::Type::toggle ? 48 : t == GuiWidget::Type::meter ? 140
                            : t == GuiWidget::Type::lamp ? 44 : t == GuiWidget::Type::xy ? 180 : t == GuiWidget::Type::label ? 28
                            : t == GuiWidget::Type::preset ? 28 : t == GuiWidget::Type::group ? 160 : t == GuiWidget::Type::keyboard ? 90 : 120;
        w.bounds = { x, y, juce::jlimit (8, maxWidth, intOf (item, "w", defaultW)), juce::jlimit (8, maxHeight, intOf (item, "h", defaultH)) };
    }

    return w;
}

juce::var GuiLayout::widgetToVar (const GuiWidget& w)
{
    auto item = object();
    set (item, "type", typeName (w.type));
    set (item, "x", w.bounds.getX());
    set (item, "y", w.bounds.getY());

    if (w.type == GuiWidget::Type::knob)
    {
        set (item, "size", w.bounds.getWidth());
    }
    else
    {
        set (item, "w", w.bounds.getWidth());
        set (item, "h", w.bounds.getHeight());
    }

    if (w.param.isNotEmpty())
        set (item, "param", w.param);

    if (w.label.isNotEmpty())
        set (item, w.type == GuiWidget::Type::label || w.type == GuiWidget::Type::group ? "text" : "label", w.label);

    if (w.style.isNotEmpty())
        set (item, "style", w.style);

    if (! w.colour.isTransparent())
        set (item, "color", colourToString (w.colour));

    if (w.fontSize > 0.0f && (w.type == GuiWidget::Type::label || w.type == GuiWidget::Type::group))
        set (item, "size", w.fontSize);

    if (w.bold)
        set (item, "bold", true);

    if (w.type == GuiWidget::Type::slider && ! w.vertical)
        set (item, "orientation", "horizontal");

    if (w.type == GuiWidget::Type::selector)
    {
        juce::Array<juce::var> options;

        for (const auto& o : w.options)
            options.add (o);

        set (item, "options", options);
    }

    if (w.source.isNotEmpty())       set (item, "source", w.source);
    if (w.mode.isNotEmpty())         set (item, "mode", w.mode);
    if (w.type == GuiWidget::Type::lamp) set (item, "threshold", w.threshold);
    if (w.path.isNotEmpty())         set (item, "path", w.path);
    if (! w.stroke.isTransparent())  set (item, "stroke", colourToString (w.stroke));
    if (w.strokeWidth > 0.0f)        set (item, "strokeWidth", w.strokeWidth);
    if (w.image.isNotEmpty())        set (item, "image", w.image);

    if (w.type == GuiWidget::Type::keyboard)
    {
        set (item, "low", noteName (w.lowNote));
        set (item, "high", noteName (w.highNote));
    }

    if (! w.roles.empty())
    {
        auto roles = object();

        for (const auto& [role, id] : w.roles)
            set (roles, role, id);

        set (item, "params", roles);
    }

    return item;
}

//==============================================================================
bool GuiLayout::isPicture (const juce::File& file)
{
    return file.existsAsFile() && file.hasFileExtension (pictureFiles().removeCharacters ("*"));
}

juce::String GuiLayout::importPicture (const juce::File& picture, const juce::File& imagesFolderFile)
{
    if (! isPicture (picture) || ! imagesFolderFile.createDirectory())
        return {};

    // Already in the folder: nothing to copy.
    if (picture.getParentDirectory() == imagesFolderFile)
        return picture.getFileName();

    const auto base = juce::File::createLegalFileName (picture.getFileNameWithoutExtension()).trim();
    const auto extension = picture.getFileExtension().toLowerCase();

    for (int n = 1; n < 1000; ++n)
    {
        const auto name = (base.isNotEmpty() ? base : juce::String ("picture")) + (n > 1 ? " " + juce::String (n) : juce::String()) + extension;
        const auto target = imagesFolderFile.getChildFile (name);

        if (target.existsAsFile())
        {
            if (target.getSize() == picture.getSize() && target.hasIdenticalContentTo (picture))
                return name;   // the same picture again

            continue;
        }

        return picture.copyFileTo (target) ? name : juce::String();
    }

    return {};
}

juce::Result GuiLayout::load (const juce::File& file, GuiLayout& result)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("No layout yet");

    juce::var json;

    if (const auto parsed = juce::JSON::parse (file.loadFileAsString(), json); parsed.failed() || ! json.isObject())
        return juce::Result::fail ("gui/layout.json isn't valid JSON");

    result = fromVar (json);
    return juce::Result::ok();
}

juce::Result GuiLayout::save (const juce::File& file) const
{
    file.getParentDirectory().createDirectory();

    juce::TemporaryFile temp (file);

    if (! temp.getFile().replaceWithText (juce::JSON::toString (toVar())) || ! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Couldn't save " + file.getFullPathName());

    return juce::Result::ok();
}

//==============================================================================
GuiLayout GuiLayout::makeDefault (const juce::Array<ParamInfo>& params, const juce::String& title)
{
    // One titled group per module, its controls in rows of up to four: plain, tidy, and a
    // starting point for Stella AI or the user to reshape. The controls take the default looks.
    GuiLayout layout;

    constexpr int margin = 20, knob = 52, cell = 76, rowH = knob + captionHeight + 18, perRow = 4, titleH = 44;
    constexpr int maxWidth = 1100;

    GuiWidget heading;
    heading.type = GuiWidget::Type::label;
    heading.label = title.toUpperCase();
    heading.fontSize = 22.0f;
    heading.bold = true;
    heading.bounds = { margin, 12, 600, 30 };
    layout.widgets.push_back (heading);

    juce::StringArray modules;

    for (const auto& p : params)
        modules.addIfNotAlreadyThere (p.module);

    int x = margin, y = titleH + 8, rowBottom = y, right = 0;

    for (const auto& module : modules)
    {
        juce::Array<ParamInfo> mine;

        for (const auto& p : params)
            if (p.module == module)
                mine.add (p);

        const auto columns = juce::jmin (perRow, mine.size());
        const auto rows = (mine.size() + perRow - 1) / perRow;
        const auto groupW = columns * cell + 16;
        const auto groupH = rows * rowH + 34;

        if (x + groupW > maxWidth && x > margin)
        {
            x = margin;
            y = rowBottom + 16;
        }

        GuiWidget group;
        group.type = GuiWidget::Type::group;
        group.label = module.toUpperCase();
        group.bounds = { x, y, groupW, groupH };
        layout.widgets.push_back (group);

        for (int i = 0; i < mine.size(); ++i)
        {
            const auto& p = mine.getReference (i);
            const auto cx = x + 8 + (i % perRow) * cell;
            const auto cy = y + 28 + (i / perRow) * rowH;

            GuiWidget w;
            w.param = p.id;
            w.label = p.name;

            // An on/off parameter (0..1, starting at 0 or 1, no unit) gets a switch.
            auto near = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };
            const bool isSwitch = near (p.min, 0.0f) && near (p.max, 1.0f) && p.unit.isEmpty() && (near (p.def, 0.0f) || near (p.def, 1.0f));

            if (isSwitch)
            {
                w.type = GuiWidget::Type::toggle;
                w.bounds = { cx + (cell - 48) / 2, cy + 6, 48, knob - 6 + captionHeight };
            }
            else
            {
                w.type = GuiWidget::Type::knob;
                w.bounds = { cx + (cell - knob) / 2, cy, knob, knob + captionHeight };
            }

            layout.widgets.push_back (w);
        }

        x += groupW + 14;
        right = juce::jmax (right, x);
        rowBottom = juce::jmax (rowBottom, y + groupH);
    }

    layout.width = juce::jlimit (420, 1200, right - 14 + margin);
    layout.height = juce::jlimit (240, 800, rowBottom + margin);
    return layout;
}
