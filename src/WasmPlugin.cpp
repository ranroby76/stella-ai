// C:\workspace\Stella AI Studio\src\WasmPlugin.cpp

#include "WasmPlugin.h"

#include <wasmtime.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace
{
    constexpr bool inRange (int value, int upper) noexcept    { return value >= 0 && value < upper; }

    //==========================================================================
    // The studio's own view of the runtime's Shared block (stella_runtime.cpp).
    // In WebAssembly pointers are 4 bytes, so this layout matches it exactly.
    constexpr int maxParams = 1024;
    constexpr int maxEvents = 512;
    constexpr int maxNodes    = 256;
    constexpr int maxTaps     = 32;
    constexpr int maxScopes   = 4;
    constexpr int maxDisplays = 256;
    constexpr int scopeSize   = 8192;   // the studio keeps this many samples per scope

    struct SharedTap
    {
        std::int32_t node, port;
        float peak, rms;
    };

    struct SharedEvent
    {
        std::uint32_t frame;
        std::uint8_t status, data1, data2, unused;
    };

    struct SharedBlock
    {
        std::uint32_t maxFrames;
        std::uint32_t numEvents;
        std::uint32_t numParams;
        std::uint32_t numNodes;
        std::uint32_t in[2];
        std::uint32_t out[2];
        std::uint32_t numTaps;
        std::uint32_t numDisplays;
        std::uint32_t scopeFrames;
        std::uint32_t reserved;
        SharedTap taps[maxTaps];
        std::int32_t scopeNode[maxScopes];
        std::int32_t scopePort[maxScopes];
        std::uint32_t scope[maxScopes];
        float displays[maxDisplays];
        std::uint8_t bypass[maxNodes];
        float params[maxParams];
        SharedEvent events[maxEvents];
    };

    static_assert (sizeof (SharedEvent) == 8, "layout must match the runtime");
    static_assert (sizeof (SharedTap) == 16, "layout must match the runtime");
    static_assert (sizeof (SharedBlock) == 48 + 16 * maxTaps + 12 * maxScopes + 4 * maxDisplays + maxNodes + 4 * maxParams + 8 * maxEvents,
                   "layout must match the runtime");

    constexpr int apiVersion = 3;   // must match stella_api_version() in stella_runtime.cpp

    constexpr std::int64_t memoryLimit = 512ll * 1024 * 1024;
    constexpr std::uint64_t startTicks = 300;   // 3 seconds to start up
    constexpr std::uint64_t blockTicks = 5;     // 40-50 ms for one block, far more than it needs

    //==========================================================================
    /** One Wasmtime engine for the whole studio, with a clock that lets a stuck plugin be
        stopped: every call gets a deadline counted in ticks of this clock. */
    struct Engine
    {
        Engine()
        {
            auto* config = wasm_config_new();
            wasmtime_config_epoch_interruption_set (config, true);
            wasmtime_config_cranelift_opt_level_set (config, WASMTIME_OPT_LEVEL_SPEED);
            engine = wasm_engine_new_with_config (config);

            ticker = std::thread ([this]
            {
                while (running.load())
                {
                    std::this_thread::sleep_for (std::chrono::milliseconds (10));
                    wasmtime_engine_increment_epoch (engine);
                }
            });
        }

        ~Engine()
        {
            running.store (false);

            if (ticker.joinable())
                ticker.join();

            wasm_engine_delete (engine);
        }

        static wasm_engine_t* get()
        {
            static Engine instance;
            return instance.engine;
        }

        wasm_engine_t* engine = nullptr;
        std::atomic<bool> running { true };
        std::thread ticker;
    };

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
        // Wasmtime's wording for a call that ran past its deadline.
        if (message.find ("interrupt") != std::string::npos)
            return "The plugin took too long to process audio (an endless loop?), so it was stopped.";

        return message;
    }
}

//==============================================================================
struct WasmPlugin::Impl
{
    ~Impl()
    {
        if (store != nullptr)
            wasmtime_store_delete (store);
    }

    bool call (const wasmtime_func_t& func, const wasmtime_val_t* args, size_t numArgs,
               wasmtime_val_t* results, size_t numResults, std::string* error)
    {
        wasm_trap_t* trap = nullptr;
        auto* problem = wasmtime_func_call (context, &func, args, numArgs, results, numResults, &trap);

        if (problem != nullptr)
        {
            const auto message = messageOf (problem);
            if (error != nullptr) *error = explain (message);
            return false;
        }

        if (trap != nullptr)
        {
            const auto message = messageOf (trap);
            if (error != nullptr) *error = explain (message);
            return false;
        }

        return true;
    }

