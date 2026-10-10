// C:\workspace\Stella AI Studio\src\PluginCanvas_Mouse.cpp
// The canvas's view, after Colosseum's rack (GraphCanvas): zoom and pan, the minimap and the
// scrollbars; then the mouse, the keys, and what's dragged in (primitives and pictures).

#include "PluginCanvas.h"
#include "PluginCanvasMenu.h"
#include "StellaLookAndFeel.h"

namespace
{
    constexpr float scrollbarThickness = 12.0f;
    constexpr float minimapWidth = 200.0f, minimapMaxHeight = 140.0f, minimapMargin = 8.0f;
    constexpr float worldMargin = 1200.0f;     // how far past the window the view can go
    constexpr float scrollSpeed = 120.0f;      // wheel scrolling, in view pixels per wheel unit (as in Colosseum)
    const juce::Colour mapControl { 0xffd8d4ca };
}

//==============================================================================
// The view
juce::Rectangle<float> PluginCanvas::viewArea() const
{
    auto area = getLocalBounds().toFloat();

    if (hasPanel())
        area = area.withTrimmedRight (scrollbarThickness).withTrimmedBottom (scrollbarThickness);

    return area;
}

juce::Rectangle<float> PluginCanvas::panelArea() const
{
    return toView ({ 0, 0, layout.width, layout.height });
}

juce::Point<float> PluginCanvas::toPlugin (juce::Point<float> view) const
{
    return view / zoom + pan;
}

juce::Rectangle<float> PluginCanvas::toView (juce::Rectangle<int> plugin) const
{
    return { ((float) plugin.getX() - pan.x) * zoom, ((float) plugin.getY() - pan.y) * zoom,
             (float) plugin.getWidth() * zoom, (float) plugin.getHeight() * zoom };
}

juce::Rectangle<float> PluginCanvas::worldBounds() const
{
    const auto margin = juce::jmax (worldMargin, (float) juce::jmax (layout.width, layout.height));
    return juce::Rectangle<float> (0.0f, 0.0f, (float) layout.width, (float) layout.height).expanded (margin);
}

void PluginCanvas::clampPan()
{
    const auto world = worldBounds();
    const auto view = viewArea();

    auto clampAxis = [] (float value, float start, float size, float visible)
    {
        if (visible >= size)
            return start + (size - visible) * 0.5f;   // all of it shows: keep it centred

        return juce::jlimit (start, start + size - visible, value);
    };

    pan.x = clampAxis (pan.x, world.getX(), world.getWidth(), view.getWidth() / zoom);
    pan.y = clampAxis (pan.y, world.getY(), world.getHeight(), view.getHeight() / zoom);
}

void PluginCanvas::centreOn (juce::Point<float> plugin)
{
    pan = plugin - viewArea().getCentre() / zoom;
    clampPan();
}

void PluginCanvas::setZoom (float newZoom, std::optional<juce::Point<float>> around)
{
    newZoom = juce::jlimit (minZoom, maxZoom, newZoom);

    if (std::abs (newZoom - zoom) < 0.0005f)
    {
        updateZoomControls();
        return;
    }

    // The plugin pixel under the point stays under it.
    const auto anchor = around.value_or (viewArea().getCentre());
    const auto under = toPlugin (anchor);

    zoom = newZoom;
    pan = under - anchor / zoom;
    clampPan();
    viewChanged();
}

void PluginCanvas::fitToView()
{
    const auto view = viewArea().reduced (44.0f, 44.0f).withTrimmedTop (12.0f);   // room for the caption above the window

    if (view.isEmpty() || ! hasPanel())
    {
        autoFit = true;
        return;
    }

    zoom = juce::jlimit (minZoom, 1.0f, juce::jmin (view.getWidth() / (float) layout.width, view.getHeight() / (float) layout.height));
    pan = juce::Point<float> ((float) layout.width, (float) layout.height) * 0.5f - view.getCentre() / zoom;
    clampPan();
    autoFit = true;
    viewChanged();
}

void PluginCanvas::viewChanged()
{
    clampPan();
    updateZoomControls();
    layOutOverlays();
    saveViewSoon();
    repaint();
}

void PluginCanvas::updateZoomControls()
{
    zoomSlider.setValue (zoom, juce::dontSendNotification);
    zoomLabel.setText (juce::String (juce::roundToInt (zoom * 100.0f)) + "%", juce::dontSendNotification);
    zoomSlider.setColour (juce::Slider::thumbColourId, std::abs (zoom - 1.0f) < 0.005f ? Theme::accent : Theme::text);
}

void PluginCanvas::loadView()
{
    if (! hasPanel())
        return;

    const auto saved = juce::JSON::parse (guiFolder.getChildFile ("view.json"));

    if (! saved.isObject() || ! saved.hasProperty ("zoom"))
        return;

    zoom = juce::jlimit (minZoom, maxZoom, (float) (double) saved.getProperty ("zoom", 1.0));
    pan = { (float) (double) saved.getProperty ("x", 0.0), (float) (double) saved.getProperty ("y", 0.0) };
    autoFit = false;
}

