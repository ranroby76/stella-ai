// C:\workspace\Stella AI Studio\src\SchematicView.cpp

#include "SchematicView.h"
#include "StellaLookAndFeel.h"

namespace
{
    const juce::Colour wireColour   { 0xffe5484d };   // Stella red
    const juce::Colour probeColour  { 0xffffcc00 };   // the yellow of the account pills
    constexpr int scopeSamples = 1024;
}

//==============================================================================
SchematicView::BlockPanel::BlockPanel()
{
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Skip this block: its first input goes straight to its first output");
    bypassButton.onClick = [this] { if (onBypass != nullptr) onBypass (bypassButton.getToggleState()); };

    instruction.setMultiLine (true, true);
    instruction.setReturnKeyStartsNewLine (false);
    instruction.setFont (Theme::font (14.0f));
    instruction.setTextToShowWhenEmpty (juce::String::fromUTF8 ("What should change in this block?\xe2\x80\xa6"), Theme::muted);
    instruction.onReturnKey = [this] { sendButton.triggerClick(); };

    sendButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    sendButton.onClick = [this]
    {
        const auto text = instruction.getText().trim();

        if (text.isNotEmpty() && onSend != nullptr)
        {
            onSend (text);
            instruction.clear();
        }
    };

    addAndMakeVisible (bypassButton);
    addAndMakeVisible (instruction);
    addAndMakeVisible (sendButton);
}

void SchematicView::BlockPanel::show (const Node& node)
{
    title = node.id;
    subtitle = node.subtitle;
    bypassButton.setToggleState (node.bypassed, juce::dontSendNotification);
    repaint();
}

void SchematicView::BlockPanel::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();

    g.setColour (Theme::panel.withAlpha (0.97f));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

    auto area = getLocalBounds().reduced (14, 12);

    g.setColour (Theme::text);
    g.setFont (Theme::font (17.0f, true));
    g.drawText (title, area.removeFromTop (22), juce::Justification::centredLeft, true);

    g.setColour (Theme::muted);
    g.setFont (Theme::font (13.0f));
    g.drawText (subtitle, area.removeFromTop (18), juce::Justification::centredLeft, true);

    area.removeFromTop (54);
    g.drawText ("Tell Stella AI what to change here:", area.removeFromTop (18), juce::Justification::centredLeft, true);
}

void SchematicView::BlockPanel::resized()
{
    auto area = getLocalBounds().reduced (14, 12);
    area.removeFromTop (48);
    bypassButton.setBounds (area.removeFromTop (30).removeFromLeft (100));
    area.removeFromTop (30);
    sendButton.setBounds (area.removeFromBottom (30));
    area.removeFromBottom (8);
    instruction.setBounds (area);
}

//==============================================================================
SchematicView::SchematicView()
{
    panel.onBypass = [this] (bool off)
    {
        if (juce::isPositiveAndBelow (selected, nodes.size()))
        {
            nodes.getReference (selected).bypassed = off;

            if (onBypass != nullptr)
                onBypass (nodes[selected].id, off);

            repaint();
        }
    };

    panel.onSend = [this] (const juce::String& text)
    {
        if (juce::isPositiveAndBelow (selected, nodes.size()) && onAskAi != nullptr)
            onAskAi (nodes[selected].id, nodes[selected].type, text);
    };

    addChildComponent (panel);
    scope.resize ((size_t) scopeSamples, 0.0f);
}

SchematicView::~SchematicView()
{
    stopTimer();
}

