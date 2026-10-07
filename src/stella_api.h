// C:\workspace\Stella AI Studio\src\stella_api.h
//
// The Stella DSP API. Every block of a Stella plugin is a module written against this
// file. Modules are compiled to WebAssembly for the live preview, and natively for the
// exported CLAP and VST3 plugins, from the same source.
//
// A module:
//   - is a class derived from stella::Module, in its own .cpp file in the project's
//     modules folder;
//   - has mono audio inputs and outputs, each one float buffer at the sample rate
//     (stereo is two ports; control signals are ports too);
//   - declares its parameters (stable ids: presets, automation and the GUI rely on them);
//   - registers itself with a stella::Registrar at the end of its file.
//
// Rules inside process(): no allocation, no locks, no waiting, no I/O. Allocate in
// prepare(). Feedback between modules isn't allowed; keep feedback inside one module.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#define STELLA_COUNT(array) ((int) (sizeof (array) / sizeof ((array)[0])))

namespace stella
{
    //==========================================================================
    /** A parameter a module exposes. Once the plugin exists, its id never changes. */
    struct Param
    {
        const char* id;     // e.g. "cutoff": letters, digits and _ only
        const char* name;   // e.g. "Cutoff"
        float min, max, def;
        const char* unit;   // e.g. "Hz", "dB", "%", "ms", or ""
        float skew;         // 1 = linear; below 1 gives the low end more room (frequencies, times)
    };

    /** One note event inside the current block. */
    struct Note
    {
        int frame;          // where in the block it happens
        int note;           // MIDI note number 0..127
        float velocity;     // 0..1 (0 for note-offs)
        bool on;            // true: note on, false: note off
    };

    /** Everything a module gets with each block. */
    struct Context
    {
        double sampleRate;
        int numFrames;
        const Note* notes;  // this block's notes, in time order
        int numNotes;
        float pitchBend;    // -1..1
        float modWheel;     // 0..1
    };

    //==========================================================================
    /** A DSP block. */
    class Module
    {
    public:
        virtual ~Module() = default;

        /** Before the first block, and whenever the sample rate or block size change.
            Allocate here, never in process(). */
        virtual void prepare (double sampleRate, int maxFrames) { (void) sampleRate; (void) maxFrames; }

        /** inputs[i] and outputs[i] hold context.numFrames samples. Unconnected inputs read
            silence. Write every sample of every output. */
        virtual void process (const Context& context, const float* const* inputs, float* const* outputs) = 0;

        /** Clear delay lines, voices and so on. */
        virtual void reset() {}

        /** The current value of this module's parameter number index (in declaration order). */
        float param (int index) const noexcept    { return params[index]; }

        /** Shows a value on the GUI (a meter or a lamp bound to "<moduleId>.<displayName>"):
            an LFO's position, an envelope's level, gain reduction... Once per block is enough. */
        void display (int index, float value) noexcept    { if (displays != nullptr) displays[index] = value; }

        float* params = nullptr;     // set by the runtime; modules only read it
        float* displays = nullptr;   // set by the runtime; write it with display()
    };

    //==========================================================================
    /** What a module type is: its name, ports and parameters. One per module class. */
    struct ModuleType
    {
        const char* type;                   // the class name, e.g. "LowPass"
        const char* name;                   // shown to the user, e.g. "Low-pass filter"
        const char* const* inputs;          // port names, e.g. { "in" }
        int numInputs;
        const char* const* outputs;         // e.g. { "out" }
        int numOutputs;
        const Param* params;
        int numParams;
        Module* (*create)();
        const char* const* displayNames = nullptr;   // optional: values the module shows, e.g. { "lfo", "gr" }
        int numDisplays = 0;
    };

    void registerModuleType (const ModuleType& type);

    /** At the end of a module's file:
            static stella::Registrar registrar ({ "LowPass", "Low-pass filter", inputs, STELLA_COUNT (inputs), ... });  */
    struct Registrar
    {
        explicit Registrar (const ModuleType& type)    { registerModuleType (type); }
    };

    //==========================================================================
    // Helpers modules may use.
    constexpr float pi    = 3.14159265358979323846f;
    constexpr float twoPi = 2.0f * pi;

    inline float noteToHz (float note) noexcept           { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }
    inline float dbToGain (float db) noexcept             { return std::pow (10.0f, db * 0.05f); }
    inline float clamp (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    /** Smooths a value per sample (a one-pole low-pass), for clickless parameter changes. */
    struct Smoother
    {
        void prepare (double sampleRate, float milliseconds = 20.0f) noexcept
        {
            coeff = 1.0f - std::exp (-1.0f / (0.001f * milliseconds * (float) sampleRate));
        }

        void snap (float v) noexcept         { value = v; }
        float next (float target) noexcept   { value += (target - value) * coeff; return value; }

        float value = 0.0f, coeff = 1.0f;
    };
}
