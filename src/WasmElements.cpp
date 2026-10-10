// C:\workspace\Stella AI Studio\src\WasmElements.cpp

#include "WasmElements.h"
#include "WasmPlugin.h"

#include <wasmtime.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    constexpr int apiVersion = 1;                         // must match stella_ui_api_version() in stella_elements.cpp
    constexpr std::int64_t memoryLimit = 256ll * 1024 * 1024;
    constexpr std::uint64_t startTicks = 300;             // 3 seconds to start up
    constexpr std::uint64_t callTicks = 40;               // 0.4 s for one call: far more than drawing needs

    std::string messageOf (wasmtime_error_t* error)
    {
        wasm_byte_vec_t text;
        wasmtime_error_message (error, &text);
        std::string message (text.data, text.size);
        wasm_byte_vec_delete (&text);
        wasmtime_error_delete (error);
        return message;
    }

    std::string messageOf (wasm_trap_t* trap)
    {
        wasm_byte_vec_t text;
        wasm_trap_message (trap, &text);
        std::string message (text.data, text.size);
        wasm_byte_vec_delete (&text);
        wasm_trap_delete (trap);

        while (! message.empty() && message.back() == 0)
            message.pop_back();

        return message;
    }

    std::string explain (const std::string& message)
    {
        if (message.find ("interrupt") != std::string::npos)
            return "a programmed element took too long (an endless loop?), so the elements were stopped";

        return "a programmed element crashed: " + message;
    }

    wasm_functype_t* functype (const std::vector<wasm_valkind_t>& params, const std::vector<wasm_valkind_t>& results)
    {
        std::vector<wasm_valtype_t*> p, r;

        for (const auto kind : params)  p.push_back (wasm_valtype_new (kind));
        for (const auto kind : results) r.push_back (wasm_valtype_new (kind));

        wasm_valtype_vec_t pv, rv;
        wasm_valtype_vec_new (&pv, p.size(), p.data());
        wasm_valtype_vec_new (&rv, r.size(), r.data());
        return wasm_functype_new (&pv, &rv);
    }

    /** The calling module's memory, checked: its numbers are never trusted blindly. */
    std::uint8_t* memoryAt (wasmtime_caller_t* caller, std::uint32_t offset, std::size_t bytes)
    {
        wasmtime_extern_t item;

        if (! wasmtime_caller_export_get (caller, "memory", 6, &item) || item.kind != WASMTIME_EXTERN_MEMORY)
            return nullptr;

        auto* context = wasmtime_caller_context (caller);
        auto* base = wasmtime_memory_data (context, &item.of.memory);
        const auto size = wasmtime_memory_data_size (context, &item.of.memory);

        if (base == nullptr || (std::size_t) offset > size || bytes > size - (std::size_t) offset)
            return nullptr;

        return base + offset;
    }

    std::int32_t i32 (const wasmtime_val_t* args, size_t i)   { return args[i].kind == WASMTIME_I32 ? args[i].of.i32 : 0; }
    float f32 (const wasmtime_val_t* args, size_t i)          { return args[i].kind == WASMTIME_F32 ? args[i].of.f32 : 0.0f; }

    void setI32 (wasmtime_val_t* results, std::int32_t v)     { results[0].kind = WASMTIME_I32; results[0].of.i32 = v; }
    void setF32 (wasmtime_val_t* results, float v)            { results[0].kind = WASMTIME_F32; results[0].of.f32 = v; }
}

//==============================================================================
struct WasmElements::Impl
{
    ~Impl()
    {
        if (store != nullptr)
            wasmtime_store_delete (store);
    }

    bool findFunc (const char* name, wasmtime_func_t& func)
    {
        wasmtime_extern_t item;

        if (! wasmtime_instance_export_get (context, &instance, name, std::strlen (name), &item) || item.kind != WASMTIME_EXTERN_FUNC)
            return false;

        func = item.of.func;
        return true;
    }

    /** One call into the module, with a deadline. Any trouble stops the elements for good. */
    bool call (const wasmtime_func_t& func, std::initializer_list<wasmtime_val_t> args, wasmtime_val_t* result, std::uint64_t ticks = callTicks)
    {
        if (failed)
            return false;

        wasmtime_context_set_epoch_deadline (context, ticks);

        wasm_trap_t* trap = nullptr;
        auto* problem = wasmtime_func_call (context, &func, args.begin(), args.size(), result, result != nullptr ? 1 : 0, &trap);

        if (problem != nullptr || trap != nullptr)
        {
            failure = explain (problem != nullptr ? messageOf (problem) : messageOf (trap));
            failed = true;
            return false;
        }

        return true;
    }

