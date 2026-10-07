#pragma once

#include <array>

namespace plydrm
{
/** Fixed set of drum voices for v1. Adding a voice later means extending
    this array, the GM note map, and the synth voice list - everything else
    (parameters, pattern engine, GUI rows) is generated from this list.

    Deliberately free of any JUCE dependency so the pattern-engine core
    (Engine/Euclidean.*, Engine/PatternEngine.*) can be built and unit
    tested as plain C++ with no audio/GUI modules linked in. */
enum class VoiceId
{
    Kick = 0,
    Snare,
    Hat,
    Perc,
    Count
};

static constexpr int numVoices = static_cast<int> (VoiceId::Count);

struct VoiceInfo
{
    const char* id;          // used as parameter-ID prefix
    const char* displayName;
    int gmNote;               // General-MIDI-ish drum map note number
    int defaultSteps;
    int defaultHits;
};

static constexpr std::array<VoiceInfo, numVoices> voiceInfo { {
    { "kick",  "Kick",  36, 16, 4 },
    { "snare", "Snare", 38, 16, 2 },
    { "hat",   "Hat",   42, 12, 7 },
    { "perc",  "Perc",  39, 10, 3 },
} };

} // namespace plydrm
