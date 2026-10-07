#pragma once

#include "Params.h"
#include "Engine/PatternEngine.h"
#include "Synth/DrumSynth.h"
#include "Synth/OutputStage.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>

namespace plydrm
{
/** Caches the atomic raw-value pointers for one voice's parameters so the
    audio thread never has to look them up by string ID. */
struct VoiceParamPointers
{
    std::atomic<float>* enabled = nullptr;
    std::atomic<float>* steps = nullptr;
    std::atomic<float>* hits = nullptr;
    std::atomic<float>* rotation = nullptr;
    std::atomic<float>* probability = nullptr;
    std::atomic<float>* everyN = nullptr;
    std::atomic<float>* swing = nullptr;
    std::atomic<float>* humanizeMs = nullptr;
    std::atomic<float>* level = nullptr;
    std::atomic<float>* pitch = nullptr;
    std::atomic<float>* drive = nullptr;
};

class PlydrmAudioProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
{
public:
    PlydrmAudioProcessor();
    ~PlydrmAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Plydrm"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Nudges every voice's hits/rotation/probability/swing (and sometimes
        everyN) by a small random amount - the "Evolve" GUI button, and what
        Auto Evolve does every N bars. Message thread only: each change goes
        through setValueNotifyingHost inside a change gesture, so the GUI
        knobs move and the host records it like a user edit. */
    void evolvePatterns();

    /** Runs evolvePatterns() if the audio thread has asked for an
        auto-evolve since the last call (at most once per call). A timer
        calls it on the message thread; tests call it directly. */
    void handleEvolveRequests();

    /** Picks a new random seed - the "New Seed" GUI button. */
    void randomizeSeed();

    /** Sets the seed every random value comes from (probability rolls,
        humanize, velocity and the synth's per-hit variation): the same seed
        gives the same hits at the same song position. It is saved with the
        session. Safe from the message thread: the audio thread picks it up
        at the start of its next block. */
    void setSeed (uint32_t seed) noexcept;
    uint32_t getSeed() const noexcept { return currentSeed.load (std::memory_order_relaxed); }

    /** What the host last reported, published by the audio thread for the
        GUI (which must not query the play head itself). */
    double getDisplayBpm() const noexcept     { return displayBpm.load (std::memory_order_relaxed); }
    bool hostReportsTempo() const noexcept    { return displayHasHostTempo.load (std::memory_order_relaxed); }
    bool hostIsPlaying() const noexcept       { return displayPlaying.load (std::memory_order_relaxed); }

    /** The pattern each voice is playing, the shared step and how many hits
        each voice has played, published by the audio thread for the pattern
        view. Safe from any thread. */
    Pattern getDisplayPattern (VoiceId voice) const noexcept;
    int64_t getDisplayStep() const noexcept   { return displayStep.load (std::memory_order_relaxed); }
    bool isClockRunning() const noexcept      { return displayRunning.load (std::memory_order_relaxed); }
    uint32_t getHitCount (VoiceId voice) const noexcept
    {
        return displayHitCounts[static_cast<size_t> (voice)].load (std::memory_order_relaxed);
    }

    juce::AudioProcessorValueTreeState apvts;

private:
    std::array<VoiceParamPointers, numVoices> voiceParams;
    std::atomic<float>* densityParam = nullptr;
    std::atomic<float>* stepDivisionParam = nullptr;
    std::atomic<float>* midiOutEnabledParam = nullptr;
    std::atomic<float>* synthEnabledParam = nullptr;
    std::atomic<float>* masterGainParam = nullptr;
    std::atomic<float>* driveTypeParam = nullptr;
    std::atomic<float>* limiterParam = nullptr;
    std::atomic<float>* autoEvolveParam = nullptr;

    PatternEngine patternEngine;
    DrumSynth drumSynth;
    OutputStage outputStage;

    // Reused every block; reserved up front so the audio thread never grows it.
    std::vector<TriggerEvent> triggers;

    struct PendingNoteOff
    {
        int64_t firePosition;
        int note;
    };
    // Bounded: when full, new MIDI notes are skipped rather than grown on the
    // audio thread (a note-on is only sent if its note-off fits).
    static constexpr size_t kMaxPendingNoteOffs = 256;
    std::vector<PendingNoteOff> pendingNoteOffs;
    int64_t samplePosition = 0;
    int64_t noteLengthSamples = 1323;       // 30 ms MIDI notes; set from the rate in prepareToPlay

    // The seed: currentSeed is what is saved and shown; a new one is posted by
    // the message thread (requestedSeed) and applied by the audio thread.
    std::atomic<uint32_t> currentSeed { 0 };
    std::atomic<uint32_t> requestedSeed { 0 };
    std::atomic<bool> seedRequested { false };

    // Auto-evolve: the engine's request count, published by the audio thread;
    // the message thread remembers how many it has handled.
    std::atomic<uint32_t> evolveRequests { 0 };
    uint32_t evolveRequestsHandled = 0;
    void timerCallback() override { handleEvolveRequests(); }

    // Host state for the GUI (see getDisplayBpm).
    static_assert (std::atomic<double>::is_always_lock_free);
    std::atomic<double> displayBpm { 120.0 };
    std::atomic<bool> displayHasHostTempo { false };
    std::atomic<bool> displayPlaying { false };

    // Pattern view state (see getDisplayPattern). A pattern is packed into one
    // word: step bits in the low 32, the length above them.
    static_assert (kMaxPatternSteps <= 32);
    std::array<std::atomic<uint64_t>, numVoices> displayPatterns {};
    std::array<std::atomic<uint32_t>, numVoices> displayHitCounts {};
    std::atomic<int64_t> displayStep { 0 };
    std::atomic<bool> displayRunning { false };

    juce::Random mutationRandom;

    void cacheParamPointers();
    VoiceParams readVoiceParams (int voiceIndex, float densityFactor) const;
    HostTimeInfo readHostTime() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlydrmAudioProcessor)
};

} // namespace plydrm
