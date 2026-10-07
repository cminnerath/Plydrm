#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Plydrm build script for macOS (and Linux). Windows: see BUILD.md.
# Needs CMake 3.22+ and Ninja (brew install cmake ninja); uses ccache if it's installed.
# Set PLYDRM_JUCE_PATH to use a local JUCE checkout instead of downloading one.
#
#   ./build.sh            configure (if needed) + build Release + run tests
#   ./build.sh build      configure (if needed) + build, no tests
#   ./build.sh test       run tests only
#   ./build.sh clean      delete the build folder
#   ./build.sh install    build, then copy the VST3 to $VST3_DIR (and on macOS the AU to $AU_DIR)
#   ./build.sh run        build, then start the standalone app (free-runs at 120 BPM) - no DAW needed
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
ACTION="${1:-all}"
case "$ACTION" in
    all|build|test|clean|install|run) ;;
    *) echo "Unknown action: $ACTION (use build, test, clean, install or run)"; exit 1 ;;
esac

# Where "install" copies the plugin: the user's own plug-in folders, which every host scans.
if [[ "$(uname)" == "Darwin" ]]; then
    VST3_DIR="${VST3_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}"
    AU_DIR="${AU_DIR:-$HOME/Library/Audio/Plug-Ins/Components}"
else
    VST3_DIR="${VST3_DIR:-$HOME/.vst3}"
    AU_DIR=""
fi

ARTEFACTS="$BUILD/Plydrm_artefacts/Release"

if [[ "$ACTION" == "clean" ]]; then
    rm -rf "$BUILD"
    echo "Removed build folder."
    exit 0
fi

command -v cmake >/dev/null || { echo "ERROR: cmake not found (brew install cmake)."; exit 1; }
# JUCE 9 needs CMake 3.22+. An older cmake earlier on PATH (e.g. /usr/local/bin) hides Homebrew's.
CMAKE_VERSION="$(cmake --version | head -1 | awk '{print $3}')"
if [[ "$(printf '%s\n' 3.22 "$CMAKE_VERSION" | sort -V | head -1)" != "3.22" ]]; then
    echo "ERROR: cmake $CMAKE_VERSION at $(command -v cmake) is too old; JUCE 9 needs 3.22+ (brew install cmake, and put"
    echo "       $(brew --prefix 2>/dev/null || echo /opt/homebrew)/bin first on PATH)."
    exit 1
fi
command -v ninja >/dev/null || { echo "ERROR: ninja not found (brew install ninja)."; exit 1; }

LAUNCHER=()
if command -v ccache >/dev/null; then
    LAUNCHER=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

# Copies a bundle over an older copy of itself (a stale file left inside an old bundle can break it).
copy_bundle()
{
    local src="$1" dest_dir="$2"
    mkdir -p "$dest_dir"
    rm -rf "$dest_dir/$(basename "$src")"
    cp -R "$src" "$dest_dir/" || { echo "ERROR: Copy of $(basename "$src") failed."; exit 1; }
    if [[ "$(uname)" == "Darwin" ]]; then
        # Sign again (ad hoc) now the bundle is final. JUCE signs a VST3 and then adds Resources/moduleinfo.json, so after a
        # clean build the signature doesn't cover that file and Live refuses the plug-in ("a sealed resource is missing or
        # invalid"); a rebuild happens to work only because the file is already there, unchanged, when JUCE signs.
        codesign --force --sign - "$dest_dir/$(basename "$src")" 2>/dev/null \
            || { echo "ERROR: Couldn't sign $(basename "$src")."; exit 1; }
    fi
    echo "Installed $dest_dir/$(basename "$src")"
}

if [[ "$ACTION" != "test" ]]; then
    # Configure (once). A build folder from another generator is replaced.
    if [[ -f "$BUILD/CMakeCache.txt" && ! -f "$BUILD/build.ninja" ]]; then
        echo "Existing build folder used a different generator - recreating it."
        rm -rf "$BUILD"
    fi
    if [[ ! -f "$BUILD/build.ninja" ]]; then
        JUCE_ARGS=()
        [[ -n "${PLYDRM_JUCE_PATH:-}" ]] && JUCE_ARGS=(-DPLYDRM_JUCE_PATH="$PLYDRM_JUCE_PATH")
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release "${LAUNCHER[@]}" "${JUCE_ARGS[@]}"
    fi

    cmake --build "$BUILD"
    echo
    echo "Plugin: $ARTEFACTS/VST3/Plydrm.vst3"
    [[ -d "$ARTEFACTS/AU/Plydrm.component" ]] && echo "        $ARTEFACTS/AU/Plydrm.component"
fi

case "$ACTION" in
    build)
        ;;
    install)
        copy_bundle "$ARTEFACTS/VST3/Plydrm.vst3" "$VST3_DIR"
        if [[ -n "$AU_DIR" && -d "$ARTEFACTS/AU/Plydrm.component" ]]; then
            copy_bundle "$ARTEFACTS/AU/Plydrm.component" "$AU_DIR"
            # Make macOS notice the new AU without logging out.
            killall -9 AudioComponentRegistrar 2>/dev/null || true
        fi
        echo "Rescan plug-ins in your DAW."
        ;;
    run)
        if [[ "$(uname)" == "Darwin" ]]; then
            STANDALONE="$ARTEFACTS/Standalone/Plydrm.app"
            [[ -d "$STANDALONE" ]] || { echo "ERROR: $STANDALONE was not built."; exit 1; }
            echo "Starting $STANDALONE"
            open "$STANDALONE"
        else
            STANDALONE="$ARTEFACTS/Standalone/Plydrm"
            [[ -x "$STANDALONE" ]] || { echo "ERROR: $STANDALONE was not built."; exit 1; }
            echo "Starting $STANDALONE"
            "$STANDALONE" &
        fi
        ;;
    all|test)
        ctest --test-dir "$BUILD" --output-on-failure
        ;;
esac
