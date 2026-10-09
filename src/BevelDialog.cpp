// C:\workspace\Stella AI Studio\src\BevelDialog.cpp
// From KnobMaker (C:\workspace\knobmaker), unchanged: the same layer builder in both apps.

/*
    BevelDialog.cpp
*/

#include "BevelDialog.h"
#include "LayerPanels.h"
#include "UiHelpers.h"

namespace
{
    constexpr int kLabelW = 96;
    constexpr int kRowH   = 24;
    constexpr int kMargin = 12;

    void styleLabelFor (juce::Label& l)
    {
        l.setColour (juce::Label::textColourId, juce::Colour (0xffc0c0c4));
        l.setFont (juce::Font (juce::FontOptions (12.0f)));
        l.setJustificationType (juce::Justification::centredLeft);
    }

    void styleHeader (juce::Label& l)
    {
        l.setColour (juce::Label::textColourId, juce::Colour (0xff7fa8d0));
        l.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    }

    void styleSlider (juce::Slider& s, double min, double max, double interval)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
        s.setRange (min, max, interval);
    }

    juce::DialogWindow* openWindow = nullptr;
}

//==============================================================================
void paintContourThumbnail (juce::Graphics& g, juce::Rectangle<int> bounds,
                            const Contour& contour, bool highlighted)
{
    auto r = bounds.toFloat().reduced (3.0f);

    g.setColour (juce::Colour (0xff1b1b1e));
    g.fillRect (bounds);

    // Filled under the curve, like Photoshop's: the area reads the shape faster
    // than a hairline does at this size.
    juce::Path fill;
    fill.startNewSubPath (r.getX(), r.getBottom());

    for (int px = 0; px <= (int) r.getWidth(); ++px)
    {
        const float x = (float) px / juce::jmax (1.0f, r.getWidth());
        fill.lineTo (r.getX() + x * r.getWidth(),
                     r.getBottom() - contour.at (x) * r.getHeight());
    }

    fill.lineTo (r.getRight(), r.getBottom());
    fill.closeSubPath();

    g.setColour (juce::Colour (0xffd8d8dc));
    g.fillPath (fill);

    g.setColour (highlighted ? juce::Colour (0xff5a9fe0) : juce::Colour (0xff3a3a42));
    g.drawRect (bounds, highlighted ? 2 : 1);
}

//==============================================================================
namespace
{
    /** The drop-down grid of preset curves. */
    class ContourGrid final : public juce::Component
    {
    public:
        explicit ContourGrid (std::function<void (int)> callback) : pick (std::move (callback))
        {
            const int count = Contour::presetNames().size();
            rows = (count + kColumns - 1) / kColumns;

            setSize (kColumns * kCell + 8, rows * kCell + 8);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff2b2b30));

            const int count = Contour::presetNames().size();

            for (int i = 0; i < count; ++i)
                paintContourThumbnail (g, cellBounds (i), Contour::preset (i), i == hover);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int i = indexAt (e.getPosition());

            if (i != hover) { hover = i; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

        void mouseUp (const juce::MouseEvent& e) override
        {
            const int i = indexAt (e.getPosition());

            if (i >= 0 && pick != nullptr)
                pick (i);

            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
        }

    private:
        static constexpr int kColumns = 4;
        static constexpr int kCell    = 62;

        juce::Rectangle<int> cellBounds (int i) const
        {
            return { 4 + (i % kColumns) * kCell, 4 + (i / kColumns) * kCell, kCell - 4, kCell - 4 };
        }

        int indexAt (juce::Point<int> position) const
        {
            const int count = Contour::presetNames().size();

            for (int i = 0; i < count; ++i)
                if (cellBounds (i).contains (position))
                    return i;

            return -1;
        }

        std::function<void (int)> pick;
        int rows = 1;
        int hover = -1;
    };
}

ContourPresetButton::ContourPresetButton() : juce::Button ("contour")
{
}

void ContourPresetButton::setContour (const Contour& c)
{
    contour = c;
    repaint();
}

