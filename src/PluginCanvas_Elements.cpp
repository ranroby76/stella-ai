// C:\workspace\Stella AI Studio\src\PluginCanvas_Elements.cpp
// The canvas's programmed elements: Stella AI's own GUI elements (the project's elements
// folder), run in their sandbox (WasmElements), drawn from the pixels they paint, given the
// mouse in Play mode, with their popups over everything.

#include "PluginCanvas.h"
#include "PluginCanvasMenu.h"
#include "StellaLookAndFeel.h"
#include "WasmElements.h"

#include <set>

namespace
{
    // As in stella_elements.h: the mouse events.
    enum { mouseDownEvent = 0, mouseDragEvent = 1, mouseUpEvent = 2, mouseMoveEvent = 3, mouseExitEvent = 4, mouseWheelEvent = 5 };

    void copyPixels (juce::Image& image, const std::uint32_t* pixels, int w, int h)
    {
        if (! image.isValid() || image.getWidth() != w || image.getHeight() != h)
            image = juce::Image (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());

        // Both are premultiplied 0xAARRGGBB.
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < h; ++y)
            std::memcpy (data.getLinePointer (y), pixels + (size_t) y * (size_t) w, sizeof (std::uint32_t) * (size_t) w);
    }
}

//==============================================================================
// What the elements ask
const GuiWidget* PluginCanvas::ElementBridge::widget (int element) const
{
    const auto& widgets = canvas.layout.widgets;
    return juce::isPositiveAndBelow (element, (int) widgets.size()) && widgets[(size_t) element].type == GuiWidget::Type::custom
               ? &widgets[(size_t) element] : nullptr;
}

const PluginCanvas::Param* PluginCanvas::ElementBridge::paramAt (int element, int slot) const
{
    const auto* w = widget (element);

    if (w == nullptr || slot < 0)
        return nullptr;

    if (slot == 0)
        return canvas.paramById (w->param);

    // Slots 1 and up: its roles, in the order the layout keeps them.
    int i = 1;

    for (const auto& [role, id] : w->roles)
        if (i++ == slot)
            return canvas.paramById (id);

    return nullptr;
}

bool PluginCanvas::ElementBridge::paramInfo (int element, int slot, int info, float& result)
{
    const auto* p = paramAt (element, slot);

    if (p == nullptr)
        return false;

    switch (info)
    {
        case valueInfo:    result = canvas.currentValue (*p); return true;
        case minInfo:      result = p->min; return true;
        case maxInfo:      result = p->max; return true;
        case defaultInfo:  result = p->def; return true;
        case skewInfo:     result = p->skew; return true;
        default:           return false;
    }
}

int PluginCanvas::ElementBridge::findSlot (int element, const std::string& role)
{
    const auto* w = widget (element);

    if (w == nullptr)
        return -1;

    const auto name = juce::String (role).trim();

    if (name.isEmpty() || name == "param")
        return w->param.isNotEmpty() ? 0 : -1;

    int i = 1;

    for (const auto& r : w->roles)
    {
        if (r.first == name)
            return i;

        ++i;
    }

    return -1;
}

void PluginCanvas::ElementBridge::setValue (int element, int slot, float newValue)
{
    if (const auto* p = paramAt (element, slot))
        canvas.setValue (*p, newValue);
}

void PluginCanvas::ElementBridge::gesture (int, int, bool)
{
    // The studio has no host to tell: each value goes to the plugin as it's set.
}

bool PluginCanvas::ElementBridge::text (int element, int which, int index, const std::string& key, std::string& result)
{
    const auto* w = widget (element);

    if (w == nullptr)
        return false;

    juce::String text;

    switch (which)
    {
        case labelText:
            text = w->label;
            break;

        case optionText:
            if (! juce::isPositiveAndBelow (index, w->options.size()))
                return false;

            text = w->options[index];
            break;

        case paramNameText:
        case unitText:
        {
            const auto* p = paramAt (element, index);

            if (p == nullptr)
                return false;

            text = which == paramNameText ? p->name : p->unit;
            break;
        }

        case settingText:
            if (w->settings.find (juce::String (key)) == w->settings.end())
                return false;

            text = w->settingText (juce::String (key));
            break;

        default:
            return false;
    }

    result = text.toStdString();
    return true;
}