void PluginCanvas::saveViewSoon()
{
    // Only a view the user chose is kept; the automatic one comes back by itself.
    if (autoFit || viewSaveScheduled || ! hasPanel())
        return;

    viewSaveScheduled = true;

    juce::Timer::callAfterDelay (800, [safeThis = juce::Component::SafePointer<PluginCanvas> (this), folder = guiFolder]
    {
        if (safeThis == nullptr)
            return;

        safeThis->viewSaveScheduled = false;

        if (safeThis->guiFolder != folder || safeThis->autoFit || ! folder.isDirectory())
            return;

        auto* object = new juce::DynamicObject();
        const juce::var view (object);
        object->setProperty ("zoom", safeThis->zoom);
        object->setProperty ("x", safeThis->pan.x);
        object->setProperty ("y", safeThis->pan.y);
        folder.getChildFile ("view.json").replaceWithText (juce::JSON::toString (view));
    });
}

void PluginCanvas::followPosition()
{
    // The toolbox showing or hiding moves the canvas's left edge: the window stays where it
    // was on screen.
    if (getX() != lastX && ! autoFit)
        pan.x += (float) (getX() - lastX) / zoom;

    lastX = getX();
}

void PluginCanvas::moved()
{
    followPosition();

    if (! autoFit)
    {
        clampPan();
        viewChanged();
    }
}

void PluginCanvas::resized()
{
    followPosition();

    if (autoFit)
        fitToView();
    else
    {
        clampPan();
        viewChanged();
    }
}

//==============================================================================
// The minimap and the scrollbars, drawn over the canvas
juce::Rectangle<float> PluginCanvas::minimapArea() const
{
    const auto world = worldBounds();
    const auto height = juce::jmin (minimapMaxHeight, minimapWidth * world.getHeight() / world.getWidth());
    const auto width = height * world.getWidth() / world.getHeight();
    const auto view = viewArea();

    return { view.getRight() - width - minimapMargin, view.getBottom() - height - minimapMargin, width, height };
}

juce::Rectangle<float> PluginCanvas::hScrollArea() const
{
    const auto view = viewArea();
    return { view.getX(), view.getBottom(), view.getWidth(), scrollbarThickness };
}

juce::Rectangle<float> PluginCanvas::vScrollArea() const
{
    const auto view = viewArea();
    return { view.getRight(), view.getY(), scrollbarThickness, view.getHeight() };
}

juce::Rectangle<float> PluginCanvas::hThumb() const
{
    const auto world = worldBounds();
    const auto track = hScrollArea().reduced (2.0f);
    const auto visible = viewArea().getWidth() / zoom;
    const auto ratio = juce::jlimit (0.0f, 1.0f, visible / world.getWidth());
    const auto thumbWidth = juce::jmax (24.0f, track.getWidth() * ratio);
    const auto range = world.getWidth() - visible;
    const auto position = range > 0.0f ? juce::jlimit (0.0f, 1.0f, (pan.x - world.getX()) / range) : 0.0f;

    return { track.getX() + (track.getWidth() - thumbWidth) * position, track.getY(), thumbWidth, track.getHeight() };
}

juce::Rectangle<float> PluginCanvas::vThumb() const
{
    const auto world = worldBounds();
    const auto track = vScrollArea().reduced (2.0f);
    const auto visible = viewArea().getHeight() / zoom;
    const auto ratio = juce::jlimit (0.0f, 1.0f, visible / world.getHeight());
    const auto thumbHeight = juce::jmax (24.0f, track.getHeight() * ratio);
    const auto range = world.getHeight() - visible;
    const auto position = range > 0.0f ? juce::jlimit (0.0f, 1.0f, (pan.y - world.getY()) / range) : 0.0f;

    return { track.getX(), track.getY() + (track.getHeight() - thumbHeight) * position, track.getWidth(), thumbHeight };
}

juce::Rectangle<float> PluginCanvas::panelHandle() const
{
    const auto area = panelArea();
    return { area.getRight() - 12.0f, area.getBottom() - 12.0f, 12.0f, 12.0f };
}

juce::Rectangle<float> PluginCanvas::handleArea (int handle) const
{
    if (! juce::isPositiveAndBelow (selected, (int) layout.widgets.size()) || handle < 0 || handle > 7)
        return {};

    const auto& w = layout.widgets[(size_t) selected];
    const auto box = toView (w.bounds).expanded (2.0f);
    const bool side = (handle % 2) == 1;

    // Sides: not on knobs (they stay round), nor where they'd crowd the corners.
    if (side && (w.type == GuiWidget::Type::knob
                 || ((handle == 1 || handle == 5) && box.getWidth() < handleSize * 4.0f)
                 || ((handle == 3 || handle == 7) && box.getHeight() < handleSize * 4.0f)))
        return {};

    const juce::Point<float> points[] { box.getTopLeft(), { box.getCentreX(), box.getY() }, box.getTopRight(),
                                        { box.getRight(), box.getCentreY() }, box.getBottomRight(),
                                        { box.getCentreX(), box.getBottom() }, box.getBottomLeft(), { box.getX(), box.getCentreY() } };

    return juce::Rectangle<float> (handleSize, handleSize).withCentre (points[handle]);
}