void ContourPresetButton::paintButton (juce::Graphics& g, bool over, bool)
{
    paintContourThumbnail (g, getLocalBounds(), contour, over);

    // The little chevron, so it reads as something that opens.
    auto arrow = getLocalBounds().removeFromRight (12).withSizeKeepingCentre (7, 4).toFloat();

    juce::Path p;
    p.addTriangle (arrow.getX(), arrow.getY(),
                   arrow.getRight(), arrow.getY(),
                   arrow.getCentreX(), arrow.getBottom());

    g.setColour (juce::Colour (0xff8a8a92));
    g.fillPath (p);
}

void ContourPresetButton::clicked()
{
    auto grid = std::make_unique<ContourGrid> ([this] (int index)
    {
        setContour (Contour::preset (index));

        if (onPick != nullptr)
            onPick (contour);
    });

    juce::CallOutBox::launchAsynchronously (std::move (grid), getScreenBounds(), nullptr);
}

//==============================================================================
void AngleDial::setAngle (float degrees)
{
    angle = degrees;
    repaint();
}

void AngleDial::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto centre = r.getCentre();
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;

    g.setColour (juce::Colour (0xff1b1b1e));
    g.fillEllipse (r);
    g.setColour (juce::Colour (0xff4a4a52));
    g.drawEllipse (r, 1.0f);

    // Screen space: y runs down, so the y term is negated to match the way the
    // renderer reads this same angle.
    const float a = angle * juce::MathConstants<float>::pi / 180.0f;
    const juce::Point<float> tip (centre.x + std::cos (a) * radius * 0.82f,
                                  centre.y - std::sin (a) * radius * 0.82f);

    g.setColour (juce::Colour (0xffe8c020));
    g.drawLine (centre.x, centre.y, tip.x, tip.y, 1.6f);
    g.fillEllipse (tip.x - 2.5f, tip.y - 2.5f, 5.0f, 5.0f);
}

void AngleDial::mouseDown (const juce::MouseEvent& e) { setFromMouse (e); }
void AngleDial::mouseDrag (const juce::MouseEvent& e) { setFromMouse (e); }

void AngleDial::setFromMouse (const juce::MouseEvent& e)
{
    const auto centre = getLocalBounds().toFloat().getCentre();
    const float dx = e.position.x - centre.x;
    const float dy = centre.y - e.position.y;

    if (dx * dx + dy * dy < 4.0f)
        return;

    angle = std::atan2 (dy, dx) * 180.0f / juce::MathConstants<float>::pi;
    repaint();

    if (onChange != nullptr)
        onChange();
}

//==============================================================================
void BevelPreview::setImage (juce::Image newImage)
{
    image = std::move (newImage);
    repaint();
}

void BevelPreview::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();

    // Checker, so an Outer Bevel painting outside the shape is readable rather
    // than sitting invisible on a flat background.
    constexpr int cell = 8;
    g.fillAll (juce::Colour (0xff2a2a2e));
    g.setColour (juce::Colour (0xff232327));

    for (int y = 0; y < r.getHeight(); y += cell)
        for (int x = 0; x < r.getWidth(); x += cell)
            if (((x / cell) + (y / cell)) % 2 == 0)
                g.fillRect (x, y, cell, cell);

    if (image.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (image, r.reduced (4).toFloat(),
                     juce::RectanglePlacement::centred);
    }

    g.setColour (juce::Colour (0xff3a3a42));
    g.drawRect (r, 1);
}

//==============================================================================
ContourEditor::ContourEditor()
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void ContourEditor::setContour (const Contour& c)
{
    contour = c;
    repaint();
}

juce::Point<float> ContourEditor::toScreen (float x, float y) const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);

    return { r.getX() + x * r.getWidth(),
             r.getBottom() - y * r.getHeight() };
}

juce::Point<float> ContourEditor::toCurve (juce::Point<float> screen) const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);

    return { juce::jlimit (0.0f, 1.0f, (screen.x - r.getX()) / juce::jmax (1.0f, r.getWidth())),
             juce::jlimit (0.0f, 1.0f, (r.getBottom() - screen.y) / juce::jmax (1.0f, r.getHeight())) };
}

