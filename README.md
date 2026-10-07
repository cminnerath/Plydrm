# Plydrm

An automatic polyrhythm / Euclidean drum pattern generator plugin (VST3 /
AU) - think "auto Aphex Twin" for drum beats. Plydrm continuously generates
evolving Euclidean/polymetric drum patterns locked to your host's tempo and timeline, and
can drive the pattern out as **MIDI** (to an external drum sampler/VST),
through a **built-in synthesized drum voice engine** (kick/snare/hat/perc,
pure synthesis - no samples), or both at once.

See [`BUILD.md`](BUILD.md) for build and DAW-loading instructions.

## Features

- Bjorklund's Euclidean rhythm algorithm per voice (kick / snare / hat /
  perc), each with its own independent step-loop length so voices phase
  against each other (polymeter/polyrhythm).
- Per-voice probability (chance a given hit actually fires) and "fire
  every Nth repeat" conditional triggers.
- Per-voice swing and randomized micro-timing humanization.
- A global density/complexity macro, plus an "Evolve / Mutate" button that
  nudges pattern parameters, and a "New Seed" button to re-roll the
  randomization.
- Simple synthesized drum voices (sine + pitch-envelope kick, filtered
  noise snare/hat, blended tone+noise perc) with randomized per-hit
  pitch/decay/filter variation, using cmfx's state-variable filters.
- Per-voice drive through one of four level-matched curves (Soft, Hard,
  Fold, Fuzz), a smoothed master gain and a soft output limiter.
- A pattern view: every voice's pattern on one shared 32-step grid, with
  each voice's playhead and a flash on every hit that actually plays.
- Simultaneous MIDI-out (GM-ish drum map: kick=36, snare=38, closed
  hat=42, perc=39) and internal-synth output, independently toggled.
- All parameters exposed via `juce::AudioProcessorValueTreeState`, so
  they're automatable and save/restore with your session.

## Project layout

```
CMakeLists.txt              Top-level build (JUCE 9 via FetchContent)
build.sh                    macOS build / test / install script
Source/
  Params.h                  Parameter IDs, voice table, APVTS layout
  PluginProcessor.*          AudioProcessor: engine <-> MIDI/audio glue
  PluginEditor.*              JUCE GUI: pattern view, a knob row per voice, global panel
  PatternView.*               The shared step grid (polls what the audio thread publishes)
  Theme.h                     Colours
  Engine/
    Euclidean.*               Bjorklund's algorithm (pure C++, unit tested)
    TransportClock.h          Host position -> musical clock (ported from Brimstone)
    PatternEngine.*           Sample-accurate polymetric trigger scheduler
  Synth/
    DrumSynth.*               Kick/snare/hat/perc synthesis voices (plain C++, unit tested)
    OutputStage.h             Smoothed master gain + soft limiter
  cmfx/                       Filters, drive curves, smoothing (vendored from Brimstone)
Tests/                        Engine unit tests, processor tests and soak tests (ctest)
BUILD.md                      Build + DAW-loading instructions
```
