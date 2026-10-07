// C:\workspace\Stella AI Studio\src\PluginCanvas.cpp

#include "PluginCanvas.h"
#include "StellaLookAndFeel.h"

namespace
{
    constexpr int snap = 4;                 // design grid, in plugin pixels
    constexpr float handleSize = 10.0f;     // the resize handle, in view pixels
    constexpr int scopeSamples = 2048;
    const juce::Colour defaultAccent { 0xffe5484d };
    const juce::Colour captionColour { 0xffd8d4ca };

    int snapped (float v)    { return juce::roundToInt (v / (float) snap) * snap; }

    bool isInteractive (const GuiWidget& w)  { return w.isControl() || w.type == GuiWidget::Type::xy || w.type == GuiWidget::Type::preset; }

    const juce::StringArray typeNames { "Knob", "Slider", "Switch", "Selector", "Label", "Group",
                                        "Meter", "Scope", "Lamp", "Envelope", "Filter curve", "XY pad", "Shape", "Presets" };
    const GuiWidget::Type typeOrder[] { GuiWidget::Type::knob, GuiWidget::Type::slider, GuiWidget::Type::toggle,
                                        GuiWidget::Type::selector, GuiWidget::Type::label, GuiWidget::Type::group,
                                        GuiWidget::Type::meter, GuiWidget::Type::scope, GuiWidget::Type::lamp,
                                        GuiWidget::Type::envelope, GuiWidget::Type::filter, GuiWidget::Type::xy, GuiWidget::Type::shape,
                                        GuiWidget::Type::preset };
    constexpr int numTypes = 14;

    const juce::StringArray meterModes { "peak", "rms" };
    const juce::StringArray meterModeNames { "Peak", "RMS" };
    const juce::StringArray filterModes { "lowpass", "lowpass24", "highpass", "highpass24", "bandpass" };
    const juce::StringArray filterModeNames { "Low-pass", "Low-pass 24 dB", "High-pass", "High-pass 24 dB", "Band-pass" };

    const char* defaultStar = "M50,5 L61,39 L97,39 L68,61 L79,95 L50,74 L21,95 L32,61 L3,39 L39,39 Z";

    juce::Rectangle<int> defaultBounds (GuiWidget::Type type, int x, int y)
    {
        switch (type)
        {
            case GuiWidget::Type::knob:      return { x, y, 52, 52 + GuiLayout::captionHeight };
            case GuiWidget::Type::slider:    return { x, y, 32, 140 };
            case GuiWidget::Type::toggle:    return { x, y, 48, 66 };
            case GuiWidget::Type::selector:  return { x, y, 180, 48 };
            case GuiWidget::Type::label:     return { x, y, 200, 28 };
            case GuiWidget::Type::group:     return { x, y, 240, 160 };
            case GuiWidget::Type::meter:     return { x, y, 18, 140 };
            case GuiWidget::Type::scope:     return { x, y, 200, 110 };
            case GuiWidget::Type::lamp:      return { x, y, 30, 44 };
            case GuiWidget::Type::envelope:  return { x, y, 200, 110 };
            case GuiWidget::Type::filter:    return { x, y, 200, 110 };
            case GuiWidget::Type::xy:        return { x, y, 160, 180 };
            case GuiWidget::Type::shape:     return { x, y, 60, 60 };
            case GuiWidget::Type::preset:    return { x, y, 200, 28 };
        }

        return { x, y, 60, 60 };
    }
}

//==============================================================================
PluginCanvas::Inspector::Inspector (PluginCanvas& owner)
    : canvas (owner)
{
    type.addItemList (typeNames, 1);

    for (auto* box : { &type, &param, &source, &mode, &style })
    {
        box->onChange = [this] { apply(); };
        addAndMakeVisible (box);
    }

    for (int i = 0; i < 4; ++i)
    {
        roles[i].onChange = [this] { apply(); };
        addAndMakeVisible (roles[i]);
        roleCaptions[i].setColour (juce::Label::textColourId, Theme::muted);
        roleCaptions[i].setFont (Theme::font (12.5f));
        addAndMakeVisible (roleCaptions[i]);
    }

    for (auto* editor : { &label, &options })
    {
        editor->setFont (Theme::font (14.0f));
        editor->onTextChange = [this] { apply(); };
        addAndMakeVisible (editor);
    }

    options.setTextToShowWhenEmpty ("Saw, Square, Triangle", Theme::muted);

    removeButton.onClick = [this]
    {
        if (juce::isPositiveAndBelow (canvas.selected, (int) canvas.layout.widgets.size()))
        {
            canvas.layout.widgets.erase (canvas.layout.widgets.begin() + canvas.selected);
            canvas.select (-1);
            canvas.edited();
        }
    };

    addAndMakeVisible (removeButton);
}

void PluginCanvas::Inspector::fillParams (juce::ComboBox& box, const juce::String& selectedId, bool withNone)
{
    box.clear (juce::dontSendNotification);

    if (withNone)
        box.addItem ("(none)", 1);

    for (int i = 0; i < canvas.params.size(); ++i)
        box.addItem (canvas.params[i].id + juce::String::fromUTF8 ("  \xc2\xb7  ") + canvas.params[i].name, i + 2);

    box.setSelectedId (1, juce::dontSendNotification);

    for (int i = 0; i < canvas.params.size(); ++i)
        if (canvas.params[i].id == selectedId)
            box.setSelectedId (i + 2, juce::dontSendNotification);
}

void PluginCanvas::Inspector::show (const GuiWidget& widget)
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);

    for (int i = 0; i < numTypes; ++i)
        if (typeOrder[i] == widget.type)
            type.setSelectedId (i + 1, juce::dontSendNotification);

    fillParams (param, widget.param, true);

    source.clear (juce::dontSendNotification);
    juce::StringArray sources (canvas.signalSources);
    sources.addArray (canvas.displaySources);

    if (widget.source.isNotEmpty())
        sources.addIfNotAlreadyThere (widget.source);

    source.addItemList (sources, 1);
    source.setSelectedId (sources.indexOf (widget.source) + 1, juce::dontSendNotification);

    roleNames = GuiWidget::rolesOf (widget.type);

    for (int i = 0; i < 4; ++i)
    {
        const bool used = i < roleNames.size();
        roles[i].setVisible (used);
        roleCaptions[i].setVisible (used);

        if (used)
        {
            const auto found = widget.roles.find (roleNames[i]);
            fillParams (roles[i], found != widget.roles.end() ? found->second : juce::String(), true);
            roleCaptions[i].setText (roleNames[i].substring (0, 1).toUpperCase() + roleNames[i].substring (1), juce::dontSendNotification);
        }
    }

    mode.clear (juce::dontSendNotification);

    if (widget.type == GuiWidget::Type::meter)
    {
        mode.addItemList (meterModeNames, 1);
        mode.setSelectedId (juce::jmax (0, meterModes.indexOf (widget.mode)) + 1, juce::dontSendNotification);
    }
    else if (widget.type == GuiWidget::Type::filter)
    {
        mode.addItemList (filterModeNames, 1);
        mode.setSelectedId (juce::jmax (0, filterModes.indexOf (widget.mode)) + 1, juce::dontSendNotification);
    }

    style.clear (juce::dontSendNotification);
    juce::StringArray styleNames;

    for (const auto& entry : canvas.layout.styles)
        styleNames.add (entry.first);

    for (const auto& preset : { "cream", "black", "metal" })
        styleNames.addIfNotAlreadyThere (preset);

    style.addItemList (styleNames, 1);
    style.setSelectedId (juce::jmax (0, styleNames.indexOf (widget.style)) + 1, juce::dontSendNotification);

    label.setText (widget.label, juce::dontSendNotification);
    options.setText (widget.options.joinIntoString (", "), juce::dontSendNotification);

    param.setVisible (widget.isControl());
    source.setVisible (widget.isLive());
    mode.setVisible (widget.type == GuiWidget::Type::meter || widget.type == GuiWidget::Type::filter);
    style.setVisible (widget.type == GuiWidget::Type::knob);
    options.setVisible (widget.type == GuiWidget::Type::selector);
    resized();
    repaint();
}

