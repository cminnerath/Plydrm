#include "TestMain.h"
#include "../Source/Engine/PatternEngine.h"
#include <vector>

using namespace plydrm;

namespace
{
    constexpr double sampleRate = 44100.0;

    PatternEngine makeEngine (double bpm = 120.0, double stepQuarterLen = 0.25)
    {
        PatternEngine engine;
        engine.prepareToPlay (sampleRate);
        engine.setTempo (bpm, stepQuarterLen);
        engine.reseed (1234);
        return engine;
    }

    void disableAllExcept (PatternEngine& engine, VoiceId keep, const VoiceParams& keepParams)
    {
        for (int i = 0; i < numVoices; ++i)
        {
            if (static_cast<VoiceId> (i) == keep)
            {
                engine.setVoiceParams (keep, keepParams);
            }
            else
            {
                VoiceParams off;
                off.enabled = false;
                engine.setVoiceParams (static_cast<VoiceId> (i), off);
            }
        }
    }

    int countForVoice (const std::vector<TriggerEvent>& events, VoiceId voice)
    {
        int n = 0;
        for (auto& e : events)
            if (e.voice == voice)
                ++n;
        return n;
    }
}

void runPatternEngineTests()
{
    // --- Sample-accurate single trigger -------------------------------------
    {
        auto engine = makeEngine (120.0, 0.25); // 1/16 step @ 120bpm -> 5512.5 samples/step
        VoiceParams kick;
        kick.enabled = true;
        kick.steps = 4;
        kick.hits = 4; // all steps active, so the very first step boundary always fires
        kick.probabilityPercent = 100.0f;
        disableAllExcept (engine, VoiceId::Kick, kick);

        std::vector<TriggerEvent> events;
        engine.process (6000, events);

        PLYDRM_CHECK (countForVoice (events, VoiceId::Kick) == 1, "expected exactly one kick trigger in first 6000 samples");
        if (! events.empty())
        {
            const int offset = events.front().sampleOffset;
            PLYDRM_CHECK (offset > 5400 && offset < 5600,
                          "trigger should land near the computed step boundary (~5512), got " + std::to_string (offset));
        }
    }

    // --- Polymeter / polyrhythm phasing: independent step counts -----------
    {
        auto engine = makeEngine (120.0, 0.25);

        VoiceParams a; a.enabled = true; a.steps = 4; a.hits = 4; a.probabilityPercent = 100.0f;
        VoiceParams b; b.enabled = true; b.steps = 3; b.hits = 3; b.probabilityPercent = 100.0f;

        engine.setVoiceParams (VoiceId::Kick, a);
        engine.setVoiceParams (VoiceId::Snare, b);
        VoiceParams off; off.enabled = false;
        engine.setVoiceParams (VoiceId::Hat, off);
        engine.setVoiceParams (VoiceId::Perc, off);

        // Process exactly 12 step-lengths worth of samples (shared LCM of 4 and 3).
        const double samplesPerStep = sampleRate * 60.0 / 120.0 * 0.25;
        const int totalSamples = static_cast<int> (samplesPerStep * 12.0) + 10;

        std::vector<TriggerEvent> events;
        engine.process (totalSamples, events);

        // Both voices have an all-hit pattern, so every one of the 12 shared
        // step boundaries should produce a trigger for each enabled voice -
        // the two voices are independent (polymetric) even though they share
        // the same underlying pulse.
        PLYDRM_CHECK (countForVoice (events, VoiceId::Kick) == 12, "4-step voice should fire on all 12 shared steps");
        PLYDRM_CHECK (countForVoice (events, VoiceId::Snare) == 12, "3-step voice should fire on all 12 shared steps");
    }

    // --- Every-Nth-repeat conditional trigger -------------------------------
    {
        auto engine = makeEngine (120.0, 0.25);
        VoiceParams v;
        v.enabled = true;
        v.steps = 4;
        v.hits = 4;
        v.probabilityPercent = 100.0f;
        v.everyN = 2; // only fire on even-numbered loop repeats
        disableAllExcept (engine, VoiceId::Kick, v);

        const double samplesPerStep = sampleRate * 60.0 / 120.0 * 0.25;
        const int totalSamples = static_cast<int> (samplesPerStep * 8.0) + 10; // 2 full loop repeats

        std::vector<TriggerEvent> events;
        engine.process (totalSamples, events);

        PLYDRM_CHECK (countForVoice (events, VoiceId::Kick) == 4,
                      "everyN=2 should only fire on the first of two loop repeats (4 steps), got "
                          + std::to_string (countForVoice (events, VoiceId::Kick)));
    }

    // --- Probability = 0 suppresses all hits ---------------------------------
    {
        auto engine = makeEngine (120.0, 0.25);
        VoiceParams v;
        v.enabled = true;
        v.steps = 4;
        v.hits = 4;
        v.probabilityPercent = 0.0f;
        disableAllExcept (engine, VoiceId::Kick, v);

        const double samplesPerStep = sampleRate * 60.0 / 120.0 * 0.25;
        const int totalSamples = static_cast<int> (samplesPerStep * 8.0) + 10;

        std::vector<TriggerEvent> events;
        engine.process (totalSamples, events);

        PLYDRM_CHECK (countForVoice (events, VoiceId::Kick) == 0, "probability=0 should never fire");
    }
}
