#pragma once

// The output stage: master gain (smoothed, so moving the knob never zippers or
// clicks), then an optional soft limiter. Plain C++, no allocation.

#include "../cmfx/Common.h"
#include <cmath>

namespace plydrm
{

// Transparent up to 0.9, then bends smoothly towards 1 and never reaches it.
// (From Brimstone's engine/OutputStage.h.)
inline float softLimit (float x) noexcept
{
    constexpr float knee = 0.9f;
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    return std::copysign (knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee)), x);
}

class OutputStage
{
public:
    /** Off the audio thread. Starts at `gain` with no glide. */
    void prepare (double sampleRate, float gain) noexcept
    {
        smoothedGain.prepare (sampleRate, 20.0);
        smoothedGain.snapTo (gain);
    }

    void setGain (float gain) noexcept       { smoothedGain.set (gain); }
    void setLimiter (bool on) noexcept       { limiter = on; }

    /** In place. A gain settled at exactly 1 with the limiter off leaves the samples untouched. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (! limiter && smoothedGain.settled() && cmfx::exactlyEqual (smoothedGain.target(), 1.0f))
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = smoothedGain.next();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float x = channels[ch][i] * g;
                if (limiter)
                    x = softLimit (x);
                channels[ch][i] = x;
            }
        }
    }

private:
    cmfx::Smoothed smoothedGain;
    bool limiter = true;
};

} // namespace plydrm
