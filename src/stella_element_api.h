// C:\workspace\Stella AI Studio\src\stella_element_api.h
//
// Programmed GUI elements: for a GUI element the toolbox doesn't have (a drop-down list,
// a step sequencer, a custom-drawn knob...), write a class that draws itself and answers
// the mouse, in elements/<Name>.cpp. The studio shows it live in Edit UI and Play, and
// exported plugins and apps draw it the same way, pixel for pixel.
//
// Use it in gui/layout.json as { "type": "custom", "element": "<Name>", "x", "y", "w", "h" }
// plus what it needs: "param" (its parameter), "params" (more, by role: { "x": "id" }),
// "label", "options" (a list of texts), "color", "source" (a signal or display to watch)
// and "settings" (anything else: { "rows": 4, "style": "round" }).
//
// Coordinates are pixels, floats, from the element's top-left corner (0, 0) to
// (width(), height()); pixel (x, y) covers x..x+1, y..y+1. Drawing is anti-aliased and
// clipped to the element (a popup to the popup). Colours are 0xAARRGGBB.
//
// Read values in paint() and the mouse functions, not in the constructor: the element
// knows its size and parameters only once it's placed (resized() is called first).

#pragma once

#include <cstdint>
#include <string>

namespace stella::ui
{
    //==========================================================================
    using Colour = std::uint32_t;   // 0xAARRGGBB: 0xff2080ff is solid blue

    constexpr Colour rgb (int r, int g, int b) noexcept
    {
        return 0xff000000u | ((std::uint32_t) (r & 255) << 16) | ((std::uint32_t) (g & 255) << 8) | (std::uint32_t) (b & 255);
    }

    Colour withAlpha (Colour c, float alpha) noexcept;    // alpha 0..1, times the colour's own
    Colour mix (Colour a, Colour b, float t) noexcept;    // t 0: a, 1: b
    Colour brighter (Colour c, float amount) noexcept;    // amount 0..1: towards white
    Colour darker (Colour c, float amount) noexcept;      // amount 0..1: towards black

    enum class Align { left, centre, right };

    //==========================================================================
    /** Where an element draws. Angles are in radians, 0 at 12 o'clock, going clockwise
        (a knob from 7 to 5 o'clock: -2.36 to 2.36). */
    class Canvas
    {
    public:
        int width() const noexcept    { return width_; }
        int height() const noexcept   { return height_; }

        void fillAll (Colour c);
        void fillRect (float x, float y, float w, float h, Colour c);
        void fillRoundedRect (float x, float y, float w, float h, float radius, Colour c);
        void fillEllipse (float x, float y, float w, float h, Colour c);
        void fillCircle (float cx, float cy, float radius, Colour c);
        void fillTriangle (float x0, float y0, float x1, float y1, float x2, float y2, Colour c);
        void fillPolygon (const float* xy, int numPoints, Colour c);           // xy: x0, y0, x1, y1, ...
        void fillPie (float cx, float cy, float radius, float from, float to, Colour c);

        /** Gradients: top to bottom in a box (radius > 0 rounds its corners), or from the
            middle of a circle out. */
        void fillGradient (float x, float y, float w, float h, Colour top, Colour bottom, float radius = 0.0f);
        void fillRadialGradient (float cx, float cy, float radius, Colour inside, Colour outside);

        /** Outlines of boxes and ellipses lie inside the box. Lines and arcs are centred on
            their path, with round ends (arcs: square ends unless roundEnds). */
        void drawRect (float x, float y, float w, float h, float thickness, Colour c);
        void drawRoundedRect (float x, float y, float w, float h, float radius, float thickness, Colour c);
        void drawEllipse (float x, float y, float w, float h, float thickness, Colour c);
        void drawLine (float x0, float y0, float x1, float y1, float thickness, Colour c);
        void drawPolyline (const float* xy, int numPoints, float thickness, Colour c);
        void drawArc (float cx, float cy, float radius, float from, float to, float thickness, Colour c, bool roundEnds = false);

        /** One line of text, centred up and down in the box and placed across it by align;
            too long, it ends in "...". size is the font's height in pixels (13 is usual). */
        void drawText (const std::string& text, float x, float y, float w, float h, float size, Colour c,
                       Align align = Align::centre, bool bold = false);
        float textWidth (const std::string& text, float size, bool bold = false);

        /** Drawing stays inside this box until resetClip() (a scrolling list, say). */
        void setClip (float x, float y, float w, float h);
        void resetClip();

    private:
        friend struct CanvasAccess;
        Canvas() = default;

