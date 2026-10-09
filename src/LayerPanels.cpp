// C:\workspace\Stella AI Studio\src\LayerPanels.cpp
// From KnobMaker (C:\workspace\knobmaker); in Stella a chosen picture is copied into the project
// (LayerImages::adopt), so the layer keeps its name, not a path on this computer.

/*
    LayerPanels.cpp
*/

#include "LayerPanels.h"
#include "BevelDialog.h"
#include "ShapeDoc.h"
#include "UiHelpers.h"
#include "TextureLibrary.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace
{
    constexpr int kLabelW = 104;
    constexpr int kGap    = 4;

    juce::Label* makeHeader (juce::Component& owner, juce::OwnedArray<juce::Label>& store,
                             const juce::String& text)
    {
        auto* l = store.add (new juce::Label ({}, text.toUpperCase()));
        l->setColour (juce::Label::textColourId, juce::Colour (0xff7fa8d0));
        l->setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        owner.addAndMakeVisible (l);
        return l;
    }
}

//==============================================================================
AnimRow::AnimRow (const juce::String& name, float min, float max, float interval)
{
    label.setText (name, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    label.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (label);

    for (auto* s : { &fromSlider, &toSlider })
    {
        s->setRange ((double) min, (double) max, (double) interval);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
        s->onValueChange = [this] { if (onChange != nullptr) onChange(); };
        addAndMakeVisible (s);
    }

    modeBox.addItemList (animModeNames(), 1);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.onChange = [this] { modeChanged(); };
    addAndMakeVisible (modeBox);

    modeChanged();
}

void AnimRow::modeChanged()
{
    const bool animated = modeBox.getSelectedId() != 1;   // 1 == Fixed
    toSlider.setEnabled (animated);
    toSlider.setAlpha (animated ? 1.0f : 0.4f);

    if (onChange != nullptr)
        onChange();
}

void AnimRow::setValue (const AnimVal& v, juce::NotificationType notify)
{
    fromSlider.setValue (v.from, juce::dontSendNotification);
    toSlider.setValue (v.to, juce::dontSendNotification);
    modeBox.setSelectedId ((int) v.mode + 1, juce::dontSendNotification);

    const bool animated = v.mode != AnimMode::Fixed;
    toSlider.setEnabled (animated);
    toSlider.setAlpha (animated ? 1.0f : 0.4f);

    if (notify != juce::dontSendNotification && onChange != nullptr)
        onChange();
}

AnimVal AnimRow::getValue() const noexcept
{
    AnimVal v;
    v.from = (float) fromSlider.getValue();
    v.to   = (float) toSlider.getValue();
    v.mode = (AnimMode) juce::jlimit (0, (int) AnimMode::numModes - 1,
                                      modeBox.getSelectedId() - 1);
    return v;
}

void AnimRow::paint (juce::Graphics&) {}

void AnimRow::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (kLabelW));
    modeBox.setBounds (r.removeFromRight (78).reduced (0, 2));
    r.removeFromRight (kGap);

    const int half = r.getWidth() / 2;
    fromSlider.setBounds (r.removeFromLeft (half - kGap / 2));
    r.removeFromLeft (kGap);
    toSlider.setBounds (r);
}

//==============================================================================
ValueRow::ValueRow (const juce::String& name, float min, float max, float interval)
{
    label.setText (name, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    label.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (label);

    slider.setRange ((double) min, (double) max, (double) interval);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    slider.onValueChange = [this] { if (onChange != nullptr) onChange(); };
    addAndMakeVisible (slider);
}

void ValueRow::setValue (float v, juce::NotificationType notify)
{
    slider.setValue ((double) v, notify);
}

void ValueRow::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (kLabelW));
    slider.setBounds (r.reduced (0, 2));
}

//==============================================================================
/** A bin, drawn rather than shipped as an asset so it scales with the row. */
class LayerTrashButton final : public juce::Button
{
public:
    LayerTrashButton() : juce::Button ("delete layer")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (3.0f);

        g.setColour (down ? juce::Colour (0xffff9a9a)
                          : (over ? juce::Colour (0xffe05a5a)
                                  : juce::Colour (0xff86868f)));

        const float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();

        g.fillRect (x + w * 0.36f, y,             w * 0.28f, h * 0.09f);   // handle
        g.fillRect (x,             y + h * 0.14f, w,         h * 0.10f);   // lid

        juce::Path body;
        body.startNewSubPath (x + w * 0.16f, y + h * 0.30f);
        body.lineTo          (x + w * 0.84f, y + h * 0.30f);
        body.lineTo          (x + w * 0.74f, y + h);
        body.lineTo          (x + w * 0.26f, y + h);
        body.closeSubPath();
        g.strokePath (body, juce::PathStrokeType (1.4f));

        g.fillRect (x + w * 0.38f, y + h * 0.44f, w * 0.06f, h * 0.40f);
        g.fillRect (x + w * 0.56f, y + h * 0.44f, w * 0.06f, h * 0.40f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerTrashButton)
};

//==============================================================================
/** One row of the stack. Owns its trash button and its drag behaviour; for
    everything else it defers to the panel, which is what lets rows be
    reordered without any of them holding a stale index. */
class LayerRow final : public juce::Component
{
public:
    explicit LayerRow (LayerListPanel& o) : owner (o)
    {
        setWantsKeyboardFocus (true);

        trash.onClick = [this] { owner.requestDelete (*this); };
        addAndMakeVisible (trash);
    }

    void paint (juce::Graphics& g) override { owner.paintRow (*this, g); }

    void resized() override
    {
        const int s = juce::jmin (20, juce::jmax (12, getHeight() - 6));
        trash.setBounds (getWidth() - s - 6, (getHeight() - s) / 2, s, s);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        dragStarted = false;
        owner.rowClicked (*this, e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        // A short wobble is a click, not a drag.
        if (! dragStarted)
        {
            if (e.getDistanceFromDragStart() < 4)
                return;

            dragStarted = true;
            owner.beginDrag (*this, e.getMouseDownY());
        }

        owner.dragTo (*this, e);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragStarted)
            return;

        dragStarted = false;
        owner.endDrag (*this);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            owner.requestDelete (*this);
            return true;
        }

        return false;
    }

private:
    LayerListPanel&   owner;
    LayerTrashButton  trash;
    bool dragStarted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerRow)
};

//==============================================================================
LayerListPanel::LayerListPanel()
{
    listViewport.setViewedComponent (&rowHolder, false);
    listViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (listViewport);

    addAndMakeVisible (addButton);
    addAndMakeVisible (dupButton);

    addButton.onClick = [this] { addLayer(); };
    dupButton.onClick = [this] { duplicateLayer(); };
}

LayerListPanel::~LayerListPanel() = default;

//==============================================================================
int LayerListPanel::numLayers() const noexcept
{
    return document != nullptr ? (int) document->layers.size() : 0;
}

int LayerListPanel::docIndexForRow (int row) const noexcept
{
    // The list shows the topmost layer first; the document stores bottom-first.
    return numLayers() - 1 - row;
}

//==============================================================================
void LayerListPanel::setDocument (LayerDoc* doc)
{
    document = doc;
    selectedIndex = 0;
    refresh();
}

void LayerListPanel::refresh()
{
    // Mid-drag the rows are positioned by hand and one of them is the component
    // currently receiving mouse events. Rebuilding the stack here would delete
    // it out from under the drag, so a live reorder only repaints.
    if (dragging)
    {
        for (auto* r : rows)
            r->repaint();

        return;
    }

    selectedIndex = juce::jlimit (0, juce::jmax (0, numLayers() - 1), selectedIndex);
    rebuildRows();
}

void LayerListPanel::setSelectedIndex (int i)
{
    selectedIndex = juce::jlimit (0, juce::jmax (0, numLayers() - 1), i);

    for (auto* r : rows)
        r->repaint();

    if (onSelectionChange != nullptr)
        onSelectionChange();
}

//==============================================================================
void LayerListPanel::rebuildRows()
{
    const int n = numLayers();

    while (rows.size() > n)
        rows.removeLast();

    while (rows.size() < n)
        rowHolder.addAndMakeVisible (rows.add (new LayerRow (*this)));

    layoutRows();

    for (auto* r : rows)
        r->repaint();
}

void LayerListPanel::layoutRows (int skipRow)
{
    const bool needsScrollbar = rows.size() * rowHeight > listViewport.getHeight();

    const int w = juce::jmax (60, listViewport.getWidth()
                                    - (needsScrollbar ? listViewport.getScrollBarThickness() : 0));

    rowHolder.setSize (w, juce::jmax (listViewport.getHeight(), rows.size() * rowHeight));

    for (int i = 0; i < rows.size(); ++i)
    {
        if (i == skipRow)
            rows.getUnchecked (i)->setSize (w, rowHeight);   // dragged: keep its y
        else
            rows.getUnchecked (i)->setBounds (0, i * rowHeight, w, rowHeight);
    }
}

