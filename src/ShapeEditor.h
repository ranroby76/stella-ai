// C:\workspace\Stella AI Studio\src\ShapeEditor.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    ShapeEditor.h

    The drawing surface for the shape inventory.

    Interaction, deliberately small:

      double-click empty space   add a node at the end of the ring
      double-click a span        insert a node into that span
      drag a node                move it — and if it is an Arc node, the span
                                 through it bows to wherever you release
      right-click a node         Corner / Arc / Delete
      right-click it twice       delete it outright
      double-click a node        toggle Corner and Arc

    There are no tangent handles anywhere, and that is the entire design. A span
    that bows through the node you are dragging is a circle through three points,
    which needs no handles to specify and cannot be dragged into the twisted
    shapes a pen tool produces when the two arms disagree.
*/

#pragma once

#include "ShapeDoc.h"

#include <functional>

//==============================================================================
class ShapeEditor final : public juce::Component,
                          private juce::Timer
{
public:
    ShapeEditor();

    void setShape (const ShapeDoc&);
    const ShapeDoc& getShape() const noexcept { return shape; }

    void setGrid (int divisions);
    void setSnapEnabled (bool);

    /** Removes every node. */
    void clearShape();

    std::function<void()> onChange;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> canvasArea() const;
    juce::Point<float> toShape (juce::Point<float> screen) const;
    juce::Point<float> toScreen (juce::Point<float> normalised) const;
    juce::Point<float> snap (juce::Point<float> normalised) const;

    int nodeAt (juce::Point<float> screen) const;
    int spanAt (juce::Point<float> screen) const;

    void timerCallback() override;
    void showMenuFor (int nodeIndex, juce::Point<int> screenPos);
    void deleteNode (int nodeIndex);
    void changed();

    ShapeDoc shape;

    int  gridDivisions = 16;
    bool snapping = true;

    int dragging = -1;
    int selected = -1;

    // A right-click cannot both open a menu and wait to see whether a second one
    // is coming: the open menu takes the modal state and swallows the second
    // click before this component ever sees it. So the menu is held back just
    // long enough to find out, and a second click within that window deletes
    // instead.
    int  pendingMenuNode = -1;
    juce::Point<int> pendingMenuPos;
    int  lastRightNode = -1;
    juce::uint32 lastRightTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeEditor)
};