int ContourEditor::hitTest (juce::Point<float> position) const
{
    for (int i = 0; i < (int) contour.points.size(); ++i)
        if (toScreen (contour.points[(size_t) i].x, contour.points[(size_t) i].y)
                .getDistanceFrom (position) < 7.0f)
            return i;

    return -1;
}

void ContourEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);

    g.setColour (juce::Colour (0xff17171a));
    g.fillRect (getLocalBounds());

    g.setColour (juce::Colour (0xff2e2e34));

    for (int i = 1; i < 4; ++i)
    {
        const float f = (float) i / 4.0f;
        g.drawHorizontalLine ((int) (r.getY() + f * r.getHeight()), r.getX(), r.getRight());
        g.drawVerticalLine   ((int) (r.getX() + f * r.getWidth()),  r.getY(), r.getBottom());
    }

    g.setColour (juce::Colour (0xff3a3a42));
    g.drawRect (r, 1.0f);

    // The curve, sampled per pixel so a corner point reads as a genuine corner.
    juce::Path path;

    for (int px = 0; px <= (int) r.getWidth(); ++px)
    {
        const float x = (float) px / juce::jmax (1.0f, r.getWidth());
        const auto point = toScreen (x, contour.at (x));

        if (px == 0) path.startNewSubPath (point);
        else         path.lineTo (point);
    }

    g.setColour (juce::Colour (0xffe8c020));
    g.strokePath (path, juce::PathStrokeType (1.6f));

    for (const auto& p : contour.points)
    {
        const auto c = toScreen (p.x, p.y);

        g.setColour (p.corner ? juce::Colour (0xffe8c020) : juce::Colour (0xff8fd0ff));

        // Corner points are drawn square, smooth ones round — so you can see
        // which is which without selecting anything.
        if (p.corner) g.fillRect (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
        else          g.fillEllipse (c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f);
    }
}

void ContourEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto position = e.position;
    const int index = hitTest (position);

    if (e.mods.isPopupMenu())
    {
        if (index > 0 && index < (int) contour.points.size() - 1)
        {
            contour.removePoint (index);
            repaint();

            if (onChange != nullptr)
                onChange();
        }

        return;
    }

    if (index >= 0)
    {
        dragging = index;
        return;
    }

    const auto c = toCurve (position);
    contour.addPoint (c.x, c.y);
    dragging = hitTest (position);
    repaint();

    if (onChange != nullptr)
        onChange();
}

void ContourEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || dragging >= (int) contour.points.size())
        return;

    auto& p = contour.points[(size_t) dragging];
    const auto c = toCurve (e.position);

    p.y = c.y;

    // The two anchors stay pinned to 0 and 1 horizontally, or the curve would
    // have a range it cannot be evaluated over.
    if (dragging > 0 && dragging < (int) contour.points.size() - 1)
    {
        const float lo = contour.points[(size_t) dragging - 1].x + 0.01f;
        const float hi = contour.points[(size_t) dragging + 1].x - 0.01f;
        p.x = juce::jlimit (lo, juce::jmax (lo, hi), c.x);
    }

    repaint();

    if (onChange != nullptr)
        onChange();
}

void ContourEditor::mouseUp (const juce::MouseEvent&)
{
    dragging = -1;
}

void ContourEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int index = hitTest (e.position);

    if (index < 0)
        return;

    contour.points[(size_t) index].corner = ! contour.points[(size_t) index].corner;
    repaint();

    if (onChange != nullptr)
        onChange();
}