//==============================================================================
void LayerListPanel::paintRow (LayerRow& r, juce::Graphics& g)
{
    const int row   = rows.indexOf (&r);
    const int index = docIndexForRow (row);

    if (document == nullptr || index < 0 || index >= numLayers())
        return;

    const auto& layer = document->layers[(size_t) index];

    const int w = r.getWidth();
    const int h = r.getHeight();

    if (row == selectedIndex)
    {
        g.setColour (juce::Colour (0xff3a5a7a));
        g.fillRect (0, 0, w, h);
    }

    // Grip lines: the affordance that says this row can be picked up.
    g.setColour (juce::Colour (0xff65656e));

    for (int i = 0; i < 3; ++i)
        g.fillRect (6, h / 2 - 4 + i * 4, 9, 1);

    g.setColour (layer.visible ? juce::Colour (0xff8fd08f) : juce::Colour (0xff555555));
    g.fillEllipse (22.0f, (float) h * 0.5f - 5.0f, 10.0f, 10.0f);

    if (layer.solo)
    {
        g.setColour (juce::Colour (0xffd0b060));
        g.fillEllipse (38.0f, (float) h * 0.5f - 5.0f, 10.0f, 10.0f);
    }

    g.setColour (juce::Colour (0xffd8d8dc));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (layer.name + "  (" + primTypeNames()[(int) layer.prim.type] + ")",
                56, 0, juce::jmax (10, w - 56 - 32), h,
                juce::Justification::centredLeft, true);
}

void LayerListPanel::rowClicked (LayerRow& r, const juce::MouseEvent& e)
{
    const int row   = rows.indexOf (&r);
    const int index = docIndexForRow (row);

    if (document == nullptr || index < 0 || index >= numLayers())
        return;

    auto& layer = document->layers[(size_t) index];

    auto select = [this, row]
    {
        if (row == selectedIndex)
            return;

        selectedIndex = row;

        for (auto* other : rows)
            other->repaint();

        if (onSelectionChange != nullptr)
            onSelectionChange();
    };

    if (e.mods.isPopupMenu())
    {
        select();
        showContextMenu (r);
        return;
    }

    if (e.x >= 18 && e.x < 34)
    {
        layer.visible = ! layer.visible;
        r.repaint();

        if (onDocumentChange != nullptr)
            onDocumentChange();

        return;
    }

    if (e.x >= 34 && e.x < 52)
    {
        layer.solo = ! layer.solo;
        r.repaint();

        if (onDocumentChange != nullptr)
            onDocumentChange();

        return;
    }

    select();
}

//==============================================================================
void LayerListPanel::beginDrag (LayerRow& r, int grabY)
{
    dragging  = true;
    dragGrabY = grabY;

    const int row = rows.indexOf (&r);

    if (row != selectedIndex)
    {
        selectedIndex = row;

        for (auto* other : rows)
            other->repaint();

        if (onSelectionChange != nullptr)
            onSelectionChange();
    }

    r.toFront (false);
    r.setAlpha (0.85f);
    r.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void LayerListPanel::dragTo (LayerRow& r, const juce::MouseEvent& e)
{
    if (! dragging || document == nullptr)
        return;

    const int n = rows.size();

    if (n < 2)
        return;

    const auto pos = e.getEventRelativeTo (&rowHolder).getPosition();
    const int  y   = juce::jlimit (0, (n - 1) * rowHeight, pos.y - dragGrabY);

    r.setTopLeftPosition (0, y);

    const int current = rows.indexOf (&r);
    const int target  = juce::jlimit (0, n - 1, (y + rowHeight / 2) / rowHeight);

    if (target == current)
        return;

    // Reorder the document and the row array together, then relay out everything
    // except the row under the cursor — that one stays where the mouse put it.
    document->moveLayer (docIndexForRow (current), docIndexForRow (target));
    rows.move (current, target);
    selectedIndex = target;

    layoutRows (target);

    for (auto* other : rows)
        other->repaint();

    if (onDocumentChange != nullptr)
        onDocumentChange();
}

void LayerListPanel::endDrag (LayerRow& r)
{
    dragging = false;

    r.setAlpha (1.0f);
    r.setMouseCursor (juce::MouseCursor::NormalCursor);

    layoutRows();

    for (auto* other : rows)
        other->repaint();

    if (onSelectionChange != nullptr)
        onSelectionChange();
}

//==============================================================================
void LayerListPanel::requestDelete (LayerRow& r)
{
    if (document == nullptr)
        return;

    const int index = docIndexForRow (rows.indexOf (&r));

    if (index < 0 || index >= numLayers())
        return;

    const auto name = document->layers[(size_t) index].name;

    juce::Component::SafePointer<LayerListPanel> safe (this);

    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::QuestionIcon,
        "Delete layer",
        "Delete \"" + name + "\"? This cannot be undone.",
        "Delete",
        "Cancel",
        this,
        juce::ModalCallbackFunction::create ([safe, index] (int result)
        {
            if (result != 1)
                return;

            if (auto* p = safe.getComponent())
                p->deleteLayerAt (index);
        }));
}

void LayerListPanel::showContextMenu (LayerRow& r)
{
    juce::PopupMenu m;
    m.addItem (1, "Duplicate layer");
    m.addItem (2, "Delete layer...");

    juce::Component::SafePointer<LayerListPanel> safe (this);
    juce::Component::SafePointer<LayerRow>       safeRow (&r);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&r),
                     [safe, safeRow] (int result)
    {
        auto* p   = safe.getComponent();
        auto* row = safeRow.getComponent();

        if (p == nullptr || row == nullptr)
            return;

        if (result == 1)       p->duplicateLayer();
        else if (result == 2)  p->requestDelete (*row);
    });
}

//==============================================================================
void LayerListPanel::addLayer()
{
    if (document == nullptr)
        return;

    DocLayer l;
    l.name = "Layer " + juce::String (numLayers() + 1);
    document->layers.push_back (l);
    selectedIndex = 0;

    refresh();

    // Selection first: the primitive and effect panels hold a DocLayer* into
    // the vector, and it may have just reallocated.
    if (onSelectionChange != nullptr)  onSelectionChange();
    if (onDocumentChange != nullptr)   onDocumentChange();
}

void LayerListPanel::duplicateLayer()
{
    if (document == nullptr || document->layers.empty())
        return;

    const int index = docIndexForRow (selectedIndex);

    if (index < 0 || index >= numLayers())
        return;

    auto copy = document->layers[(size_t) index];
    copy.name += " copy";
    document->layers.insert (document->layers.begin() + index + 1, copy);

    refresh();

    if (onSelectionChange != nullptr)  onSelectionChange();
    if (onDocumentChange != nullptr)   onDocumentChange();
}

void LayerListPanel::deleteLayerAt (int documentIndex)
{
    if (document == nullptr || documentIndex < 0 || documentIndex >= numLayers())
        return;

    document->removeLayer (documentIndex);
    selectedIndex = juce::jlimit (0, juce::jmax (0, numLayers() - 1), selectedIndex);

    refresh();

    if (onSelectionChange != nullptr)  onSelectionChange();
    if (onDocumentChange != nullptr)   onDocumentChange();
}

//==============================================================================
void LayerListPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262629));

    g.setColour (juce::Colour (0xff202023));
    g.fillRect (listViewport.getBounds());
}

void LayerListPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto buttons = r.removeFromBottom (26);

    const int w = buttons.getWidth() / 2;
    addButton.setBounds (buttons.removeFromLeft (w).reduced (1));
    dupButton.setBounds (buttons.reduced (1));

    r.removeFromBottom (4);
    listViewport.setBounds (r);

    layoutRows();
}

