# Building Plydrm

Plydrm is a CMake + JUCE project. JUCE itself is **not** vendored in this
repo - CMake's `FetchContent` downloads a pinned JUCE release (tag
`7.0.12`, see `JUCE_TAG` in `CMakeLists.txt`) automatically on first
configure. This keeps the repo small and avoids a `git submodule update`
step that's easy to forget, at the cost of a one-time download (~100 MB)
and a working internet connection the first time you configure.

## Prerequisites

- CMake >= 3.22
- A C++17 compiler:
  - **Windows**: Visual Studio 2022 (Desktop development with C++ workload)
  - **macOS**: Xcode 14+ and the command line tools (`xcode-select --install`)
- Git (used by FetchContent to clone JUCE)

## Windows (VST3)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Plydrm_VST3
```

The first configure will clone JUCE into `build/_deps/juce-src` - this can
take a few minutes. Subsequent configures reuse the cached checkout.

The build produces:

```
build\Plydrm_artefacts\Release\VST3\Plydrm.vst3
```

`COPY_PLUGIN_AFTER_BUILD` is deliberately **off** by default: JUCE's
default install location on Windows is
`C:\Program Files\Common Files\VST3`, which a normal (non-admin) build
can't write to, and would otherwise make every build fail with a
"permission denied" error. To get Plydrm into your DAW's scan path, either:

- Copy the `.vst3` folder above into
  `C:\Program Files\Common Files\VST3\` yourself (one-time, needs admin), or
- Point your DAW's custom VST3 scan path directly at
  `build\Plydrm_artefacts\Release\VST3\`, or
- Set `COPY_PLUGIN_AFTER_BUILD TRUE` in `CMakeLists.txt` and build from an
  elevated/admin shell to have CMake copy it for you on every build.

## macOS (VST3 + AU)

```bash
cmake -S . -B build -G Xcode
cmake --build build --config Release --target Plydrm_VST3
cmake --build build --config Release --target Plydrm_AU
```

(Or open `build/Plydrm.xcodeproj` in Xcode and build the `Plydrm_VST3` /
`Plydrm_AU` schemes directly.)

Built plugins are copied automatically to:

```
~/Library/Audio/Plug-Ins/VST3/Plydrm.vst3
~/Library/Audio/Plug-Ins/Components/Plydrm.component
```

For AU you may need to re-run `auval -a` or restart your DAW's AU cache
the first time so it's recognized:

```bash
auval -v aumu Pld1 Plyd
```

(`Pld1` / `Plyd` are the plugin/manufacturer codes set in `CMakeLists.txt`.)

## Running the pattern-engine unit tests

The Euclidean/polymeter/probability logic lives in plain C++ classes
(`Source/Engine/Euclidean.*`, `Source/Engine/PatternEngine.*`) with no
audio-callback or GUI dependencies, so it's covered by a small standalone
test executable (`PlydrmTests`, built alongside the plugin by default -
set `-DPLYDRM_BUILD_TESTS=OFF` to skip it):

```bash
cmake --build build --config Release --target PlydrmTests
ctest --test-dir build -C Release --output-on-failure
```

## Loading Plydrm in a DAW for testing

1. Build the `Plydrm_VST3` (and `Plydrm_AU` on macOS) target as above.
2. Point your DAW at the install folder above (most DAWs already scan the
   default VST3/AU locations; Reaper/Ableton/Bitwig/Cubase all do by
   default on both platforms).
3. Rescan plugins if your DAW caches its plugin list.
4. Create a new MIDI/instrument track and insert "Plydrm" as an
   instrument (it's registered as a synth, `IS_SYNTH TRUE`, since it can
   generate its own audio).
5. Press play in the DAW - Plydrm free-runs even with no incoming MIDI; it
   reads the host's tempo via `juce::AudioPlayHead`, or falls back to an
   internal 120 BPM clock when the host isn't reporting a tempo (e.g. a
   bare audio-only test host).
6. Toggle **MIDI Out** on and arm a second instrument track pointed at
   Plydrm's MIDI output (route it the way your DAW routes inter-track
   MIDI, e.g. Ableton's "MIDI From" dropdown) to drive an external drum
   sampler/VST with the generated pattern, independently of (or alongside)
   the internal synth engine.
7. Use **Evolve / Mutate** to nudge hits/rotation/probability/swing over
   time, and **New Seed** to re-roll the probability/humanize/synth
   randomization without changing the Euclidean pattern shape itself.

## Notes on the CMake structure

- `juce_add_plugin(Plydrm ...)` declares the plugin target; `FORMATS` is
  `VST3` on Windows and `VST3;AU` on macOS (set conditionally near the top
  of `CMakeLists.txt`).
- `PlydrmTests` is a second, independent executable that links only
  `juce_core` plus the two plain-C++ engine source files - it does not
  link JUCE's audio/GUI modules, so it builds fast and can run in CI
  without any audio hardware.