int PluginCanvas::handleAt (juce::Point<float> view) const
{
    // Corners first: they win where they meet the sides.
    for (const auto handle : { 0, 2, 4, 6, 1, 3, 5, 7 })
        if (const auto area = handleArea (handle); ! area.isEmpty() && area.expanded (3.0f).contains (view))
            return handle;

    return -1;
}

bool PluginCanvas::isBackdrop (const GuiWidget& w) const
{
    return w.type == GuiWidget::Type::image && w.bounds.contains (juce::Rectangle<int> (0, 0, layout.width, layout.height));
}

void PluginCanvas::reorder (int index, Order order)
{
    if (! juce::isPositiveAndBelow (index, (int) layout.widgets.size()))
        return;

    const auto last = (int) layout.widgets.size() - 1;
    const auto target = order == Order::front    ? last
                      : order == Order::back     ? 0
                      : order == Order::forward  ? juce::jmin (last, index + 1)
                                                 : juce::jmax (0, index - 1);

    if (target == index)
        return;

    releaseKey();

    const auto moving = layout.widgets[(size_t) index];
    layout.widgets.erase (layout.widgets.begin() + index);
    layout.widgets.insert (layout.widgets.begin() + target, moving);
    meterLevels.clear();   // kept by element index

    select (target);
    edited();
}

void PluginCanvas::showElementPopup (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) layout.widgets.size()))
        return;

    const auto last = (int) layout.widgets.size() - 1;

    auto item = [] (int id, const char* text, bool enabled, const char* keys)
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.isEnabled = enabled;
        i.shortcutKeyDescription = keys;
        return i;
    };

    juce::PopupMenu menuItems;
    menuItems.addSectionHeader (typeDisplayName (layout.widgets[(size_t) index].type));
    menuItems.addItem (item (1, "Bring to front", index < last, "Ctrl+Shift+]"));
    menuItems.addItem (item (2, "Bring forward", index < last, "Ctrl+]"));
    menuItems.addItem (item (3, "Send backward", index > 0, "Ctrl+["));
    menuItems.addItem (item (4, "Send to back", index > 0, "Ctrl+Shift+["));
    menuItems.addSeparator();
    menuItems.addItem (item (5, "Edit", true, ""));
    menuItems.addItem (item (6, "Delete", true, "Del"));
    menuItems.addSeparator();
    menuItems.addItem (item (7, "Window size and colours", true, ""));

    menuItems.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                             [safeThis = juce::Component::SafePointer<PluginCanvas> (this), index] (int choice)
                             {
                                 if (safeThis == nullptr || ! juce::isPositiveAndBelow (index, (int) safeThis->layout.widgets.size()))
                                     return;

                                 safeThis->grabKeyboardFocus();   // the menu took it: the layer keys work again

                                 switch (choice)
                                 {
                                     case 1: safeThis->reorder (index, Order::front); break;
                                     case 2: safeThis->reorder (index, Order::forward); break;
                                     case 3: safeThis->reorder (index, Order::backward); break;
                                     case 4: safeThis->reorder (index, Order::back); break;
                                     case 5: safeThis->select (index); safeThis->openMenu(); break;
                                     case 6: safeThis->select (index); safeThis->removeSelected(); break;
                                     case 7: safeThis->selectPanel(); safeThis->openMenu(); break;
                                     default: break;
                                 }
                             });
}

void PluginCanvas::showWindowPopup()
{
    juce::PopupMenu menuItems;
    menuItems.addSectionHeader ("Plugin window");
    menuItems.addItem (1, "Window size and colours");
    menuItems.addItem (2, juce::String::fromUTF8 ("Add a background picture\xe2\x80\xa6"));

    menuItems.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                             [safeThis = juce::Component::SafePointer<PluginCanvas> (this)] (int choice)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 safeThis->grabKeyboardFocus();

                                 if (choice == 1)
                                 {
                                     safeThis->selectPanel();
                                     safeThis->openMenu();
                                 }
                                 else if (choice == 2)
                                 {
                                     safeThis->choosePicture (-1);
                                 }
                             });
}

void PluginCanvas::navigateMinimapTo (juce::Point<float> view)
{
    const auto map = minimapArea();
    const auto world = worldBounds();
    const auto x = juce::jlimit (0.0f, 1.0f, (view.x - map.getX()) / map.getWidth());
    const auto y = juce::jlimit (0.0f, 1.0f, (view.y - map.getY()) / map.getHeight());

    autoFit = false;
    centreOn ({ world.getX() + world.getWidth() * x, world.getY() + world.getHeight() * y });
    viewChanged();
}

