#pragma once

#include "Euclidean.h"
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
    This class is intentionally free of juce_audio_processors / audio-thread
    JUCE dependencies (beyond juce_core for Random) so it can be driven and
    unit tested outside of a real plugin host. */
class PatternEngine
{
public:
    PatternEngine();

    void prepareToPlay (double newSampleRate);

    /** Called once per block before process(), with the latest parameter
        values for one voice. */
    void setVoiceParams (VoiceId voice, const VoiceParams& params);

    /** Updates the shared tempo clock. quarterNoteStepLength is the step
        length expressed as a fraction of a quarter note (e.g. 0.25 = 1/16). */
    void setTempo (double bpmIn, double quarterNoteStepLength);

    /** Advances the clock by numSamples and appends any triggers that fall
        within this block to outTriggers (sampleOffset is relative to the
        start of this block). Safe to call with outTriggers non-empty; new
        events are appended. */
    void process (int numSamples, std::vector<TriggerEvent>& outTriggers);

    /** Re-seeds the per-voice RNGs used for probability rolls and
        micro-timing humanization. */
    void reseed (uint32_t newSeed);

    /** Resets playback position (e.g. on host transport restart) without
        touching cached patterns or RNG state. */
    void resetPosition();

    /** Returns the cached Euclidean pattern currently in use for a voice,
        mainly for GUI visualisation and unit tests. */
    const std::vector<bool>& getPatternForVoice (VoiceId voice) const;

private:
    struct VoiceRuntime
    {
        VoiceParams params;
        std::vector<bool> pattern;
        int cachedSteps = -1;
        int cachedHits = -1;
        int cachedRotation = -1;
        int64_t loopRepeatCount = 0;
        std::mt19937 rng { std::random_device {}() };
    };

    struct PendingEvent
    {
        int64_t fireSample;
        VoiceId voice;
        float velocity;
    };

    double sampleRate = 44100.0;
    double bpm = 120.0;
    double stepLengthQuarterNotes = 0.25;
    double samplesPerStep = 44100.0 * 0.5; // recomputed in setTempo
    double nextStepBoundary = 0.0;         // absolute sample position (double, for sub-sample accuracy)
    int64_t totalSamplesElapsed = 0;
    int64_t globalStepCounter = 0;

    std::array<VoiceRuntime, numVoices> voices;
    std::vector<PendingEvent> pending;

    void regeneratePatternIfNeeded (VoiceRuntime& v);
    void handleStepBoundary (int64_t stepIndex, int64_t boundarySample);
    void flushPending (int64_t blockStart, int64_t blockEnd, std::vector<TriggerEvent>& outTriggers);
};

} // namespace plydrm