    bool findFunc (const char* name, wasmtime_func_t& func)
    {
        wasmtime_extern_t item;

        if (! wasmtime_instance_export_get (context, &instance, name, std::strlen (name), &item))
            return false;

        if (item.kind != WASMTIME_EXTERN_FUNC)
            return false;

        func = item.of.func;
        return true;
    }

    /** The plugin's memory, checked: the plugin's own numbers are never trusted blindly. */
    std::uint8_t* at (std::uint32_t offset, std::size_t bytes) const noexcept
    {
        auto* base = wasmtime_memory_data (context, &memory);
        const auto size = wasmtime_memory_data_size (context, &memory);

        if (base == nullptr || (std::size_t) offset > size || bytes > size - (std::size_t) offset)
            return nullptr;

        return base + offset;
    }

    void fail (const std::string& message) noexcept
    {
        if (failed.load())
            return;

        failure = message;            // written once, before failed is published
        failed.store (true, std::memory_order_release);
    }

    wasmtime_store_t* store = nullptr;
    wasmtime_context_t* context = nullptr;
    wasmtime_instance_t instance {};
    wasmtime_memory_t memory {};
    wasmtime_func_t processFunc {};

    std::uint32_t shared = 0;
    int maxFrames = 0;
    int numParams = 0;
    double sampleRate = 48000.0;

    std::unique_ptr<std::atomic<float>[]> values;
    std::string description, failure;
    std::atomic<bool> failed { false };

    std::atomic<std::uint8_t> bypass[maxNodes] {};

    // Meters: what to watch (set from the message thread), and the levels the audio thread found.
    std::atomic<int> numTaps { 0 };
    std::atomic<int> tapNode[maxTaps] {}, tapPort[maxTaps] {};
    std::atomic<float> tapPeak[maxTaps] {}, tapRms[maxTaps] {};

    // Scopes: what each slot watches, and its ring of samples (audio thread writes, GUI reads).
    std::atomic<int> scopeNode[maxScopes] { WasmPlugin::off, WasmPlugin::off, WasmPlugin::off, WasmPlugin::off };
    std::atomic<int> scopePort[maxScopes] {};
    std::unique_ptr<float[]> scopeRing[maxScopes] { std::make_unique<float[]> ((size_t) scopeSize), std::make_unique<float[]> ((size_t) scopeSize),
                                                    std::make_unique<float[]> ((size_t) scopeSize), std::make_unique<float[]> ((size_t) scopeSize) };
    std::atomic<std::uint32_t> scopeWrite[maxScopes] {};

    int numDisplays = 0;
    std::atomic<float> displayValues[maxDisplays] {};
};

//==============================================================================
WasmPlugin::WasmPlugin()
    : impl (std::make_unique<Impl>())
{
}

WasmPlugin::~WasmPlugin() = default;

wasm_engine_t* WasmPlugin::engine()
{
    return Engine::get();
}

