// C:\workspace\Stella AI Studio\src\BevelDialog.h
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    BevelDialog.h

    Photoshop's Bevel & Emboss, in a window of its own because five sections do
    not fit in the Shape tab's viewport.

    Non-modal on purpose. Photoshop needs a Preview checkbox because its dialog
    blocks; ours does not, so the canvas re-renders live behind it and there is
    nothing to tick.

    The dialog never owns a layer. It holds a SafePointer back to the panel and
    asks for the current BevelSpec every time it touches one, so deleting the
    layer, switching selection or closing the Layers tab underneath it is safe.
*/

#pragma once

#include "LayerDoc.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

class PrimitivePanel;

//==============================================================================
/** The Gloss Contour editor: drag points, click empty space to add one,
    right-click a point to remove it. A point is either a corner — segments
    either side run straight — or smooth, which eases between its neighbours. */
class ContourEditor final : public juce::Component
{
public:
    ContourEditor();

    void setContour (const Contour&);
    Contour getContour() const { return contour; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void()> onChange;

private:
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> toCurve (juce::Point<float> screen) const;
    int hitTest (juce::Point<float> position) const;

    Contour contour;
    int dragging = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourEditor)
};

//==============================================================================
/** Draws one contour curve small, for a picker cell or a preset button. */
void paintContourThumbnail (juce::Graphics&, juce::Rectangle<int>, const Contour&, bool highlighted);

//==============================================================================
/** A button showing the current curve, which opens a grid of presets — the way
    Photoshop does it. A name in a combo box tells you nothing about the shape
    of a curve; a thumbnail tells you everything. */
class ContourPresetButton final : public juce::Button
{
public:
    ContourPresetButton();

    void setContour (const Contour&);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void clicked() override;

    std::function<void (const Contour&)> onPick;

private:
    Contour contour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContourPresetButton)
};

//==============================================================================
/** Photoshop's little angle dial. A slider is fine arithmetic and a poor way to
    think about a direction. */
class AngleDial final : public juce::Component
{
public:
    // Explicit, because JUCE_DECLARE_NON_COPYABLE below declares a deleted copy
    // constructor, and any user-declared constructor suppresses the implicit
    // default one — which this needs, since it is held as a plain member.
    AngleDial() = default;

    void setAngle (float degrees);
    float getAngle() const noexcept { return angle; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    std::function<void()> onChange;

private:
    void setFromMouse (const juce::MouseEvent&);

    float angle = 120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AngleDial)
};

//==============================================================================
/** The layer being edited, rendered on its own with the current settings, so
    the curve above it is not an abstract graph. Drawn on a checker so the
    transparent parts of an Outer Bevel are readable. */
class BevelPreview final : public juce::Component
{
public:
    BevelPreview() = default;

    void setImage (juce::Image newImage);
    void paint (juce::Graphics&) override;

private:
    juce::Image image;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BevelPreview)
};

//==============================================================================
class BevelDialog final : public juce::Component,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    explicit BevelDialog (PrimitivePanel& owner);
    ~BevelDialog() override;

    /** Opens the window, or brings the one already open to the front. */
    static void launch (PrimitivePanel& owner);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    BevelSpec* target() const;

    void pull();      ///< controls -> document
    void refresh();   ///< document -> controls
    void updateEnablement();
    void refreshPreview();
    void notifyChanged();
    void openColour (juce::TextButton&, bool highlight);

    juce::Component::SafePointer<PrimitivePanel> panel;
    bool updating = false;
    bool editingHighlightColour = true;

    /** Which layer's bevel the controls were last loaded from. Re-reading the
        document on a timer would fight a slider mid-drag, so it only happens
        when this changes. */
    BevelSpec* lastTarget = nullptr;

    juce::ToggleButton enableButton { "Enabled" };

    juce::Label    structureHeader { {}, "STRUCTURE" };
    juce::Label    styleLabel { {}, "Style" },      techniqueLabel { {}, "Technique" };
    juce::ComboBox styleBox, techniqueBox;
    juce::Label    depthLabel { {}, "Depth" },      sizeLabel { {}, "Size" },
                   softenLabel { {}, "Soften" };
    juce::Slider   depthSlider, sizeSlider, softenSlider;
    juce::Label    directionLabel { {}, "Direction" };
    juce::ToggleButton upButton { "Up" }, downButton { "Down" };

    juce::Label    shadingHeader { {}, "SHADING" };
    juce::ToggleButton globalLightButton { "Use Global Light" };
    juce::Label    angleLabel { {}, "Angle" }, altitudeLabel { {}, "Altitude" };
    juce::Slider   angleSlider, altitudeSlider;

    // Contour — reshapes the bevel's cross-section.
    juce::Label        contourHeader { {}, "CONTOUR" };
    juce::ToggleButton contourEnableButton { "Contour" };
    ContourPresetButton bevelContourPresetBox;
    ContourEditor      bevelContourEditor;
    juce::Label        rangeLabel { {}, "Range" };
    juce::Slider       rangeSlider;

    BevelPreview       preview;

    // Gloss Contour — reshapes the lighting response.
    juce::Label    contourLabel { {}, "Gloss Contour" };
    ContourPresetButton contourPresetBox;
    juce::ToggleButton antiAliasButton { "Anti-aliased" };
    AngleDial          angleDial;
    ContourEditor  contourEditor;

    juce::Label      highlightLabel { {}, "Highlight" };
    juce::ComboBox   highlightModeBox;
    juce::TextButton highlightColourButton;
    juce::Label      highlightOpacityLabel { {}, "Opacity" };
    juce::Slider     highlightOpacitySlider;

    juce::Label      shadowLabel { {}, "Shadow" };
    juce::ComboBox   shadowModeBox;
    juce::TextButton shadowColourButton;
    juce::Label      shadowOpacityLabel { {}, "Opacity" };
    juce::Slider     shadowOpacitySlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BevelDialog)
};
