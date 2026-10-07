#pragma once

#include "../Params.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <random>

namespace plydrm
{
/** Simple synthesized drum voice engine: kick is a sine with a fast pitch
    envelope, snare/hat/perc are a blend of a short tone plus filtered noise.
    Every triggered hit randomizes its own pitch/decay/filter-cutoff slightly
    around the voice's configured value for an organic, non-looping feel. */
class DrumSynth
{
public:
    DrumSynth();

    void prepare (double sampleRate, int maxBlockSize);

    /** Re-seeds the per-hit variation and noise. Audio thread only. */
    void reseed (uint32_t seed) noexcept { rng.seed (seed); }

    /** Triggers one hit for a voice. pitchSemitones is the user's per-voice
        tuning offset; velocity is 0-1. */
    void noteOn (VoiceId voice, float velocity, float pitchSemitones);

    /** Renders numSamples starting at startSample into buffer, ADDING to
        (not overwriting) whatever is already there. */
    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

private:
    static constexpr int maxActiveHits = 24;

    enum class Kind { Kick, Snare, Hat, Perc };

    struct ActiveHit
    {
        bool active = false;
        Kind kind = Kind::Kick;
        double phase = 0.0;
        double startFreq = 100.0;
        double endFreq = 50.0;
        double freqEnvRate = 1.0;     // per-sample exponential decay rate for pitch envelope
        double ampEnv = 1.0;
        double ampDecayCoeff = 0.999; // per-sample multiplier
        float toneMix = 1.0f;         // 0 = pure noise, 1 = pure tone
        float velocity = 1.0f;
        double elapsedSamples = 0.0;
        juce::dsp::StateVariableTPTFilter<float> filter;
    };

    double sampleRate = 44100.0;
    std::array<ActiveHit, maxActiveHits> hits;
    std::mt19937 rng { std::random_device {}() };

    ActiveHit* allocateHit();
    float renderSample (ActiveHit& hit);
};

} // namespace plydrm
