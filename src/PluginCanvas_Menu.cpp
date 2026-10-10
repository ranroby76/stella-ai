// C:\workspace\Stella AI Studio\src\PluginCanvas_Menu.cpp
// The canvas's edit menu (an element's, or the window's) with its Stella AI instruction,
// the Stella AI banner, and the selection that opens them.

#include "PluginCanvas.h"
#include "PluginCanvasMenu.h"
#include "StellaLookAndFeel.h"
#include "stella_keys.h"

namespace
{
    const juce::StringArray meterModes { "peak", "rms" };
    const juce::StringArray meterModeNames { "Peak", "RMS" };
    const juce::StringArray filterModes { "lowpass", "lowpass24", "highpass", "highpass24", "bandpass" };
    const juce::StringArray filterModeNames { "Low-pass", "Low-pass 24 dB", "High-pass", "High-pass 24 dB", "Band-pass" };

    const GuiWidget::Type typeOrder[] { GuiWidget::Type::knob, GuiWidget::Type::slider, GuiWidget::Type::toggle,
                                        GuiWidget::Type::selector, GuiWidget::Type::xy, GuiWidget::Type::keyboard, GuiWidget::Type::preset,
                                        GuiWidget::Type::meter, GuiWidget::Type::scope, GuiWidget::Type::lamp,
                                        GuiWidget::Type::envelope, GuiWidget::Type::filter,
                                        GuiWidget::Type::image, GuiWidget::Type::label, GuiWidget::Type::group, GuiWidget::Type::shape };
    constexpr int numTypes = (int) (sizeof (typeOrder) / sizeof (typeOrder[0]));

    constexpr int headerHeight = 46, footerHeight = 118, pad = 12;
    const juce::Colour askYellow { 0xffffcc00 };   // as in the chat: what the user should answer or do

    juce::String pictureChoice (const juce::String& name)
    {
        return name.isNotEmpty() ? name : juce::String::fromUTF8 ("Choose picture\xe2\x80\xa6");
    }

    // A keyboard's lowest and highest key: the white keys from A0 to C8, the piano's range.
    constexpr int lowestPianoKey = 21, highestPianoKey = 108;

    void fillKeys (juce::ComboBox& box, int selected)
    {
        box.clear (juce::dontSendNotification);

        for (int note = lowestPianoKey; note <= highestPianoKey; ++note)
            if (! stella::keys::isBlack (note))
                box.addItem (GuiLayout::noteName (note) + (note == 60 ? "  (middle C)" : ""), note + 1);

        box.setSelectedId (selected + 1, juce::dontSendNotification);
    }
}

//==============================================================================
int PluginCanvas::ElementMenu::Rows::layOut (int width)
{
    int y = 6;

    for (auto& row : rows)
    {
        row.first->setVisible (row.shown);

        if (row.second != nullptr)
            row.second->setVisible (row.shown);

        if (! row.shown)
            continue;

        if (row.caption.isNotEmpty())
            y += 18;

        if (row.second != nullptr)
        {
            const auto firstWidth = juce::roundToInt ((float) (width - 8) * row.firstShare);
            row.first->setBounds (0, y, firstWidth, row.height);
            row.second->setBounds (firstWidth + 8, y, width - firstWidth - 8, row.height);
        }
        else
        {
            row.first->setBounds (0, y, juce::roundToInt ((float) width * row.firstShare), row.height);
        }

        y += row.height + 9;
    }

    setSize (width, y);
    return y;
}

void PluginCanvas::ElementMenu::Rows::paint (juce::Graphics& g)
{
    g.setFont (Theme::font (12.5f));

    for (const auto& row : rows)
    {
        if (! row.shown)
            continue;

        if (row.caption.isNotEmpty())
        {
            g.setColour (Theme::muted);
            g.drawText (row.caption, row.first->getBounds().translated (0, -18).withHeight (16).withWidth (getWidth()),
                        juce::Justification::centredLeft, true);
        }

        // Colour swatches get a frame, so dark colours still show where they are.
        for (auto* c : { row.first, row.second })
        {
            if (dynamic_cast<ColourSwatchButton*> (c) != nullptr)
            {
                g.setColour (Theme::outline.brighter (0.35f));
                g.drawRoundedRectangle (c->getBounds().toFloat().expanded (1.5f), 4.0f, 1.0f);
            }
        }
    }
}

