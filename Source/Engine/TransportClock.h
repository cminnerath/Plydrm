#pragma once

// Ported from Brimstone (Source/engine/TransportClock.h).
//
// Once per block the plugin hands us whatever the host reported. We turn it
// into a musical clock (PPQ = quarter notes) that can be queried at any sample
// inside the block, and that keeps running on its own when the host gives no
// position (a standalone app, or a host without a transport).

#include <algorithm>
#include <cmath>

namespace plydrm
{

// Host info, decoupled from JUCE so the engine stays testable.
// Every field the host may omit has a has* flag.
struct HostTimeInfo
{
    bool   hasBpm = false;      double bpm = 120.0;
    bool   hasPpq = false;      double ppq = 0.0;
    bool   hasBarStart = false; double barStartPpq = 0.0;
    bool   hasTimeSig = false;  int    timeSigNum = 4, timeSigDen = 4;
    bool   isPlaying = false;
};

class TransportClock
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        bpm = 120.0;
        beatsPerBar = 4.0;
        blockStartPpq = nextBlockPpq = 0.0;
        barAnchorPpq = 0.0;
        followingHost = false;
        jumped = false;
    }

    void beginBlock (const HostTimeInfo& host, int numSamples) noexcept
    {
        if (host.hasBpm && host.bpm > 0.0)
            bpm = std::clamp (host.bpm, 10.0, 999.0);

        if (host.hasTimeSig && host.timeSigNum > 0 && host.timeSigDen > 0)
            beatsPerBar = host.timeSigNum * 4.0 / host.timeSigDen;

        if (host.isPlaying && host.hasPpq)
        {
            // Following the host. Flag discontinuities (arrangement loop, user
            // relocating the playhead).
            jumped = followingHost && std::abs (host.ppq - nextBlockPpq) > 1.0e-3;
            followingHost = true;
            blockStartPpq = host.ppq;
            barAnchorPpq  = host.hasBarStart ? host.barStartPpq
                                             : std::floor (host.ppq / beatsPerBar) * beatsPerBar;
        }
        else
        {
            // Stopped (or host gave no position): free-run from where we were.
            jumped = false;
            followingHost = false;
            blockStartPpq = nextBlockPpq;
        }

        ppqPerSample = bpm / (60.0 * sampleRate);
        nextBlockPpq = blockStartPpq + numSamples * ppqPerSample;
    }

    // PPQ at a sample offset inside the current block. Tempo is treated as
    // constant within a block (hosts only report it at block start).
    double ppqAt (int sampleOffset) const noexcept { return blockStartPpq + sampleOffset * ppqPerSample; }

    double getPpqPerSample() const noexcept { return ppqPerSample; }
    double samplesPerBeat() const noexcept { return sampleRate * 60.0 / bpm; }
    double getBpm() const noexcept         { return bpm; }
    double getBeatsPerBar() const noexcept { return beatsPerBar; }
    bool   isFollowingHost() const noexcept { return followingHost; }
    bool   jumpedThisBlock() const noexcept { return jumped; }

    // Most recent grid line at or before `ppq` for a division of `lengthBeats`.
    // The grid restarts at every bar line, so dotted values and odd meters
    // stay musically anchored to the bar rather than drifting across it.
    double gridLineAtOrBefore (double ppq, double lengthBeats) const noexcept
    {
        const double barStart = barAnchorPpq + std::floor ((ppq - barAnchorPpq) / beatsPerBar) * beatsPerBar;
        // Small epsilon so a position that is exactly on a grid line (but lost
        // a few ULPs in the PPQ math) is not pushed back a whole division.
        const double cells = std::floor ((ppq - barStart) / lengthBeats + 1.0e-9);
        return barStart + cells * lengthBeats;
    }

private:
    double sampleRate = 44100.0;
    double bpm = 120.0;
    double beatsPerBar = 4.0;
    double ppqPerSample = 0.0;
    double blockStartPpq = 0.0;
    double nextBlockPpq = 0.0;
    double barAnchorPpq = 0.0;
    bool   followingHost = false;
    bool   jumped = false;
};

} // namespace plydrm
