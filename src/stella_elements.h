// C:\workspace\Stella AI Studio\src\stella_elements.h
//
// The programmed elements' runtime (stella_elements.cpp), compiled with the elements
// themselves: into a WebAssembly module the studio runs, or into an exported plugin. One
// ElementRuntime serves one host; it creates the elements, draws them into pixel buffers
// and passes the mouse on.

#pragma once

#include "stella_element_api.h"
#include "stella_element_host.h"

#include <memory>

namespace stella::ui
{
    class ElementRuntime
    {
    public:
        explicit ElementRuntime (ElementHost& host);
        ~ElementRuntime();

        /** The element types compiled in (STELLA_ELEMENT), and what's wrong with them, if
            anything (two with one name): empty if nothing. */
        static int numTypes();
        static const char* typeName (int index);
        static const char* typeDescription (int index);
        static const char* problem();

        /** element: the host's own number for it. False if there's no such type. */
        bool create (int element, const char* type, int width, int height);
        void destroy (int element);
        void resize (int element, int width, int height);

        /** Something it reads from the layout changed: draw it again. */
        void invalidate (int element);

        /** Whether it looks different now: it was used, or a value it showed changed. */
        bool needsPaint (int element);

        /** Draws it (and its popup, if open) into its pixels. */
        bool render (int element);

        /** What render drew: premultiplied 0xAARRGGBB, top row first, the element's size
            (or the popup's). Null if there's nothing. */
        const std::uint32_t* pixels (int element, bool popup) const;

        /** The open popup, from the element's top-left; false if none is open. */
        bool popupArea (int element, int& x, int& y, int& w, int& h) const;

        enum Event { down = 0, drag = 1, up = 2, move = 3, exit = 4, wheel = 5 };

        /** The mouse, in the element's (or the popup's) pixels. For down: whether it takes
            the drag and the release. */
        bool mouse (int element, bool popup, int event, const Mouse& m, float notches);

        /** A click outside the popup: it closes, and the element hears popupClosed(). */
        void closePopup (int element);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
