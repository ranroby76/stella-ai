// C:\workspace\Stella AI Studio\src\LayerPanels.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    LayerPanels.h

    The editing surface for a LayerDoc: the layer stack, the selected layer's
    primitive, and the selected layer's effects.

    AnimRow is the control that makes the whole thing work — every animatable
    parameter is edited as From / To / Curve in one row, so animating a
    parameter is never a different action from setting it.
*/

#pragma once

#include "LayerDoc.h"

#include <functional>

//==============================================================================
class AnimRow final : public juce::Component
{
public:
    AnimRow (const juce::String& name, float min, float max, float interval);

    void setValue (const AnimVal&, juce::NotificationType);
    AnimVal getValue() const noexcept;

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int rowHeight = 24;

    std::function<void()> onChange;

private:
    void modeChanged();

    juce::Label    label;
    juce::Slider   fromSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider   toSlider   { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox modeBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnimRow)
};

//==============================================================================
class ValueRow final : public juce::Component
{
public:
    ValueRow (const juce::String& name, float min, float max, float interval);

    void  setValue (float, juce::NotificationType);
    float getValue() const noexcept { return (float) slider.getValue(); }

    /** Some rows are shared between primitives that use them for different
        things. Length is the meter's INNER reach, so it should say so. */
    void setLabelText (const juce::String& text) { label.setText (text, juce::dontSendNotification); }

    void resized() override;

    static constexpr int rowHeight = 24;

    std::function<void()> onChange;

private:
    juce::Label  label;
    juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueRow)
};

//==============================================================================
class LayerRow;

/** The layer stack. Order in this list IS the render order — topmost row is
    drawn last — so reordering is done by dragging a row, not by nudging it with
    buttons. Each row carries its own delete control.

    Built from plain Components in a Viewport rather than a ListBox: a ListBox
    row is a paint callback, not a component, so it cannot hold a button and
    cannot be picked up and moved. */
class LayerListPanel final : public juce::Component
{
public:
    LayerListPanel();
    ~LayerListPanel() override;

    void setDocument (LayerDoc* doc);
    void refresh();

    int  getSelectedIndex() const noexcept { return selectedIndex; }
    void setSelectedIndex (int);

    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onSelectionChange;
    std::function<void()> onDocumentChange;

    static constexpr int rowHeight = 26;

private:
    friend class LayerRow;

    //==========================================================================
    // Called by LayerRow. A row never knows its own index — it asks, so nothing
    // has to be renumbered when the stack is reordered.
    void paintRow     (LayerRow&, juce::Graphics&);
    void rowClicked   (LayerRow&, const juce::MouseEvent&);
    void requestDelete (LayerRow&);
    void beginDrag    (LayerRow&, int grabY);
    void dragTo       (LayerRow&, const juce::MouseEvent&);
    void endDrag      (LayerRow&);

    //==========================================================================
    void addLayer();
    void duplicateLayer();
    void deleteLayerAt (int documentIndex);
    void showContextMenu (LayerRow&);

    void rebuildRows();
    void layoutRows (int skipRow = -1);

    int  numLayers() const noexcept;
    int  docIndexForRow (int row) const noexcept;

    LayerDoc* document = nullptr;
    int selectedIndex = 0;

    juce::Viewport  listViewport;
    juce::Component rowHolder;
    juce::OwnedArray<LayerRow> rows;

    bool dragging  = false;
    int  dragGrabY = 0;

    juce::TextButton addButton { "+" };
    juce::TextButton dupButton { "Dup" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerListPanel)
};

