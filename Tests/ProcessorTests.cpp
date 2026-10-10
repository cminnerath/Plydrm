// Drives the real PlydrmAudioProcessor (not just the engine) through
// processBlock with a fake play head, to check the JUCE glue: host position
// parsing, that synth hits start on their own sample (not at the start of the
// block they fall in), that processBlock never allocates, that no MIDI note is
// left hanging, and that New Seed is safe to press while audio runs.

#include "../Source/PluginProcessor.h"
#include "AllocCounter.h"
#include "FakeHost.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace plydrm;
using test::FakePlayHead;

namespace
{
    constexpr double kSampleRate = 48000.0;      // 1/16 at 120 BPM = 6000 samples

    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    void setParam (PlydrmAudioProcessor& p, const juce::String& id, float value)
    {
        test::setParam (p.apvts, id, value);
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
                ph->moveTo (start);
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
            ph.moveTo (s);
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

    std::printf ("host requirements\n");
    {
        // Ableton Live refuses to open a VST3 instrument that has no MIDI (event) input bus:
        // "plugin has instrument category, but no valid event input bus".
        PlydrmAudioProcessor p;
        check (p.acceptsMidi(), "accepts MIDI (Live needs an event input on an instrument)");
        check (p.producesMidi(), "produces MIDI");
    #if defined (JucePlugin_WantsMidiInput)
        check (JucePlugin_WantsMidiInput != 0, "the plugin is built with a MIDI input (NEEDS_MIDI_INPUT)");
    #endif
    }

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
            // The perform pad: a finger sweeping, a ball, recording and playing, Complexity moving.
            setParam (p, ParamIDs::complexity, static_cast<float> ((block * 11) % 101));
            setParam (p, ParamIDs::padBall, (block / 300) % 2 == 0 ? 0.0f : 1.0f);
            p.getPadHost().setFinger (static_cast<float> (block % 50) / 50.0f, static_cast<float> (block % 37) / 37.0f,
                                      (block / 20) % 3 != 2);
            if (block % 500 == 100)
                p.getPadHost().command (cmperf::PadHost::Command::record);
            if (block % 500 == 400)
                p.getPadHost().command (cmperf::PadHost::Command::clear);
            if (block % 150 == 0)
                setParam (p, ParamIDs::midiOutEnabled, (block / 150) % 2 == 0 ? 1.0f : 0.0f);
            ph.playing = (block / 200) % 4 != 3;
            ph.moveTo (pos);

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
            ph.moveTo (block * 256);
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

    std::printf ("the seed is saved with the session\n");
    {
        // Render a session, save it, load it into a new instance: the same audio.
        auto renderFresh = [] (PlydrmAudioProcessor& p)
        {
            FakePlayHead ph;
            for (const auto& v : voiceInfo)
                setParam (p, ParamIDs::voiceParam (v, "probability"), 55.0f);
            return render (p, &ph, 48000 * 2, 512);
        };

        PlydrmAudioProcessor original;
        original.setSeed (123456789u);
        const auto before = renderFresh (original);

        juce::MemoryBlock saved;
        original.getStateInformation (saved);

        PlydrmAudioProcessor loaded;
        check (loaded.getSeed() != 123456789u, "a new instance starts with its own seed");
        loaded.setStateInformation (saved.getData(), (int) saved.getSize());
        check (loaded.getSeed() == 123456789u, "loading the session restores its seed");
        check (renderFresh (loaded) == before, "and the loaded session plays exactly the same");

        PlydrmAudioProcessor other;
        other.setSeed (987654321u);
        check (renderFresh (other) != before, "another seed plays differently");

        // A session saved before the seed was (no seed property) keeps the instance's own seed.
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        xml->removeAttribute ("seed");
        juce::MemoryBlock oldSession;
        juce::AudioProcessor::copyXmlToBinary (*xml, oldSession);
        PlydrmAudioProcessor fresh;
        const auto ownSeed = fresh.getSeed();
        fresh.setStateInformation (oldSession.getData(), (int) oldSession.getSize());
        check (fresh.getSeed() == ownSeed, "an old session without a seed keeps the instance's seed");
    }

    std::printf ("auto evolve\n");
    {
        // Every bar: the pattern parameters change once shortly before each bar line, and only then.
        PlydrmAudioProcessor p;
        FakePlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kSampleRate, 512);
        setParam (p, ParamIDs::autoEvolve, 1.0f);       // "1 bar"

        auto snapshot = [&p]
        {
            juce::String s;
            for (const auto& v : voiceInfo)
                for (const char* id : { "hits", "rotation", "probability", "swing", "everyN" })
                    s << p.apvts.getRawParameterValue (ParamIDs::voiceParam (v, id))->load() << " ";
            return s;
        };

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        std::vector<int64_t> changes;
        auto last = snapshot();
        for (int64_t s = 0; s < 96000 * 3 - 20000; s += 512)    // just under 3 bars
        {
            ph.moveTo (s);
            midi.clear();
            p.processBlock (buffer, midi);
            p.handleEvolveRequests();                            // what the message-thread timer does
            const auto now = snapshot();
            if (now != last)
                changes.push_back (s);
            last = now;
        }

        check (changes.size() == 2, "two changes in just under 3 bars (got " + juce::String ((int) changes.size()) + ")");
        if (changes.size() == 2)
            check (changes[0] > 96000 - 7200 - 512 && changes[0] < 96000 && changes[1] > 192000 - 7200 - 512 && changes[1] < 192000,
                   "each change lands in the 0.15 s before a bar line (at " + juce::String (changes[0]) + " and "
                       + juce::String (changes[1]) + ")");

        // Off: nothing changes.
        setParam (p, ParamIDs::autoEvolve, 0.0f);
        const auto still = snapshot();
        for (int64_t s = 96000 * 3; s < 96000 * 5; s += 512)
        {
            ph.moveTo (s);
            midi.clear();
            p.processBlock (buffer, midi);
            p.handleEvolveRequests();
        }
        check (snapshot() == still, "with Auto Evolve off nothing changes");
    }

