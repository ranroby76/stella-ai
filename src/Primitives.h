// C:\workspace\Stella AI Studio\src\Primitives.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "GuiLayout.h"

//==============================================================================
/**
    A primitive: one item of the Edit UI tab's toolbox, FlowStone-style. Each is a small
    JSON file in the primitives folder, compiled into the app:

        { "name": "Knob", "category": "Controls", "order": 10, "icon": "knob",
          "description": "A round knob that turns one parameter.",
          "widget": { "type": "knob", "size": 56, "style": "black" } }

    "widget" is the element it makes, written like an element of gui/layout.json (without
    x and y: it lands where it's dropped). A primitive with "action": "background" makes no
    element: it sets the window's background picture.

    More primitives can sit in Documents\Stella AI Studio\Primitives: they're read at start
    up, and one with the same file name as a built-in one replaces it.
*/
struct Primitive
{
    juce::String id;                 // the file name, without .json
    juce::String name, category, icon, description;
    int order = 0;
    juce::String action;             // "background", or empty
    juce::var widget;                // the element, as in layout.json

    /** The element it makes, at the top-left of the plugin window. */
    GuiWidget makeWidget() const;

    bool setsBackground() const      { return action == "background"; }
};

//==============================================================================
class PrimitiveLibrary final
{
public:
    /** Read once, the first time it's asked for. */
    static const PrimitiveLibrary& get();

    /** All primitives, by order. */
    const juce::Array<Primitive>& getAll() const noexcept    { return primitives; }

    /** The categories, in the order of their first primitive. */
    juce::StringArray getCategories() const;

    const Primitive* find (const juce::String& id) const;

    /** Where the user's own primitives go: Documents\Stella AI Studio\Primitives. */
    static juce::File getUserFolder();

    /** What a toolbox drag carries: "primitive:<id>". */
    static juce::String dragDescription (const juce::String& id)    { return "primitive:" + id; }
    static juce::String idFromDrag (const juce::var& description);

private:
    PrimitiveLibrary();
    void add (const juce::String& id, const juce::String& json);

    juce::Array<Primitive> primitives;
};
