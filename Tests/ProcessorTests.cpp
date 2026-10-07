// Drives the real PlydrmAudioProcessor (not just the engine) through
// processBlock with a fake play head, to check the JUCE glue: host position
// parsing, that synth hits start on their own sample (not at the start of the
// block they fall in), that processBlock never allocates, that no MIDI note is
// left hanging, and that New Seed is safe to press while audio runs.

#include "../Source/PluginProcessor.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

using namespace plydrm;

// ---- allocation counter (from Brimstone's SoakTests) ------------------------
// Counts every operator new / delete made by a thread while its `countingAllocs`
// is set. Only processBlock is wrapped in a CountScope, so the harness (and
// other threads) are free to allocate.

static thread_local bool countingAllocs = false;
static long allocCount = 0, freeCount = 0;

void* operator new (std::size_t n)
{
    if (countingAllocs)
        ++allocCount;
    if (void* p = std::malloc (n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete (void* p) noexcept { if (countingAllocs && p != nullptr) ++freeCount; std::free (p); }
void operator delete[] (void* p) noexcept { ::operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { ::operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { ::operator delete (p); }

struct CountScope
{
    CountScope() { countingAllocs = true; }
    ~CountScope() { countingAllocs = false; }
};

namespace
{
    constexpr double kSampleRate = 48000.0;      // 1/16 at 120 BPM = 6000 samples

    struct FakePlayHead final : juce::AudioPlayHead
    {
        juce::int64 samplePos = 0;
        double startPpq = 0.0;
        double bpm = 120.0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setTimeSignature (TimeSignature { 4, 4 });
            info.setIsPlaying (playing);
            const double ppq = startPpq + (double) samplePos * bpm / (60.0 * kSampleRate);
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

    struct MidiLog
    {
        std::vector<int64_t> onTimes, offTimes;
    };

    // Runs blocks of `blockSize` from sample `start`, logging MIDI note-ons and -offs at absolute sample times.
    void runMidi (PlydrmAudioProcessor& p, FakePlayHead& ph, int64_t start, int64_t total, int blockSize, MidiLog& log)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int64_t s = start; s < start + total; s += blockSize)
        {
            midi.clear();
            ph.samplePos = s;
            p.processBlock (buffer, midi);
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn())
                    log.onTimes.push_back (s + meta.samplePosition);
                else if (msg.isNoteOff())
                    log.offTimes.push_back (s + meta.samplePosition);
            }
        }
    }

    // Only the kick, on every 1/16 step, MIDI out on.
    void kickMidiOnEveryStep (PlydrmAudioProcessor& p)
    {
        hatOnEveryStep (p);
        const auto& kick = voiceInfo[static_cast<size_t> (VoiceId::Kick)];
        const auto& hat = voiceInfo[static_cast<size_t> (VoiceId::Hat)];
        setParam (p, ParamIDs::voiceParam (hat, "enabled"), 0.0f);
        setParam (p, ParamIDs::voiceParam (kick, "enabled"), 1.0f);
        setParam (p, ParamIDs::voiceParam (kick, "steps"), 4.0f);
        setParam (p, ParamIDs::voiceParam (kick, "hits"), 4.0f);
        setParam (p, ParamIDs::midiOutEnabled, 1.0f);
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

    std::printf ("processBlock never allocates\n");
    {
        // Every voice on with humanize and swing, MIDI and synth on, and the
        // parameters that rebuild patterns changing every block, across
        // transport stops/starts, New Seed and odd block sizes.
        PlydrmAudioProcessor p;
        FakePlayHead ph;
        ph.bpm = 240.0;                         // several steps per large block
        p.setPlayHead (&ph);
        constexpr int maxBlock = 4096;
        p.prepareToPlay (kSampleRate, maxBlock);
        for (const auto& v : voiceInfo)
            setParam (p, ParamIDs::voiceParam (v, "swing"), 40.0f);
        setParam (p, ParamIDs::midiOutEnabled, 1.0f);

        juce::AudioBuffer<float> buffer (2, maxBlock);
        juce::MidiBuffer midi;
        midi.ensureSize (32768);

        // Block 0 (1 sample) is the uncounted warm-up, so it can't pre-grow anything.
        const int blockSizes[] = { 1, 4096, 333, 512, 64, 777, 2048 };
        const auto& kick = voiceInfo[static_cast<size_t> (VoiceId::Kick)];
        const auto& hat = voiceInfo[static_cast<size_t> (VoiceId::Hat)];
        bool finite = true;
        int64_t pos = 0;
        long allocsBefore = allocCount, freesBefore = freeCount;

        for (int block = 0; block < 3000; ++block)
        {
            const int n = blockSizes[block % 7];
            setParam (p, ParamIDs::voiceParam (kick, "hits"), static_cast<float> (block % 17));
            setParam (p, ParamIDs::voiceParam (hat, "steps"), static_cast<float> (1 + block % 32));
            setParam (p, ParamIDs::voiceParam (hat, "rotation"), static_cast<float> (block % 31));
            setParam (p, ParamIDs::density, static_cast<float> ((block * 7) % 101));
            setParam (p, ParamIDs::stepDivision, static_cast<float> ((block / 40) % 3));
            if (block % 7 == 0)
                p.randomizeSeed();
            if (block % 150 == 0)
                setParam (p, ParamIDs::midiOutEnabled, (block / 150) % 2 == 0 ? 1.0f : 0.0f);
            ph.playing = (block / 200) % 4 != 3;
            ph.samplePos = pos;

            buffer.setSize (2, n, false, false, true);
            midi.clear();

            if (block == 0)
            {
                p.processBlock (buffer, midi);   // warm-up block, not counted
                allocsBefore = allocCount;
                freesBefore = freeCount;
            }
            else
            {
                CountScope counting;
                p.processBlock (buffer, midi);
            }

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    finite = finite && std::isfinite (buffer.getSample (ch, i));
            pos += n;
        }

        check (allocCount - allocsBefore == 0, "no allocations in 2999 blocks (got " + juce::String (allocCount - allocsBefore) + ")");
        check (freeCount - freesBefore == 0, "no frees in 2999 blocks (got " + juce::String (freeCount - freesBefore) + ")");
        check (finite, "output stays finite");
    }

    std::printf ("no MIDI note is left hanging\n");
    {
        // Kick note-on at 24000 (step 4), in the block 23552..24063. MIDI Out is
        // switched off before its note-off (30 ms later) is due.
        PlydrmAudioProcessor p;
        kickMidiOnEveryStep (p);
        FakePlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kSampleRate, 512);

        MidiLog log;
        runMidi (p, ph, 0, 24064, 512, log);
        setParam (p, ParamIDs::midiOutEnabled, 0.0f);
        runMidi (p, ph, 24064, 48000, 512, log);

        check (! log.onTimes.empty() && log.onTimes.back() == 24000, "last note-on at 24000, just before MIDI Out goes off");
        check (! log.offTimes.empty() && log.offTimes.front() == 1440, "note-offs come 30 ms after their note-on (got "
                                                                          + juce::String (log.offTimes.empty() ? -1 : (int) log.offTimes.front()) + ")");
        check (log.offTimes.size() == log.onTimes.size(), "every note-on gets its note-off after MIDI Out is switched off ("
                                                             + juce::String ((int) log.onTimes.size()) + " on, "
                                                             + juce::String ((int) log.offTimes.size()) + " off)");
    }
    {
        // Same, but the host calls prepareToPlay again while the note is on.
        PlydrmAudioProcessor p;
        kickMidiOnEveryStep (p);
        FakePlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kSampleRate, 512);

        MidiLog before, after;
        runMidi (p, ph, 0, 24064, 512, before);
        p.prepareToPlay (kSampleRate, 512);
        runMidi (p, ph, 0, 512, 512, after);

        check (before.offTimes.size() + 1 == before.onTimes.size(), "one note still on when prepareToPlay is called");
        check (! after.offTimes.empty() && after.offTimes.front() == 0, "its note-off goes out at the start of the next block");
    }

    std::printf ("New Seed while audio runs\n");
    {
        // The message thread presses New Seed and reads the tempo display while
        // the audio thread runs. (Build with -fsanitize=thread to check for races.)
        PlydrmAudioProcessor p;
        FakePlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kSampleRate, 256);

        std::atomic<bool> done { false };
        std::thread gui ([&]
        {
            double sink = 0.0;
            while (! done.load())
            {
                p.randomizeSeed();
                sink += p.getDisplayBpm() + (p.hostIsPlaying() ? 1.0 : 0.0) + (p.hostReportsTempo() ? 1.0 : 0.0);
            }
            juce::ignoreUnused (sink);
        });

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        bool finite = true;
        for (int block = 0; block < 4000; ++block)
        {
            midi.clear();
            ph.samplePos = block * 256;
            p.processBlock (buffer, midi);
            for (int i = 0; i < 256; ++i)
                finite = finite && std::isfinite (buffer.getSample (0, i));
        }
        done = true;
        gui.join();

        check (finite, "output stays finite");
        check (std::abs (p.getDisplayBpm() - 120.0) < 1.0e-9 && p.hostReportsTempo() && p.hostIsPlaying(),
               "the GUI sees the host's tempo and play state");
    }

    if (failures == 0)
        std::printf ("All processor tests passed.\n");
    else
        std::printf ("%d processor test check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
