#include "TestMain.h"
#include "../Source/Synth/DrumSynth.h"
#include "../Source/Synth/OutputStage.h"
#include <cmath>
#include <cstring>
#include <vector>

using namespace plydrm;

namespace
{
    constexpr double sampleRate = 48000.0;

    // Mono render of one hit for `seconds`.
    std::vector<float> renderHit (VoiceId voice, float drive, cmfx::DriveType type, float pitch = 0.0f,
                                  double seconds = 1.0, uint32_t variation = 7)
    {
        DrumSynth synth;
        synth.prepare (sampleRate);
        synth.setDriveType (type);
        synth.noteOn (voice, 1.0f, pitch, drive, variation);

        std::vector<float> out (static_cast<size_t> (seconds * sampleRate), 0.0f);
        float* channels[] = { out.data() };
        synth.render (channels, 1, 0, static_cast<int> (out.size()));
        return out;
    }

    float peakOf (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
    {
        float p = 0.0f;
        for (size_t i = from; i < std::min (to, x.size()); ++i)
            p = std::max (p, std::abs (x[i]));
        return p;
    }

    bool allFinite (const std::vector<float>& x)
    {
        for (float v : x)
            if (! std::isfinite (v))
                return false;
        return true;
    }

    int zeroCrossings (const std::vector<float>& x, size_t from, size_t to)
    {
        int n = 0;
        for (size_t i = from + 1; i < to; ++i)
            n += (x[i - 1] < 0.0f) != (x[i] < 0.0f) ? 1 : 0;
        return n;
    }

    const char* voiceName (int v) { return voiceInfo[static_cast<size_t> (v)].displayName; }
}

void runSynthTests()
{
    // --- Every voice, every drive type: finite, bounded, and dies away -------
    for (int v = 0; v < numVoices; ++v)
        for (int t = 0; t < static_cast<int> (cmfx::DriveType::Count); ++t)
            for (float drive : { 0.0f, 0.5f, 1.0f })
            {
                const auto out = renderHit (static_cast<VoiceId> (v), drive, static_cast<cmfx::DriveType> (t));
                const std::string what = std::string (voiceName (v)) + " drive " + std::to_string (drive) + " type " + std::to_string (t);
                PLYDRM_CHECK (allFinite (out), what + ": output should be finite");
                PLYDRM_CHECK (peakOf (out) > 0.01f, what + ": should be audible");
                PLYDRM_CHECK (peakOf (out) < 2.0f, what + ": peak " + std::to_string (peakOf (out)));
                PLYDRM_CHECK (peakOf (out, out.size() - 4800) == 0.0f, what + ": should be silent by the last 100 ms");
            }

    // --- Drive changes the sound, drive 0 leaves it alone ---------------------
    {
        const auto clean = renderHit (VoiceId::Kick, 0.0f, cmfx::DriveType::Fuzz);
        const auto cleanSoft = renderHit (VoiceId::Kick, 0.0f, cmfx::DriveType::Soft);
        const auto driven = renderHit (VoiceId::Kick, 1.0f, cmfx::DriveType::Fuzz);
        PLYDRM_CHECK (clean == cleanSoft, "with drive 0 the drive type makes no difference");
        PLYDRM_CHECK (clean != driven, "drive should change the kick");
    }

    // --- Pitch: +12 semitones roughly doubles the kick's frequency -----------
    {
        const auto low = renderHit (VoiceId::Kick, 0.0f, cmfx::DriveType::Soft, 0.0f);
        const auto high = renderHit (VoiceId::Kick, 0.0f, cmfx::DriveType::Soft, 12.0f);
        const int zLow = zeroCrossings (low, 4800, 9600), zHigh = zeroCrossings (high, 4800, 9600);
        PLYDRM_CHECK (zHigh > zLow * 3 / 2 && zHigh < zLow * 5 / 2,
                      "+12 semitones should about double the kick's frequency (" + std::to_string (zLow) + " vs "
                          + std::to_string (zHigh) + " zero crossings)");
    }

    // --- Same variation, same hit; different variation, different hit ---------
    {
        PLYDRM_CHECK (renderHit (VoiceId::Snare, 0.0f, cmfx::DriveType::Soft, 0.0f, 0.5, 1)
                          == renderHit (VoiceId::Snare, 0.0f, cmfx::DriveType::Soft, 0.0f, 0.5, 1),
                      "the same variation should give the same snare");
        PLYDRM_CHECK (renderHit (VoiceId::Snare, 0.0f, cmfx::DriveType::Soft, 0.0f, 0.5, 1)
                          != renderHit (VoiceId::Snare, 0.0f, cmfx::DriveType::Soft, 0.0f, 0.5, 2),
                      "a different variation should give a different snare");
    }

    // --- Rendering in pieces gives the same samples as one call --------------
    {
        auto render = [] (int chunk)
        {
            DrumSynth synth;
            synth.prepare (sampleRate);
            for (int v = 0; v < numVoices; ++v)
                synth.noteOn (static_cast<VoiceId> (v), 0.8f, 0.0f, 0.3f, static_cast<uint32_t> (v + 3));
            std::vector<float> out (24000, 0.0f);
            float* channels[] = { out.data() };
            for (int start = 0; start < 24000; start += chunk)
                synth.render (channels, 1, start, std::min (chunk, 24000 - start));
            return out;
        };
        const auto whole = render (24000);
        PLYDRM_CHECK (render (1) == whole, "1-sample renders should match one long render");
        PLYDRM_CHECK (render (333) == whole, "333-sample renders should match one long render");
    }

    // --- Voice stealing: a burst of hits stays bounded and stops -------------
    {
        DrumSynth synth;
        synth.prepare (sampleRate);
        std::vector<float> out (48000, 0.0f);
        float* channels[] = { out.data() };
        for (int i = 0; i < 200; ++i)                  // 200 hits, 10 samples apart: far more than the pool holds
        {
            synth.noteOn (static_cast<VoiceId> (i % numVoices), 1.0f, 0.0f, 1.0f, static_cast<uint32_t> (i));
            synth.render (channels, 1, i * 10, 10);
        }
        synth.render (channels, 1, 2000, 46000);
        PLYDRM_CHECK (allFinite (out), "a burst of hits should stay finite");
        PLYDRM_CHECK (peakOf (out) < 48.0f, "a burst of hits should stay bounded, peak " + std::to_string (peakOf (out)));
        PLYDRM_CHECK (synth.activeHits() == 0, "every hit should have finished after a second");
    }

    // --- Output stage ------------------------------------------------------------
    {
        // Gain 1, limiter off: the samples are not touched.
        OutputStage stage;
        stage.prepare (sampleRate, 1.0f);
        stage.setLimiter (false);
        std::vector<float> x { 0.0f, 0.5f, -2.0f, 3.0e-39f, 1.0f };
        const auto before = x;
        float* channels[] = { x.data() };
        stage.process (channels, 1, static_cast<int> (x.size()));
        PLYDRM_CHECK (std::memcmp (x.data(), before.data(), x.size() * sizeof (float)) == 0,
                      "gain 1 with the limiter off should be bit-exact");
    }
    {
        // The limiter keeps anything within +-1 (it only reaches 1 itself once float rounding gets there), and is
        // transparent up to 0.9.
        OutputStage stage;
        stage.prepare (sampleRate, 1.0f);
        stage.setLimiter (true);
        std::vector<float> x { 0.5f, -0.9f, 1.0f, -5.0f, 1000.0f };
        float* channels[] = { x.data() };
        stage.process (channels, 1, static_cast<int> (x.size()));
        PLYDRM_CHECK (x[0] == 0.5f && x[1] == -0.9f, "the limiter should leave levels up to 0.9 alone");
        PLYDRM_CHECK (std::abs (x[2]) < 1.0f && std::abs (x[3]) <= 1.0f && std::abs (x[4]) <= 1.0f && x[4] > 0.99f,
                      "the limiter should keep peaks within 1");
    }
    {
        // A gain change glides (no jump) with a 20 ms time constant, and lands exactly on its target.
        OutputStage stage;
        stage.prepare (sampleRate, 1.0f);
        stage.setLimiter (false);
        stage.setGain (0.0f);
        std::vector<float> x (24000, 0.5f);
        float* channels[] = { x.data() };
        stage.process (channels, 1, static_cast<int> (x.size()));
        PLYDRM_CHECK (x[0] > 0.49f, "the first sample after a gain change should barely move, got " + std::to_string (x[0]));
        PLYDRM_CHECK (x[960] > 0.15f && x[960] < 0.22f, "one time constant (20 ms) in, the gain should be at about 1/e, got "
                                                          + std::to_string (x[960] / 0.5f));
        PLYDRM_CHECK (x.back() == 0.0f, "the gain should arrive exactly at its target");
    }
}