    std::int32_t callInt (const wasmtime_func_t& func, std::initializer_list<wasmtime_val_t> args, std::int32_t fallback = 0)
    {
        wasmtime_val_t result {};
        return call (func, args, &result) && result.kind == WASMTIME_I32 ? result.of.i32 : fallback;
    }

    std::uint8_t* at (std::uint32_t offset, std::size_t bytes) const noexcept
    {
        auto* base = wasmtime_memory_data (context, &memory);
        const auto size = wasmtime_memory_data_size (context, &memory);

        if (base == nullptr || (std::size_t) offset > size || bytes > size - (std::size_t) offset)
            return nullptr;

        return base + offset;
    }

    std::string stringAt (std::uint32_t offset) const
    {
        std::string text;

        for (std::size_t i = 0; i < 4096; ++i)
        {
            const auto* c = at (offset + (std::uint32_t) i, 1);

            if (c == nullptr || *c == 0)
                break;

            text += (char) *c;
        }

        return text;
    }

    /** Text into the module's scratch buffer; returns where it is (0: it couldn't). */
    std::uint32_t put (const std::string& text)
    {
        const auto where = (std::uint32_t) callInt (scratchFunc, { val ((std::int32_t) text.size()) });
        auto* target = where != 0 ? at (where, text.size() + 1) : nullptr;

        if (target == nullptr)
            return 0;

        std::memcpy (target, text.data(), text.size());
        target[text.size()] = 0;
        return where;
    }

    static wasmtime_val_t val (std::int32_t v)   { wasmtime_val_t x {}; x.kind = WASMTIME_I32; x.of.i32 = v; return x; }
    static wasmtime_val_t val (float v)          { wasmtime_val_t x {}; x.kind = WASMTIME_F32; x.of.f32 = v; return x; }

    wasmtime_store_t* store = nullptr;
    wasmtime_context_t* context = nullptr;
    wasmtime_instance_t instance {};
    wasmtime_memory_t memory {};

    wasmtime_func_t scratchFunc {}, createFunc {}, destroyFunc {}, resizeFunc {}, invalidateFunc {}, needsPaintFunc {},
                    renderFunc {}, pixelsFunc {}, popupFunc {}, mouseFunc {}, closePopupFunc {};

    stella::ui::ElementHost* host = nullptr;
    std::vector<Type> types;
    std::string failure;
    bool failed = false;

    //==========================================================================
    // What the elements ask, through the module's imports.
    static Impl& of (void* env)   { return *static_cast<Impl*> (env); }

    static wasm_trap_t* paramInfo (void* env, wasmtime_caller_t*, const wasmtime_val_t* args, size_t, wasmtime_val_t* results, size_t)
    {
        auto& d = of (env);
        float value = std::numeric_limits<float>::quiet_NaN();

        if (d.host != nullptr && ! d.host->paramInfo (i32 (args, 0), i32 (args, 1), i32 (args, 2), value))
            value = std::numeric_limits<float>::quiet_NaN();

        setF32 (results, value);
        return nullptr;
    }

    static wasm_trap_t* findSlot (void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args, size_t, wasmtime_val_t* results, size_t)
    {
        auto& d = of (env);
        const auto length = (std::size_t) std::max (0, std::min (1024, i32 (args, 2)));
        const auto* text = memoryAt (caller, (std::uint32_t) i32 (args, 1), length);
        setI32 (results, d.host != nullptr && text != nullptr ? d.host->findSlot (i32 (args, 0), std::string ((const char*) text, length)) : -1);
        return nullptr;
    }

    static wasm_trap_t* setValue (void* env, wasmtime_caller_t*, const wasmtime_val_t* args, size_t, wasmtime_val_t*, size_t)
    {
        if (auto* host = of (env).host; host != nullptr && std::isfinite (f32 (args, 2)))
            host->setValue (i32 (args, 0), i32 (args, 1), f32 (args, 2));

        return nullptr;
    }

    static wasm_trap_t* gesture (void* env, wasmtime_caller_t*, const wasmtime_val_t* args, size_t, wasmtime_val_t*, size_t)
    {
        if (auto* host = of (env).host)
            host->gesture (i32 (args, 0), i32 (args, 1), i32 (args, 2) != 0);

        return nullptr;
    }

