// C:\workspace\Stella AI Studio\src\ShapeWorkspace.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    ShapeWorkspace.h

    The Shapes tab: the editor on the left, the inventory on the right.

    Shapes live on disk in their own folder rather than inside a document, so one
    house pointer outline can be pulled into every plugin you build instead of
    being redrawn per project. A Shape layer stores the NAME, so editing the
    shape here updates every layer that uses it.
*/

#pragma once

#include "ShapeEditor.h"

#include <functional>

//==============================================================================
class ShapeWorkspace final : public juce::Component,
                             private juce::ListBoxModel
{
public:
    ShapeWorkspace();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Refreshes the inventory list — call when the tab is shown. */
    void refreshInventory();

    std::function<void (const juce::String&)> onStatus;

private:
    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void saveCurrent();
    void deleteSelected();
    void status (const juce::String&);

    ShapeEditor editor;

    juce::StringArray inventory;
    juce::ListBox     list { "shapes", this };

    juce::Label      nameLabel { {}, "Name" };
    juce::TextEditor nameBox;

    juce::TextButton newButton    { "New" };
    juce::TextButton saveButton   { "Save to inventory" };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton folderButton { "Folder" };

    juce::Label      gridLabel { {}, "Grid" };
    juce::Slider     gridSlider;
    juce::ToggleButton snapButton { "Snap to grid" };

    juce::Label      exampleLabel { {}, "Start from" };
    juce::ComboBox   exampleBox;

    juce::Label      statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeWorkspace)
};