void PluginCanvas::Inspector::apply()
{
    if (updating || ! juce::isPositiveAndBelow (canvas.selected, (int) canvas.layout.widgets.size()))
        return;

    auto& w = canvas.layout.widgets[(size_t) canvas.selected];
    const auto newType = typeOrder[juce::jlimit (0, numTypes - 1, type.getSelectedId() - 1)];

    if (newType != w.type)
    {
        w.bounds = defaultBounds (newType, w.bounds.getX(), w.bounds.getY());
        w.type = newType;
        w.roles.clear();
        w.mode.clear();

        if (newType == GuiWidget::Type::shape && w.path.isEmpty())
            w.path = defaultStar;

        show (w);
        canvas.edited();
        return;
    }

    auto paramAt = [this] (const juce::ComboBox& box)
    {
        const auto index = box.getSelectedId() - 2;
        return juce::isPositiveAndBelow (index, canvas.params.size()) ? canvas.params[index].id : juce::String();
    };

    w.param = w.isControl() ? paramAt (param) : juce::String();
    w.source = w.isLive() ? source.getText() : juce::String();

    for (int i = 0; i < roleNames.size(); ++i)
    {
        const auto id = paramAt (roles[i]);

        if (id.isNotEmpty()) w.roles[roleNames[i]] = id;
        else                 w.roles.erase (roleNames[i]);
    }

    if (w.type == GuiWidget::Type::meter)
        w.mode = meterModes[juce::jmax (0, mode.getSelectedId() - 1)];
    else if (w.type == GuiWidget::Type::filter)
        w.mode = filterModes[juce::jmax (0, mode.getSelectedId() - 1)];

    w.label = label.getText();
    w.style = w.type == GuiWidget::Type::knob ? style.getText() : juce::String();
    w.options = juce::StringArray::fromTokens (options.getText(), ",", "");
    w.options.trim();
    w.options.removeEmptyStrings();

    canvas.edited();
}

void PluginCanvas::Inspector::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();
    g.setColour (Theme::panel.withAlpha (0.97f));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (Theme::muted);
    g.setFont (Theme::font (12.5f));

    const std::pair<const juce::Component*, const char*> rows[] {
        { &type, "Type" }, { &param, "Parameter" }, { &source, "Watches" }, { &mode, "Mode" },
        { &label, "Label / text" }, { &style, "Knob look" }, { &options, "Positions (comma separated)" } };

    for (const auto& [component, caption] : rows)
        if (component->isVisible())
            g.drawText (caption, component->getBounds().translated (0, -17).withHeight (16), juce::Justification::centredLeft, false);
}

void PluginCanvas::Inspector::resized()
{
    auto area = getLocalBounds().reduced (12, 10);

    auto row = [&area] (juce::Component& c)
    {
        if (! c.isVisible())
            return;

        area.removeFromTop (17);
        c.setBounds (area.removeFromTop (26));
        area.removeFromTop (7);
    };

    row (type);
    row (param);
    row (source);

    for (int i = 0; i < 4; ++i)
    {
        if (! roles[i].isVisible())
            continue;

        roleCaptions[i].setBounds (area.removeFromTop (17));
        roles[i].setBounds (area.removeFromTop (26));
        area.removeFromTop (7);
    }

    row (mode);
    row (label);
    row (style);
    row (options);

    removeButton.setBounds (area.removeFromTop (28).removeFromLeft (90));
}

//==============================================================================
PluginCanvas::PluginCanvas()
{
    setWantsKeyboardFocus (true);
    addChildComponent (inspector);
    scopeBuffer.resize ((size_t) scopeSamples, 0.0f);
    startTimerHz (30);
}

PluginCanvas::~PluginCanvas()
{
    stopTimer();
}

void PluginCanvas::setLayout (const GuiLayout& newLayout)
{
    layout = newLayout;
    renderers.clear();
    meterLevels.clear();

    if (! juce::isPositiveAndBelow (selected, (int) layout.widgets.size()))
        selected = -1;

    select (selected);
    reportSources();
    resized();
    repaint();
}

void PluginCanvas::setDesignMode (bool shouldDesign)
{
    design = shouldDesign;
    select (design ? selected : -1);
    repaint();
}

void PluginCanvas::setParameters (const juce::Array<Param>& newParams)
{
    params = newParams;
    values.clear();

    for (const auto& p : params)
        values[p.index] = p.value;

    select (selected);
    repaint();
}

void PluginCanvas::setSources (const juce::StringArray& signals, const juce::StringArray& displays)
{
    signalSources = signals;
    displaySources = displays;
    select (selected);
}

void PluginCanvas::edited()
{
    renderers.clear();   // a style may have changed
    reportSources();

    if (onLayoutEdited != nullptr)
        onLayoutEdited();

    repaint();
}

void PluginCanvas::reportSources()
{
    // Tell the live preview which signals the GUI watches, when that changes.
    juce::StringArray levels, scopes;

    for (const auto& w : layout.widgets)
    {
        if ((w.type == GuiWidget::Type::meter || w.type == GuiWidget::Type::lamp) && w.source.isNotEmpty())
            levels.addIfNotAlreadyThere (w.source);
        else if (w.type == GuiWidget::Type::scope && w.source.isNotEmpty() && scopes.size() < 3)
            scopes.addIfNotAlreadyThere (w.source);
    }

    if (levels != reportedLevels || scopes != reportedScopes)
    {
        reportedLevels = levels;
        reportedScopes = scopes;

        if (onSourcesChanged != nullptr)
            onSourcesChanged (levels, scopes);
    }
}

void PluginCanvas::select (int index)
{
    selected = design && juce::isPositiveAndBelow (index, (int) layout.widgets.size()) ? index : -1;

    if (selected >= 0)
        inspector.show (layout.widgets[(size_t) selected]);

    inspector.setVisible (selected >= 0);
    repaint();
}

