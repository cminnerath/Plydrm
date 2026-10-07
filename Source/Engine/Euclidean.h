#pragma once

#include <array>

namespace plydrm
{
/** Longest loop a voice can have (the Steps parameter's maximum). */
inline constexpr int kMaxPatternSteps = 32;

/** A fixed-capacity on/off step pattern. Plain data, so building, copying
    and rotating one never allocates (it is rebuilt on the audio thread
    whenever steps/hits/rotation change). */
struct Pattern
{
    std::array<bool, kMaxPatternSteps> step {};
    int length = 0;

    bool operator[] (int i) const noexcept { return step[static_cast<size_t> (i)]; }
    bool operator== (const Pattern&) const = default;
};

/** Bjorklund's algorithm: distributes `hits` pulses as evenly as possible
    across `steps` slots. steps is clamped to 0..kMaxPatternSteps and hits to
    0..steps. Pure function, no JUCE dependency, no allocation. */
Pattern euclideanPattern (int steps, int hits) noexcept;

/** Rotates a pattern left by `rotation` steps (wrapping). */
Pattern rotatePattern (const Pattern& pattern, int rotation) noexcept;

} // namespace plydrm
