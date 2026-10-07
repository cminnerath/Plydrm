#include "PluginEditor.h"
#include "Theme.h"

namespace plydrm
{

namespace
{
    constexpr int margin = 10;
    constexpr int headerHeight = 30;
    constexpr int patternHeight = 132;
    constexpr int rowHeight = 84;
    constexpr int rowTitleWidth = 76;
    constexpr int knobWidth = 62;
    constexpr int captionHeight = 14;
    constexpr int globalWidth = 230;
    constexpr int numVoiceKnobs = 10;

    constexpr int voiceAreaWidth = rowTitleWidth + knobWidth * numVoiceKnobs;
    constexpr int editorWidth = margin + voiceAreaWidth + margin + globalWidth + margin;
    constexpr int editorHeight = margin + headerHeight + 6 + patternHeight + 8 + rowHeight * numVoices + margin;
}

PlydrmAudioProcessorEditor::PlydrmAudioProcessorEditor (PlydrmAudioProcessor& p)
    : AudioProcessorEditor (&p), plydrmProcessor (p)
{
    lookAndFeel.setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, theme::outline);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, theme::text);
    lookAndFeel.setColour (juce::Label::textColourId, theme::text);
    lookAndFeel.setColour (juce::ToggleButton::textColourId, theme::text);
    lookAndFeel.setColour (juce::ToggleButton::tickColourId, theme::text);
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, theme::cell);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, theme::outline);
    lookAndFeel.setColour (juce::TextButton::buttonColourId, theme::cell);
    lookAndFeel.setColour (juce::TextButton::textColourOffId, theme::text);
    setLookAndFeel (&lookAndFeel);

    titleLabel.setText ("Plydrm", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    tempoLabel.setText ("Tempo: --", juce::dontSendNotification);
    tempoLabel.setColour (juce::Label::textColourId, theme::dimText);
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    addAndMakeVisible (patternView);

    for (int i = 0; i < numVoices; ++i)
        setupVoice (i);

    const auto accent = theme::text.withAlpha (0.8f);
    setupKnob (densityKnob, "Density", ParamIDs::density, accent);
    setupKnob (masterGainKnob, "Master Gain", ParamIDs::masterGain, accent);
    setupChoice (stepDivision, "Step", ParamIDs::stepDivision,
                 juce::StringArray (stepDivisionChoices.data(), (int) stepDivisionChoices.size()));
    setupChoice (driveType, "Drive", ParamIDs::driveType,
                 juce::StringArray (driveTypeChoices.data(), (int) driveTypeChoices.size()));
    setupChoice (autoEvolve, "Auto", ParamIDs::autoEvolve,
                 juce::StringArray (autoEvolveChoices.data(), (int) autoEvolveChoices.size()));
    autoEvolve.box.setTooltip ("Evolve the pattern automatically every N bars");

    for (auto* toggle : { &midiOutToggle, &synthToggle, &limiterToggle })
        addAndMakeVisible (*toggle);
    midiOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        plydrmProcessor.apvts, ParamIDs::midiOutEnabled, midiOutToggle);
    synthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        plydrmProcessor.apvts, ParamIDs::synthEnabled, synthToggle);
    limiterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        plydrmProcessor.apvts, ParamIDs::limiter, limiterToggle);

    evolveButton.onClick = [this] { plydrmProcessor.evolvePatterns(); };
    randomizeSeedButton.onClick = [this] { plydrmProcessor.randomizeSeed(); };
    addAndMakeVisible (evolveButton);
    addAndMakeVisible (randomizeSeedButton);

    seedLabel.setColour (juce::Label::textColourId, theme::dimText);
    seedLabel.setJustificationType (juce::Justification::centredRight);
    seedLabel.setFont (juce::FontOptions (12.0f));
    seedLabel.setTooltip ("The seed behind every random choice. It is saved with the session, so it always plays the same.");
    addAndMakeVisible (seedLabel);

    setSize (editorWidth, editorHeight);
    timerCallback();   // fill in the tempo now, not on the first tick
    startTimerHz (4);
}

PlydrmAudioProcessorEditor::~PlydrmAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PlydrmAudioProcessorEditor::setupKnob (KnobControl& knob, const juce::String& captionText, const juce::String& paramID,
                                            juce::Colour colour)
{
    knob.caption.setText (captionText, juce::dontSendNotification);
    knob.caption.setJustificationType (juce::Justification::centred);
    knob.caption.setFont (juce::FontOptions (11.5f));
    knob.caption.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (knob.caption);

    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth - 4, 14);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, theme::text);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    knob.slider.setColour (juce::Slider::thumbColourId, colour.brighter (0.3f));
    addAndMakeVisible (knob.slider);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        plydrmProcessor.apvts, paramID, knob.slider);
}

void PlydrmAudioProcessorEditor::setupChoice (ChoiceControl& choice, const juce::String& captionText, const juce::String& paramID,
                                              const juce::StringArray& items)
{
    choice.caption.setText (captionText, juce::dontSendNotification);
    choice.caption.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (choice.caption);

    choice.box.addItemList (items, 1);
    addAndMakeVisible (choice.box);
    choice.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        plydrmProcessor.apvts, paramID, choice.box);
}