void PluginCanvas::addWidget (GuiWidget::Type type)
{
    auto unusedParam = [this] (const juce::StringArray& avoid)
    {
        for (const auto& p : params)
        {
            bool shown = avoid.contains (p.id);

            for (const auto& w : layout.widgets)
                shown = shown || w.param == p.id;

            if (! shown)
                return p.id;
        }

        return juce::String();
    };

    auto paramNamed = [this] (const juce::String& word)
    {
        for (const auto& p : params)
            if (p.id.fromFirstOccurrenceOf (".", false, false).containsIgnoreCase (word) || p.name.containsIgnoreCase (word))
                return p.id;

        return juce::String();
    };

    GuiWidget w;
    w.type = type;
    const auto offset = (int) (layout.widgets.size() % 6) * 12;
    w.bounds = defaultBounds (type, 24 + offset, 24 + offset);

    switch (type)
    {
        case GuiWidget::Type::knob:
        case GuiWidget::Type::slider:
        case GuiWidget::Type::toggle:
        case GuiWidget::Type::selector:
            w.param = unusedParam ({});

            for (const auto& p : params)
                if (p.id == w.param)
                    w.label = p.name;

            if (type == GuiWidget::Type::knob)
                w.style = layout.styles.empty() ? "black" : layout.styles.begin()->first;

            if (type == GuiWidget::Type::selector)
                w.options = { "One", "Two", "Three" };
            break;

        case GuiWidget::Type::label:     w.label = "Label"; break;
        case GuiWidget::Type::group:     w.label = "SECTION"; break;
        case GuiWidget::Type::meter:     w.source = "plugin.out L"; w.mode = "peak"; w.label = "Level"; break;
        case GuiWidget::Type::scope:     w.source = "plugin.out L"; w.label = "Scope"; break;
        case GuiWidget::Type::lamp:      w.source = displaySources.isEmpty() ? juce::String ("plugin.out L") : displaySources[0]; w.label = "Lamp"; break;

        case GuiWidget::Type::envelope:
            for (const auto& role : GuiWidget::rolesOf (type))
                if (const auto id = paramNamed (role); id.isNotEmpty())
                    w.roles[role] = id;

            w.label = "Envelope";
            break;

        case GuiWidget::Type::filter:
            if (const auto id = paramNamed ("cutoff"); id.isNotEmpty())    w.roles["cutoff"] = id;
            if (const auto id = paramNamed ("reso"); id.isNotEmpty())      w.roles["resonance"] = id;
            w.mode = "lowpass";
            w.label = "Filter";
            break;

        case GuiWidget::Type::xy:
        {
            const auto x = unusedParam ({});
            const auto y = unusedParam ({ x });
            if (x.isNotEmpty()) w.roles["x"] = x;
            if (y.isNotEmpty()) w.roles["y"] = y;
            w.label = "XY";
            break;
        }

        case GuiWidget::Type::shape:     w.path = defaultStar; break;
        case GuiWidget::Type::preset:    break;
    }

    // Groups go underneath, everything else on top.
    if (type == GuiWidget::Type::group)
    {
        layout.widgets.insert (layout.widgets.begin(), w);
        select (0);
    }
    else
    {
        layout.widgets.push_back (w);
        select ((int) layout.widgets.size() - 1);
    }

    edited();
}

void PluginCanvas::setPresets (const juce::StringArray& names, int current)
{
    presetNames = names;
    currentPreset = current;
    repaint();
}

void PluginCanvas::setKnobStyle (const juce::String& name, const KnobStyle& style)
{
    if (name.isEmpty())
        return;

    layout.styles[name] = style;
    renderers.erase (name);

    if (onLayoutEdited != nullptr)
        onLayoutEdited();

    if (selected >= 0)
        inspector.show (layout.widgets[(size_t) selected]);

    repaint();
}

juce::String PluginCanvas::makeKnobUnique (int widgetIndex)
{
    if (! juce::isPositiveAndBelow (widgetIndex, (int) layout.widgets.size()))
        return {};

    auto& w = layout.widgets[(size_t) widgetIndex];
    const auto base = (w.label.isNotEmpty() ? w.label : juce::String ("knob")).toLowerCase();
    auto name = base;

    for (int i = 2; layout.styles.find (name) != layout.styles.end(); ++i)
        name = base + " " + juce::String (i);

    layout.styles[name] = layout.styleFor (w.style);
    w.style = name;
    edited();
    return name;
}

//==============================================================================
juce::Rectangle<float> PluginCanvas::panelArea() const
{
    // The plugin keeps its own size; it only shrinks to fit. Design mode leaves room for
    // the panel on the right.
    auto available = getLocalBounds().reduced (24).toFloat();

    if (design && inspector.isVisible())
        available.removeFromRight ((float) inspector.getWidth() + 16.0f);

    const auto scale = juce::jmin (1.0f, available.getWidth() / (float) layout.width, available.getHeight() / (float) layout.height);
    return available.withSizeKeepingCentre ((float) layout.width * scale, (float) layout.height * scale);
}

juce::Point<float> PluginCanvas::toPlugin (juce::Point<float> view) const
{
    const auto area = panelArea();
    const auto scale = area.getWidth() / (float) layout.width;
    return (view - area.getTopLeft()) / scale;
}

juce::Rectangle<float> PluginCanvas::toView (juce::Rectangle<int> plugin) const
{
    const auto area = panelArea();
    const auto scale = area.getWidth() / (float) layout.width;
    return plugin.toFloat() * scale + area.getTopLeft();
}

int PluginCanvas::widgetAt (juce::Point<float> plugin, bool interactiveOnly) const
{
    for (int i = (int) layout.widgets.size(); --i >= 0;)
    {
        const auto& w = layout.widgets[(size_t) i];

        if (interactiveOnly && ! isInteractive (w))
            continue;

        if (w.type == GuiWidget::Type::group && ! interactiveOnly)
        {
            // A group is picked by its border and title, so what's inside stays reachable.
            const auto outer = w.bounds.toFloat();

            if (outer.contains (plugin) && ! outer.reduced (8.0f).withTrimmedTop (16.0f).contains (plugin))
                return i;

            continue;
        }

        if (w.bounds.toFloat().contains (plugin))
            return i;
    }

    return -1;
}

const PluginCanvas::Param* PluginCanvas::paramById (const juce::String& id) const
{
    if (id.isEmpty())
        return nullptr;

    for (const auto& p : params)
        if (p.id == id)
            return &p;

    return nullptr;
}

float PluginCanvas::proportionOf (const Param& p, float value) const
{
    const auto t = juce::jlimit (0.0f, 1.0f, (value - p.min) / juce::jmax (1.0e-9f, p.max - p.min));
    return std::abs (p.skew - 1.0f) > 1.0e-4f && p.skew > 0.0f ? std::pow (t, p.skew) : t;
}

float PluginCanvas::valueAt (const Param& p, float proportion) const
{
    auto t = juce::jlimit (0.0f, 1.0f, proportion);

    if (std::abs (p.skew - 1.0f) > 1.0e-4f && p.skew > 0.0f)
        t = std::pow (t, 1.0f / p.skew);

    return p.min + (p.max - p.min) * t;
}

float PluginCanvas::currentValue (const Param& p) const
{
    const auto found = values.find (p.index);
    return found != values.end() ? found->second : p.def;
}

float PluginCanvas::roleProportion (const GuiWidget& w, const juce::String& role, float fallback) const
{
    const auto found = w.roles.find (role);

    if (found == w.roles.end())
        return fallback;

    const auto* p = paramById (found->second);
    return p != nullptr ? proportionOf (*p, currentValue (*p)) : fallback;
}

void PluginCanvas::setValue (const Param& p, float value)
{
    value = juce::jlimit (p.min, p.max, value);
    values[p.index] = value;

    if (onParameterChanged != nullptr)
        onParameterChanged (p.index, value);

    repaint();
}

void PluginCanvas::setXy (const GuiWidget& w, juce::Point<float> plugin)
{
    const auto pad = w.bounds.toFloat().withTrimmedBottom ((float) GuiLayout::captionHeight).reduced (2.0f);

    if (const auto found = w.roles.find ("x"); found != w.roles.end())
        if (const auto* p = paramById (found->second))
            setValue (*p, valueAt (*p, (plugin.x - pad.getX()) / pad.getWidth()));

    if (const auto found = w.roles.find ("y"); found != w.roles.end())
        if (const auto* p = paramById (found->second))
            setValue (*p, valueAt (*p, (pad.getBottom() - plugin.y) / pad.getHeight()));
}

KnobRenderer& PluginCanvas::rendererFor (const juce::String& style)
{
    auto& renderer = renderers[style];

    if (renderer == nullptr)
        renderer = std::make_unique<KnobRenderer> (layout.styleFor (style));

    return *renderer;
}

juce::Rectangle<int> PluginCanvas::knobSquare (const GuiWidget& w) const
{
    const auto size = juce::jmax (8, juce::jmin (w.bounds.getWidth(), w.bounds.getHeight() - GuiLayout::captionHeight));
    return { w.bounds.getX() + (w.bounds.getWidth() - size) / 2, w.bounds.getY(), size, size };
}

