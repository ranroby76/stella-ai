// C:\workspace\Stella AI Studio\src\ShapeDoc.cpp
// From KnobMaker (C:\workspace\knobmaker); in Stella the shape inventory is in Documents\Stella AI
// Studio\Shapes.

/*
    ShapeDoc.cpp
*/

#include "ShapeDoc.h"

#include <cmath>

namespace
{
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    float wrapAngle (float a) noexcept
    {
        while (a <  0.0f)    a += kTwoPi;
        while (a >= kTwoPi)  a -= kTwoPi;

        return a;
    }

    struct ArcSpec
    {
        juce::Point<float> centre;
        float radius = 0.0f, start = 0.0f, sweep = 0.0f;
        bool ok = false;
    };

    /** The unique circular arc that leaves `p` along `tangent` and ends at
        `target`. Two points and a tangent determine one circle. */
    ArcSpec arcFrom (juce::Point<float> p, juce::Point<float> tangent, juce::Point<float> target)
    {
        ArcSpec a;

        const juce::Point<float> nrm (-tangent.y, tangent.x);
        const auto m = target - p;

        const float denom = 2.0f * (nrm.x * m.x + nrm.y * m.y);

        if (std::abs (denom) < 1.0e-7f)
            return a;                       // target lies along the tangent: straight

        const float k = (m.x * m.x + m.y * m.y) / denom;

        a.centre = p + nrm * k;
        a.radius = std::abs (k);

        if (a.radius < 1.0e-6f || a.radius > 1.0e5f)
            return a;

        a.start = std::atan2 (p.y - a.centre.y, p.x - a.centre.x);

        const float endAngle = std::atan2 (target.y - a.centre.y, target.x - a.centre.x);

        // Which way round: the counter-clockwise tangent at the start angle
        // either agrees with the direction we were told to leave in, or it does
        // not, and that decides the sign of the sweep.
        const juce::Point<float> ccw (-std::sin (a.start), std::cos (a.start));

        a.sweep = wrapAngle (endAngle - a.start);

        if ((ccw.x * tangent.x + ccw.y * tangent.y) <= 0.0f)
            a.sweep -= kTwoPi;

        a.ok = true;
        return a;
    }

    void emitArc (juce::Path& path, const ArcSpec& a, float from, float sweep)
    {
        const int steps = juce::jlimit (4, 64, (int) std::lround (std::abs (sweep) * 20.0f));

        for (int i = 1; i <= steps; ++i)
        {
            const float t = from + sweep * ((float) i / (float) steps);
            path.lineTo (a.centre.x + std::cos (t) * a.radius,
                         a.centre.y + std::sin (t) * a.radius);
        }
    }

    /** Appends the curve from `from` through `via` to `to`, as a BIARC: two
        circular arcs meeting at `via` with a common tangent parallel to the
        chord.

        This is what gives the via point's position along the span a meaning.
        A single circle through three points is still a circle wherever you put
        the middle one — sliding it along only moves the circle. A biarc gives
        each half its own radius, computed from how far that half has to travel:
        park the point equidistant and both radii match, so the two halves are
        one circle and it reads as a true round. Slide it toward one end and
        that half tightens while the other goes lazy, which is the oval you get
        between a corner and an arc.

        The tangent is taken parallel to the chord because that is what the
        symmetric case demands — at the apex of a circular bulge the tangent IS
        parallel to the chord — so the round case falls out exactly rather than
        being approximated. */
    bool appendArc (juce::Path& path,
                    juce::Point<float> from, juce::Point<float> via, juce::Point<float> to)
    {
        const auto chord = to - from;
        const float length = chord.getDistanceFromOrigin();

        if (length < 1.0e-6f)
            return false;

        const juce::Point<float> dir (chord.x / length, chord.y / length);

        // The second half leaves `via` along the chord; the first half arrives
        // there, so it is computed backwards and drawn in reverse.
        const auto second = arcFrom (via, dir, to);
        const auto first  = arcFrom (via, { -dir.x, -dir.y }, from);

        if (! first.ok && ! second.ok)
            return false;

        if (first.ok) emitArc (path, first, first.start + first.sweep, -first.sweep);
        else          path.lineTo (via);

        if (second.ok) emitArc (path, second, second.start, second.sweep);
        else           path.lineTo (to);

        return true;
    }
}

