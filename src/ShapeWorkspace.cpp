// C:\workspace\Stella AI Studio\src\ShapeWorkspace.cpp
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    ShapeWorkspace.cpp
*/

#include "ShapeWorkspace.h"
#include "UiHelpers.h"

//==============================================================================
ShapeWorkspace::ShapeWorkspace()
{
    addAndMakeVisible (editor);

    editor.onChange = [this]
    {
        status (juce::String (editor.getShape().nodes.size()) + " points");
    };

    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff202023));
    addAndMakeVisible (list);

    nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    nameLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (nameLabel);

    nameBox.setTextToShowWhenEmpty ("Pointer", juce::Colours::grey);
    nameBox.setText ("Pointer", juce::dontSendNotification);
    addAndMakeVisible (nameBox);

    newButton.onClick    = [this] { editor.clearShape(); nameBox.setText ("Untitled",
                                                                          juce::dontSendNotification);
                                    status ("Empty shape — double-click to place points"); };
    saveButton.onClick   = [this] { saveCurrent(); };
    deleteButton.onClick = [this] { deleteSelected(); };
    folderButton.onClick = [this]
    {
        auto f = ShapeLibrary::folder();
        f.createDirectory();
        f.revealToUser();
    };

    for (auto* b : { &newButton, &saveButton, &deleteButton, &folderButton })
        addAndMakeVisible (b);

    gridLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    gridLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (gridLabel);

    gridSlider.setSliderStyle (juce::Slider::IncDecButtons);
    gridSlider.setRange (2.0, 64.0, 1.0);
    gridSlider.setValue (16.0, juce::dontSendNotification);
    gridSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 46, 20);
    gridSlider.onValueChange = [this] { editor.setGrid ((int) gridSlider.getValue()); };
    addAndMakeVisible (gridSlider);

    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.onClick = [this] { editor.setSnapEnabled (snapButton.getToggleState()); };
    addAndMakeVisible (snapButton);

    exampleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    exampleLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (exampleLabel);

    exampleBox.addItemList (ShapeDoc::exampleNames(), 1);
    exampleBox.setSelectedId (1, juce::dontSendNotification);
    exampleBox.onChange = [this]
    {
        auto doc = ShapeDoc::exampleByIndex (exampleBox.getSelectedId() - 1);
        editor.setShape (doc);
        nameBox.setText (doc.name, juce::dontSendNotification);
        status ("Loaded example: " + doc.name);
    };
    addAndMakeVisible (exampleBox);

    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (statusLabel);

    configurePanelControls (*this);

    editor.setShape (ShapeDoc::pointerExample());
    refreshInventory();
    status ("Double-click to place points.  Right-click a point for Corner / Arc.");
}

//==============================================================================
void ShapeWorkspace::status (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);

    if (onStatus != nullptr)
        onStatus (text);
}

void ShapeWorkspace::refreshInventory()
{
    inventory = ShapeLibrary::names();
    list.updateContent();
    list.repaint();
}

//==============================================================================
void ShapeWorkspace::saveCurrent()
{
    auto doc = editor.getShape();
    doc.name = nameBox.getText().trim();

    if (doc.name.isEmpty())
    {
        status ("Give the shape a name first.");
        return;
    }

    if (doc.nodes.size() < 2)
    {
        status ("A shape needs at least two points.");
        return;
    }

    if (! ShapeLibrary::save (doc))
    {
        status ("Could not write the shape to " + ShapeLibrary::folder().getFullPathName());
        return;
    }

    refreshInventory();
    status ("Saved \"" + doc.name + "\" — Shape layers referring to it update straight away.");
}

void ShapeWorkspace::deleteSelected()
{
    const int row = list.getSelectedRow();

    if (row < 0 || row >= inventory.size())
    {
        status ("Select a shape in the inventory first.");
        return;
    }

    const auto name = inventory[row];

    juce::Component::SafePointer<ShapeWorkspace> safe (this);

    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::QuestionIcon,
        "Delete shape",
        "Delete \"" + name + "\"? Layers referring to it will fall back to a plain outline.",
        "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe, name] (int result)
        {
            if (result != 1)
                return;

            if (auto* self = safe.getComponent())
            {
                ShapeLibrary::remove (name);
                self->refreshInventory();
                self->status ("Deleted \"" + name + "\"");
            }
        }));
}

//==============================================================================
int ShapeWorkspace::getNumRows()
{
    return inventory.size();
}

void ShapeWorkspace::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= inventory.size())
        return;

    if (selected)
    {
        g.setColour (juce::Colour (0xff3a5a7a));
        g.fillRect (0, 0, w, h);
    }

    // A thumbnail of the outline, because a list of names tells you nothing
    // about shapes.
    auto doc = ShapeLibrary::load (inventory[row]);

    if (! doc.isEmpty())
    {
        auto path = doc.buildPath();
        const float side = (float) h - 6.0f;
        path.applyTransform (juce::AffineTransform::scale (side, side).translated (4.0f, 3.0f));

        g.setColour (juce::Colour (0xffe8c020));
        g.fillPath (path);
    }

    g.setColour (juce::Colour (0xffd8d8dc));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (inventory[row], h + 8, 0, w - h - 12, h, juce::Justification::centredLeft, true);
}

void ShapeWorkspace::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= inventory.size())
        return;

    auto doc = ShapeLibrary::load (inventory[row]);

    if (doc.isEmpty())
    {
        status ("\"" + inventory[row] + "\" could not be read.");
        return;
    }

    editor.setShape (doc);
    nameBox.setText (doc.name, juce::dontSendNotification);
    status ("Editing \"" + doc.name + "\"");
}

//==============================================================================
void ShapeWorkspace::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e21));
}

void ShapeWorkspace::resized()
{
    auto area = getLocalBounds().reduced (8);

    statusLabel.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (6);

    auto right = area.removeFromRight (280);
    area.removeFromRight (8);

    {
        auto bar = area.removeFromTop (26);
        exampleLabel.setBounds (bar.removeFromLeft (70));
        exampleBox.setBounds (bar.removeFromLeft (130));
        bar.removeFromLeft (16);
        gridLabel.setBounds (bar.removeFromLeft (40));
        gridSlider.setBounds (bar.removeFromLeft (110));
        bar.removeFromLeft (10);
        snapButton.setBounds (bar.removeFromLeft (130));
    }

    area.removeFromTop (6);
    editor.setBounds (area);

    {
        auto bar = right.removeFromTop (26);
        nameLabel.setBounds (bar.removeFromLeft (46));
        nameBox.setBounds (bar);
    }

    right.removeFromTop (6);

    {
        auto bar = right.removeFromTop (26);
        newButton.setBounds (bar.removeFromLeft (60));
        bar.removeFromLeft (6);
        saveButton.setBounds (bar);
    }

    right.removeFromTop (6);

    {
        auto bar = right.removeFromBottom (26);
        deleteButton.setBounds (bar.removeFromLeft (80));
        bar.removeFromLeft (6);
        folderButton.setBounds (bar.removeFromLeft (80));
    }

    right.removeFromBottom (6);
    list.setBounds (right);
}
