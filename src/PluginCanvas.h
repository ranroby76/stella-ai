// C:\workspace\Stella AI Studio\src\PluginCanvas.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "GuiLayout.h"
#include "Looks.h"
#include "Primitives.h"

#include <bitset>
#include <functional>
#include <map>
#include <memory>
#include <optional>

//==============================================================================
/**
    The Edit UI tab's canvas: the plugin's window, drawn from its layout, sitting on a big
    grid you zoom and move around, like Colosseum's rack. Ctrl + wheel zooms (10% to 200%)
    around the mouse, the wheel scrolls (Shift + wheel: sideways), dragging empty space
    moves the view, and the minimap and the scrollbars jump anywhere. Fit shows the whole
    window.

    Play mode: the controls work like the finished plugin's: drag a knob or slider
    (Shift for fine moves), click a switch or a selector's position, drag in an XY pad,
    double-click for the default, use the mouse wheel; a keyboard's keys play notes while
    they're held (sliding across them plays each in turn). Meters, lamps and scopes move
    with the sound; envelope and filter curves follow their parameters; keyboards light up
    for every note that sounds, a MIDI keyboard's too.

    Edit mode: drag primitives in from the toolbox (or pictures from the desktop). Click an
    element to select it and open its edit menu, which also takes an instruction for
    Stella AI; click the window's empty space for the window's own menu (size, colours,
    a picture behind everything). Drag to move (snapped); drag the selected element's
    handles (corners and sides) to resize it, Shift on a corner keeps its shape. Delete
    removes it, the arrow keys nudge it (Shift: further). Dragging a group carries what's
    inside. The window's own corner handle resizes the window. Double-clicking a knob,
    slider or switch opens its look in the Knob Studio; double-clicking a picture picks its
    file.

    Elements lie in layers, in the layout's order: the first at the back. Right-click one
    for Bring to front, Bring forward, Send backward, Send to back (also Ctrl+Shift+],
    Ctrl+], Ctrl+[, Ctrl+Shift+[). A background picture is a picture element covering the
    window at the back; until it's selected, dragging it moves the view, as empty space does.

    Knobs, sliders and switches are drawn from their looks (see Looks): a frame for each
    value, made by KnobMaker's layer renderer.
*/
class PluginCanvas final : public juce::Component,
                           public juce::DragAndDropTarget,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    struct Param
    {
        int index = -1;
        juce::String id, name, unit;
        float min = 0.0f, max = 1.0f, def = 0.0f, value = 0.0f, skew = 1.0f;
    };

    PluginCanvas();
    ~PluginCanvas() override;

    void setLayout (const GuiLayout& newLayout);
    const GuiLayout& getLayout() const noexcept    { return layout; }

    /** The open project's gui folder: its pictures are in images/, the view is kept in
        view.json. None: no project. */
    void setGuiFolder (const juce::File& folder);

    void setDesignMode (bool shouldDesign);
    void setParameters (const juce::Array<Param>& newParams);

    /** What meters, lamps and scopes can watch: signals ("voices.out", "plugin.out L") and
        module displays ("lfo.position"). For the edit menu's lists. */
    void setSources (const juce::StringArray& signals, const juce::StringArray& displays);

    /** The presets a preset widget steps through, and the one chosen. */
    void setPresets (const juce::StringArray& names, int current);

    /** The looks knobs, sliders and switches are drawn from (owned by the Workspace). */
    void setLooks (Looks& looksToUse);

    /** A look changed, came or went: draw again. */
    void looksChanged();

    /** A control takes a look; or gets a copy of its look of its own (named after its caption,
        returned), so changing it changes only that control. */
    void useLook (int widgetIndex, const juce::String& lookName);
    juce::String giveOwnLook (int widgetIndex);

    /** Edit mode: a primitive's element, centred on a point of the plugin window (in its
        pixels), or in the middle of what's showing. A background primitive asks for the
        picture instead. */
    void addPrimitive (const Primitive& primitive, std::optional<juce::Point<float>> centre = {});

    //==========================================================================
    /** The view: how big the window shows, 10% to 200%, kept around a point of the canvas
        (by default its middle). */
    void setZoom (float newZoom, std::optional<juce::Point<float>> around = {});
    float getZoom() const noexcept    { return zoom; }

    /** The whole window in view (at most at 100%). */
    void fitToView();

    static constexpr float minZoom = 0.10f, maxZoom = 2.0f;

    //==========================================================================
    /** An edit menu's instruction for Stella AI, about one element (-1: the whole window). */
    std::function<void (int widgetIndex, const juce::String& instruction)> onAskAi;

    /** The banner's Open chat button. */
    std::function<void()> onOpenChat;

    /** The banner across the top: Stella AI at work on a request made here, then its answer. */
    void showAiWorking (const juce::String& what);
    void showAiReply (const juce::String& reply);

    //==========================================================================
    /** For Export: the GUI as images the exported plugin draws from. Everything that never
        moves is in the background (pictures too); each control's moving part is a strip of
        frames (controls with the same look and size share one: their look's own frames, a
        switch's off and on). Meters, scopes, curves and XY dots are drawn live by the plugin
        itself. */
    struct Bake
    {
        juce::Image background;

        struct Strip
        {
            juce::Image image;   // frames stacked vertically
            int frameWidth = 0, frameHeight = 0, frames = 0;
        };

        std::vector<Strip> strips;
        std::vector<int> stripOf;                        // per widget: its strip, or -1
        std::vector<juce::Rectangle<int>> movingArea;    // per widget: where its moving part goes
    };

    Bake bake();

    std::function<void (int index, float value)> onParameterChanged;
    std::function<void (int note, float velocity)> onNote;         // a keyboard's key: down (velocity 0..1) or up (0)
    std::function<bool (int note)> isNoteDown;                     // whether a note is sounding, for the keyboards to show
    std::function<void()> onLayoutEdited;                          // save it
    std::function<void (int widgetIndex)> onEditLook;              // open a control's look in the Knob Studio
    std::function<void (int presetIndex)> onPresetChosen;          // a preset widget was clicked in Play mode

    /** Live data, read about 30 times a second. */
    std::function<float (const juce::String& source, bool rms)> readLevel;
    std::function<void (const juce::String& source, float* destination, int numSamples)> readScope;
    std::function<void (const juce::StringArray& levels, const juce::StringArray& scopes)> onSourcesChanged;

    //==========================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void moved() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Primitives dragged in from the toolbox.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    // Pictures dragged in from the desktop.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    class ElementMenu;
    class AiBanner;

    //==========================================================================
    // The elements
    static constexpr int snapStep = 4;                 // design grid, in plugin pixels
    static constexpr int edgeSnap = 8;                 // how near a resized side sticks to the window's edge
    static constexpr float handleSize = 10.0f;         // resize handles, in view pixels
    static int snapped (float v)                       { return juce::roundToInt (v / (float) snapStep) * snapStep; }
    static bool isInteractive (const GuiWidget& w);
    static juce::String typeDisplayName (GuiWidget::Type type);
    static juce::Rectangle<int> defaultBounds (GuiWidget::Type type, int x, int y);
    static const char* defaultStarPath();

    /** A project is open: the window shows (even with nothing on it yet). */
    bool hasPanel() const noexcept    { return guiFolder != juce::File(); }

    void timerCallback() override;
    int widgetAt (juce::Point<float> plugin, bool interactiveOnly) const;
    const Param* paramById (const juce::String& id) const;
    const Param* paramFor (const GuiWidget& widget) const    { return paramById (widget.param); }
    float proportionOf (const Param& p, float value) const;
    float valueAt (const Param& p, float proportion) const;
    float currentValue (const Param& p) const;
    float roleProportion (const GuiWidget& w, const juce::String& role, float fallback) const;
    void setValue (const Param& p, float value);
    void setXy (const GuiWidget& w, juce::Point<float> plugin);

    /** What to draw of a widget: all of it, only what never moves, or only what moves. */
    enum class Part { all, still, moving };

    /** override >= 0 draws the moving part as if at that value: a proportion for knobs and
        sliders, 0 or 1 for switches and lamps, a position for selectors. */
    void drawWidget (juce::Graphics&, int index, Part part = Part::all, float override = -1.0f);
    void drawLive (juce::Graphics&, const GuiWidget&, int index, Part part, float override);
    void drawCurve (juce::Graphics&, const GuiWidget&, Part part);
    void drawPanel (juce::Graphics&);   // the window's colours and background picture, in plugin pixels
    void drawPicture (juce::Graphics&, const juce::String& name, juce::Rectangle<float> area, const juce::String& mode);
    juce::Rectangle<int> movingAreaOf (const GuiWidget& widget) const;
    juce::Rectangle<int> knobSquare (const GuiWidget& widget) const;

    /** A knob's, slider's or switch's look: its frame for a value, in its moving area. */
    void drawLook (juce::Graphics&, const GuiWidget&, juce::Rectangle<int> area, float value);

    /** A keyboard: its keys at rest (what never moves), then the keys that are down, drawn
        the way the exported plugin draws them over its baked background. */
    void drawKeyboard (juce::Graphics&, const GuiWidget&, int index, Part part);
    void drawKeyboardAtRest (juce::Graphics&, const GuiWidget&);

    /** Play mode: the key under the mouse plays (the one before stops); -1 lets go. */
    void playKey (int widgetIndex, juce::Point<float> plugin);
    void releaseKey();

    /** The look most of the window's controls of this kind wear, for a new one (none: the default). */
    juce::String commonLook (const GuiWidget& widget) const;
    void edited();
    void reportSources();

    /** Adds an element: its parameter, caption and the like filled in where they're missing.
        Returns its index. */
    int addElement (GuiWidget w, std::optional<juce::Point<float>> centre);
    void removeSelected();

    /** An element's box centred on a point (plugin pixels), snapped, inside the window. */
    juce::Rectangle<int> placed (juce::Rectangle<int> bounds, juce::Point<float> centre) const;

    /** Asks for a picture and copies it into the project: for an element, or (-1) the background. */
    void choosePicture (int widgetIndex);
    void usePicture (int widgetIndex, const juce::String& name);
    const juce::Image& picture (const juce::String& name);
    juce::File imagesFolder() const;

    //==========================================================================
    // The view (Colosseum's rack): plugin pixel p shows at (p - pan) * zoom.
    juce::Rectangle<float> panelArea() const;   // where the plugin window shows
    juce::Point<float> toPlugin (juce::Point<float> view) const;
    juce::Rectangle<float> toView (juce::Rectangle<int> plugin) const;
    juce::Rectangle<float> worldBounds() const;  // how far the view can go, in plugin pixels
    juce::Rectangle<float> viewArea() const;     // the canvas minus the scrollbars
    void clampPan();
    void centreOn (juce::Point<float> plugin);
    void followPosition();                       // the window stays put when the canvas's left edge moves
    void viewChanged();
    void loadView();
    void saveViewSoon();

    juce::Rectangle<float> minimapArea() const;
    juce::Rectangle<float> hScrollArea() const;
    juce::Rectangle<float> vScrollArea() const;
    juce::Rectangle<float> hThumb() const;
    juce::Rectangle<float> vThumb() const;
    juce::Rectangle<float> panelHandle() const;  // the window's own resize corner

    /** The selected element's resize handles: 0 top-left, 1 top, 2 top-right, 3 right,
        4 bottom-right, 5 bottom, 6 bottom-left, 7 left. Knobs (they stay round) have the
        corners only; a small element, its corners only. Empty: no such handle. */
    juce::Rectangle<float> handleArea (int handle) const;
    int handleAt (juce::Point<float> view) const;   // -1: none

    //==========================================================================
    // Layers: the layout's order, the first element at the back.
    enum class Order { front, forward, backward, back };
    void reorder (int index, Order order);
    void showElementPopup (int index);
    void showWindowPopup();

    /** A picture covering the whole window: the background. */
    bool isBackdrop (const GuiWidget& w) const;
    void navigateMinimapTo (juce::Point<float> view);
    void drawGrid (juce::Graphics&);
    void drawMinimap (juce::Graphics&);
    void drawScrollbars (juce::Graphics&);
    void drawDropGhost (juce::Graphics&);
    void showDropGhost (const juce::String& primitiveId, juce::Point<float> view);
    void repaintDropGhost();

    //==========================================================================
    // Selection and the edit menu
    void select (int index);                     // -1: nothing
    void selectPanel();
    void openMenu();                             // for what's selected
    void closeMenu();
    void layOutOverlays();
    void updateZoomControls();

    //==========================================================================
    GuiLayout layout;
    juce::Array<Param> params;
    std::map<int, float> values;                                   // by parameter index
    Looks* looks = nullptr;
    juce::StringArray signalSources, displaySources;
    juce::StringArray presetNames;
    int currentPreset = -1;

    int heldNote = -1, heldKeyboard = -1;                          // the key the mouse holds down, and on which keyboard
    std::bitset<128> notesShown;                                   // the notes the keyboards show as down

    std::map<int, float> meterLevels;                              // by widget: shown level, with fall-back
    std::map<juce::String, float> frameLevels;                     // read once per frame per source
    juce::StringArray reportedLevels, reportedScopes;
    std::vector<float> scopeBuffer;

    juce::File guiFolder;
    std::map<juce::String, juce::Image> pictures;                  // by file name in images/
    std::unique_ptr<juce::FileChooser> pictureChooser;

    bool design = true, baking = false;
    int selected = -1, active = -1, hovered = -1;
    bool panelSelected = false, menuOpen = false;

    // What a press is doing.
    enum class Drag { none, element, resizeElement, resizePanel, pan, minimap, hScroll, vScroll, control };
    Drag drag = Drag::none;
    bool changed = false, dragMoved = false;                       // dragMoved: the press became a drag
    bool pressedPanel = false;                                     // the press started on the window's empty space
    int pressedBackdrop = -1;                                      // ...or on the background picture, not selected yet
    int resizeHandle = -1;                                         // the handle being dragged (see handleArea)
    bool reopenMenu = false;                                       // the menu was open when the press started
    juce::Point<float> dragStart;
    juce::Point<float> panAtDragStart;
    float startProportion = 0.0f;
    juce::Rectangle<int> startBounds;
    juce::Point<int> startPanelSize;
    std::vector<std::pair<int, juce::Rectangle<int>>> carried;     // a group's contents, while it moves

    // The view.
    float zoom = 1.0f;
    juce::Point<float> pan;                                        // the plugin pixel at the view's top-left
    int lastX = 0;                                                 // where the canvas sat in its parent
    bool autoFit = true;               // the view follows the window until the user moves it
    bool viewSaveScheduled = false;

    // Something dragged over the canvas: where it would land, in plugin pixels.
    std::optional<juce::Rectangle<int>> dropGhost;

    std::unique_ptr<ElementMenu> menu;
    std::unique_ptr<AiBanner> banner;
    juce::TextButton fitButton { "Fit" };
    juce::Slider zoomSlider;
    juce::Label zoomLabel;

    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCanvas)
};
