// C:\workspace\Stella AI Studio\src\demo_StereoDelay.cpp
//
// Demo module: a stereo delay with feedback and a dry/wet mix. The time glides smoothly
// when it changes, like a tape delay.

#include "stella_api.h"

#include <algorithm>
#include <vector>

class StereoDelay final : public stella::Module
{
public:
    void prepare (double sampleRate, int) override
    {
        rate = (float) sampleRate;
        size = (int) (rate * 2.1f) + 4;   // up to two seconds

        for (auto& line : lines)
            line.assign ((size_t) size, 0.0f);

        time.prepare (sampleRate, 150.0f);
        time.snap (param (0));
        position = 0;
    }

    void reset() override
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
    }

    void process (const stella::Context& context, const float* const* inputs, float* const* outputs) override
    {
        const float feedback = stella::clamp (param (1), 0.0f, 0.95f);
        const float mix = stella::clamp (param (2), 0.0f, 1.0f);

        for (int i = 0; i < context.numFrames; ++i)
        {
            const float delay = stella::clamp (time.next (param (0)) * 0.001f * rate, 1.0f, (float) size - 2.0f);

            // Whole samples and the fraction kept apart: index arithmetic stays exact.
            const int whole = (int) delay;
            const float frac = delay - (float) whole;

            int a = position - whole;      // "whole" samples back
            if (a < 0) a += size;

            int b = a - 1;                 // one sample further back
            if (b < 0) b += size;

            for (int c = 0; c < 2; ++c)
            {
                auto& line = lines[c];
                const float delayed = line[(size_t) a] + (line[(size_t) b] - line[(size_t) a]) * frac;
                const float dry = inputs[c][i];

                line[(size_t) position] = dry + delayed * feedback;
                outputs[c][i] = dry * (1.0f - mix) + delayed * mix;
            }

            if (++position >= size)
                position = 0;
        }
    }

private:
    std::vector<float> lines[2];
    stella::Smoother time;
    float rate = 48000.0f;
    int size = 1, position = 0;
};

namespace
{
    const char* const inputs[]  { "in L", "in R" };
    const char* const outputs[] { "out L", "out R" };

    const stella::Param params[]
    {
        { "time",     "Time",     10.0f, 2000.0f, 350.0f, "ms", 0.5f },
        { "feedback", "Feedback", 0.0f,  0.95f,   0.4f,   "",   1.0f },
        { "mix",      "Mix",      0.0f,  1.0f,    0.35f,  "",   1.0f },
    };

    stella::Registrar registrar ({ "StereoDelay", "Stereo delay", inputs, STELLA_COUNT (inputs), outputs, STELLA_COUNT (outputs),
                                   params, STELLA_COUNT (params), [] () -> stella::Module* { return new StereoDelay(); } });
}
