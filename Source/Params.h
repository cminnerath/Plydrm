#pragma once

#include "Engine/VoiceTypes.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <string>

namespace plydrm
{
namespace ParamIDs
{
    inline juce::String voiceParam (const VoiceInfo& v, const char* suffix)
    {
        return juce::String (v.id) + "_" + suffix;
    }

    static constexpr const char* density        = "density";
    static constexpr const char* stepDivision   = "stepDivision";
    static constexpr const char* midiOutEnabled = "midiOutEnabled";
    static constexpr const char* synthEnabled   = "synthEnabled";
    static constexpr const char* masterGain     = "masterGain";
}

/** Step-rate choices the global clock can run at. */
static constexpr std::array<const char*, 3> stepDivisionChoices { "1/8", "1/16", "1/32" };
/** Number of 1/16th-note "steps" each choice advances per step (used to derive
    samples-per-step from the host tempo). Expressed as quarter-note fraction. */
static constexpr std::array<double, 3> stepDivisionQuarterLength { 0.5, 0.25, 0.125 };

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (const auto& v : voiceInfo)
    {
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            ParamIDs::voiceParam (v, "enabled"), juce::String (v.displayName) + " On", true));

        params.push_back (std::make_unique<juce::AudioParameterInt> (
            ParamIDs::voiceParam (v, "steps"), juce::String (v.displayName) + " Steps", 1, 32, v.defaultSteps));

        params.push_back (std::make_unique<juce::AudioParameterInt> (
            ParamIDs::voiceParam (v, "hits"), juce::String (v.displayName) + " Hits", 0, 32, v.defaultHits));

        params.push_back (std::make_unique<juce::AudioParameterInt> (
            ParamIDs::voiceParam (v, "rotation"), juce::String (v.displayName) + " Rotation", 0, 31, 0));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "probability"), juce::String (v.displayName) + " Probability",
            juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f));

        params.push_back (std::make_unique<juce::AudioParameterInt> (
            ParamIDs::voiceParam (v, "everyN"), juce::String (v.displayName) + " Every N", 1, 8, 1));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "swing"), juce::String (v.displayName) + " Swing",
            juce::NormalisableRange<float> (0.0f, 75.0f), 0.0f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "humanizeMs"), juce::String (v.displayName) + " Humanize",
            juce::NormalisableRange<float> (0.0f, 30.0f), 4.0f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "level"), juce::String (v.displayName) + " Level",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "pitch"), juce::String (v.displayName) + " Pitch",
            juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f));
    }

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::density, "Density", juce::NormalisableRange<float> (0.0f, 100.0f), 50.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::stepDivision, "Step Division",
        juce::StringArray (stepDivisionChoices.data(), (int) stepDivisionChoices.size()), 1));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParamIDs::midiOutEnabled, "MIDI Out", true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParamIDs::synthEnabled, "Internal Synth", true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::masterGain, "Master Gain", juce::NormalisableRange<float> (0.0f, 1.5f), 0.9f));

    return { params.begin(), params.end() };
}
} // namespace plydrm