    static wasm_trap_t* text (void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args, size_t, wasmtime_val_t* results, size_t)
    {
        auto& d = of (env);
        const auto keyLength = (std::size_t) std::max (0, std::min (1024, i32 (args, 4)));
        const auto* key = memoryAt (caller, (std::uint32_t) i32 (args, 3), keyLength);
        std::string result;

        if (d.host == nullptr || key == nullptr
            || ! d.host->text (i32 (args, 0), i32 (args, 1), i32 (args, 2), std::string ((const char*) key, keyLength), result))
        {
            setI32 (results, -1);
            return nullptr;
        }

        const auto capacity = (std::size_t) std::max (0, i32 (args, 6));
        const auto count = std::min (capacity, result.size());

        if (auto* target = memoryAt (caller, (std::uint32_t) i32 (args, 5), count); target != nullptr)
            std::memcpy (target, result.data(), count);

        setI32 (results, (std::int32_t) std::min<std::size_t> (result.size(), 1 << 20));
        return nullptr;
    }

    static wasm_trap_t* numOptions (void* env, wasmtime_caller_t*, const wasmtime_val_t* args, size_t, wasmtime_val_t* results, size_t)
    {
        auto* host = of (env).host;
        setI32 (results, host != nullptr ? host->numOptions (i32 (args, 0)) : 0);
        return nullptr;
    }

    static wasm_trap_t* colour (void* env, wasmtime_caller_t*, const wasmtime_val_t* args, size_t, wasmtime_val_t* results, size_t)
    {
        auto* host = of (env).host;
        setI32 (results, host != nullptr ? (std::int32_t) host->colour (i32 (args, 0)) : 0);
        return nullptr;
    }

    static wasm_trap_t* level (void* env, wasmtime_caller_t*, const wasmtime_val_t* args, size_t, wasmtime_val_t* results, size_t)
    {
        auto* host = of (env).host;
        setF32 (results, host != nullptr ? host->level (i32 (args, 0), i32 (args, 1) != 0) : 0.0f);
        return nullptr;
    }

    static wasm_trap_t* scope (void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args, size_t, wasmtime_val_t*, size_t)
    {
        auto* host = of (env).host;
        const auto count = std::max (0, std::min (1 << 16, i32 (args, 2)));
        auto* target = memoryAt (caller, (std::uint32_t) i32 (args, 1), sizeof (float) * (std::size_t) count);

        if (target == nullptr || count == 0)
            return nullptr;

        std::vector<float> samples ((std::size_t) count, 0.0f);

        if (host != nullptr)
            host->readScope (i32 (args, 0), samples.data(), count);

        std::memcpy (target, samples.data(), sizeof (float) * (std::size_t) count);
        return nullptr;
    }

    static wasm_trap_t* note (void* env, wasmtime_caller_t*, const wasmtime_val_t* args, size_t, wasmtime_val_t*, size_t)
    {
        if (auto* host = of (env).host)
            host->playNote (i32 (args, 0), i32 (args, 1), std::isfinite (f32 (args, 2)) ? f32 (args, 2) : 0.0f);

        return nullptr;
    }

    static wasm_trap_t* noteDown (void* env, wasmtime_caller_t*, const wasmtime_val_t* args, size_t, wasmtime_val_t* results, size_t)
    {
        auto* host = of (env).host;
        setI32 (results, host != nullptr && host->isNoteDown (i32 (args, 0)) ? 1 : 0);
        return nullptr;
    }

    static wasm_trap_t* seconds (void* env, wasmtime_caller_t*, const wasmtime_val_t*, size_t, wasmtime_val_t* results, size_t)
    {
        auto* host = of (env).host;
        results[0].kind = WASMTIME_F64;
        results[0].of.f64 = host != nullptr ? host->seconds() : 0.0;
        return nullptr;
    }
};

//==============================================================================
WasmElements::WasmElements()
    : impl (std::make_unique<Impl>())
{
}

WasmElements::~WasmElements() = default;

