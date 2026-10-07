# Building Plydrm

Plydrm is a CMake + JUCE project. JUCE itself is **not** vendored in this
repo - CMake's `FetchContent` downloads a pinned JUCE release (tag
`9.0.3`, see `CMakeLists.txt`) automatically on first configure. To use a
JUCE checkout you already have (version 9), pass
`-DPLYDRM_JUCE_PATH=/path/to/JUCE` on the first configure, or set the
`PLYDRM_JUCE_PATH` environment variable for `build.sh`.

## Prerequisites

- CMake >= 3.22 (JUCE 9 needs it)
- A C++20 compiler:
  - **Windows**: Visual Studio 2022 (Desktop development with C++ workload)
  - **macOS**: Xcode 15+ and the command line tools (`xcode-select --install`)
- Git (used by FetchContent to clone JUCE)
- **macOS**: Ninja for `build.sh` (`brew install cmake ninja`)

## macOS (VST3 + AU + Standalone): `build.sh`

```bash
./build.sh            # configure (if needed), build Release, run the tests
./build.sh build      # build only
./build.sh test       # run the tests only
./build.sh install    # build, then copy the VST3 and AU to your plug-in folders
./build.sh run        # build, then start the standalone app - no DAW needed
./build.sh clean      # delete the build folder
```

If `build.sh` says your cmake is too old, an older cmake is earlier on your
`PATH` than Homebrew's (for example `/usr/local/bin/cmake`); put
`/opt/homebrew/bin` first or remove the old one.

`install` copies the plugins to:

```
~/Library/Audio/Plug-Ins/VST3/Plydrm.vst3
~/Library/Audio/Plug-Ins/Components/Plydrm.component
```

and signs them again (ad hoc). JUCE adds a file to the VST3 after signing it,
so a freshly built bundle can be refused by Ableton Live until it is re-signed.
Set `VST3_DIR` / `AU_DIR` to install somewhere else. The build itself never
copies anything (`COPY_PLUGIN_AFTER_BUILD` is off).

To check the AU after installing:

```bash
auval -v aumu Pld1 Plyd
```

(`Pld1` / `Plyd` are the plugin/manufacturer codes set in `CMakeLists.txt`.)

## Windows (VST3 + Standalone)

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

To get Plydrm into your DAW's scan path, either:

- Copy the `.vst3` folder above into
  `C:\Program Files\Common Files\VST3\` yourself (needs admin), or
- Point your DAW's custom VST3 scan path directly at
  `build\Plydrm_artefacts\Release\VST3\`.

## Running the pattern-engine unit tests

Everything that decides when a drum hits and how it sounds
(`Source/Engine/*`: the Euclidean patterns, the transport clock and the step
scheduler; `Source/Synth/*`: the drum synth and the output stage) is plain
C++ with no JUCE, built as the `plydrm_engine` library. `PlydrmTests` tests
it without a host and builds without JUCE at all (`-DPLYDRM_BUILD_PLUGIN=OFF`
skips the plugin):

```bash
cmake --build build --target PlydrmTests
ctest --test-dir build -C Release --output-on-failure
```

`ProcessorTests` (built with the plugin) drives the real processor through a
fake host play head. It checks that:

- each synth hit starts on its own sample at several block sizes
- hits land on the host's grid, and the plugin is silent while the host is
  stopped
- `processBlock` never allocates (it counts `operator new` around it, while
  parameters, block sizes, tempo and transport change)
- every MIDI note-on gets its note-off, even when MIDI Out is switched off or
  the host calls `prepareToPlay` while a note is on
- pressing New Seed from another thread while audio runs is safe

`SoakTests` (ported from Brimstone) fuzzes the whole audio path: random
parameter changes, Evolve, New Seed, tempo changes and ramps, transport
stops / starts / jumps / loops, missing host fields and play heads, odd time
signatures and re-prepare, at five sample rates, block sizes 1-4096, mono
and stereo. The output must stay finite and bounded, MIDI well formed with
no hanging notes, and `processBlock` must never allocate. It also renders the
same sessions with different block layouts and requires bit-identical audio
and MIDI (an offline export sounds exactly like the realtime session), and
prints the cost per sample of a few patches. `ctest` runs a quick version;
for the long one:

```bash
./build/SoakTests full
```

`EditorTests` builds the real editor without a window and checks its layout
(every control inside the window, none overlapping) and the pattern view.
`./build/EditorTests editor.png` also saves a snapshot of the editor.

To check `ProcessorTests`' New Seed test for data races, build it with
ThreadSanitizer:

```bash
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_C_FLAGS=-fsanitize=thread \
    -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan --target ProcessorTests
./build-tsan/ProcessorTests
```

## Loading Plydrm in a DAW for testing

1. Build and install as above (most DAWs already scan the default VST3/AU
   folders), and rescan plug-ins if your DAW caches its list.
2. Create a new MIDI/instrument track and insert "Plydrm" as an
   instrument (it's registered as a synth, `IS_SYNTH TRUE`, since it
   generates its own audio).
3. Press play. Plydrm follows the host transport: step 0 is the start of
   the song, so the pattern lines up with bars and comes out the same
   wherever you start playback. While the host is stopped it is silent.
   With no host transport at all (the standalone app) it free-runs at the
   host's tempo, or 120 BPM.
4. Toggle **MIDI Out** on and arm a second instrument track pointed at
   Plydrm's MIDI output (route it the way your DAW routes inter-track
   MIDI, e.g. Ableton's "MIDI From" dropdown) to drive an external drum
   sampler/VST with the generated pattern, independently of (or alongside)
   the internal synth engine. Logic does not take MIDI output from an AU
   instrument.
5. Use **Evolve / Mutate** to nudge hits/rotation/probability/swing, and
   **New Seed** to re-roll the probability/humanize/synth randomization
   without changing the Euclidean pattern shape itself.

## Notes on the CMake structure

- `plydrm_engine` (static library, no JUCE) holds `Source/Engine/*` and
  `Source/Synth/*`. The plugin and every test executable link it.
- `juce_add_plugin(Plydrm ...)` declares the plugin target; `FORMATS` is
  `VST3` + `Standalone` on Windows, plus `AU` on macOS, and `VST3` only on
  Linux.
- `-ffp-contract=off` is set for every target (JUCE included) so Clang on
  Apple Silicon does the same floating-point arithmetic as MSVC, and step
  timing matches across platforms.
