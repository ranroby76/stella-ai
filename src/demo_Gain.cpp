// C:\workspace\Stella AI Studio\src\demo_Gain.cpp
//
// Demo module: output volume in decibels, smoothed so moving it never clicks.

#include "stella_api.h"

class Gain final : public stella::Module
{
public:
    void prepare (double sampleRate, int) override
    {
        level.prepare (sampleRate, 20.0f);
        level.snap (stella::dbToGain (param (0)));
    }

    void process (const stella::Context& context, const float* const* inputs, float* const* outputs) override
    {
        const float target = param (0) <= -59.9f ? 0.0f : stella::dbToGain (param (0));

        for (int i = 0; i < context.numFrames; ++i)
            outputs[0][i] = inputs[0][i] * level.next (target);
    }

private:
    stella::Smoother level;
};

namespace
{
    const char* const inputs[]  { "in" };
    const char* const outputs[] { "out" };

    const stella::Param params[]
    {
        { "volume", "Volume", -60.0f, 6.0f, -6.0f, "dB", 1.0f },
    };

    stella::Registrar registrar ({ "Gain", "Volume", inputs, STELLA_COUNT (inputs), outputs, STELLA_COUNT (outputs),
                                   params, STELLA_COUNT (params), [] () -> stella::Module* { return new Gain(); } });
}
