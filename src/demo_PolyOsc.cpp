// C:\workspace\Stella AI Studio\src\demo_PolyOsc.cpp
//
// Demo module: an eight-voice synth, two slightly detuned saws per voice through an
// ADSR envelope. Written against stella_api.h like every Stella module.

#include "stella_api.h"

class PolyOsc final : public stella::Module
{
public:
    void prepare (double sampleRate, int) override
    {
        rate = (float) sampleRate;
        reset();
    }

    void reset() override
    {
        for (auto& voice : voices)
            voice = Voice();
    }

    void process (const stella::Context& context, const float* const*, float* const* outputs) override
    {
        float* out = outputs[0];

        const float attack  = 1.0f / (stella::clamp (param (0), 1.0f, 10000.0f) * 0.001f * rate);
        const float decay   = 1.0f - std::exp (-1.0f / (stella::clamp (param (1), 1.0f, 10000.0f) * 0.001f * rate));
        const float sustain = stella::clamp (param (2), 0.0f, 1.0f);
        const float release = 1.0f - std::exp (-1.0f / (stella::clamp (param (3), 1.0f, 10000.0f) * 0.001f * rate));
        const float detune  = std::pow (2.0f, param (4) / 1200.0f);
        const float bend    = std::pow (2.0f, context.pitchBend * 2.0f / 12.0f);   // +-2 semitones

        int nextNote = 0;

        for (int i = 0; i < context.numFrames; ++i)
        {
            while (nextNote < context.numNotes && context.notes[nextNote].frame <= i)
                handle (context.notes[nextNote++]);

            float sum = 0.0f;

            for (auto& v : voices)
            {
                if (v.stage == Stage::off)
                    continue;

                switch (v.stage)
                {
                    case Stage::attack:
                        v.env += attack;
                        if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Stage::decay; }
                        break;

                    case Stage::decay:
                        v.env += (sustain - v.env) * decay;
                        break;

                    case Stage::release:
                        v.env -= v.env * release;
                        if (v.env < 0.0001f) { v.stage = Stage::off; continue; }
                        break;

                    case Stage::off:
                        break;
                }

                const float freq = stella::noteToHz ((float) v.note) * bend;
                const float dt1 = freq / rate;
                const float dt2 = dt1 * detune;

                sum += (saw (v.phase1, dt1) + saw (v.phase2, dt2)) * 0.5f * v.env * v.velocity;

                v.phase1 += dt1; if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                v.phase2 += dt2; if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
            }

            out[i] = sum * 0.25f;
        }

        while (nextNote < context.numNotes)
            handle (context.notes[nextNote++]);
    }

private:
    enum class Stage { off, attack, decay, release };

    struct Voice
    {
        int note = -1;
        float phase1 = 0.0f, phase2 = 0.37f, env = 0.0f, velocity = 0.0f;
        Stage stage = Stage::off;
        unsigned age = 0;
    };

    static float saw (float phase, float dt) noexcept
    {
        // A band-limited saw (PolyBLEP): no harsh aliasing on high notes.
        float value = 2.0f * phase - 1.0f;

        if (phase < dt)              { const float t = phase / dt;          value -= t + t - t * t - 1.0f; }
        else if (phase > 1.0f - dt)  { const float t = (phase - 1.0f) / dt; value -= t * t + t + t + 1.0f; }

        return value;
    }

    void handle (const stella::Note& note) noexcept
    {
        if (note.on)
        {
            Voice* chosen = nullptr;

            for (auto& v : voices)
                if (v.stage == Stage::off) { chosen = &v; break; }

            if (chosen == nullptr)
            {
                chosen = &voices[0];

                for (auto& v : voices)
                    if (v.age < chosen->age)
                        chosen = &v;
            }

            chosen->note = note.note;
            chosen->velocity = 0.3f + 0.7f * note.velocity;
            chosen->stage = Stage::attack;
            chosen->age = ++counter;
        }
        else
        {
            for (auto& v : voices)
                if (v.note == note.note && v.stage != Stage::off && v.stage != Stage::release)
                    v.stage = Stage::release;
        }
    }

    Voice voices[8];
    float rate = 48000.0f;
    unsigned counter = 0;
};

namespace
{
    const char* const outputs[] { "out" };

    const stella::Param params[]
    {
        { "attack",  "Attack",  1.0f, 2000.0f, 5.0f,   "ms",    0.35f },
        { "decay",   "Decay",   1.0f, 4000.0f, 300.0f, "ms",    0.35f },
        { "sustain", "Sustain", 0.0f, 1.0f,    0.7f,   "",      1.0f  },
        { "release", "Release", 1.0f, 5000.0f, 400.0f, "ms",    0.35f },
        { "detune",  "Detune",  0.0f, 50.0f,   7.0f,   "cents", 1.0f  },
    };

    stella::Registrar registrar ({ "PolyOsc", "Saw voices", nullptr, 0, outputs, STELLA_COUNT (outputs),
                                   params, STELLA_COUNT (params), [] () -> stella::Module* { return new PolyOsc(); } });
}