void PluginCanvas::ElementMenu::SendButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto circle = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre()).reduced (1.0f);
    const auto centre = circle.getCentre();

    g.setColour (down ? Theme::accent.darker (0.2f) : (highlighted ? Theme::accent.brighter (0.15f) : Theme::accent));
    g.fillEllipse (circle);

    // An arrow pointing up, as in the chat.
    juce::Path arrow;
    const auto a = size * 0.22f;
    arrow.startNewSubPath (centre.x, centre.y + a);
    arrow.lineTo (centre.x, centre.y - a);
    arrow.startNewSubPath (centre.x - a * 0.8f, centre.y - a * 0.2f);
    arrow.lineTo (centre.x, centre.y - a);
    arrow.lineTo (centre.x + a * 0.8f, centre.y - a * 0.2f);
    g.setColour (juce::Colours::white);
    g.strokePath (arrow, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
PluginCanvas::ElementMenu::ElementMenu (PluginCanvas& owner)
    : canvas (owner)
{
    closeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
    closeButton.setTooltip ("Close (Esc)");
    closeButton.onClick = [this] { canvas.select (-1); };
    addAndMakeVisible (closeButton);

    for (int i = 0; i < numTypes; ++i)
        type.addItem (PluginCanvas::typeDisplayName (typeOrder[i]), i + 1);

    pictureMode.addItemList (GuiLayout::pictureModeNames(), 1);

    for (auto* box : { &type, &param, &source, &mode, &style, &pictureMode, &lowKey, &highKey })
        box->onChange = [this] { apply(); };

    for (auto& box : roles)
        box.onChange = [this] { apply(); };

    for (auto* editor : { &label, &options, &textSize, &widthBox, &heightBox })
    {
        editor->setFont (Theme::font (14.0f));
        editor->setIndents (6, 4);
    }

    label.onTextChange = [this] { apply(); };
    options.onTextChange = [this] { apply(); };
    options.setTextToShowWhenEmpty ("Saw, Square, Triangle", Theme::muted);

    textSize.setInputRestrictions (4, "0123456789.");
    textSize.setTextToShowWhenEmpty ("auto", Theme::muted);
    textSize.onReturnKey = [this] { apply(); };
    textSize.onFocusLost = [this] { apply(); };
    boldToggle.onClick = [this] { apply(); };

    colour.setTooltip ("Its colour (see-through: the standard one)");
    colour.onColourChange = [this] { apply(); };

    for (auto* box : { &widthBox, &heightBox })
    {
        box->setInputRestrictions (4, "0123456789");
        box->onReturnKey = [this] { applyWindow(); showFor (-1); };
        box->onFocusLost = [this] { applyWindow(); };
    }

    topColour.onColourChange = [this] { applyWindow(); };
    bottomColour.onColourChange = [this] { applyWindow(); };

    pictureButton.setTooltip ("Choose the picture it shows (PNG, JPEG or GIF): it's copied into the project");
    pictureButton.onClick = [this] { canvas.choosePicture (widget); };

    backgroundButton.setTooltip ("A picture behind everything, filling the window (it's copied into the project). "
                                 "Drag its corners and sides to stretch it; right-click it to change its layer");
    backgroundButton.setButtonText (juce::String::fromUTF8 ("Add a background picture\xe2\x80\xa6"));
    backgroundButton.onClick = [this] { canvas.choosePicture (-1); };

    knobStudioButton.setTooltip ("Open its look in the Knob Studio: layers, bevels, textures, the KnobMan gallery");
    knobStudioButton.onClick = [this]
    {
        if (widget >= 0 && canvas.onEditLook != nullptr)
            canvas.onEditLook (widget);
    };

    removeButton.setTooltip ("Delete it from the window (Delete key)");
    removeButton.onClick = [this] { canvas.removeSelected(); };

    // An element's rows, in the order they show...
    addRow ("Type", type);
    addRow ("Parameter", param);
    addRow ("Watches", source);

    for (auto& box : roles)
        addRow ("Role", box);

    addRow ("Mode", mode);
    addRow ("Text", label);
    addRow ("Text size, bold", textSize, &boldToggle, 0.4f);
    addRow ("Look", style);
    addRow ("Positions (comma separated)", options);
    addRow ("Keys (lowest, highest)", lowKey, &highKey, 0.5f);
    addRow ("Picture", pictureButton);
    addRow ("Picture fit", pictureMode);
    addRow ("Colour", colour, nullptr, 1.0f, 22);
    addRow ({}, knobStudioButton, nullptr, 1.0f, 28);
    addRow ({}, removeButton, nullptr, 0.4f, 28);

    // ...and the window's.
    addRow ("Window size (width, height)", widthBox, &heightBox, 0.5f);
    addRow ("Background colours (top, bottom)", topColour, &bottomColour, 0.5f, 22);
    addRow ("Background picture", backgroundButton);

    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    // Stella AI, at the foot.
    instruction.setMultiLine (true, true);
    instruction.setReturnKeyStartsNewLine (false);
    instruction.setFont (Theme::font (14.0f));
    instruction.setIndents (8, 6);
    instruction.onReturnKey = [this] { send(); };
    addAndMakeVisible (instruction);

    sendButton.setTooltip ("Send it to Stella AI (Enter)");
    sendButton.onClick = [this] { send(); };
    addAndMakeVisible (sendButton);
}

PluginCanvas::ElementMenu::~ElementMenu() = default;

void PluginCanvas::ElementMenu::addRow (const juce::String& caption, juce::Component& first, juce::Component* second, float firstShare, int height)
{
    rows.rows.push_back ({ caption, &first, second, firstShare, height, false });
    rows.addChildComponent (first);

    if (second != nullptr)
        rows.addChildComponent (*second);
}

void PluginCanvas::ElementMenu::setShown (juce::Component& first, bool shouldShow, const juce::String& caption)
{
    for (auto& row : rows.rows)
    {
        if (row.first != &first)
            continue;

        row.shown = shouldShow;

        if (caption.isNotEmpty())
            row.caption = caption;
    }
}

void PluginCanvas::ElementMenu::fillParams (juce::ComboBox& box, const juce::String& selectedId, bool withNone)
{
    box.clear (juce::dontSendNotification);

    if (withNone)
        box.addItem ("(none)", 1);

    for (int i = 0; i < canvas.params.size(); ++i)
        box.addItem (canvas.params[i].name + juce::String::fromUTF8 ("  \xc2\xb7  ") + canvas.params[i].id, i + 2);

    box.setSelectedId (1, juce::dontSendNotification);

    for (int i = 0; i < canvas.params.size(); ++i)
        if (canvas.params[i].id == selectedId)
            box.setSelectedId (i + 2, juce::dontSendNotification);
}

//==============================================================================
void PluginCanvas::ElementMenu::showFor (int widgetIndex)
{
    const juce::ScopedValueSetter<bool> quiet (updating, true);
    widget = juce::isPositiveAndBelow (widgetIndex, (int) canvas.layout.widgets.size()) ? widgetIndex : -1;

    for (auto& row : rows.rows)
        row.shown = false;

    if (widget < 0)
    {
        // The window itself.
        const auto& l = canvas.layout;
        title = "Plugin window";
        subtitle = juce::String (l.width) + juce::String::fromUTF8 (" \xc3\x97 ") + juce::String (l.height);

        widthBox.setText (juce::String (l.width), juce::dontSendNotification);
        heightBox.setText (juce::String (l.height), juce::dontSendNotification);
        topColour.setColourValue (l.backgroundTop, juce::dontSendNotification);
        bottomColour.setColourValue (l.backgroundBottom, juce::dontSendNotification);

        setShown (widthBox, true);
        setShown (topColour, true);
        setShown (backgroundButton, true);

        instruction.setTextToShowWhenEmpty ("e.g. a dark wooden look with brass knobs", Theme::muted);
    }
    else
    {
        const auto& w = canvas.layout.widgets[(size_t) widget];
        const auto t = w.type;
        title = PluginCanvas::typeDisplayName (t);
        subtitle = t == GuiWidget::Type::image ? w.image : w.label;

        for (int i = 0; i < numTypes; ++i)
            if (typeOrder[i] == t)
                type.setSelectedId (i + 1, juce::dontSendNotification);

        setShown (type, true);

        if (w.isControl())
        {
            fillParams (param, w.param, true);
            setShown (param, true);
        }

        if (w.isLive())
        {
            source.clear (juce::dontSendNotification);
            juce::StringArray sources (canvas.signalSources);
            sources.addArray (canvas.displaySources);

            if (w.source.isNotEmpty())
                sources.addIfNotAlreadyThere (w.source);

            source.addItemList (sources, 1);
            source.setSelectedId (sources.indexOf (w.source) + 1, juce::dontSendNotification);
            setShown (source, true);
        }

        roleNames = GuiWidget::rolesOf (t);

        for (int i = 0; i < 4; ++i)
        {
            if (i >= roleNames.size())
                continue;

            const auto found = w.roles.find (roleNames[i]);
            fillParams (roles[i], found != w.roles.end() ? found->second : juce::String(), true);
            setShown (roles[i], true, roleNames[i].substring (0, 1).toUpperCase() + roleNames[i].substring (1));
        }

        mode.clear (juce::dontSendNotification);

        if (t == GuiWidget::Type::meter)
        {
            mode.addItemList (meterModeNames, 1);
            mode.setSelectedId (juce::jmax (0, meterModes.indexOf (w.mode)) + 1, juce::dontSendNotification);
            setShown (mode, true);
        }
        else if (t == GuiWidget::Type::filter)
        {
            mode.addItemList (filterModeNames, 1);
            mode.setSelectedId (juce::jmax (0, filterModes.indexOf (w.mode)) + 1, juce::dontSendNotification);
            setShown (mode, true);
        }

        const bool isText = t == GuiWidget::Type::label || t == GuiWidget::Type::group;
        const bool hasCaption = t != GuiWidget::Type::image && t != GuiWidget::Type::shape && t != GuiWidget::Type::preset
                             && t != GuiWidget::Type::keyboard;

        label.setText (w.label, juce::dontSendNotification);
        setShown (label, hasCaption, isText ? "Text" : "Caption");

        textSize.setText (w.fontSize > 0.0f ? juce::String (w.fontSize, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") : juce::String(),
                          juce::dontSendNotification);
        boldToggle.setToggleState (w.bold, juce::dontSendNotification);
        setShown (textSize, isText);

        if (Looks::takesLook (w) && canvas.looks != nullptr)
        {
            // Every look of its kind: the plugin's own, mine, then the built-in ones.
            style.clear (juce::dontSendNotification);
            lookNames.clear();

            const auto shown = canvas.looks->lookFor (w).name;
            auto origin = Looks::Origin::builtIn;
            bool first = true;

            for (const auto* look : canvas.looks->listFor (Looks::kindOf (w)))
            {
                if (first || look->origin != origin)
                {
                    origin = look->origin;
                    style.addSectionHeading (origin == Looks::Origin::project ? "This plugin's own"
                                           : origin == Looks::Origin::mine    ? "My looks"
                                                                              : "Built in");
                    first = false;
                }

                lookNames.add (look->name);
                style.addItem (look->name, lookNames.size());
            }

            style.setSelectedId (lookNames.indexOf (shown) + 1, juce::dontSendNotification);
            setShown (style, true);
            setShown (knobStudioButton, true);
        }

        options.setText (w.options.joinIntoString (", "), juce::dontSendNotification);
        setShown (options, t == GuiWidget::Type::selector);

        if (t == GuiWidget::Type::keyboard)
        {
            fillKeys (lowKey, w.lowNote);
            fillKeys (highKey, w.highNote);
            setShown (lowKey, true);
        }

        if (t == GuiWidget::Type::image)
        {
            pictureButton.setButtonText (pictureChoice (w.image));
            pictureMode.setSelectedId (juce::jmax (0, GuiLayout::pictureModes().indexOf (w.mode.isNotEmpty() ? w.mode : juce::String ("fit"))) + 1,
                                       juce::dontSendNotification);
            setShown (pictureButton, true);
            setShown (pictureMode, true);
        }

        colour.setColourValue (w.colour, juce::dontSendNotification);
        setShown (colour, ! Looks::takesLook (w) && t != GuiWidget::Type::image && t != GuiWidget::Type::meter,
                  t == GuiWidget::Type::keyboard ? "Colour of the keys that are down" : "Colour");
        setShown (removeButton, true);

        instruction.setTextToShowWhenEmpty ("e.g. make it bigger and gold", Theme::muted);
    }

    resized();
    repaint();
}

int PluginCanvas::ElementMenu::getIdealHeight() const
{
    int rowsHeight = 6;

    for (const auto& row : rows.rows)
        if (row.shown)
            rowsHeight += (row.caption.isNotEmpty() ? 18 : 0) + row.height + 9;

    return headerHeight + rowsHeight + footerHeight;
}

void PluginCanvas::ElementMenu::resized()
{
    auto area = getLocalBounds();

    headerArea = area.removeFromTop (headerHeight);
    closeButton.setBounds (headerArea.withTrimmedRight (8).removeFromRight (30).withSizeKeepingCentre (30, 28));

    footerArea = area.removeFromBottom (footerHeight);

    // The rows scroll when the menu is shorter than they are.
    viewport.setBounds (area.reduced (pad, 0));
    auto width = viewport.getWidth();

    if (rows.layOut (width) > viewport.getHeight())
        rows.layOut (width - viewport.getScrollBarThickness() - 4);

    auto foot = footerArea.reduced (pad, 0);
    foot.removeFromTop (34);
    auto box = foot.removeFromTop (66);
    sendButton.setBounds (box.removeFromRight (34).removeFromBottom (34));
    box.removeFromRight (6);
    instruction.setBounds (box);
}

void PluginCanvas::ElementMenu::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();

    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 16, { 0, 4 }).drawForRectangle (g, getLocalBounds());
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (box, 9.0f);
    g.setColour (Theme::outline.brighter (0.15f));
    g.drawRoundedRectangle (box.reduced (0.5f), 9.0f, 1.0f);

    // What it edits.
    auto header = headerArea.reduced (pad, 0).withTrimmedRight (34);
    const auto titleFont = Theme::font (15.5f, true);
    g.setColour (Theme::text);
    g.setFont (titleFont);
    g.drawText (title, header, juce::Justification::centredLeft, true);

    if (subtitle.isNotEmpty())
    {
        const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (titleFont, title) + 10;
        g.setColour (Theme::muted);
        g.setFont (Theme::font (13.5f));
        g.drawText (subtitle, header.withTrimmedLeft (titleWidth), juce::Justification::centredLeft, true);
    }

    g.setColour (Theme::outline);
    g.fillRect (pad, headerArea.getBottom() - 1, getWidth() - pad * 2, 1);
    g.fillRect (pad, footerArea.getY(), getWidth() - pad * 2, 1);

    // Stella AI's corner.
    g.setColour (askYellow);
    g.setFont (Theme::font (13.5f, true));
    g.drawText (widget < 0 ? "Tell Stella AI what to do with the window" : "Tell Stella AI what to do with it",
                footerArea.reduced (pad, 0).removeFromTop (34), juce::Justification::centredLeft, true);
}

