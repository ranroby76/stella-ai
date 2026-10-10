// C:\workspace\Stella AI Studio\src\PluginCanvas.cpp
// The canvas's elements: adding them, drawing them, baking them for Export. The view and
// the mouse are in PluginCanvas_Mouse.cpp, the edit menu in PluginCanvas_Menu.cpp.

#include "PluginCanvas.h"
#include "PluginCanvasMenu.h"
#include "StellaLookAndFeel.h"
#include "stella_keys.h"

namespace
{
    constexpr int scopeSamples = 2048;
    const juce::Colour defaultAccent { 0xffe5484d };
    const juce::Colour captionColour { 0xffd8d4ca };
}

//==============================================================================
bool PluginCanvas::isInteractive (const GuiWidget& w)
{
    return w.isControl() || w.type == GuiWidget::Type::xy || w.type == GuiWidget::Type::preset || w.type == GuiWidget::Type::keyboard;
}

juce::String PluginCanvas::typeDisplayName (GuiWidget::Type type)
{
    switch (type)
    {
        case GuiWidget::Type::knob:      return "Knob";
        case GuiWidget::Type::slider:    return "Slider";
        case GuiWidget::Type::toggle:    return "Switch";
        case GuiWidget::Type::selector:  return "Selector";
        case GuiWidget::Type::label:     return "Text";
        case GuiWidget::Type::group:     return "Frame";
        case GuiWidget::Type::meter:     return "Level meter";
        case GuiWidget::Type::scope:     return "Scope";
        case GuiWidget::Type::lamp:      return "Lamp";
        case GuiWidget::Type::envelope:  return "Envelope curve";
        case GuiWidget::Type::filter:    return "Filter curve";
        case GuiWidget::Type::xy:        return "XY pad";
        case GuiWidget::Type::shape:     return "Shape";
        case GuiWidget::Type::preset:    return "Preset browser";
        case GuiWidget::Type::image:     return "Picture";
        case GuiWidget::Type::keyboard:  return "Keyboard";
    }

    return "Element";
}

juce::Rectangle<int> PluginCanvas::defaultBounds (GuiWidget::Type type, int x, int y)
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
        case GuiWidget::Type::image:     return { x, y, 160, 120 };
        case GuiWidget::Type::keyboard:  return { x, y, 600, 90 };
    }

    return { x, y, 60, 60 };
}

const char* PluginCanvas::defaultStarPath()
{
    return "M50,5 L61,39 L97,39 L68,61 L79,95 L50,74 L21,95 L32,61 L3,39 L39,39 Z";
}

//==============================================================================
PluginCanvas::PluginCanvas()
{
    setWantsKeyboardFocus (true);
    scopeBuffer.resize ((size_t) scopeSamples, 0.0f);

    menu = std::make_unique<ElementMenu> (*this);
    addChildComponent (*menu);

    banner = std::make_unique<AiBanner> (*this);
    addChildComponent (*banner);

    // The zoom, bottom left: Fit, a slider (double-click: 100%) and the percentage.
    fitButton.setTooltip ("Show the whole window (Ctrl+0)");
    fitButton.onClick = [this] { fitToView(); };

    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setRange (minZoom, maxZoom, 0.01);
    zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    zoomSlider.setDoubleClickReturnValue (true, 1.0);
    zoomSlider.setValue (1.0, juce::dontSendNotification);
    zoomSlider.setTooltip ("Zoom (also Ctrl + mouse wheel). Double-click: 100%");
    zoomSlider.onValueChange = [this]
    {
        autoFit = false;
        setZoom ((float) zoomSlider.getValue());
    };

    zoomLabel.setFont (Theme::font (12.0f, true));
    zoomLabel.setJustificationType (juce::Justification::centredLeft);
    zoomLabel.setColour (juce::Label::textColourId, Theme::muted);
    zoomLabel.setInterceptsMouseClicks (false, false);

    for (auto* c : std::initializer_list<juce::Component*> { &fitButton, &zoomSlider, &zoomLabel })
        addChildComponent (c);

    startTimerHz (30);
}

PluginCanvas::~PluginCanvas()
{
    *alive = false;
    stopTimer();
    pictureChooser = nullptr;
}

void PluginCanvas::setGuiFolder (const juce::File& folder)
{
    if (folder == guiFolder)
        return;

    guiFolder = folder;
    pictures.clear();
    dropGhost.reset();
    select (-1);

    // Each project keeps its own view; one never moved fits the window.
    autoFit = true;
    zoom = 1.0f;
    pan = {};
    loadView();

    for (auto* c : std::initializer_list<juce::Component*> { &fitButton, &zoomSlider, &zoomLabel })
        c->setVisible (hasPanel());

    banner->setVisible (false);

    if (autoFit)
        fitToView();
    else
        viewChanged();
}