//==============================================================================
BevelDialog::BevelDialog (PrimitivePanel& owner) : panel (&owner)
{
    addAndMakeVisible (enableButton);
    enableButton.onClick = [this] { pull(); updateEnablement(); };

    for (auto* l : { &structureHeader, &shadingHeader })
    {
        styleHeader (*l);
        addAndMakeVisible (l);
    }

    for (auto* l : { &styleLabel, &techniqueLabel, &depthLabel, &sizeLabel, &softenLabel,
                     &directionLabel, &angleLabel, &altitudeLabel, &contourLabel,
                     &highlightLabel, &shadowLabel, &highlightOpacityLabel, &shadowOpacityLabel })
    {
        styleLabelFor (*l);
        addAndMakeVisible (l);
    }

    styleBox.addItemList (bevelStyleNames(), 1);
    techniqueBox.addItemList (bevelTechniqueNames(), 1);
    highlightModeBox.addItemList (blendModeNames(), 1);
    shadowModeBox.addItemList (blendModeNames(), 1);

    for (auto* c : { &styleBox, &techniqueBox, &highlightModeBox, &shadowModeBox })
    {
        c->onChange = [this] { pull(); };
        addAndMakeVisible (c);
    }

    styleHeader (contourHeader);
    addAndMakeVisible (contourHeader);

    styleLabelFor (rangeLabel);
    addAndMakeVisible (rangeLabel);

    contourEnableButton.onClick = [this]
    {
        // A contour that is still the default straight line maps every height to
        // itself, so ticking this would change nothing — the same trap the
        // Enabled toggle had. Seed a curve that visibly does something, but only
        // while the curve has never been touched.
        if (contourEnableButton.getToggleState()
              && bevelContourEditor.getContour().points.size() <= 2)
        {
            bevelContourEditor.setContour (Contour::preset (3));   // Half Round
            bevelContourPresetBox.setContour (Contour::preset (3));
        }

        pull();
    };
    addAndMakeVisible (contourEnableButton);

    bevelContourPresetBox.onPick = [this] (const Contour& c)
    {
        bevelContourEditor.setContour (c);
        pull();
    };
    addAndMakeVisible (bevelContourPresetBox);

    bevelContourEditor.onChange = [this] { pull(); };
    contourEditor.onChange = [this] { pull(); };
    addAndMakeVisible (bevelContourEditor);

    addAndMakeVisible (preview);

    contourPresetBox.onPick = [this] (const Contour& c)
    {
        contourEditor.setContour (c);
        pull();
    };
    addAndMakeVisible (contourPresetBox);

    antiAliasButton.onClick = [this] { pull(); };
    addAndMakeVisible (antiAliasButton);

    angleDial.onChange = [this]
    {
        angleSlider.setValue (angleDial.getAngle(), juce::dontSendNotification);
        pull();
    };
    addAndMakeVisible (angleDial);

    styleSlider (depthSlider,   1.0,  1000.0, 1.0);
    styleSlider (sizeSlider,    0.5,   100.0, 0.5);
    styleSlider (softenSlider,  0.0,    24.0, 0.5);
    styleSlider (angleSlider, -180.0,  180.0, 1.0);
    styleSlider (altitudeSlider, 0.0,   90.0, 1.0);
    styleSlider (rangeSlider, 1.0, 100.0, 1.0);
    styleSlider (highlightOpacitySlider, 0.0, 100.0, 1.0);
    styleSlider (shadowOpacitySlider,    0.0, 100.0, 1.0);

    for (auto* s : { &depthSlider, &sizeSlider, &softenSlider, &angleSlider, &altitudeSlider,
                     &rangeSlider, &highlightOpacitySlider, &shadowOpacitySlider })
    {
        s->onValueChange = [this] { pull(); };
        addAndMakeVisible (s);
    }

    // Radio pair: Direction is Up or Down, never neither.
    upButton.setRadioGroupId (7001);
    downButton.setRadioGroupId (7001);
    upButton.onClick   = [this] { pull(); };
    downButton.onClick = [this] { pull(); };
    addAndMakeVisible (upButton);
    addAndMakeVisible (downButton);

    globalLightButton.onClick = [this] { pull(); resized(); };
    addAndMakeVisible (globalLightButton);

    contourEditor.onChange = [this] { pull(); };
    addAndMakeVisible (contourEditor);

    highlightColourButton.onClick = [this] { openColour (highlightColourButton, true); };
    shadowColourButton.onClick    = [this] { openColour (shadowColourButton, false); };
    addAndMakeVisible (highlightColourButton);
    addAndMakeVisible (shadowColourButton);

    configurePanelControls (*this);

    // Two columns rather than one tall stack: with a second contour and a live
    // preview the single-column form ran to about 780px, which does not fit on
    // a laptop once the title bar is counted.
    setSize (764, 512);
    lastTarget = target();
    refresh();

    // The layer under us can change while this window is open — a different
    // selection, or the layer deleted outright. Poll rather than assume.
    startTimer (300);
}