//==============================================================================
juce::StringArray nodeModeNames()
{
    return { "Corner", "Arc" };
}

//==============================================================================
juce::Path ShapeDoc::buildPath() const
{
    juce::Path path;

    const int n = (int) nodes.size();

    if (n < 2)
        return path;

    auto at = [this, n] (int i) -> const ShapeNode& { return nodes[(size_t) (((i % n) + n) % n)]; };

    // An open path cannot bow its endpoints — an arc needs a neighbour on each
    // side — so they behave as corners however they are set.
    auto modeOf = [this, n, &at] (int i)
    {
        if (! closed && (i == 0 || i == n - 1))
            return NodeMode::Corner;

        return at (i).mode;
    };

    // Where one node's contribution hands over to the next. A corner owns the
    // handover point outright, because the outline genuinely passes through it.
    // Two arcs side by side split the difference, which is what lets a run of
    // arc points curve continuously instead of fighting over a shared end.
    auto boundary = [&at, &modeOf] (int i, int j)
    {
        const bool a = modeOf (i) == NodeMode::Arc;
        const bool b = modeOf (j) == NodeMode::Arc;

        if (a && b) return (at (i).pos + at (j).pos) * 0.5f;
        if (a)      return at (j).pos;

        return at (i).pos;
    };

    // Every node is visited exactly once, and only ever reads its OWN mode.
    //
    // The previous version walked the ring consuming two nodes per arc — the
    // via point and its far end — so whether a node's Arc setting was honoured
    // depended on the parity of where the walk happened to be when it arrived.
    // Two points placed symmetrically could therefore behave differently, which
    // is exactly the fault this replaces.
    bool started = false;

    auto moveTo = [&path, &started] (juce::Point<float> p)
    {
        if (! started) { path.startNewSubPath (p); started = true; }
        else           { path.lineTo (p); }
    };

    for (int i = 0; i < n; ++i)
    {
        if (modeOf (i) != NodeMode::Arc)
        {
            moveTo (at (i).pos);
            continue;
        }

        const auto entry = boundary (i - 1, i);
        const auto exit  = boundary (i, i + 1);

        moveTo (entry);

        if (! appendArc (path, entry, at (i).pos, exit))
        {
            // Too near a straight line to define a circle: two segments through
            // the node rather than a circumcentre out at infinity.
            path.lineTo (at (i).pos);
            path.lineTo (exit);
        }
    }

    if (closed)
        path.closeSubPath();

    return path;
}

//==============================================================================
juce::String ShapeDoc::toString() const
{
    juce::StringArray parts;

    parts.add (closed ? "closed" : "open");

    for (const auto& node : nodes)
        parts.add (juce::String (node.pos.x, 5) + "," + juce::String (node.pos.y, 5)
                     + "," + juce::String ((int) node.mode));

    return parts.joinIntoString (";");
}

ShapeDoc ShapeDoc::fromString (const juce::String& text)
{
    ShapeDoc doc;

    juce::StringArray parts;
    parts.addTokens (text, ";", {});
    parts.removeEmptyStrings();

    if (parts.isEmpty())
        return doc;

    int first = 0;

    if (parts[0] == "closed" || parts[0] == "open")
    {
        doc.closed = (parts[0] == "closed");
        first = 1;
    }

    for (int i = first; i < parts.size(); ++i)
    {
        juce::StringArray f;
        f.addTokens (parts[i], ",", {});

        if (f.size() < 2)
            continue;

        ShapeNode node;
        node.pos = { f[0].getFloatValue(), f[1].getFloatValue() };

        if (f.size() > 2)
            node.mode = (NodeMode) juce::jlimit (0, (int) NodeMode::numModes - 1,
                                                 f[2].getIntValue());

        doc.nodes.push_back (node);
    }

    return doc;
}