void PluginCanvas::setLayout (const GuiLayout& newLayout)
{
    const bool sizeChanged = newLayout.width != layout.width || newLayout.height != layout.height;

    releaseKey();   // its keyboard may be gone, or elsewhere now
    layout = newLayout;
    meterLevels.clear();

    // Pictures that weren't there may be now.
    for (auto it = pictures.begin(); it != pictures.end();)
        it = it->second.isValid() ? std::next (it) : pictures.erase (it);

    if (! juce::isPositiveAndBelow (selected, (int) layout.widgets.size()))
        selected = -1;

    if (selected < 0 && ! panelSelected)
        closeMenu();
    else if (menuOpen)
        openMenu();   // shows the element's new state

    if (autoFit && sizeChanged)
        fitToView();
    else
        viewChanged();

    reportSources();
}

void PluginCanvas::setDesignMode (bool shouldDesign)
{
    releaseKey();
    design = shouldDesign;

    if (! design)
        select (-1);

    repaint();
}

void PluginCanvas::setParameters (const juce::Array<Param>& newParams)
{
    params = newParams;
    values.clear();

    for (const auto& p : params)
        values[p.index] = p.value;

    if (menuOpen)
        openMenu();   // the parameter lists

    repaint();
}

void PluginCanvas::setSources (const juce::StringArray& signals, const juce::StringArray& displays)
{
    signalSources = signals;
    displaySources = displays;

    if (menuOpen)
        openMenu();
}

