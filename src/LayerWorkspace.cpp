// C:\workspace\Stella AI Studio\src\LayerWorkspace.cpp
// From KnobMaker (C:\workspace\knobmaker); in Stella see LayerWorkspace.h for what changed.

/*
    LayerWorkspace.cpp
*/

#include "LayerWorkspace.h"
#include "UiHelpers.h"

namespace
{
    // Document-bar metrics. Stella: the bar holds only the canvas size and the
    // frame count (the studio's New look menu has the examples, the gallery and
    // the .knob import).
    constexpr int kBarGap   = 6;

    constexpr int kWDimLabel   =  20;
    constexpr int kWDimSpin    = 104;
    constexpr int kWFrameLabel =  52;
    constexpr int kWFrameSpin  = 104;
    constexpr int kWLink       =  56;
}

//==============================================================================
CanvasPreview::CanvasPreview()
{
}

void CanvasPreview::setDocument (LayerDoc* d)
{
    document = d;
    invalidate();
}

void CanvasPreview::invalidate()
{
    dirty = true;
    repaint();
}

void CanvasPreview::setFrame (float t)
{
    time = juce::jlimit (0.0f, 1.0f, t);
    invalidate();
}

void CanvasPreview::nudgeFrame (float delta)
{
    // Wraps, because a knob does not stop being a knob at the end of its
    // filmstrip and scrubbing past the end should come back round.
    float t = time + delta;
    t -= std::floor (t);

    setFrame (t);

    if (onFrameDragged != nullptr)
        onFrameDragged (t);
}

void CanvasPreview::mouseDown (const juce::MouseEvent&)
{
    // Turning it by hand is a deliberate act; playback would fight it.
    setPlaying (false);
    dragStartTime = time;
}

void CanvasPreview::mouseDrag (const juce::MouseEvent& e)
{
    // Up and right both increase, which is how every DAW rotary behaves.
    const float travel = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
    const float sensitivity = e.mods.isShiftDown() ? 600.0f : 180.0f;

    float t = dragStartTime + travel / sensitivity;
    t -= std::floor (t);

    setFrame (t);

    if (onFrameDragged != nullptr)
        onFrameDragged (t);
}

void CanvasPreview::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    setPlaying (false);
    nudgeFrame (wheel.deltaY * 0.25f);
}

void CanvasPreview::setActualSize (bool shouldUseActualSize)
{
    if (actualSize == shouldUseActualSize)
        return;

    actualSize = shouldUseActualSize;
    invalidate();
}

void CanvasPreview::cycleBackground()
{
    backgroundMode = (backgroundMode + 1) % 3;
    repaint();
}

void CanvasPreview::setPlaying (bool shouldPlay)
{
    playing = shouldPlay;

    if (playing) startTimerHz (30);
    else         stopTimer();
}

void CanvasPreview::timerCallback()
{
    if (document == nullptr)
        return;

    const int n = juce::jmax (1, document->frames);
    time += 1.0f / (float) juce::jmax (2, n);

    if (time > 1.0f)
        time = 0.0f;

    invalidate();
}

void CanvasPreview::rerender (float scale)
{
    if (document == nullptr)
        return;

    RenderNotes notes;

    frame = LayerRender::renderFrame (*document, time, scale, &notes);
    renderedScale = scale;
    dirty = false;

    // Only speak up when the situation changes. This runs every frame during
    // playback, and a status line that rewrites itself thirty times a second is
    // worse than no status line at all.
    const auto note = notes.messages.joinIntoString ("   |   ");

    if (note != lastRenderNote)
    {
        lastRenderNote = note;

        if (onRenderNote != nullptr)
            onRenderNote (note);
    }
}

