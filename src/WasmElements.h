// C:\workspace\Stella AI Studio\src\WasmElements.h

#pragma once

#include "stella_element_host.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

//==============================================================================
/**
    The project's programmed GUI elements (the elements folder's .cpp files, written against
    stella_element_api.h), running in a sandbox like the plugin itself: WebAssembly, run by
    Wasmtime, with no way out to the studio's memory, files or network, at most 256 MB, and
    a call that runs too long (an endless loop in paint, say) stopped.

    What the elements ask (parameter values, their options, the time) goes to the host set
    with setHost: the Edit UI canvas. Load it on any thread; then use it on the message thread.
*/
class WasmElements final
{
public:
    /** Compiles and starts the module. On failure returns nullptr and says why in error. */
    static std::unique_ptr<WasmElements> load (const std::vector<std::uint8_t>& wasm, std::string& error);
    ~WasmElements();

    struct Type
    {
        std::string name, description;
    };

    /** The element types in the module (STELLA_ELEMENT). */
    const std::vector<Type>& getTypes() const noexcept;
    bool hasType (const std::string& name) const;

    void setHost (stella::ui::ElementHost* host) noexcept;

    /** element: the canvas's own number for it (its index in the layout). See stella_elements.h. */
    bool create (int element, const std::string& type, int width, int height);
    void destroy (int element);
    void resize (int element, int width, int height);
    void invalidate (int element);
    bool needsPaint (int element);
    bool render (int element);

    /** What render drew, width x height premultiplied 0xAARRGGBB pixels (the element's size,
        or the popup's), or null. Valid until the next call. */
    const std::uint32_t* pixels (int element, bool popup, int width, int height);

    bool popupArea (int element, int& x, int& y, int& w, int& h);

    /** event: 0 down, 1 drag, 2 up, 3 move, 4 exit, 5 wheel; flags: 1 Shift, 2 double-click.
        For down: whether the element takes the drag. */
    bool mouse (int element, bool popup, int event, float x, float y, int flags, float notches);
    void closePopup (int element);

    /** After a crash or a stuck call, nothing runs any more; this says why. */
    bool hasFailed() const noexcept;
    const std::string& getFailure() const noexcept;

private:
    WasmElements();

    struct Impl;
    std::unique_ptr<Impl> impl;
};
