#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace plydrm
{

namespace
{
    std::atomic<float>* raw (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        return state.getRawParameterValue (id);
    }
}

PlydrmAudioProcessor::PlydrmAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    cacheParamPointers();
    patternEngine.reseed (static_cast<uint32_t> (juce::Time::getMillisecondCounter()));
}

void PlydrmAudioProcessor::cacheParamPointers()
{
    for (int i = 0; i < numVoices; ++i)
    {
        const auto& info = voiceInfo[static_cast<size_t> (i)];
        auto& p = voiceParams[static_cast<size_t> (i)];
        p.enabled = raw (apvts, ParamIDs::voiceParam (info, "enabled"));
        p.steps = raw (apvts, ParamIDs::voiceParam (info, "steps"));
        p.hits = raw (apvts, ParamIDs::voiceParam (info, "hits"));
        p.rotation = raw (apvts, ParamIDs::voiceParam (info, "rotation"));
        p.probability = raw (apvts, ParamIDs::voiceParam (info, "probability"));
        p.everyN = raw (apvts, ParamIDs::voiceParam (info, "everyN"));
        p.swing = raw (apvts, ParamIDs::voiceParam (info, "swing"));
        p.humanizeMs = raw (apvts, ParamIDs::voiceParam (info, "humanizeMs"));
        p.level = raw (apvts, ParamIDs::voiceParam (info, "level"));
        p.pitch = raw (apvts, ParamIDs::voiceParam (info, "pitch"));
    }

    densityParam = raw (apvts, ParamIDs::density);
    stepDivisionParam = raw (apvts, ParamIDs::stepDivision);
    midiOutEnabledParam = raw (apvts, ParamIDs::midiOutEnabled);
    synthEnabledParam = raw (apvts, ParamIDs::synthEnabled);
    masterGainParam = raw (apvts, ParamIDs::masterGain);
}

void PlydrmAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    patternEngine.prepareToPlay (sampleRate);
    drumSynth.prepare (sampleRate, samplesPerBlock);
    samplePosition = 0;
    pendingNoteOffs.clear();
}

void PlydrmAudioProcessor::releaseResources() {}

bool PlydrmAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

VoiceParams PlydrmAudioProcessor::readVoiceParams (int voiceIndex, float densityFactor) const
{
    const auto& p = voiceParams[static_cast<size_t> (voiceIndex)];

    VoiceParams vp;
    vp.enabled = p.enabled->load() > 0.5f;
    vp.steps = juce::jlimit (1, 32, static_cast<int> (std::lround (p.steps->load())));

    const int baseHits = static_cast<int> (std::lround (p.hits->load()));
    vp.hits = juce::jlimit (0, vp.steps, static_cast<int> (std::lround (baseHits * densityFactor)));

    vp.rotation = static_cast<int> (std::lround (p.rotation->load()));
    vp.probabilityPercent = p.probability->load();
    vp.everyN = juce::jlimit (1, 8, static_cast<int> (std::lround (p.everyN->load())));
    vp.swingPercent = p.swing->load();
    vp.humanizeMs = p.humanizeMs->load();
    return vp;
}

void PlydrmAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    midiMessages.clear();

    // --- Tempo sync -------------------------------------------------------
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
    {
        if (auto position = ph->getPosition())
            if (auto bpmOpt = position->getBpm())
                bpm = *bpmOpt > 1.0 ? *bpmOpt : 120.0;
    }

    const int divisionIndex = juce::jlimit (0, (int) stepDivisionQuarterLength.size() - 1,
                                             static_cast<int> (std::lround (stepDivisionParam->load())));
    patternEngine.setTempo (bpm, stepDivisionQuarterLength[static_cast<size_t> (divisionIndex)]);

    // --- Pull latest parameter values into the engine ----------------------
    const float density = densityParam->load();
    const float densityFactor = 0.4f + (density / 100.0f) * 1.2f; // 50% -> 1.0x

    for (int i = 0; i < numVoices; ++i)
        patternEngine.setVoiceParams (static_cast<VoiceId> (i), readVoiceParams (i, densityFactor));

    // --- Generate this block's triggers -------------------------------------
    std::vector<TriggerEvent> triggers;
    patternEngine.process (numSamples, triggers);

    const bool midiOutEnabled = midiOutEnabledParam->load() > 0.5f;
    const bool synthEnabled = synthEnabledParam->load() > 0.5f;
    const int64_t blockStart = samplePosition;
    const int64_t blockEnd = blockStart + numSamples;
    constexpr int64_t noteOffDelaySamplesDefault = 0; // computed per-sample-rate below

    for (const auto& trig : triggers)
    {
        const auto& info = voiceInfo[static_cast<size_t> (trig.voice)];
        const auto& p = voiceParams[static_cast<size_t> (trig.voice)];
        const float level = p.level->load();
        const float pitch = p.pitch->load();

        if (synthEnabled)
            drumSynth.noteOn (trig.voice, trig.velocity * level, pitch);

        if (midiOutEnabled)
        {
            const int velocity127 = juce::jlimit (1, 127, static_cast<int> (std::lround (trig.velocity * level * 127.0f)));
            midiMessages.addEvent (juce::MidiMessage::noteOn (10, info.gmNote, static_cast<juce::uint8> (velocity127)),
                                    trig.sampleOffset);

            const int64_t noteOffAt = blockStart + trig.sampleOffset + juce::jmax<int64_t> (1, static_cast<int64_t> (getSampleRate() * 0.03));
            pendingNoteOffs.push_back ({ noteOffAt, info.gmNote });
        }
    }
    juce::ignoreUnused (noteOffDelaySamplesDefault);

    if (midiOutEnabled)
    {
        for (auto it = pendingNoteOffs.begin(); it != pendingNoteOffs.end(); )
        {
            if (it->firePosition < blockEnd)
            {
                const int offset = juce::jlimit (0, numSamples - 1, static_cast<int> (it->firePosition - blockStart));
                midiMessages.addEvent (juce::MidiMessage::noteOff (10, it->note), offset);
                it = pendingNoteOffs.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }

    // --- Synthesize audio ---------------------------------------------------
    if (synthEnabled)
        drumSynth.renderNextBlock (buffer, 0, numSamples);

    const float masterGain = masterGainParam->load();
    buffer.applyGain (masterGain);

    samplePosition = blockEnd;
}

void PlydrmAudioProcessor::evolvePatterns()
{
    for (int i = 0; i < numVoices; ++i)
    {
        const auto& info = voiceInfo[static_cast<size_t> (i)];

        auto nudgeInt = [this, &info] (const char* suffix, int lo, int hi, int maxStep)
        {
            if (auto* param = apvts.getParameter (ParamIDs::voiceParam (info, suffix)))
            {
                const auto range = param->getNormalisableRange();
                const int current = static_cast<int> (std::lround (range.convertFrom0to1 (param->getValue())));
                const int delta = mutationRandom.nextInt (juce::Range<int> (-maxStep, maxStep + 1));
                const int next = juce::jlimit (lo, hi, current + delta);
                param->setValueNotifyingHost (range.convertTo0to1 (static_cast<float> (next)));
            }
        };

        auto nudgeFloat = [this, &info] (const char* suffix, float lo, float hi, float maxStep)
        {
            if (auto* param = apvts.getParameter (ParamIDs::voiceParam (info, suffix)))
            {
                const auto range = param->getNormalisableRange();
                const float current = range.convertFrom0to1 (param->getValue());
                const float delta = mutationRandom.nextFloat() * 2.0f * maxStep - maxStep;
                const float next = juce::jlimit (lo, hi, current + delta);
                param->setValueNotifyingHost (range.convertTo0to1 (next));
            }
        };

        nudgeInt ("hits", 0, 32, 2);
        nudgeInt ("rotation", 0, 31, 2);
        nudgeFloat ("probability", 40.0f, 100.0f, 10.0f);
        nudgeFloat ("swing", 0.0f, 75.0f, 8.0f);

        if (mutationRandom.nextFloat() < 0.15f)
            nudgeInt ("everyN", 1, 8, 1);
    }
}

void PlydrmAudioProcessor::randomizeSeed()
{
    patternEngine.reseed (static_cast<uint32_t> (mutationRandom.nextInt64()));
}

void PlydrmAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }
}

void PlydrmAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PlydrmAudioProcessor::createEditor()
{
    return new PlydrmAudioProcessorEditor (*this);
}

} // namespace plydrm

// This export must be in the global namespace.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new plydrm::PlydrmAudioProcessor();
}
