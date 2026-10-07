#include "PatternEngine.h"
#include <algorithm>
#include <cmath>

namespace plydrm
{

namespace
{
    // Floor division and modulo that stay correct for negative step indices
    // (hosts report negative PPQ during pre-roll / count-in).
    int64_t floorDiv (int64_t a, int64_t b) noexcept
    {
        const int64_t q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    int64_t positiveMod (int64_t a, int64_t b) noexcept
    {
        const int64_t m = a % b;
        return m < 0 ? m + b : m;
    }

    // Tolerance for "is this step inside the block": steps exactly on a block
    // edge (give or take float error) belong to the block that starts there.
    constexpr double kPpqEpsilon = 1.0e-9;
}

PatternEngine::PatternEngine()
{
    for (int i = 0; i < numVoices; ++i)
        voices[static_cast<size_t> (i)].params = {};

    pending.reserve (kMaxPendingEvents);
}

void PatternEngine::prepareToPlay (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    resetPosition();
}

void PatternEngine::resetPosition()
{
    clock.prepare (sampleRate);
    wasFollowingHost = false;
    hasLastStep = false;
    totalSamplesElapsed = 0;
    pending.clear();
}

void PatternEngine::setStepLength (double quarterNotes)
{
    stepLengthQuarterNotes = quarterNotes > 0.0 ? quarterNotes : 0.25;
}

void PatternEngine::setVoiceParams (VoiceId voice, const VoiceParams& params)
{
    auto& v = voices[static_cast<size_t> (voice)];
    v.params = params;
    regeneratePatternIfNeeded (v);
}

void PatternEngine::regeneratePatternIfNeeded (VoiceRuntime& v)
{
    const int steps = std::clamp (v.params.steps, 1, kMaxPatternSteps);
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

const Pattern& PatternEngine::getPatternForVoice (VoiceId voice) const
{
    return voices[static_cast<size_t> (voice)].pattern;
}

void PatternEngine::handleStep (int64_t stepIndex, int64_t stepSample, int64_t blockStart, double samplesPerStep)
{
    for (int vi = 0; vi < numVoices; ++vi)
    {
        auto& v = voices[static_cast<size_t> (vi)];
        if (! v.params.enabled || v.pattern.length == 0)
            continue;

        const int64_t steps = v.pattern.length;
        const int64_t localStep = positiveMod (stepIndex, steps);
        const int64_t loopRepeat = floorDiv (stepIndex, steps);

        if (! v.pattern[static_cast<int> (localStep)])
            continue;

        const int64_t everyN = std::max (1, v.params.everyN);
        if (positiveMod (loopRepeat, everyN) != 0)
            continue;

        std::uniform_real_distribution<float> pct (0.0f, 100.0f);
        if (pct (v.rng) > v.params.probabilityPercent)
            continue;

        // Swing follows the shared clock, not the voice's own loop, so every
        // voice swings the same off-beats whatever its length.
        double delaySamples = 0.0;
        if (positiveMod (stepIndex, 2) == 1 && v.params.swingPercent > 0.0f)
            delaySamples = samplesPerStep * (static_cast<double> (v.params.swingPercent) / 100.0) * 0.5;

        double jitterSamples = 0.0;
        if (v.params.humanizeMs > 0.0f)
        {
            const float range = std::min (v.params.humanizeMs, kMaxHumanizeMs);
            std::uniform_real_distribution<float> jit (-range, range);
            jitterSamples = static_cast<double> (jit (v.rng)) / 1000.0 * sampleRate;
        }

        // Only a step right after playback (re)starts can be pulled before the
        // block it is found in; it plays at the start of that block instead.
        int64_t fireSample = stepSample + static_cast<int64_t> (std::llround (delaySamples + jitterSamples));
        fireSample = std::max (fireSample, blockStart);

        std::uniform_real_distribution<float> velRange (0.75f, 1.0f);
        const float velocity = velRange (v.rng);

        // Full queue: drop the hit rather than allocate on the audio thread.
        if (pending.size() >= static_cast<size_t> (kMaxPendingEvents))
            continue;

        pending.push_back ({ fireSample, static_cast<VoiceId> (vi), velocity });
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

void PatternEngine::process (const HostTimeInfo& host, int numSamples, std::vector<TriggerEvent>& outTriggers)
{
    if (numSamples <= 0)
        return;

    clock.beginBlock (host, numSamples);

    // The host has a transport and it is stopped: stay silent. (With no
    // position at all, e.g. standalone, the clock free-runs instead.)
    const bool hostStopped = host.hasPpq && ! host.isPlaying;
    running = ! hostStopped;
    currentStep = static_cast<int64_t> (std::floor (clock.ppqAt (0) / stepLengthQuarterNotes + kPpqEpsilon));

    // Playback started, stopped, or the playhead moved: anything scheduled
    // belongs to the old position, and step history no longer applies.
    const bool following = clock.isFollowingHost();
    if (hostStopped || clock.jumpedThisBlock() || following != wasFollowingHost)
    {
        pending.clear();
        hasLastStep = false;
    }
    wasFollowingHost = following;

    const int64_t blockStart = totalSamplesElapsed;
    const int64_t blockEnd = blockStart + numSamples;

    // Emit any events scheduled by previous blocks that land in this one.
    flushPending (blockStart, blockEnd, outTriggers);

    if (! hostStopped)
    {
        const double startPpq = clock.ppqAt (0);
        const double endPpq = clock.ppqAt (numSamples);
        const double ppqPerSample = clock.getPpqPerSample();
        const double stepLen = stepLengthQuarterNotes;
        const double samplesPerStep = stepLen / ppqPerSample;

        // Look ahead by the largest humanize offset (plus a sample for rounding),
        // so a hit humanized early is scheduled before its block is rendered.
        const double lookaheadSamples = std::ceil (static_cast<double> (kMaxHumanizeMs) * 0.001 * sampleRate) + 1.0;
        const double scheduleEndPpq = endPpq + lookaheadSamples * ppqPerSample;

        // Step k sits at PPQ k * stepLen. Take every step in [startPpq, scheduleEndPpq).
        for (auto k = static_cast<int64_t> (std::ceil (startPpq / stepLen - kPpqEpsilon)); ; ++k)
        {
            const double stepPpq = static_cast<double> (k) * stepLen;
            if (stepPpq >= scheduleEndPpq - kPpqEpsilon)
                break;

            // A step a few ULPs before a block edge can be seen by both blocks.
            if (hasLastStep && stepPpq <= lastStepPpq + kPpqEpsilon)
                continue;

            const double offset = std::max (0.0, (stepPpq - startPpq) / ppqPerSample);
            const int64_t stepSample = blockStart + static_cast<int64_t> (std::llround (offset));
            handleStep (k, stepSample, blockStart, samplesPerStep);

            lastStepPpq = stepPpq;
            hasLastStep = true;
        }
    }

    totalSamplesElapsed = blockEnd;

    // Newly generated events may already fall inside this block.
    flushPending (blockStart, blockEnd, outTriggers);

    std::sort (outTriggers.begin(), outTriggers.end(),
               [] (const TriggerEvent& a, const TriggerEvent& b) { return a.sampleOffset < b.sampleOffset; });
}

} // namespace plydrm