std::unique_ptr<WasmPlugin> WasmPlugin::load (const std::vector<std::uint8_t>& wasm, double sampleRate,
                                              int maxFrames, std::string& error)
{
    auto* engine = Engine::get();

    wasmtime_module_t* module = nullptr;

    if (auto* failure = wasmtime_module_new (engine, wasm.data(), wasm.size(), &module))
    {
        error = "The plugin couldn't be loaded: " + messageOf (failure);
        return nullptr;
    }

    std::unique_ptr<WasmPlugin> plugin (new WasmPlugin());
    auto& d = *plugin->impl;

    d.store = wasmtime_store_new (engine, nullptr, nullptr);
    d.context = wasmtime_store_context (d.store);
    d.sampleRate = sampleRate;

    // The sandbox: a memory ceiling, and WASI with no files, network or environment.
    wasmtime_store_limiter (d.store, memoryLimit, -1, -1, -1, -1);

    if (auto* failure = wasmtime_context_set_wasi (d.context, wasi_config_new()))
    {
        wasmtime_module_delete (module);
        error = "The plugin's sandbox couldn't be set up: " + messageOf (failure);
        return nullptr;
    }

    auto* linker = wasmtime_linker_new (engine);
    auto* failure = wasmtime_linker_define_wasi (linker);
    wasm_trap_t* trap = nullptr;

    wasmtime_context_set_epoch_deadline (d.context, startTicks);

    if (failure == nullptr)
        failure = wasmtime_linker_instantiate (linker, d.context, module, &d.instance, &trap);

    wasmtime_linker_delete (linker);
    wasmtime_module_delete (module);

    if (failure != nullptr || trap != nullptr)
    {
        error = "The plugin couldn't start: " + (failure != nullptr ? messageOf (failure) : messageOf (trap));
        return nullptr;
    }

    wasmtime_extern_t memoryExport;

    if (! wasmtime_instance_export_get (d.context, &d.instance, "memory", 6, &memoryExport)
        || memoryExport.kind != WASMTIME_EXTERN_MEMORY)
    {
        error = "The plugin wasn't built by Stella (it has no memory).";
        return nullptr;
    }

    d.memory = memoryExport.of.memory;

    wasmtime_func_t initialize {}, apiVersionFunc {}, create {}, sharedFunc {}, describe {};

    if (! d.findFunc ("stella_api_version", apiVersionFunc) || ! d.findFunc ("stella_create", create)
        || ! d.findFunc ("stella_shared", sharedFunc) || ! d.findFunc ("stella_describe", describe)
        || ! d.findFunc ("stella_process", d.processFunc))
    {
        error = "The plugin wasn't built by Stella (its runtime is missing).";
        return nullptr;
    }

    // Static constructors (each module registering its type) run here.
    if (d.findFunc ("_initialize", initialize) && ! d.call (initialize, nullptr, 0, nullptr, 0, &error))
    {
        error = "The plugin failed while starting: " + error;
        return nullptr;
    }

    wasmtime_val_t result {};

    if (! d.call (apiVersionFunc, nullptr, 0, &result, 1, &error) || result.kind != WASMTIME_I32 || result.of.i32 != apiVersion)
    {
        error = "The plugin was built for a different version of Stella.";
        return nullptr;
    }

    wasmtime_val_t args[2];
    args[0].kind = WASMTIME_F32;  args[0].of.f32 = (float) sampleRate;
    args[1].kind = WASMTIME_I32;  args[1].of.i32 = maxFrames;

    const bool created = d.call (create, args, 2, &result, 1, &error);

    // The description says what the plugin holds, or what's wrong with it.
    wasmtime_val_t text {};

    if (d.call (describe, nullptr, 0, &text, 1, nullptr) && text.kind == WASMTIME_I32)
    {
        const auto offset = (std::uint32_t) text.of.i32;

        for (std::size_t length = 0; length < (1u << 20); ++length)
        {
            const auto* c = d.at (offset + (std::uint32_t) length, 1);

            if (c == nullptr || *c == 0)
                break;

            d.description += (char) *c;
        }
    }

    if (! created)
    {
        error = "The plugin failed while starting: " + error;
        return nullptr;
    }

    if (result.kind != WASMTIME_I32 || result.of.i32 != 1)
    {
        // The runtime's own message sits in the description's "error" field.
        const auto key = std::string ("\"error\":\"");
        const auto start = d.description.find (key);
        std::string message;

        if (start != std::string::npos)
        {
            for (auto i = start + key.size(); i < d.description.size() && d.description[i] != '"'; ++i)
            {
                if (d.description[i] == '\\' && i + 1 < d.description.size())
                    ++i;

                message += d.description[i];
            }
        }

        error = message.empty() ? std::string ("The plugin couldn't set itself up.") : message;
        return nullptr;
    }

    if (! d.call (sharedFunc, nullptr, 0, &result, 1, &error) || result.kind != WASMTIME_I32)
    {
        error = "The plugin failed while starting: " + error;
        return nullptr;
    }

    d.shared = (std::uint32_t) result.of.i32;
    const auto* block = reinterpret_cast<const SharedBlock*> (d.at (d.shared, sizeof (SharedBlock)));

    if (block == nullptr || block->maxFrames == 0 || block->numParams > (std::uint32_t) maxParams)
    {
        error = "The plugin wasn't built by Stella (its shared block is wrong).";
        return nullptr;
    }

    d.maxFrames = (int) block->maxFrames;
    d.numParams = (int) block->numParams;
    d.numDisplays = (int) std::min<std::uint32_t> (block->numDisplays, (std::uint32_t) maxDisplays);
    d.values = std::make_unique<std::atomic<float>[]> ((size_t) std::max (1, d.numParams));

    for (int i = 0; i < d.numParams; ++i)
        d.values[(size_t) i].store (block->params[i]);   // the defaults

    return plugin;
}