//==============================================================================
void PluginCanvas::ElementMenu::apply()
{
    if (updating || ! juce::isPositiveAndBelow (widget, (int) canvas.layout.widgets.size()))
        return;

    auto& w = canvas.layout.widgets[(size_t) widget];
    const auto newType = typeOrder[juce::jlimit (0, numTypes - 1, type.getSelectedId() - 1)];

    if (newType != w.type)
    {
        w.bounds = PluginCanvas::defaultBounds (newType, w.bounds.getX(), w.bounds.getY());
        w.type = newType;
        w.roles.clear();
        w.mode.clear();
        w.style.clear();

        if (Looks::takesLook (w))
            w.style = canvas.commonLook (w);   // the look its new kind mostly wears here

        if (newType == GuiWidget::Type::shape && w.path.isEmpty())
            w.path = PluginCanvas::defaultStarPath();

        canvas.edited();
        showFor (widget);
        canvas.layOutOverlays();
        return;
    }

    auto paramAt = [this] (const juce::ComboBox& box)
    {
        const auto index = box.getSelectedId() - 2;
        return juce::isPositiveAndBelow (index, canvas.params.size()) ? canvas.params[index].id : juce::String();
    };

    if (w.isControl())
        w.param = paramAt (param);

    if (w.isLive())
        w.source = source.getText();

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
    else if (w.type == GuiWidget::Type::image)
        w.mode = GuiLayout::pictureModes()[juce::jmax (0, pictureMode.getSelectedId() - 1)];

    if (w.type != GuiWidget::Type::image && w.type != GuiWidget::Type::shape && w.type != GuiWidget::Type::preset
        && w.type != GuiWidget::Type::keyboard)
        w.label = label.getText();

    if (w.type == GuiWidget::Type::keyboard)
    {
        // The highest key stays above the lowest, at least an octave up.
        w.lowNote = juce::jmax (0, lowKey.getSelectedId() - 1);
        w.highNote = juce::jmax (0, highKey.getSelectedId() - 1);
        stella::keys::normalise (w.lowNote, w.highNote);

        if (highKey.getSelectedId() != w.highNote + 1)
            fillKeys (highKey, w.highNote);
    }

    if (Looks::takesLook (w) && juce::isPositiveAndBelow (style.getSelectedId() - 1, lookNames.size()))
        w.style = lookNames[style.getSelectedId() - 1];

    if (w.type == GuiWidget::Type::selector)
    {
        w.options = juce::StringArray::fromTokens (options.getText(), ",", "");
        w.options.trim();
        w.options.removeEmptyStrings();
    }

    if (w.type == GuiWidget::Type::label || w.type == GuiWidget::Type::group)
    {
        w.fontSize = juce::jlimit (0.0f, 200.0f, textSize.getText().getFloatValue());
        w.bold = boldToggle.getToggleState();
    }

    if (! Looks::takesLook (w) && w.type != GuiWidget::Type::image && w.type != GuiWidget::Type::meter)
        w.colour = colour.getColourValue();

    subtitle = w.type == GuiWidget::Type::image ? w.image : w.label;
    repaint (headerArea);
    canvas.edited();
}

