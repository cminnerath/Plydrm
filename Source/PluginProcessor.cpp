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

    const auto seed = static_cast<uint32_t> (juce::Time::getMillisecondCounter());
    patternEngine.reseed (seed);
    drumSynth.reseed (seed ^ 0x9e3779b9u);

    triggers.reserve (2 * PatternEngine::kMaxPendingEvents);
    pendingNoteOffs.reserve (kMaxPendingNoteOffs);
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
        p.drive = raw (apvts, ParamIDs::voiceParam (info, "drive"));
    }

    densityParam = raw (apvts, ParamIDs::density);
    stepDivisionParam = raw (apvts, ParamIDs::stepDivision);
    midiOutEnabledParam = raw (apvts, ParamIDs::midiOutEnabled);
    synthEnabledParam = raw (apvts, ParamIDs::synthEnabled);
    masterGainParam = raw (apvts, ParamIDs::masterGain);
    driveTypeParam = raw (apvts, ParamIDs::driveType);
    limiterParam = raw (apvts, ParamIDs::limiter);
}

void PlydrmAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    noteLengthSamples = juce::jmax<int64_t> (1, static_cast<int64_t> (sampleRate * 0.03));
    patternEngine.prepareToPlay (sampleRate);
    drumSynth.prepare (sampleRate);
    outputStage.prepare (sampleRate, masterGainParam->load());
    juce::ignoreUnused (samplesPerBlock);
    samplePosition = 0;

    // Notes still waiting for their note-off would be left hanging on the
    // receiving instrument: send those note-offs at the start of the next block.
    for (auto& off : pendingNoteOffs)
        off.firePosition = 0;
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
    vp.hits = juce::jlimit (0, vp.steps, static_cast<int> (std::lround (static_cast<float> (baseHits) * densityFactor)));

    vp.rotation = static_cast<int> (std::lround (p.rotation->load()));
    vp.probabilityPercent = p.probability->load();
    vp.everyN = juce::jlimit (1, 8, static_cast<int> (std::lround (p.everyN->load())));
    vp.swingPercent = p.swing->load();
    vp.humanizeMs = p.humanizeMs->load();
    return vp;
}

HostTimeInfo PlydrmAudioProcessor::readHostTime() const
{
    HostTimeInfo h;

    auto* ph = getPlayHead();
    if (ph == nullptr)
        return h;

    const auto pos = ph->getPosition();
    if (! pos.hasValue())
        return h;

    if (const auto bpm = pos->getBpm())                         { h.hasBpm = true; h.bpm = *bpm; }
    if (const auto ppq = pos->getPpqPosition())                 { h.hasPpq = true; h.ppq = *ppq; }
    if (const auto bar = pos->getPpqPositionOfLastBarStart())   { h.hasBarStart = true; h.barStartPpq = *bar; }
    if (const auto sig = pos->getTimeSignature())
    {
        h.hasTimeSig = true;
        h.timeSigNum = sig->numerator;
        h.timeSigDen = sig->denominator;
    }
    h.isPlaying = pos->getIsPlaying();
    return h;
}

void PlydrmAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    midiMessages.clear();

    // --- New Seed (posted by the message thread) ----------------------------
    if (seedRequested.exchange (false, std::memory_order_acquire))
    {
        const auto seed = requestedSeed.load (std::memory_order_relaxed);
        patternEngine.reseed (seed);
        drumSynth.reseed (seed ^ 0x9e3779b9u);
    }

    // --- Host transport ---------------------------------------------------
    const auto host = readHostTime();
    const int divisionIndex = juce::jlimit (0, (int) stepDivisionQuarterLength.size() - 1,
                                             static_cast<int> (std::lround (stepDivisionParam->load())));
    patternEngine.setStepLength (stepDivisionQuarterLength[static_cast<size_t> (divisionIndex)]);

    const int driveTypeIndex = juce::jlimit (0, (int) cmfx::DriveType::Count - 1,
                                              static_cast<int> (std::lround (driveTypeParam->load())));
    drumSynth.setDriveType (static_cast<cmfx::DriveType> (driveTypeIndex));

    // --- Pull latest parameter values into the engine ----------------------
    const float density = densityParam->load();
    const float densityFactor = 0.4f + (density / 100.0f) * 1.2f; // 50% -> 1.0x

    for (int i = 0; i < numVoices; ++i)
        patternEngine.setVoiceParams (static_cast<VoiceId> (i), readVoiceParams (i, densityFactor));

    // --- Generate this block's triggers -------------------------------------
    triggers.clear();
    patternEngine.process (host, numSamples, triggers);

    for (int i = 0; i < numVoices; ++i)
    {
        const auto& pattern = patternEngine.getPatternForVoice (static_cast<VoiceId> (i));
        uint64_t packed = static_cast<uint64_t> (pattern.length) << 32;
        for (int k = 0; k < pattern.length; ++k)
            if (pattern[k])
                packed |= uint64_t { 1 } << k;
        displayPatterns[static_cast<size_t> (i)].store (packed, std::memory_order_relaxed);
    }
    displayStep.store (patternEngine.getCurrentStep(), std::memory_order_relaxed);
    displayRunning.store (patternEngine.isRunning(), std::memory_order_relaxed);
    for (const auto& trig : triggers)
        displayHitCounts[static_cast<size_t> (trig.voice)].fetch_add (1, std::memory_order_relaxed);

    displayBpm.store (patternEngine.getBpm(), std::memory_order_relaxed);
    displayHasHostTempo.store (host.hasBpm, std::memory_order_relaxed);
    displayPlaying.store (host.isPlaying, std::memory_order_relaxed);

    const bool midiOutEnabled = midiOutEnabledParam->load() > 0.5f;
    const bool synthEnabled = synthEnabledParam->load() > 0.5f;
    const int64_t blockStart = samplePosition;
    const int64_t blockEnd = blockStart + numSamples;

    // Triggers come sorted by offset. The synth renders up to each one before
    // it starts, so every hit begins on its own sample rather than at the
    // start of the block.
    int rendered = 0;

    for (const auto& trig : triggers)
    {
        const auto& info = voiceInfo[static_cast<size_t> (trig.voice)];
        const auto& p = voiceParams[static_cast<size_t> (trig.voice)];
        const float level = p.level->load();
        const float pitch = p.pitch->load();
        const float drive = p.drive->load() / 100.0f;

        if (synthEnabled)
        {
            if (trig.sampleOffset > rendered)
            {
                drumSynth.render (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), rendered, trig.sampleOffset - rendered);
                rendered = trig.sampleOffset;
            }
            drumSynth.noteOn (trig.voice, trig.velocity * level, pitch, drive);
        }

        if (midiOutEnabled && pendingNoteOffs.size() < kMaxPendingNoteOffs)
        {
            const int velocity127 = juce::jlimit (1, 127, static_cast<int> (std::lround (trig.velocity * level * 127.0f)));
            midiMessages.addEvent (juce::MidiMessage::noteOn (10, info.gmNote, static_cast<juce::uint8> (velocity127)),
                                    trig.sampleOffset);

            const int64_t noteOffAt = blockStart + trig.sampleOffset + noteLengthSamples;
            pendingNoteOffs.push_back ({ noteOffAt, info.gmNote });
        }
    }

    // --- Synthesize the rest of the block -----------------------------------
    if (synthEnabled && rendered < numSamples)
        drumSynth.render (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), rendered, numSamples - rendered);

    // Note-offs go out even when MIDI Out has just been switched off, so no
    // note it already started is left hanging.
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

    // --- Output: smoothed master gain, then the soft limiter ----------------
    outputStage.setGain (masterGainParam->load());
    outputStage.setLimiter (limiterParam->load() > 0.5f);
    outputStage.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);

    samplePosition = blockEnd;
}

Pattern PlydrmAudioProcessor::getDisplayPattern (VoiceId voice) const noexcept
{
    const auto packed = displayPatterns[static_cast<size_t> (voice)].load (std::memory_order_relaxed);
    Pattern p;
    p.length = std::clamp (static_cast<int> (packed >> 32), 0, kMaxPatternSteps);
    for (int k = 0; k < p.length; ++k)
        p.step[static_cast<size_t> (k)] = ((packed >> k) & 1u) != 0;
    return p;
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
    setSeed (static_cast<uint32_t> (mutationRandom.nextInt64()));
}

void PlydrmAudioProcessor::setSeed (uint32_t seed) noexcept
{
    requestedSeed.store (seed, std::memory_order_relaxed);
    seedRequested.store (true, std::memory_order_release);
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