void PluginCanvas::edited()
{
    reportSources();

    if (onLayoutEdited != nullptr)
        onLayoutEdited();

    layOutOverlays();
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

//==============================================================================
void PluginCanvas::addPrimitive (const Primitive& primitive, std::optional<juce::Point<float>> centre)
{
    if (! design || ! hasPanel())
        return;

    // The background picture: a picture element filling the window, behind everything.
    if (primitive.setsBackground())
    {
        choosePicture (-1);
        return;
    }

    const auto index = addElement (primitive.makeWidget(), centre);

    // A picture needs its file: ask for it at once.
    const auto& w = layout.widgets[(size_t) index];

    if (w.type == GuiWidget::Type::image && w.image.isEmpty())
        choosePicture (index);
}

juce::Rectangle<int> PluginCanvas::placed (juce::Rectangle<int> bounds, juce::Point<float> centre) const
{
    auto b = bounds.withPosition (snapped (centre.x - (float) bounds.getWidth() * 0.5f),
                                  snapped (centre.y - (float) bounds.getHeight() * 0.5f));

    b.setX (juce::jlimit (0, juce::jmax (0, layout.width - b.getWidth()), b.getX()));
    b.setY (juce::jlimit (0, juce::jmax (0, layout.height - b.getHeight()), b.getY()));
    return b;
}

int PluginCanvas::addElement (GuiWidget w, std::optional<juce::Point<float>> centre)
{
    auto unusedParam = [this] (const juce::StringArray& avoid)
    {
        for (const auto& p : params)
        {
            bool shown = avoid.contains (p.id);

            for (const auto& other : layout.widgets)
                shown = shown || other.param == p.id;

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

    // What the primitive leaves open: the first parameter not shown yet, its name as the
    // caption, the look its kind mostly wears in this window, a source to watch.
    switch (w.type)
    {
        case GuiWidget::Type::knob:
        case GuiWidget::Type::slider:
        case GuiWidget::Type::toggle:
        case GuiWidget::Type::selector:
            if (w.param.isEmpty())
                w.param = unusedParam ({});

            if (w.label.isEmpty())
                for (const auto& p : params)
                    if (p.id == w.param)
                        w.label = p.name;

            if (Looks::takesLook (w) && w.style.isEmpty())
                w.style = commonLook (w);

            if (w.type == GuiWidget::Type::selector && w.options.isEmpty())
                w.options = { "One", "Two", "Three" };
            break;

        case GuiWidget::Type::label:     if (w.label.isEmpty()) w.label = "Text"; break;
        case GuiWidget::Type::group:     if (w.label.isEmpty()) w.label = "SECTION"; break;

        case GuiWidget::Type::meter:
            if (w.source.isEmpty()) w.source = "plugin.out L";
            if (w.mode.isEmpty())   w.mode = "peak";
            break;

        case GuiWidget::Type::scope:
            if (w.source.isEmpty()) w.source = "plugin.out L";
            break;

        case GuiWidget::Type::lamp:
            if (w.source.isEmpty()) w.source = displaySources.isEmpty() ? juce::String ("plugin.out L") : displaySources[0];
            break;

        case GuiWidget::Type::envelope:
            if (w.roles.empty())
                for (const auto& role : GuiWidget::rolesOf (w.type))
                    if (const auto id = paramNamed (role); id.isNotEmpty())
                        w.roles[role] = id;
            break;

        case GuiWidget::Type::filter:
            if (w.roles.empty())
            {
                if (const auto id = paramNamed ("cutoff"); id.isNotEmpty())    w.roles["cutoff"] = id;
                if (const auto id = paramNamed ("reso"); id.isNotEmpty())      w.roles["resonance"] = id;
            }

            if (w.mode.isEmpty())
                w.mode = "lowpass";
            break;

        case GuiWidget::Type::xy:
            if (w.roles.empty())
            {
                const auto x = unusedParam ({});
                const auto y = unusedParam ({ x });
                if (x.isNotEmpty()) w.roles["x"] = x;
                if (y.isNotEmpty()) w.roles["y"] = y;
            }
            break;

        case GuiWidget::Type::shape:     if (w.path.isEmpty()) w.path = defaultStarPath(); break;

        case GuiWidget::Type::keyboard:
            stella::keys::normalise (w.lowNote, w.highNote);
            break;

        case GuiWidget::Type::preset:
        case GuiWidget::Type::image:     break;
    }

    // Centred where it was dropped, or in the middle of what's showing; always inside the window.
    w.bounds = placed (w.bounds, centre.value_or (toPlugin (viewArea().getCentre())));

    // Frames go underneath, everything else on top.
    int index = 0;

    if (w.type == GuiWidget::Type::group)
    {
        layout.widgets.insert (layout.widgets.begin(), w);
    }
    else
    {
        layout.widgets.push_back (w);
        index = (int) layout.widgets.size() - 1;
    }

    select (index);
    openMenu();
    edited();
    return index;
}

void PluginCanvas::removeSelected()
{
    if (! juce::isPositiveAndBelow (selected, (int) layout.widgets.size()))
        return;

    layout.widgets.erase (layout.widgets.begin() + selected);
    select (-1);
    edited();
}

//==============================================================================
juce::File PluginCanvas::imagesFolder() const
{
    return hasPanel() ? guiFolder.getChildFile (GuiLayout::imagesFolder) : juce::File();
}

const juce::Image& PluginCanvas::picture (const juce::String& name)
{
    static const juce::Image none;

    if (name.isEmpty() || ! hasPanel())
        return none;

    auto found = pictures.find (name);

    if (found == pictures.end())
    {
        const auto file = imagesFolder().getChildFile (name);
        found = pictures.emplace (name, file.existsAsFile() ? juce::ImageFileFormat::loadFrom (file) : juce::Image()).first;
    }

    return found->second;
}

void PluginCanvas::choosePicture (int widgetIndex)
{
    if (! hasPanel())
        return;

    pictureChooser = std::make_unique<juce::FileChooser> (widgetIndex < 0 ? "Choose the background picture" : "Choose a picture",
                                                          juce::File(), GuiLayout::pictureFiles());

    pictureChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [safeThis = juce::Component::SafePointer<PluginCanvas> (this), widgetIndex] (const juce::FileChooser& chooser)
                                 {
                                     if (safeThis == nullptr)
                                         return;

                                     const auto file = chooser.getResult();

                                     if (file == juce::File())
                                         return;

                                     const auto name = GuiLayout::importPicture (file, safeThis->imagesFolder());

                                     if (name.isEmpty())
                                     {
                                         juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't use that picture",
                                                                                 "Pictures can be PNG, JPEG or GIF files.");
                                         return;
                                     }

                                     safeThis->usePicture (widgetIndex, name);
                                 });
}

void PluginCanvas::usePicture (int widgetIndex, const juce::String& name)
{
    pictures.erase (name);   // read afresh

    if (widgetIndex < 0)
    {
        // A background picture: a picture element covering the window, at the back. Its
        // corners and sides stretch it; its menu and right-click change its layer.
        releaseKey();

        GuiWidget backdrop;
        backdrop.type = GuiWidget::Type::image;
        backdrop.image = name;
        backdrop.mode = "stretch";
        backdrop.bounds = { 0, 0, layout.width, layout.height };
        layout.widgets.insert (layout.widgets.begin(), backdrop);
        layout.backgroundImage.clear();
        meterLevels.clear();

        select (0);
        edited();
        openMenu();
        return;
    }

    if (juce::isPositiveAndBelow (widgetIndex, (int) layout.widgets.size()))
    {
        auto& w = layout.widgets[(size_t) widgetIndex];
        const bool first = w.image.isEmpty();
        w.image = name;

        // A new picture element takes the picture's own size (made to fit in the window).
        if (const auto& image = picture (name); first && image.isValid())
        {
            const auto scale = juce::jmin (1.0f, (float) layout.width / (float) image.getWidth(), (float) layout.height / (float) image.getHeight());
            const auto size = juce::Rectangle<int> (juce::jmax (8, juce::roundToInt ((float) image.getWidth() * scale)),
                                                    juce::jmax (8, juce::roundToInt ((float) image.getHeight() * scale)));
            w.bounds = placed (size, w.bounds.getCentre().toFloat());
        }
    }

    edited();

    if (menuOpen)
        openMenu();
}

//==============================================================================
void PluginCanvas::setPresets (const juce::StringArray& names, int current)
{
    presetNames = names;
    currentPreset = current;
    repaint();
}

void PluginCanvas::setLooks (Looks& looksToUse)
{
    looks = &looksToUse;
    repaint();
}

void PluginCanvas::looksChanged()
{
    // The edit menu lists the looks: it shows the new ones, once whatever changed them is done.
    if (menuOpen)
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<PluginCanvas> (this)]
                                         {
                                             if (safeThis != nullptr && safeThis->menuOpen)
                                                 safeThis->openMenu();
                                         });

    repaint();
}