void PlydrmAudioProcessorEditor::setupVoice (int voiceIndex)
{
    auto& v = voiceControls[static_cast<size_t> (voiceIndex)];
    const auto& info = voiceInfo[static_cast<size_t> (voiceIndex)];
    const auto colour = theme::voiceColour (static_cast<VoiceId> (voiceIndex));

    v.title.setText (info.displayName, juce::dontSendNotification);
    v.title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    v.title.setColour (juce::Label::textColourId, colour);
    addAndMakeVisible (v.title);

    addAndMakeVisible (v.enabled);
    v.enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        plydrmProcessor.apvts, ParamIDs::voiceParam (info, "enabled"), v.enabled);

    setupKnob (v.steps, "Steps", ParamIDs::voiceParam (info, "steps"), colour);
    setupKnob (v.hits, "Hits", ParamIDs::voiceParam (info, "hits"), colour);
    setupKnob (v.rotation, "Rotate", ParamIDs::voiceParam (info, "rotation"), colour);
    setupKnob (v.probability, "Prob", ParamIDs::voiceParam (info, "probability"), colour);
    setupKnob (v.everyN, "Every N", ParamIDs::voiceParam (info, "everyN"), colour);
    setupKnob (v.swing, "Swing", ParamIDs::voiceParam (info, "swing"), colour);
    setupKnob (v.humanizeMs, "Humanize", ParamIDs::voiceParam (info, "humanizeMs"), colour);
    setupKnob (v.level, "Level", ParamIDs::voiceParam (info, "level"), colour);
    setupKnob (v.pitch, "Pitch", ParamIDs::voiceParam (info, "pitch"), colour);
    setupKnob (v.drive, "Drive", ParamIDs::voiceParam (info, "drive"), colour);
}

void PlydrmAudioProcessorEditor::placeKnob (KnobControl& knob, juce::Rectangle<int> area)
{
    knob.caption.setBounds (area.removeFromTop (captionHeight));
    knob.slider.setBounds (area);
}

void PlydrmAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    for (int i = 0; i < numVoices; ++i)
    {
        const auto row = voiceRowBounds[static_cast<size_t> (i)].toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (row.reduced (0.0f, 2.0f), 8.0f);
        g.setColour (theme::voiceColour (static_cast<VoiceId> (i)));
        g.fillRoundedRectangle (row.reduced (0.0f, 2.0f).withWidth (4.0f), 2.0f);
    }

    g.setColour (theme::panel);
    g.fillRoundedRectangle (globalPanelBounds.toFloat(), 8.0f);
}

void PlydrmAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    auto header = area.removeFromTop (headerHeight);
    titleLabel.setBounds (header.removeFromLeft (200));
    tempoLabel.setBounds (header);
    area.removeFromTop (6);

    patternView.setBounds (area.removeFromTop (patternHeight));
    area.removeFromTop (8);

    auto voiceArea = area.removeFromLeft (voiceAreaWidth);
    area.removeFromLeft (margin);
    globalPanelBounds = area;

    for (int i = 0; i < numVoices; ++i)
    {
        auto& v = voiceControls[static_cast<size_t> (i)];
        auto row = voiceArea.removeFromTop (rowHeight);
        voiceRowBounds[static_cast<size_t> (i)] = row;

        auto titleArea = row.removeFromLeft (rowTitleWidth).reduced (12, 0);
        titleArea.removeFromTop (14);
        v.title.setBounds (titleArea.removeFromTop (22));
        v.enabled.setBounds (titleArea.removeFromTop (24));

        row = row.reduced (0, 4);
        for (auto* knob : v.knobs())
            placeKnob (*knob, row.removeFromLeft (knobWidth));
    }

    auto panel = globalPanelBounds.reduced (12, 8);

    auto knobRow = panel.removeFromTop (rowHeight - 8);
    placeKnob (densityKnob, knobRow.removeFromLeft (knobRow.getWidth() / 2));
    placeKnob (masterGainKnob, knobRow);
    panel.removeFromTop (8);

    for (auto* choice : { &stepDivision, &driveType, &autoEvolve })
    {
        auto line = panel.removeFromTop (24);
        choice->caption.setBounds (line.removeFromLeft (56));
        choice->box.setBounds (line);
        panel.removeFromTop (6);
    }

    for (auto* toggle : { &midiOutToggle, &synthToggle, &limiterToggle })
        toggle->setBounds (panel.removeFromTop (22));

    panel.removeFromTop (8);
    evolveButton.setBounds (panel.removeFromTop (28));
    panel.removeFromTop (6);
    auto seedRow = panel.removeFromTop (28);
    randomizeSeedButton.setBounds (seedRow.removeFromLeft (100));
    seedLabel.setBounds (seedRow);
}

void PlydrmAudioProcessorEditor::timerCallback()
{
    seedLabel.setText ("Seed " + juce::String::toHexString ((juce::int64) plydrmProcessor.getSeed()).toUpperCase().paddedLeft ('0', 8),
                       juce::dontSendNotification);

    // Read what the audio thread published: the play head may only be asked
    // from inside processBlock.
    if (plydrmProcessor.hostReportsTempo())
        tempoLabel.setText (juce::String::formatted ("%.1f BPM%s", plydrmProcessor.getDisplayBpm(),
                                                     plydrmProcessor.hostIsPlaying() ? "  -  playing" : "  -  stopped"),
                            juce::dontSendNotification);
    else
        tempoLabel.setText (juce::String::formatted ("free-run %.0f BPM", plydrmProcessor.getDisplayBpm()),
                            juce::dontSendNotification);
}

} // namespace plydrm