void CanvasPreview::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    switch (backgroundMode)
    {
        case 1:  g.fillAll (juce::Colour (0xffe8e8e8)); break;
        case 2:
        {
            const int cell = 12;
            g.fillAll (juce::Colour (0xff909090));
            g.setColour (juce::Colour (0xffb4b4b4));

            for (int y = 0; y < area.getHeight(); y += cell)
                for (int x = 0; x < area.getWidth(); x += cell)
                    if (((x / cell) + (y / cell)) % 2 == 0)
                        g.fillRect (x, y, cell, cell);

            break;
        }
        default: g.fillAll (juce::Colour (0xff1b1b1e)); break;
    }

    if (document == nullptr)
        return;

    // Work the display size out from the DOCUMENT, not from the cached image —
    // the image's own size now depends on the scale we are about to choose.
    const int docW = juce::jmax (8, document->canvasWidth);
    const int docH = juce::jmax (8, document->canvasHeight);

    const float fitW = (float) area.getWidth()  / (float) docW;
    const float fitH = (float) area.getHeight() / (float) docH;

    // 1:1 means exactly that — one document pixel to one screen pixel, which is
    // what the exported filmstrip frame will be. No fitting, no 0.85 margin.
    const float zoom = actualSize ? 1.0f
                                  : juce::jmax (0.05f, juce::jmin (fitW, fitH) * 0.85f);

    // This is the whole difference between the two tabs. The Knob page draws
    // its flutes, pointer and ticks as live geometry at the on-screen size and
    // supersamples its cached body layers, so it is sharp at any size. This
    // page used to rasterise the document once at its own pixel size — 128x128
    // — and then blow that up threefold, which is why it looked chewed.
    //
    // LayerRender is resolution independent: every primitive is described in
    // fractions of the canvas, so rendering at scale gives a genuinely sharper
    // image rather than an upscaled one. So render at the size we are actually
    // going to draw at, including the display's physical pixel ratio. Capped,
    // because playback re-renders every frame and the per-pixel emboss and
    // gloss passes are not free.
    const float physical = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const float wanted   = juce::jlimit (0.25f, 4.0f, zoom * juce::jmax (1.0f, physical));

    if (dirty || ! frame.isValid() || std::abs (wanted - renderedScale) > 0.01f)
        rerender (wanted);

    if (! frame.isValid())
        return;

    const int w = juce::jmax (1, (int) std::lround (docW * zoom));
    const int h = juce::jmax (1, (int) std::lround (docH * zoom));

    auto dest = juce::Rectangle<int> (w, h).withCentre (area.getCentre());

    // The integer-zoom step that used to live here existed only to stop the
    // upscale smearing. There is no upscale now, so the preview can use the
    // whole panel instead of snapping down to the nearest whole multiple.
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (frame, dest.toFloat(), juce::RectanglePlacement::stretchToFit);

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRect (dest, 1);
}

void CanvasPreview::resized()
{
    invalidate();
}