//==============================================================================
void PluginCanvas::timerCallback()
{
    bool live = false;

    for (const auto& w : layout.widgets)
        live = live || w.isLive();

    if (! live || ! isShowing() || readLevel == nullptr)
        return;

    // Each source is read once per frame: a peak is "the highest since the last read".
    frameLevels.clear();

    for (int i = 0; i < (int) layout.widgets.size(); ++i)
    {
        const auto& w = layout.widgets[(size_t) i];

        if (w.type != GuiWidget::Type::meter && w.type != GuiWidget::Type::lamp)
            continue;

        const bool rms = w.type == GuiWidget::Type::meter && w.mode == "rms";
        const auto key = w.source + (rms ? "|rms" : "|peak");

        if (frameLevels.find (key) == frameLevels.end())
            frameLevels[key] = readLevel (w.source, rms);

        const auto level = frameLevels[key];
        auto& shown = meterLevels[i];

        // Meters on a signal fall back smoothly; displays and lamps show the value as it is.
        shown = (w.type == GuiWidget::Type::meter && ! displaySources.contains (w.source)) ? juce::jmax (level, shown * 0.86f) : level;
    }

    repaint (panelArea().toNearestInt().expanded (2));
}

void PluginCanvas::drawLive (juce::Graphics& g, const GuiWidget& w, int index, Part part, float override)
{
    const auto b = w.bounds.toFloat();
    const auto accent = w.colour.isTransparent() ? defaultAccent : w.colour;
    const auto found = meterLevels.find (index);
    const auto level = found != meterLevels.end() ? found->second : 0.0f;
    const bool isDisplay = displaySources.contains (w.source);
    const bool still = part != Part::moving, moving = part != Part::still;

    auto caption = [&] (juce::Rectangle<float> area)
    {
        if (! still)
            return;

        g.setColour (captionColour.withAlpha (0.85f));
        g.setFont (Theme::font (12.5f));
        g.drawText (w.label, area, juce::Justification::centred, true);
    };

    if (w.type == GuiWidget::Type::meter)
    {
        auto area = b.withTrimmedBottom (w.label.isNotEmpty() ? (float) GuiLayout::captionHeight : 0.0f);
        const bool vertical = area.getHeight() >= area.getWidth();

        // Signals in dB (-60..+6), displays as they are (0..1).
        const auto fraction = isDisplay ? juce::jlimit (0.0f, 1.0f, level)
                                        : juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (juce::jmax (level, 1.0e-5f)) + 60.0f) / 66.0f);

        if (still)
        {
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (area, 2.0f);
        }

        const auto inner = area.reduced (2.0f);
        const auto bar = vertical ? inner.withTop (inner.getBottom() - inner.getHeight() * fraction)
                                  : inner.withWidth (inner.getWidth() * fraction);

        juce::ColourGradient gradient (juce::Colour (0xff00c853), vertical ? inner.getX() : inner.getX(), vertical ? inner.getBottom() : inner.getY(),
                                       juce::Colour (0xffd50000), vertical ? inner.getX() : inner.getRight(), vertical ? inner.getY() : inner.getY(), false);
        gradient.addColour (isDisplay ? 0.75 : 48.0 / 66.0, juce::Colour (0xff00c853));
        gradient.addColour (isDisplay ? 0.9 : 57.0 / 66.0, juce::Colour (0xffffb300));
        g.setGradientFill (gradient);

        if (moving)
            g.fillRect (bar);

        if (! isDisplay && moving)
        {
            // A tick at 0 dB.
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            const auto zero = 60.0f / 66.0f;

            if (vertical) g.drawHorizontalLine (juce::roundToInt (inner.getBottom() - inner.getHeight() * zero), inner.getX(), inner.getRight());
            else          g.drawVerticalLine (juce::roundToInt (inner.getX() + inner.getWidth() * zero), inner.getY(), inner.getBottom());
        }

        if (w.label.isNotEmpty())
            caption (b.withTop (b.getBottom() - (float) GuiLayout::captionHeight).expanded (14.0f, 0.0f));

        return;
    }

    if (w.type == GuiWidget::Type::lamp)
    {
        auto area = b.withTrimmedBottom (w.label.isNotEmpty() ? (float) GuiLayout::captionHeight : 0.0f);
        const auto size = juce::jmin (area.getWidth(), area.getHeight()) * 0.6f;
        const auto lamp = juce::Rectangle<float> (size, size).withCentre (area.getCentre());
        const auto colour = w.colour.isTransparent() ? juce::Colour (0xffffb020) : w.colour;
        const bool on = override >= 0.0f ? override >= 0.5f : level >= w.threshold;

        if (moving)
        {
            if (on)
            {
                g.setColour (colour.withAlpha (0.35f));
                g.fillEllipse (lamp.expanded (size * 0.35f));
            }

            g.setColour (on ? colour : colour.darker (2.2f).withAlpha (0.9f));
            g.fillEllipse (lamp);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawEllipse (lamp, 1.0f);
        }

        if (w.label.isNotEmpty())
            caption (b.withTop (b.getBottom() - (float) GuiLayout::captionHeight).expanded (14.0f, 0.0f));

        return;
    }

    // Scope: the newest samples, steadied on a rising zero crossing.
    auto area = b.withTrimmedBottom (w.label.isNotEmpty() ? (float) GuiLayout::captionHeight : 0.0f);

    if (still)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX() + 2.0f, area.getRight() - 2.0f);
    }

    if (moving && readScope != nullptr && w.source.isNotEmpty())
    {
        readScope (w.source, scopeBuffer.data(), scopeSamples);

        constexpr int shown = 1024;
        int start = 0;

        for (int i = 1; i < scopeSamples - shown; ++i)
        {
            if (scopeBuffer[(size_t) i - 1] < 0.0f && scopeBuffer[(size_t) i] >= 0.0f)
            {
                start = i;
                break;
            }
        }

        juce::Path trace;
        const auto inner = area.reduced (3.0f);

        for (int i = 0; i < shown; i += 2)
        {
            const auto x = inner.getX() + inner.getWidth() * (float) i / (float) (shown - 1);
            const auto y = inner.getCentreY() - juce::jlimit (-1.0f, 1.0f, scopeBuffer[(size_t) (start + i)]) * inner.getHeight() * 0.5f;

            if (i == 0) trace.startNewSubPath (x, y);
            else        trace.lineTo (x, y);
        }

        g.setColour (accent);
        g.strokePath (trace, juce::PathStrokeType (1.5f));
    }

    if (w.label.isNotEmpty())
        caption (b.withTop (b.getBottom() - (float) GuiLayout::captionHeight));
}