//==============================================================================
void SchematicView::setGraph (bool projectOpen, bool isEffect, const juce::Array<Block>& blocks, const juce::Array<Wire>& wires)
{
    hasProject = projectOpen;

    const auto previouslySelected = juce::isPositiveAndBelow (selected, nodes.size()) ? nodes[selected].id : juce::String();
    const auto previouslyProbed = juce::isPositiveAndBelow (probed, links.size())
                                      ? nodes[links[probed].from].id + "|" + juce::String (links[probed].fromPort)
                                      : juce::String();

    nodes.clear();
    links.clear();
    selected = probed = -1;

    if (! projectOpen)
    {
        panel.setVisible (false);
        stopTimer();
        repaint();
        return;
    }

    bool usesInput = isEffect;

    for (const auto& wire : wires)
        if (wire.fromModule == "plugin")
            usesInput = true;

    if (usesInput)
    {
        Node input;
        input.id = "plugin_in";
        input.title = "AUDIO IN";
        input.outputs = { "in L", "in R" };
        input.isPluginIo = true;
        nodes.add (input);
    }

    for (const auto& block : blocks)
    {
        Node node;
        node.id = block.id;
        node.type = block.type;
        node.title = block.id;
        node.subtitle = (block.name.isNotEmpty() ? block.name : block.type) + juce::String::fromUTF8 (" \xc2\xb7 modules/") + block.type + ".cpp";
        node.inputs = block.inputs;
        node.outputs = block.outputs;
        node.bypassed = block.bypassed;
        nodes.add (node);
    }

    Node output;
    output.id = "plugin_out";
    output.title = "AUDIO OUT";
    output.inputs = { "out L", "out R" };
    output.isPluginIo = true;
    nodes.add (output);

    auto find = [this] (const juce::String& id)
    {
        for (int i = 0; i < nodes.size(); ++i)
            if (nodes[i].id == id)
                return i;

        return -1;
    };

    for (const auto& wire : wires)
    {
        Link link;
        link.from = wire.fromModule == "plugin" ? find ("plugin_in") : find (wire.fromModule);
        link.to = wire.toModule == "plugin" ? find ("plugin_out") : find (wire.toModule);

        if (link.from < 0 || link.to < 0)
            continue;

        link.fromPort = nodes[link.from].outputs.indexOf (wire.fromPort);
        link.toPort = nodes[link.to].inputs.indexOf (wire.toPort);

        if (link.fromPort >= 0 && link.toPort >= 0)
            links.add (link);
    }

    layout();

    // Keep the selection and the probe across rebuilds when they still exist.
    if (previouslySelected.isNotEmpty())
        select (find (previouslySelected));
    else
        panel.setVisible (false);

    for (int i = 0; i < links.size(); ++i)
        if (nodes[links[i].from].id + "|" + juce::String (links[i].fromPort) == previouslyProbed)
            probed = i;

    if (probed < 0 && previouslyProbed.isNotEmpty() && onProbe != nullptr)
        onProbe ({}, 0);

    if (probed >= 0) startTimerHz (30); else stopTimer();

    updateLinks();
    repaint();
}

void SchematicView::setPositions (const juce::var& positions)
{
    saved.clear();

    if (auto* object = positions.getDynamicObject())
        for (const auto& property : object->getProperties())
            if (const auto* pair = property.value.getArray(); pair != nullptr && pair->size() == 2)
                saved[property.name.toString()] = { (float) (double) pair->getUnchecked (0), (float) (double) pair->getUnchecked (1) };

    layout();
    updateLinks();
    repaint();
}

juce::var SchematicView::getPositions() const
{
    auto* object = new juce::DynamicObject();

    for (const auto& node : nodes)
        object->setProperty (node.id, juce::Array<juce::var> { juce::roundToInt (node.position.x), juce::roundToInt (node.position.y) });

    return juce::var (object);
}

//==============================================================================
void SchematicView::layout()
{
    // Columns by depth: every block to the right of the blocks feeding it. AUDIO IN is the
    // first column, AUDIO OUT the last.
    juce::Array<int> depth;

    for (const auto& node : nodes)
        depth.add (node.id == "plugin_in" ? 0 : 1);

    for (int pass = 0; pass < nodes.size(); ++pass)
        for (const auto& link : links)
            if (nodes[link.to].id != "plugin_out")
                depth.set (link.to, juce::jmax (depth[link.to], depth[link.from] + 1));

    int deepest = 0;

    for (int i = 0; i < nodes.size(); ++i)
        if (! nodes[i].isPluginIo)
            deepest = juce::jmax (deepest, depth[i]);

    for (int i = 0; i < nodes.size(); ++i)
        if (nodes[i].id == "plugin_out")
            depth.set (i, deepest + 1);

    std::map<int, float> nextY;

    for (int i = 0; i < nodes.size(); ++i)
    {
        auto& node = nodes.getReference (i);

        if (const auto found = saved.find (node.id); found != saved.end())
        {
            node.position = found->second;
            continue;
        }

        auto& y = nextY[depth[i]];
        node.position = { 40.0f + (float) depth[i] * (nodeWidth + 70.0f), 40.0f + y };
        y += nodeHeight (node) + 36.0f;
    }
}