        std::uint32_t* pixels_ = nullptr;
        int width_ = 0, height_ = 0;
        int clip_[4] {};
        void* painter_ = nullptr;
    };

    //==========================================================================
    struct Mouse
    {
        float x = 0.0f, y = 0.0f;    // in the element's pixels (on the popup: the popup's)
        bool shift = false;          // Shift or Ctrl held: fine moves
        bool doubleClick = false;
    };

    //==========================================================================
    /** A programmed element. Override paint(), and the mouse functions it needs. */
    class Element
    {
    public:
        virtual ~Element() = default;

        virtual void paint (Canvas& g) = 0;
        virtual void resized() {}                                   // width() / height() are new (also once at the start)

        /** The mouse on the element. Return true from mouseDown to get the drag and mouseUp. */
        virtual bool mouseDown (const Mouse&)                       { return false; }
        virtual void mouseDrag (const Mouse&)                       {}
        virtual void mouseUp (const Mouse&)                         {}
        virtual void mouseMove (const Mouse&)                       {}  // hovering, no button down
        virtual void mouseExit()                                    {}
        virtual void mouseWheel (const Mouse&, float /*notches*/)   {}  // + is up

        //======================================================================
        // A popup: a box drawn above the whole window (a drop-down's list, say), while it's
        // open. A click outside it closes it (and isn't passed on).
        void openPopup (float x, float y, float w, float h);       // in the element's pixels: x 0, y height() opens it just below
        void closePopup();
        bool isPopupOpen() const;

        virtual void paintPopup (Canvas&)                           {}  // the popup's own pixels: (0, 0) is its top-left
        virtual void popupMouseDown (const Mouse&)                  {}
        virtual void popupMouseDrag (const Mouse&)                  {}
        virtual void popupMouseUp (const Mouse&)                    {}
        virtual void popupMouseMove (const Mouse&)                  {}
        virtual void popupMouseExit()                               {}
        virtual void popupMouseWheel (const Mouse&, float /*notches*/) {}
        virtual void popupClosed()                                  {}  // closed by a click outside it

        //======================================================================
        int width() const;
        int height() const;

        /** Parameters. role "" is the element's own ("param" in the layout); others are
            named in its "params". Values are the parameter's own (e.g. Hz); normalised is
            0..1 along its range, as a knob shows it. */
        bool hasParam (const char* role = "") const;
        float value (const char* role = "") const;
        float normalised (const char* role = "") const;
        float minimum (const char* role = "") const;
        float maximum (const char* role = "") const;
        float defaultValue (const char* role = "") const;
        int choices (const char* role = "") const;                 // whole-number positions: maximum - minimum + 1
        std::string paramName (const char* role = "") const;
        std::string unit (const char* role = "") const;
        std::string valueText (const char* role = "") const;       // "440 Hz"

        /** Setting a value. Alone, each call is one undoable change for the host; for a drag,
            put the calls between beginEdit and endEdit (endEdit comes by itself on mouseUp). */
        void setValue (float newValue, const char* role = "");
        void setNormalised (float proportion, const char* role = "");
        void beginEdit (const char* role = "");
        void endEdit (const char* role = "");

        /** From the layout. */
        std::string label() const;
        int numOptions() const;
        std::string option (int index) const;
        Colour colour (Colour fallback) const;                      // "color", or fallback if none
        float setting (const char* key, float fallback) const;      // "settings": a number
        std::string settingText (const char* key, const std::string& fallback = {}) const;

        /** Live. Reading level() or seconds() in paint() redraws the element every frame. */
        float level (bool rms = false) const;                       // its "source": a peak 0..1+ (or RMS), or a display's 0..1
        void readScope (float* destination, int numSamples) const;  // its "source": the newest samples, oldest first
        double seconds() const;                                     // a clock, for animation
        void playNote (int note, float velocity);                   // MIDI note 0..127; velocity 0..1, 0 stops it
        bool isNoteDown (int note) const;                           // sounding, from anywhere

        /** Draw again (it's done by itself after the mouse, and when its values change). */
        void repaint();

    private:
        friend struct ElementAccess;
        void* instance_ = nullptr;
    };

    //==========================================================================
    struct Registrar
    {
        Registrar (const char* name, const char* description, Element* (*make)());
    };
}

/** Makes an element available, after its class: STELLA_ELEMENT (DropDown, "A drop-down list"). */
#define STELLA_ELEMENT(Type, Description) \
    static ::stella::ui::Registrar stellaElementRegistrar_##Type (#Type, Description, [] () -> ::stella::ui::Element* { return new Type(); });