void PluginCanvas::drawGrid (juce::Graphics& g)
{
    // Fine lines every 16 plugin pixels, stronger every 64, lined up with the window;
    // lines that would crowd together when zoomed out are left out.
    const auto scale = hasPanel() ? zoom : 1.0f;
    const auto origin = hasPanel() ? juce::Point<float> (-pan.x * zoom, -pan.y * zoom) : juce::Point<float>();

    for (int step : { 16, 64 })
    {
        auto spacing = (float) step * scale;

        if (step == 16 && spacing < 7.0f)
            continue;

        while (spacing < 14.0f)
            spacing *= 4.0f;

        g.setColour (juce::Colours::white.withAlpha (step == 16 ? 0.028f : 0.05f));

        auto x = std::fmod (origin.x, spacing);
        auto y = std::fmod (origin.y, spacing);

        if (x < 0.0f) x += spacing;
        if (y < 0.0f) y += spacing;

        for (; x < (float) getWidth(); x += spacing)
            g.drawVerticalLine (juce::roundToInt (x), 0.0f, (float) getHeight());

        for (; y < (float) getHeight(); y += spacing)
            g.drawHorizontalLine (juce::roundToInt (y), 0.0f, (float) getWidth());
    }
}

void PluginCanvas::drawMinimap (juce::Graphics& g)
{
    const auto map = minimapArea();
    const auto world = worldBounds();

    g.setColour (juce::Colour (0xdd101014));
    g.fillRoundedRectangle (map, 4.0f);
    g.setColour (Theme::outline.brighter (0.2f));
    g.drawRoundedRectangle (map, 4.0f, 1.0f);

    auto toMap = [&] (juce::Rectangle<float> plugin)
    {
        return juce::Rectangle<float> (map.getX() + (plugin.getX() - world.getX()) / world.getWidth() * map.getWidth(),
                                       map.getY() + (plugin.getY() - world.getY()) / world.getHeight() * map.getHeight(),
                                       plugin.getWidth() / world.getWidth() * map.getWidth(),
                                       plugin.getHeight() / world.getHeight() * map.getHeight());
    };

    // The window and what's on it.
    const auto window = toMap ({ 0.0f, 0.0f, (float) layout.width, (float) layout.height });
    g.setColour (layout.backgroundTop.interpolatedWith (layout.backgroundBottom, 0.5f).brighter (0.25f));
    g.fillRect (window);

    for (int i = 0; i < (int) layout.widgets.size(); ++i)
    {
        const auto& w = layout.widgets[(size_t) i];

        if (w.type == GuiWidget::Type::group)
            continue;

        g.setColour (i == selected ? Theme::accent : (isInteractive (w) ? mapControl : Theme::muted));
        g.fillRect (toMap (w.bounds.toFloat()).expanded (0.5f));
    }

    // What's showing.
    const auto view = viewArea();
    const auto visible = toMap ({ pan.x, pan.y, view.getWidth() / zoom, view.getHeight() / zoom }).getIntersection (map);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRect (visible);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawRect (visible, 1.2f);

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (Theme::font (10.0f, true));
    g.drawText ("MAP " + juce::String (juce::roundToInt (zoom * 100.0f)) + "%", map.reduced (5.0f, 3.0f).removeFromTop (12.0f),
                juce::Justification::centredLeft, false);
}

void PluginCanvas::drawScrollbars (juce::Graphics& g)
{
    for (const bool horizontal : { true, false })
    {
        const auto track = horizontal ? hScrollArea() : vScrollArea();
        const auto thumb = horizontal ? hThumb() : vThumb();
        const bool dragging = drag == (horizontal ? Drag::hScroll : Drag::vScroll);

        g.setColour (juce::Colour (0xff141418));
        g.fillRect (track);
        g.setColour (dragging ? Theme::text.withAlpha (0.7f) : Theme::muted.withAlpha (0.55f));
        g.fillRoundedRectangle (thumb, 3.0f);
    }

    // The corner where they meet.
    const auto view = viewArea();
    g.setColour (juce::Colour (0xff141418));
    g.fillRect (juce::Rectangle<float> (view.getRight(), view.getBottom(), scrollbarThickness, scrollbarThickness));
}

void PluginCanvas::repaintDropGhost()
{
    if (dropGhost.has_value())
        repaint (toView (*dropGhost).getSmallestIntegerContainer().expanded (3));
}

void PluginCanvas::drawDropGhost (juce::Graphics& g)
{
    if (! dropGhost.has_value())
        return;

    const auto box = toView (*dropGhost);
    g.setColour (Theme::accent.withAlpha (0.18f));
    g.fillRect (box);
    g.setColour (Theme::accent);
    g.drawRect (box, 1.5f);
}

void PluginCanvas::showDropGhost (const juce::String& primitiveId, juce::Point<float> view)
{
    const auto* primitive = PrimitiveLibrary::get().find (primitiveId);
    repaintDropGhost();   // where it was

    if (primitive == nullptr)
        dropGhost.reset();
    else if (primitive->setsBackground())
        dropGhost = juce::Rectangle<int> (0, 0, layout.width, layout.height);
    else
        dropGhost = placed (primitive->makeWidget().bounds, toPlugin (view));

    repaintDropGhost();   // where it is now
}

