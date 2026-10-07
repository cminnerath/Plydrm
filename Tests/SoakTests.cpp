// Soak, fuzz, determinism and cost tests for the whole audio path (ported from
// Brimstone's SoakTests). Plydrm's synth uses JUCE, so unlike Brimstone these
// drive the real processor through processBlock with a fake host.
//
//   fuzz         random parameter changes, Evolve, New Seed, tempo changes and ramps, transport stops / starts / jumps /
//                loops, missing host fields and play heads, odd time signatures and re-prepare, at five sample rates and
//                block sizes from 1 to 4096, mono and stereo. The output must stay finite and bounded, MIDI must be well
//                formed with every note-on matched by a note-off, and NOTHING in processBlock may allocate or free.
//   determinism  the same events at the same sample times give bit-identical audio and MIDI whatever the block layout
//                (what an offline export does versus what a realtime session does).
//   cost         ns per sample for a few patches, and a generous ceiling on the worst.
//
// `SoakTests` runs a quick version (a few seconds, part of ctest); `SoakTests full` runs the long one.

#include "../Source/PluginProcessor.h"
#include "AllocCounter.h"
#include "FakeHost.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

using namespace plydrm;
using test::FakePlayHead;

// ---- bookkeeping -------------------------------------------------------------------------------------------------------

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++g_checks;                                                            \
        if (! (cond)) {                                                        \
            ++g_failures;                                                      \
            std::printf ("  FAIL %s:%d: ", __FILE__, __LINE__);                \
            std::printf (__VA_ARGS__);                                         \
            std::printf ("\n");                                                \
        }                                                                      \
    } while (0)

// splitmix64: small, fast, and the same sequence on every platform.
struct Rng
{
    uint64_t state;

    uint64_t next() noexcept
    {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    double uniform() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
    int range (int lo, int hi) noexcept { return lo + (int) (next() % (uint64_t) (hi - lo + 1)); }  // inclusive
    bool chance (double p) noexcept { return uniform() < p; }
};

static int randomBlockSize (Rng& r)
{
    static constexpr int powers[] = { 32, 64, 128, 256, 512, 1024, 2048, 4096 };
    static constexpr int odd[] = { 7, 97, 333, 441, 1031, 2909 };
    switch (r.range (0, 4))
    {
        case 0:  return r.range (1, 16);
        case 1:  return powers[r.range (0, 7)];
        case 2:  return r.range (1, 4096);
        case 3:  return odd[r.range (0, 5)];
        default: return 512;
    }
}

static void randomizeParam (PlydrmAudioProcessor& p, Rng& r)
{
    const auto& params = p.getParameters();
    params[r.range (0, params.size() - 1)]->setValueNotifyingHost ((float) r.uniform());
}

static constexpr int kMidiChannel = 10;

static bool isPlydrmNote (int note)
{
    for (const auto& v : voiceInfo)
        if (v.gmNote == note)
            return true;
    return false;
}

// ---- fuzz --------------------------------------------------------------------------------------------------------------

struct FuzzResult
{
    long blocks = 0;
    double peak = 0.0;
    long nonFinite = 0;
    long badMidi = 0;
    long notesOn = 0;
    int hanging = 0;
    long allocs = 0, frees = 0;
    long transportChanges = 0, tempoChanges = 0, missingHostInfo = 0, prepares = 0;   // how much of the host side was exercised
};

static FuzzResult fuzz (uint64_t seed, double sr, double seconds, bool mono)
{
    FuzzResult res;
    Rng r { seed };

    PlydrmAudioProcessor p;
    if (mono)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (p.setBusesLayout (layout), "mono layout refused");
    }

    constexpr int maxBlock = 4096;
    FakePlayHead ph;
    ph.sampleRate = sr;
    bool playHeadAttached = true;
    p.setPlayHead (&ph);
    p.prepareToPlay (sr, maxBlock);
    test::setParam (p.apvts, ParamIDs::midiOutEnabled, 1.0f);

    const int channels = p.getTotalNumOutputChannels();
    juce::AudioBuffer<float> buffer (channels, maxBlock);
    juce::MidiBuffer midi;
    midi.ensureSize (65536);
    std::array<int, 128> held {};       // note-ons minus note-offs per note

