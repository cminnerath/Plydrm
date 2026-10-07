#pragma once

#include "Euclidean.h"
#include "TransportClock.h"
#include "VoiceTypes.h"
#include <array>
#include <vector>
#include <random>
#include <cstdint>

namespace plydrm
{
/** Immutable-per-block snapshot of one voice's pattern parameters. The
    engine recomputes its cached Euclidean pattern only when steps/hits/
    rotation actually change, so calling setVoiceParams() every block (with
    the latest APVTS values) is cheap. */
struct VoiceParams
{
    bool enabled = true;
    int steps = 16;
    int hits = 4;
    int rotation = 0;
    float probabilityPercent = 100.0f;   // chance a would-be hit actually fires
    int everyN = 1;                      // fire only every Nth time this voice's loop repeats
    float swingPercent = 0.0f;           // 0-75, delays odd-numbered steps
    float humanizeMs = 0.0f;             // +/- micro-timing jitter range
};

/** A single scheduled drum hit, timed in samples relative to the start of
    the audio block it is reported in. */
struct TriggerEvent
{
    VoiceId voice;
    int sampleOffset = 0;
    float velocity = 1.0f;
};

/** Sample-accurate, tempo-synced Euclidean/polymeter pattern generator.

    Step k sits at PPQ position k * stepLength, so the pattern is locked to
    the host's timeline: step 0 is the start of the song, every voice's loop
    position follows from k, and starting playback anywhere gives the same
    hits as playing through to that point. While the host transport is
    stopped nothing is generated; with no host position at all (standalone)
    the clock free-runs from 0.

    Plain C++ (no JUCE), so it can be driven and unit tested outside of a
    real plugin host. */
class PatternEngine
{
public:
    /** Most hits that can be scheduled ahead at once (swing / humanize
        delays carry hits into later blocks). Hits beyond this are dropped
        rather than growing the queue on the audio thread. process() appends
        at most 2 * kMaxPendingEvents triggers per call, so an outTriggers
        vector reserved to that size never reallocates. */
    static constexpr int kMaxPendingEvents = 256;

    /** Largest humanize offset (the humanize parameter's maximum). Steps are
        scheduled this far ahead of the block being processed, so a hit pulled
        early always lands in a block not yet rendered: where it lands never
        depends on how the host cuts its blocks. */
    static constexpr float kMaxHumanizeMs = 30.0f;

    PatternEngine();

    void prepareToPlay (double newSampleRate);

    /** Called once per block before process(), with the latest parameter
        values for one voice. */
    void setVoiceParams (VoiceId voice, const VoiceParams& params);

    /** Step length as a fraction of a quarter note (e.g. 0.25 = 1/16). */
    void setStepLength (double quarterNotes);

    /** Advances the clock by numSamples using what the host reported for
        this block, and appends any triggers that fall within the block to
        outTriggers (sampleOffset is relative to the start of this block). */
    void process (const HostTimeInfo& host, int numSamples, std::vector<TriggerEvent>& outTriggers);

    /** Re-seeds the per-voice RNGs used for probability rolls and
        micro-timing humanization. */
    void reseed (uint32_t newSeed);

    /** Forgets the playback position and anything scheduled, without
        touching cached patterns or RNG state. */
    void resetPosition();

    /** Returns the cached Euclidean pattern currently in use for a voice.
        Audio thread and tests only: the GUI must not read it while the
        audio thread may be rebuilding it. */
    const Pattern& getPatternForVoice (VoiceId voice) const;

    double getBpm() const noexcept { return clock.getBpm(); }

    /** The shared step at the start of the last processed block (step k is
        at PPQ k * step length), and whether the clock is running (false
        while the host is stopped). For display. */
    int64_t getCurrentStep() const noexcept { return currentStep; }
    bool isRunning() const noexcept { return running; }

private:
    struct VoiceRuntime
    {
        VoiceParams params;
        Pattern pattern;
        int cachedSteps = -1;
        int cachedHits = -1;
        int cachedRotation = -1;
        std::mt19937 rng { std::random_device {}() };
    };

    struct PendingEvent
    {
        int64_t fireSample;
        VoiceId voice;
        float velocity;
    };

    double sampleRate = 44100.0;
    double stepLengthQuarterNotes = 0.25;
    TransportClock clock;
    bool wasFollowingHost = false;
    double lastStepPpq = 0.0;              // PPQ of the last step handled, so no step fires twice
    bool hasLastStep = false;
    int64_t totalSamplesElapsed = 0;
    int64_t currentStep = 0;
    bool running = false;

    std::array<VoiceRuntime, numVoices> voices;
    std::vector<PendingEvent> pending;

    void regeneratePatternIfNeeded (VoiceRuntime& v);
    void handleStep (int64_t stepIndex, int64_t stepSample, int64_t blockStart, double samplesPerStep);
    void flushPending (int64_t blockStart, int64_t blockEnd, std::vector<TriggerEvent>& outTriggers);
};

} // namespace plydrm
