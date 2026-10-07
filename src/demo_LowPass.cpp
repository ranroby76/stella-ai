// C:\workspace\Stella AI Studio\src\demo_LowPass.cpp
//
// Demo module: a resonant low-pass filter (a state-variable filter that stays stable
// however fast the cutoff moves).

#include "stella_api.h"

class LowPass final : public stella::Module
{
public:
    void prepare (double sampleRate, int) override
    {
        rate = (float) sampleRate;
        cutoff.prepare (sampleRate, 15.0f);
        cutoff.snap (param (0));
        reset();
    }

    void reset() override
    {
        ic1 = ic2 = 0.0f;
    }

    void process (const stella::Context& context, const float* const* inputs, float* const* outputs) override
    {
        const float* in = inputs[0];
        float* out = outputs[0];

        const float k = 2.0f - 1.96f * stella::clamp (param (1), 0.0f, 1.0f);   // resonance: 0 = none

        for (int i = 0; i < context.numFrames; ++i)
        {
            const float fc = stella::clamp (cutoff.next (param (0)), 20.0f, 0.45f * rate);
            const float g = std::tan (stella::pi * fc / rate);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;

            const float v3 = in[i] - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;

            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;

            out[i] = v2;
        }
    }

private:
    float rate = 48000.0f, ic1 = 0.0f, ic2 = 0.0f;
    stella::Smoother cutoff;
};

namespace
{
    const char* const inputs[]  { "in" };
    const char* const outputs[] { "out" };

    const stella::Param params[]
    {
        { "cutoff",    "Cutoff",    20.0f, 20000.0f, 2500.0f, "Hz", 0.25f },
        { "resonance", "Resonance", 0.0f,  1.0f,     0.3f,    "",   1.0f  },
    };

    stella::Registrar registrar ({ "LowPass", "Low-pass filter", inputs, STELLA_COUNT (inputs), outputs, STELLA_COUNT (outputs),
                                   params, STELLA_COUNT (params), [] () -> stella::Module* { return new LowPass(); } });
}