    auto runBlock = [&] (int n)
    {
        buffer.setSize (channels, n, false, false, true);
        midi.clear();
        {
            const long a = allocCount, f = freeCount;
            CountScope counting;
            p.processBlock (buffer, midi);
            res.allocs += allocCount - a;
            res.frees += freeCount - f;
        }
        ++res.blocks;

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float x = buffer.getSample (ch, i);
                if (! std::isfinite (x))
                    ++res.nonFinite;
                else
                    res.peak = std::max (res.peak, (double) std::abs (x));
            }

        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const bool ok = (msg.isNoteOn() || msg.isNoteOff()) && msg.getChannel() == kMidiChannel
                            && isPlydrmNote (msg.getNoteNumber()) && meta.samplePosition >= 0 && meta.samplePosition < n;
            if (! ok)
            {
                ++res.badMidi;
                continue;
            }
            if (msg.isNoteOn())
            {
                ++held[(size_t) msg.getNoteNumber()];
                ++res.notesOn;
            }
            else
            {
                --held[(size_t) msg.getNoteNumber()];
            }
        }

        // The host's clock moves on at whatever tempo it reports.
        if (ph.playing)
            ph.ppq += n * ph.bpm / (60.0 * sr);
    };

    static constexpr std::array<std::array<int, 2>, 7> timeSigs { { { 4, 4 }, { 3, 4 }, { 7, 8 }, { 5, 4 }, { 6, 8 }, { 15, 16 }, { 0, 4 } } };
    bool tempoRamp = false;

    const auto total = (int64_t) (seconds * sr);
    for (int64_t pos = 0; pos < total; )
    {
        const int n = (int) std::min<int64_t> (randomBlockSize (r), total - pos);

        // What a host and a user might do between two blocks.
        if (r.chance (0.05))
            for (int k = r.range (1, 3); k > 0; --k)
                randomizeParam (p, r);
        if (r.chance (0.002)) p.evolvePatterns();
        if (r.chance (0.005)) p.randomizeSeed();
        if (r.chance (0.01))  { ph.bpm = 20.0 + r.uniform() * 280.0; ++res.tempoChanges; }
        if (r.chance (0.01))  tempoRamp = ! tempoRamp;
        if (tempoRamp)        ph.bpm = std::clamp (ph.bpm * (1.0 + (r.uniform() - 0.5) * 0.02), 20.0, 300.0);
        if (r.chance (0.0005)) { ph.bpm = r.chance (0.5) ? 0.0 : 5000.0; ++res.tempoChanges; }   // garbage from a host
        if (r.chance (0.01))  { ph.playing = ! ph.playing; ++res.transportChanges; }
        if (r.chance (0.005)) { ph.ppq = -4.0 + r.uniform() * 1000.0; ++res.transportChanges; }              // relocate the playhead
        if (r.chance (0.005)) { ph.ppq = std::floor (ph.ppq / 4.0) * 4.0 - 8.0; ++res.transportChanges; }   // arrangement loop jumps back
        if (r.chance (0.003)) { ph.hasBpm = ! ph.hasBpm; ++res.missingHostInfo; }
        if (r.chance (0.003)) { ph.hasPpq = ! ph.hasPpq; ++res.missingHostInfo; }
        if (r.chance (0.003)) { ph.hasBarStart = ! ph.hasBarStart; ++res.missingHostInfo; }
        if (r.chance (0.003)) { ph.hasTimeSig = ! ph.hasTimeSig; ++res.missingHostInfo; }
        if (r.chance (0.003)) { ph.hasPosition = ! ph.hasPosition; ++res.missingHostInfo; }
        if (r.chance (0.003))
        {
            const auto& sig = timeSigs[(size_t) r.range (0, (int) timeSigs.size() - 1)];
            ph.timeSigNum = sig[0];
            ph.timeSigDen = sig[1];
        }
        if (r.chance (0.002))
        {
            playHeadAttached = ! playHeadAttached;
            p.setPlayHead (playHeadAttached ? &ph : nullptr);
            ++res.missingHostInfo;
        }
        if (r.chance (0.001))
        {
            p.releaseResources();
            p.prepareToPlay (sr, maxBlock);       // allocates by design: not counted
            ++res.prepares;
        }

        runBlock (n);
        pos += n;
    }

    // Drain: a sane host, playing, with MIDI out on, long enough for any note-off to go out.
    ph.hasPosition = ph.hasBpm = ph.hasPpq = ph.hasBarStart = ph.hasTimeSig = ph.playing = true;
    ph.bpm = 120.0;
    ph.timeSigNum = ph.timeSigDen = 4;
    p.setPlayHead (&ph);
    test::setParam (p.apvts, ParamIDs::midiOutEnabled, 1.0f);
    for (const auto& v : voiceInfo)
        test::setParam (p.apvts, ParamIDs::voiceParam (v, "enabled"), 0.0f);
    for (int k = 0; k < 64; ++k)
        runBlock (512);

    for (int h : held)
        res.hanging += std::abs (h);
    return res;
}

