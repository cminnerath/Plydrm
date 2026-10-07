#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace plydrm
{
/** Shows every voice's Euclidean pattern on one shared grid of 32 step
    columns, so voices with different loop lengths visibly phase against each
    other. Each row's playhead is the shared step modulo that voice's length,
    and a cell flashes when the voice actually plays (after probability and
    every-N). Display only: it polls what the audio thread publishes and
    never touches the engine. */
class PatternView final : public juce::Component,
                          private juce::Timer
{
public:
    explicit PatternView (PlydrmAudioProcessor&);
    ~PatternView() override;

    void paint (juce::Graphics&) override;

    /** Reads the latest published state and repaints if it changed (the
        timer calls this; tests call it directly). */
    void refresh();

    juce::Rectangle<int> cellBounds (int voice, int step) const;

private:
    static constexpr int labelWidth = 64;
    static constexpr int headerHeight = 16;

    void timerCallback() override { refresh(); }

    PlydrmAudioProcessor& processor;

    std::array<Pattern, numVoices> patterns {};
    std::array<bool, numVoices> enabled {};
    std::array<uint32_t, numVoices> seenHits {};
    std::array<float, numVoices> flash {};      // 1 on a hit, fading to 0
    int64_t step = 0;
    bool running = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternView)
};
} // namespace plydrm