//==============================================================================
bool WasmPlugin::process (const float* inL, const float* inR, float* outL, float* outR, int numFrames,
                          const MidiEvent* events, int numEvents) noexcept
{
    auto& d = *impl;

    if (d.failed.load (std::memory_order_acquire))
    {
        if (outL != nullptr) std::memset (outL, 0, sizeof (float) * (size_t) numFrames);
        if (outR != nullptr) std::memset (outR, 0, sizeof (float) * (size_t) numFrames);
        return false;
    }

    int eventIndex = 0;

    for (int start = 0; start < numFrames; start += d.maxFrames)
    {
        const int frames = std::min (d.maxFrames, numFrames - start);

        // Memory can move when the plugin grows it, so it's looked up for each block.
        auto* block = reinterpret_cast<SharedBlock*> (d.at (d.shared, sizeof (SharedBlock)));

        if (block == nullptr)
        {
            d.fail ("The plugin damaged its own memory.");
            return process (inL, inR, outL, outR, numFrames, nullptr, 0);
        }

        const float* inputs[2] { inL, inR };

        for (int c = 0; c < 2; ++c)
        {
            if (auto* target = reinterpret_cast<float*> (d.at (block->in[c], sizeof (float) * (size_t) frames)))
            {
                if (inputs[c] != nullptr)
                    std::memcpy (target, inputs[c] + start, sizeof (float) * (size_t) frames);
                else
                    std::memset (target, 0, sizeof (float) * (size_t) frames);
            }
        }

        for (int i = 0; i < d.numParams; ++i)
            block->params[i] = d.values[(size_t) i].load (std::memory_order_relaxed);

        for (int i = 0; i < maxNodes; ++i)
            block->bypass[i] = d.bypass[i].load (std::memory_order_relaxed);

        const auto taps = std::min (d.numTaps.load (std::memory_order_relaxed), maxTaps);
        block->numTaps = (std::uint32_t) taps;

        for (int t = 0; t < taps; ++t)
        {
            block->taps[t].node = d.tapNode[t].load (std::memory_order_relaxed);
            block->taps[t].port = d.tapPort[t].load (std::memory_order_relaxed);
        }

        for (int s = 0; s < maxScopes; ++s)
        {
            block->scopeNode[s] = d.scopeNode[s].load (std::memory_order_relaxed);
            block->scopePort[s] = d.scopePort[s].load (std::memory_order_relaxed);
        }

        std::uint32_t count = 0;

        while (eventIndex < numEvents && (int) events[eventIndex].frame < start + frames && count < (std::uint32_t) maxEvents)
        {
            const auto& e = events[eventIndex++];
            block->events[count++] = { (std::uint32_t) std::max (0, (int) e.frame - start), e.status, e.data1, e.data2, 0 };
        }

        block->numEvents = count;

        wasmtime_val_t arg;
        arg.kind = WASMTIME_I32;
        arg.of.i32 = frames;

        wasmtime_context_set_epoch_deadline (d.context, blockTicks);

        std::string error;

        if (! d.call (d.processFunc, &arg, 1, nullptr, 0, &error))
        {
            d.fail (error);
            return process (inL, inR, outL, outR, numFrames, nullptr, 0);
        }

        block = reinterpret_cast<SharedBlock*> (d.at (d.shared, sizeof (SharedBlock)));

        // What the GUI watches: tap levels, scope samples, module displays.
        if (block != nullptr)
        {
            for (int t = 0; t < taps; ++t)
            {
                auto held = d.tapPeak[t].load (std::memory_order_relaxed);
                const auto peak = block->taps[t].peak;

                while (peak > held && ! d.tapPeak[t].compare_exchange_weak (held, peak, std::memory_order_relaxed)) {}

                d.tapRms[t].store (block->taps[t].rms, std::memory_order_relaxed);
            }

            const auto scopeCount = std::min ((int) block->scopeFrames, frames);

            for (int s = 0; s < maxScopes && scopeCount > 0; ++s)
            {
                if (d.scopeNode[s].load (std::memory_order_relaxed) == WasmPlugin::off)
                    continue;

                if (const auto* samples = reinterpret_cast<const float*> (d.at (block->scope[s], sizeof (float) * (size_t) scopeCount)))
                {
                    auto write = d.scopeWrite[s].load (std::memory_order_relaxed);

                    for (int i = 0; i < scopeCount; ++i)
                        d.scopeRing[s][(write + (std::uint32_t) i) % (std::uint32_t) scopeSize] = samples[i];

                    d.scopeWrite[s].store (write + (std::uint32_t) scopeCount, std::memory_order_release);
                }
            }

            for (int i = 0; i < d.numDisplays; ++i)
                d.displayValues[i].store (block->displays[i], std::memory_order_relaxed);
        }

        float* outputs[2] { outL, outR };

        for (int c = 0; c < 2; ++c)
        {
            if (outputs[c] == nullptr)
                continue;

            const auto* source = block != nullptr ? reinterpret_cast<const float*> (d.at (block->out[c], sizeof (float) * (size_t) frames))
                                                  : nullptr;

            if (source != nullptr)
                std::memcpy (outputs[c] + start, source, sizeof (float) * (size_t) frames);
            else
                std::memset (outputs[c] + start, 0, sizeof (float) * (size_t) frames);
        }
    }

    return true;
}