void PluginCanvas::drawCurve (juce::Graphics& g, const GuiWidget& w, Part part)
{
    const auto b = w.bounds.toFloat();
    const auto accent = w.colour.isTransparent() ? defaultAccent : w.colour;
    auto area = b.withTrimmedBottom (w.label.isNotEmpty() ? (float) GuiLayout::captionHeight : 0.0f);
    const bool still = part != Part::moving, moving = part != Part::still;

    if (still)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (area, 4.0f);
    }

    const auto inner = area.reduced (6.0f);
    juce::Path curve;

    if (w.type == GuiWidget::Type::envelope && moving)
    {
        // Each stage's width follows its knob (as the knob shows it), so short and long
        // settings both read clearly. The sustain plateau keeps a fixed width.
        const auto a = roleProportion (w, "attack", 0.1f);
        const auto d = roleProportion (w, "decay", 0.3f);
        const auto s = roleProportion (w, "sustain", 0.7f);
        const auto r = roleProportion (w, "release", 0.4f);

        const float widths[] { 0.08f + 0.92f * a, 0.08f + 0.92f * d, 0.6f, 0.08f + 0.92f * r };
        const auto unit = inner.getWidth() / (widths[0] + widths[1] + widths[2] + widths[3]);
        const auto top = inner.getY(), bottom = inner.getBottom();
        const auto sustainY = bottom - (bottom - top) * s;

        juce::Point<float> points[] { { inner.getX(), bottom },
                                      { inner.getX() + widths[0] * unit, top },
                                      { inner.getX() + (widths[0] + widths[1]) * unit, sustainY },
                                      { inner.getX() + (widths[0] + widths[1] + widths[2]) * unit, sustainY },
                                      { inner.getRight(), bottom } };

        curve.startNewSubPath (points[0]);

        for (int i = 1; i < 5; ++i)
            curve.lineTo (points[i]);

        auto filled = curve;
        filled.closeSubPath();
        g.setColour (accent.withAlpha (0.22f));
        g.fillPath (filled);
        g.setColour (accent);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (int i = 1; i < 4; ++i)
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (points[i]));
    }
    else if (w.type == GuiWidget::Type::filter)
    {
        // A filter's response over 20 Hz to 20 kHz (log), -30..+24 dB.
        const auto* cutoffParam = [&] () -> const Param* { const auto f = w.roles.find ("cutoff"); return f != w.roles.end() ? paramById (f->second) : nullptr; }();
        float cutoff = 1000.0f;

        if (cutoffParam != nullptr)
            cutoff = cutoffParam->unit.equalsIgnoreCase ("Hz") || cutoffParam->unit.equalsIgnoreCase ("kHz")
                         ? currentValue (*cutoffParam) * (cutoffParam->unit.equalsIgnoreCase ("kHz") ? 1000.0f : 1.0f)
                         : 20.0f * std::pow (1000.0f, proportionOf (*cutoffParam, currentValue (*cutoffParam)));

        cutoff = juce::jlimit (20.0f, 20000.0f, cutoff);

        const auto resonance = roleProportion (w, "resonance", 0.2f);
        const auto q = 0.707f + resonance * 9.0f;
        const auto steep = w.mode.endsWith ("24");
        const auto kind = w.mode.startsWith ("high") ? 1 : (w.mode.startsWith ("band") ? 2 : 0);

        if (still)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));

            for (const auto f : { 100.0f, 1000.0f, 10000.0f })
                g.drawVerticalLine (juce::roundToInt (inner.getX() + inner.getWidth() * std::log10 (f / 20.0f) / 3.0f), inner.getY(), inner.getBottom());

            const auto zeroY = inner.getY() + inner.getHeight() * (24.0f / 54.0f);
            g.drawHorizontalLine (juce::roundToInt (zeroY), inner.getX(), inner.getRight());
        }

        for (int i = 0; i <= 120 && moving; ++i)
        {
            const auto t = (float) i / 120.0f;
            const auto f = 20.0f * std::pow (1000.0f, t);
            const auto x = f / cutoff;
            const auto denominator = std::sqrt ((1.0f - x * x) * (1.0f - x * x) + (x / q) * (x / q));
            auto magnitude = kind == 1 ? (x * x) / denominator : (kind == 2 ? (x / q) / denominator : 1.0f / denominator);

            if (steep)
                magnitude *= kind == 2 ? 1.0f : magnitude / juce::jmax (1.0f, q * 0.5f);

            const auto db = juce::jlimit (-30.0f, 24.0f, 20.0f * std::log10 (juce::jmax (magnitude, 1.0e-6f)));
            const auto px = inner.getX() + inner.getWidth() * t;
            const auto py = inner.getY() + inner.getHeight() * ((24.0f - db) / 54.0f);

            if (i == 0) curve.startNewSubPath (px, py);
            else        curve.lineTo (px, py);
        }

        if (moving)
        {
            auto filled = curve;
            filled.lineTo (inner.getRight(), inner.getBottom());
            filled.lineTo (inner.getX(), inner.getBottom());
            filled.closeSubPath();

            g.setColour (accent.withAlpha (0.22f));
            g.fillPath (filled);
            g.setColour (accent);
            g.strokePath (curve, juce::PathStrokeType (2.0f));
        }
    }

    if (w.label.isNotEmpty() && still)
    {
        g.setColour (captionColour.withAlpha (0.85f));
        g.setFont (Theme::font (12.5f));
        g.drawText (w.label, b.withTop (b.getBottom() - (float) GuiLayout::captionHeight), juce::Justification::centred, true);
    }
}