void PluginCanvas::ElementMenu::applyWindow()
{
    if (updating || widget >= 0)
        return;

    auto& l = canvas.layout;
    const auto newWidth = juce::jlimit (GuiLayout::minWidth, GuiLayout::maxWidth, widthBox.getText().getIntValue());
    const auto newHeight = juce::jlimit (GuiLayout::minHeight, GuiLayout::maxHeight, heightBox.getText().getIntValue());
    const bool resizedWindow = newWidth != l.width || newHeight != l.height;

    // A picture filling the window keeps filling it.
    const juce::Rectangle<int> oldWindow (0, 0, l.width, l.height);

    for (auto& w : l.widgets)
        if (w.type == GuiWidget::Type::image && w.bounds == oldWindow)
            w.bounds = { 0, 0, newWidth, newHeight };

    l.width = newWidth;
    l.height = newHeight;
    l.backgroundTop = topColour.getColourValue();
    l.backgroundBottom = bottomColour.getColourValue();

    subtitle = juce::String (l.width) + juce::String::fromUTF8 (" \xc3\x97 ") + juce::String (l.height);
    repaint (headerArea);
    canvas.edited();

    if (resizedWindow)
        canvas.viewChanged();
}

void PluginCanvas::ElementMenu::send()
{
    const auto text = instruction.getText().trim();

    if (text.isEmpty() || canvas.onAskAi == nullptr)
        return;

    instruction.clear();
    canvas.onAskAi (widget, text);
}