//==============================================================================
class PrimitivePanel final : public juce::Component,
                            private juce::ChangeListener
{
public:
    PrimitivePanel();

    void setLayer (DocLayer*);
    void refresh();

    void resized() override;
    void paint (juce::Graphics&) override;

    int getRequiredHeight() const noexcept { return requiredHeight; }

    /** For the Bevel & Emboss window, which outlives no layer of its own and
        asks for the current one every time it touches it. Null when nothing is
        selected. */
    BevelSpec* getBevelTarget() const noexcept
    {
        return layer != nullptr ? &layer->prim.bevel : nullptr;
    }

    void bevelChanged();

    /** Renders the selected layer on its own, for the Bevel window's live
        thumbnail. Supplied by LayerWorkspace, which is the only thing that
        knows both the document and which row is selected. Returns an invalid
        image when there is nothing selected. */
    std::function<juce::Image()> makeLayerThumbnail;

    std::function<void()> onChange;

    /** Fired when the panel's required height changes because rows appeared or
        disappeared. The owner must re-run its sizing, or the Viewport keeps the
        old scroll range and the new rows are unreachable. */
    std::function<void()> onLayoutChanged;

private:
    void pull();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    DocLayer* layer = nullptr;
    bool updating = false;
    bool notifyingLayout = false;
    int  requiredHeight = 0;

    juce::Label    typeLabel { {}, "Type" };
    juce::ComboBox typeBox;

    juce::Label      colourLabel { {}, "Colour" };
    juce::TextButton colourButton { "" };

    juce::OwnedArray<ValueRow> rows;

    juce::Label      textLabel { {}, "Text" };
    juce::TextEditor textEditor;
    juce::ToggleButton boldButton   { "Bold" };
    juce::ToggleButton italicButton { "Italic" };
    juce::ToggleButton fillButton   { "Fill" };

    juce::Label      fileLabel { {}, "Image" };
    juce::TextButton fileButton { "Choose..." };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton   bevelButton { "Bevel & Emboss..." };

    juce::Label        shadowHeader { {}, "DROP SHADOW" };
    juce::ToggleButton shadowEnableButton { "Enabled" };
    juce::Label        shadowColourLabel { {}, "Colour" };
    juce::TextButton   shadowColourButton;
    juce::Label        shadowBlendLabel { {}, "Blend" };
    juce::ComboBox     shadowBlendBox;
    juce::ToggleButton shadowKnockoutButton { "Keep clear of layer" };

    juce::Label        innerHeader { {}, "INNER SHADOW" };
    juce::ToggleButton innerEnableButton { "Enabled" };
    juce::Label        innerColourLabel { {}, "Colour" };
    juce::TextButton   innerColourButton;
    juce::Label        innerBlendLabel { {}, "Blend" };
    juce::ComboBox     innerBlendBox;

    juce::Label        glowHeader { {}, "OUTER GLOW" };
    juce::ToggleButton glowEnableButton { "Enabled" };
    juce::Label        glowColourLabel { {}, "Colour" };
    juce::TextButton   glowColourButton;
    juce::Label        glowBlendLabel { {}, "Blend" };
    juce::ComboBox     glowBlendBox;

    juce::Label        strokeHeader { {}, "OUTLINE" };
    juce::ToggleButton strokeEnableButton { "Enabled" };
    juce::Label        strokeColourLabel { {}, "Colour" };
    juce::TextButton   strokeColourButton;
    juce::Label        strokePosLabel { {}, "Position" };
    juce::ComboBox     strokePosBox;

    /** Which swatch the open ColourSelector belongs to — they all share one
        changeListenerCallback. */
    enum class ColourTarget { Fill, Stroke, Shadow, InnerShadow, Glow, LedBezel };
    ColourTarget colourTarget = ColourTarget::Fill;

    juce::Label      shapePickLabel { {}, "Shape" };
    juce::ComboBox   shapePickBox;
    juce::TextButton shapeReloadButton { "Reload" };

    void refillShapeNames();

    juce::Label        meterLabelsLabel { {}, "Values" };
    juce::TextEditor   meterLabelsBox;
    juce::ToggleButton meterLabelsInsideButton { "Values on the inside" };

    juce::Label        ledHeader { {}, "LED" };
    juce::Label        ledShapeLabel { {}, "Shape" };
    juce::ComboBox     ledShapeBox;
    juce::ToggleButton ledOnButton { "On" };
    juce::ToggleButton ledFollowButton { "Brightness follows frame" };
    juce::Label        ledBezelColourLabel { {}, "Bezel col" };
    juce::TextButton   ledBezelColourButton;

    juce::Label      textureHeader { {}, "TEXTURE" };
    juce::Label      texModeLabel { {}, "Mode" };
    juce::ComboBox   texModeBox;
    juce::Label      texNameLabel { {}, "Pattern" };
    juce::ComboBox   texNameBox;
    juce::TextButton texLocateButton { "Locate..." };
    juce::TextButton texFolderButton { "Open" };
    juce::TextButton texRescanButton { "Rescan" };
    juce::Label      texPathLabel;

    void refillTextureNames();
    void updateTexturePathLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrimitivePanel)
};

//==============================================================================
class EffectPanel final : public juce::Component
{
public:
    EffectPanel();

    void setLayer (DocLayer*);
    void refresh();

    void resized() override;
    void paint (juce::Graphics&) override;

    int getRequiredHeight() const noexcept { return requiredHeight; }

    std::function<void()> onChange;

    /** Fired when the panel's required height changes — enabling a mask adds
        four rows, and without this the Viewport never learns it got taller. */
    std::function<void()> onLayoutChanged;

private:
    struct MaskControls
    {
        juce::Label        header;
        juce::ToggleButton enabled { "Enabled" };
        juce::ComboBox     type;
        juce::ToggleButton orOp  { "Or (else And)" };
        juce::ToggleButton biDir { "BiDir" };
        std::unique_ptr<AnimRow> start;
        std::unique_ptr<AnimRow> stop;
    };

    void buildMask (MaskControls&, const juce::String& title);
    void pull();

    DocLayer* layer = nullptr;
    bool updating = false;
    bool notifyingLayout = false;
    int  requiredHeight = 0;

    juce::OwnedArray<juce::Label> headers;
    juce::OwnedArray<AnimRow>     animRows;
    juce::OwnedArray<ValueRow>    valueRows;

    juce::ToggleButton antialiasButton { "Antialias" };
    juce::ToggleButton separateButton  { "Separate X/Y zoom" };
    juce::ToggleButton keepDirButton   { "Keep direction" };

    MaskControls mask1, mask2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectPanel)
};