BevelDialog::~BevelDialog()
{
    stopTimer();
    openWindow = nullptr;
}

//==============================================================================
void BevelDialog::launch (PrimitivePanel& owner)
{
    if (openWindow != nullptr)
    {
        openWindow->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new BevelDialog (owner));
    options.dialogTitle = "Bevel & Emboss";
    options.dialogBackgroundColour = juce::Colour (0xff262629);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    // Non-modal: the canvas keeps re-rendering behind it, so there is no need
    // for Photoshop's Preview checkbox.
    openWindow = options.launchAsync();
}

//==============================================================================
BevelSpec* BevelDialog::target() const
{
    if (auto* p = panel.getComponent())
        return p->getBevelTarget();

    return nullptr;
}

void BevelDialog::notifyChanged()
{
    if (auto* p = panel.getComponent())
        p->bevelChanged();
}

void BevelDialog::timerCallback()
{
    if (panel.getComponent() == nullptr)
    {
        setEnabled (false);
        return;
    }

    // Its own window, so a sensitivity change in the toolbar does not reach it
    // any other way. Cheap enough at this rate to just re-apply.
    configurePanelControls (*this);

    // Only re-read the document when the layer under us actually changed.
    // Doing it every tick fought any slider being dragged: setValue does not
    // update the Slider's internal drag anchor, so the next mouse event
    // snapped straight back to the value the timer had just written.
    auto* b = target();

    if (b != lastTarget)
    {
        lastTarget = b;
        refresh();
    }
}

//==============================================================================
void BevelDialog::refresh()
{
    auto* b = target();

    setEnabled (b != nullptr);

    if (b == nullptr)
        return;

    const juce::ScopedValueSetter<bool> guard (updating, true);

    enableButton.setToggleState (b->enabled, juce::dontSendNotification);
    styleBox.setSelectedId ((int) b->style + 1, juce::dontSendNotification);
    techniqueBox.setSelectedId ((int) b->technique + 1, juce::dontSendNotification);

    depthSlider.setValue (b->depth,   juce::dontSendNotification);
    sizeSlider.setValue (b->size,     juce::dontSendNotification);
    softenSlider.setValue (b->soften, juce::dontSendNotification);

    upButton.setToggleState (b->up, juce::dontSendNotification);
    downButton.setToggleState (! b->up, juce::dontSendNotification);

    globalLightButton.setToggleState (b->useGlobalLight, juce::dontSendNotification);
    angleSlider.setValue (b->angle,       juce::dontSendNotification);
    altitudeSlider.setValue (b->altitude, juce::dontSendNotification);

    contourEnableButton.setToggleState (b->contourEnabled, juce::dontSendNotification);
    bevelContourEditor.setContour (b->bevelContour);
    bevelContourPresetBox.setContour (b->bevelContour);
    rangeSlider.setValue (b->contourRange, juce::dontSendNotification);

    contourEditor.setContour (b->glossContour);
    contourPresetBox.setContour (b->glossContour);
    antiAliasButton.setToggleState (b->antiAliased, juce::dontSendNotification);
    angleDial.setAngle (b->angle);

    highlightModeBox.setSelectedId ((int) b->highlightMode + 1, juce::dontSendNotification);
    highlightColourButton.setColour (juce::TextButton::buttonColourId, b->highlightColour);
    highlightColourButton.setButtonText (b->highlightColour.toDisplayString (true));
    highlightOpacitySlider.setValue (b->highlightOpacity, juce::dontSendNotification);

    shadowModeBox.setSelectedId ((int) b->shadowMode + 1, juce::dontSendNotification);
    shadowColourButton.setColour (juce::TextButton::buttonColourId, b->shadowColour);
    shadowColourButton.setButtonText (b->shadowColour.toDisplayString (true));
    shadowOpacitySlider.setValue (b->shadowOpacity, juce::dontSendNotification);

    updateEnablement();
    refreshPreview();
}

