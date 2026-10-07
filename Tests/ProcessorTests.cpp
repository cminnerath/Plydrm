// Drives the real PlydrmAudioProcessor (not just the engine) through
// processBlock with a fake play head, to check the JUCE glue: host position
// parsing and that synth hits start on their own sample, not at the start of
// the block they fall in.

#include "../Source/PluginProcessor.h"

#include <cstdio>
#include <vector>

using namespace plydrm;

namespace
{
    constexpr double kSampleRate = 48000.0;      // 1/16 at 120 BPM = 6000 samples

    struct FakePlayHead final : juce::AudioPlayHead
    {
        juce::int64 samplePos = 0;
        double startPpq = 0.0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (120.0);
            info.setTimeSignature (TimeSignature { 4, 4 });
            info.setIsPlaying (playing);
            const double ppq = startPpq + (double) samplePos / (kSampleRate * 0.5);
            info.setPpqPosition (ppq);
            info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            return info;
        }
    };

    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    void setParam (PlydrmAudioProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    // Only the hat, on every 1/16 step, with no humanize or swing. The hat
    // falls silent at most ~4750 samples after a hit (0.09 s decay to -60 dB,
    // cut off at -66 dB), so each hit is over before the next step at 6000.
    void hatOnEveryStep (PlydrmAudioProcessor& p)
    {
        for (const auto& v : voiceInfo)
        {
            const bool isHat = juce::String (v.id) == "hat";
            setParam (p, ParamIDs::voiceParam (v, "enabled"), isHat ? 1.0f : 0.0f);
            setParam (p, ParamIDs::voiceParam (v, "humanizeMs"), 0.0f);
            setParam (p, ParamIDs::voiceParam (v, "swing"), 0.0f);
            setParam (p, ParamIDs::voiceParam (v, "probability"), 100.0f);
        }
        const auto& hat = voiceInfo[static_cast<size_t> (VoiceId::Hat)];
        setParam (p, ParamIDs::voiceParam (hat, "steps"), 4.0f);
        setParam (p, ParamIDs::voiceParam (hat, "hits"), 4.0f);
        setParam (p, ParamIDs::density, 50.0f);     // hits x 1.0
        setParam (p, ParamIDs::midiOutEnabled, 0.0f);
        setParam (p, ParamIDs::synthEnabled, 1.0f);
    }

    // Runs `total` samples in blocks of `blockSize` and returns the left channel.
    std::vector<float> render (PlydrmAudioProcessor& p, FakePlayHead* ph, int total, int blockSize)
    {
        p.setPlayHead (ph);
        p.prepareToPlay (kSampleRate, blockSize);

        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int start = 0; start < total; start += blockSize)
        {
            const int n = std::min (blockSize, total - start);
            buffer.setSize (2, n, false, false, true);
            midi.clear();
            if (ph != nullptr)
                ph->samplePos = start;
            p.processBlock (buffer, midi);
            for (int i = 0; i < n; ++i)
                out.push_back (buffer.getSample (0, i));
        }
        return out;
    }

    // First sample at or after `from` that isn't silent, or -1.
    int onsetFrom (const std::vector<float>& x, int from)
    {
        for (int i = from; i < (int) x.size(); ++i)
            if (x[(size_t) i] != 0.0f)
                return i;
        return -1;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("synth hits start on their own sample\n");
    for (int blockSize : { 512, 333, 4096 })
    {
        // No play head: free-runs at 120 BPM, steps at 0, 6000, 12000 ...
        PlydrmAudioProcessor p;
        hatOnEveryStep (p);
        const auto out = render (p, nullptr, 20000, blockSize);

        check (onsetFrom (out, 0) == 0, juce::String ("step 0 sounds at sample 0, block size ") + juce::String (blockSize));
        check (onsetFrom (out, 5000) == 6000, "step 1 sounds at sample 6000, block size " + juce::String (blockSize)
                                                 + " (got " + juce::String (onsetFrom (out, 5000)) + ")");
        check (onsetFrom (out, 11000) == 12000, "step 2 sounds at sample 12000, block size " + juce::String (blockSize)
                                                   + " (got " + juce::String (onsetFrom (out, 11000)) + ")");
    }

    std::printf ("follows the host play head\n");
    {
        // Playback starts at PPQ 0.1: the next step (PPQ 0.25) is 0.15 beats = 3600 samples in.
        PlydrmAudioProcessor p;
        hatOnEveryStep (p);
        FakePlayHead ph;
        ph.startPpq = 0.1;
        const auto out = render (p, &ph, 8000, 512);
        check (onsetFrom (out, 0) == 3600, "first hit lands on the host's 1/16 grid at sample 3600 (got "
                                               + juce::String (onsetFrom (out, 0)) + ")");
    }
    {
        PlydrmAudioProcessor p;
        hatOnEveryStep (p);
        FakePlayHead ph;
        ph.playing = false;
        const auto out = render (p, &ph, 48000, 512);
        check (onsetFrom (out, 0) == -1, "silent while the host is stopped");
    }

    if (failures == 0)
        std::printf ("All processor tests passed.\n");
    else
        std::printf ("%d processor test check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