//==============================================================================
PrimitivePanel::PrimitivePanel()
{
    typeLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    typeLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (typeLabel);

    typeBox.addItemList (primTypeNames(), 1);
    typeBox.setSelectedId (4, juce::dontSendNotification);
    typeBox.onChange = [this] { pull(); resized(); };
    addAndMakeVisible (typeBox);

    colourLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    colourLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (colourLabel);

    colourButton.onClick = [this]
    {
        if (layer == nullptr)
            return;

        colourTarget = ColourTarget::Fill;

        auto selector = std::make_unique<juce::ColourSelector> (
            juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
          | juce::ColourSelector::showColourspace | juce::ColourSelector::showAlphaChannel);

        selector->setCurrentColour (layer->prim.colour, juce::dontSendNotification);
        selector->setSize (300, 380);
        selector->addChangeListener (this);

        juce::CallOutBox::launchAsynchronously (std::move (selector),
                                                colourButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (colourButton);

    auto addRow = [this] (const juce::String& name, float min, float max, float step)
    {
        auto* r = rows.add (new ValueRow (name, min, max, step));
        r->onChange = [this] { pull(); };
        addAndMakeVisible (r);
        return r;
    };

    addRow ("Aspect",        -100.0f, 100.0f, 1.0f);   // 0
    addRow ("Round",            0.0f, 100.0f, 1.0f);   // 1
    addRow ("Width",            0.0f, 100.0f, 1.0f);   // 2  ring/base/wave depth
    addRow ("Length",           0.0f, 100.0f, 1.0f);   // 3
    addRow ("Step",             1.0f, 128.0f, 1.0f);   // 4
    addRow ("Angle",         -180.0f, 180.0f, 1.0f);   // 5  rotation / wave lobes
    addRow ("Rim Bevel",     -100.0f, 100.0f, 1.0f);   // 6  circle family only
    addRow ("Rim Fade",         0.0f, 100.0f, 1.0f);   // 7  circle family only
    addRow ("Ambient",          0.0f, 100.0f, 1.0f);   // 8
    addRow ("Light Dir",        0.0f, 360.0f, 1.0f);   // 9
    addRow ("Specular",         0.0f, 100.0f, 1.0f);   // 10
    addRow ("Spec Width",       1.0f, 100.0f, 1.0f);   // 11
    addRow ("Edge Fade",        0.0f, 100.0f, 1.0f);   // 12  (KnobMan "Diffuse")
    addRow ("Gloss",            0.0f, 100.0f, 1.0f);   // 13  (our lighting)
    addRow ("Font Size",        4.0f, 200.0f, 1.0f);   // 14
    addRow ("Tex Depth",        0.0f, 100.0f, 1.0f);   // 15
    addRow ("Tex Scale",        2.0f, 200.0f, 1.0f);   // 16
    addRow ("Tex Angle",     -180.0f, 180.0f, 1.0f);   // 17
    addRow ("Outline W",       0.25f,  40.0f, 0.25f);  // 18
    addRow ("Ray Bend",      -90.0f,  90.0f, 1.0f);    // 19  RadiateLine only
    addRow ("Ray Skew",     -100.0f, 100.0f, 1.0f);    // 20  RadiateLine only
    addRow ("Ray Round",       0.0f, 100.0f, 1.0f);    // 21  RadiateLine only
    addRow ("Shadow Op",       0.0f, 100.0f, 1.0f);    // 22
    addRow ("Shadow X",     -100.0f, 100.0f, 0.5f);    // 23
    addRow ("Shadow Y",     -100.0f, 100.0f, 0.5f);    // 24
    addRow ("Shadow Size",     0.0f, 300.0f, 0.5f);    // 25  room permitting
    addRow ("Shadow Fade",     0.0f, 100.0f, 1.0f);    // 26
    addRow ("Inner Op",        0.0f, 100.0f, 1.0f);    // 27
    addRow ("Inner X",      -100.0f, 100.0f, 0.5f);    // 28
    addRow ("Inner Y",      -100.0f, 100.0f, 0.5f);    // 29
    addRow ("Inner Size",      0.0f, 300.0f, 0.5f);    // 30
    addRow ("Inner Fade",      0.0f, 100.0f, 1.0f);    // 31
    addRow ("Glow Op",         0.0f, 100.0f, 1.0f);    // 32
    addRow ("Glow Size",       0.0f, 300.0f, 0.5f);    // 33
    addRow ("Glow Fade",       0.0f, 100.0f, 1.0f);    // 34
    addRow ("Shorts Between",  0.0f,  31.0f, 1.0f);    // 35  meters only
    addRow ("Short Len",       0.0f, 100.0f, 1.0f);    // 36  meters only
    addRow ("Short Width",     0.0f, 100.0f, 1.0f);    // 37  meters only
    addRow ("Arc Start",    -180.0f, 180.0f, 1.0f);    // 38  MeterCircle only
    addRow ("Arc End",      -180.0f, 180.0f, 1.0f);    // 39  MeterCircle only
    addRow ("Intensity",       0.0f, 100.0f, 1.0f);    // 40  LED only
    addRow ("Core Size",       2.0f, 100.0f, 1.0f);    // 41  LED only
    addRow ("Off Level",       0.0f, 100.0f, 1.0f);    // 42  LED only
    addRow ("Bloom",           0.0f, 100.0f, 1.0f);    // 43  LED only
    addRow ("Bezel",           0.0f, 100.0f, 1.0f);    // 44  LED only
    addRow ("Outer Len",       0.0f, 100.0f, 1.0f);    // 45  meters only
    addRow ("Value Gap",       0.0f, 100.0f, 1.0f);    // 46  meters only
    addRow ("Lens Size",       5.0f, 100.0f, 1.0f);    // 47  LED only
    addRow ("Shadow Curve",    0.0f, 100.0f, 1.0f);    // 48
    addRow ("Shadow Hot",      0.0f, 100.0f, 1.0f);    // 49
    addRow ("Inner Curve",     0.0f, 100.0f, 1.0f);    // 50
    addRow ("Inner Hot",       0.0f, 100.0f, 1.0f);    // 51
    addRow ("Glow Curve",      0.0f, 100.0f, 1.0f);    // 52
    addRow ("Glow Hot",        0.0f, 100.0f, 1.0f);    // 53
                                                       //     appended, not
                                                       //     inserted: addRow
                                                       //     numbers by call
                                                       //     order, so slotting
                                                       //     one in the middle
                                                       //     silently re-points
                                                       //     every row after it

    shapePickLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    shapePickLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (shapePickLabel);

    shapePickBox.onChange = [this] { pull(); };
    addAndMakeVisible (shapePickBox);

    shapeReloadButton.onClick = [this]
    {
        // The inventory lives on disk and the Shapes tab writes to it, so the
        // list here can go stale while a layer is selected.
        refillShapeNames();

        if (onChange != nullptr)
            onChange();
    };
    addAndMakeVisible (shapeReloadButton);

    meterLabelsLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    meterLabelsLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (meterLabelsLabel);

    meterLabelsBox.setMultiLine (false);
    meterLabelsBox.setTextToShowWhenEmpty ("0, , , , 5, , , , 10   —   @ leaves one blank",
                                           juce::Colours::grey);
    meterLabelsBox.onTextChange = [this] { pull(); };
    addAndMakeVisible (meterLabelsBox);

    meterLabelsInsideButton.onClick = [this] { pull(); };
    addAndMakeVisible (meterLabelsInsideButton);

    ledHeader.setColour (juce::Label::textColourId, juce::Colour (0xff7fa8d0));
    ledHeader.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    addAndMakeVisible (ledHeader);

    for (auto* l : { &ledShapeLabel, &ledBezelColourLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
        l->setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (l);
    }

    ledShapeBox.addItemList (ledShapeNames(), 1);
    ledShapeBox.onChange = [this] { pull(); resized(); };
    addAndMakeVisible (ledShapeBox);

    ledOnButton.onClick     = [this] { pull(); };
    ledFollowButton.onClick = [this] { pull(); resized(); };
    addAndMakeVisible (ledOnButton);
    addAndMakeVisible (ledFollowButton);

    ledBezelColourButton.onClick = [this]
    {
        if (layer == nullptr)
            return;

        colourTarget = ColourTarget::LedBezel;

        auto selector = std::make_unique<juce::ColourSelector> (
            juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
          | juce::ColourSelector::showColourspace | juce::ColourSelector::showAlphaChannel);

        selector->setCurrentColour (layer->prim.ledBezelColour, juce::dontSendNotification);
        selector->setSize (300, 380);
        selector->addChangeListener (this);

        juce::CallOutBox::launchAsynchronously (std::move (selector),
                                                ledBezelColourButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (ledBezelColourButton);

    // Inner shadow and outer glow are built the same way as the drop shadow, so
    // they are wired the same way too.
    auto setUpEdgeBlock = [this] (juce::Label& header, juce::ToggleButton& enable,
                                  juce::Label& label, juce::TextButton& swatch,
                                  juce::Label& blendLabel, juce::ComboBox& blendBox,
                                  ColourTarget target, ShadowSpec PrimitiveSpec::* member)
    {
        blendLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
        blendLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (blendLabel);

        blendBox.addItemList (blendModeNames(), 1);
        blendBox.onChange = [this] { pull(); };
        addAndMakeVisible (blendBox);

        header.setColour (juce::Label::textColourId, juce::Colour (0xff7fa8d0));
        header.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        addAndMakeVisible (header);

        label.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
        label.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (label);

        enable.onClick = [this] { pull(); resized(); };
        addAndMakeVisible (enable);

        swatch.onClick = [this, target, member, &swatch]
        {
            if (layer == nullptr)
                return;

            colourTarget = target;

            auto selector = std::make_unique<juce::ColourSelector> (
                juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
              | juce::ColourSelector::showColourspace | juce::ColourSelector::showAlphaChannel);

            selector->setCurrentColour ((layer->prim.*member).colour, juce::dontSendNotification);
            selector->setSize (300, 380);
            selector->addChangeListener (this);

            juce::CallOutBox::launchAsynchronously (std::move (selector),
                                                    swatch.getScreenBounds(), nullptr);
        };
        addAndMakeVisible (swatch);
    };

    setUpEdgeBlock (innerHeader, innerEnableButton, innerColourLabel, innerColourButton,
                    innerBlendLabel, innerBlendBox,
                    ColourTarget::InnerShadow, &PrimitiveSpec::innerShadow);

    setUpEdgeBlock (glowHeader, glowEnableButton, glowColourLabel, glowColourButton,
                    glowBlendLabel, glowBlendBox,
                    ColourTarget::Glow, &PrimitiveSpec::outerGlow);

    shadowBlendLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    shadowBlendLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (shadowBlendLabel);

    shadowBlendBox.addItemList (blendModeNames(), 1);
    shadowBlendBox.onChange = [this] { pull(); };
    addAndMakeVisible (shadowBlendBox);

    shadowHeader.setColour (juce::Label::textColourId, juce::Colour (0xff7fa8d0));
    shadowHeader.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    addAndMakeVisible (shadowHeader);

    shadowColourLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    shadowColourLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (shadowColourLabel);

    shadowEnableButton.onClick   = [this] { pull(); resized(); };
    shadowKnockoutButton.onClick = [this] { pull(); };
    addAndMakeVisible (shadowEnableButton);
    addAndMakeVisible (shadowKnockoutButton);

    shadowColourButton.onClick = [this]
    {
        if (layer == nullptr)
            return;

        colourTarget = ColourTarget::Shadow;

        auto selector = std::make_unique<juce::ColourSelector> (
            juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
          | juce::ColourSelector::showColourspace | juce::ColourSelector::showAlphaChannel);

        selector->setCurrentColour (layer->prim.shadow.colour, juce::dontSendNotification);
        selector->setSize (300, 380);
        selector->addChangeListener (this);

        juce::CallOutBox::launchAsynchronously (std::move (selector),
                                                shadowColourButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (shadowColourButton);

    bevelButton.onClick = [this]
    {
        if (layer != nullptr)
            BevelDialog::launch (*this);
    };
    addAndMakeVisible (bevelButton);

    strokeHeader.setColour (juce::Label::textColourId, juce::Colour (0xff7fa8d0));
    strokeHeader.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    addAndMakeVisible (strokeHeader);

    for (auto* l : { &strokeColourLabel, &strokePosLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
        l->setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (l);
    }

    strokeEnableButton.onClick = [this] { pull(); resized(); };
    addAndMakeVisible (strokeEnableButton);

    strokeColourButton.onClick = [this]
    {
        if (layer == nullptr)
            return;

        colourTarget = ColourTarget::Stroke;

        auto selector = std::make_unique<juce::ColourSelector> (
            juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
          | juce::ColourSelector::showColourspace | juce::ColourSelector::showAlphaChannel);

        selector->setCurrentColour (layer->prim.strokeColour, juce::dontSendNotification);
        selector->setSize (300, 380);
        selector->addChangeListener (this);

        juce::CallOutBox::launchAsynchronously (std::move (selector),
                                                strokeColourButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (strokeColourButton);

    strokePosBox.addItemList (strokePositionNames(), 1);
    strokePosBox.setSelectedId (1, juce::dontSendNotification);
    strokePosBox.onChange = [this] { pull(); };
    addAndMakeVisible (strokePosBox);

    textureHeader.setColour (juce::Label::textColourId, juce::Colour (0xff7fa8d0));
    textureHeader.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    addAndMakeVisible (textureHeader);

    for (auto* l : { &texModeLabel, &texNameLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
        l->setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (l);
    }

    texModeBox.addItemList (textureModeNames(), 1);
    texModeBox.setSelectedId (1, juce::dontSendNotification);
    texModeBox.onChange = [this] { pull(); resized(); };
    addAndMakeVisible (texModeBox);

    refillTextureNames();
    texNameBox.onChange = [this] { pull(); };
    addAndMakeVisible (texNameBox);

    // Point the library at wherever the user actually keeps their textures —
    // their own library, or a JKnobMan install's Texture directory. The choice
    // is written to settings.xml, so it holds for every session after this one.
    texLocateButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Locate textures folder",
                                                       TextureLibrary::userFolder(),
                                                       juce::String());

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();

            if (folder == juce::File() || ! folder.isDirectory())
                return;

            TextureLibrary::setUserFolder (folder);
            refillTextureNames();
            updateTexturePathLabel();

            if (onChange != nullptr)
                onChange();
        });
    };
    addAndMakeVisible (texLocateButton);

    texFolderButton.onClick = [this]
    {
        auto folder = TextureLibrary::userFolder();
        folder.createDirectory();
        folder.revealToUser();
    };
    addAndMakeVisible (texFolderButton);

    texPathLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8a8a92));
    texPathLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    texPathLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (texPathLabel);

    texRescanButton.onClick = [this]
    {
        // Re-reads the folder and drops cached tiles, so an image dropped in
        // while the app was open shows up without a restart.
        TextureLibrary::rescan();
        refillTextureNames();
        updateTexturePathLabel();

        if (onChange != nullptr)
            onChange();
    };
    addAndMakeVisible (texRescanButton);

    updateTexturePathLabel();

    textLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    textLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (textLabel);

    textEditor.onTextChange = [this] { pull(); };
    addAndMakeVisible (textEditor);

    for (auto* b : { &boldButton, &italicButton, &fillButton })
    {
        b->onClick = [this] { pull(); };
        addAndMakeVisible (b);
    }

    fileLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
    fileLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (fileLabel);

    fileButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose image", juce::File(),
                                                       "*.png;*.jpg;*.jpeg;*.bmp;*.gif");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();

            if (f == juce::File() || layer == nullptr)
                return;

            // Stella: copied into the project's pictures, so the look opens anywhere.
            layer->prim.imageFile = LayerImages::adopt (f);
            fileButton.setButtonText (f.getFileName());

            if (onChange != nullptr)
                onChange();
        });
    };
    addAndMakeVisible (fileButton);

    // The wheel belongs to the Viewport, not to whichever control happens to be
    // sitting under the pointer.
    configurePanelControls (*this);
}

void PrimitivePanel::refillShapeNames()
{
    const auto wanted = layer != nullptr ? layer->prim.shapeName : shapePickBox.getText();

    auto names = ShapeLibrary::names();

    shapePickBox.clear (juce::dontSendNotification);
    shapePickBox.addItem ("(none)", 1);
    shapePickBox.addItemList (names, 2);

    const int index = names.indexOf (wanted);

    shapePickBox.setSelectedId (index >= 0 ? index + 2 : 1, juce::dontSendNotification);
}

void PrimitivePanel::refillTextureNames()
{
    const auto wanted = layer != nullptr ? layer->prim.textureName : texNameBox.getText();

    texNameBox.clear (juce::dontSendNotification);
    texNameBox.addItemList (TextureLibrary::availableNames(), 1);

    // Keep the document's choice selected even when the file behind it is not
    // in the folder right now — losing it silently would rewrite the document.
    const int index = TextureLibrary::availableNames().indexOf (wanted);

    if (index >= 0)
        texNameBox.setSelectedId (index + 1, juce::dontSendNotification);
    else if (wanted.isNotEmpty())
        texNameBox.setText (wanted + "  (missing)", juce::dontSendNotification);
}

void PrimitivePanel::bevelChanged()
{
    if (onChange != nullptr)
        onChange();
}

void PrimitivePanel::updateTexturePathLabel()
{
    auto folder = TextureLibrary::userFolder();

    const int count = folder.isDirectory()
                        ? folder.getNumberOfChildFiles (juce::File::findFiles,
                                                        "*.png;*.jpg;*.jpeg;*.bmp;*.gif")
                        : 0;

    texPathLabel.setText (folder.getFullPathName()
                            + "   -   " + juce::String (count) + " image"
                            + (count == 1 ? "" : "s")
                            + (TextureLibrary::isUsingCustomFolder() ? "" : "  (default)"),
                          juce::dontSendNotification);
}

void PrimitivePanel::setLayer (DocLayer* l)
{
    layer = l;
    refresh();
}

void PrimitivePanel::refresh()
{
    if (layer == nullptr)
        return;

    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& p = layer->prim;

    typeBox.setSelectedId ((int) p.type + 1, juce::dontSendNotification);
    colourButton.setColour (juce::TextButton::buttonColourId, p.colour);
    colourButton.setButtonText (p.colour.toDisplayString (true));

    rows[0]->setValue (p.aspect,        juce::dontSendNotification);
    rows[1]->setValue (p.round,         juce::dontSendNotification);
    rows[2]->setValue (p.width,         juce::dontSendNotification);
    rows[3]->setValue (p.length,        juce::dontSendNotification);
    rows[4]->setValue ((float) p.step,  juce::dontSendNotification);
    rows[5]->setValue (p.angleStep,     juce::dontSendNotification);
    rows[6]->setValue (p.emboss,        juce::dontSendNotification);
    rows[7]->setValue (p.embossDiffuse, juce::dontSendNotification);
    rows[8]->setValue (p.ambient,       juce::dontSendNotification);
    rows[9]->setValue (p.lightDir,      juce::dontSendNotification);
    rows[10]->setValue (p.specular,     juce::dontSendNotification);
    rows[11]->setValue (p.specularWidth,juce::dontSendNotification);
    rows[12]->setValue (p.diffuse,      juce::dontSendNotification);
    rows[13]->setValue (p.gloss,        juce::dontSendNotification);
    rows[14]->setValue (p.fontSize,     juce::dontSendNotification);
    rows[15]->setValue (p.textureDepth, juce::dontSendNotification);
    rows[16]->setValue (p.textureScale, juce::dontSendNotification);
    rows[17]->setValue (p.textureAngle, juce::dontSendNotification);
    rows[18]->setValue (p.strokeWidth,  juce::dontSendNotification);
    rows[19]->setValue (p.rayBend,      juce::dontSendNotification);
    rows[20]->setValue (p.raySkew,      juce::dontSendNotification);
    rows[21]->setValue (p.rayRoundness, juce::dontSendNotification);
    rows[22]->setValue (p.shadow.opacity, juce::dontSendNotification);
    rows[23]->setValue (p.shadow.offsetX, juce::dontSendNotification);
    rows[24]->setValue (p.shadow.offsetY, juce::dontSendNotification);
    rows[25]->setValue (p.shadow.size,    juce::dontSendNotification);
    rows[26]->setValue (p.shadow.fade,    juce::dontSendNotification);

    rows[27]->setValue (p.innerShadow.opacity, juce::dontSendNotification);
    rows[28]->setValue (p.innerShadow.offsetX, juce::dontSendNotification);
    rows[29]->setValue (p.innerShadow.offsetY, juce::dontSendNotification);
    rows[30]->setValue (p.innerShadow.size,    juce::dontSendNotification);
    rows[31]->setValue (p.innerShadow.fade,    juce::dontSendNotification);
    rows[32]->setValue (p.outerGlow.opacity,   juce::dontSendNotification);
    rows[33]->setValue (p.outerGlow.size,      juce::dontSendNotification);
    rows[34]->setValue (p.outerGlow.fade,      juce::dontSendNotification);
    // Stored as "a long every N"; shown as "how many shorts sit between two
    // longs", which is the way you actually think about a scale.
    rows[35]->setValue ((float) (p.meterEvery - 1), juce::dontSendNotification);
    rows[36]->setValue (p.meterMinorLen,       juce::dontSendNotification);
    rows[37]->setValue (p.meterMinorWidth,     juce::dontSendNotification);
    rows[38]->setValue (p.meterStart,          juce::dontSendNotification);
    rows[39]->setValue (p.meterEnd,            juce::dontSendNotification);
    rows[40]->setValue (p.ledIntensity, juce::dontSendNotification);
    rows[41]->setValue (p.ledCore,      juce::dontSendNotification);
    rows[42]->setValue (p.ledOffLevel,  juce::dontSendNotification);
    rows[43]->setValue (p.ledGlow,      juce::dontSendNotification);
    rows[44]->setValue (p.ledBezel,     juce::dontSendNotification);
    rows[45]->setValue (p.meterOuterLen, juce::dontSendNotification);
    rows[46]->setValue (p.meterLabelGap, juce::dontSendNotification);
    rows[47]->setValue (p.ledSize,       juce::dontSendNotification);
    rows[48]->setValue (p.shadow.curve,        juce::dontSendNotification);
    rows[49]->setValue (p.shadow.hotCore,      juce::dontSendNotification);
    rows[50]->setValue (p.innerShadow.curve,   juce::dontSendNotification);
    rows[51]->setValue (p.innerShadow.hotCore, juce::dontSendNotification);
    rows[52]->setValue (p.outerGlow.curve,     juce::dontSendNotification);
    rows[53]->setValue (p.outerGlow.hotCore,   juce::dontSendNotification);

    if (meterLabelsBox.getText() != p.meterLabels)
        meterLabelsBox.setText (p.meterLabels, juce::dontSendNotification);

    meterLabelsInsideButton.setToggleState (p.meterLabelsInside, juce::dontSendNotification);

    ledShapeBox.setSelectedId ((int) p.ledShape + 1, juce::dontSendNotification);
    ledOnButton.setToggleState (p.ledOn, juce::dontSendNotification);
    ledFollowButton.setToggleState (p.ledFollowFrame, juce::dontSendNotification);
    ledBezelColourButton.setColour (juce::TextButton::buttonColourId, p.ledBezelColour);
    ledBezelColourButton.setButtonText (p.ledBezelColour.toDisplayString (true));

    shadowBlendBox.setSelectedId ((int) p.shadow.blend + 1,      juce::dontSendNotification);
    innerBlendBox.setSelectedId  ((int) p.innerShadow.blend + 1, juce::dontSendNotification);
    glowBlendBox.setSelectedId   ((int) p.outerGlow.blend + 1,   juce::dontSendNotification);

    innerEnableButton.setToggleState (p.innerShadow.enabled, juce::dontSendNotification);
    innerColourButton.setColour (juce::TextButton::buttonColourId, p.innerShadow.colour);
    innerColourButton.setButtonText (p.innerShadow.colour.toDisplayString (true));

    glowEnableButton.setToggleState (p.outerGlow.enabled, juce::dontSendNotification);
    glowColourButton.setColour (juce::TextButton::buttonColourId, p.outerGlow.colour);
    glowColourButton.setButtonText (p.outerGlow.colour.toDisplayString (true));

    shadowEnableButton.setToggleState (p.shadow.enabled, juce::dontSendNotification);
    shadowKnockoutButton.setToggleState (p.shadow.knockout, juce::dontSendNotification);
    shadowColourButton.setColour (juce::TextButton::buttonColourId, p.shadow.colour);
    shadowColourButton.setButtonText (p.shadow.colour.toDisplayString (true));

    strokeEnableButton.setToggleState (p.strokeEnabled, juce::dontSendNotification);
    strokeColourButton.setColour (juce::TextButton::buttonColourId, p.strokeColour);
    strokeColourButton.setButtonText (p.strokeColour.toDisplayString (true));
    strokePosBox.setSelectedId ((int) p.strokePosition + 1, juce::dontSendNotification);

    refillShapeNames();

    texModeBox.setSelectedId ((int) p.textureMode + 1, juce::dontSendNotification);
    refillTextureNames();
    updateTexturePathLabel();

    textEditor.setText (p.text, juce::dontSendNotification);
    boldButton.setToggleState (p.bold, juce::dontSendNotification);
    italicButton.setToggleState (p.italic, juce::dontSendNotification);
    fillButton.setToggleState (p.fill, juce::dontSendNotification);

    fileButton.setButtonText (p.imageFile.isEmpty() ? "Choose..."
                                                    : p.imageFile.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false));
    resized();
}