void PluginCanvas::useLook (int widgetIndex, const juce::String& lookName)
{
    if (! juce::isPositiveAndBelow (widgetIndex, (int) layout.widgets.size()))
        return;

    auto& w = layout.widgets[(size_t) widgetIndex];

    if (! Looks::takesLook (w) || w.style == lookName)
        return;

    w.style = lookName;
    edited();
}

juce::String PluginCanvas::giveOwnLook (int widgetIndex)
{
    if (looks == nullptr || ! juce::isPositiveAndBelow (widgetIndex, (int) layout.widgets.size()))
        return {};

    auto& w = layout.widgets[(size_t) widgetIndex];

    if (! Looks::takesLook (w))
        return {};

    // A copy of what it wears now, named after it ("Cutoff"), saved as the plugin's own: never
    // the name of a look other controls wear.
    juce::StringArray worn;

    for (const auto& other : layout.widgets)
        if (Looks::takesLook (other))
            worn.addIfNotAlreadyThere (looks->lookFor (other).name);

    const auto& current = looks->lookFor (w);
    const auto doc = current.doc;
    const auto name = looks->freeName (w.label.isNotEmpty() ? w.label : current.name, worn);

    looks->setProjectLook (name, Looks::kindOf (w), doc);
    w.style = name;
    edited();
    return name;
}

juce::String PluginCanvas::commonLook (const GuiWidget& widget) const
{
    std::map<juce::String, int> counts;   // "" counts too: the default look
    const auto kind = Looks::kindOf (widget);

    for (const auto& other : layout.widgets)
        if (Looks::takesLook (other) && Looks::kindOf (other) == kind)
            ++counts[other.style];

    juce::String best;
    int most = 0;

    for (const auto& [name, count] : counts)
    {
        if (count > most)
        {
            best = name;
            most = count;
        }
    }

    return best;
}

//==============================================================================
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

void PluginCanvas::drawLook (juce::Graphics& g, const GuiWidget& w, juce::Rectangle<int> area, float value)
{
    if (looks == nullptr || area.isEmpty())
        return;

    const auto kind = Looks::kindOf (w);
    const auto& look = looks->lookFor (w);

    // Zoomed in, it's drawn with more pixels so it stays sharp; Export bakes plugin pixels.
    const auto resolution = baking ? 1.0f
                                   : (g.getInternalContext().getPhysicalPixelScaleFactor() > 1.3f ? 2.0f : 1.0f);

    g.drawImage (looks->frame (look, kind, area.getWidth(), area.getHeight(), value, resolution), area.toFloat());
}

juce::Rectangle<int> PluginCanvas::knobSquare (const GuiWidget& w) const
{
    const auto size = juce::jmax (8, juce::jmin (w.bounds.getWidth(), w.bounds.getHeight() - GuiLayout::captionHeight));
    return { w.bounds.getX() + (w.bounds.getWidth() - size) / 2, w.bounds.getY(), size, size };
}