static void testFuzz (bool full)
{
    std::printf ("fuzz: random parameters, Evolve, New Seed, tempo, transport and host fields at five sample rates and block sizes 1-4096\n");

    const double rates[] = { 32000.0, 44100.0, 48000.0, 96000.0, 192000.0 };
    const int seeds = full ? 8 : 2;
    const double seconds = full ? 30.0 : 8.0;

    FuzzResult sum;
    int runs = 0;
    for (int s = 0; s < seeds; ++s)
        for (double sr : rates)
        {
            const bool mono = (runs % 2) == 1;
            const auto res = fuzz (0xC0FFEEull + (uint64_t) s * 7919u + (uint64_t) sr, sr, seconds, mono);
            ++runs;

            CHECK (res.nonFinite == 0, "seed %d at %.0f Hz (%s): %ld non-finite samples", s, sr, mono ? "mono" : "stereo", res.nonFinite);
            CHECK (res.peak < 64.0, "seed %d at %.0f Hz: peak %.2f", s, sr, res.peak);
            CHECK (res.badMidi == 0, "seed %d at %.0f Hz: %ld malformed MIDI events", s, sr, res.badMidi);
            CHECK (res.hanging == 0, "seed %d at %.0f Hz: %d notes left hanging", s, sr, res.hanging);
            CHECK (res.allocs == 0 && res.frees == 0, "seed %d at %.0f Hz: %ld allocations, %ld frees in processBlock",
                   s, sr, res.allocs, res.frees);

            sum.blocks += res.blocks;
            sum.peak = std::max (sum.peak, res.peak);
            sum.nonFinite += res.nonFinite;
            sum.badMidi += res.badMidi;
            sum.notesOn += res.notesOn;
            sum.hanging += res.hanging;
            sum.allocs += res.allocs;
            sum.transportChanges += res.transportChanges;
            sum.tempoChanges += res.tempoChanges;
            sum.missingHostInfo += res.missingHostInfo;
            sum.prepares += res.prepares;
        }

    std::printf ("  %d runs, %ld blocks, %ld MIDI notes, peak %.2f, non-finite %ld, bad MIDI %ld, hanging notes %d, allocations %ld\n",
                 runs, sum.blocks, sum.notesOn, sum.peak, sum.nonFinite, sum.badMidi, sum.hanging, sum.allocs);
    std::printf ("  host side: %ld transport stops/starts/jumps, %ld tempo changes, %ld missing-field toggles, %ld re-prepares\n",
                 sum.transportChanges, sum.tempoChanges, sum.missingHostInfo, sum.prepares);
    CHECK (sum.notesOn > 0, "the fuzz never produced a MIDI note");
}

// ---- determinism -------------------------------------------------------------------------------------------------------

struct Render
{
    std::vector<float> audio;                  // both channels, interleaved
    std::vector<std::array<int64_t, 3>> midi;  // absolute sample, status, note
};

struct FixedEvent
{
    int64_t time;
    enum Kind { Param, Seed, Stop, Play } kind;
    int index;
    float value;
};

static std::vector<FixedEvent> makeEvents (Rng& r, int numParams, int64_t total)
{
    std::vector<FixedEvent> events;
    for (int64_t t = (int64_t) (r.uniform() * 9000.0) + 1; t < total; t += r.range (2000, 24000))
    {
        const int roll = r.range (0, 19);
        if (roll < 16)      events.push_back ({ t, FixedEvent::Param, r.range (0, numParams - 1), (float) r.uniform() });
        else if (roll < 18) events.push_back ({ t, FixedEvent::Seed, r.range (0, 1 << 30), 0.0f });
        else if (roll < 19) events.push_back ({ t, FixedEvent::Stop, 0, 0.0f });
        else                events.push_back ({ t, FixedEvent::Play, 0, (float) r.range (-4, 64) * 0.25f });   // play from a new PPQ
    }
    return events;
}

