// C:\workspace\Stella AI Studio\src\ShapeEditor.cpp
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    ShapeEditor.cpp
*/

#include "ShapeEditor.h"

namespace
{
    constexpr float kNodeHit = 9.0f;
    constexpr float kSpanHit = 7.0f;

    // Long enough to catch a deliberate second click, short enough that the menu
    // does not feel like it is lagging.
    constexpr int kRightClickWindowMs = 260;
}

//==============================================================================
ShapeEditor::ShapeEditor()
{
    setWantsKeyboardFocus (true);
    shape = ShapeDoc::pointerExample();
}

void ShapeEditor::setShape (const ShapeDoc& s)
{
    shape = s;
    dragging = -1;
    selected = -1;
    repaint();
}

void ShapeEditor::setGrid (int divisions)
{
    gridDivisions = juce::jlimit (2, 64, divisions);
    repaint();
}

void ShapeEditor::setSnapEnabled (bool shouldSnap)
{
    snapping = shouldSnap;
}

void ShapeEditor::clearShape()
{
    shape.nodes.clear();
    dragging = selected = -1;
    changed();
}

void ShapeEditor::changed()
{
    repaint();

    if (onChange != nullptr)
        onChange();
}

//==============================================================================
juce::Rectangle<float> ShapeEditor::canvasArea() const
{
    // Square, because a shape is authored in a square and stretched by the
    // layer. Authoring it in a rectangle would bake the distortion in.
    auto r = getLocalBounds().toFloat().reduced (10.0f);
    const float side = juce::jmin (r.getWidth(), r.getHeight());

    return juce::Rectangle<float> (side, side).withCentre (r.getCentre());
}

juce::Point<float> ShapeEditor::toShape (juce::Point<float> screen) const
{
    auto r = canvasArea();

    return { juce::jlimit (0.0f, 1.0f, (screen.x - r.getX()) / juce::jmax (1.0f, r.getWidth())),
             juce::jlimit (0.0f, 1.0f, (screen.y - r.getY()) / juce::jmax (1.0f, r.getHeight())) };
}

juce::Point<float> ShapeEditor::toScreen (juce::Point<float> n) const
{
    auto r = canvasArea();
    return { r.getX() + n.x * r.getWidth(), r.getY() + n.y * r.getHeight() };
}

juce::Point<float> ShapeEditor::snap (juce::Point<float> n) const
{
    if (! snapping)
        return n;

    const float d = (float) gridDivisions;

    return { juce::jlimit (0.0f, 1.0f, std::round (n.x * d) / d),
             juce::jlimit (0.0f, 1.0f, std::round (n.y * d) / d) };
}

int ShapeEditor::nodeAt (juce::Point<float> screen) const
{
    for (int i = 0; i < (int) shape.nodes.size(); ++i)
        if (toScreen (shape.nodes[(size_t) i].pos).getDistanceFrom (screen) < kNodeHit)
            return i;

    return -1;
}

int ShapeEditor::spanAt (juce::Point<float> screen) const
{
    const int n = (int) shape.nodes.size();

    if (n < 2)
        return -1;

    const int last = shape.closed ? n : n - 1;

    for (int i = 0; i < last; ++i)
    {
        const auto a = toScreen (shape.nodes[(size_t) i].pos);
        const auto b = toScreen (shape.nodes[(size_t) ((i + 1) % n)].pos);

        // getDistanceFromPoint always writes back the nearest point on the line,
        // so it needs somewhere to put it even when we only want the distance.
        juce::Point<float> nearest;

        if (juce::Line<float> (a, b).getDistanceFromPoint (screen, nearest) < kSpanHit)
            return i;
    }

    return -1;
}

//==============================================================================
void ShapeEditor::paint (juce::Graphics& g)
{
    auto r = canvasArea();

    g.fillAll (juce::Colour (0xff1b1b1e));

    g.setColour (juce::Colour (0xff232327));
    g.fillRect (r);

    // Grid
    g.setColour (juce::Colour (0xff2c2c32));

    for (int i = 1; i < gridDivisions; ++i)
    {
        const float f = (float) i / (float) gridDivisions;
        g.drawVerticalLine   ((int) (r.getX() + f * r.getWidth()),  r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) (r.getY() + f * r.getHeight()), r.getX(), r.getRight());
    }

    g.setColour (juce::Colour (0xff3a3a42));
    g.drawVerticalLine   ((int) r.getCentreX(), r.getY(), r.getBottom());
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
    g.drawRect (r, 1.0f);

    if (shape.nodes.size() >= 2)
    {
        auto path = shape.buildPath();
        path.applyTransform (juce::AffineTransform::scale (r.getWidth(), r.getHeight())
                                 .translated (r.getX(), r.getY()));

        g.setColour (juce::Colour (0x33e8c020));
        g.fillPath (path);

        g.setColour (juce::Colour (0xffe8c020));
        g.strokePath (path, juce::PathStrokeType (1.6f));
    }

    for (int i = 0; i < (int) shape.nodes.size(); ++i)
    {
        const auto& node = shape.nodes[(size_t) i];
        const auto p = toScreen (node.pos);

        // Arc nodes draw round, corners square — the same language the contour
        // editor uses, so the shape of the marker tells you what it does.
        const bool isArc = node.mode == NodeMode::Arc;

        g.setColour (i == selected ? juce::Colour (0xffff6b6b)
                                   : (isArc ? juce::Colour (0xff8fd0ff)
                                            : juce::Colour (0xffe8c020)));

        if (isArc) g.fillEllipse (p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f);
        else       g.fillRect (p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f);

        if (i == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawEllipse (p.x - 7.5f, p.y - 7.5f, 15.0f, 15.0f, 1.2f);
        }
    }

    if (shape.nodes.empty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText ("Double-click to place points.  Drag a point to move it.  "
                    "Right-click for Corner / Arc.",
                    r.reduced (16), juce::Justification::centred, true);
    }
}

