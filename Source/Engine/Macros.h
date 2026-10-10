#pragma once

#include "PatternEngine.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace plydrm
{
/** The two global macros, Density and Complexity, which the perform pad plays
    (Density across, Complexity up). Both are applied on top of each voice's own
    knobs every block, so a macro never moves a knob, and a macro at its default
    leaves the knobs exactly as they are.

    Density scales every voice's hits: 0 % gives 0.4x, 50 % (the default) 1x,
    100 % 1.6x, never more than the voice's steps.

    Complexity (default 0 %: no change) pulls the voices apart. Every loop but
    the kick's gets shorter, by up to kComplexityShrink steps at 100 % (the
    default 16 / 16 / 12 / 10 become 16 / 15 / 11 / 7: lengths that share no
    factor with the kick's, so the voices phase against it and the whole pattern
    takes far longer to come round), and every voice's probability drops by up
    to kComplexityThin of itself, so hits drop out and no two bars are the same.
    The kick keeps its length, so the bar is still there to hear. */
static constexpr std::array<int, numVoices> kComplexityShrink { 0, 1, 1, 3 };
static constexpr float kComplexityThin = 0.35f;

/** `hits` scaled by Density (0 - 100 %), within 0 .. steps. */
inline int densityHits (int hits, int steps, float densityPercent) noexcept
{
    const float factor = 0.4f + (std::clamp (densityPercent, 0.0f, 100.0f) / 100.0f) * 1.2f;   // 50 % -> 1.0x
    return std::clamp (static_cast<int> (std::lround (static_cast<float> (hits) * factor)), 0, steps);
}

/** A voice's knobs (`knobs`, its hits as set) with both macros applied. */
inline VoiceParams applyMacros (VoiceParams knobs, VoiceId voice, float densityPercent, float complexityPercent) noexcept
{
    const float c = std::clamp (complexityPercent, 0.0f, 100.0f) / 100.0f;
    const auto shrink = static_cast<int> (std::lround (c * static_cast<float> (kComplexityShrink[static_cast<size_t> (voice)])));
    knobs.steps = std::max (1, knobs.steps - shrink);
    knobs.hits = densityHits (knobs.hits, knobs.steps, densityPercent);
    knobs.probabilityPercent *= 1.0f - kComplexityThin * c;
    return knobs;
}

} // namespace plydrm