// Renders `total` samples of one session. Events land on block boundaries in every layout (blocks are split at them), and
// the host reports exact positions, so the only thing that differs between layouts is where else the blocks are cut.
static Render renderFixed (uint64_t seed, double sr, int64_t total, const std::function<int (int64_t)>& blockSize)
{
    Rng r { seed };
    PlydrmAudioProcessor p;
    FakePlayHead ph;
    ph.sampleRate = sr;
    ph.bpm = 90.0 + r.uniform() * 90.0;
    p.setPlayHead (&ph);
    constexpr int maxBlock = 4096;
    p.prepareToPlay (sr, maxBlock);

    // A random patch with sound and MIDI on.
    const int numParams = p.getParameters().size();
    for (int k = 0; k < 40; ++k)
        randomizeParam (p, r);
    test::setParam (p.apvts, ParamIDs::synthEnabled, 1.0f);
    test::setParam (p.apvts, ParamIDs::midiOutEnabled, 1.0f);
    test::setParam (p.apvts, ParamIDs::masterGain, 0.9f);
    p.setSeed ((uint32_t) r.next());

    const auto events = makeEvents (r, numParams, total);

    Render out;
    out.audio.reserve ((size_t) total * 2);
    juce::AudioBuffer<float> buffer (2, maxBlock);
    juce::MidiBuffer midi;

    int64_t playStartSample = 0;
    double playStartPpq = 0.0;
    size_t nextEvent = 0;

    for (int64_t pos = 0; pos < total; )
    {
        while (nextEvent < events.size() && events[nextEvent].time == pos)
        {
            const auto& e = events[nextEvent++];
            switch (e.kind)
            {
                case FixedEvent::Param: p.getParameters()[e.index]->setValueNotifyingHost (e.value); break;
                case FixedEvent::Seed:  p.setSeed ((uint32_t) e.index); break;
                case FixedEvent::Stop:  ph.playing = false; break;
                case FixedEvent::Play:  ph.playing = true; playStartSample = pos; playStartPpq = e.value; break;
            }
        }

        int n = (int) std::min<int64_t> (blockSize (pos), total - pos);
        if (nextEvent < events.size())
            n = (int) std::min<int64_t> (n, events[nextEvent].time - pos);

        if (ph.playing)
            ph.ppq = playStartPpq + (double) (pos - playStartSample) * ph.bpm / (60.0 * sr);

        buffer.setSize (2, n, false, false, true);
        midi.clear();
        p.processBlock (buffer, midi);

        for (int i = 0; i < n; ++i)
        {
            out.audio.push_back (buffer.getSample (0, i));
            out.audio.push_back (buffer.getSample (1, i));
        }
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            out.midi.push_back ({ pos + meta.samplePosition, msg.getRawData()[0], msg.getNoteNumber() });
        }
        pos += n;
    }
    return out;
}

static void testDeterminism (bool full)
{
    std::printf ("determinism: the same events at the same sample times give bit-identical audio and MIDI whatever the block layout\n");

    const int seeds = full ? 20 : 3;
    const double seconds = full ? 12.0 : 4.0;

    struct Layout { const char* name; std::function<int (int64_t)> size; };
    const Layout layouts[] = {
        { "1",      [] (int64_t) { return 1; } },
        { "64",     [] (int64_t) { return 64; } },
        { "333",    [] (int64_t) { return 333; } },
        { "4096",   [] (int64_t) { return 4096; } },
        { "random", [] (int64_t pos) { return 1 + (int) (((uint64_t) pos * 2654435761ull) % 2048); } },
    };

    int compared = 0, audioMismatches = 0, midiMismatches = 0;
    for (int s = 0; s < seeds; ++s)
    {
        const double sr = (s % 2 == 0) ? 48000.0 : 44100.0;
        const auto total = (int64_t) (seconds * sr);
        const uint64_t seed = 0xD1CEull + (uint64_t) s * 104729u;
        const auto ref = renderFixed (seed, sr, total, [] (int64_t) { return 512; });

        for (const auto& layout : layouts)
        {
            const auto got = renderFixed (seed, sr, total, layout.size);
            ++compared;

            if (got.midi != ref.midi)
            {
                ++midiMismatches;
                size_t i = 0;
                while (i < got.midi.size() && i < ref.midi.size() && got.midi[i] == ref.midi[i])
                    ++i;
                CHECK (false, "seed %d, layout %s: MIDI differs from 512-sample blocks at event %zu (%zu vs %zu events)",
                       s, layout.name, i, got.midi.size(), ref.midi.size());
            }

            if (got.audio.size() != ref.audio.size()
                || std::memcmp (got.audio.data(), ref.audio.data(), ref.audio.size() * sizeof (float)) != 0)
            {
                ++audioMismatches;
                size_t i = 0;
                while (i < got.audio.size() && i < ref.audio.size() && std::memcmp (&got.audio[i], &ref.audio[i], sizeof (float)) == 0)
                    ++i;
                CHECK (false, "seed %d, layout %s: audio differs from 512-sample blocks from sample %zu", s, layout.name, i / 2);
            }
        }
    }

    std::printf ("  %d seeds x %d layouts: %d with different audio, %d with different MIDI\n",
                 seeds, (int) std::size (layouts), audioMismatches, midiMismatches);
    juce::ignoreUnused (compared);
}

