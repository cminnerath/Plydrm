#pragma once

#include <vector>
#include <cstdint>

namespace plydrm
{
/** Bjorklund's algorithm: distributes `hits` pulses as evenly as possible
    across `steps` slots. Pure function, no JUCE dependency, so it can be
    unit tested in isolation from the audio engine. */
std::vector<bool> euclideanPattern (int steps, int hits);

/** Rotates a pattern left by `rotation` steps (wrapping). */
std::vector<bool> rotatePattern (const std::vector<bool>& pattern, int rotation);

} // namespace plydrm