//==============================================================================
PluginCanvas::AiBanner::AiBanner (PluginCanvas& owner)
    : canvas (owner)
{
    chatButton.setTooltip ("See the whole conversation in Build with AI");
    chatButton.onClick = [this] { if (canvas.onOpenChat != nullptr) canvas.onOpenChat(); };

    closeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
    closeButton.onClick = [this]
    {
        stopTimer();
        setVisible (false);
    };

    addAndMakeVisible (chatButton);
    addAndMakeVisible (closeButton);
}

PluginCanvas::AiBanner::~AiBanner()
{
    stopTimer();
}

void PluginCanvas::AiBanner::showWorking (const juce::String& what)
{
    text = what;
    working = true;
    dots = 0;
    startTimerHz (3);
    setVisible (true);
    canvas.layOutOverlays();
    repaint();
}

void PluginCanvas::AiBanner::showReply (const juce::String& reply)
{
    text = reply.trim().isNotEmpty() ? reply.trim() : juce::String ("Done.");
    working = false;
    stopTimer();
    setVisible (true);
    canvas.layOutOverlays();
    repaint();
}

void PluginCanvas::AiBanner::timerCallback()
{
    dots = (dots + 1) % 4;
    repaint();
}

juce::AttributedString PluginCanvas::AiBanner::makeText() const
{
    juce::AttributedString attributed;
    attributed.setWordWrap (juce::AttributedString::byWord);
    attributed.setJustification (juce::Justification::topLeft);
    attributed.setLineSpacing (2.0f);

    if (working)
    {
        attributed.append ("Stella AI is working on " + text + juce::String::repeatedString (".", dots), Theme::font (14.0f), Theme::text);
        return attributed;
    }

    // Short markdown, as in the chat: **bold**, and ==what the user should answer or do== in yellow.
    attributed.append ("Stella AI  ", Theme::font (14.0f, true), Theme::accent.brighter (0.2f));

    bool bold = false, ask = false;
    juce::String run;

    auto flush = [&]
    {
        if (run.isNotEmpty())
            attributed.append (run, Theme::font (14.0f, bold), ask ? askYellow : Theme::text);

        run.clear();
    };

    for (int i = 0; i < text.length();)
    {
        const auto pair = text.substring (i, i + 2);

        if (pair == "**" || pair == "==")
        {
            flush();
            (pair == "**" ? bold : ask) = ! (pair == "**" ? bold : ask);
            i += 2;
            continue;
        }

        run += text.substring (i, i + 1);
        ++i;
    }

    flush();
    return attributed;
}