// ---- cost --------------------------------------------------------------------------------------------------------------

struct CostCase
{
    const char* name;
    std::function<void (PlydrmAudioProcessor&)> setup;
    double bpm;
};

static double nsPerSample (const CostCase& c, double sr)
{
    constexpr int blockSize = 512;
    const int blocks = (int) (2.0 * sr / blockSize);     // 2 s of audio
    double best = 1.0e30;

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        PlydrmAudioProcessor p;
        FakePlayHead ph;
        ph.sampleRate = sr;
        ph.bpm = c.bpm;
        p.setPlayHead (&ph);
        p.prepareToPlay (sr, blockSize);
        c.setup (p);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.ensureSize (65536);

        const auto start = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            ph.moveTo ((juce::int64) b * blockSize);
            midi.clear();
            p.processBlock (buffer, midi);
        }
        const double ns = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count();
        best = std::min (best, ns / ((double) blocks * blockSize));
    }
    return best;
}

static void testCost (bool)
{
    std::printf ("cost: nanoseconds per sample (stereo, 48 kHz, 512-sample blocks, best of 3); %% = share of one core in real time\n");

    auto allVoices = [] (PlydrmAudioProcessor& p, int steps, int hits)
    {
        for (const auto& v : voiceInfo)
        {
            test::setParam (p.apvts, ParamIDs::voiceParam (v, "enabled"), 1.0f);
            test::setParam (p.apvts, ParamIDs::voiceParam (v, "steps"), (float) steps);
            test::setParam (p.apvts, ParamIDs::voiceParam (v, "hits"), (float) hits);
        }
    };

    const CostCase cases[] = {
        { "idle (every voice off)", [] (PlydrmAudioProcessor& p) {
              for (const auto& v : voiceInfo)
                  test::setParam (p.apvts, ParamIDs::voiceParam (v, "enabled"), 0.0f);
          }, 120.0 },
        { "default patch", [] (PlydrmAudioProcessor&) {}, 120.0 },
        { "MIDI out only (synth off)", [] (PlydrmAudioProcessor& p) {
              test::setParam (p.apvts, ParamIDs::synthEnabled, 0.0f);
          }, 120.0 },
        { "dense: every voice, every 1/32 at 240 BPM", [allVoices] (PlydrmAudioProcessor& p) {
              allVoices (p, 32, 32);
              test::setParam (p.apvts, ParamIDs::stepDivision, 2.0f);
              test::setParam (p.apvts, ParamIDs::density, 50.0f);
          }, 240.0 },
    };

    double worstPct = 0.0;
    for (const auto& c : cases)
    {
        const double ns = nsPerSample (c, 48000.0);
        const double pct = ns * 48000.0 / 1.0e9 * 100.0;
        worstPct = std::max (worstPct, pct);
        std::printf ("  %-44s %8.1f ns/sample   %5.2f %% of a core\n", c.name, ns, pct);
    }
    CHECK (worstPct < 20.0, "the most expensive patch takes %.1f %% of a core (ceiling 20 %%)", worstPct);
}

// ---- main --------------------------------------------------------------------------------------------------------------

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const bool full = argc > 1 && std::strcmp (argv[1], "full") == 0;

    std::printf ("soak tests (%s)\n", full ? "full" : "quick");
    testFuzz (full);
    testDeterminism (full);
    testCost (full);

    std::printf ("\n%d checks, %d failed\n", g_checks, g_failures);
    if (g_failures != 0)
    {
        std::printf ("Soak tests FAILED.\n");
        return 1;
    }
    std::printf ("All soak tests passed.\n");
    return 0;
}
