// C:\workspace\Stella AI Studio\src\KnobStudio.h

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "GuiLayout.h"
#include "PreviewComponent.h"
#include "StyleEditorPanel.h"

#include <functional>

//==============================================================================
/**
    The Knob Studio tab: KnobMaker's knob editor, working on the plugin's own knob looks.

        left     the plugin's knob looks and how many knobs use each
        middle   KnobMaker's live preview: turn the big knob, and see it at plugin sizes
        right    KnobMaker's style editor: lighting, geometry, flutes, pointer, colours

    Every change shows on the plugin at once, on every knob that uses the look. Opened
    from a knob (double-click in Design mode), that knob can get a look of its own.
*/
class KnobStudio final : public juce::Component,
                         private juce::ListBoxModel
{
public:
    KnobStudio();
    ~KnobStudio() override;

    /** The looks and who uses them; the selection stays if the look still exists. */
    void setLayout (const GuiLayout& layout);

    /** Opens the look of this knob (an index into the layout's widgets). */
    void editKnob (int widgetIndex);

    std::function<void (const juce::String& name, const KnobStyle& style)> onStyleChanged;   // also adds a new look
    std::function<juce::String (int widgetIndex)> onMakeUnique;                              // returns the new look's name
    std::function<void()> onBack;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void select (const juce::String& name);
    int usersOf (const juce::String& name) const;
    juce::String freeName (const juce::String& base) const;
    void addLook (const KnobStyle& style, const juce::String& base);
    void showNewMenu();

    GuiLayout layout;
    juce::StringArray names;
    juce::String current;
    int editingWidget = -1;

    juce::ListBox list { "Knob looks", this };
    juce::TextButton newButton { "+ New look" }, duplicateButton { "Duplicate" };
    juce::TextButton uniqueButton { "Give this knob its own look" }, backButton { "Back to the plugin" };

    PreviewComponent preview;
    StyleEditorPanel editor;
    juce::Viewport editorViewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobStudio)
};
