// C:\workspace\Stella AI Studio\src\WasmPlugin.h

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

//==============================================================================
/**
    One plugin being built, running live in a sandbox (WebAssembly, run by Wasmtime).

    Whatever the AI-written code does, it can't reach the studio's memory, files or
    network; it can't use more than 512 MB; and a block that runs too long (an endless
    loop, say) is stopped and the plugin goes silent instead of freezing the audio.

    Talks to the Stella runtime compiled into the plugin (stella_runtime.cpp): audio in
    and out, MIDI events and parameter values pass through one shared block of memory.
*/
class WasmPlugin final
{
public:
    struct MidiEvent
    {
        std::uint32_t frame;
        std::uint8_t status, data1, data2;
    };

    /** Compiles and starts a plugin. Takes a while: call it on a worker thread.
        On failure returns nullptr and says why in error. */
    static std::unique_ptr<WasmPlugin> load (const std::vector<std::uint8_t>& wasm, double sampleRate,
                                             int maxFrames, std::string& error);
    ~WasmPlugin();

    /** Audio thread. Any of the pointers may be null (a missing channel). Long blocks are
        split up. Returns false once the plugin has failed: from then on it's silent. */
    bool process (const float* inL, const float* inR, float* outL, float* outR, int numFrames,
                  const MidiEvent* events, int numEvents) noexcept;

    int getNumParameters() const noexcept;
    void setParameter (int index, float value) noexcept;   // any thread: used from the next block
    float getParameter (int index) const noexcept;

    /** Modules are numbered as in graph.json. A bypassed module passes its first input
        straight to its first output. Any thread; used from the next block. */
    void setBypass (int moduleIndex, bool bypassed) noexcept;
    bool isBypassed (int moduleIndex) const noexcept;

    //==============================================================================
    // Watching signals. A signal is a module's output port (moduleIndex >= 0), the plugin's
    // input (pluginInput, port 0 or 1) or its output (pluginOutput, port 0 or 1).
    static constexpr int pluginInput = -1, pluginOutput = -2, off = -100;
    static constexpr int maxTaps = 32, maxScopes = 4;

    /** The signals the GUI's meters watch (up to 32), as (moduleIndex, port). Any thread. */
    void setTaps (const std::vector<std::pair<int, int>>& taps) noexcept;

    /** A tap's highest level since the last call (0..1+), and its RMS in the latest block. */
    float takeTapPeak (int tap) noexcept;
    float getTapRms (int tap) const noexcept;

    /** Scope slots 0..3 each copy one signal (slot 0 is the schematic's probe). */
    void setScope (int slot, int moduleIndex, int port) noexcept;

    /** The newest numSamples of a scope slot, oldest first (zeros before it started).
        Any thread; for a scope it doesn't matter if a block lands mid-read. */
    void readScope (int slot, float* destination, int numSamples) const noexcept;

    /** The values modules show (stella::Module::display), numbered as in the description. */
    int getNumDisplays() const noexcept;
    float getDisplay (int index) const noexcept;

    /** JSON from the plugin: its modules (ports) and parameters (ids, names, ranges). */
    const std::string& getDescription() const noexcept;

    bool hasFailed() const noexcept;
    std::string getFailure() const;   // why it stopped, once hasFailed()

    int getMaxFrames() const noexcept;
    double getSampleRate() const noexcept;

private:
    WasmPlugin();

    struct Impl;
    std::unique_ptr<Impl> impl;
};
