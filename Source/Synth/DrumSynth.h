#pragma once

#include "../Engine/VoiceTypes.h"
#include "../cmfx/Common.h"
#include "../cmfx/Svf.h"
#include <array>
#include <cstdint>
#include <random>

namespace plydrm
{
/** Simple synthesized drum voice engine: kick is a sine with a fast pitch
    envelope through a low-pass, snare/hat/perc are a blend of a short tone
    plus filtered noise. Every triggered hit randomizes its own pitch/decay/
    filter-cutoff slightly around the voice's configured value for an
    organic, non-looping feel, and can be driven through one of cmfx's
    level-matched drive curves.

    Plain C++ (no JUCE), real-time safe after prepare(), and its output does
    not depend on how a block is split into render() calls. */
class DrumSynth
{
public:
    void prepare (double sampleRate) noexcept;

    /** Re-seeds the per-hit variation and noise. Audio thread only. */
    void reseed (uint32_t seed) noexcept { rng.seed (seed); }

    /** The drive curve every hit uses (from the Drive Type parameter). */
    void setDriveType (cmfx::DriveType type) noexcept { driveType = type; }

    /** Triggers one hit for a voice. velocity is 0-1, pitchSemitones the
        user's per-voice tuning offset, drive 0-1 (0 = clean). */
    void noteOn (VoiceId voice, float velocity, float pitchSemitones, float drive) noexcept;

    /** Renders numSamples starting at startSample into every channel, ADDING
        to (not overwriting) whatever is already there. */
    void render (float* const* channels, int numChannels, int startSample, int numSamples) noexcept;

    int activeHits() const noexcept;

private:
    static constexpr int maxActiveHits = 24;

    enum class Kind { Kick, Snare, Hat, Perc };
    enum class FilterOut { LowPass, BandPass, HighPass };

    struct ActiveHit
    {
        bool active = false;
        Kind kind = Kind::Kick;
        double phase = 0.0;
        double startFreq = 100.0;
        double endFreq = 50.0;
        double pitchEnv = 1.0;          // 1 -> 0: glide from startFreq to endFreq
        double pitchEnvCoeff = 0.999;   // per-sample multiplier
        double ampEnv = 1.0;
        double ampDecayCoeff = 0.999;   // per-sample multiplier
        float toneMix = 1.0f;           // 0 = pure noise, 1 = pure tone
        float velocity = 1.0f;
        float drive = 0.0f;
        FilterOut filterOut = FilterOut::LowPass;
        cmfx::SvfCoef filter;
        cmfx::SvfState filterState;
    };

    double sampleRate = 44100.0;
    cmfx::DriveType driveType = cmfx::DriveType::Soft;
    std::array<ActiveHit, maxActiveHits> hits;
    std::mt19937 rng { std::random_device {}() };

    ActiveHit* allocateHit() noexcept;
    float renderSample (ActiveHit& hit) noexcept;
};

} // namespace plydrm
