#pragma once

#include "Params.h"
#include "Engine/PatternEngine.h"
#include "Synth/DrumSynth.h"
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
};

class PlydrmAudioProcessor final : public juce::AudioProcessor
{
public:
    PlydrmAudioProcessor();
    ~PlydrmAudioProcessor() override = default;

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

    /** Nudges every enabled voice's hits/rotation/probability/swing/everyN
        by a small random amount - the "evolve" GUI button. Safe to call
        from the message thread; writes go through setValueNotifyingHost so
        the GUI sliders animate and the host sees the automation change. */
    void evolvePatterns();

    /** Re-seeds the pattern engine's RNGs (probability rolls, humanize
        jitter, synth hit variation) - the "randomize seed" GUI button. */
    void randomizeSeed();

    juce::AudioProcessorValueTreeState apvts;

private:
    std::array<VoiceParamPointers, numVoices> voiceParams;
    std::atomic<float>* densityParam = nullptr;
    std::atomic<float>* stepDivisionParam = nullptr;
    std::atomic<float>* midiOutEnabledParam = nullptr;
    std::atomic<float>* synthEnabledParam = nullptr;
    std::atomic<float>* masterGainParam = nullptr;

    PatternEngine patternEngine;
    DrumSynth drumSynth;

    struct PendingNoteOff
    {
        int64_t firePosition;
        int note;
    };
    std::vector<PendingNoteOff> pendingNoteOffs;
    int64_t samplePosition = 0;

    juce::Random mutationRandom;

    void cacheParamPointers();
    VoiceParams readVoiceParams (int voiceIndex, float densityFactor) const;
    HostTimeInfo readHostTime() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlydrmAudioProcessor)
};

} // namespace plydrm