void PrimitivePanel::pull()
{
    if (updating || layer == nullptr)
        return;

    auto& p = layer->prim;

    p.type          = (PrimType) juce::jlimit (0, (int) PrimType::numTypes - 1,
                                               typeBox.getSelectedId() - 1);
    p.aspect        = rows[0]->getValue();
    p.round         = rows[1]->getValue();
    p.width         = rows[2]->getValue();
    p.length        = rows[3]->getValue();
    p.step          = (int) rows[4]->getValue();
    p.angleStep     = rows[5]->getValue();
    p.emboss        = rows[6]->getValue();
    p.embossDiffuse = rows[7]->getValue();
    p.ambient       = rows[8]->getValue();
    p.lightDir      = rows[9]->getValue();
    p.specular      = rows[10]->getValue();
    p.specularWidth = rows[11]->getValue();
    p.diffuse       = rows[12]->getValue();
    p.gloss         = rows[13]->getValue();
    p.fontSize      = rows[14]->getValue();
    p.textureDepth  = rows[15]->getValue();
    p.textureScale  = rows[16]->getValue();
    p.textureAngle  = rows[17]->getValue();
    p.strokeWidth   = rows[18]->getValue();
    p.rayBend       = rows[19]->getValue();
    p.raySkew       = rows[20]->getValue();
    p.rayRoundness  = rows[21]->getValue();
    p.shadow.opacity = rows[22]->getValue();
    p.shadow.offsetX = rows[23]->getValue();
    p.shadow.offsetY = rows[24]->getValue();
    p.shadow.size    = rows[25]->getValue();
    p.shadow.fade    = rows[26]->getValue();

    p.innerShadow.opacity = rows[27]->getValue();
    p.innerShadow.offsetX = rows[28]->getValue();
    p.innerShadow.offsetY = rows[29]->getValue();
    p.innerShadow.size    = rows[30]->getValue();
    p.innerShadow.fade    = rows[31]->getValue();
    p.outerGlow.opacity   = rows[32]->getValue();
    p.outerGlow.size      = rows[33]->getValue();
    p.outerGlow.fade      = rows[34]->getValue();
    p.meterEvery      = (int) rows[35]->getValue() + 1;
    p.meterMinorLen   = rows[36]->getValue();
    p.meterMinorWidth = rows[37]->getValue();
    p.meterStart      = rows[38]->getValue();
    p.meterEnd        = rows[39]->getValue();
    p.ledIntensity = rows[40]->getValue();
    p.ledCore      = rows[41]->getValue();
    p.ledOffLevel  = rows[42]->getValue();
    p.ledGlow      = rows[43]->getValue();
    p.ledBezel     = rows[44]->getValue();
    p.meterOuterLen = rows[45]->getValue();
    p.meterLabelGap = rows[46]->getValue();
    p.ledSize       = rows[47]->getValue();
    p.shadow.curve        = rows[48]->getValue();
    p.shadow.hotCore      = rows[49]->getValue();
    p.innerShadow.curve   = rows[50]->getValue();
    p.innerShadow.hotCore = rows[51]->getValue();
    p.outerGlow.curve     = rows[52]->getValue();
    p.outerGlow.hotCore   = rows[53]->getValue();

    p.meterLabels       = meterLabelsBox.getText();
    p.meterLabelsInside = meterLabelsInsideButton.getToggleState();

    p.ledShape       = (LedShape) juce::jlimit (0, (int) LedShape::numShapes - 1,
                                                ledShapeBox.getSelectedId() - 1);
    p.ledOn          = ledOnButton.getToggleState();
    p.ledFollowFrame = ledFollowButton.getToggleState();

    auto blendFrom = [] (const juce::ComboBox& box)
    {
        return (BlendMode) juce::jlimit (0, (int) BlendMode::numModes - 1, box.getSelectedId() - 1);
    };

    p.shadow.blend      = blendFrom (shadowBlendBox);
    p.innerShadow.blend = blendFrom (innerBlendBox);
    p.outerGlow.blend   = blendFrom (glowBlendBox);

    p.innerShadow.enabled = innerEnableButton.getToggleState();
    p.outerGlow.enabled   = glowEnableButton.getToggleState();

    p.shadow.enabled  = shadowEnableButton.getToggleState();
    p.shadow.knockout = shadowKnockoutButton.getToggleState();

    p.strokeEnabled  = strokeEnableButton.getToggleState();
    p.strokePosition = (StrokePosition) juce::jlimit (0, (int) StrokePosition::numPositions - 1,
                                                      strokePosBox.getSelectedId() - 1);

    // Item 1 is "(none)", so anything above it is a real inventory entry.
    p.shapeName = shapePickBox.getSelectedId() > 1 ? shapePickBox.getText() : juce::String();

    p.textureMode = (TextureMode) juce::jlimit (0, (int) TextureMode::numModes - 1,
                                                texModeBox.getSelectedId() - 1);

    if (texNameBox.getSelectedId() > 0)
        p.textureName = texNameBox.getText();

    p.text   = textEditor.getText();
    p.bold   = boldButton.getToggleState();
    p.italic = italicButton.getToggleState();
    p.fill   = fillButton.getToggleState();

    if (onChange != nullptr)
        onChange();
}

void PrimitivePanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (layer == nullptr)
        return;

    if (auto* selector = dynamic_cast<juce::ColourSelector*> (source))
    {
        auto apply = [selector] (juce::Colour& target, juce::TextButton& button)
        {
            target = selector->getCurrentColour();
            button.setColour (juce::TextButton::buttonColourId, target);
            button.setButtonText (target.toDisplayString (true));
        };

        switch (colourTarget)
        {
            case ColourTarget::Stroke: apply (layer->prim.strokeColour, strokeColourButton); break;
            case ColourTarget::Shadow: apply (layer->prim.shadow.colour, shadowColourButton); break;
            case ColourTarget::InnerShadow: apply (layer->prim.innerShadow.colour, innerColourButton); break;
            case ColourTarget::Glow:   apply (layer->prim.outerGlow.colour, glowColourButton); break;
            case ColourTarget::LedBezel: apply (layer->prim.ledBezelColour, ledBezelColourButton); break;
            case ColourTarget::Fill:
            default:                   apply (layer->prim.colour, colourButton); break;
        }

        if (onChange != nullptr)
            onChange();
    }
}

void PrimitivePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262629));
}

void PrimitivePanel::resized()
{
    auto area = getLocalBounds().reduced (8, 6);
    int y = area.getY();

    auto place = [&] (juce::Component& c, int h)
    {
        c.setBounds (area.getX(), y, area.getWidth(), h);
        y += h;
    };

    auto placeLabelled = [&] (juce::Component& lbl, juce::Component& ctl, int h)
    {
        lbl.setBounds (area.getX(), y, kLabelW, h);
        ctl.setBounds (area.getX() + kLabelW, y, area.getWidth() - kLabelW, h - 2);
        y += h;
    };

    placeLabelled (typeLabel, typeBox, 26);
    placeLabelled (colourLabel, colourButton, 26);

    const PrimType type = layer != nullptr ? layer->prim.type
                                           : (PrimType) (typeBox.getSelectedId() - 1);

    auto show = [&] (int index, bool visible)
    {
        rows[index]->setVisible (visible);

        if (visible)
            place (*rows[index], ValueRow::rowHeight);
    };

    // --- what this primitive is --------------------------------------------
    // All the plain type tests first, because the usesXxx rules below are built
    // out of them.
    const bool isShape = type != PrimType::None;
    const bool isText  = type == PrimType::Text;
    const bool isImage = type == PrimType::Image;
    const bool isLed   = type == PrimType::Led;
    const bool isMeter = type == PrimType::MeterCircle || type == PrimType::MeterLines;

    // The four analytic circle renderers. Ambient, Edge Fade, Rim Bevel and Rim
    // Fade exist only inside renderCircleFamily and renderRing — every other
    // primitive ignores them completely.
    const bool circleFam = type == PrimType::Circle || type == PrimType::CircleFill
                        || type == PrimType::MetalCircle || type == PrimType::Sphere;

    // Of those, the three that are shaded solids. A plain ring has no interior
    // to bevel, so Rim Bevel, Rim Fade and Ambient do nothing on it.
    const bool shadedCircle = type == PrimType::CircleFill
                           || type == PrimType::MetalCircle || type == PrimType::Sphere;

    // --- which shared rows it actually reads --------------------------------
    // WaveCircle is not in usesStep on purpose: its lobe count comes from Angle.
    const bool usesStep = type == PrimType::RadiateLine || type == PrimType::HLines
                       || type == PrimType::VLines
                       || type == PrimType::MetalCircle || isMeter;
    const bool usesRound = type == PrimType::Rect || type == PrimType::RectFill
                        || type == PrimType::Line || type == PrimType::Triangle
                        || isMeter || isLed;
    const bool usesAngle = type == PrimType::Triangle || type == PrimType::Line
                        || type == PrimType::RadiateLine || type == PrimType::WaveCircle
                        || type == PrimType::MeterLines || type == PrimType::Shape;
    const bool usesLength = type == PrimType::Line || type == PrimType::RadiateLine
                         || type == PrimType::Triangle || isMeter;

    // Width only reaches the renderer through strokeW, so the primitives that
    // never stroke anything cannot use it.
    const bool usesWidth = ! shadedCircle && ! isLed
                        && type != PrimType::RectFill
                        && type != PrimType::Text && type != PrimType::Image;

    // Font Size drives the meter's value text as well as the Text primitive, so
    // this has to be known before the shape rows are placed — show() positions
    // a row where it is called, and Font Size belongs up here for Text.
    const bool hasValues = isMeter && meterLabelsBox.getText().isNotEmpty();

    // Text was excluded here, but it lays out into `body` like everything else,
    // so Aspect does narrow its box. Hiding a control that works is the same
    // fault as showing one that does not.
    show (0, isShape);
    show (1, usesRound);
    show (2, usesWidth);
    show (3, usesLength);
    show (4, usesStep);
    show (5, usesAngle);

    // Rows 6 and 7 are the analytic rim built inside renderCircleFamily.
    show (6, shadedCircle);
    show (7, shadedCircle);

    show (8,  shadedCircle);            // Ambient
    show (9,  isShape);                 // Light Dir  — the shared bevel and gloss
    show (10, circleFam || isLed);      // Specular   — the LED reads it as rim shade
    show (11, shadedCircle || isLed);   // Spec Width
    show (12, circleFam);               // Edge Fade
    show (13, isShape);                 // Gloss      — applyGloss, every shape

    show (14, isText || hasValues);

    bevelButton.setVisible (isShape);

    if (isShape)
    {
        y += 4;
        bevelButton.setBounds (area.getX() + kLabelW, y, area.getWidth() - kLabelW, 24);
        y += 28;
    }


    // The three spoke-shaping rows only mean anything to RadiateLine.
    const bool isRadiate = type == PrimType::RadiateLine;

    // LED. Its own block, because none of these rows mean anything elsewhere.
    ledHeader.setVisible (isLed);
    ledShapeLabel.setVisible (isLed);
    ledShapeBox.setVisible (isLed);
    ledFollowButton.setVisible (isLed);

    // On is meaningless while brightness is being driven by the frame.
    const bool ledManual = isLed && ! ledFollowButton.getToggleState();
    ledOnButton.setVisible (ledManual);

    const bool hasBezel = isLed && rows[44]->getValue() > 0.5;
    ledBezelColourLabel.setVisible (hasBezel);
    ledBezelColourButton.setVisible (hasBezel);

    if (isLed)
    {
        y += 6;
        place (ledHeader, 22);
        placeLabelled (ledShapeLabel, ledShapeBox, 26);

        ledFollowButton.setBounds (area.getX() + kLabelW, y, area.getWidth() - kLabelW, 22);
        y += 26;

        if (ledManual)
        {
            ledOnButton.setBounds (area.getX() + kLabelW, y, 100, 22);
            y += 26;
        }
    }

    // Lens Size first — it is the one you reach for after switching the type,
    // because everything else is relative to how big the lens is.
    show (47, isLed);

    for (int i = 40; i <= 44; ++i)
        show (i, isLed);

    if (hasBezel)
        placeLabelled (ledBezelColourLabel, ledBezelColourButton, 26);

    // Meter scale rows. The arc is the MeterCircle's mask — where the scale
    // starts and stops — so it means nothing to the straight one.
    // Length is the inward reach for a meter, so say so rather than leaving a
    // bare "Length" next to an "Outer Len".
    rows[3]->setLabelText (isMeter ? "Inner Len" : "Length");

    // On an LED these two drive the rim shadow inside the lens, not a highlight.
    rows[10]->setLabelText (isLed ? "Inner Shade" : "Specular");
    rows[11]->setLabelText (isLed ? "Shade Width" : "Spec Width");

    show (45, isMeter);
    show (35, isMeter);

    // Values are only worth showing once there is something to hang them on.
    meterLabelsLabel.setVisible (isMeter);
    meterLabelsBox.setVisible (isMeter);
    meterLabelsInsideButton.setVisible (hasValues);

    if (isMeter)
        placeLabelled (meterLabelsLabel, meterLabelsBox, 26);

    if (hasValues)
    {
        meterLabelsInsideButton.setBounds (area.getX() + kLabelW, y, area.getWidth() - kLabelW, 22);
        y += 26;
    }

    show (46, hasValues);
    show (36, isMeter);
    show (37, isMeter);
    show (38, type == PrimType::MeterCircle);
    show (39, type == PrimType::MeterCircle);

    show (19, isRadiate);
    show (20, isRadiate);
    show (21, isRadiate);

    // Shape picker. Layers reference an inventory entry by name, so this is how
    // you point one at a shape without typing it.
    const bool isShapePrim = type == PrimType::Shape;

    shapePickLabel.setVisible (isShapePrim);
    shapePickBox.setVisible (isShapePrim);
    shapeReloadButton.setVisible (isShapePrim);

    if (isShapePrim)
    {
        y += 6;

        auto r = juce::Rectangle<int> (area.getX(), y, area.getWidth(), 26);
        shapePickLabel.setBounds (r.removeFromLeft (kLabelW));
        shapeReloadButton.setBounds (r.removeFromRight (68).reduced (0, 2));
        r.removeFromRight (6);
        shapePickBox.setBounds (r.reduced (0, 2));
        y += 30;
    }

    // Drop shadow. Cast by the finished silhouette, so it picks up the outline
    // and any mask along with the shape itself.
    shadowHeader.setVisible (isShape);
    shadowEnableButton.setVisible (isShape);

    const bool shadowOn = isShape && shadowEnableButton.getToggleState();

    shadowColourLabel.setVisible (shadowOn);
    shadowColourButton.setVisible (shadowOn);
    shadowKnockoutButton.setVisible (shadowOn);

    if (isShape)
    {
        y += 6;
        place (shadowHeader, 22);
        shadowEnableButton.setBounds (area.getX() + kLabelW, y, 100, 22);
        y += 26;
    }

    shadowBlendLabel.setVisible (shadowOn);
    shadowBlendBox.setVisible (shadowOn);

    if (shadowOn)
    {
        placeLabelled (shadowColourLabel, shadowColourButton, 26);
        placeLabelled (shadowBlendLabel, shadowBlendBox, 26);
        shadowKnockoutButton.setBounds (area.getX() + kLabelW, y, area.getWidth() - kLabelW, 22);
        y += 26;
    }

    for (int i : { 22, 23, 24, 25, 26, 48, 49 })
        show (i, shadowOn);

    // Inner shadow and outer glow, laid out exactly like the drop shadow above.
    auto placeEdgeBlock = [&] (juce::Label& header, juce::ToggleButton& enable,
                               juce::Label& label, juce::TextButton& swatch,
                               juce::Label& blendLabel, juce::ComboBox& blendBox,
                               std::initializer_list<int> blockRows) -> void
    {
        header.setVisible (isShape);
        enable.setVisible (isShape);

        const bool on = isShape && enable.getToggleState();

        label.setVisible (on);
        swatch.setVisible (on);
        blendLabel.setVisible (on);
        blendBox.setVisible (on);

        if (isShape)
        {
            y += 6;
            place (header, 22);
            enable.setBounds (area.getX() + kLabelW, y, 100, 22);
            y += 26;
        }

        if (on)
        {
            placeLabelled (label, swatch, 26);
            placeLabelled (blendLabel, blendBox, 26);
        }

        for (int i : blockRows)
            show (i, on);
    };

    placeEdgeBlock (innerHeader, innerEnableButton, innerColourLabel, innerColourButton,
                    innerBlendLabel, innerBlendBox, { 27, 28, 29, 30, 31, 50, 51 });
    placeEdgeBlock (glowHeader,  glowEnableButton,  glowColourLabel,  glowColourButton,
                    glowBlendLabel,  glowBlendBox,  { 32, 33, 34, 52, 53 });

    // Outline. Photoshop's Stroke: it traces whatever silhouette the layer
    // already has, so it applies to every drawn primitive.
    strokeHeader.setVisible (isShape);
    strokeEnableButton.setVisible (isShape);

    const bool strokeOn = isShape && strokeEnableButton.getToggleState();

    strokeColourLabel.setVisible (strokeOn);
    strokeColourButton.setVisible (strokeOn);
    strokePosLabel.setVisible (strokeOn);
    strokePosBox.setVisible (strokeOn);

    if (isShape)
    {
        y += 6;
        place (strokeHeader, 22);
        strokeEnableButton.setBounds (area.getX() + kLabelW, y, 100, 22);
        y += 26;
    }

    if (strokeOn)
    {
        placeLabelled (strokeColourLabel, strokeColourButton, 26);
        placeLabelled (strokePosLabel, strokePosBox, 26);
    }

    show (18, strokeOn);

    // Texture. KnobMan only offers it on the solid primitives, but there is no
    // reason it cannot shade any drawn shape, so it follows isShape.
    textureHeader.setVisible (isShape);
    texModeLabel.setVisible (isShape);
    texModeBox.setVisible (isShape);

    const bool texOn = isShape && texModeBox.getSelectedId() > 1;

    texNameLabel.setVisible (texOn);
    texNameBox.setVisible (texOn);
    texLocateButton.setVisible (texOn);
    texFolderButton.setVisible (texOn);
    texRescanButton.setVisible (texOn);
    texPathLabel.setVisible (texOn);

    if (isShape)
    {
        y += 6;
        place (textureHeader, 22);
        placeLabelled (texModeLabel, texModeBox, 26);
    }

    if (texOn)
    {
        placeLabelled (texNameLabel, texNameBox, 26);

        const int bx = area.getX() + kLabelW;
        texLocateButton.setBounds (bx, y, 74, 22);
        texFolderButton.setBounds (bx + 78, y, 58, 22);
        texRescanButton.setBounds (bx + 140, y, 64, 22);
        y += 26;

        place (texPathLabel, 18);
        y += 4;
    }

    show (15, texOn);
    show (16, texOn);
    show (17, texOn);

    textLabel.setVisible (isText);
    textEditor.setVisible (isText);
    boldButton.setVisible (isText);
    italicButton.setVisible (isText);

    if (isText)
    {
        placeLabelled (textLabel, textEditor, 26);
        boldButton.setBounds (area.getX() + kLabelW, y, 80, 22);
        italicButton.setBounds (area.getX() + kLabelW + 84, y, 80, 22);
        y += 26;
    }

    const bool usesFill = type == PrimType::WaveCircle || type == PrimType::Triangle
                       || type == PrimType::Shape;
    fillButton.setVisible (usesFill);

    if (usesFill)
    {
        fillButton.setBounds (area.getX() + kLabelW, y, 100, 22);
        y += 26;
    }

    fileLabel.setVisible (isImage);
    fileButton.setVisible (isImage);

    if (isImage)
        placeLabelled (fileLabel, fileButton, 26);

    const int newRequired = y - area.getY() + 16;

    if (newRequired != requiredHeight)
    {
        requiredHeight = newRequired;

        // The panel lives in a Viewport that was sized from the OLD required
        // height. Tell the owner, or the rows just added sit below the scroll
        // range and cannot be reached.
        if (onLayoutChanged != nullptr && ! notifyingLayout)
        {
            const juce::ScopedValueSetter<bool> guard (notifyingLayout, true);
            onLayoutChanged();
        }
    }
}