int PluginCanvas::ElementBridge::numOptions (int element)
{
    const auto* w = widget (element);
    return w != nullptr ? w->options.size() : 0;
}

std::uint32_t PluginCanvas::ElementBridge::colour (int element)
{
    const auto* w = widget (element);
    return w != nullptr && ! w->colour.isTransparent() ? w->colour.getARGB() : 0u;
}

float PluginCanvas::ElementBridge::level (int element, bool rms)
{
    const auto* w = widget (element);
    return w != nullptr && w->source.isNotEmpty() ? canvas.frameLevel (w->source, rms) : 0.0f;
}

void PluginCanvas::ElementBridge::readScope (int element, float* destination, int numSamples)
{
    const auto* w = widget (element);

    if (w != nullptr && w->source.isNotEmpty() && canvas.readScope != nullptr)
        canvas.readScope (w->source, destination, numSamples);
    else
        std::fill (destination, destination + juce::jmax (0, numSamples), 0.0f);
}

void PluginCanvas::ElementBridge::playNote (int, int note, float velocity)
{
    if (canvas.onNote != nullptr && note >= 0 && note < 128)
        canvas.onNote (note, juce::jlimit (0.0f, 1.0f, velocity));
}

bool PluginCanvas::ElementBridge::isNoteDown (int note)
{
    return canvas.isNoteDown != nullptr && note >= 0 && note < 128 && canvas.isNoteDown (note);
}

double PluginCanvas::ElementBridge::seconds()
{
    return juce::Time::getMillisecondCounterHiRes() / 1000.0;
}

//==============================================================================
float PluginCanvas::frameLevel (const juce::String& source, bool rms)
{
    // Each source is read once per frame: a peak is "the highest since the last read".
    const auto key = source + (rms ? "|rms" : "|peak");

    if (const auto found = frameLevels.find (key); found != frameLevels.end())
        return found->second;

    const auto level = readLevel != nullptr ? readLevel (source, rms) : 0.0f;
    frameLevels[key] = level;
    return level;
}

void PluginCanvas::setElements (std::shared_ptr<WasmElements> newElements)
{
    if (elements != nullptr)
    {
        closeElementPopups();
        elements->setHost (nullptr);
    }

    elementViews.clear();
    hoveredElement = -1;
    elementsProblem.clear();
    elements = std::move (newElements);

    if (elementBridge == nullptr)
        elementBridge = std::make_unique<ElementBridge> (*this);

    if (elements != nullptr)
        elements->setHost (elementBridge.get());

    syncElements();
    repaint();
}

void PluginCanvas::checkElementsFailed()
{
    if (elements == nullptr || ! elements->hasFailed() || elementsProblem.isNotEmpty())
        return;

    elementsProblem = juce::String (elements->getFailure());
    elementViews.clear();
    hoveredElement = -1;
    repaint();

    if (onElementsFailed != nullptr)
        onElementsFailed (elementsProblem);
}

