#pragma once

#include "Engine/VoiceTypes.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
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
    static constexpr const char* driveType      = "driveType";
    static constexpr const char* limiter        = "limiter";
    static constexpr const char* autoEvolve     = "autoEvolve";
    static constexpr const char* complexity     = "complexity";

    // The perform pad (Density across, Complexity up) and its Pad Motion.
    static constexpr const char* padHold        = "padHold";
    static constexpr const char* padRelease     = "padRelease";
    static constexpr const char* padBars        = "padBars";
    static constexpr const char* padBall        = "padBall";
    static constexpr const char* padFriction    = "padFriction";
    static constexpr const char* padWrap        = "padWrap";
}

/** How long a Pad Motion recording is: 1, 2 or 4 bars (choice index i is 2^i bars). */
static constexpr std::array<const char*, 3> padBarsChoices { "1 bar", "2 bars", "4 bars" };

/** Auto-evolve choices, and how many bars apart each one evolves (0 = off). */
static constexpr std::array<const char*, 6> autoEvolveChoices { "Off", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" };
static constexpr std::array<int, 6> autoEvolveBars { 0, 1, 2, 4, 8, 16 };

/** Drive curves (cmfx::DriveType order). */
static constexpr std::array<const char*, 4> driveTypeChoices { "Soft", "Hard", "Fold", "Fuzz" };

/** Step-rate choices the global clock can run at. */
static constexpr std::array<const char*, 3> stepDivisionChoices { "1/8", "1/16", "1/32" };
/** Number of 1/16th-note "steps" each choice advances per step (used to derive
    samples-per-step from the host tempo). Expressed as quarter-note fraction. */
static constexpr std::array<double, 3> stepDivisionQuarterLength { 0.5, 0.25, 0.125 };

/** How float parameters show their values (knob text boxes and host
    automation lanes), and read back what a user types. */
namespace ValueText
{
    inline juce::AudioParameterFloatAttributes withText (std::function<juce::String (float)> toText,
                                                         std::function<float (const juce::String&)> fromText = nullptr)
    {
        if (fromText == nullptr)
            fromText = [] (const juce::String& t) { return t.retainCharacters ("+-.0123456789").getFloatValue(); };
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
            .withValueFromStringFunction (std::move (fromText));
    }

    inline juce::AudioParameterFloatAttributes percent()
    {
        return withText ([] (float v) { return juce::String (juce::roundToInt (v)) + " %"; });
    }

    /** A 0-1 gain shown in dB. */
    inline juce::AudioParameterFloatAttributes decibels()
    {
        return withText ([] (float v) { return juce::Decibels::toString (juce::Decibels::gainToDecibels (v), 1); },
                         [] (const juce::String& t)
                         {
                             if (t.containsIgnoreCase ("inf"))
                                 return 0.0f;
                             return juce::Decibels::decibelsToGain (t.retainCharacters ("+-.0123456789").getFloatValue());
                         });
    }

    inline juce::AudioParameterFloatAttributes milliseconds()
    {
        return withText ([] (float v) { return juce::String (v, 1) + " ms"; });
    }

    inline juce::AudioParameterFloatAttributes semitones()
    {
        return withText ([] (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " st"; });
    }
}

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
            juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f, ValueText::percent()));

        params.push_back (std::make_unique<juce::AudioParameterInt> (
            ParamIDs::voiceParam (v, "everyN"), juce::String (v.displayName) + " Every N", 1, 8, 1));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "swing"), juce::String (v.displayName) + " Swing",
            juce::NormalisableRange<float> (0.0f, 75.0f), 0.0f, ValueText::percent()));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "humanizeMs"), juce::String (v.displayName) + " Humanize",
            juce::NormalisableRange<float> (0.0f, 30.0f), 4.0f, ValueText::milliseconds()));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "level"), juce::String (v.displayName) + " Level",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f, ValueText::decibels()));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "pitch"), juce::String (v.displayName) + " Pitch",
            juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, ValueText::semitones()));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            ParamIDs::voiceParam (v, "drive"), juce::String (v.displayName) + " Drive",
            juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, ValueText::percent()));
    }

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::density, "Density", juce::NormalisableRange<float> (0.0f, 100.0f), 50.0f, ValueText::percent()));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::stepDivision, "Step Division",
        juce::StringArray (stepDivisionChoices.data(), (int) stepDivisionChoices.size()), 1));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParamIDs::midiOutEnabled, "MIDI Out", true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParamIDs::synthEnabled, "Internal Synth", true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::masterGain, "Master Gain", juce::NormalisableRange<float> (0.0f, 1.5f), 0.9f, ValueText::decibels()));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::driveType, "Drive Type",
        juce::StringArray (driveTypeChoices.data(), (int) driveTypeChoices.size()), 0));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParamIDs::limiter, "Limiter", true));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::autoEvolve, "Auto Evolve",
        juce::StringArray (autoEvolveChoices.data(), (int) autoEvolveChoices.size()), 0));

    // Added with the perform pad (after every older parameter, so their order in a host is unchanged).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::complexity, "Complexity", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, ValueText::percent()));

    params.push_back (std::make_unique<juce::AudioParameterBool> (ParamIDs::padHold, "Pad Hold", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::padRelease, "Pad Release", juce::NormalisableRange<float> (0.0f, 4.0f, 0.0f, 0.5f), 0.5f,
        ValueText::withText ([] (float v) { return juce::String (v, 2) + " s"; })));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        ParamIDs::padBars, "Pad Motion Length",
        juce::StringArray (padBarsChoices.data(), (int) padBarsChoices.size()), 0));

    params.push_back (std::make_unique<juce::AudioParameterBool> (ParamIDs::padBall, "Pad Ball", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::padFriction, "Pad Friction", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f,
        ValueText::withText ([] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; },
                             [] (const juce::String& t) { return t.retainCharacters ("+-.0123456789").getFloatValue() / 100.0f; })));

    params.push_back (std::make_unique<juce::AudioParameterBool> (ParamIDs::padWrap, "Pad Wrap", false));

    return { params.begin(), params.end() };
}
} // namespace plydrm