int PluginCanvas::AiBanner::getIdealHeight (int width) const
{
    juce::TextLayout textLayout;
    textLayout.createLayout (makeText(), (float) juce::jmax (100, width - 28 - 130));
    return juce::jlimit (46, 180, juce::roundToInt (textLayout.getHeight()) + 26);
}

void PluginCanvas::AiBanner::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);

    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 14, { 0, 3 }).drawForRectangle (g, getLocalBounds());
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (box, 9.0f);
    g.setColour (working ? Theme::hot.withAlpha (0.7f) : Theme::accent.withAlpha (0.55f));
    g.drawRoundedRectangle (box, 9.0f, 1.2f);

    juce::TextLayout textLayout;
    const auto area = getLocalBounds().toFloat().reduced (14.0f, 13.0f).withTrimmedRight (124.0f);
    textLayout.createLayout (makeText(), area.getWidth());
    textLayout.draw (g, area);
}

void PluginCanvas::AiBanner::resized()
{
    auto top = getLocalBounds().reduced (10, 9).removeFromTop (28);
    closeButton.setBounds (top.removeFromRight (28));
    top.removeFromRight (6);
    chatButton.setBounds (top.removeFromRight (88));
}

//==============================================================================
void PluginCanvas::showAiWorking (const juce::String& what)
{
    banner->showWorking (what);
    banner->toFront (false);
}

