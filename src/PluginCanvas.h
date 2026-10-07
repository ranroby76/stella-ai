// C:\workspace\Stella AI Studio\src\PluginCanvas.h

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "GuiLayout.h"
#include "KnobRenderer.h"

#include <functional>
#include <map>
#include <memory>

//==============================================================================
/**
    The plugin's GUI, drawn from its layout and played or reshaped right here.

    Play mode: the controls work like the finished plugin's: drag a knob or slider
    (Shift for fine moves), click a switch or a selector's position, drag in an XY pad,
    double-click for the default, use the mouse wheel. Meters, lamps and scopes move with
    the sound; envelope and filter curves follow their parameters.

    Design mode: click to select, drag to move (snapped), drag the corner handle to
    resize, Delete to remove, arrow keys to nudge (Shift: further). Dragging a group
    carries the elements inside it. The panel on the right edits the selected element.
    Double-clicking a knob opens its look in the Knob Studio.
*/
class PluginCanvas final : public juce::Component,
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

    void setDesignMode (bool shouldDesign);
    void setParameters (const juce::Array<Param>& newParams);

    /** What meters, lamps and scopes can watch: signals ("voices.out", "plugin.out L") and
        module displays ("lfo.position"). For the design panel's lists. */
    void setSources (const juce::StringArray& signals, const juce::StringArray& displays);

    /** Design mode: a new element of this type, for the first parameter not shown yet. */
    void addWidget (GuiWidget::Type type);

    /** The presets a preset widget steps through, and the one chosen. */
    void setPresets (const juce::StringArray& names, int current);

    /** The Knob Studio's edits: a look changed (or was added), or a knob got a look of its own. */
    void setKnobStyle (const juce::String& name, const KnobStyle& style);
    juce::String makeKnobUnique (int widgetIndex);

    //==========================================================================
    /** For Export: the GUI as images the exported plugin draws from. Everything that never
        moves is in the background; each control's moving part is a strip of frames (knobs
        with the same look and size share one). Meters, scopes, curves and XY dots are drawn
        live by the plugin itself. */
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
    std::function<void()> onLayoutEdited;                          // save it
    std::function<void (int widgetIndex)> onEditKnob;              // double-click on a knob in Design mode
    std::function<void (int presetIndex)> onPresetChosen;          // a preset widget was clicked in Play mode

    /** Live data, read about 30 times a second. */
    std::function<float (const juce::String& source, bool rms)> readLevel;
    std::function<void (const juce::String& source, float* destination, int numSamples)> readScope;
    std::function<void (const juce::StringArray& levels, const juce::StringArray& scopes)> onSourcesChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    //==========================================================================
    /** Design mode: edits the selected element. */
    class Inspector final : public juce::Component
    {
    public:
        explicit Inspector (PluginCanvas& owner);
        void show (const GuiWidget& widget);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void apply();
        void fillParams (juce::ComboBox& box, const juce::String& selectedId, bool withNone);

        PluginCanvas& canvas;
        juce::ComboBox type, param, source, mode, style;
        juce::ComboBox roles[4];
        juce::Label roleCaptions[4];
        juce::TextEditor label, options;
        juce::TextButton removeButton { "Delete" };
        juce::StringArray roleNames;
        bool updating = false;
    };

    void timerCallback() override;
    juce::Rectangle<float> panelArea() const;   // where the plugin window is drawn
    juce::Point<float> toPlugin (juce::Point<float> view) const;
    juce::Rectangle<float> toView (juce::Rectangle<int> plugin) const;
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
    juce::Rectangle<int> movingAreaOf (const GuiWidget& widget) const;
    KnobRenderer& rendererFor (const juce::String& style);
    void select (int index);
    void edited();
    void reportSources();
    juce::Rectangle<int> knobSquare (const GuiWidget& widget) const;

    GuiLayout layout;
    juce::Array<Param> params;
    std::map<int, float> values;                                   // by parameter index
    std::map<juce::String, std::unique_ptr<KnobRenderer>> renderers;
    juce::StringArray signalSources, displaySources;
    juce::StringArray presetNames;
    int currentPreset = -1;

    std::map<int, float> meterLevels;                              // by widget: shown level, with fall-back
    std::map<juce::String, float> frameLevels;                     // read once per frame per source
    juce::StringArray reportedLevels, reportedScopes;
    std::vector<float> scopeBuffer;

    bool design = true;
    int selected = -1, active = -1, hovered = -1;
    bool resizing = false, changed = false;
    juce::Point<float> dragStart;
    float startProportion = 0.0f;
    juce::Rectangle<int> startBounds;
    std::vector<std::pair<int, juce::Rectangle<int>>> carried;     // a group's contents, while it moves

    Inspector inspector { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCanvas)
};