float SchematicView::nodeHeight (const Node& node) const
{
    const auto rows = juce::jmax (1, node.inputs.size(), node.outputs.size());
    return headerHeight + (float) rows * rowHeight + 8.0f;
}

juce::Rectangle<float> SchematicView::nodeBounds (const Node& node) const
{
    return { node.position.x + offset.x, node.position.y + offset.y, nodeWidth, nodeHeight (node) };
}

juce::Point<float> SchematicView::inputPoint (const Node& node, int port) const
{
    const auto b = nodeBounds (node);
    return { b.getX(), b.getY() + headerHeight + ((float) port + 0.5f) * rowHeight };
}

juce::Point<float> SchematicView::outputPoint (const Node& node, int port) const
{
    const auto b = nodeBounds (node);
    return { b.getRight(), b.getY() + headerHeight + ((float) port + 0.5f) * rowHeight };
}

void SchematicView::updateLinks()
{
    for (auto& link : links)
    {
        const auto a = outputPoint (nodes[link.from], link.fromPort);
        const auto b = inputPoint (nodes[link.to], link.toPort);
        const auto bend = juce::jmax (40.0f, std::abs (b.x - a.x) * 0.5f);

        link.path.clear();
        link.path.startNewSubPath (a);
        link.path.cubicTo (a.translated (bend, 0.0f), b.translated (-bend, 0.0f), b);
    }
}

int SchematicView::nodeAt (juce::Point<float> point) const
{
    for (int i = nodes.size(); --i >= 0;)
        if (nodeBounds (nodes[i]).contains (point))
            return i;

    return -1;
}

int SchematicView::linkAt (juce::Point<float> point) const
{
    for (int i = links.size(); --i >= 0;)
    {
        // getReference: Array's [] returns a copy, and the iterator must not outlive its path.
        juce::Point<float> nearest;
        juce::PathFlatteningIterator flat (links.getReference (i).path);
        juce::Line<float> segment;

        while (flat.next())
        {
            segment = { flat.x1, flat.y1, flat.x2, flat.y2 };

            if (segment.getDistanceFromPoint (point, nearest) < 6.0f)
                return i;
        }
    }

    return -1;
}

void SchematicView::select (int index)
{
    selected = index;

    if (juce::isPositiveAndBelow (selected, nodes.size()) && ! nodes[selected].isPluginIo)
    {
        panel.show (nodes[selected]);
        panel.setVisible (true);
    }
    else
    {
        selected = -1;
        panel.setVisible (false);
    }

    repaint();
}

juce::Rectangle<int> SchematicView::scopeArea() const
{
    return getLocalBounds().reduced (16).removeFromBottom (140).removeFromLeft (juce::jmin (380, getWidth() - 32));
}

//==============================================================================
void SchematicView::mouseDown (const juce::MouseEvent& e)
{
    const auto point = e.position;
    moved = false;
    dragStart = point;

    if (const auto node = nodeAt (point); node >= 0)
    {
        dragging = node;
        dragOrigin = nodes[node].position;
        select (node);
        return;
    }

    if (const auto link = linkAt (point); link >= 0)
    {
        // Click a wire to watch it; click it again to stop.
        probed = probed == link ? -1 : link;

        if (onProbe != nullptr)
        {
            if (probed < 0)
                onProbe ({}, 0);
            else
            {
                const auto& source = nodes[links[probed].from];
                onProbe (source.id == "plugin_in" ? juce::String ("plugin") : source.id, links[probed].fromPort);
            }
        }

        if (probed >= 0) startTimerHz (30); else stopTimer();
        repaint();
        return;
    }

    select (-1);
    panning = true;
    dragOrigin = offset;
}

