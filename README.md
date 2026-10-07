# Plydrm

An automatic polyrhythm / Euclidean drum pattern generator plugin (VST3 /
AU) - think "auto Aphex Twin" for drum beats. Plydrm continuously generates
evolving Euclidean/polymetric drum patterns synced to your host's tempo and
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
  nudges pattern parameters over time, and a "New Seed" button to re-roll
  the randomization.
- Simple synthesized drum voices (sine + pitch-envelope kick, filtered
  noise snare/hat, blended tone+noise perc) with randomized per-hit
  pitch/decay/filter variation.
- Simultaneous MIDI-out (GM-ish drum map: kick=36, snare=38, closed
  hat=42, perc=39) and internal-synth output, independently toggled.
- All parameters exposed via `juce::AudioProcessorValueTreeState`, so
  they're automatable and save/restore with your session.

## Project layout

```
CMakeLists.txt              Top-level build (JUCE via FetchContent)
Source/
  Params.h                  Parameter IDs, voice table, APVTS layout
  PluginProcessor.*          AudioProcessor: engine <-> MIDI/audio glue
  PluginEditor.*              Plain JUCE GUI (knobs + toggles)
  Engine/
    Euclidean.*               Bjorklund's algorithm (pure C++, unit tested)
    PatternEngine.*           Sample-accurate polymetric trigger scheduler
  Synth/
    DrumSynth.*               Kick/snare/hat/perc synthesis voices
Tests/                        Standalone unit tests for Engine/* (ctest)
BUILD.md                      Build + DAW-loading instructions
```