void PluginCanvas::drawWidget (juce::Graphics& g, int index, Part part, float override)
{
    const auto& w = layout.widgets[(size_t) index];

    if (w.isLive())
    {
        drawLive (g, w, index, part, override);
        return;
    }

    if (w.type == GuiWidget::Type::envelope || w.type == GuiWidget::Type::filter)
    {
        drawCurve (g, w, part);
        return;
    }

    const bool still = part != Part::moving, moving = part != Part::still;
    const auto* p = paramFor (w);
    const auto value = p != nullptr ? currentValue (*p) : 0.0f;
    const auto proportion = override >= 0.0f ? override : (p != nullptr ? proportionOf (*p, value) : 0.0f);
    const auto accent = w.colour.isTransparent() ? defaultAccent : w.colour;
    const bool missing = w.isControl() && p == nullptr;
    const auto b = w.bounds.toFloat();

    auto caption = [&] (juce::Rectangle<float> area)
    {
        if (! still)
            return;

        // The caption shows the value while it's being set or pointed at.
        const bool showValue = p != nullptr && (index == active || (index == hovered && ! design));
        juce::String text = w.label;

        if (showValue)
        {
            const auto decimals = (p->max - p->min) >= 100.0f ? 0 : 2;
            text = juce::String (value, decimals) + (p->unit.isNotEmpty() ? " " + p->unit : juce::String());
        }

        g.setColour (missing ? Theme::muted : captionColour.withAlpha (showValue ? 1.0f : 0.85f));
        g.setFont (Theme::font (12.5f, showValue));
        g.drawText (text, area, juce::Justification::centred, true);
    };

    switch (w.type)
    {
        case GuiWidget::Type::group:
        {
            if (! still)
                break;

            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (w.colour.isTransparent() ? captionColour.withAlpha (0.75f) : w.colour);
            g.setFont (Theme::font (w.fontSize > 0.0f ? w.fontSize : 11.5f, true));
            g.drawText (w.label, b.reduced (10.0f, 4.0f).withHeight (16.0f), juce::Justification::centredLeft, true);
            break;
        }

        case GuiWidget::Type::label:
        {
            if (! still)
                break;

            g.setColour (w.colour.isTransparent() ? captionColour : w.colour);
            g.setFont (Theme::font (w.fontSize > 0.0f ? w.fontSize : 14.0f, w.bold));
            g.drawText (w.label, b, juce::Justification::centredLeft, true);
            break;
        }

        case GuiWidget::Type::preset:
        {
            // "<  Preset name  >": the whole widget moves (one frame per preset).
            if (! moving)
                break;

            const auto chosen = override >= 0.0f ? juce::roundToInt (override) : currentPreset;
            const auto name = juce::isPositiveAndBelow (chosen, presetNames.size()) ? presetNames[chosen] : juce::String ("No presets");

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

            const auto arrow = juce::jmin (b.getHeight() * 0.32f, 8.0f);
            juce::Path left, right;
            left.addTriangle (b.getX() + 10.0f, b.getCentreY(), b.getX() + 10.0f + arrow, b.getCentreY() - arrow, b.getX() + 10.0f + arrow, b.getCentreY() + arrow);
            right.addTriangle (b.getRight() - 10.0f, b.getCentreY(), b.getRight() - 10.0f - arrow, b.getCentreY() - arrow, b.getRight() - 10.0f - arrow, b.getCentreY() + arrow);
            g.setColour (accent);
            g.fillPath (left);
            g.fillPath (right);

            g.setColour (captionColour);
            g.setFont (Theme::font (juce::jmin (14.0f, b.getHeight() * 0.5f), true));
            g.drawFittedText (name, b.reduced (14.0f + arrow * 2.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
            break;
        }

        case GuiWidget::Type::shape:
        {
            if (! still)
                break;

            auto path = juce::Drawable::parseSVGPath (w.path);

            if (path.isEmpty())
            {
                if (design)
                {
                    g.setColour (Theme::muted.withAlpha (0.5f));
                    g.drawRect (b, 1.0f);
                }

                break;
            }

            path.applyTransform (path.getTransformToScaleToFit (b.reduced (w.strokeWidth * 0.5f), true));
            g.setColour (w.colour.isTransparent() ? captionColour.withAlpha (0.9f) : w.colour);
            g.fillPath (path);

            if (w.strokeWidth > 0.0f)
            {
                g.setColour (w.stroke.isTransparent() ? juce::Colours::black : w.stroke);
                g.strokePath (path, juce::PathStrokeType (w.strokeWidth));
            }
            break;
        }

        case GuiWidget::Type::xy:
        {
            const auto pad = b.withTrimmedBottom ((float) GuiLayout::captionHeight);
            const auto x = roleProportion (w, "x", 0.5f), y = roleProportion (w, "y", 0.5f);
            const auto dot = juce::Point<float> (pad.getX() + 2.0f + (pad.getWidth() - 4.0f) * x, pad.getBottom() - 2.0f - (pad.getHeight() - 4.0f) * y);

            if (still)
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRoundedRectangle (pad, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.07f));

                for (int i = 1; i < 4; ++i)
                {
                    g.drawVerticalLine (juce::roundToInt (pad.getX() + pad.getWidth() * (float) i / 4.0f), pad.getY(), pad.getBottom());
                    g.drawHorizontalLine (juce::roundToInt (pad.getY() + pad.getHeight() * (float) i / 4.0f), pad.getX(), pad.getRight());
                }

                g.setColour (captionColour.withAlpha (0.85f));
                g.setFont (Theme::font (12.5f));
                g.drawText (w.label, b.withTop (pad.getBottom()), juce::Justification::centred, true);
            }

            if (moving)
            {
                g.setColour (accent.withAlpha (0.4f));
                g.drawVerticalLine (juce::roundToInt (dot.x), pad.getY() + 1.0f, pad.getBottom() - 1.0f);
                g.drawHorizontalLine (juce::roundToInt (dot.y), pad.getX() + 1.0f, pad.getRight() - 1.0f);

                g.setColour (accent.withAlpha (0.35f));
                g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (dot));
                g.setColour (accent);
                g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (dot));
            }
            break;
        }

        case GuiWidget::Type::knob:
        {
            const auto square = knobSquare (w).toFloat();

            if (moving)
                rendererFor (w.style).draw (g, square, proportion, KnobRenderer::defaultStartAngle, KnobRenderer::defaultEndAngle,
                                            index == hovered, index == active, ! missing);

            // The caption may be wider than the knob, so names like "Resonance" fit.
            caption (b.withTop (square.getBottom()).withHeight ((float) GuiLayout::captionHeight).expanded (14.0f, 0.0f));
            break;
        }

        case GuiWidget::Type::slider:
        {
            auto area = b.withTrimmedBottom ((float) GuiLayout::captionHeight);
            const auto track = w.vertical ? area.withSizeKeepingCentre (6.0f, area.getHeight() - 8.0f)
                                          : area.withSizeKeepingCentre (area.getWidth() - 8.0f, 6.0f);

            if (! moving)
            {
                caption (b.withTop (b.getBottom() - (float) GuiLayout::captionHeight).expanded (14.0f, 0.0f));
                break;
            }

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (track, 3.0f);

            const auto filled = w.vertical ? track.withTop (track.getBottom() - track.getHeight() * proportion)
                                           : track.withWidth (track.getWidth() * proportion);
            g.setColour (missing ? Theme::muted : accent);
            g.fillRoundedRectangle (filled, 3.0f);

            const auto thumbCentre = w.vertical ? juce::Point<float> (track.getCentreX(), filled.getY())
                                                : juce::Point<float> (filled.getRight(), track.getCentreY());
            const auto thumb = w.vertical ? juce::Rectangle<float> (area.getWidth() * 0.9f, 12.0f).withCentre (thumbCentre)
                                          : juce::Rectangle<float> (12.0f, area.getHeight() * 0.9f).withCentre (thumbCentre);

            g.setColour (juce::Colour (0xffe9e6df));
            g.fillRoundedRectangle (thumb, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (thumb, 3.0f, 1.0f);

            caption (b.withTop (b.getBottom() - (float) GuiLayout::captionHeight).expanded (14.0f, 0.0f));
            break;
        }

        case GuiWidget::Type::toggle:
        {
            if (! moving)
            {
                caption (b.withTop (b.getBottom() - (float) GuiLayout::captionHeight).expanded (14.0f, 0.0f));
                break;
            }

            const bool on = override >= 0.0f ? override >= 0.5f : (p != nullptr && proportion >= 0.5f);
            auto area = b.withTrimmedBottom ((float) GuiLayout::captionHeight);
            const auto lamp = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ area.getCentreX(), area.getY() + 8.0f });
            const auto button = area.withTrimmedTop (18.0f).withSizeKeepingCentre (juce::jmin (area.getWidth(), 40.0f), juce::jmax (12.0f, area.getHeight() - 20.0f));

            const auto ledColour = w.colour.isTransparent() ? juce::Colour (0xffffb020) : w.colour;   // amber, like the old pilot lights

            if (on)
            {
                g.setColour (ledColour.withAlpha (0.35f));
                g.fillEllipse (lamp.expanded (4.0f));
            }

            g.setColour (on ? ledColour : juce::Colour (0xff3a3326));
            g.fillEllipse (lamp);

            g.setGradientFill (juce::ColourGradient (juce::Colour (on ? 0xff2d2d31 : 0xff4a4a50), button.getX(), button.getY(),
                                                     juce::Colour (on ? 0xff45454b : 0xff2a2a2e), button.getX(), button.getBottom(), false));
            g.fillRoundedRectangle (button, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (button, 4.0f, 1.0f);

            caption (b.withTop (b.getBottom() - (float) GuiLayout::captionHeight).expanded (14.0f, 0.0f));
            break;
        }

        case GuiWidget::Type::selector:
        {
            auto area = b.withTrimmedBottom ((float) GuiLayout::captionHeight);
            const auto count = juce::jmax (1, w.options.size() > 0 ? w.options.size() : (p != nullptr ? (int) (p->max - p->min) + 1 : 1));
            const auto chosen = override >= 0.0f ? juce::roundToInt (override) : (p != nullptr ? juce::roundToInt (value - p->min) : -1);
            const auto cellW = area.getWidth() / (float) count;

            if (! moving)
            {
                caption (b.withTop (b.getBottom() - (float) GuiLayout::captionHeight));
                break;
            }

            for (int i = 0; i < count; ++i)
            {
                const auto cell = juce::Rectangle<float> (area.getX() + (float) i * cellW, area.getY(), cellW, area.getHeight()).reduced (1.0f);
                g.setColour (i == chosen ? accent : juce::Colours::black.withAlpha (0.45f));
                g.fillRoundedRectangle (cell, 3.0f);
                g.setColour (i == chosen ? juce::Colours::white : captionColour.withAlpha (0.8f));
                g.setFont (Theme::font (12.0f, i == chosen));
                g.drawFittedText (i < w.options.size() ? w.options[i] : juce::String (i + 1), cell.toNearestInt(), juce::Justification::centred, 1, 0.8f);
            }

            caption (b.withTop (b.getBottom() - (float) GuiLayout::captionHeight));
            break;
        }

        case GuiWidget::Type::meter:
        case GuiWidget::Type::scope:
        case GuiWidget::Type::lamp:
        case GuiWidget::Type::envelope:
        case GuiWidget::Type::filter:
            break;   // drawn above
    }
}