//==============================================================================
EffectPanel::EffectPanel()
{
    for (auto* b : { &antialiasButton, &separateButton, &keepDirButton })
    {
        b->onClick = [this] { pull(); resized(); };
        addAndMakeVisible (b);
    }

    auto addAnim = [this] (const juce::String& name, float min, float max, float step)
    {
        auto* r = animRows.add (new AnimRow (name, min, max, step));
        r->onChange = [this] { pull(); };
        addAndMakeVisible (r);
        return r;
    };

    auto addValue = [this] (const juce::String& name, float min, float max, float step)
    {
        auto* r = valueRows.add (new ValueRow (name, min, max, step));
        r->onChange = [this] { pull(); };
        addAndMakeVisible (r);
        return r;
    };

    makeHeader (*this, headers, "Transform");
    addAnim ("Zoom X",   0.0f, 400.0f, 0.5f);    // 0
    addAnim ("Zoom Y",   0.0f, 400.0f, 0.5f);    // 1
    addAnim ("Offset X", -200.0f, 200.0f, 0.5f); // 2
    addAnim ("Offset Y", -200.0f, 200.0f, 0.5f); // 3
    addAnim ("Angle",   -1800.0f, 1800.0f, 1.0f);// 4
    addValue ("Centre X", 0.0f, 100.0f, 0.5f);   // 0
    addValue ("Centre Y", 0.0f, 100.0f, 0.5f);   // 1

    makeHeader (*this, headers, "Colour");
    addAnim ("Alpha",       0.0f, 100.0f, 0.5f); // 5
    addAnim ("Brightness", -100.0f, 100.0f, 1.0f);// 6
    addAnim ("Contrast",   -100.0f, 100.0f, 1.0f);// 7
    addAnim ("Saturation", -100.0f, 100.0f, 1.0f);// 8
    addAnim ("Hue",        -180.0f, 180.0f, 1.0f);// 9

    buildMask (mask1, "Mask 1");
    buildMask (mask2, "Mask 2");
}

