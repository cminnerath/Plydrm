#include "PluginEditor.h"

namespace plydrm
{

namespace
{
    constexpr int knobSize = 70;
    constexpr int captionHeight = 16;
    constexpr int columnWidth = 90;
}

PlydrmAudioProcessorEditor::PlydrmAudioProcessorEditor (PlydrmAudioProcessor& p)
    : AudioProcessorEditor (&p), plydrmProcessor (p)
{
    titleLabel.setText ("Plydrm - Euclidean / Polyrhythm Drum Generator", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    tempoLabel.setText ("Tempo: --", juce::dontSendNotification);
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    for (int i = 0; i < numVoices; ++i)
        setupVoice (i);

    setupKnob (densityKnob, "Density", ParamIDs::density);
    setupKnob (masterGainKnob, "Master Gain", ParamIDs::masterGain);

    stepDivisionBox.addItemList (juce::StringArray (stepDivisionChoices.data(), (int) stepDivisionChoices.size()), 1);
    addAndMakeVisible (stepDivisionBox);
    stepDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        plydrmProcessor.apvts, ParamIDs::stepDivision, stepDivisionBox);

    addAndMakeVisible (midiOutToggle);
    addAndMakeVisible (synthToggle);
    midiOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        plydrmProcessor.apvts, ParamIDs::midiOutEnabled, midiOutToggle);
    synthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        plydrmProcessor.apvts, ParamIDs::synthEnabled, synthToggle);

    evolveButton.onClick = [this] { plydrmProcessor.evolvePatterns(); };
    randomizeSeedButton.onClick = [this] { plydrmProcessor.randomizeSeed(); };
    addAndMakeVisible (evolveButton);
    addAndMakeVisible (randomizeSeedButton);

    setSize (columnWidth * numVoices + 260, 520);
    startTimerHz (4);
}

PlydrmAudioProcessorEditor::~PlydrmAudioProcessorEditor()
{
    stopTimer();
}

void PlydrmAudioProcessorEditor::setupKnob (KnobControl& knob, const juce::String& captionText, const juce::String& paramID)
{
    knob.caption.setText (captionText, juce::dontSendNotification);
    knob.caption.setJustificationType (juce::Justification::centred);
    knob.caption.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (knob.caption);

    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, columnWidth - 10, 16);
    addAndMakeVisible (knob.slider);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        plydrmProcessor.apvts, paramID, knob.slider);
}

void PlydrmAudioProcessorEditor::setupVoice (int voiceIndex)
{
    auto& v = voiceControls[static_cast<size_t> (voiceIndex)];
    const auto& info = voiceInfo[static_cast<size_t> (voiceIndex)];

    v.title.setText (info.displayName, juce::dontSendNotification);
    v.title.setJustificationType (juce::Justification::centred);
    v.title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (v.title);

    addAndMakeVisible (v.enabled);
    v.enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        plydrmProcessor.apvts, ParamIDs::voiceParam (info, "enabled"), v.enabled);

    setupKnob (v.steps, "Steps", ParamIDs::voiceParam (info, "steps"));
    setupKnob (v.hits, "Hits", ParamIDs::voiceParam (info, "hits"));
    setupKnob (v.rotation, "Rotate", ParamIDs::voiceParam (info, "rotation"));
    setupKnob (v.probability, "Prob %", ParamIDs::voiceParam (info, "probability"));
    setupKnob (v.everyN, "Every N", ParamIDs::voiceParam (info, "everyN"));
    setupKnob (v.swing, "Swing", ParamIDs::voiceParam (info, "swing"));
    setupKnob (v.humanizeMs, "Humanize", ParamIDs::voiceParam (info, "humanizeMs"));
    setupKnob (v.level, "Level", ParamIDs::voiceParam (info, "level"));
    setupKnob (v.pitch, "Pitch", ParamIDs::voiceParam (info, "pitch"));
}

void PlydrmAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202329));
    g.setColour (juce::Colours::white);
}

void PlydrmAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    auto topRow = area.removeFromTop (28);
    titleLabel.setBounds (topRow.removeFromLeft (topRow.getWidth() - 150));
    tempoLabel.setBounds (topRow);

    area.removeFromTop (6);

    auto voiceArea = area.removeFromLeft (columnWidth * numVoices);
    auto globalArea = area;

    for (auto& v : voiceControls)
    {
        auto col = voiceArea.removeFromLeft (columnWidth).reduced (2);

        v.title.setBounds (col.removeFromTop (captionHeight));
        v.enabled.setBounds (col.removeFromTop (20));

        for (auto* knob : { &v.steps, &v.hits, &v.rotation, &v.probability, &v.everyN, &v.swing, &v.humanizeMs, &v.level, &v.pitch })
        {
            auto knobArea = col.removeFromTop (captionHeight + knobSize);
            knob->caption.setBounds (knobArea.removeFromTop (captionHeight));
            knob->slider.setBounds (knobArea);
        }
    }

    globalArea.removeFromLeft (10);
    auto densityArea = globalArea.removeFromTop (captionHeight + knobSize);
    densityKnob.caption.setBounds (densityArea.removeFromTop (captionHeight));
    densityKnob.slider.setBounds (densityArea);

    auto gainArea = globalArea.removeFromTop (captionHeight + knobSize);
    masterGainKnob.caption.setBounds (gainArea.removeFromTop (captionHeight));
    masterGainKnob.slider.setBounds (gainArea);

    globalArea.removeFromTop (6);
    stepDivisionBox.setBounds (globalArea.removeFromTop (24));

    globalArea.removeFromTop (6);
    midiOutToggle.setBounds (globalArea.removeFromTop (24));
    synthToggle.setBounds (globalArea.removeFromTop (24));

    globalArea.removeFromTop (10);
    evolveButton.setBounds (globalArea.removeFromTop (28));
    globalArea.removeFromTop (6);
    randomizeSeedButton.setBounds (globalArea.removeFromTop (28));
}

void PlydrmAudioProcessorEditor::timerCallback()
{
    // Read what the audio thread published: the play head may only be asked
    // from inside processBlock.
    if (plydrmProcessor.hostReportsTempo())
        tempoLabel.setText (juce::String::formatted ("Tempo: %.1f BPM%s", plydrmProcessor.getDisplayBpm(),
                                                     plydrmProcessor.hostIsPlaying() ? " (playing)" : ""),
                            juce::dontSendNotification);
    else
        tempoLabel.setText (juce::String::formatted ("Tempo: free-run %.0f BPM", plydrmProcessor.getDisplayBpm()),
                            juce::dontSendNotification);
}

} // namespace plydrm
