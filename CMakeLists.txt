cmake_minimum_required(VERSION 3.22)

project(Plydrm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PLYDRM_BUILD_TESTS "Build the standalone pattern-engine unit tests" ON)

# ---------------------------------------------------------------------------
# JUCE acquisition
# ---------------------------------------------------------------------------
# JUCE is pulled in with FetchContent rather than a git submodule so that a
# fresh clone of this repo "just works" with a single CMake configure step
# (no `git submodule update --init` step to forget, no CPM bootstrap script
# to vendor). The version is pinned for reproducible builds; bump JUCE_TAG
# to pick up a newer JUCE release.
include(FetchContent)

set(JUCE_TAG "7.0.12" CACHE STRING "JUCE git tag to fetch")

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${JUCE_TAG}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
set(PLYDRM_FORMATS VST3)
if(APPLE)
    list(APPEND PLYDRM_FORMATS AU)
endif()

# On Windows, JUCE's default VST3 copy destination is
# "C:/Program Files/Common Files/VST3", which needs admin rights to write
# to and would break a plain `cmake --build` for most developers. macOS's
# default (~/Library/Audio/Plug-Ins) is user-writable, so we only disable
# auto-copy on Windows - see BUILD.md for how to install the plugin there.
if(WIN32)
    set(PLYDRM_COPY_PLUGIN_AFTER_BUILD FALSE)
else()
    set(PLYDRM_COPY_PLUGIN_AFTER_BUILD TRUE)
endif()

juce_add_plugin(Plydrm
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Plydrm"
    PLUGIN_MANUFACTURER_CODE Plyd
    PLUGIN_CODE Pld1
    FORMATS ${PLYDRM_FORMATS}
    PRODUCT_NAME "Plydrm"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # On Windows, JUCE's default VST3 copy destination is
    # "C:/Program Files/Common Files/VST3", which needs admin rights to
    # write to and would break a plain `cmake --build` for most developers.
    # macOS's default (~/Library/Audio/Plug-Ins) is user-writable, so we
    # only disable auto-copy on Windows - see BUILD.md.
    COPY_PLUGIN_AFTER_BUILD ${PLYDRM_COPY_PLUGIN_AFTER_BUILD}
    VST3_CATEGORIES "Instrument" "Drum"
    AU_MAIN_TYPE "kAudioUnitType_MusicDevice")

juce_generate_juce_header(Plydrm)

target_sources(Plydrm
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Engine/Euclidean.cpp
        Source/Engine/PatternEngine.cpp
        Source/Synth/DrumSynth.cpp)

target_compile_definitions(Plydrm
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Plydrm
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_include_directories(Plydrm PRIVATE Source)

# ---------------------------------------------------------------------------
# Unit tests for the plain-C++ pattern engine (no JUCE audio graph needed)
# ---------------------------------------------------------------------------
if(PLYDRM_BUILD_TESTS)
    add_executable(PlydrmTests
        Tests/TestMain.cpp
        Tests/EuclideanTests.cpp
        Tests/PatternEngineTests.cpp
        Source/Engine/Euclidean.cpp
        Source/Engine/PatternEngine.cpp)

    target_include_directories(PlydrmTests PRIVATE Source)
    target_link_libraries(PlydrmTests PRIVATE juce::juce_core)
    target_compile_definitions(PlydrmTests PRIVATE JUCE_STANDALONE_APPLICATION=1)

    enable_testing()
    add_test(NAME PlydrmTests COMMAND PlydrmTests)
endif()