//==============================================================================
LayerWorkspace::LayerWorkspace()
{
    addAndMakeVisible (canvas);
    canvas.setDocument (&doc);

    layerList.setDocument (&doc);
    layerList.onSelectionChange = [this] { selectionChanged(); };
    layerList.onDocumentChange  = [this] { documentChanged(); };
    addAndMakeVisible (layerList);

    primPanel.onChange   = [this] { documentChanged(); };
    effectPanel.onChange = [this] { documentChanged(); };

    // The Bevel window shows the layer it is editing. Only this class knows
    // both the document and which row is selected, so it supplies the render.
    primPanel.makeLayerThumbnail = [this]() -> juce::Image
    {
        const int row = layerList.getSelectedIndex();
        const int index = (int) doc.layers.size() - 1 - row;

        if (index < 0 || index >= (int) doc.layers.size())
            return {};

        // Scale 1: canvas size, which is small and cheap. The thumbnail is
        // drawn fitted, so there is nothing to gain from rendering it larger.
        return LayerRender::renderLayer (doc, index, canvas.getFrame(), 1.0f);
    };

    primPanel.onLayoutChanged   = [this] { layoutEditorPanels(); };
    effectPanel.onLayoutChanged = [this] { layoutEditorPanels(); };

    primViewport.setViewedComponent (&primPanel, false);
    primViewport.setScrollBarsShown (true, false);

    effectViewport.setViewedComponent (&effectPanel, false);
    effectViewport.setScrollBarsShown (true, false);

    tabs.setOutline (0);
    tabs.addTab ("Shape",  juce::Colour (0xff262629), &primViewport,   false);
    tabs.addTab ("Effect", juce::Colour (0xff262629), &effectViewport, false);
    addAndMakeVisible (tabs);

    frameSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    frameSlider.setRange (0.0, 1.0, 0.0);
    frameSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // Scrubbing a filmstrip means clicking a spot on the strip and landing on
    // it, so this one goes on tracking the pointer whatever the panel
    // sensitivity is set to.
    keepPointerTracking (frameSlider);
    frameSlider.onValueChange = [this]
    {
        canvas.setFrame ((float) frameSlider.getValue());
        const int n = juce::jmax (1, doc.frames);
        const int f = juce::jlimit (1, n, 1 + (int) std::lround (frameSlider.getValue() * (n - 1)));
        frameLabel.setText (juce::String (f) + " / " + juce::String (n), juce::dontSendNotification);
    };
    addAndMakeVisible (frameSlider);

    // Dragging on the widget itself moves the transport with it, so the frame
    // readout and the slider never disagree with what is on screen.
    canvas.onFrameDragged = [this] (float t)
    {
        frameSlider.setValue (t, juce::dontSendNotification);

        const int n = juce::jmax (1, doc.frames);
        const int f = juce::jlimit (1, n, 1 + (int) std::lround (t * (n - 1)));
        frameLabel.setText (juce::String (f) + " / " + juce::String (n), juce::dontSendNotification);

        playButton.setButtonText (canvas.isPlaying() ? "Stop" : "Play");
    };

    canvas.onRenderNote = [this] (const juce::String& note)
    {
        if (onStatus != nullptr)
            onStatus (note.isEmpty() ? "Ready" : note);
    };

    canvas.setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);

    playButton.onClick = [this]
    {
        canvas.setPlaying (! canvas.isPlaying());
        playButton.setButtonText (canvas.isPlaying() ? "Stop" : "Play");
    };
    addAndMakeVisible (playButton);

    frameLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    frameLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    frameLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (frameLabel);

    // The wheel scrolls panels; it does not nudge whatever control happens to
    // be under the pointer. The canvas preview is excluded on purpose — see
    // CanvasPreview::mouseWheelMove, where the wheel scrubs frames.
    configurePanelControls (*this);

    auto setupSpin = [this] (juce::Slider& s, double min, double max, double value)
    {
        s.setSliderStyle (juce::Slider::IncDecButtons);
        s.setRange (min, max, 1.0);
        s.setValue (value, juce::dontSendNotification);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 20);
        addAndMakeVisible (s);
    };

    setupSpin (canvasWSlider, 8, 1024, doc.canvasWidth);
    setupSpin (canvasHSlider, 8, 1024, doc.canvasHeight);
    setupSpin (framesSlider,  1,  512, doc.frames);

    // W and H get their own handlers now, because Link has to know which one
    // the user actually moved in order to mirror it into the other.
    canvasWSlider.onValueChange = [this]
    {
        if (linkSizeButton.getToggleState())
            canvasHSlider.setValue (canvasWSlider.getValue(), juce::dontSendNotification);

        pullCanvasFields();
    };

    canvasHSlider.onValueChange = [this]
    {
        if (linkSizeButton.getToggleState())
            canvasWSlider.setValue (canvasHSlider.getValue(), juce::dontSendNotification);

        pullCanvasFields();
    };

    framesSlider.onValueChange = [this] { pullCanvasFields(); };

    linkSizeButton.setColour (juce::ToggleButton::textColourId, juce::Colours::grey);
    linkSizeButton.onClick = [this]
    {
        // Ticking it squares the canvas up straight away rather than waiting
        // for the next nudge, so the state you see matches the state you get.
        if (! linkSizeButton.getToggleState())
            return;

        canvasHSlider.setValue (canvasWSlider.getValue(), juce::dontSendNotification);
        pullCanvasFields();
    };
    addAndMakeVisible (linkSizeButton);

    for (auto* l : { &canvasWLabel, &canvasHLabel, &framesLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colours::grey);
        l->setFont (juce::Font (juce::FontOptions (12.0f)));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    selectionChanged();
    frameSlider.onValueChange();
}

LayerWorkspace::~LayerWorkspace()
{
    tabs.clearTabs();
}

//==============================================================================
void LayerWorkspace::setDocument (const LayerDoc& d)
{
    const juce::ScopedValueSetter<bool> quiet (settingDocument, true);
    doc = d;
    layerList.setDocument (&doc);
    canvas.setDocument (&doc);
    updateCanvasSizeFields();
    selectionChanged();
    documentChanged();
}

void LayerWorkspace::updateCanvasSizeFields()
{
    canvasWSlider.setValue (doc.canvasWidth,  juce::dontSendNotification);
    canvasHSlider.setValue (doc.canvasHeight, juce::dontSendNotification);
    framesSlider.setValue  (doc.frames,       juce::dontSendNotification);

    // A document that is not square cannot have been linked, so do not claim it
    // was — the next nudge would silently destroy one of its two dimensions.
    if (doc.canvasWidth != doc.canvasHeight)
        linkSizeButton.setToggleState (false, juce::dontSendNotification);
}

void LayerWorkspace::pullCanvasFields()
{
    doc.canvasWidth  = (int) canvasWSlider.getValue();
    doc.canvasHeight = (int) canvasHSlider.getValue();
    doc.frames       = (int) framesSlider.getValue();
    documentChanged();
}

void LayerWorkspace::selectionChanged()
{
    const int row = layerList.getSelectedIndex();
    const int index = (int) doc.layers.size() - 1 - row;

    DocLayer* layer = (index >= 0 && index < (int) doc.layers.size())
                        ? &doc.layers[(size_t) index]
                        : nullptr;

    primPanel.setLayer (layer);
    effectPanel.setLayer (layer);
    resized();
}

