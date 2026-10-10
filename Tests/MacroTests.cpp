#include "TestMain.h"
#include "../Source/Engine/Macros.h"

using namespace plydrm;

void runMacroTests()
{
    VoiceParams knobs;
    knobs.steps = 10;
    knobs.hits = 3;
    knobs.probabilityPercent = 80.0f;

    // The defaults (Density 50 %, Complexity 0 %) leave the knobs exactly as they are.
    for (int v = 0; v < numVoices; ++v)
    {
        const auto out = applyMacros (knobs, static_cast<VoiceId> (v), 50.0f, 0.0f);
        PLYDRM_CHECK (out.steps == 10 && out.hits == 3 && out.probabilityPercent == 80.0f,
                      "Density 50 % and Complexity 0 % change nothing");
    }

    // Density: 0.4x at 0 %, 1.6x at 100 %, never past the steps.
    PLYDRM_CHECK (densityHits (5, 16, 0.0f) == 2, "Density 0 % gives 0.4x the hits");
    PLYDRM_CHECK (densityHits (5, 16, 100.0f) == 8, "Density 100 % gives 1.6x the hits");
    PLYDRM_CHECK (densityHits (12, 16, 100.0f) == 16, "Density never gives more hits than steps");
    PLYDRM_CHECK (densityHits (5, 16, 250.0f) == 8 && densityHits (5, 16, -20.0f) == 2, "Density is clamped to 0 .. 100 %");

    // Complexity 100 %: the default lengths 16 / 16 / 12 / 10 become 16 / 15 / 11 / 7, and probability drops by 35 %.
    for (int v = 0; v < numVoices; ++v)
    {
        auto k = knobs;
        k.steps = voiceInfo[static_cast<size_t> (v)].defaultSteps;
        k.hits = voiceInfo[static_cast<size_t> (v)].defaultHits;
        const auto out = applyMacros (k, static_cast<VoiceId> (v), 50.0f, 100.0f);
        const int expected[] = { 16, 15, 11, 7 };
        PLYDRM_CHECK (out.steps == expected[v], std::string ("Complexity 100 % shortens ") + voiceInfo[static_cast<size_t> (v)].id);
        PLYDRM_CHECK (std::abs (out.probabilityPercent - 80.0f * 0.65f) < 1.0e-4f, "Complexity 100 % thins probability by 35 %");
    }

    // Hits stay within the shortened loop; a loop never goes below one step.
    auto full = knobs;
    full.steps = 2;
    full.hits = 2;
    const auto perc = applyMacros (full, VoiceId::Perc, 100.0f, 100.0f);
    PLYDRM_CHECK (perc.steps == 1 && perc.hits == 1, "a short loop shortens to one step at most, its hits with it");

    // Halfway: the perc (3 steps at 100 %) loses two, the snare and hat (1) one (rounded half away from zero).
    PLYDRM_CHECK (applyMacros (knobs, VoiceId::Perc, 50.0f, 50.0f).steps == 8, "Complexity 50 % takes 2 of the perc's 3");
    PLYDRM_CHECK (applyMacros (knobs, VoiceId::Hat, 50.0f, 50.0f).steps == 9, "Complexity 50 % takes the hat's 1");
    PLYDRM_CHECK (applyMacros (knobs, VoiceId::Kick, 50.0f, 100.0f).steps == 10, "the kick keeps its length");
}
