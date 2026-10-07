#pragma once

#include "Common.h"

namespace cmfx
{

// Topology-preserving-transform (Zavalishin) state-variable filter: stable and well behaved while its cutoff is swept, which
// is what every effect here does with it. One `SvfCoef` is shared by both channels; each channel has its own `SvfState`.
// Doubles throughout, so a cutoff of a few tens of Hz at 96 kHz is still accurate.
//
// Outputs: low-pass `lp`, band-pass `bp` (its peak is 1/k = Q, so `k * bp` is the unity-gain band-pass), high-pass `hp`;
// the notch is `lp + hp` = `x - k * bp`.
struct SvfCoef
{
    double k = 1.4142135623730951, a1 = 0.0, a2 = 0.0, a3 = 0.0;

    // `hz` is clamped to 10 Hz .. 0.45 sample rate; `q` > 0 (0.7071 is Butterworth).
    void set (double sampleRate, double hz, double q) noexcept
    {
        const double g = std::tan (kTwoPi / 2.0 * std::clamp (hz, 10.0, 0.45 * sampleRate) / sampleRate);
        k = 1.0 / std::max (q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;

    void reset() noexcept { ic1 = ic2 = 0.0; }

    void process (const SvfCoef& c, double x, double& lp, double& bp, double& hp) noexcept
    {
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - c.k * v1 - v2;
    }
};

} // namespace cmfx