//==============================================================================
void ShapeEditor::mouseDown (const juce::MouseEvent& e)
{
    const int node = nodeAt (e.position);

    if (e.mods.isPopupMenu())
    {
        if (node < 0)
            return;

        selected = node;
        repaint();

        const auto now = juce::Time::getMillisecondCounter();

        // Second right-click on the same point, before the menu had a chance to
        // open: that is the delete gesture.
        if (isTimerRunning() && node == lastRightNode
              && now - lastRightTime < (juce::uint32) (kRightClickWindowMs * 2))
        {
            stopTimer();
            pendingMenuNode = -1;
            lastRightNode = -1;
            deleteNode (node);
            return;
        }

        lastRightNode = node;
        lastRightTime = now;
        pendingMenuNode = node;
        pendingMenuPos = e.getScreenPosition();
        startTimer (kRightClickWindowMs);
        return;
    }

    selected = node;
    dragging = node;
    repaint();
}

void ShapeEditor::timerCallback()
{
    stopTimer();

    const int node = pendingMenuNode;
    pendingMenuNode = -1;

    if (node >= 0)
        showMenuFor (node, pendingMenuPos);
}

void ShapeEditor::deleteNode (int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= (int) shape.nodes.size())
        return;

    shape.nodes.erase (shape.nodes.begin() + nodeIndex);
    selected = -1;
    changed();
}

void ShapeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || dragging >= (int) shape.nodes.size())
        return;

    // Where the mouse is released IS the value. On an Arc node this is the
    // point the span bows through, so the curve follows the cursor exactly
    // rather than being inferred from a handle somewhere else.
    shape.nodes[(size_t) dragging].pos = snap (toShape (e.position));
    changed();
}

void ShapeEditor::mouseUp (const juce::MouseEvent&)
{
    dragging = -1;
}

void ShapeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int node = nodeAt (e.position);

    if (node >= 0)
    {
        // Flip between the two behaviours without going to the menu.
        auto& m = shape.nodes[(size_t) node].mode;
        m = (m == NodeMode::Arc) ? NodeMode::Corner : NodeMode::Arc;
        changed();
        return;
    }

    const int span = spanAt (e.position);
    ShapeNode fresh;
    fresh.pos = snap (toShape (e.position));

    if (span >= 0)
    {
        // Dropped onto an existing span: go in between, so the ring keeps its
        // order. Appending would jump the outline across the shape.
        fresh.mode = NodeMode::Arc;
        shape.nodes.insert (shape.nodes.begin() + span + 1, fresh);
        selected = span + 1;
    }
    else
    {
        shape.nodes.push_back (fresh);
        selected = (int) shape.nodes.size() - 1;
    }

    changed();
}

//==============================================================================
void ShapeEditor::showMenuFor (int nodeIndex, juce::Point<int> screenPos)
{
    if (nodeIndex < 0 || nodeIndex >= (int) shape.nodes.size())
        return;

    const auto current = shape.nodes[(size_t) nodeIndex].mode;

    juce::PopupMenu m;
    m.addItem (1, "Corner", true, current == NodeMode::Corner);
    m.addItem (2, "Arc",    true, current == NodeMode::Arc);
    m.addSeparator();
    m.addItem (3, "Delete point", shape.nodes.size() > 2);

    juce::Component::SafePointer<ShapeEditor> safe (this);

    // Positioned where the click landed, not on the component — by the time
    // this opens the pointer has had a moment to be somewhere else.
    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [safe, nodeIndex] (int result)
    {
        auto* self = safe.getComponent();

        if (self == nullptr || result == 0)
            return;

        if (nodeIndex >= (int) self->shape.nodes.size())
            return;

        if (result == 1)      self->shape.nodes[(size_t) nodeIndex].mode = NodeMode::Corner;
        else if (result == 2) self->shape.nodes[(size_t) nodeIndex].mode = NodeMode::Arc;
        else if (result == 3)
        {
            self->deleteNode (nodeIndex);
            return;
        }

        self->changed();
    });
}