//==============================================================================
// The mouse
void PluginCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    drag = Drag::none;
    changed = dragMoved = false;
    pressedPanel = false;
    pressedBackdrop = -1;
    reopenMenu = menuOpen;
    carried.clear();
    dragStart = e.position;
    panAtDragStart = pan;

    if (! hasPanel())
        return;

    // The minimap and the scrollbars come first (as in Colosseum).
    if (e.mods.isLeftButtonDown())
    {
        if (minimapArea().contains (e.position))
        {
            drag = Drag::minimap;
            navigateMinimapTo (e.position);
            return;
        }

        if (hScrollArea().contains (e.position) || vScrollArea().contains (e.position))
        {
            const bool horizontal = hScrollArea().contains (e.position);
            const auto thumb = horizontal ? hThumb() : vThumb();

            // A click beside the thumb jumps there; then it drags.
            if (! thumb.contains (e.position))
            {
                const auto world = worldBounds();
                const auto track = horizontal ? hScrollArea() : vScrollArea();
                const auto t = horizontal ? (e.position.x - track.getX()) / track.getWidth() : (e.position.y - track.getY()) / track.getHeight();
                const auto target = horizontal ? juce::Point<float> (world.getX() + world.getWidth() * t, toPlugin (viewArea().getCentre()).y)
                                               : juce::Point<float> (toPlugin (viewArea().getCentre()).x, world.getY() + world.getHeight() * t);
                autoFit = false;
                centreOn (target);
                viewChanged();
                panAtDragStart = pan;
            }

            drag = horizontal ? Drag::hScroll : Drag::vScroll;
            return;
        }
    }

    // The middle button always moves the view.
    if (e.mods.isMiddleButtonDown())
    {
        drag = Drag::pan;
        return;
    }

    const auto plugin = toPlugin (e.position);
    const bool insidePanel = panelArea().contains (e.position);

    if (design)
    {
        // A right-click: the element's layers and the like, or the window's.
        if (e.mods.isPopupMenu())
        {
            const auto index = insidePanel ? widgetAt (plugin, false) : -1;

            if (index >= 0)
            {
                select (index);
                showElementPopup (index);
            }
            else if (insidePanel)
            {
                showWindowPopup();
            }

            return;
        }

        // The selected element's handles resize it; the window's own corner resizes the window.
        if (const auto handle = selected >= 0 ? handleAt (e.position) : -1; handle >= 0)
        {
            drag = Drag::resizeElement;
            resizeHandle = handle;
            startBounds = layout.widgets[(size_t) selected].bounds;
            closeMenu();
            return;
        }

        if (panelHandle().expanded (4.0f).contains (e.position))
        {
            drag = Drag::resizePanel;
            startPanelSize = { layout.width, layout.height };
            selectPanel();
            closeMenu();
            return;
        }

        const auto index = insidePanel ? widgetAt (plugin, false) : -1;

        // The background picture isn't grabbed by accident: until it's selected, a drag on it
        // moves the view and a click selects it.
        if (index >= 0 && index != selected && isBackdrop (layout.widgets[(size_t) index]))
        {
            pressedBackdrop = index;
            drag = Drag::pan;
            return;
        }

        if (index >= 0)
        {
            // An element: it's selected now (an open menu follows it); a click opens its menu.
            select (index);
            drag = Drag::element;
            startBounds = layout.widgets[(size_t) selected].bounds;

            if (layout.widgets[(size_t) selected].type == GuiWidget::Type::group)
                for (int i = 0; i < (int) layout.widgets.size(); ++i)
                    if (i != selected && startBounds.contains (layout.widgets[(size_t) i].bounds))
                        carried.push_back ({ i, layout.widgets[(size_t) i].bounds });

            return;
        }

        // Empty space: a click selects the window (or nothing, outside it); a drag moves the view.
        pressedPanel = insidePanel;
        drag = Drag::pan;
        return;
    }

    // Play mode: the controls work; elsewhere a drag moves the view.
    active = insidePanel ? widgetAt (plugin, true) : -1;

    if (active < 0)
    {
        drag = Drag::pan;
        return;
    }

    drag = Drag::control;
    const auto& w = layout.widgets[(size_t) active];

    if (w.type == GuiWidget::Type::keyboard)
    {
        playKey (active, plugin);
        return;
    }

    if (w.type == GuiWidget::Type::xy)
    {
        setXy (w, plugin);
        return;
    }

    if (w.type == GuiWidget::Type::preset)
    {
        active = -1;
        drag = Drag::none;

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

        juce::PopupMenu choices;

        for (int i = 0; i < count; ++i)
            choices.addItem (i + 1, presetNames[i], true, i == currentPreset);

        choices.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (toView (w.bounds).toNearestInt())),
                               [safeThis = juce::Component::SafePointer<PluginCanvas> (this)] (int choice)
                               {
                                   if (safeThis != nullptr && choice > 0 && safeThis->onPresetChosen != nullptr)
                                       safeThis->onPresetChosen (choice - 1);
                               });
        return;
    }

    const auto* p = paramFor (w);

    if (p == nullptr)
    {
        active = -1;
        drag = Drag::pan;
        return;
    }

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
    const auto offset = e.position - dragStart;
    const auto delta = offset / zoom;   // in plugin pixels

    if (e.getDistanceFromDragStart() > 3)
        dragMoved = true;

    switch (drag)
    {
        case Drag::minimap:
            navigateMinimapTo (e.position);
            return;

        case Drag::hScroll:
        case Drag::vScroll:
        {
            const bool horizontal = drag == Drag::hScroll;
            const auto world = worldBounds();
            const auto track = horizontal ? hScrollArea() : vScrollArea();
            const auto thumb = horizontal ? hThumb() : vThumb();
            const auto travel = (horizontal ? track.getWidth() - thumb.getWidth() : track.getHeight() - thumb.getHeight());
            const auto range = horizontal ? world.getWidth() - viewArea().getWidth() / zoom : world.getHeight() - viewArea().getHeight() / zoom;

            if (travel > 0.0f && range > 0.0f)
            {
                autoFit = false;

                if (horizontal) pan.x = panAtDragStart.x + offset.x / travel * range;
                else            pan.y = panAtDragStart.y + offset.y / travel * range;

                clampPan();
                viewChanged();
            }
            return;
        }

        case Drag::pan:
        {
            if (! dragMoved)
                return;

            autoFit = false;
            pan = panAtDragStart - delta;
            clampPan();
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            viewChanged();
            return;
        }

        case Drag::resizePanel:
        {
            // Never smaller than what's on it; a picture filling the window keeps filling it.
            const juce::Rectangle<int> oldWindow (0, 0, layout.width, layout.height);
            int needW = GuiLayout::minWidth, needH = GuiLayout::minHeight;

            for (const auto& w : layout.widgets)
            {
                if (w.type == GuiWidget::Type::image && w.bounds == oldWindow)
                    continue;

                needW = juce::jmax (needW, w.bounds.getRight());
                needH = juce::jmax (needH, w.bounds.getBottom());
            }

            layout.width = juce::jlimit (juce::jmin (needW, GuiLayout::maxWidth), GuiLayout::maxWidth, snapped ((float) startPanelSize.x + delta.x));
            layout.height = juce::jlimit (juce::jmin (needH, GuiLayout::maxHeight), GuiLayout::maxHeight, snapped ((float) startPanelSize.y + delta.y));

            for (auto& w : layout.widgets)
                if (w.type == GuiWidget::Type::image && w.bounds == oldWindow)
                    w.bounds = { 0, 0, layout.width, layout.height };
            changed = true;
            autoFit = false;
            clampPan();
            layOutOverlays();
            repaint();
            return;
        }

        case Drag::resizeElement:
        {
            if (selected < 0)
                return;

            auto& w = layout.widgets[(size_t) selected];
            const bool knob = w.type == GuiWidget::Type::knob;
            const auto caption = knob ? GuiLayout::captionHeight : 0;
            const auto minW = 12, minH = 12 + caption;

            // The sides the handle moves; the others stay where they were.
            const bool left = resizeHandle == 0 || resizeHandle == 6 || resizeHandle == 7;
            const bool right = resizeHandle == 2 || resizeHandle == 3 || resizeHandle == 4;
            const bool top = resizeHandle == 0 || resizeHandle == 1 || resizeHandle == 2;
            const bool bottom = resizeHandle == 4 || resizeHandle == 5 || resizeHandle == 6;

            auto x0 = startBounds.getX(), y0 = startBounds.getY(), x1 = startBounds.getRight(), y1 = startBounds.getBottom();

            // A side near the window's edge sticks to it, so a picture is easily made to fill it.
            auto toEdge = [] (int v, int edge) { return std::abs (v - edge) <= edgeSnap ? edge : v; };

            if (left)   x0 = juce::jmin (x1 - minW, toEdge (snapped ((float) x0 + delta.x), 0));
            if (right)  x1 = juce::jmax (x0 + minW, toEdge (snapped ((float) x1 + delta.x), layout.width));
            if (top)    y0 = juce::jmin (y1 - minH, toEdge (snapped ((float) y0 + delta.y), 0));
            if (bottom) y1 = juce::jmax (y0 + minH, toEdge (snapped ((float) y1 + delta.y), layout.height));

            auto bounds = juce::Rectangle<int>::leftTopRightBottom (x0, y0, x1, y1);

            // A knob stays round (its caption stays under it); Shift on a corner keeps the shape.
            if (knob || ((left || right) && (top || bottom) && e.mods.isShiftDown()))
            {
                const auto startW = (float) startBounds.getWidth(), startH = (float) (startBounds.getHeight() - caption);
                const auto scaleW = (float) bounds.getWidth() / juce::jmax (1.0f, startW);
                const auto scaleH = (float) (bounds.getHeight() - caption) / juce::jmax (1.0f, startH);
                const auto scale = std::abs (scaleW - 1.0f) >= std::abs (scaleH - 1.0f) ? scaleW : scaleH;
                const auto newW = juce::jmax (minW, juce::roundToInt (startW * scale));
                const auto newH = knob ? newW : juce::jmax (minH - caption, juce::roundToInt (startH * scale));

                // Anchored at the corner opposite the handle.
                const auto anchorX = left ? startBounds.getRight() : startBounds.getX();
                const auto anchorY = top ? startBounds.getBottom() : startBounds.getY();
                bounds = { left ? anchorX - newW : anchorX, top ? anchorY - (newH + caption) : anchorY, newW, newH + caption };
            }

            w.bounds = bounds;
            changed = true;
            repaint();
            return;
        }

        case Drag::element:
        {
            if (selected < 0 || ! dragMoved)
                return;

            auto& w = layout.widgets[(size_t) selected];
            const auto dx = snapped (delta.x), dy = snapped (delta.y);
            w.bounds = startBounds.translated (dx, dy);

            for (const auto& [index, bounds] : carried)
                layout.widgets[(size_t) index].bounds = bounds.translated (dx, dy);

            changed = true;
            closeMenu();
            repaint();
            return;
        }

        case Drag::control:
        {
            if (active < 0)
                return;

            const auto& w = layout.widgets[(size_t) active];

            // Sliding across the keys plays each in turn; off the keyboard, nothing plays.
            if (w.type == GuiWidget::Type::keyboard)
            {
                playKey (active, toPlugin (e.position));
                return;
            }

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
            return;
        }

        case Drag::none:
            return;
    }
}