//==============================================================================
void PluginCanvas::timerCallback()
{
    if (! isShowing())
        return;

    // Keyboards light up for the notes that sound: the mouse's, a MIDI keyboard's, the host's.
    if (isNoteDown != nullptr)
    {
        std::bitset<128> down;

        for (const auto& w : layout.widgets)
            if (w.type == GuiWidget::Type::keyboard)
                for (int note = juce::jmax (0, w.lowNote); note <= juce::jmin (127, w.highNote); ++note)
                    if (isNoteDown (note))
                        down.set ((size_t) note);

        if (down != notesShown)
        {
            notesShown = down;

            for (const auto& w : layout.widgets)
                if (w.type == GuiWidget::Type::keyboard)
                    repaint (toView (w.bounds).getSmallestIntegerContainer().expanded (2));
        }
    }

    bool live = false;

    for (const auto& w : layout.widgets)
        live = live || w.isLive();

    if (! live || readLevel == nullptr)
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

    // Only what moves is drawn again: the rest of the window (a big background picture,
    // say) stays as it is.
    for (const auto& w : layout.widgets)
        if (w.isLive())
            repaint (toView (w.bounds).getSmallestIntegerContainer().expanded (2));
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

    if (w.type == GuiWidget::Type::keyboard)
    {
        drawKeyboard (g, w, index, part);
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
        case GuiWidget::Type::slider:
        case GuiWidget::Type::toggle:
        {
            // Its look, at the frame for its value (a switch: off or on). Unbound, it's faded.
            if (moving)
            {
                const auto position = w.type != GuiWidget::Type::toggle ? proportion
                                    : (override >= 0.0f ? (override >= 0.5f ? 1.0f : 0.0f) : (p != nullptr && proportion >= 0.5f ? 1.0f : 0.0f));

                juce::Graphics::ScopedSaveState state (g);

                if (missing && ! baking)
                    g.setOpacity (0.45f);

                drawLook (g, w, movingAreaOf (w), position);
            }

            // The caption may be wider than the control, so names like "Resonance" fit.
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

        case GuiWidget::Type::image:
        {
            if (! still)
                break;

            if (picture (w.image).isValid())
            {
                drawPicture (g, w.image, b, w.mode.isNotEmpty() ? w.mode : juce::String ("fit"));
            }
            else if (design && ! baking)
            {
                // No picture yet: a dashed frame that says how to get one.
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRect (b);
                g.setColour (captionColour.withAlpha (0.6f));
                const float dashes[] { 5.0f, 4.0f };

                for (const auto& edge : { juce::Line<float> (b.getTopLeft(), b.getTopRight()), juce::Line<float> (b.getTopRight(), b.getBottomRight()),
                                          juce::Line<float> (b.getBottomRight(), b.getBottomLeft()), juce::Line<float> (b.getBottomLeft(), b.getTopLeft()) })
                    g.drawDashedLine (edge, dashes, 2, 1.0f);

                g.setFont (Theme::font (12.5f));
                g.drawFittedText (w.image.isNotEmpty() ? "Picture missing: " + w.image : juce::String ("Double-click to choose a picture"),
                                  b.reduced (6.0f).toNearestInt(), juce::Justification::centred, 3, 0.9f);
            }
            break;
        }

        case GuiWidget::Type::meter:
        case GuiWidget::Type::scope:
        case GuiWidget::Type::lamp:
        case GuiWidget::Type::envelope:
        case GuiWidget::Type::filter:
        case GuiWidget::Type::keyboard:
            break;   // drawn above
    }
}

void PluginCanvas::drawKeyboardAtRest (juce::Graphics& g, const GuiWidget& w)
{
    const auto b = w.bounds.toFloat();
    const auto low = w.lowNote, high = w.highNote;

    auto keyArea = [&] (int note)
    {
        const auto k = stella::keys::keyOf (note, low, high, b.getX(), b.getY(), b.getWidth(), b.getHeight());
        return juce::Rectangle<float> (k.x, k.y, k.w, k.h);
    };

    // The gaps between the white keys show the dark bed they sit in.
    g.setColour (juce::Colour (0xff111113));
    g.fillRect (b);

    for (int note = low; note <= high; ++note)
    {
        if (stella::keys::isBlack (note))
            continue;

        const auto key = keyArea (note).reduced (0.5f, 0.0f);
        juce::ColourGradient ivory (juce::Colour (0xffcfcac0), key.getX(), key.getY(), juce::Colour (0xfffbfaf6), key.getX(), key.getBottom(), false);
        ivory.addColour (0.18, juce::Colour (0xffefece5));
        g.setGradientFill (ivory);
        g.fillRoundedRectangle (key.withTrimmedBottom (1.0f), 2.0f);

        // The front lip, a shade darker.
        g.setColour (juce::Colour (0xffd8d3c9));
        g.fillRect (key.withTop (key.getBottom() - juce::jmax (2.0f, key.getHeight() * 0.05f)).withTrimmedBottom (1.0f));
    }

    for (int note = low; note <= high; ++note)
    {
        if (! stella::keys::isBlack (note))
            continue;

        const auto key = keyArea (note);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c31), key.getX(), key.getY(), juce::Colour (0xff09090b), key.getX(), key.getBottom(), false));
        g.fillRoundedRectangle (key, 1.5f);

        // Its top face, lit from above.
        const auto face = key.reduced (key.getWidth() * 0.14f, 0.0f).withTrimmedBottom (key.getHeight() * 0.12f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1b1f), face.getX(), face.getY(), juce::Colour (0xff3d3d44), face.getX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 1.0f);
    }

    // The felt strip's shadow along the top.
    const auto shadow = b.withHeight (juce::jmax (4.0f, b.getHeight() * 0.08f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), shadow.getX(), shadow.getY(),
                                             juce::Colours::transparentBlack, shadow.getX(), shadow.getBottom(), false));
    g.fillRect (shadow);
}