std::unique_ptr<WasmElements> WasmElements::load (const std::vector<std::uint8_t>& wasm, std::string& error)
{
    auto* engine = WasmPlugin::engine();
    wasmtime_module_t* module = nullptr;

    if (auto* failure = wasmtime_module_new (engine, wasm.data(), wasm.size(), &module))
    {
        error = "The programmed elements couldn't be loaded: " + messageOf (failure);
        return nullptr;
    }

    std::unique_ptr<WasmElements> elements (new WasmElements());
    auto& d = *elements->impl;

    d.store = wasmtime_store_new (engine, nullptr, nullptr);
    d.context = wasmtime_store_context (d.store);
    wasmtime_store_limiter (d.store, memoryLimit, -1, -1, -1, -1);

    if (auto* failure = wasmtime_context_set_wasi (d.context, wasi_config_new()))
    {
        wasmtime_module_delete (module);
        error = "The programmed elements' sandbox couldn't be set up: " + messageOf (failure);
        return nullptr;
    }

    auto* linker = wasmtime_linker_new (engine);
    auto* failure = wasmtime_linker_define_wasi (linker);

    using I = std::vector<wasm_valkind_t>;

    struct Import
    {
        const char* name;
        I params, results;
        wasmtime_func_callback_t callback;
    };

    const Import imports[]
    {
        { "param_info",  I { WASM_I32, WASM_I32, WASM_I32 }, I { WASM_F32 }, &Impl::paramInfo },
        { "find_slot",   I { WASM_I32, WASM_I32, WASM_I32 }, I { WASM_I32 }, &Impl::findSlot },
        { "set_value",   I { WASM_I32, WASM_I32, WASM_F32 }, I {},           &Impl::setValue },
        { "gesture",     I { WASM_I32, WASM_I32, WASM_I32 }, I {},           &Impl::gesture },
        { "text",        I { WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32 }, I { WASM_I32 }, &Impl::text },
        { "num_options", I { WASM_I32 },                     I { WASM_I32 }, &Impl::numOptions },
        { "colour",      I { WASM_I32 },                     I { WASM_I32 }, &Impl::colour },
        { "level",       I { WASM_I32, WASM_I32 },           I { WASM_F32 }, &Impl::level },
        { "scope",       I { WASM_I32, WASM_I32, WASM_I32 }, I {},           &Impl::scope },
        { "note",        I { WASM_I32, WASM_I32, WASM_F32 }, I {},           &Impl::note },
        { "note_down",   I { WASM_I32 },                     I { WASM_I32 }, &Impl::noteDown },
        { "seconds",     I {},                               I { WASM_F64 }, &Impl::seconds },
    };

    for (const auto& import : imports)
    {
        if (failure != nullptr)
            break;

        auto* type = functype (import.params, import.results);
        failure = wasmtime_linker_define_func (linker, "stella", 6, import.name, std::strlen (import.name), type, import.callback, &d, nullptr);
        wasm_functype_delete (type);
    }

    wasm_trap_t* trap = nullptr;
    wasmtime_context_set_epoch_deadline (d.context, startTicks);

    if (failure == nullptr)
        failure = wasmtime_linker_instantiate (linker, d.context, module, &d.instance, &trap);

    wasmtime_linker_delete (linker);
    wasmtime_module_delete (module);

    if (failure != nullptr || trap != nullptr)
    {
        error = "The programmed elements couldn't start: " + (failure != nullptr ? messageOf (failure) : messageOf (trap));
        return nullptr;
    }

    wasmtime_extern_t memoryExport;

    if (! wasmtime_instance_export_get (d.context, &d.instance, "memory", 6, &memoryExport) || memoryExport.kind != WASMTIME_EXTERN_MEMORY)
    {
        error = "The programmed elements weren't built by Stella (no memory).";
        return nullptr;
    }

    d.memory = memoryExport.of.memory;

    wasmtime_func_t initialize {}, versionFunc {}, countFunc {}, nameFunc {}, descriptionFunc {}, problemFunc {};

    if (! d.findFunc ("stella_ui_api_version", versionFunc) || ! d.findFunc ("stella_ui_type_count", countFunc)
        || ! d.findFunc ("stella_ui_type_name", nameFunc) || ! d.findFunc ("stella_ui_type_description", descriptionFunc)
        || ! d.findFunc ("stella_ui_problem", problemFunc) || ! d.findFunc ("stella_ui_scratch", d.scratchFunc)
        || ! d.findFunc ("stella_ui_create", d.createFunc) || ! d.findFunc ("stella_ui_destroy", d.destroyFunc)
        || ! d.findFunc ("stella_ui_resize", d.resizeFunc) || ! d.findFunc ("stella_ui_invalidate", d.invalidateFunc)
        || ! d.findFunc ("stella_ui_needs_paint", d.needsPaintFunc) || ! d.findFunc ("stella_ui_render", d.renderFunc)
        || ! d.findFunc ("stella_ui_pixels", d.pixelsFunc) || ! d.findFunc ("stella_ui_popup", d.popupFunc)
        || ! d.findFunc ("stella_ui_mouse", d.mouseFunc) || ! d.findFunc ("stella_ui_close_popup", d.closePopupFunc))
    {
        error = "The programmed elements weren't built by Stella (their runtime is missing).";
        return nullptr;
    }

    // Static constructors run here: each element registers its type.
    if (d.findFunc ("_initialize", initialize) && ! d.call (initialize, {}, nullptr, startTicks))
    {
        error = "The programmed elements failed while starting: " + d.failure;
        return nullptr;
    }

    if (d.callInt (versionFunc, {}) != apiVersion)
    {
        error = "The programmed elements were built for a different version of Stella.";
        return nullptr;
    }

    if (const auto problem = d.stringAt ((std::uint32_t) d.callInt (problemFunc, {})); ! problem.empty())
    {
        error = problem;
        return nullptr;
    }

    const auto count = std::min (256, d.callInt (countFunc, {}));

    for (int i = 0; i < count; ++i)
        d.types.push_back ({ d.stringAt ((std::uint32_t) d.callInt (nameFunc, { Impl::val (i) })),
                             d.stringAt ((std::uint32_t) d.callInt (descriptionFunc, { Impl::val (i) })) });

    if (d.failed)
    {
        error = "The programmed elements failed while starting: " + d.failure;
        return nullptr;
    }

    return elements;
}

