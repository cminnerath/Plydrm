#include "TestMain.h"
#include "../Source/Engine/PatternEngine.h"
#include <cstring>
#include <functional>
#include <vector>

using namespace plydrm;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double samplesPerStep = 6000.0;     // 1/16 at 120 BPM, 48 kHz
    constexpr double ppqPerSample = 1.0 / 24000.0;

    PatternEngine makeEngine (double stepQuarterLen = 0.25)
    {
        PatternEngine engine;
        engine.prepareToPlay (sampleRate);
        engine.setStepLength (stepQuarterLen);
        engine.setSeed (1234);
        return engine;
    }

    // No transport at all (standalone): the clock free-runs at the host's tempo.
    HostTimeInfo freeRun()
    {
        HostTimeInfo h;
        h.hasBpm = true;
        h.bpm = 120.0;
        return h;
    }

    HostTimeInfo playingAt (double ppq)
    {
        HostTimeInfo h = freeRun();
        h.hasPpq = true;
        h.ppq = ppq;
        h.isPlaying = true;
        return h;
    }

    HostTimeInfo stoppedAt (double ppq)
    {
        HostTimeInfo h = playingAt (ppq);
        h.isPlaying = false;
        return h;
    }

    VoiceParams allHits (int steps)
    {
        VoiceParams v;
        v.enabled = true;
        v.steps = steps;
        v.hits = steps;
        v.probabilityPercent = 100.0f;
        v.humanizeMs = 0.0f;
        return v;
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

    struct AbsoluteTrigger
    {
        int64_t sample;
        VoiceId voice;
        float velocity = 1.0f;
        uint32_t variation = 0;
        bool operator== (const AbsoluteTrigger& o) const
        {
            return sample == o.sample && voice == o.voice && std::memcmp (&velocity, &o.velocity, sizeof (float)) == 0
                   && variation == o.variation;
        }
    };

    // Runs [0, total) in blocks; `blockSize` and `host` are asked for each block by its start sample.
    std::vector<AbsoluteTrigger> run (PatternEngine& engine, int64_t total,
                                      const std::function<int (int64_t)>& blockSize,
                                      const std::function<HostTimeInfo (int64_t)>& host)
    {
        std::vector<AbsoluteTrigger> out;
        std::vector<TriggerEvent> events;
        for (int64_t start = 0; start < total; )
        {
            const int n = static_cast<int> (std::min<int64_t> (blockSize (start), total - start));
            events.clear();
            engine.process (host (start), n, events);
            for (auto& e : events)
                out.push_back ({ start + e.sampleOffset, e.voice, e.velocity, e.variation });
            start += n;
        }
        return out;
    }
}