void PluginCanvas::drawKeyboard (juce::Graphics& g, const GuiWidget& w, int index, Part part)
{
    if (part != Part::moving)
        drawKeyboardAtRest (g, w);

    // The keys that are down (never baked: the plugin draws them live, the same way).
    if (part == Part::still || baking)
        return;

    const auto b = w.bounds.toFloat();
    const auto low = w.lowNote, high = w.highNote;
    const auto accent = w.colour.isTransparent() ? defaultAccent : w.colour;

    auto isDown = [&] (int note)
    {
        return (note == heldNote && index == heldKeyboard) || (note >= 0 && note < 128 && notesShown[(size_t) note]);
    };

    auto keyArea = [&] (int note)
    {
        const auto k = stella::keys::keyOf (note, low, high, b.getX(), b.getY(), b.getWidth(), b.getHeight());
        return juce::Rectangle<float> (k.x, k.y, k.w, k.h);
    };

    bool whiteDown = false;

    for (int note = low; note <= high; ++note)
    {
        if (stella::keys::isBlack (note) || ! isDown (note))
            continue;

        g.setColour (accent.withAlpha (0.55f));
        g.fillRect (keyArea (note).reduced (0.5f, 0.0f));
        whiteDown = true;
    }

    // The black keys lie on top: as they were, over a white key that went down.
    if (whiteDown)
    {
        juce::RectangleList<int> blacks;

        for (int note = low; note <= high; ++note)
            if (stella::keys::isBlack (note))
                blacks.addWithoutMerging (keyArea (note).getSmallestIntegerContainer());

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (blacks);
        drawKeyboardAtRest (g, w);
    }

    for (int note = low; note <= high; ++note)
    {
        if (! stella::keys::isBlack (note) || ! isDown (note))
            continue;

        g.setColour (accent.withAlpha (0.8f));
        g.fillRect (keyArea (note));
    }
}

void PluginCanvas::playKey (int widgetIndex, juce::Point<float> plugin)
{
    if (! juce::isPositiveAndBelow (widgetIndex, (int) layout.widgets.size()))
        return;

    const auto& w = layout.widgets[(size_t) widgetIndex];
    const auto b = w.bounds.toFloat();
    const auto note = stella::keys::noteAt (plugin.x, plugin.y, w.lowNote, w.highNote, b.getX(), b.getY(), b.getWidth(), b.getHeight());

    if (note == heldNote && widgetIndex == heldKeyboard)
        return;

    releaseKey();

    if (note < 0)
        return;

    heldNote = note;
    heldKeyboard = widgetIndex;

    if (onNote != nullptr)
        onNote (note, stella::keys::velocityAt (plugin.y, stella::keys::keyOf (note, w.lowNote, w.highNote, b.getX(), b.getY(), b.getWidth(), b.getHeight())));

    repaint (toView (w.bounds).getSmallestIntegerContainer().expanded (2));
}

void PluginCanvas::releaseKey()
{
    if (heldNote < 0)
        return;

    const auto note = heldNote;
    const auto keyboard = heldKeyboard;
    heldNote = heldKeyboard = -1;

    if (onNote != nullptr)
        onNote (note, 0.0f);

    if (juce::isPositiveAndBelow (keyboard, (int) layout.widgets.size()))
        repaint (toView (layout.widgets[(size_t) keyboard].bounds).getSmallestIntegerContainer().expanded (2));
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
        case GuiWidget::Type::preset:
        case GuiWidget::Type::keyboard:  return w.bounds;
        case GuiWidget::Type::label:
        case GuiWidget::Type::group:
        case GuiWidget::Type::shape:
        case GuiWidget::Type::image:     break;
    }

    return {};
}

