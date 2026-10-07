#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

namespace plydrm
{
/** One rotary knob with a caption above it and an APVTS attachment. */
struct KnobControl
{
    juce::Label caption;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** All the controls for one drum voice (one column of the GUI). */
struct VoiceControls
{
    juce::Label title;
    juce::ToggleButton enabled { "On" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment;

    KnobControl steps, hits, rotation, probability, everyN, swing, humanizeMs, level, pitch;
};

class PlydrmAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit PlydrmAudioProcessorEditor (PlydrmAudioProcessor&);
    ~PlydrmAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PlydrmAudioProcessor& processor;

    juce::Label titleLabel;
    juce::Label tempoLabel;

    std::array<VoiceControls, numVoices> voiceControls;

    KnobControl densityKnob;
    KnobControl masterGainKnob;
    juce::ComboBox stepDivisionBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stepDivisionAttachment;

    juce::ToggleButton midiOutToggle { "MIDI Out" };
    juce::ToggleButton synthToggle { "Internal Synth" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiOutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> synthAttachment;

    juce::TextButton evolveButton { "Evolve / Mutate" };
    juce::TextButton randomizeSeedButton { "New Seed" };

    void setupKnob (KnobControl& knob, const juce::String& captionText, const juce::String& paramID);
    void setupVoice (int voiceIndex);

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlydrmAudioProcessorEditor)
};

} // namespace plydrm