void PluginCanvas::syncElements()
{
    checkElementsFailed();

    if (elements == nullptr || elements->hasFailed())
    {
        elementViews.clear();
        return;
    }

    std::set<int> shown;

    for (int i = 0; i < (int) layout.widgets.size(); ++i)
    {
        const auto& w = layout.widgets[(size_t) i];

        if (w.type != GuiWidget::Type::custom)
            continue;

        shown.insert (i);
        auto& view = elementViews[i];
        const auto width = w.bounds.getWidth(), height = w.bounds.getHeight();

        if (view.created && view.type == w.element)
        {
            if (view.width != width || view.height != height)
            {
                elements->resize (i, width, height);
                view.width = width;
                view.height = height;
            }

            elements->invalidate (i);   // its label, options or colour may have changed
            continue;
        }

        // A new element here (or a different one): made afresh.
        if (view.created)
            elements->destroy (i);

        view = {};
        view.type = w.element;
        view.width = width;
        view.height = height;
        view.created = elements->hasType (w.element.toStdString()) && elements->create (i, w.element.toStdString(), width, height);
    }

    for (auto it = elementViews.begin(); it != elementViews.end();)
    {
        if (shown.count (it->first) > 0)
        {
            ++it;
            continue;
        }

        if (it->second.created)
            elements->destroy (it->first);

        it = elementViews.erase (it);
    }

    refreshElements (true);
}

void PluginCanvas::refreshElements (bool all)
{
    if (elements == nullptr || elements->hasFailed())
    {
        checkElementsFailed();
        return;
    }

    for (auto& [index, view] : elementViews)
    {
        if (! view.created || ! juce::isPositiveAndBelow (index, (int) layout.widgets.size()))
            continue;

        if (! all && ! elements->needsPaint (index))
            continue;

        const auto& w = layout.widgets[(size_t) index];
        const auto before = view.popup;

        if (! elements->render (index))
            break;

        if (const auto* pixels = elements->pixels (index, false, view.width, view.height))
            copyPixels (view.image, pixels, view.width, view.height);
        else
            view.image = {};

        // The popup, kept inside the window where it fits.
        int px = 0, py = 0, pw = 0, ph = 0;

        if (! design && elements->popupArea (index, px, py, pw, ph))
        {
            view.popup = { juce::jlimit (0, juce::jmax (0, layout.width - pw), w.bounds.getX() + px),
                           juce::jlimit (0, juce::jmax (0, layout.height - ph), w.bounds.getY() + py), pw, ph };

            if (const auto* pixels = elements->pixels (index, true, pw, ph))
                copyPixels (view.popupImage, pixels, pw, ph);
            else
                view.popupImage = {};
        }
        else
        {
            view.popup = {};
            view.popupImage = {};
        }

        for (const auto& area : { w.bounds, before, view.popup })
            if (! area.isEmpty())
                repaint (toView (area).getSmallestIntegerContainer().expanded (2));
    }

    checkElementsFailed();
}

void PluginCanvas::drawElement (juce::Graphics& g, const GuiWidget& w, int index)
{
    const auto b = w.bounds.toFloat();
    const auto found = elementViews.find (index);

    if (found != elementViews.end() && found->second.created && found->second.image.isValid())
    {
        auto& view = found->second;

        // Being resized: drawn again at the new size.
        if (view.width != w.bounds.getWidth() || view.height != w.bounds.getHeight())
        {
            elements->resize (index, w.bounds.getWidth(), w.bounds.getHeight());
            view.width = w.bounds.getWidth();
            view.height = w.bounds.getHeight();

            if (elements->render (index))
                if (const auto* pixels = elements->pixels (index, false, view.width, view.height))
                    copyPixels (view.image, pixels, view.width, view.height);
        }

        g.setOpacity (1.0f);   // drawImage draws at the current colour's alpha
        g.drawImage (view.image, b);
        return;
    }

    if (baking)
        return;   // the exported plugin draws it itself

    // Not running (yet): a frame that says why.
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (Theme::accent.withAlpha (0.55f));
    const float dashes[] { 5.0f, 4.0f };

    for (const auto& edge : { juce::Line<float> (b.getTopLeft(), b.getTopRight()), juce::Line<float> (b.getTopRight(), b.getBottomRight()),
                              juce::Line<float> (b.getBottomRight(), b.getBottomLeft()), juce::Line<float> (b.getBottomLeft(), b.getTopLeft()) })
        g.drawDashedLine (edge, dashes, 2, 1.0f);

    const auto name = w.element.isNotEmpty() ? w.element : juce::String ("(no name)");
    juce::String why;

    if (elementsProblem.isNotEmpty())
        why = "Stopped: it crashed or got stuck";   // Stella AI is told the details
    else if (elements == nullptr)
        why = "Shows after the next build";
    else
        why = "Not programmed yet (elements/" + name + ".cpp)";

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (w.bounds);
    g.setColour (juce::Colour (0xffd8d4ca));
    g.setFont (Theme::font (juce::jmin (12.5f, juce::jmax (9.0f, b.getHeight() * 0.3f)), true));
    g.drawFittedText (name + (b.getHeight() >= 34.0f ? "\n" : ": ") + why, b.reduced (6.0f, 2.0f).toNearestInt(),
                      juce::Justification::centred, b.getHeight() >= 34.0f ? 2 : 1, 0.7f);
}