void PluginCanvas::mouseUp (const juce::MouseEvent&)
{
    const auto was = drag;
    drag = Drag::none;
    releaseKey();
    setMouseCursor (juce::MouseCursor::NormalCursor);

    if (design && changed)
        edited();

    if (design)
    {
        if (was == Drag::element && ! dragMoved)
        {
            openMenu();                         // a click on an element opens its menu
        }
        else if (was == Drag::element || was == Drag::resizeElement || was == Drag::resizePanel)
        {
            if (reopenMenu)
                openMenu();                     // back where it was before the move
        }
        else if (was == Drag::pan && ! dragMoved)
        {
            // A click on the background picture selects it and opens its menu; on the window's
            // empty space, the window's menu; outside it: nothing.
            if (juce::isPositiveAndBelow (pressedBackdrop, (int) layout.widgets.size()))
            {
                select (pressedBackdrop);
                openMenu();
            }
            else if (pressedPanel)
            {
                selectPanel();
                openMenu();
            }
            else
            {
                select (-1);
            }
        }
    }

    changed = false;
    active = -1;
    carried.clear();
    repaint();
}

void PluginCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! hasPanel() || ! panelArea().contains (e.position))
        return;

    const auto index = widgetAt (toPlugin (e.position), ! design);

    if (index < 0)
        return;

    const auto& w = layout.widgets[(size_t) index];

    if (design)
    {
        if (Looks::takesLook (w) && onEditLook != nullptr)
            onEditLook (index);
        else if (w.type == GuiWidget::Type::image)
            choosePicture (index);

        return;
    }

    if (const auto* p = paramFor (w); p != nullptr && (w.type == GuiWidget::Type::knob || w.type == GuiWidget::Type::slider))
        setValue (*p, p->def);
}

void PluginCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! hasPanel())
        return;

    // Ctrl + wheel zooms around the mouse (Colosseum's step).
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        autoFit = false;
        setZoom (zoom + wheel.deltaY * 0.15f * juce::jmax (0.5f, zoom), e.position);
        return;
    }

    // Play mode: the wheel turns the control under the mouse.
    if (! design)
    {
        const auto index = panelArea().contains (e.position) ? widgetAt (toPlugin (e.position), true) : -1;

        if (index >= 0)
        {
            const auto& w = layout.widgets[(size_t) index];

            if (const auto* p = paramFor (w); p != nullptr && (w.type == GuiWidget::Type::knob || w.type == GuiWidget::Type::slider))
            {
                setValue (*p, valueAt (*p, proportionOf (*p, currentValue (*p)) + wheel.deltaY * 0.08f));
                return;
            }
        }
    }

    // Otherwise it scrolls: down and up, Shift for sideways (a trackpad's sideways moves too).
    autoFit = false;

    if (e.mods.isShiftDown())
        pan.x -= wheel.deltaY * scrollSpeed / zoom;
    else
    {
        pan.y -= wheel.deltaY * scrollSpeed / zoom;
        pan.x -= wheel.deltaX * scrollSpeed / zoom;
    }

    clampPan();
    viewChanged();
}