void EffectPanel::buildMask (MaskControls& m, const juce::String& title)
{
    m.header.setText (title.toUpperCase(), juce::dontSendNotification);
    m.header.setColour (juce::Label::textColourId, juce::Colour (0xff7fa8d0));
    m.header.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    addAndMakeVisible (m.header);

    m.enabled.onClick = [this] { pull(); resized(); };
    addAndMakeVisible (m.enabled);

    m.type.addItemList (maskTypeNames(), 1);
    m.type.setSelectedId (1, juce::dontSendNotification);
    m.type.onChange = [this] { pull(); };
    addAndMakeVisible (m.type);

    m.orOp.onClick  = [this] { pull(); };
    m.biDir.onClick = [this] { pull(); };
    addAndMakeVisible (m.orOp);
    addAndMakeVisible (m.biDir);

    m.start = std::make_unique<AnimRow> ("Start", -1800.0f, 1800.0f, 1.0f);
    m.stop  = std::make_unique<AnimRow> ("Stop",  -1800.0f, 1800.0f, 1.0f);
    m.start->onChange = [this] { pull(); };
    m.stop->onChange  = [this] { pull(); };
    addAndMakeVisible (*m.start);
    addAndMakeVisible (*m.stop);

    configurePanelControls (*this);
}

void EffectPanel::setLayer (DocLayer* l)
{
    layer = l;
    refresh();
}