juce::Rectangle<int> PluginCanvas::movingAreaOf (const GuiWidget& w) const
{
    switch (w.type)
    {
        case GuiWidget::Type::knob:      return knobSquare (w);
        case GuiWidget::Type::slider:
        case GuiWidget::Type::toggle:
        case GuiWidget::Type::selector:  return w.bounds.withTrimmedBottom (GuiLayout::captionHeight);
        case GuiWidget::Type::lamp:
        case GuiWidget::Type::meter:
        case GuiWidget::Type::scope:
        case GuiWidget::Type::envelope:
        case GuiWidget::Type::filter:    return w.bounds.withTrimmedBottom (w.label.isNotEmpty() ? GuiLayout::captionHeight : 0);
        case GuiWidget::Type::xy:        return w.bounds.withTrimmedBottom (GuiLayout::captionHeight);
        case GuiWidget::Type::preset:    return w.bounds;
        case GuiWidget::Type::label:
        case GuiWidget::Type::group:
        case GuiWidget::Type::shape:     break;
    }

    return {};
}

PluginCanvas::Bake PluginCanvas::bake()
{
    const juce::ScopedValueSetter<int> noHover (hovered, -1), noActive (active, -1);
    renderers.clear();

    Bake result;
    result.background = juce::Image (juce::Image::ARGB, layout.width, layout.height, true);

    {
        juce::Graphics g (result.background);
        g.setGradientFill (juce::ColourGradient (layout.backgroundTop, 0.0f, 0.0f, layout.backgroundBottom, 0.0f, (float) layout.height, false));
        g.fillAll();

        for (int i = 0; i < (int) layout.widgets.size(); ++i)
            drawWidget (g, i, Part::still);
    }

    std::map<juce::String, int> knobStrips;   // knobs with the same look and size share frames

    for (int i = 0; i < (int) layout.widgets.size(); ++i)
    {
        const auto& w = layout.widgets[(size_t) i];
        const auto area = movingAreaOf (w);
        result.stripOf.push_back (-1);
        result.movingArea.push_back (area);

        if (area.isEmpty())
            continue;

        int frames = 0;

        switch (w.type)
        {
            case GuiWidget::Type::knob:      frames = 128; break;
            case GuiWidget::Type::slider:    frames = 100; break;
            case GuiWidget::Type::preset:    frames = juce::jmax (1, presetNames.size()); break;
            case GuiWidget::Type::toggle:    frames = 2; break;
            case GuiWidget::Type::lamp:      frames = 2; break;
            case GuiWidget::Type::selector:
            {
                const auto* p = paramFor (w);
                frames = juce::jmax (1, w.options.size() > 0 ? w.options.size() : (p != nullptr ? (int) (p->max - p->min) + 1 : 1));
                break;
            }
            case GuiWidget::Type::label:
            case GuiWidget::Type::group:
            case GuiWidget::Type::meter:
            case GuiWidget::Type::scope:
            case GuiWidget::Type::envelope:
            case GuiWidget::Type::filter:
            case GuiWidget::Type::xy:
            case GuiWidget::Type::shape:     break;
        }

        if (frames == 0)
            continue;

        if (w.type == GuiWidget::Type::knob)
        {
            const auto key = w.style + "|" + juce::String (area.getWidth());

            if (const auto found = knobStrips.find (key); found != knobStrips.end())
            {
                result.stripOf.back() = found->second;
                continue;
            }

            Bake::Strip strip;
            strip.image = rendererFor (w.style).renderFilmstrip (area.getWidth(), frames, true,
                                                                 KnobRenderer::defaultStartAngle, KnobRenderer::defaultEndAngle);
            strip.frameWidth = strip.frameHeight = area.getWidth();
            strip.frames = frames;

            knobStrips[key] = (int) result.strips.size();
            result.stripOf.back() = (int) result.strips.size();
            result.strips.push_back (strip);
            continue;
        }

        Bake::Strip strip;
        strip.frameWidth = area.getWidth();
        strip.frameHeight = area.getHeight();
        strip.frames = frames;
        strip.image = juce::Image (juce::Image::ARGB, area.getWidth(), area.getHeight() * frames, true);

        for (int f = 0; f < frames; ++f)
        {
            juce::Graphics g (strip.image);
            g.setOrigin ({ -area.getX(), f * area.getHeight() - area.getY() });
            g.reduceClipRegion (area);

            const auto override = w.type == GuiWidget::Type::slider ? (float) f / (float) (frames - 1) : (float) f;
            drawWidget (g, i, Part::moving, override);
        }

        result.stripOf.back() = (int) result.strips.size();
        result.strips.push_back (strip);
    }

    renderers.clear();
    return result;
}

void PluginCanvas::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);

    const auto area = panelArea();
    const auto scale = area.getWidth() / (float) layout.width;

    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 18, { 0, 6 }).drawForRectangle (g, area.toNearestInt());

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area.toNearestInt());
        g.addTransform (juce::AffineTransform::scale (scale).translated (area.getX(), area.getY()));

        const auto plugin = juce::Rectangle<float> (0.0f, 0.0f, (float) layout.width, (float) layout.height);
        g.setGradientFill (juce::ColourGradient (layout.backgroundTop, 0.0f, 0.0f, layout.backgroundBottom, 0.0f, plugin.getBottom(), false));
        g.fillRect (plugin);

        if (design)
        {
            g.setColour (juce::Colours::white.withAlpha (0.035f));

            for (int x = 16; x < layout.width; x += 16)
                g.drawVerticalLine (x, 0.0f, plugin.getBottom());

            for (int y = 16; y < layout.height; y += 16)
                g.drawHorizontalLine (y, 0.0f, plugin.getRight());
        }

        for (int i = 0; i < (int) layout.widgets.size(); ++i)
            drawWidget (g, i);

        if (layout.widgets.empty())
        {
            g.setColour (captionColour.withAlpha (0.6f));
            g.setFont (Theme::font (15.0f));
            g.drawText ("The GUI appears here once the plugin is built.", plugin, juce::Justification::centred, false);
        }
    }

    g.setColour (design ? Theme::accent.withAlpha (0.55f) : Theme::outline);
    g.drawRect (area, design ? 1.5f : 1.0f);

    // Which tab, above the window's top-left corner.
    g.setColour (design ? Theme::accent : Theme::safe);
    g.setFont (Theme::font (12.0f, true));
    g.drawText (design ? "BUILD" : "PLAY", juce::Rectangle<float> (area.getX(), area.getY() - 20.0f, 200.0f, 16.0f),
                juce::Justification::centredLeft, false);

    if (design && selected >= 0)
    {
        const auto box = toView (layout.widgets[(size_t) selected].bounds);
        g.setColour (Theme::accent);
        g.drawRect (box.expanded (2.0f), 1.5f);
        g.fillRect (juce::Rectangle<float> (handleSize, handleSize).withCentre (box.getBottomRight()));
    }
}

void PluginCanvas::resized()
{
    inspector.setBounds (getLocalBounds().reduced (12).removeFromRight (260).withHeight (juce::jmin (getHeight() - 24, 470)));
}

