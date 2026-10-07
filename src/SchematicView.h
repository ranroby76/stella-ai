// C:\workspace\Stella AI Studio\src\SchematicView.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>

//==============================================================================
/**
    The Schematic tab: the plugin's modules as blocks, wired as in graph.json, with the
    plugin's own audio in and out at the edges.

        click a block     select it: bypass it, or tell Stella AI what to change in it
        drag a block      move it (positions are kept with the project)
        click a wire      watch that signal on the scope; click it again to stop
        drag the canvas   look around
*/
class SchematicView final : public juce::Component,
                            private juce::Timer
{
public:
    struct Block
    {
        juce::String id, type, name;
        juce::StringArray inputs, outputs;
        bool bypassed = false;
    };

    struct Wire
    {
        juce::String fromModule, fromPort, toModule, toPort;
    };

    SchematicView();
    ~SchematicView() override;

    void setGraph (bool projectOpen, bool isEffect, const juce::Array<Block>& blocks, const juce::Array<Wire>& wires);

    /** Saved block positions: { "moduleId": [x, y], ... }. */
    void setPositions (const juce::var& saved);
    juce::var getPositions() const;

    std::function<void (const juce::String& moduleId, bool bypassed)> onBypass;
    std::function<void (const juce::String& moduleId, int port)> onProbe;   // empty id: stop watching
    std::function<void (const juce::String& moduleId, const juce::String& type, const juce::String& instruction)> onAskAi;
    std::function<void()> onPositionsChanged;
    std::function<void (float* destination, int numSamples)> readProbe;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Node
    {
        juce::String id, title, subtitle, type;
        juce::StringArray inputs, outputs;
        bool bypassed = false, isPluginIo = false;
        juce::Point<float> position;   // top-left, in canvas coordinates
    };

    struct Link
    {
        int from = -1, fromPort = 0, to = -1, toPort = 0;
        juce::Path path;               // in view coordinates, for drawing and clicking
    };

    //==========================================================================
    /** The selected block's panel: bypass, and an instruction for Stella AI. */
    class BlockPanel final : public juce::Component
    {
    public:
        BlockPanel();
        void show (const Node& node);
        void paint (juce::Graphics&) override;
        void resized() override;

        std::function<void (bool bypassed)> onBypass;
        std::function<void (const juce::String& instruction)> onSend;

    private:
        juce::String title, subtitle;
        juce::TextButton bypassButton { "Bypass" }, sendButton { "Send to Stella AI" };
        juce::TextEditor instruction;
    };

    void timerCallback() override;
    void layout();
    void updateLinks();
    float nodeHeight (const Node& node) const;
    juce::Rectangle<float> nodeBounds (const Node& node) const;   // view coordinates
    juce::Point<float> inputPoint (const Node& node, int port) const;
    juce::Point<float> outputPoint (const Node& node, int port) const;
    int nodeAt (juce::Point<float> point) const;
    int linkAt (juce::Point<float> point) const;
    void select (int index);
    juce::Rectangle<int> scopeArea() const;

    juce::Array<Node> nodes;
    juce::Array<Link> links;
    std::map<juce::String, juce::Point<float>> saved;

    juce::Point<float> offset { 0.0f, 0.0f };
    int selected = -1, dragging = -1, probed = -1;
    bool panning = false, moved = false, hasProject = false;
    juce::Point<float> dragStart, dragOrigin;

    BlockPanel panel;
    std::vector<float> scope;

    static constexpr float nodeWidth = 172.0f, headerHeight = 40.0f, rowHeight = 22.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SchematicView)
};