void runPatternEngineTests()
{
    // --- The first step lands on sample 0, the next one a step later --------
    {
        auto engine = makeEngine();
        disableAllExcept (engine, VoiceId::Kick, allHits (4));

        std::vector<TriggerEvent> events;
        engine.process (freeRun(), 6500, events);

        PLYDRM_CHECK (countForVoice (events, VoiceId::Kick) == 2, "expected two kick triggers in the first 6500 samples, got "
                                                                    + std::to_string (countForVoice (events, VoiceId::Kick)));
        if (events.size() == 2)
        {
            PLYDRM_CHECK (events[0].sampleOffset == 0, "step 0 should fire at sample 0, got " + std::to_string (events[0].sampleOffset));
            PLYDRM_CHECK (events[1].sampleOffset == 6000, "step 1 should fire at sample 6000, got " + std::to_string (events[1].sampleOffset));
        }
    }

    // --- Polymeter / polyrhythm phasing: independent step counts -----------
    {
        auto engine = makeEngine();
        engine.setVoiceParams (VoiceId::Kick, allHits (4));
        engine.setVoiceParams (VoiceId::Snare, allHits (3));
        VoiceParams off; off.enabled = false;
        engine.setVoiceParams (VoiceId::Hat, off);
        engine.setVoiceParams (VoiceId::Perc, off);

        // Steps 0..11 (shared LCM of 4 and 3), stopping short of step 12.
        std::vector<TriggerEvent> events;
        engine.process (freeRun(), static_cast<int> (samplesPerStep * 12.0) - 10, events);

        PLYDRM_CHECK (countForVoice (events, VoiceId::Kick) == 12, "4-step voice should fire on all 12 shared steps");
        PLYDRM_CHECK (countForVoice (events, VoiceId::Snare) == 12, "3-step voice should fire on all 12 shared steps");
    }

    // --- Every-Nth-repeat conditional trigger -------------------------------
    {
        auto engine = makeEngine();
        auto v = allHits (4);
        v.everyN = 2; // only fire on even-numbered loop repeats
        disableAllExcept (engine, VoiceId::Kick, v);

        std::vector<TriggerEvent> events;
        engine.process (freeRun(), static_cast<int> (samplesPerStep * 8.0) - 10, events); // 2 full loop repeats

        PLYDRM_CHECK (countForVoice (events, VoiceId::Kick) == 4,
                      "everyN=2 should only fire on the first of two loop repeats (4 steps), got "
                          + std::to_string (countForVoice (events, VoiceId::Kick)));
    }

    // --- Probability = 0 suppresses all hits ---------------------------------
    {
        auto engine = makeEngine();
        auto v = allHits (4);
        v.probabilityPercent = 0.0f;
        disableAllExcept (engine, VoiceId::Kick, v);

        std::vector<TriggerEvent> events;
        engine.process (freeRun(), static_cast<int> (samplesPerStep * 8.0), events);

        PLYDRM_CHECK (countForVoice (events, VoiceId::Kick) == 0, "probability=0 should never fire");
    }

    // --- Following the host: steps sit on the host's 1/16 grid ---------------
    {
        auto engine = makeEngine();
        disableAllExcept (engine, VoiceId::Kick, allHits (4));

        // Playback starts at PPQ 0.1, between steps 0 (0.0) and 1 (0.25).
        std::vector<TriggerEvent> events;
        engine.process (playingAt (0.1), 4096, events);

        PLYDRM_CHECK (events.size() == 1, "expected one trigger, got " + std::to_string (events.size()));
        if (events.size() == 1)
            PLYDRM_CHECK (events[0].sampleOffset == 3600,
                          "step 1 (PPQ 0.25) should land 0.15 beats in = 3600 samples, got " + std::to_string (events[0].sampleOffset));
    }

    // --- Stopped transport generates nothing ---------------------------------
    {
        auto engine = makeEngine();
        disableAllExcept (engine, VoiceId::Kick, allHits (4));

        const auto events = run (engine, 48000 * 2, [] (int64_t) { return 512; },
                                 [] (int64_t) { return stoppedAt (4.0); });
        PLYDRM_CHECK (events.empty(), "a stopped host should produce no triggers, got " + std::to_string (events.size()));
    }

    // --- Pressing play again restarts the pattern from the host position ----
    {
        auto engine = makeEngine();
        auto v = allHits (16);
        v.hits = 1; // only step 0 of 16 fires
        disableAllExcept (engine, VoiceId::Kick, v);

        // Play 1.5 bars from 0, stop for a while, then play from 0 again.
        const int64_t playLen = 48000 * 3;   // 6 beats
        const int64_t stopLen = 48000;
        const auto events = run (engine, playLen + stopLen + 4096, [] (int64_t) { return 512; },
                                 [&] (int64_t s)
                                 {
                                     if (s < playLen)           return playingAt (static_cast<double> (s) * ppqPerSample);
                                     if (s < playLen + stopLen) return stoppedAt (6.0);
                                     return playingAt (static_cast<double> (s - playLen - stopLen) * ppqPerSample);
                                 });

        // Bar 1 downbeat at 0, bar 2 downbeat at 4 beats = 96000, then the restart's downbeat.
        PLYDRM_CHECK (events.size() == 3, "expected three downbeats, got " + std::to_string (events.size()));
        if (events.size() == 3)
        {
            PLYDRM_CHECK (events[0].sample == 0, "first downbeat at sample 0");
            PLYDRM_CHECK (events[1].sample == 96000, "second downbeat at sample 96000, got " + std::to_string (events[1].sample));
            PLYDRM_CHECK (events[2].sample == playLen + stopLen,
                          "restart downbeat at the moment play is pressed, got " + std::to_string (events[2].sample));
        }
    }

    // --- Swing delays the shared clock's odd steps, whatever the voice length
    {
        auto engine = makeEngine();
        auto v = allHits (3);
        v.swingPercent = 50.0f;
        disableAllExcept (engine, VoiceId::Kick, v);

        const auto events = run (engine, static_cast<int64_t> (samplesPerStep * 9.0), [] (int64_t) { return 512; },
                                 [] (int64_t) { return freeRun(); });

        PLYDRM_CHECK (events.size() == 9, "expected 9 triggers, got " + std::to_string (events.size()));
        for (auto& e : events)
        {
            const int64_t step = e.sample / 6000;
            const int64_t delay = e.sample % 6000;
            const int64_t expected = (step % 2 == 1) ? 1500 : 0; // 6000 * 50% * 0.5
            PLYDRM_CHECK (delay == expected, "step " + std::to_string (step) + " should be delayed by "
                                                 + std::to_string (expected) + ", got " + std::to_string (delay));
        }
    }

    // --- Negative PPQ (pre-roll / count-in) works and keeps the grid ---------
    {
        auto engine = makeEngine();
        auto v = allHits (3);
        v.everyN = 2;
        disableAllExcept (engine, VoiceId::Kick, v);

        // From PPQ -1 to +1: steps -4..3. Loops are -6..-4 (repeat -2, fires), -3..-1 (repeat -1, skipped),
        // 0..2 (repeat 0, fires) and 3..5 (repeat 1, skipped): steps -4, 0, 1 and 2 fire.
        const auto events = run (engine, 48000, [] (int64_t) { return 512; },
                                 [] (int64_t s) { return playingAt (-1.0 + static_cast<double> (s) * ppqPerSample); });

        PLYDRM_CHECK (events.size() == 4, "expected 4 triggers across the pre-roll, got " + std::to_string (events.size()));
        if (! events.empty())
            PLYDRM_CHECK (events[0].sample == 0, "step -4 sits exactly at PPQ -1 = sample 0");
    }

    // --- The result does not depend on how the host splits blocks ------------
    {
        auto pattern = [] (PatternEngine& engine)
        {
            // Humanize pulls hits up to 20 ms early, across block boundaries.
            auto kick = allHits (5); kick.hits = 3; kick.swingPercent = 30.0f; kick.humanizeMs = 20.0f;
            auto hat = allHits (7);  hat.hits = 4; hat.rotation = 2; hat.humanizeMs = 20.0f;
            engine.setVoiceParams (VoiceId::Kick, kick);
            engine.setVoiceParams (VoiceId::Snare, hat);
            VoiceParams off; off.enabled = false;
            engine.setVoiceParams (VoiceId::Hat, off);
            engine.setVoiceParams (VoiceId::Perc, off);
        };

        const int64_t total = 48000 * 4;
        const auto following = [] (int64_t s) { return playingAt (0.5 + static_cast<double> (s) * ppqPerSample); };

        auto a = makeEngine(); pattern (a);
        auto b = makeEngine(); pattern (b);
        auto c = makeEngine(); pattern (c);

        const auto fixed = run (a, total, [] (int64_t) { return 512; }, following);
        const auto odd = run (b, total, [] (int64_t s) { return 1 + static_cast<int> ((s * 7919) % 997); }, following);
        const auto tiny = run (c, total, [] (int64_t) { return 1; }, following);

        PLYDRM_CHECK (! fixed.empty(), "the pattern should fire");
        PLYDRM_CHECK (fixed == odd, "uneven block sizes should give the same triggers as 512-sample blocks");
        PLYDRM_CHECK (fixed == tiny, "1-sample blocks should give the same triggers as 512-sample blocks");
    }

    // --- The seed: same seed, same hits; another seed, other hits -------------
    {
        auto randomish = [] (PatternEngine& engine)
        {
            for (int i = 0; i < numVoices; ++i)
            {
                auto v = allHits (8);
                v.probabilityPercent = 50.0f;
                v.humanizeMs = 10.0f;
                engine.setVoiceParams (static_cast<VoiceId> (i), v);
            }
        };
        const auto playing = [] (int64_t s) { return playingAt (static_cast<double> (s) * ppqPerSample); };
        const auto blocks = [] (int64_t) { return 512; };

        auto a = makeEngine(); randomish (a); a.setSeed (99);
        auto b = makeEngine(); randomish (b); b.setSeed (99);
        auto c = makeEngine(); randomish (c); c.setSeed (100);
        const auto ra = run (a, 48000 * 4, blocks, playing);
        PLYDRM_CHECK (ra == run (b, 48000 * 4, blocks, playing), "the same seed should give the same hits");
        PLYDRM_CHECK (ra != run (c, 48000 * 4, blocks, playing), "another seed should give other hits");
    }

    // --- Starting mid-song gives the same hits as playing through to there ----
    {
        auto randomish = [] (PatternEngine& engine)
        {
            auto kick = allHits (5); kick.probabilityPercent = 60.0f; kick.humanizeMs = 15.0f; kick.swingPercent = 20.0f;
            auto hat = allHits (7);  hat.hits = 5; hat.probabilityPercent = 40.0f; hat.everyN = 2;
            engine.setVoiceParams (VoiceId::Kick, kick);
            engine.setVoiceParams (VoiceId::Hat, hat);
            VoiceParams off; off.enabled = false;
            engine.setVoiceParams (VoiceId::Snare, off);
            engine.setVoiceParams (VoiceId::Perc, off);
        };

        // A plays from the start of the song for 16 beats; B starts at beat 8.
        auto a = makeEngine(); randomish (a); a.setSeed (5);
        auto b = makeEngine(); randomish (b); b.setSeed (5);
        const int64_t beat = 24000;
        const auto fromStart = run (a, 16 * beat, [] (int64_t) { return 512; },
                                    [] (int64_t s) { return playingAt (static_cast<double> (s) * ppqPerSample); });
        auto fromMiddle = run (b, 8 * beat, [] (int64_t) { return 333; },
                               [] (int64_t s) { return playingAt (8.0 + static_cast<double> (s) * ppqPerSample); });
        for (auto& t : fromMiddle)
            t.sample += 8 * beat;

        // Skip the first 30 ms after B starts: a hit humanized early there can't be played before playback began.
        const int64_t from = 8 * beat + 1440;
        std::vector<AbsoluteTrigger> expected, got;
        for (auto& t : fromStart)  if (t.sample >= from) expected.push_back (t);
        for (auto& t : fromMiddle) if (t.sample >= from) got.push_back (t);

        PLYDRM_CHECK (! expected.empty(), "the second half should have hits");
        PLYDRM_CHECK (got == expected, "starting at beat 8 should give the same hits as playing through ("
                                           + std::to_string (got.size()) + " vs " + std::to_string (expected.size()) + ")");
    }

    // --- Auto-evolve requests: shortly before every Nth bar line --------------
    {
        auto requestTimes = [] (int everyBars, bool playing)
        {
            auto engine = makeEngine();
            disableAllExcept (engine, VoiceId::Kick, allHits (4));
            engine.setEvolveInterval (everyBars);
            std::vector<int64_t> times;
            uint32_t seen = 0;
            std::vector<TriggerEvent> events;
            for (int64_t s = 0; s < 4 * 96000 + 4800; s += 512)    // 4 bars and a bit (a bar is 96000 samples)
            {
                events.clear();
                engine.process (playing ? playingAt (static_cast<double> (s) * ppqPerSample) : stoppedAt (0.0), 512, events);
                if (engine.getEvolveRequestCount() != seen)
                {
                    seen = engine.getEvolveRequestCount();
                    times.push_back (s);
                }
            }
            return times;
        };

        const auto every1 = requestTimes (1, true);
        PLYDRM_CHECK (every1.size() == 4, "every bar: 4 requests in 4 bars, got " + std::to_string (every1.size()));
        bool onTime = true;
        for (size_t i = 0; i < every1.size(); ++i)
        {
            // kEvolveLeadSeconds (0.15 s = 7200 samples) before bar line i + 1.
            const int64_t due = static_cast<int64_t> (i + 1) * 96000 - 7200;
            onTime = onTime && every1[i] <= due && every1[i] + 512 > due;
        }
        PLYDRM_CHECK (onTime, "each request should come in the block 0.15 s before its bar line");
        PLYDRM_CHECK (requestTimes (2, true).size() == 2, "every 2 bars: 2 requests in 4 bars");
        PLYDRM_CHECK (requestTimes (0, true).empty(), "off: no requests");
        PLYDRM_CHECK (requestTimes (1, false).empty(), "stopped: no requests");
    }
}