void PluginCanvas::mouseMove (const juce::MouseEvent& e)
{
    const auto index = hasPanel() && panelArea().contains (e.position) ? widgetAt (toPlugin (e.position), ! design) : -1;

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }

    // The resize handles show it.
    const auto handle = design && hasPanel() && selected >= 0 ? handleAt (e.position) : -1;

    if (handle >= 0)
    {
        static const juce::MouseCursor::StandardCursorType cursors[] {
            juce::MouseCursor::TopLeftCornerResizeCursor, juce::MouseCursor::UpDownResizeCursor,
            juce::MouseCursor::TopRightCornerResizeCursor, juce::MouseCursor::LeftRightResizeCursor,
            juce::MouseCursor::BottomRightCornerResizeCursor, juce::MouseCursor::UpDownResizeCursor,
            juce::MouseCursor::BottomLeftCornerResizeCursor, juce::MouseCursor::LeftRightResizeCursor };

        setMouseCursor (cursors[handle]);
        return;
    }

    const bool onPanelHandle = design && hasPanel() && panelHandle().expanded (4.0f).contains (e.position);
    setMouseCursor (onPanelHandle ? juce::MouseCursor::BottomRightCornerResizeCursor : juce::MouseCursor::NormalCursor);
}

void PluginCanvas::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

bool PluginCanvas::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    // The view: Ctrl+0 fits, Ctrl+1 is 100%, Ctrl + / - zoom.
    if (hasPanel() && (mods.isCtrlDown() || mods.isCommandDown()))
    {
        if (key.isKeyCode ('0'))                          { fitToView(); return true; }
        if (key.isKeyCode ('1'))                          { autoFit = false; setZoom (1.0f); return true; }
        if (key.isKeyCode ('=') || key.isKeyCode ('+'))   { autoFit = false; setZoom (zoom * 1.25f); return true; }
        if (key.isKeyCode ('-'))                          { autoFit = false; setZoom (zoom / 1.25f); return true; }
    }

    if (! design)
        return false;

    if (key == juce::KeyPress::escapeKey && (menuOpen || selected >= 0 || panelSelected))
    {
        select (-1);
        return true;
    }

    if (selected < 0)
        return false;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        removeSelected();
        return true;
    }

    // Layers: Ctrl+] forward, Ctrl+[ backward; with Shift, all the way.
    if (mods.isCtrlDown() || mods.isCommandDown())
    {
        const auto code = key.getKeyCode();

        if (code == ']' || code == '}')
        {
            reorder (selected, mods.isShiftDown() ? Order::front : Order::forward);
            return true;
        }

        if (code == '[' || code == '{')
        {
            reorder (selected, mods.isShiftDown() ? Order::back : Order::backward);
            return true;
        }
    }

    const auto step = mods.isShiftDown() ? 8 : 1;
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

//==============================================================================
// Primitives from the toolbox
bool PluginCanvas::isInterestedInDragSource (const SourceDetails& details)
{
    return design && hasPanel() && PrimitiveLibrary::idFromDrag (details.description).isNotEmpty();
}

void PluginCanvas::itemDragEnter (const SourceDetails& details)
{
    showDropGhost (PrimitiveLibrary::idFromDrag (details.description), details.localPosition.toFloat());
}

void PluginCanvas::itemDragMove (const SourceDetails& details)
{
    showDropGhost (PrimitiveLibrary::idFromDrag (details.description), details.localPosition.toFloat());
}

void PluginCanvas::itemDragExit (const SourceDetails&)
{
    repaintDropGhost();
    dropGhost.reset();
}

void PluginCanvas::itemDropped (const SourceDetails& details)
{
    dropGhost.reset();
    repaint();

    if (const auto* primitive = PrimitiveLibrary::get().find (PrimitiveLibrary::idFromDrag (details.description)))
        addPrimitive (*primitive, toPlugin (details.localPosition.toFloat()));
}

//==============================================================================
// Pictures from the desktop
bool PluginCanvas::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (! design || ! hasPanel())
        return false;

    for (const auto& path : files)
        if (GuiLayout::isPicture (juce::File (path)))
            return true;

    return false;
}

void PluginCanvas::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void PluginCanvas::fileDragMove (const juce::StringArray&, int x, int y)
{
    repaintDropGhost();
    dropGhost = placed ({ 0, 0, 160, 120 }, toPlugin ({ (float) x, (float) y }));
    repaintDropGhost();
}

void PluginCanvas::fileDragExit (const juce::StringArray&)
{
    repaintDropGhost();
    dropGhost.reset();
}

void PluginCanvas::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropGhost.reset();
    auto centre = toPlugin ({ (float) x, (float) y });

    for (const auto& path : files)
    {
        const juce::File file (path);
        const auto name = GuiLayout::isPicture (file) ? GuiLayout::importPicture (file, imagesFolder()) : juce::String();

        if (name.isEmpty())
            continue;

        GuiWidget w;
        w.type = GuiWidget::Type::image;
        w.mode = "fit";
        w.bounds = { 0, 0, 160, 120 };

        const auto index = addElement (w, centre);
        usePicture (index, name);
        centre += { 24.0f, 24.0f };   // several pictures: a little apart
    }

    repaint();
}