//==============================================================================
void PluginCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    const auto plugin = toPlugin (e.position);
    dragStart = e.position;
    changed = false;
    carried.clear();

    if (design)
    {
        // The selected element's corner handle resizes it.
        if (selected >= 0)
        {
            const auto box = toView (layout.widgets[(size_t) selected].bounds);

            if (juce::Rectangle<float> (handleSize * 1.6f, handleSize * 1.6f).withCentre (box.getBottomRight()).contains (e.position))
            {
                resizing = true;
                startBounds = layout.widgets[(size_t) selected].bounds;
                return;
            }
        }

        select (widgetAt (plugin, false));

        if (selected >= 0)
        {
            startBounds = layout.widgets[(size_t) selected].bounds;

            if (layout.widgets[(size_t) selected].type == GuiWidget::Type::group)
                for (int i = 0; i < (int) layout.widgets.size(); ++i)
                    if (i != selected && startBounds.contains (layout.widgets[(size_t) i].bounds))
                        carried.push_back ({ i, layout.widgets[(size_t) i].bounds });
        }

        return;
    }

    active = widgetAt (plugin, true);

    if (active < 0)
        return;

    const auto& w = layout.widgets[(size_t) active];

    if (w.type == GuiWidget::Type::xy)
    {
        setXy (w, plugin);
        return;
    }

    if (w.type == GuiWidget::Type::preset)
    {
        active = -1;

        if (presetNames.isEmpty() || onPresetChosen == nullptr)
            return;

        // The arrows step; the middle lists them all.
        const auto x = (plugin.x - (float) w.bounds.getX()) / (float) juce::jmax (1, w.bounds.getWidth());
        const auto count = presetNames.size();

        if (x < 0.22f || x > 0.78f)
        {
            onPresetChosen (((currentPreset < 0 ? 0 : currentPreset) + (x < 0.22f ? count - 1 : 1)) % count);
            return;
        }

        juce::PopupMenu menu;

        for (int i = 0; i < count; ++i)
            menu.addItem (i + 1, presetNames[i], true, i == currentPreset);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (toView (w.bounds).toNearestInt())),
                            [safeThis = juce::Component::SafePointer<PluginCanvas> (this)] (int choice)
                            {
                                if (safeThis != nullptr && choice > 0 && safeThis->onPresetChosen != nullptr)
                                    safeThis->onPresetChosen (choice - 1);
                            });
        return;
    }

    const auto* p = paramFor (w);

    if (p == nullptr)
        return;

    startProportion = proportionOf (*p, currentValue (*p));

    if (w.type == GuiWidget::Type::toggle)
    {
        setValue (*p, startProportion >= 0.5f ? p->min : p->max);
    }
    else if (w.type == GuiWidget::Type::selector)
    {
        const auto count = juce::jmax (1, w.options.size() > 0 ? w.options.size() : (int) (p->max - p->min) + 1);
        const auto cell = (int) ((plugin.x - (float) w.bounds.getX()) / ((float) w.bounds.getWidth() / (float) count));
        setValue (*p, p->min + (float) juce::jlimit (0, count - 1, cell));
    }
    else if (w.type == GuiWidget::Type::slider)
    {
        mouseDrag (e);
    }

    repaint();
}

void PluginCanvas::mouseDrag (const juce::MouseEvent& e)
{
    const auto scale = panelArea().getWidth() / (float) layout.width;
    const auto delta = (e.position - dragStart) / scale;

    if (design)
    {
        if (selected < 0)
            return;

        auto& w = layout.widgets[(size_t) selected];

        if (resizing)
        {
            const auto minW = 12, minH = w.type == GuiWidget::Type::knob ? 12 + GuiLayout::captionHeight : 12;
            const auto newW = juce::jmax (minW, snapped ((float) startBounds.getWidth() + delta.x));
            const auto newH = juce::jmax (minH, snapped ((float) startBounds.getHeight() + delta.y));

            // A knob stays round: its size follows the drag, its caption stays under it.
            w.bounds = w.type == GuiWidget::Type::knob ? startBounds.withSize (newW, newW + GuiLayout::captionHeight)
                                                       : startBounds.withSize (newW, newH);
        }
        else
        {
            const auto dx = snapped (delta.x), dy = snapped (delta.y);
            w.bounds = startBounds.translated (dx, dy);

            for (const auto& [index, bounds] : carried)
                layout.widgets[(size_t) index].bounds = bounds.translated (dx, dy);
        }

        changed = true;
        repaint();
        return;
    }

    if (active < 0)
        return;

    const auto& w = layout.widgets[(size_t) active];

    if (w.type == GuiWidget::Type::xy)
    {
        setXy (w, toPlugin (e.position));
        return;
    }

    const auto* p = paramFor (w);

    if (p == nullptr)
        return;

    if (w.type == GuiWidget::Type::knob)
    {
        // Up or right turns it up; Shift moves it finely.
        const auto travel = e.mods.isShiftDown() ? 900.0f : 200.0f;
        setValue (*p, valueAt (*p, startProportion + (delta.x - delta.y) / travel));
    }
    else if (w.type == GuiWidget::Type::slider)
    {
        const auto plugin = toPlugin (e.position);
        const auto area = w.bounds.toFloat().withTrimmedBottom ((float) GuiLayout::captionHeight).reduced (4.0f);
        const auto t = w.vertical ? (area.getBottom() - plugin.y) / area.getHeight() : (plugin.x - area.getX()) / area.getWidth();
        setValue (*p, valueAt (*p, t));
    }
}

void PluginCanvas::mouseUp (const juce::MouseEvent&)
{
    if (design && changed)
        edited();

    resizing = false;
    changed = false;
    active = -1;
    carried.clear();

    if (design && selected >= 0)
        inspector.show (layout.widgets[(size_t) selected]);

    repaint();
}

void PluginCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto index = widgetAt (toPlugin (e.position), ! design);

    if (index < 0)
        return;

    const auto& w = layout.widgets[(size_t) index];

    if (design)
    {
        if (w.type == GuiWidget::Type::knob && onEditKnob != nullptr)
            onEditKnob (index);

        return;
    }

    if (const auto* p = paramFor (w); p != nullptr && (w.type == GuiWidget::Type::knob || w.type == GuiWidget::Type::slider))
        setValue (*p, p->def);
}

void PluginCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (design)
        return;

    const auto index = widgetAt (toPlugin (e.position), true);

    if (index < 0)
        return;

    const auto& w = layout.widgets[(size_t) index];

    if (const auto* p = paramFor (w); p != nullptr && (w.type == GuiWidget::Type::knob || w.type == GuiWidget::Type::slider))
        setValue (*p, valueAt (*p, proportionOf (*p, currentValue (*p)) + wheel.deltaY * 0.08f));
}

void PluginCanvas::mouseMove (const juce::MouseEvent& e)
{
    const auto index = widgetAt (toPlugin (e.position), ! design);

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void PluginCanvas::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

bool PluginCanvas::keyPressed (const juce::KeyPress& key)
{
    if (! design || selected < 0)
        return false;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        layout.widgets.erase (layout.widgets.begin() + selected);
        select (-1);
        edited();
        return true;
    }

    const auto step = key.getModifiers().isShiftDown() ? 8 : 1;
    juce::Point<int> move;

    if (key.isKeyCode (juce::KeyPress::leftKey))  move = { -step, 0 };
    if (key.isKeyCode (juce::KeyPress::rightKey)) move = { step, 0 };
    if (key.isKeyCode (juce::KeyPress::upKey))    move = { 0, -step };
    if (key.isKeyCode (juce::KeyPress::downKey))  move = { 0, step };

    if (move.isOrigin())
        return false;

    layout.widgets[(size_t) selected].bounds.translate (move.x, move.y);
    edited();
    return true;
}
