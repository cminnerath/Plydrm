#include "DrumSynth.h"
#include "../Engine/StepRandom.h"
#include <algorithm>
#include <cmath>

namespace plydrm
{

namespace
{
    // Per-sample multiplier that takes an exponential envelope down to -60 dB in `seconds`.
    double decayCoeff (double seconds, double sampleRate) noexcept
    {
        return std::exp (std::log (0.001) / (seconds * sampleRate));
    }
}

void DrumSynth::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    for (auto& h : hits)
    {
        h.active = false;
        h.filterState.reset();
    }
}

int DrumSynth::activeHits() const noexcept
{
    return static_cast<int> (std::count_if (hits.begin(), hits.end(), [] (const ActiveHit& h) { return h.active; }));
}

DrumSynth::ActiveHit* DrumSynth::allocateHit() noexcept
{
    for (auto& h : hits)
        if (! h.active)
            return &h;

    // Pool exhausted: steal the hit with the smallest remaining envelope.
    ActiveHit* quietest = &hits[0];
    for (auto& h : hits)
        if (h.ampEnv < quietest->ampEnv)
            quietest = &h;
    return quietest;
}

void DrumSynth::noteOn (VoiceId voice, float velocity, float pitchSemitones, float drive, uint32_t variation) noexcept
{
    auto* hit = allocateHit();

    // Everything random about this hit follows from `variation`.
    hit->noiseState = splitmix64 (variation);
    auto next01 = [hit]
    {
        hit->noiseState = splitmix64 (hit->noiseState);
        return static_cast<double> (unitFloat (hit->noiseState));
    };
    const double r1 = next01();
    const double r2 = next01();
    const double r3 = next01();

    const double pitchMult = std::pow (2.0, static_cast<double> (pitchSemitones) / 12.0);

    hit->active = true;
    hit->phase = 0.0;
    hit->pitchEnv = 1.0;
    hit->ampEnv = 1.0;
    hit->velocity = std::clamp (velocity, 0.0f, 1.0f);
    hit->drive = std::clamp (drive, 0.0f, 1.0f);
    hit->filterState.reset();

    // freqEnvRate: how fast (per second) the pitch glide decays.
    auto setPitchGlide = [hit, this] (double freqEnvRate) { hit->pitchEnvCoeff = std::exp (-freqEnvRate / sampleRate); };

    switch (voice)
    {
        case VoiceId::Kick:
            hit->kind = Kind::Kick;
            hit->startFreq = (140.0 + 40.0 * r1) * pitchMult;
            hit->endFreq = (45.0 + 10.0 * r2) * pitchMult;
            setPitchGlide (18.0 + 10.0 * r3);
            hit->ampDecayCoeff = decayCoeff (0.18 + 0.20 * r2, sampleRate);
            hit->toneMix = 1.0f;
            hit->filterOut = FilterOut::LowPass;     // tames the harmonics the pitch glide adds
            hit->filter.set (sampleRate, 800.0, 0.3);
            break;

        case VoiceId::Snare:
            hit->kind = Kind::Snare;
            hit->startFreq = (190.0 + 40.0 * r1) * pitchMult;
            hit->endFreq = hit->startFreq * 0.85;
            setPitchGlide (40.0);
            hit->ampDecayCoeff = decayCoeff (0.10 + 0.12 * r2, sampleRate);
            hit->toneMix = 0.35f;
            hit->filterOut = FilterOut::BandPass;
            hit->filter.set (sampleRate, 1600.0 + 800.0 * r3, 0.6);
            break;

        case VoiceId::Hat:
            hit->kind = Kind::Hat;
            hit->startFreq = 400.0 * pitchMult;
            hit->endFreq = hit->startFreq;
            setPitchGlide (0.0);
            hit->ampDecayCoeff = decayCoeff (0.03 + 0.06 * r2, sampleRate);
            hit->toneMix = 0.0f;
            hit->filterOut = FilterOut::HighPass;
            hit->filter.set (sampleRate, 6000.0 + 3000.0 * r3, 0.4);
            break;

        case VoiceId::Perc:
        case VoiceId::Count:
            hit->kind = Kind::Perc;
            hit->startFreq = (300.0 + 200.0 * r1) * pitchMult;
            hit->endFreq = hit->startFreq * 0.6;
            setPitchGlide (25.0);
            hit->ampDecayCoeff = decayCoeff (0.07 + 0.10 * r2, sampleRate);
            hit->toneMix = 0.6f;
            hit->filterOut = FilterOut::BandPass;
            hit->filter.set (sampleRate, 900.0 + 1500.0 * r3, 0.5);
            break;
    }
}

float DrumSynth::renderSample (ActiveHit& hit) noexcept
{
    // Pitch envelope: exponential glide from startFreq to endFreq.
    const double freq = hit.endFreq + (hit.startFreq - hit.endFreq) * hit.pitchEnv;
    hit.pitchEnv *= hit.pitchEnvCoeff;

    hit.phase += freq / sampleRate;
    if (hit.phase >= 1.0)
        hit.phase -= 1.0;

    const double tone = std::sin (cmfx::kTwoPi * hit.phase);

    double lp, bp, hp;
    double dry;

    if (hit.kind == Kind::Kick)
    {
        // The kick is all tone, through its own low-pass.
        hit.filterState.process (hit.filter, tone, lp, bp, hp);
        dry = lp;
    }
    else
    {
        hit.noiseState = splitmix64 (hit.noiseState);
        const double white = 2.0 * static_cast<double> (unitFloat (hit.noiseState)) - 1.0;
        hit.filterState.process (hit.filter, white, lp, bp, hp);
        const double noise = hit.filterOut == FilterOut::HighPass ? hp
                           : hit.filterOut == FilterOut::BandPass ? bp
                                                                  : lp;
        dry = hit.toneMix * tone + (1.0 - hit.toneMix) * noise;
    }

    if (hit.drive > 0.0f)
        dry = cmfx::driveSample (driveType, dry, hit.drive);

    const float out = static_cast<float> (dry * hit.ampEnv) * hit.velocity;

    hit.ampEnv *= hit.ampDecayCoeff;
    if (hit.ampEnv < 0.0005)
        hit.active = false;

    return out;
}

void DrumSynth::render (float* const* channels, int numChannels, int startSample, int numSamples) noexcept
{
    if (std::none_of (hits.begin(), hits.end(), [] (const ActiveHit& h) { return h.active; }))
        return;

    // Sample by sample, every hit in turn (each hit has its own noise
    // generator, so the output does not depend on how the block is split).
    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        float s = 0.0f;
        for (auto& hit : hits)
            if (hit.active)
                s += renderSample (hit);

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] += s;
    }
}

} // namespace plydrm