//==============================================================================
const std::vector<WasmElements::Type>& WasmElements::getTypes() const noexcept   { return impl->types; }
void WasmElements::setHost (stella::ui::ElementHost* host) noexcept              { impl->host = host; }
bool WasmElements::hasFailed() const noexcept                                    { return impl->failed; }
const std::string& WasmElements::getFailure() const noexcept                     { return impl->failure; }

bool WasmElements::hasType (const std::string& name) const
{
    return std::any_of (impl->types.begin(), impl->types.end(), [&name] (const Type& t) { return t.name == name; });
}

bool WasmElements::create (int element, const std::string& type, int width, int height)
{
    auto& d = *impl;
    const auto where = d.put (type);

    if (where == 0)
        return false;

    return d.callInt (d.createFunc, { Impl::val (element), Impl::val ((std::int32_t) where), Impl::val ((std::int32_t) type.size()),
                                      Impl::val (width), Impl::val (height) }) == 1;
}

void WasmElements::destroy (int element)
{
    impl->call (impl->destroyFunc, { Impl::val (element) }, nullptr);
}

void WasmElements::resize (int element, int width, int height)
{
    impl->call (impl->resizeFunc, { Impl::val (element), Impl::val (width), Impl::val (height) }, nullptr);
}

void WasmElements::invalidate (int element)
{
    impl->call (impl->invalidateFunc, { Impl::val (element) }, nullptr);
}

bool WasmElements::needsPaint (int element)
{
    return impl->callInt (impl->needsPaintFunc, { Impl::val (element) }) != 0;
}

bool WasmElements::render (int element)
{
    return impl->callInt (impl->renderFunc, { Impl::val (element) }) == 1;
}

const std::uint32_t* WasmElements::pixels (int element, bool popup, int width, int height)
{
    if (width <= 0 || height <= 0)
        return nullptr;

    const auto where = (std::uint32_t) impl->callInt (impl->pixelsFunc, { Impl::val (element), Impl::val (popup ? 1 : 0) });

    if (where == 0 || (where & 3u) != 0)
        return nullptr;

    return reinterpret_cast<const std::uint32_t*> (impl->at (where, sizeof (std::uint32_t) * (std::size_t) width * (std::size_t) height));
}

bool WasmElements::popupArea (int element, int& x, int& y, int& w, int& h)
{
    auto& d = *impl;

    if (d.callInt (d.popupFunc, { Impl::val (element), Impl::val (0) }) != 1)
        return false;

    x = d.callInt (d.popupFunc, { Impl::val (element), Impl::val (1) });
    y = d.callInt (d.popupFunc, { Impl::val (element), Impl::val (2) });
    w = d.callInt (d.popupFunc, { Impl::val (element), Impl::val (3) });
    h = d.callInt (d.popupFunc, { Impl::val (element), Impl::val (4) });
    return w > 0 && h > 0 && ! d.failed;
}

bool WasmElements::mouse (int element, bool popup, int event, float x, float y, int flags, float notches)
{
    return impl->callInt (impl->mouseFunc, { Impl::val (element), Impl::val (popup ? 1 : 0), Impl::val (event), Impl::val (x), Impl::val (y),
                                             Impl::val (flags), Impl::val (notches) }) != 0;
}

void WasmElements::closePopup (int element)
{
    impl->call (impl->closePopupFunc, { Impl::val (element) }, nullptr);
}
