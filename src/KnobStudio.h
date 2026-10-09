// C:\workspace\Stella AI Studio\src\KnobStudio.h

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "GuiLayout.h"
#include "LayerWorkspace.h"
#include "Looks.h"
#include "ShapeWorkspace.h"

#include <functional>

//==============================================================================
/**
    The Knob Studio tab: KnobMaker's layer builder, the KnobMan way, working on the looks of
    the plugin's knobs, sliders and switches.

        header   the look being edited and how many controls wear it; Use on this knob,
                 Give this knob its own look, Back to the plugin
        left     the plugin's looks (what its controls wear, then its own), with New look,
                 Duplicate, Save to my looks and Delete
        right    Looks: the layer builder (the preview, its frames, the layers, each layer's
                 shape and effects, Bevel & Emboss, textures). Shapes: the shape inventory
                 Shape layers draw from

    Every change is saved into the plugin's own looks at once and shows on the plugin. A
    built-in look (or one of mine) becomes the plugin's own the moment it's changed. Opened
    from a control (double-click in Edit UI), the studio shows that control's look, and a
    look made here goes onto it.
*/
class KnobStudio final : public juce::Component,
                         private juce::ListBoxModel
{
public:
    explicit KnobStudio (Looks& looksToEdit);
    ~KnobStudio() override;

    /** Which looks the plugin's controls wear, and how many wear each. */
    void setLayout (const GuiLayout& layout);

    /** Opens a control's look (an index into the layout's widgets); -1: no control. */
    void editControl (int widgetIndex);

    /** The looks changed: the list again, and the open look if it changed elsewhere. */
    void looksChanged();

    std::function<void (int widgetIndex, const juce::String& lookName)> onUseLook;   // a control takes a look
    std::function<juce::String (int widgetIndex)> onGiveOwnLook;                       // returns the new look's name
    std::function<void()> onBack;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void refreshList();
    void select (const juce::String& name);
    void updateButtons();
    int usersOf (const juce::String& name) const;
    const GuiWidget* editedControl() const;
    Looks::Kind kindForNew() const;
    juce::String controlName() const;   // "knob", "slider" or "switch"

    void showNewMenu();
    void addLook (const juce::String& baseName, Looks::Kind kind, const LayerDoc& doc, const juce::String& description = {});
    void openLookFile();
    void status (const juce::String& text);

    Looks& looks;
    GuiLayout layout;
    juce::StringArray names;     // the list: what the controls wear, then the plugin's own
    juce::String current;
    int loadedRevision = 0;      // the revision of the look showing in the builder
    int editingWidget = -1;
    bool saving = false;

    juce::ListBox list { "Looks", this };
    juce::TextButton newButton { "+ New look" }, duplicateButton { "Duplicate" };
    juce::TextButton mineButton { "Save to my looks" }, deleteButton { "Delete" };
    juce::TextButton useButton, uniqueButton, backButton { "Back to the plugin" };

    /** Looks and Shapes; the Shapes tab reads the inventory again when it's shown. */
    struct Tabs final : public juce::TabbedComponent
    {
        Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}
        std::function<void (int)> onChange;
        void currentTabChanged (int index, const juce::String&) override   { if (onChange != nullptr) onChange (index); }
    };

    LayerWorkspace layers;
    ShapeWorkspace shapes;
    Tabs tabs;
    juce::Rectangle<int> headerText, statusArea;
    juce::String statusText;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobStudio)
};
