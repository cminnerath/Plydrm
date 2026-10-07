#pragma once

// A scriptable host play head for driving the real processor in tests
// (ported from Brimstone's ProcessorTests, extended for fuzzing). Every field
// a real host may leave out can be switched off, and the whole position can
// be missing.

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace plydrm::test
{
struct FakePlayHead final : juce::AudioPlayHead
{
    bool hasPosition = true;     // false: getPosition() returns nothing at all

    bool hasBpm = true;          double bpm = 120.0;
    bool hasPpq = true;          double ppq = 0.0;
    bool hasBarStart = true;
    bool hasTimeSig = true;      int timeSigNum = 4, timeSigDen = 4;
    bool playing = true;

    /** Where playback started, for moveTo(). */
    double startPpq = 0.0;
    double sampleRate = 48000.0;

    /** Sets ppq for a block starting `samplePos` samples after playback
        started at startPpq, at a constant tempo. */
    void moveTo (juce::int64 samplePos) noexcept
    {
        ppq = startPpq + (double) samplePos * bpm / (60.0 * sampleRate);
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! hasPosition)
            return {};

        PositionInfo info;
        info.setIsPlaying (playing);
        if (hasBpm)
            info.setBpm (bpm);
        if (hasTimeSig)
            info.setTimeSignature (TimeSignature { timeSigNum, timeSigDen });
        if (hasPpq)
        {
            info.setPpqPosition (ppq);
            if (hasBarStart)
            {
                const double beatsPerBar = timeSigNum * 4.0 / timeSigDen;
                info.setPpqPositionOfLastBarStart (std::floor (ppq / beatsPerBar) * beatsPerBar);
            }
        }
        return info;
    }
};

/** Sets a parameter to a real (not normalised) value, as a host would. */
inline void setParam (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
{
    auto* param = state.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}
} // namespace plydrm::test