void LayerWorkspace::documentChanged()
{
    canvas.invalidate();
    layerList.refresh();

    const int n = juce::jmax (1, doc.frames);
    const int f = juce::jlimit (1, n, 1 + (int) std::lround (frameSlider.getValue() * (n - 1)));
    frameLabel.setText (juce::String (f) + " / " + juce::String (n), juce::dontSendNotification);

    // Stella: the studio saves it into the project and the plugin shows it.
    if (! settingDocument && onDocumentChanged != nullptr)
        onDocumentChanged();
}

//==============================================================================
void LayerWorkspace::applyImport (const LayerDoc& imported, const juce::String& name,
                                  const juce::StringArray& warnings)
{
    // Stella: an import is a new look of its own; the studio adds it and opens it here.
    if (onImported != nullptr)
        onImported (imported, name);
    else
        setDocument (imported);

    juce::String message = "Imported " + name + " - "
                             + juce::String ((int) imported.layers.size()) + " layers, "
                             + juce::String (imported.frames) + " frames";

    if (! warnings.isEmpty())
        message += "  |  " + warnings.joinIntoString ("  |  ");

    if (onStatus != nullptr)
        onStatus (message);
}

void LayerWorkspace::openGallery()
{
    auto browser = std::make_unique<GalleryBrowser>();
    browser->setSize (720, 560);

    browser->onImport = [this] (const LayerDoc& d, const juce::String& n,
                                const juce::StringArray& w)
    {
        applyImport (d, n, w);
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (browser.release());
    options.dialogTitle = "KnobMan Gallery";
    options.dialogBackgroundColour = juce::Colour (0xff262629);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}

void LayerWorkspace::importKnobFile()
{
    knobChooser = std::make_unique<juce::FileChooser> ("Import a KnobMan file",
                                                       juce::File(), "*.knob");

    knobChooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == juce::File())
            return;

        auto result = KnobFile::parseFile (file);

        if (! result.ok)
        {
            if (onStatus != nullptr)
                onStatus ("Import failed: " + result.error);

            return;
        }

        applyImport (result.document, file.getFileName(), result.warnings);
    });
}

//==============================================================================
void LayerWorkspace::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e21));
}

void LayerWorkspace::layoutEditorPanels()
{
    primPanel.setSize   (editorViewW, juce::jmax (editorViewH, primPanel.getRequiredHeight()));
    effectPanel.setSize (editorViewW, juce::jmax (editorViewH, effectPanel.getRequiredHeight()));
}

void LayerWorkspace::resized()
{
    auto area = getLocalBounds();

    // ---- document bar ------------------------------------------------------
    // Stella: one row, the canvas size and the frame count.
    {
        auto rowTwo = area.removeFromTop (34).reduced (8, 4);

        auto place = [] (juce::Rectangle<int>& bar, juce::Component& c, int w)
        {
            if (bar.getWidth() < w)
            {
                c.setBounds ({});   // nowhere at all, never at stale bounds
                return;
            }

            c.setBounds (bar.removeFromLeft (w));
            bar.removeFromLeft (kBarGap);
        };

        place (rowTwo, canvasWLabel,  kWDimLabel);
        place (rowTwo, canvasWSlider, kWDimSpin);
        place (rowTwo, canvasHLabel,  kWDimLabel);
        place (rowTwo, canvasHSlider, kWDimSpin);
        place (rowTwo, linkSizeButton, kWLink);
        place (rowTwo, framesLabel,   kWFrameLabel);
        place (rowTwo, framesSlider,  kWFrameSpin);
    }

    // ---- right-hand editing column ----------------------------------------
    auto right = area.removeFromRight (juce::jlimit (300, 400, area.getWidth() / 3));
    layerList.setBounds (right.removeFromTop (juce::jlimit (120, 200, right.getHeight() / 3)));
    tabs.setBounds (right);

    const int viewW = right.getWidth() - primViewport.getScrollBarThickness();
    const int viewH = juce::jmax (100, right.getHeight() - 32);

    editorViewW = juce::jmax (100, viewW);
    editorViewH = viewH;

    layoutEditorPanels();

    // ---- transport ---------------------------------------------------------
    {
        auto transport = area.removeFromBottom (34).reduced (8, 4);
        playButton.setBounds (transport.removeFromLeft (60));
        transport.removeFromLeft (8);
        frameLabel.setBounds (transport.removeFromRight (80));
        frameSlider.setBounds (transport);
    }

    canvas.setBounds (area);
}
