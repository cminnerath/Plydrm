#include "PatternView.h"
#include "Theme.h"

namespace plydrm
{

PatternView::PatternView (PlydrmAudioProcessor& p)
    : processor (p)
{
    for (int v = 0; v < numVoices; ++v)
        seenHits[static_cast<size_t> (v)] = processor.getHitCount (static_cast<VoiceId> (v));

    refresh();
    startTimerHz (30);
}

PatternView::~PatternView()
{
    stopTimer();
}

juce::Rectangle<int> PatternView::cellBounds (int voice, int stepIndex) const
{
    const auto area = getLocalBounds().reduced (8, 6).withTrimmedTop (headerHeight).withTrimmedLeft (labelWidth);
    const float cellW = (float) area.getWidth() / (float) kMaxPatternSteps;
    const float rowH = (float) area.getHeight() / (float) numVoices;

    const int x0 = area.getX() + juce::roundToInt (cellW * (float) stepIndex);
    const int x1 = area.getX() + juce::roundToInt (cellW * (float) (stepIndex + 1));
    const int y0 = area.getY() + juce::roundToInt (rowH * (float) voice);
    const int y1 = area.getY() + juce::roundToInt (rowH * (float) (voice + 1));
    return { x0 + 1, y0 + 2, x1 - x0 - 2, y1 - y0 - 4 };
}

void PatternView::refresh()
{
    bool changed = false;

    const auto newStep = processor.getDisplayStep();
    const bool newRunning = processor.isClockRunning();
    changed |= newStep != step || newRunning != running;
    step = newStep;
    running = newRunning;

    for (int v = 0; v < numVoices; ++v)
    {
        const auto i = static_cast<size_t> (v);
        const auto voice = static_cast<VoiceId> (v);

        const auto pattern = processor.getDisplayPattern (voice);
        changed |= ! (pattern == patterns[i]);
        patterns[i] = pattern;

        const bool on = processor.apvts.getRawParameterValue (ParamIDs::voiceParam (voiceInfo[i], "enabled"))->load() > 0.5f;
        changed |= on != enabled[i];
        enabled[i] = on;

        const auto hits = processor.getHitCount (voice);
        if (hits != seenHits[i])
        {
            seenHits[i] = hits;
            flash[i] = 1.0f;
            changed = true;
        }
        else if (flash[i] > 0.0f)
        {
            flash[i] = flash[i] < 0.05f ? 0.0f : flash[i] * 0.7f;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void PatternView::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // Step numbers on every beat (4 steps).
    g.setFont (juce::FontOptions (10.0f));
    for (int s = 0; s < kMaxPatternSteps; s += 4)
    {
        const auto c = cellBounds (0, s);
        g.setColour (theme::dimText);
        g.drawText (juce::String (s + 1), c.getX(), c.getY() - headerHeight - 2, c.getWidth() + 8, headerHeight,
                    juce::Justification::centredLeft);
    }

    for (int v = 0; v < numVoices; ++v)
    {
        const auto i = static_cast<size_t> (v);
        const auto& pattern = patterns[i];
        const auto colour = theme::voiceColour (static_cast<VoiceId> (v));
        const float rowAlpha = enabled[i] ? 1.0f : 0.3f;

        const auto first = cellBounds (v, 0);
        g.setColour (colour.withAlpha (rowAlpha));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (voiceInfo[i].displayName, 8, first.getY(), labelWidth - 30, first.getHeight(), juce::Justification::centredLeft);
        g.setColour (theme::dimText.withAlpha (rowAlpha));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (pattern.length), labelWidth - 26, first.getY(), 22, first.getHeight(), juce::Justification::centredRight);

        const int playing = (running && pattern.length > 0)
                              ? static_cast<int> (((step % pattern.length) + pattern.length) % pattern.length)
                              : -1;

        for (int s = 0; s < kMaxPatternSteps; ++s)
        {
            const auto c = cellBounds (v, s).toFloat();

            if (s >= pattern.length)
            {
                // Past the loop: an outline only, so the loop length is visible.
                g.setColour (theme::outline.withAlpha (0.35f * rowAlpha));
                g.drawRoundedRectangle (c.reduced (0.5f), 3.0f, 1.0f);
                continue;
            }

            g.setColour (theme::cell.withAlpha (rowAlpha));
            g.fillRoundedRectangle (c, 3.0f);

            if (pattern[s])
            {
                g.setColour (colour.withAlpha (rowAlpha * (s == playing ? 1.0f : 0.7f)));
                g.fillRoundedRectangle (c.reduced (2.0f), 2.0f);
            }

            if (s == playing)
            {
                g.setColour (theme::text.withAlpha (rowAlpha * 0.9f));
                g.drawRoundedRectangle (c.reduced (0.5f), 3.0f, 1.5f);

                if (flash[i] > 0.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.5f * flash[i] * rowAlpha));
                    g.fillRoundedRectangle (c, 3.0f);
                }
            }
        }
    }
}

} // namespace plydrm