//==============================================================================
int WasmPlugin::getNumParameters() const noexcept              { return impl->numParams; }
const std::string& WasmPlugin::getDescription() const noexcept  { return impl->description; }
bool WasmPlugin::hasFailed() const noexcept                     { return impl->failed.load (std::memory_order_acquire); }
int WasmPlugin::getMaxFrames() const noexcept                   { return impl->maxFrames; }
double WasmPlugin::getSampleRate() const noexcept               { return impl->sampleRate; }

std::string WasmPlugin::getFailure() const
{
    return hasFailed() ? impl->failure : std::string();
}

void WasmPlugin::setParameter (int index, float value) noexcept
{
    if (index >= 0 && index < impl->numParams)
        impl->values[(size_t) index].store (value, std::memory_order_relaxed);
}

void WasmPlugin::setBypass (int moduleIndex, bool bypassed) noexcept
{
    if (moduleIndex >= 0 && moduleIndex < maxNodes)
        impl->bypass[moduleIndex].store (bypassed ? 1 : 0, std::memory_order_relaxed);
}

bool WasmPlugin::isBypassed (int moduleIndex) const noexcept
{
    return moduleIndex >= 0 && moduleIndex < maxNodes && impl->bypass[moduleIndex].load (std::memory_order_relaxed) != 0;
}

void WasmPlugin::setTaps (const std::vector<std::pair<int, int>>& taps) noexcept
{
    const auto count = std::min ((int) taps.size(), maxTaps);
    impl->numTaps.store (0, std::memory_order_relaxed);   // no half-set taps in the meantime

    for (int t = 0; t < count; ++t)
    {
        impl->tapNode[t].store (taps[(size_t) t].first, std::memory_order_relaxed);
        impl->tapPort[t].store (taps[(size_t) t].second, std::memory_order_relaxed);
        impl->tapPeak[t].store (0.0f, std::memory_order_relaxed);
        impl->tapRms[t].store (0.0f, std::memory_order_relaxed);
    }

    impl->numTaps.store (count, std::memory_order_release);
}

float WasmPlugin::takeTapPeak (int tap) noexcept
{
    return inRange (tap, maxTaps) ? impl->tapPeak[tap].exchange (0.0f, std::memory_order_relaxed) : 0.0f;
}

float WasmPlugin::getTapRms (int tap) const noexcept
{
    return inRange (tap, maxTaps) ? impl->tapRms[tap].load (std::memory_order_relaxed) : 0.0f;
}

void WasmPlugin::setScope (int slot, int moduleIndex, int port) noexcept
{
    if (! inRange (slot, maxScopes))
        return;

    impl->scopePort[slot].store (port, std::memory_order_relaxed);
    impl->scopeNode[slot].store (moduleIndex, std::memory_order_relaxed);
}

void WasmPlugin::readScope (int slot, float* destination, int numSamples) const noexcept
{
    if (! inRange (slot, maxScopes))
    {
        std::fill (destination, destination + numSamples, 0.0f);
        return;
    }

    const auto count = std::min (numSamples, scopeSize);
    const auto write = impl->scopeWrite[slot].load (std::memory_order_acquire);

    for (int i = 0; i < count; ++i)
    {
        const auto age = (std::uint32_t) (count - i);
        destination[i] = age <= write ? impl->scopeRing[slot][(write - age) % (std::uint32_t) scopeSize] : 0.0f;
    }

    for (int i = count; i < numSamples; ++i)
        destination[i] = 0.0f;
}

int WasmPlugin::getNumDisplays() const noexcept
{
    return impl->numDisplays;
}

float WasmPlugin::getDisplay (int index) const noexcept
{
    return index >= 0 && index < impl->numDisplays ? impl->displayValues[index].load (std::memory_order_relaxed) : 0.0f;
}

float WasmPlugin::getParameter (int index) const noexcept
{
    return index >= 0 && index < impl->numParams ? impl->values[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
}
