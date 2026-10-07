#pragma once

#include "PatternView.h"
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

/** All the controls for one drum voice (one row of the GUI). */
struct VoiceControls
{
    juce::Label title;
    juce::ToggleButton enabled { "On" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment;

    KnobControl steps, hits, rotation, probability, everyN, swing, humanizeMs, level, pitch, drive;

    std::array<KnobControl*, 10> knobs() noexcept
    {
        return { &steps, &hits, &rotation, &probability, &everyN, &swing, &humanizeMs, &level, &pitch, &drive };
    }
};

/** A labelled choice box with its APVTS attachment. */
struct ChoiceControl
{
    juce::Label caption;
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Layout: the pattern view across the top, one row of knobs per voice
    under it, and the global controls in a column on the right. */
class PlydrmAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit PlydrmAudioProcessorEditor (PlydrmAudioProcessor&);
    ~PlydrmAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    PatternView& getPatternView() noexcept { return patternView; }

private:
    PlydrmAudioProcessor& plydrmProcessor;
    juce::LookAndFeel_V4 lookAndFeel;

    juce::Label titleLabel;
    juce::Label tempoLabel;

    PatternView patternView { plydrmProcessor };
    std::array<VoiceControls, numVoices> voiceControls;
    std::array<juce::Rectangle<int>, numVoices> voiceRowBounds;
    juce::Rectangle<int> globalPanelBounds;

    KnobControl densityKnob;
    KnobControl masterGainKnob;
    ChoiceControl stepDivision;
    ChoiceControl driveType;
    ChoiceControl autoEvolve;

    juce::ToggleButton midiOutToggle { "MIDI Out" };
    juce::ToggleButton synthToggle { "Internal Synth" };
    juce::ToggleButton limiterToggle { "Limiter" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiOutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> synthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limiterAttachment;

    juce::TextButton evolveButton { "Evolve / Mutate" };
    juce::TextButton randomizeSeedButton { "New Seed" };
    juce::Label seedLabel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    void setupKnob (KnobControl& knob, const juce::String& captionText, const juce::String& paramID, juce::Colour colour);
    void setupChoice (ChoiceControl& choice, const juce::String& captionText, const juce::String& paramID,
                      const juce::StringArray& items);
    void setupVoice (int voiceIndex);
    static void placeKnob (KnobControl& knob, juce::Rectangle<int> area);

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlydrmAudioProcessorEditor)
};

} // namespace plydrm
