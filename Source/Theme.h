#pragma once

#include "Engine/VoiceTypes.h"
#include <juce_graphics/juce_graphics.h>

// Plydrm's colours: dark panels (after Brimstone's), one accent per voice.
namespace plydrm::theme
{
inline const juce::Colour background   { 0xff17191d };
inline const juce::Colour panel        { 0xff22252b };
inline const juce::Colour cell         { 0xff2d3138 };
inline const juce::Colour outline      { 0xff3a3f48 };
inline const juce::Colour text         { 0xffe8e6e3 };
inline const juce::Colour dimText      { 0xff8d8f96 };

inline juce::Colour voiceColour (VoiceId voice)
{
    switch (voice)
    {
        case VoiceId::Kick:  return juce::Colour (0xffff7a2e);   // Brimstone's accent
        case VoiceId::Snare: return juce::Colour (0xff4fc3f7);
        case VoiceId::Hat:   return juce::Colour (0xffffd54f);
        case VoiceId::Perc:
        case VoiceId::Count: break;
    }
    return juce::Colour (0xffba68c8);
}
} // namespace plydrm::theme
