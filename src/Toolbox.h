// C:\workspace\Stella AI Studio\src\Toolbox.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

//==============================================================================
/**
    The Edit UI tab's toolbox, FlowStone-style: every primitive by category, with a search
    box on top. Drag one onto the plugin window to add it there, or double-click it to add
    it in the middle of what's showing.

    It must sit inside a DragAndDropContainer that also holds the canvas.
*/
class Toolbox final : public juce::Component
{
public:
    Toolbox();
    ~Toolbox() override;

    /** A double-click: add this primitive in the middle of what's showing. */
    std::function<void (const juce::String& primitiveId)> onAdd;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** A primitive's icon, by its "icon" name (unknown names get a plain square). */
    static void drawIcon (juce::Graphics&, juce::Rectangle<float> area, const juce::String& icon);

    static constexpr int width = 214;

private:
    class Item;
    class List;

    juce::TextEditor search;
    juce::Viewport viewport;
    std::unique_ptr<List> list;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toolbox)
};
