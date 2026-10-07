#include "PatternEngine.h"
#include <algorithm>
#include <cmath>

namespace plydrm
{

PatternEngine::PatternEngine()
{
    for (int i = 0; i < numVoices; ++i)
        voices[static_cast<size_t> (i)].params = {};
}

void PatternEngine::prepareToPlay (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    setTempo (bpm, stepLengthQuarterNotes);
    resetPosition();
}

void PatternEngine::resetPosition()
{
    totalSamplesElapsed = 0;
    globalStepCounter = 0;
    nextStepBoundary = samplesPerStep;
    pending.clear();
    for (auto& v : voices)
        v.loopRepeatCount = 0;
}

void PatternEngine::setTempo (double bpmIn, double quarterNoteStepLength)
{
    bpm = bpmIn > 1.0 ? bpmIn : 120.0;
    stepLengthQuarterNotes = quarterNoteStepLength > 0.0 ? quarterNoteStepLength : 0.25;

    const double samplesPerQuarterNote = sampleRate * 60.0 / bpm;
    samplesPerStep = samplesPerQuarterNote * stepLengthQuarterNotes;

    if (samplesPerStep < 1.0)
        samplesPerStep = 1.0;
}

void PatternEngine::setVoiceParams (VoiceId voice, const VoiceParams& params)
{
    auto& v = voices[static_cast<size_t> (voice)];
    v.params = params;
    regeneratePatternIfNeeded (v);
}

void PatternEngine::regeneratePatternIfNeeded (VoiceRuntime& v)
{
    const int steps = std::max (1, v.params.steps);
    const int hits = std::clamp (v.params.hits, 0, steps);
    const int rotation = v.params.rotation;

    if (steps == v.cachedSteps && hits == v.cachedHits && rotation == v.cachedRotation)
        return;

    v.pattern = rotatePattern (euclideanPattern (steps, hits), rotation);
    v.cachedSteps = steps;
    v.cachedHits = hits;
    v.cachedRotation = rotation;
}

void PatternEngine::reseed (uint32_t newSeed)
{
    for (size_t i = 0; i < voices.size(); ++i)
        voices[i].rng.seed (newSeed + static_cast<uint32_t> (i) * 7919u);
}

const std::vector<bool>& PatternEngine::getPatternForVoice (VoiceId voice) const
{
    return voices[static_cast<size_t> (voice)].pattern;
}

void PatternEngine::handleStepBoundary (int64_t stepIndex, int64_t boundarySample)
{
    for (int vi = 0; vi < numVoices; ++vi)
    {
        auto& v = voices[static_cast<size_t> (vi)];
        if (! v.params.enabled || v.pattern.empty())
            continue;

        const int steps = static_cast<int> (v.pattern.size());
        const int localStep = static_cast<int> (stepIndex % steps);
        const int64_t loopRepeat = stepIndex / steps;
        v.loopRepeatCount = loopRepeat;

        if (! v.pattern[static_cast<size_t> (localStep)])
            continue;

        const int everyN = std::max (1, v.params.everyN);
        if (loopRepeat % everyN != 0)
            continue;

        std::uniform_real_distribution<float> pct (0.0f, 100.0f);
        if (pct (v.rng) > v.params.probabilityPercent)
            continue;

        double delaySamples = 0.0;
        if ((localStep % 2) == 1 && v.params.swingPercent > 0.0f)
            delaySamples = samplesPerStep * (static_cast<double> (v.params.swingPercent) / 100.0) * 0.5;

        double jitterSamples = 0.0;
        if (v.params.humanizeMs > 0.0f)
        {
            std::uniform_real_distribution<float> jit (-v.params.humanizeMs, v.params.humanizeMs);
            jitterSamples = static_cast<double> (jit (v.rng)) / 1000.0 * sampleRate;
        }

        int64_t fireSample = boundarySample + static_cast<int64_t> (std::llround (delaySamples + jitterSamples));
        fireSample = std::max<int64_t> (fireSample, 0);

        std::uniform_real_distribution<float> velRange (0.75f, 1.0f);
        pending.push_back ({ fireSample, static_cast<VoiceId> (vi), velRange (v.rng) });
    }
}

void PatternEngine::flushPending (int64_t blockStart, int64_t blockEnd, std::vector<TriggerEvent>& outTriggers)
{
    const int64_t blockLen = std::max<int64_t> (1, blockEnd - blockStart);

    for (auto it = pending.begin(); it != pending.end(); )
    {
        if (it->fireSample < blockEnd)
        {
            int64_t offset = it->fireSample - blockStart;
            offset = std::clamp<int64_t> (offset, 0, blockLen - 1);
            outTriggers.push_back ({ it->voice, static_cast<int> (offset), it->velocity });
            it = pending.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

void PatternEngine::process (int numSamples, std::vector<TriggerEvent>& outTriggers)
{
    if (numSamples <= 0)
        return;

    const int64_t blockStart = totalSamplesElapsed;
    const int64_t blockEnd = blockStart + numSamples;

    // Emit any events scheduled by previous blocks that land in this one.
    flushPending (blockStart, blockEnd, outTriggers);

    // Step the shared clock forward, generating new trigger candidates for
    // every step boundary that falls within this block.
    while (nextStepBoundary < static_cast<double> (blockEnd))
    {
        const int64_t boundarySample = static_cast<int64_t> (std::llround (nextStepBoundary));
        handleStepBoundary (globalStepCounter, boundarySample);
        ++globalStepCounter;
        nextStepBoundary += samplesPerStep;
    }

    totalSamplesElapsed = blockEnd;

    // Newly generated events may already fall inside this block.
    flushPending (blockStart, blockEnd, outTriggers);

    std::sort (outTriggers.begin(), outTriggers.end(),
               [] (const TriggerEvent& a, const TriggerEvent& b) { return a.sampleOffset < b.sampleOffset; });
}

} // namespace plydrm