void PluginCanvas::drawElementPopups (juce::Graphics& g)
{
    for (const auto& [index, view] : elementViews)
    {
        if (view.popup.isEmpty() || ! view.popupImage.isValid())
            continue;

        // A soft shadow under it, as menus have.
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 10, { 0, 3 }).drawForRectangle (g, view.popup);
        g.setOpacity (1.0f);   // drawImage draws at the current colour's alpha
        g.drawImage (view.popupImage, view.popup.toFloat());
    }
}

int PluginCanvas::popupAt (juce::Point<float> plugin) const
{
    // The topmost: the last in the layout.
    for (auto it = elementViews.rbegin(); it != elementViews.rend(); ++it)
        if (! it->second.popup.isEmpty() && it->second.popup.toFloat().contains (plugin))
            return it->first;

    return -1;
}

bool PluginCanvas::anyPopupOpen() const
{
    for (const auto& [index, view] : elementViews)
        if (! view.popup.isEmpty())
            return true;

    return false;
}

void PluginCanvas::closeElementPopups()
{
    if (elements == nullptr)
        return;

    for (auto& [index, view] : elementViews)
    {
        if (view.popup.isEmpty())
            continue;

        repaint (toView (view.popup).getSmallestIntegerContainer().expanded (12));
        elements->closePopup (index);
        view.popup = {};
        view.popupImage = {};
    }

    refreshElements (false);
}

bool PluginCanvas::elementMouse (int index, bool popup, int event, juce::Point<float> plugin, const juce::ModifierKeys& mods,
                                 bool doubleClick, float notches)
{
    if (elements == nullptr || ! juce::isPositiveAndBelow (index, (int) layout.widgets.size()))
        return false;

    const auto found = elementViews.find (index);

    if (found == elementViews.end() || ! found->second.created)
        return false;

    const auto origin = popup ? found->second.popup.getPosition() : layout.widgets[(size_t) index].bounds.getPosition();
    const auto local = plugin - origin.toFloat();
    const auto mouseFlags = (mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown() ? 1 : 0) | (doubleClick ? 2 : 0);

    const auto taken = elements->mouse (index, popup, event, local.x, local.y, mouseFlags, notches);
    refreshElements (false);
    return taken;
}

void PluginCanvas::hoverElements (juce::Point<float> plugin, const juce::ModifierKeys& mods)
{
    if (elements == nullptr || design)
        return;

    int index = popupAt (plugin);
    bool popup = index >= 0;

    if (! popup)
    {
        const juce::Rectangle<float> window (0.0f, 0.0f, (float) layout.width, (float) layout.height);
        index = window.contains (plugin) ? widgetAt (plugin, true) : -1;

        if (index >= 0 && layout.widgets[(size_t) index].type != GuiWidget::Type::custom)
            index = -1;
    }

    if (index != hoveredElement || popup != hoveredPopup)
    {
        if (hoveredElement >= 0)
            elementMouse (hoveredElement, hoveredPopup, mouseExitEvent, plugin, mods);

        hoveredElement = index;
        hoveredPopup = popup;
    }

    if (index >= 0)
        elementMouse (index, popup, mouseMoveEvent, plugin, mods);
}
