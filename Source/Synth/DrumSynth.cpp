#include "DrumSynth.h"
#include <algorithm>
#include <cmath>

namespace plydrm
{

DrumSynth::DrumSynth() = default;

void DrumSynth::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (maxBlockSize), 1 };
    for (auto& h : hits)
    {
        h.filter.prepare (spec);
        h.active = false;
    }
}

DrumSynth::ActiveHit* DrumSynth::allocateHit()
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

void DrumSynth::noteOn (VoiceId voice, float velocity, float pitchSemitones)
{
    auto* hit = allocateHit();
    if (hit == nullptr)
        return;

    std::uniform_real_distribution<float> rand01 (0.0f, 1.0f);
    const float r1 = rand01 (rng);
    const float r2 = rand01 (rng);
    const float r3 = rand01 (rng);

    const double pitchMult = std::pow (2.0, static_cast<double> (pitchSemitones) / 12.0);

    hit->active = true;
    hit->phase = 0.0;
    hit->elapsedSamples = 0.0;
    hit->velocity = juce::jlimit (0.0f, 1.0f, velocity);
    hit->filter.reset();

    switch (voice)
    {
        case VoiceId::Kick:
        {
            hit->kind = Kind::Kick;
            hit->startFreq = (140.0 + 40.0 * r1) * pitchMult;
            hit->endFreq = (45.0 + 10.0 * r2) * pitchMult;
            hit->freqEnvRate = 18.0 + 10.0 * r3;
            const double decaySeconds = 0.18 + 0.20 * r2;
            hit->ampDecayCoeff = std::exp (std::log (0.001) / (decaySeconds * sampleRate));
            hit->toneMix = 1.0f;
            hit->filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
            hit->filter.setCutoffFrequency (800.0f);
            hit->filter.setResonance (0.3f);
            break;
        }
        case VoiceId::Snare:
        {
            hit->kind = Kind::Snare;
            hit->startFreq = (190.0 + 40.0 * r1) * pitchMult;
            hit->endFreq = hit->startFreq * 0.85;
            hit->freqEnvRate = 40.0;
            const double decaySeconds = 0.10 + 0.12 * r2;
            hit->ampDecayCoeff = std::exp (std::log (0.001) / (decaySeconds * sampleRate));
            hit->toneMix = 0.35f;
            hit->filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
            hit->filter.setCutoffFrequency (1600.0f + 800.0f * r3);
            hit->filter.setResonance (0.6f);
            break;
        }
        case VoiceId::Hat:
        {
            hit->kind = Kind::Hat;
            hit->startFreq = 400.0 * pitchMult;
            hit->endFreq = hit->startFreq;
            hit->freqEnvRate = 0.0;
            const double decaySeconds = 0.03 + 0.06 * r2;
            hit->ampDecayCoeff = std::exp (std::log (0.001) / (decaySeconds * sampleRate));
            hit->toneMix = 0.0f;
            hit->filter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
            hit->filter.setCutoffFrequency (6000.0f + 3000.0f * r3);
            hit->filter.setResonance (0.4f);
            break;
        }
        case VoiceId::Perc:
        case VoiceId::Count:
        default:
        {
            hit->kind = Kind::Perc;
            hit->startFreq = (300.0 + 200.0 * r1) * pitchMult;
            hit->endFreq = hit->startFreq * 0.6;
            hit->freqEnvRate = 25.0;
            const double decaySeconds = 0.07 + 0.10 * r2;
            hit->ampDecayCoeff = std::exp (std::log (0.001) / (decaySeconds * sampleRate));
            hit->toneMix = 0.6f;
            hit->filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
            hit->filter.setCutoffFrequency (900.0f + 1500.0f * r3);
            hit->filter.setResonance (0.5f);
            break;
        }
    }

    hit->ampEnv = 1.0;
}

float DrumSynth::renderSample (ActiveHit& hit)
{
    // Pitch envelope: exponential glide from startFreq to endFreq.
    const double envProgress = std::exp (-hit.elapsedSamples * hit.freqEnvRate / sampleRate);
    const double freq = hit.endFreq + (hit.startFreq - hit.endFreq) * envProgress;

    hit.phase += freq / sampleRate;
    if (hit.phase >= 1.0)
        hit.phase -= 1.0;

    const float tone = static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * hit.phase));

    std::uniform_real_distribution<float> noiseDist (-1.0f, 1.0f);
    const float noiseRaw = noiseDist (rng);
    const float noiseFiltered = hit.filter.processSample (0, noiseRaw);

    float dry = hit.toneMix * tone + (1.0f - hit.toneMix) * noiseFiltered;

    // The kick's body also benefits from being pushed through its own
    // lowpass to tame harmonics introduced by the pitch envelope.
    if (hit.kind == Kind::Kick)
        dry = hit.filter.processSample (0, dry);

    const float out = dry * static_cast<float> (hit.ampEnv) * hit.velocity;

    hit.ampEnv *= hit.ampDecayCoeff;
    hit.elapsedSamples += 1.0;

    if (hit.ampEnv < 0.0005)
        hit.active = false;

    return out;
}

void DrumSynth::renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (std::none_of (hits.begin(), hits.end(), [] (const ActiveHit& h) { return h.active; }))
        return;

    const int numChannels = buffer.getNumChannels();

    // Sample by sample, every hit in turn: the noise generator is then drawn
    // in the same order however the block is split, so the output does not
    // depend on the host's block size.
    for (int i = 0; i < numSamples; ++i)
    {
        float s = 0.0f;
        for (auto& hit : hits)
            if (hit.active)
                s += renderSample (hit);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addSample (ch, startSample + i, s);
    }
}

} // namespace plydrm
