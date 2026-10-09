// C:\workspace\Stella AI Studio\src\ShapeDoc.h
// From KnobMaker (C:\workspace\knobmaker); in Stella the inventory's folder is the studio's own.

/*
    ShapeDoc.h

    A hand-drawn outline, and the inventory of them.

    The model is a single closed ring of nodes. Each node is either a CORNER —
    the outline passes through it with straight segments either side — or an
    ARC, where the span from the previous node to the next bows through it as a
    true circle.

    That second one is the whole point of the editor. Three points define
    exactly one circle, so "bow this span through here" is a circumcircle, not
    an approximation built out of tangent handles. It is what lets a Davies-style
    pointer knob be five points and two drags instead of a fight with a pen tool.

    Static and Corner are the same geometry: a node the outline passes through
    with straight segments IS a tip. The editor still offers both, because the
    distinction is about what dragging does, not about what gets drawn.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

//==============================================================================
enum class NodeMode
{
    Corner = 0,   ///< straight in, straight out — a plain vertex or a tip
    Arc,          ///< the span through this node bows as a circle
    numModes
};

juce::StringArray nodeModeNames();

//==============================================================================
struct ShapeNode
{
    juce::Point<float> pos;                 ///< normalised, 0..1 across the grid
    NodeMode mode = NodeMode::Corner;
};

//==============================================================================
struct ShapeDoc
{
    juce::String name { "Untitled" };
    std::vector<ShapeNode> nodes;
    bool closed = true;

    /** Builds the outline in normalised 0..1 space. Arc nodes become circular
        sweeps through their neighbours; anything too near collinear to define a
        circle falls back to a straight line rather than flying off to a
        circumcentre at infinity. */
    juce::Path buildPath() const;

    bool isEmpty() const noexcept { return nodes.size() < 2; }

    juce::String toString() const;
    static ShapeDoc fromString (const juce::String&);

    /** A Davies-style pointer: round skirt, sharp nose. Also the worked example
        for what the editor is for. */
    static ShapeDoc pointerExample();
    static ShapeDoc teardropExample();
    static ShapeDoc arrowExample();

    static juce::StringArray exampleNames();
    static ShapeDoc exampleByIndex (int);
};

//==============================================================================
/** The inventory: named shapes on disk, shared by every document. */
namespace ShapeLibrary
{
    /** Stella: Documents/Stella AI Studio/Shapes */
    juce::File folder();

    juce::StringArray names();

    /** Invalid-but-empty ShapeDoc when the name is not on disk. */
    ShapeDoc load (const juce::String& name);

    bool save (const ShapeDoc&);
    bool remove (const juce::String& name);

    /** Bumped on every save or delete, so a panel can tell its list is stale. */
    int revision();
}
