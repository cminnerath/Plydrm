// Builds the real editor without a window and checks its layout: every control
// is inside the editor, has room, and overlaps no other control (the old layout
// pushed the bottom knobs of each column off the window). Also checks the
// pattern view shows what the audio thread published.
//
//   EditorTests                 run the checks
//   EditorTests out.png         also save a snapshot of the editor (after a bar of playback)

#include "../Source/PluginEditor.h"
#include "FakeHost.h"

#include <cstdio>

using namespace plydrm;

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    juce::String describe (const juce::Component& c)
    {
        juce::String name = typeid (c).name();
        if (auto* label = dynamic_cast<const juce::Label*> (&c))
            name = "label '" + label->getText() + "'";
        else if (auto* button = dynamic_cast<const juce::Button*> (&c))
            name = "button '" + button->getButtonText() + "'";
        else if (dynamic_cast<const juce::Slider*> (&c) != nullptr)
            name = "slider";
        return name + " " + c.getBounds().toString();
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    PlydrmAudioProcessor p;
    test::FakePlayHead ph;
    p.setPlayHead (&ph);
    p.prepareToPlay (48000.0, 512);
    test::setParam (p.apvts, ParamIDs::voiceParam (voiceInfo[static_cast<size_t> (VoiceId::Perc)], "drive"), 40.0f);

    // A bar and a bit of playback, so there are patterns, a playhead and hits to show.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 200; ++b)
    {
        ph.moveTo ((juce::int64) b * 512);
        midi.clear();
        p.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> owned (p.createEditor());
    auto* editor = dynamic_cast<PlydrmAudioProcessorEditor*> (owned.get());
    check (editor != nullptr, "createEditor() makes a PlydrmAudioProcessorEditor");
    if (editor == nullptr)
        return 1;
    editor->getPatternView().refresh();

    std::printf ("layout (%d x %d)\n", editor->getWidth(), editor->getHeight());
    const auto bounds = editor->getLocalBounds();
    std::vector<juce::Component*> children;
    for (auto* c : editor->getChildren())
        if (c->isVisible())
            children.push_back (c);

    int outside = 0, tooSmall = 0, overlaps = 0;
    for (auto* c : children)
    {
        if (! bounds.contains (c->getBounds()))
        {
            ++outside;
            std::printf ("    outside the editor: %s\n", describe (*c).toRawUTF8());
        }
        if (c->getWidth() < 20 || c->getHeight() < 12)
        {
            ++tooSmall;
            std::printf ("    too small: %s\n", describe (*c).toRawUTF8());
        }
    }
    for (size_t i = 0; i < children.size(); ++i)
        for (size_t j = i + 1; j < children.size(); ++j)
            if (children[i]->getBounds().intersects (children[j]->getBounds()))
            {
                ++overlaps;
                std::printf ("    overlap: %s and %s\n", describe (*children[i]).toRawUTF8(), describe (*children[j]).toRawUTF8());
            }

    check (children.size() > 50, "the editor has its controls (" + juce::String ((int) children.size()) + " visible)");
    check (outside == 0, "every control is inside the editor");
    check (tooSmall == 0, "every control has room");
    check (overlaps == 0, "no two controls overlap");

    std::printf ("pattern view\n");
    {
        const auto kick = p.getDisplayPattern (VoiceId::Kick);
        const auto hat = p.getDisplayPattern (VoiceId::Hat);
        check (kick.length == voiceInfo[static_cast<size_t> (VoiceId::Kick)].defaultSteps
                   && hat.length == voiceInfo[static_cast<size_t> (VoiceId::Hat)].defaultSteps,
               "the processor publishes each voice's pattern length");
        check (kick == euclideanPattern (16, 4), "the published kick pattern is E(4,16)");
        check (p.getHitCount (VoiceId::Kick) > 0, "the processor counts the kick's hits");
        check (p.isClockRunning(), "the clock is running");

        // The last cell of a 12-step voice is the 12th column; columns don't overlap.
        auto& view = editor->getPatternView();
        check (view.cellBounds (2, 11).getRight() < view.cellBounds (2, 12).getX(), "pattern cells don't overlap");
        check (view.getLocalBounds().contains (view.cellBounds (numVoices - 1, kMaxPatternSteps - 1)),
               "the last cell fits in the pattern view");
    }

    if (argc > 1)
    {
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        check (out.openedOk() && png.writeImageToStream (image, out), "snapshot saved to " + file.getFullPathName());
    }

    owned.reset();
    if (failures == 0)
        std::printf ("All editor tests passed.\n");
    else
        std::printf ("%d editor test check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