void BevelDialog::refreshPreview()
{
    if (auto* p = panel.getComponent())
        if (p->makeLayerThumbnail != nullptr)
            preview.setImage (p->makeLayerThumbnail());
}

void BevelDialog::updateEnablement()
{
    // Nothing below the toggle does anything while the bevel is off, so nothing
    // below the toggle should look as though it might.
    const bool on = enableButton.getToggleState() && target() != nullptr;

    for (auto* child : getChildren())
        if (child != &enableButton)
            child->setEnabled (on);
}

void BevelDialog::pull()
{
    if (updating)
        return;

    auto* b = target();

    if (b == nullptr)
        return;

    b->enabled   = enableButton.getToggleState();
    b->style     = (BevelStyle) juce::jlimit (0, (int) BevelStyle::numStyles - 1,
                                              styleBox.getSelectedId() - 1);
    b->technique = (BevelTechnique) juce::jlimit (0, (int) BevelTechnique::numTechniques - 1,
                                                  techniqueBox.getSelectedId() - 1);

    b->depth  = (float) depthSlider.getValue();
    b->size   = (float) sizeSlider.getValue();
    b->soften = (float) softenSlider.getValue();
    b->up     = upButton.getToggleState();

    b->useGlobalLight = globalLightButton.getToggleState();
    b->angle    = (float) angleSlider.getValue();
    b->altitude = (float) altitudeSlider.getValue();

    b->contourEnabled = contourEnableButton.getToggleState();
    b->bevelContour   = bevelContourEditor.getContour();
    b->contourRange   = (float) rangeSlider.getValue();

    b->glossContour = contourEditor.getContour();
    b->antiAliased  = antiAliasButton.getToggleState();

    contourPresetBox.setContour (b->glossContour);
    bevelContourPresetBox.setContour (b->bevelContour);

    b->highlightMode    = (BlendMode) juce::jlimit (0, (int) BlendMode::numModes - 1,
                                                    highlightModeBox.getSelectedId() - 1);
    b->highlightOpacity = (float) highlightOpacitySlider.getValue();

    b->shadowMode    = (BlendMode) juce::jlimit (0, (int) BlendMode::numModes - 1,
                                                 shadowModeBox.getSelectedId() - 1);
    b->shadowOpacity = (float) shadowOpacitySlider.getValue();

    notifyChanged();
    refreshPreview();
}

//==============================================================================
void BevelDialog::openColour (juce::TextButton& button, bool highlight)
{
    if (target() == nullptr)
        return;

    editingHighlightColour = highlight;

    auto selector = std::make_unique<juce::ColourSelector> (
        juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
      | juce::ColourSelector::showColourspace | juce::ColourSelector::showAlphaChannel);

    selector->setCurrentColour (highlight ? target()->highlightColour : target()->shadowColour,
                                juce::dontSendNotification);
    selector->setSize (300, 380);
    selector->addChangeListener (this);

    juce::CallOutBox::launchAsynchronously (std::move (selector),
                                            button.getScreenBounds(), nullptr);
}

void BevelDialog::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    auto* b = target();

    if (b == nullptr)
        return;

    if (auto* selector = dynamic_cast<juce::ColourSelector*> (source))
    {
        if (editingHighlightColour) b->highlightColour = selector->getCurrentColour();
        else                        b->shadowColour    = selector->getCurrentColour();

        refresh();
        notifyChanged();
    }
}

//==============================================================================
void BevelDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262629));
}