void EffectPanel::refresh()
{
    if (layer == nullptr)
        return;

    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& e = layer->eff;

    antialiasButton.setToggleState (e.antialias, juce::dontSendNotification);
    separateButton.setToggleState (e.zoomSeparate, juce::dontSendNotification);
    keepDirButton.setToggleState (e.keepDir, juce::dontSendNotification);

    animRows[0]->setValue (e.zoomX,      juce::dontSendNotification);
    animRows[1]->setValue (e.zoomY,      juce::dontSendNotification);
    animRows[2]->setValue (e.offsetX,    juce::dontSendNotification);
    animRows[3]->setValue (e.offsetY,    juce::dontSendNotification);
    animRows[4]->setValue (e.angle,      juce::dontSendNotification);
    animRows[5]->setValue (e.alpha,      juce::dontSendNotification);
    animRows[6]->setValue (e.brightness, juce::dontSendNotification);
    animRows[7]->setValue (e.contrast,   juce::dontSendNotification);
    animRows[8]->setValue (e.saturation, juce::dontSendNotification);
    animRows[9]->setValue (e.hue,        juce::dontSendNotification);

    valueRows[0]->setValue (e.centreX, juce::dontSendNotification);
    valueRows[1]->setValue (e.centreY, juce::dontSendNotification);

    auto loadMask = [] (MaskControls& mc, const MaskSpec& ms)
    {
        mc.enabled.setToggleState (ms.enabled, juce::dontSendNotification);
        mc.type.setSelectedId ((int) ms.type + 1, juce::dontSendNotification);
        mc.orOp.setToggleState (ms.orOp, juce::dontSendNotification);
        mc.biDir.setToggleState (ms.biDir, juce::dontSendNotification);
        mc.start->setValue (ms.start, juce::dontSendNotification);
        mc.stop->setValue (ms.stop, juce::dontSendNotification);
    };

    loadMask (mask1, e.mask1);
    loadMask (mask2, e.mask2);

    resized();
}

void EffectPanel::pull()
{
    if (updating || layer == nullptr)
        return;

    auto& e = layer->eff;

    e.antialias    = antialiasButton.getToggleState();
    e.zoomSeparate = separateButton.getToggleState();
    e.keepDir      = keepDirButton.getToggleState();

    e.zoomX      = animRows[0]->getValue();
    e.zoomY      = animRows[1]->getValue();
    e.offsetX    = animRows[2]->getValue();
    e.offsetY    = animRows[3]->getValue();
    e.angle      = animRows[4]->getValue();
    e.alpha      = animRows[5]->getValue();
    e.brightness = animRows[6]->getValue();
    e.contrast   = animRows[7]->getValue();
    e.saturation = animRows[8]->getValue();
    e.hue        = animRows[9]->getValue();

    e.centreX = valueRows[0]->getValue();
    e.centreY = valueRows[1]->getValue();

    auto storeMask = [] (const MaskControls& mc, MaskSpec& ms)
    {
        ms.enabled = mc.enabled.getToggleState();
        ms.type    = (MaskType) juce::jlimit (0, (int) MaskType::numTypes - 1,
                                              mc.type.getSelectedId() - 1);
        ms.orOp    = mc.orOp.getToggleState();
        ms.biDir   = mc.biDir.getToggleState();
        ms.start   = mc.start->getValue();
        ms.stop    = mc.stop->getValue();
    };

    storeMask (mask1, e.mask1);
    storeMask (mask2, e.mask2);

    if (onChange != nullptr)
        onChange();
}

void EffectPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262629));
}

void EffectPanel::resized()
{
    auto area = getLocalBounds().reduced (8, 6);
    int y = area.getY();

    auto place = [&] (juce::Component& c, int h)
    {
        c.setBounds (area.getX(), y, area.getWidth(), h);
        y += h;
    };

    if (headers.size() > 0) place (*headers[0], 24);

    place (antialiasButton, 22);
    place (separateButton, 22);
    place (keepDirButton, 22);

    place (*animRows[0], AnimRow::rowHeight);

    animRows[1]->setVisible (separateButton.getToggleState());

    if (separateButton.getToggleState())
        place (*animRows[1], AnimRow::rowHeight);

    place (*animRows[2], AnimRow::rowHeight);
    place (*animRows[3], AnimRow::rowHeight);
    place (*animRows[4], AnimRow::rowHeight);
    place (*valueRows[0], ValueRow::rowHeight);
    place (*valueRows[1], ValueRow::rowHeight);

    y += 6;

    if (headers.size() > 1) place (*headers[1], 24);

    for (int i = 5; i <= 9; ++i)
        place (*animRows[i], AnimRow::rowHeight);

    auto placeMask = [&] (MaskControls& m)
    {
        y += 6;
        place (m.header, 24);
        place (m.enabled, 22);

        const bool on = m.enabled.getToggleState();

        m.type.setVisible (on);
        m.orOp.setVisible (on);
        m.biDir.setVisible (on);
        m.start->setVisible (on);
        m.stop->setVisible (on);

        if (! on)
            return;

        m.type.setBounds (area.getX() + kLabelW, y, area.getWidth() - kLabelW, 24);
        y += 26;
        m.orOp.setBounds (area.getX() + kLabelW, y, 140, 22);
        m.biDir.setBounds (area.getX() + kLabelW + 144, y, 80, 22);
        y += 24;
        place (*m.start, AnimRow::rowHeight);
        place (*m.stop, AnimRow::rowHeight);
    };

    placeMask (mask1);
    placeMask (mask2);

    const int newRequired = y - area.getY() + 16;

    if (newRequired != requiredHeight)
    {
        requiredHeight = newRequired;

        // Enabling a mask adds a type box, two toggles and two AnimRows. The
        // Viewport was sized from the height this panel needed BEFORE that, so
        // without telling the owner to re-size us the new rows are off the
        // bottom of the scroll range and the panel appears stuck.
        if (onLayoutChanged != nullptr && ! notifyingLayout)
        {
            const juce::ScopedValueSetter<bool> guard (notifyingLayout, true);
            onLayoutChanged();
        }
    }
}