void PluginCanvas::drawPanel (juce::Graphics& g)
{
    const auto plugin = juce::Rectangle<float> (0.0f, 0.0f, (float) layout.width, (float) layout.height);
    g.setGradientFill (juce::ColourGradient (layout.backgroundTop, 0.0f, 0.0f, layout.backgroundBottom, 0.0f, plugin.getBottom(), false));
    g.fillRect (plugin);

    if (layout.backgroundImage.isNotEmpty())
        drawPicture (g, layout.backgroundImage, plugin, layout.backgroundMode);
}

void PluginCanvas::drawPicture (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, const juce::String& mode)
{
    const auto& image = picture (name);

    if (! image.isValid() || area.isEmpty())
        return;

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.getSmallestIntegerContainer());
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (1.0f);

    if (mode == "tile")
    {
        g.setTiledImageFill (image, juce::roundToInt (area.getX()), juce::roundToInt (area.getY()), 1.0f);
        g.fillRect (area);
        return;
    }

    const auto placement = mode == "stretch" ? juce::RectanglePlacement (juce::RectanglePlacement::stretchToFit)
                         : mode == "fit"     ? juce::RectanglePlacement (juce::RectanglePlacement::centred)
                         : mode == "centre"  ? juce::RectanglePlacement (juce::RectanglePlacement::centred | juce::RectanglePlacement::doNotResize)
                                             : juce::RectanglePlacement (juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    g.drawImage (image, area, placement);
}

PluginCanvas::Bake PluginCanvas::bake()
{
    const juce::ScopedValueSetter<int> noHover (hovered, -1), noActive (active, -1);
    const juce::ScopedValueSetter<bool> bakingNow (baking, true);

    Bake result;
    result.background = juce::Image (juce::Image::ARGB, layout.width, layout.height, true);

    // Still things lying over something that moves (a frame over a meter, glass over a knob,
    // a caption on that glass) stay in front of it in the plugin too: layers of their own,
    // not part of the background.
    std::vector<bool> layered (layout.widgets.size(), false);

    for (size_t i = 0; i < layout.widgets.size(); ++i)
    {
        const auto& w = layout.widgets[i];
        const bool still = w.type == GuiWidget::Type::label || w.type == GuiWidget::Type::group || w.type == GuiWidget::Type::shape
                        || (w.type == GuiWidget::Type::image && w.image.isNotEmpty());

        for (size_t below = 0; still && below < i && ! layered[i]; ++below)
            layered[i] = movingAreaOf (layout.widgets[below]).intersects (w.bounds)
                      || (layered[below] && layout.widgets[below].bounds.intersects (w.bounds));
    }

    {
        juce::Graphics g (result.background);
        drawPanel (g);

        for (int i = 0; i < (int) layout.widgets.size(); ++i)
            if (! layered[(size_t) i])
                drawWidget (g, i, Part::still);
    }

    std::map<juce::String, int> lookStrips;   // controls with the same look and size share frames

    for (int i = 0; i < (int) layout.widgets.size(); ++i)
    {
        const auto& w = layout.widgets[(size_t) i];
        const auto area = layered[(size_t) i] ? w.bounds.getIntersection ({ 0, 0, layout.width, layout.height }) : movingAreaOf (w);
        result.stripOf.push_back (-1);
        result.movingArea.push_back (area);

        if (area.isEmpty())
            continue;

        // A layer in front of something that moves: one frame, drawn over it by the plugin.
        if (layered[(size_t) i])
        {
            Bake::Strip strip;
            strip.frameWidth = area.getWidth();
            strip.frameHeight = area.getHeight();
            strip.frames = 1;
            strip.image = juce::Image (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);

            {
                juce::Graphics g (strip.image);
                g.setOrigin ({ -area.getX(), -area.getY() });
                drawWidget (g, i, Part::still);
            }

            result.stripOf.back() = (int) result.strips.size();
            result.strips.push_back (strip);
            continue;
        }

        // Knobs, sliders and switches: their look's frames (a switch: off, on).
        if (Looks::takesLook (w) && looks != nullptr)
        {
            const auto kind = Looks::kindOf (w);
            const auto& look = looks->lookFor (w);
            const auto key = juce::String (look.revision) + "|" + Looks::kindName (kind) + "|"
                           + juce::String (area.getWidth()) + "x" + juce::String (area.getHeight());

            if (const auto found = lookStrips.find (key); found != lookStrips.end())
            {
                result.stripOf.back() = found->second;
                continue;
            }

            Bake::Strip strip;
            strip.image = looks->strip (look, kind, area.getWidth(), area.getHeight());
            strip.frameWidth = area.getWidth();
            strip.frameHeight = area.getHeight();
            strip.frames = Looks::framesFor (look, kind);

            lookStrips[key] = (int) result.strips.size();
            result.stripOf.back() = (int) result.strips.size();
            result.strips.push_back (strip);
            continue;
        }

        int frames = 0;

        switch (w.type)
        {
            case GuiWidget::Type::preset:    frames = juce::jmax (1, presetNames.size()); break;
            case GuiWidget::Type::lamp:      frames = 2; break;
            case GuiWidget::Type::selector:
            {
                const auto* p = paramFor (w);
                frames = juce::jmax (1, w.options.size() > 0 ? w.options.size() : (p != nullptr ? (int) (p->max - p->min) + 1 : 1));
                break;
            }
            case GuiWidget::Type::knob:
            case GuiWidget::Type::slider:
            case GuiWidget::Type::toggle:
            case GuiWidget::Type::label:
            case GuiWidget::Type::group:
            case GuiWidget::Type::meter:
            case GuiWidget::Type::scope:
            case GuiWidget::Type::envelope:
            case GuiWidget::Type::filter:
            case GuiWidget::Type::xy:
            case GuiWidget::Type::shape:
            case GuiWidget::Type::image:
            case GuiWidget::Type::keyboard:  break;   // its keys are in the background; the plugin draws the ones that are down
        }

        if (frames == 0)
            continue;

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

            drawWidget (g, i, Part::moving, (float) f);
        }

        result.stripOf.back() = (int) result.strips.size();
        result.strips.push_back (strip);
    }

    return result;
}

