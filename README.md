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
- Two global macros on top of every voice's knobs: **Density** scales the
  hits (0.4x to 1.6x), and **Complexity** pulls the voices apart (the snare,
  hat and perc loops shorten to lengths that phase against the kick's, and
  hits drop out). At their defaults (50 % and 0 %) they change nothing.
- A **perform pad** (cmperf's XY pad): Density across, Complexity up. A
  touch takes both macros over from their knobs and letting go hands them
  back over Release, unless Hold is on. **Pad Motion** records 1, 2 or 4 bars
  of the pad and loops it on the bar (saved with the session), and **Ball**
  lets a lift flick a ball that keeps moving them (Wrap with Friction 0
  never stops).
- An "Evolve / Mutate" button that nudges pattern parameters, and Auto
  Evolve to do it every 1-16 bars (in time for the downbeat, recorded by the
  host like a user edit).
- A seed behind every random choice (probability rolls, humanize, velocity,
  per-hit sound variation). It is saved with the session, and each random
  value depends only on the seed and the song position, so a session plays
  the same every time, wherever playback starts and in an offline bounce.
  "New Seed" picks another.
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

## Ideas (not done)

From the iOS ambient and drone survey in Crucible
(`docs/research/ios-ambient-synths-vs-aether.md`, 2026-10-10), the items
that fit a pattern generator:

- **Seed as a text code:** show the session's seed as a short code
  (Low Poly Synth's 8 characters) that can be copied and pasted back, so a
  pattern someone likes is shareable in a message; "New Seed" stays.
- **Pad Evolve modes** from cmperf item 26: Wander, Breathe and Decay on
  the perform pad's Density and Complexity position, and a glide time
  when a position is tapped or automated. Auto Evolve already mutates the
  pattern on the bar; this moves the macros between mutations.
- **MIDI learn** for the knobs and the pad's settings (Brimstone's learn
  is the reference; Crucible is porting it into its shared layer).
- **Record to file** and Ableton Link in the standalone, if an iOS build
  is ever made.

Plydrm is also a source for the others: its Auto Evolve (mutate every
1-16 bars, in time for the downbeat, recorded by the host) and its
seed-and-song-position randomness are the model for Aether's patch Evolve
and its randomizer's seed; its MIDI-out path is the one Aether and
Brimstone copy for a MIDI bus.

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
    StepRandom.h              Seeded randomness as a function of (seed, voice, step)
    TransportClock.h          Host position -> musical clock (ported from Brimstone)
    PatternEngine.*           Sample-accurate polymetric trigger scheduler
    Macros.h                  Density and Complexity, applied on top of each voice's knobs
  Synth/
    DrumSynth.*               Kick/snare/hat/perc synthesis voices (plain C++, unit tested)
    OutputStage.h             Smoothed master gain + soft limiter
  cmfx/                       Filters, drive curves, smoothing (vendored from Brimstone)
Tests/                        Engine unit tests, processor tests and soak tests (ctest)
BUILD.md                      Build + DAW-loading instructions
```
