#pragma once

#include <cstdint>

// Randomness as a pure function of (seed, voice, step, what): the same seed
// gives the same probability rolls, humanize, velocities and hit variations
// at the same song position, however playback got there (started mid-song,
// looped, bounced offline). splitmix64, as in Brimstone's cmfx Lfo.h.

namespace plydrm
{

inline uint64_t splitmix64 (uint64_t z) noexcept
{
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/** What a random value is for, so the values for one step are independent. */
enum class StepRandomUse : uint64_t { Probability = 1, Humanize, Velocity, Variation };

inline uint64_t stepRandom (uint32_t seed, int voice, int64_t step, StepRandomUse use) noexcept
{
    uint64_t h = splitmix64 (seed);
    h = splitmix64 (h ^ static_cast<uint64_t> (voice));
    h = splitmix64 (h ^ static_cast<uint64_t> (step));
    return splitmix64 (h ^ static_cast<uint64_t> (use));
}

/** [0, 1) from the top 24 bits of a hash. */
inline float unitFloat (uint64_t h) noexcept
{
    return static_cast<float> (h >> 40) * (1.0f / 16777216.0f);
}

} // namespace plydrm