void SchematicView::mouseDrag (const juce::MouseEvent& e)
{
    const auto delta = e.position - dragStart;

    if (delta.getDistanceFromOrigin() > 3.0f)
        moved = true;

    if (dragging >= 0)
    {
        nodes.getReference (dragging).position = dragOrigin + delta;
        updateLinks();
        repaint();
    }
    else if (panning)
    {
        offset = dragOrigin + delta;
        updateLinks();
        repaint();
    }
}

void SchematicView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0 && moved)
    {
        saved[nodes[dragging].id] = nodes[dragging].position;

        if (onPositionsChanged != nullptr)
            onPositionsChanged();
    }

    dragging = -1;
    panning = false;
}

void SchematicView::timerCallback()
{
    repaint (scopeArea());
}

//==============================================================================
void SchematicView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::inset);

    // The dot grid moves with the canvas.
    g.setColour (Theme::text.withAlpha (0.07f));
    const auto ox = (int) std::fmod (offset.x, 24.0f), oy = (int) std::fmod (offset.y, 24.0f);

    for (int y = 12 + oy - 24; y < getHeight(); y += 24)
        for (int x = 12 + ox - 24; x < getWidth(); x += 24)
            g.fillRect (x, y, 2, 2);

    if (! hasProject || nodes.size() <= 1)
    {
        auto area = getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 40), 90);
        g.setColour (Theme::text);
        g.setFont (Theme::font (20.0f, true));
        g.drawText (hasProject ? "No blocks yet" : "No project open", area.removeFromTop (30), juce::Justification::centred, false);
        g.setColour (Theme::muted);
        g.setFont (Theme::font (14.5f));
        g.drawFittedText (hasProject ? "Describe your plugin to Stella AI: its blocks and wires appear here once it's built."
                                     : "Start a new plugin or open one from the top bar.",
                          area, juce::Justification::centredTop, 3, 1.0f);
        return;
    }

    // Wires under the blocks.
    for (int i = 0; i < links.size(); ++i)
    {
        const bool isProbed = i == probed;
        g.setColour (isProbed ? probeColour : wireColour.withAlpha (nodes[links[i].to].bypassed ? 0.35f : 0.8f));
        g.strokePath (links[i].path, juce::PathStrokeType (isProbed ? 3.0f : 2.0f));
    }

    for (int i = 0; i < nodes.size(); ++i)
    {
        const auto& node = nodes[i];
        const auto box = nodeBounds (node);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (box.translated (0.0f, 3.0f), 7.0f);

        g.setColour (node.isPluginIo ? Theme::raised.darker (0.2f) : Theme::raised);
        g.fillRoundedRectangle (box, 7.0f);

        // Header: the block's id over its type, or AUDIO IN / OUT.
        const auto header = box.withHeight (headerHeight);
        const auto titleArea = juce::Rectangle<float> (box.getX() + 10.0f, box.getY() + (node.isPluginIo ? 9.0f : 5.0f), nodeWidth - 20.0f, 18.0f);

        g.setColour (node.bypassed ? Theme::muted : (node.isPluginIo ? Theme::muted.brighter (0.2f) : Theme::text));
        g.setFont (Theme::font (14.5f, true));
        g.drawText (node.title, titleArea, juce::Justification::centredLeft, true);

        if (! node.isPluginIo)
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::font (11.5f));
            g.drawText (node.type, titleArea.translated (0.0f, 16.0f).withHeight (13.0f), juce::Justification::centredLeft, true);
        }

        if (node.bypassed)
        {
            g.setColour (Theme::hot);
            g.setFont (Theme::font (10.5f, true));
            g.drawText ("BYPASSED", header.reduced (8.0f, 6.0f), juce::Justification::topRight, false);
        }

        g.setColour (Theme::outline);
        g.drawHorizontalLine ((int) (box.getY() + headerHeight - 2.0f), box.getX() + 1.0f, box.getRight() - 1.0f);

        // Ports, named inside the block.
        g.setFont (Theme::font (12.0f));

        for (int p = 0; p < node.inputs.size(); ++p)
        {
            const auto at = inputPoint (node, p);
            g.setColour (Theme::text.withAlpha (0.75f));
            g.drawText (node.inputs[p], juce::Rectangle<float> (at.x + 10.0f, at.y - 9.0f, nodeWidth * 0.5f - 12.0f, 18.0f),
                        juce::Justification::centredLeft, true);
            g.setColour (wireColour);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (at));
        }

        for (int p = 0; p < node.outputs.size(); ++p)
        {
            const auto at = outputPoint (node, p);
            g.setColour (Theme::text.withAlpha (0.75f));
            g.drawText (node.outputs[p], juce::Rectangle<float> (at.x - nodeWidth * 0.5f, at.y - 9.0f, nodeWidth * 0.5f - 10.0f, 18.0f),
                        juce::Justification::centredRight, true);
            g.setColour (wireColour);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (at));
        }

        // Outline: selected, bypassed (dashed) or plain.
        juce::Path outline;
        outline.addRoundedRectangle (box.reduced (0.5f), 7.0f);

        if (i == selected)
        {
            g.setColour (Theme::accent);
            g.strokePath (outline, juce::PathStrokeType (2.0f));
        }
        else if (node.bypassed)
        {
            juce::Path dashed;
            const float dashes[] { 6.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Theme::hot.withAlpha (0.8f));
            g.fillPath (dashed);
        }
        else
        {
            g.setColour (Theme::outline);
            g.strokePath (outline, juce::PathStrokeType (1.0f));
        }
    }

    // The scope, while a wire is watched.
    if (juce::isPositiveAndBelow (probed, links.size()))
    {
        const auto area = scopeArea().toFloat();

        g.setColour (Theme::panel.withAlpha (0.96f));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (probeColour.withAlpha (0.8f));
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

        const auto& link = links[probed];
        const auto& source = nodes[link.from];
        const auto name = (source.id == "plugin_in" ? juce::String ("audio in") : source.id) + "." + source.outputs[link.fromPort];

        if (readProbe != nullptr)
            readProbe (scope.data(), scopeSamples);

        float peak = 0.0f;

        for (const auto v : scope)
            peak = juce::jmax (peak, std::abs (v));

        auto text = area.reduced (12.0f, 8.0f);
        auto top = text.removeFromTop (18.0f);

        g.setColour (Theme::text);
        g.setFont (Theme::font (13.0f, true));
        g.drawText (juce::String::fromUTF8 ("Probe \xc2\xb7 ") + name, top, juce::Justification::centredLeft, true);

        g.setColour (peak >= 1.0f ? Theme::clip : Theme::muted);
        g.setFont (Theme::monoFont (12.5f));
        g.drawText (peak > 0.0f ? juce::String (20.0f * std::log10 (peak), 1) + " dB peak" : juce::String ("silent"),
                    top, juce::Justification::centredRight, false);

        const auto wave = text.reduced (0.0f, 4.0f);
        g.setColour (Theme::outline);
        g.drawHorizontalLine (juce::roundToInt (wave.getCentreY()), wave.getX(), wave.getRight());

        juce::Path trace;

        for (int i = 0; i < scopeSamples; ++i)
        {
            const auto x = wave.getX() + wave.getWidth() * (float) i / (float) (scopeSamples - 1);
            const auto y = wave.getCentreY() - juce::jlimit (-1.0f, 1.0f, scope[(size_t) i]) * wave.getHeight() * 0.5f;

            if (i == 0) trace.startNewSubPath (x, y);
            else        trace.lineTo (x, y);
        }

        g.setColour (probeColour);
        g.strokePath (trace, juce::PathStrokeType (1.5f));
    }
}

void SchematicView::resized()
{
    // Bottom right: the blocks start at the top left, so the panel covers as few as possible.
    panel.setBounds (getLocalBounds().reduced (14).removeFromRight (300).removeFromBottom (290));
    updateLinks();
}
