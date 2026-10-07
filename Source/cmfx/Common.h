#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// cmfx: small, self-contained audio effects. Standard library only: no JUCE, no Brimstone headers, so this folder
// can be copied or added (add_subdirectory) into any other project.
//
// Rules every effect here follows:
//  - `prepare (...)` is the only function that allocates. Everything else is real-time safe (no allocation, no locks,
//    no exceptions) and `noexcept`.
//  - `reset()` clears the audio state and snaps every smoothed value to its target; it does not allocate.
//  - Process functions work in place on float channel pointers. Passing the same pointer for left and right means
//    mono and is handled (the channel is processed once).
//  - A "neutral" setting (mix 0, depth 0, ...) is documented per effect and is a bit-exact pass-through.
namespace cmfx
{

constexpr double kTwoPi = 6.283185307179586476925286766559;

// An exact floating-point comparison that is meant to be exact (change detection, "landed exactly on the target").
// Spelled this way rather than == so it reads as deliberate and Clang's -Wfloat-equal stays quiet: std::equal_to
// compares inside a system header, where warnings aren't reported.
template <typename T>
constexpr bool exactlyEqual (T a, T b) noexcept
{
    return std::equal_to<T>{} (a, b);
}

// One-pole parameter smoother. Lands exactly on the target (so neutral settings are bit-exact once settled).
class Smoothed
{
public:
    void prepare (double sampleRate, double milliseconds) noexcept
    {
        coef = (float) (1.0 - std::exp (-1.0 / (0.001 * milliseconds * sampleRate)));
    }

    void set (float newTarget) noexcept { tgt = newTarget; }
    void snap() noexcept                { cur = tgt; }
    void snapTo (float v) noexcept      { cur = tgt = v; }
    float target() const noexcept       { return tgt; }
    bool settled() const noexcept       { return exactlyEqual (cur, tgt); }

    float next() noexcept
    {
        if (exactlyEqual (cur, tgt))
            return cur;
        cur += (tgt - cur) * coef;
        if (std::abs (tgt - cur) < 1.0e-6f)
            cur = tgt;
        return cur;
    }

private:
    float cur = 0.0f, tgt = 0.0f, coef = 1.0f;
};

// The lag of an opto-coupled control (a light-dependent resistor lit by a lamp or LED): it follows a rising control
// quickly and lets go of a falling one slowly, which gives a swept filter a warm, liquid movement. `amount` 0 .. 1 sets
// the two time constants (0.5 ms up / 2 ms down at 0, 10 ms / 80 ms at 1); an effect skips the lag entirely when its
// opto setting is 0, so 0 means no lag at all. `next` moves `state` (one per channel) towards `target`.
class OptoLag
{
public:
    static double riseMs (float amount) noexcept { return 0.5 + 9.5 * (double) amount; }
    static double fallMs (float amount) noexcept { return 2.0 + 78.0 * (double) amount; }

    void set (float amount, double sampleRate) noexcept
    {
        rise = coefficient (riseMs (amount), sampleRate);
        fall = coefficient (fallMs (amount), sampleRate);
    }

    double next (double& state, double target) const noexcept
    {
        state += (target - state) * (double) (target > state ? rise : fall);
        return state;
    }

private:
    static float coefficient (double ms, double sampleRate) noexcept { return (float) (1.0 - std::exp (-1.0 / (0.001 * ms * sampleRate))); }

    float rise = 1.0f, fall = 1.0f;
};

// Drive curves for the filters' input stage. All share the drive (1 .. 10 from `drive` 0 .. 1) and are level-matched at a
// reference of 0.2 (a 0.2 peak comes out at about 0.2 whatever the drive; quieter signals come up, louder ones are squashed),
// so changing the type never changes the loudness much. Each is exactly the input at drive 0 and bounded for any input.
//   Soft  a smooth tanh bend (the curve the filters always had)
//   Hard  a clip at +-1 after the drive: flat tops, odd harmonics, brighter
//   Fold  the drive goes through a sine, so peaks fold back instead of flattening: metallic, hollow
//   Fuzz  an asymmetric bend (tanh with a bias, zero at zero, so no DC from silence): even harmonics too, like a blown speaker
enum class DriveType : int { Soft, Hard, Fold, Fuzz, Count };

inline double driveSample (DriveType type, double x, double drive) noexcept
{
    constexpr double kReference = 0.2;
    const double g = 1.0 + 9.0 * drive;
    if (type == DriveType::Hard || type == DriveType::Fold || type == DriveType::Fuzz)
    {
        auto core = [type] (double v) noexcept
        {
            constexpr double kBias = 0.35;
            switch (type)
            {
                case DriveType::Hard: return std::clamp (v, -1.0, 1.0);
                case DriveType::Fold: return std::sin (v);
                case DriveType::Fuzz:
                case DriveType::Soft:                 // not reached: Soft takes the plain tanh below
                case DriveType::Count:
                default:              return std::tanh (v + kBias) - std::tanh (kBias);
            }
        };
        // Matched on the mean of the positive and negative reference peaks, so the lopsided Fuzz isn't louder on its long side.
        const double match = kReference / (0.5 * (std::abs (core (g * kReference)) + std::abs (core (-g * kReference))));
        return (1.0 - drive) * x + drive * core (g * x) * match;
    }
    return (1.0 - drive) * x + drive * std::tanh (g * x) * kReference / std::tanh (g * kReference);
}

// Mono delay line with a fractional (4-point Hermite) read. `allocate` is the only call that allocates.
// `read (d)` is the signal `d` samples before the one about to be written (so `read (D)` with an integer D, then
// `write`, gives an exact D-sample delay). `d` is clamped to [kMinDelay, size - 4].
class DelayLine
{
public:
    static constexpr double kMinDelay = 3.0;

    void allocate (std::size_t minSamples)
    {
        std::size_t n = 16;
        while (n < minSamples + 8)
            n <<= 1;
        data.assign (n, 0.0f);
        mask = n - 1;
        pos = 0;
    }

    void clear() noexcept
    {
        std::fill (data.begin(), data.end(), 0.0f);
        pos = 0;
    }

    double maxDelay() const noexcept { return (double) data.size() - 4.0; }

    float read (double d) const noexcept
    {
        d = std::clamp (d, kMinDelay, maxDelay());
        const double x = (double) pos - d;
        const double fl = std::floor (x);
        const float f = (float) (x - fl);
        const std::size_t i = (std::size_t) (std::int64_t) fl;
        const float y0 = data[(i - 1) & mask], y1 = data[i & mask], y2 = data[(i + 1) & mask], y3 = data[(i + 2) & mask];
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    void write (float x) noexcept
    {
        data[pos & mask] = x;
        ++pos;
    }

private:
    std::vector<float> data;
    std::size_t mask = 0, pos = 0;
};

} // namespace cmfx