//==============================================================================
ShapeDoc ShapeDoc::pointerExample()
{
    // The Davies 1900H silhouette: a round skirt with a sharp nose. Five nodes
    // and three arcs, which is the case the whole editor exists for.
    ShapeDoc doc;
    doc.name = "Pointer";

    doc.nodes = { { { 0.50f, 0.06f }, NodeMode::Corner },   // nose
                  { { 0.72f, 0.30f }, NodeMode::Arc    },   // right shoulder
                  { { 0.94f, 0.66f }, NodeMode::Corner },
                  { { 0.50f, 0.96f }, NodeMode::Arc    },   // skirt
                  { { 0.06f, 0.66f }, NodeMode::Corner },
                  { { 0.28f, 0.30f }, NodeMode::Arc    } }; // left shoulder

    return doc;
}

ShapeDoc ShapeDoc::teardropExample()
{
    ShapeDoc doc;
    doc.name = "Teardrop";

    doc.nodes = { { { 0.50f, 0.04f }, NodeMode::Corner },
                  { { 0.90f, 0.50f }, NodeMode::Arc    },
                  { { 0.50f, 0.96f }, NodeMode::Corner },
                  { { 0.10f, 0.50f }, NodeMode::Arc    } };

    return doc;
}

ShapeDoc ShapeDoc::arrowExample()
{
    ShapeDoc doc;
    doc.name = "Arrow";

    doc.nodes = { { { 0.50f, 0.04f }, NodeMode::Corner },
                  { { 0.88f, 0.46f }, NodeMode::Corner },
                  { { 0.64f, 0.46f }, NodeMode::Corner },
                  { { 0.64f, 0.96f }, NodeMode::Corner },
                  { { 0.36f, 0.96f }, NodeMode::Corner },
                  { { 0.36f, 0.46f }, NodeMode::Corner },
                  { { 0.12f, 0.46f }, NodeMode::Corner } };

    return doc;
}

juce::StringArray ShapeDoc::exampleNames()
{
    return { "Pointer", "Teardrop", "Arrow" };
}

ShapeDoc ShapeDoc::exampleByIndex (int index)
{
    switch (index)
    {
        case 1:  return teardropExample();
        case 2:  return arrowExample();
        default: return pointerExample();
    }
}

//==============================================================================
namespace
{
    int libraryRevision = 0;
}

juce::File ShapeLibrary::folder()
{
    // Stella: beside the studio's primitives, looks and textures.
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Stella AI Studio")
               .getChildFile ("Shapes");
}

juce::StringArray ShapeLibrary::names()
{
    juce::StringArray out;

    auto dir = folder();

    if (! dir.isDirectory())
        return out;

    juce::Array<juce::File> files;
    dir.findChildFiles (files, juce::File::findFiles, false, "*.fkshape");

    for (const auto& f : files)
        out.add (f.getFileNameWithoutExtension());

    out.sort (true);
    return out;
}

ShapeDoc ShapeLibrary::load (const juce::String& name)
{
    auto file = folder().getChildFile (juce::File::createLegalFileName (name) + ".fkshape");

    if (! file.existsAsFile())
        return {};

    auto doc = ShapeDoc::fromString (file.loadFileAsString());
    doc.name = name;
    return doc;
}

bool ShapeLibrary::save (const ShapeDoc& doc)
{
    if (doc.name.isEmpty())
        return false;

    auto dir = folder();
    dir.createDirectory();

    auto file = dir.getChildFile (juce::File::createLegalFileName (doc.name) + ".fkshape");

    if (! file.replaceWithText (doc.toString()))
        return false;

    ++libraryRevision;
    return true;
}

bool ShapeLibrary::remove (const juce::String& name)
{
    auto file = folder().getChildFile (juce::File::createLegalFileName (name) + ".fkshape");

    if (! file.existsAsFile() || ! file.deleteFile())
        return false;

    ++libraryRevision;
    return true;
}

int ShapeLibrary::revision()
{
    return libraryRevision;
}