void BevelDialog::resized()
{
    auto area = getLocalBounds().reduced (kMargin);

    // Two columns. Structure and the bevel Contour on the left, everything to
    // do with light on the right, with the live thumbnail beside it.
    auto left  = area.removeFromLeft ((area.getWidth() - kMargin) / 2);
    area.removeFromLeft (kMargin);
    auto right = area;

    auto row = [] (juce::Rectangle<int>& column, int height)
    {
        auto r = column.removeFromTop (height);
        column.removeFromTop (4);
        return r;
    };

    auto labelled = [] (juce::Rectangle<int> r, juce::Component& label, juce::Component& control)
    {
        label.setBounds (r.removeFromLeft (kLabelW));
        control.setBounds (r);
    };

    //--------------------------------------------------------------- left ----
    enableButton.setBounds (row (left, kRowH));
    left.removeFromTop (6);

    structureHeader.setBounds (row (left, 20));
    labelled (row (left, kRowH), styleLabel, styleBox);
    labelled (row (left, kRowH), techniqueLabel, techniqueBox);
    labelled (row (left, kRowH), depthLabel, depthSlider);

    {
        auto r = row (left, kRowH);
        directionLabel.setBounds (r.removeFromLeft (kLabelW));
        upButton.setBounds (r.removeFromLeft (70));
        r.removeFromLeft (8);
        downButton.setBounds (r.removeFromLeft (80));
    }

    labelled (row (left, kRowH), sizeLabel, sizeSlider);
    labelled (row (left, kRowH), softenLabel, softenSlider);

    left.removeFromTop (8);
    contourHeader.setBounds (row (left, 20));

    {
        auto r = row (left, kRowH);
        contourEnableButton.setBounds (r.removeFromLeft (kLabelW));
        bevelContourPresetBox.setBounds (r.removeFromLeft (72));
    }

    bevelContourEditor.setBounds (left.removeFromTop (118));
    left.removeFromTop (4);

    labelled (row (left, kRowH), rangeLabel, rangeSlider);

    //-------------------------------------------------------------- right ----
    // The thumbnail sits at the top of the light column, where the eye already
    // is while pushing Angle and Altitude around.
    preview.setBounds (right.removeFromTop (118).removeFromLeft (118));
    right.removeFromTop (6);

    shadingHeader.setBounds (row (right, 20));

    {
        auto r = row (right, kRowH);
        r.removeFromLeft (kLabelW);
        globalLightButton.setBounds (r);
    }

    const bool custom = ! globalLightButton.getToggleState();

    angleLabel.setVisible (custom);
    angleSlider.setVisible (custom);
    angleDial.setVisible (custom);
    altitudeLabel.setVisible (custom);
    altitudeSlider.setVisible (custom);

    if (custom)
    {
        auto r = row (right, kRowH);
        angleLabel.setBounds (r.removeFromLeft (kLabelW));
        angleDial.setBounds (r.removeFromRight (kRowH));
        r.removeFromRight (6);
        angleSlider.setBounds (r);

        labelled (row (right, kRowH), altitudeLabel, altitudeSlider);
    }

    {
        auto r = row (right, kRowH);
        contourLabel.setBounds (r.removeFromLeft (kLabelW));
        contourPresetBox.setBounds (r.removeFromLeft (72));
        r.removeFromLeft (10);
        antiAliasButton.setBounds (r);
    }

    contourEditor.setBounds (right.removeFromTop (96));
    right.removeFromTop (6);

    {
        auto r = row (right, kRowH);
        highlightLabel.setBounds (r.removeFromLeft (kLabelW));
        highlightColourButton.setBounds (r.removeFromRight (92));
        r.removeFromRight (6);
        highlightModeBox.setBounds (r);
    }

    labelled (row (right, kRowH), highlightOpacityLabel, highlightOpacitySlider);

    {
        auto r = row (right, kRowH);
        shadowLabel.setBounds (r.removeFromLeft (kLabelW));
        shadowColourButton.setBounds (r.removeFromRight (92));
        r.removeFromRight (6);
        shadowModeBox.setBounds (r);
    }

    labelled (row (right, kRowH), shadowOpacityLabel, shadowOpacitySlider);
}