void PluginCanvas::showAiReply (const juce::String& reply)
{
    banner->showReply (reply);
    banner->toFront (false);
}

void PluginCanvas::select (int index)
{
    selected = design && juce::isPositiveAndBelow (index, (int) layout.widgets.size()) ? index : -1;
    panelSelected = false;

    if (selected < 0)
        closeMenu();
    else if (menuOpen)
        openMenu();   // the menu follows the selection

    repaint();
}

void PluginCanvas::selectPanel()
{
    selected = -1;
    panelSelected = design && hasPanel();

    if (! panelSelected)
        closeMenu();
    else if (menuOpen)
        openMenu();

    repaint();
}

void PluginCanvas::openMenu()
{
    if (! design || (selected < 0 && ! panelSelected))
    {
        closeMenu();
        return;
    }

    menu->showFor (panelSelected ? -1 : selected);
    menuOpen = true;
    menu->setVisible (true);
    layOutOverlays();
    menu->toFront (false);
}

void PluginCanvas::closeMenu()
{
    menuOpen = false;

    if (menu != nullptr)
        menu->setVisible (false);
}

void PluginCanvas::layOutOverlays()
{
    const auto view = viewArea().toNearestInt();

    // The zoom, bottom left, just above the scrollbar.
    auto bar = juce::Rectangle<int> (view.getX() + 10, view.getBottom() - 36, 236, 26);
    fitButton.setBounds (bar.removeFromLeft (46));
    bar.removeFromLeft (8);
    zoomSlider.setBounds (bar.removeFromLeft (124));
    bar.removeFromLeft (6);
    zoomLabel.setBounds (bar);

    // Stella AI's banner, across the top.
    if (banner->isVisible())
    {
        const auto width = juce::jmin (620, view.getWidth() - 40);
        banner->setBounds (view.getCentreX() - width / 2, view.getY() + 10, width, banner->getIdealHeight (width));
    }

    // The edit menu, beside what it edits: on its right if there's room, otherwise on its left,
    // otherwise along the canvas's right edge.
    if (menuOpen && menu->isVisible())
    {
        const auto target = panelSelected || ! juce::isPositiveAndBelow (selected, (int) layout.widgets.size())
                                ? panelArea()
                                : toView (layout.widgets[(size_t) selected].bounds);
        const auto width = ElementMenu::menuWidth;
        const auto height = juce::jmin (menu->getIdealHeight(), view.getHeight() - 20);

        auto x = (int) target.getRight() + 14;

        if (x + width > view.getRight() - 10)
            x = (int) target.getX() - 14 - width;

        if (x < view.getX() + 10)
            x = view.getRight() - 10 - width;

        const auto y = juce::jlimit (view.getY() + 10, juce::jmax (view.getY() + 10, view.getBottom() - 10 - height), (int) target.getY());
        menu->setBounds (x, y, width, height);
    }
}