    std::printf ("the perform pad: Density across, Complexity up\n");
    {
        auto lengths = [] (PlydrmAudioProcessor& p)
        {
            juce::String s;
            for (int v = 0; v < numVoices; ++v)
                s << p.getDisplayPattern (static_cast<VoiceId> (v)).length << " ";
            return s.trim();
        };
        auto run = [] (PlydrmAudioProcessor& p, FakePlayHead& ph, int64_t& pos, int blocks)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (int b = 0; b < blocks; ++b, pos += 512)
            {
                ph.moveTo (pos);
                midi.clear();
                p.processBlock (buffer, midi);
            }
        };

        PlydrmAudioProcessor p;
        FakePlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kSampleRate, 512);
        int64_t pos = 0;
        run (p, ph, pos, 4);
        check (lengths (p) == "16 16 12 10", "untouched, the voices play their own lengths (" + lengths (p) + ")");
        check (std::abs (p.getDisplayDensity() - 50.0f) < 1.0e-6f && p.getDisplayComplexity() == 0.0f,
               "untouched, the knobs are what is in effect");

        // Pad Hold on an untouched pad takes nothing over (it keeps what a touch gave).
        setParam (p, ParamIDs::padHold, 1.0f);
        run (p, ph, pos, 20);
        check (std::abs (p.getDisplayDensity() - 50.0f) < 1.0e-6f && p.getDisplayComplexity() == 0.0f,
               "Pad Hold on an untouched pad changes nothing");
        setParam (p, ParamIDs::padHold, 0.0f);

        setParam (p, ParamIDs::complexity, 100.0f);
        run (p, ph, pos, 2);
        check (lengths (p) == "16 15 11 7", "the Complexity knob at 100 % gives 16 15 11 7 (" + lengths (p) + ")");
        setParam (p, ParamIDs::complexity, 0.0f);
        run (p, ph, pos, 2);

        // A finger in the top-right corner takes both macros to 100 %.
        auto& host = p.getPadHost();
        host.setFinger (1.0f, 1.0f, true);
        run (p, ph, pos, 20);
        check (p.getDisplayDensity() > 99.0f && p.getDisplayComplexity() > 99.0f,
               "a touch top right takes Density and Complexity to 100 % (" + juce::String (p.getDisplayDensity()) + ", "
                   + juce::String (p.getDisplayComplexity()) + ")");
        check (lengths (p) == "16 15 11 7", "and the loops shorten as the knob would");
        const auto kick = p.getDisplayPattern (VoiceId::Kick);
        int kickHits = 0;
        for (int k = 0; k < kick.length; ++k)
            kickHits += kick[k] ? 1 : 0;
        check (kickHits == 6, "Density 100 % gives the kick 1.6x its 4 hits (" + juce::String (kickHits) + ")");

        // The lift releases over Pad Release (0.5 s): a second later the knobs are back.
        host.setFinger (1.0f, 1.0f, false);
        run (p, ph, pos, 100);
        check (std::abs (p.getDisplayDensity() - 50.0f) < 0.01f && p.getDisplayComplexity() < 0.01f && lengths (p) == "16 16 12 10",
               "a second after the lift the knobs are back (" + juce::String (p.getDisplayDensity()) + ", "
                   + juce::String (p.getDisplayComplexity()) + ")");

        // With Pad Hold the pad keeps them.
        setParam (p, ParamIDs::padHold, 1.0f);
        host.setFinger (0.0f, 1.0f, true);
        run (p, ph, pos, 20);
        host.setFinger (0.0f, 1.0f, false);
        run (p, ph, pos, 100);
        check (p.getDisplayDensity() < 0.01f && p.getDisplayComplexity() > 99.99f, "with Pad Hold the pad keeps them after the lift");
        setParam (p, ParamIDs::padHold, 0.0f);
        run (p, ph, pos, 100);
        check (std::abs (p.getDisplayDensity() - 50.0f) < 0.01f, "Hold off gives the knobs back");
    }

    std::printf ("Pad Motion is saved with the session\n");
    {
        PlydrmAudioProcessor p;
        FakePlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (kSampleRate, 512);
        auto& host = p.getPadHost();
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        // Record one bar (2 s at 120 BPM) of a finger sweeping across the pad.
        host.command (cmperf::PadHost::Command::record);
        int64_t pos = 0;
        for (; pos < 96000 + 48000; pos += 512)
        {
            host.setFinger (static_cast<float> (pos % 96000) / 96000.0f, 0.5f, pos < 96000);
            ph.moveTo (pos);
            midi.clear();
            p.processBlock (buffer, midi);
        }
        check (host.display().motion == cmperf::PadHost::Motion::playing, "a recording closes itself after a bar and plays");
        p.handlePadGesture();

        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        check (xml != nullptr && xml->getChildByName ("PAD") != nullptr, "the session has a PAD child");

        PlydrmAudioProcessor loaded;
        FakePlayHead ph2;
        loaded.setPlayHead (&ph2);
        loaded.prepareToPlay (kSampleRate, 512);
        loaded.setStateInformation (saved.getData(), (int) saved.getSize());
        midi.clear();
        loaded.processBlock (buffer, midi);
        check (loaded.getPadHost().display().motion == cmperf::PadHost::Motion::playing, "the loaded session's loop plays");
        juce::MemoryBlock resaved;
        loaded.getStateInformation (resaved);
        check (resaved == saved, "and saves the same session again");

        // Clear forgets it, and the session then has no PAD child.
        host.command (cmperf::PadHost::Command::clear);
        midi.clear();
        p.processBlock (buffer, midi);
        p.handlePadGesture();
        juce::MemoryBlock cleared;
        p.getStateInformation (cleared);
        const auto clearedXml = juce::AudioProcessor::getXmlFromBinary (cleared.getData(), (int) cleared.getSize());
        check (clearedXml != nullptr && clearedXml->getChildByName ("PAD") == nullptr, "after Clear the session has no loop");

        // Loading a session without a loop empties a pad that had one.
        loaded.setStateInformation (cleared.getData(), (int) cleared.getSize());
        midi.clear();
        loaded.processBlock (buffer, midi);
        check (loaded.getPadHost().display().motion == cmperf::PadHost::Motion::idle, "a session without a loop leaves the pad empty");
    }

    if (failures == 0)
        std::printf ("All processor tests passed.\n");
    else
        std::printf ("%d processor test check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