void PluginCanvas::paint (juce::Graphics& g)
{
    // A grid over the whole tab, FlowStone-style; it moves and zooms with the view.
    g.fillAll (Theme::panel);
    drawGrid (g);

    if (! hasPanel())
        return;

    const auto area = panelArea();

    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 18, { 0, 6 }).drawForRectangle (g, area.toNearestInt());

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area.toNearestInt());
        g.addTransform (juce::AffineTransform::scale (zoom).translated (area.getX(), area.getY()));

        drawPanel (g);

        if (design)
        {
            const auto plugin = juce::Rectangle<float> (0.0f, 0.0f, (float) layout.width, (float) layout.height);
            g.setColour (juce::Colours::white.withAlpha (0.035f));

            for (int x = 16; x < layout.width; x += 16)
                g.drawVerticalLine (x, 0.0f, plugin.getBottom());

            for (int y = 16; y < layout.height; y += 16)
                g.drawHorizontalLine (y, 0.0f, plugin.getRight());
        }

        for (int i = 0; i < (int) layout.widgets.size(); ++i)
            drawWidget (g, i);
    }

    g.setColour (design ? (panelSelected ? Theme::accent : Theme::accent.withAlpha (0.55f)) : Theme::outline);
    g.drawRect (area, design ? (panelSelected ? 2.0f : 1.5f) : 1.0f);

    // Which tab, and the window's size, above its top-left corner.
    {
        const auto tab = juce::String (design ? "EDIT" : "PLAY");
        const auto font = Theme::font (12.0f, true);
        const auto caption = juce::Rectangle<float> (area.getX(), area.getY() - 20.0f, 320.0f, 16.0f);

        g.setColour (design ? Theme::accent : Theme::safe);
        g.setFont (font);
        g.drawText (tab, caption, juce::Justification::centredLeft, false);

        g.setColour (Theme::muted);
        g.setFont (Theme::font (12.0f));
        g.drawText (juce::String (layout.width) + juce::String::fromUTF8 (" \xc3\x97 ") + juce::String (layout.height),
                    caption.withTrimmedLeft ((float) juce::GlyphArrangement::getStringWidthInt (font, tab) + 10.0f),
                    juce::Justification::centredLeft, false);
    }

    if (design)
    {
        // The window's own corner: drag it to resize the window.
        const auto grip = panelHandle();
        g.setColour (panelSelected || drag == Drag::resizePanel ? Theme::accent : Theme::accent.withAlpha (0.7f));
        juce::Path corner;
        corner.addTriangle (grip.getTopRight(), grip.getBottomRight(), grip.getBottomLeft());
        g.fillPath (corner);

        if (selected >= 0)
        {
            const auto box = toView (layout.widgets[(size_t) selected].bounds);
            g.setColour (Theme::accent);
            g.drawRect (box.expanded (2.0f), 1.5f);

            // Its handles: corners and sides.
            for (int handle = 0; handle < 8; ++handle)
            {
                if (const auto square = handleArea (handle); ! square.isEmpty())
                {
                    g.setColour (Theme::accent);
                    g.fillRect (square);
                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    g.drawRect (square, 1.0f);
                }
            }
        }
    }

    drawDropGhost (g);
    drawScrollbars (g);
    drawMinimap (g);
}
