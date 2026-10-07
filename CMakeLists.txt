cmake_minimum_required(VERSION 3.22)

project(Plydrm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PLYDRM_BUILD_PLUGIN "Build the audio plugin (needs JUCE)" ON)
option(PLYDRM_BUILD_TESTS  "Build the pattern-engine unit tests (no JUCE needed)" ON)
set(PLYDRM_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout. Leave empty to download JUCE automatically.")

# Never fuse a * b + c into one FMA instruction. Clang does on arm64 (Apple Silicon) and MSVC doesn't, and the fused
# result rounds differently, so step timing could differ by a sample between platforms. Set before JUCE is added so it
# gets it too: every platform does the same arithmetic.
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()

# ---------------------------------------------------------------------------
# Pattern engine: plain C++, no JUCE. Everything that decides when a drum hits
# lives here so it can be unit-tested without a host.
# ---------------------------------------------------------------------------
add_library(plydrm_engine STATIC
    Source/Engine/Euclidean.cpp
    Source/Engine/PatternEngine.cpp)
target_include_directories(plydrm_engine PUBLIC Source)

if(PLYDRM_BUILD_TESTS)
    enable_testing()
    add_executable(PlydrmTests
        Tests/TestMain.cpp
        Tests/EuclideanTests.cpp
        Tests/PatternEngineTests.cpp)
    target_link_libraries(PlydrmTests PRIVATE plydrm_engine)
    add_test(NAME PlydrmTests COMMAND PlydrmTests)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(PLYDRM_BUILD_PLUGIN)
    if(PLYDRM_JUCE_PATH)
        add_subdirectory(${PLYDRM_JUCE_PATH} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        9.0.3
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(juce)
    endif()

    # AU only exists on macOS. Standalone is handy for quick tests without a DAW
    # (with no host transport the pattern free-runs at 120 BPM).
    set(PLYDRM_FORMATS VST3)
    if(APPLE)
        list(APPEND PLYDRM_FORMATS AU)
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND PLYDRM_FORMATS Standalone)
    endif()

    juce_add_plugin(Plydrm
        VERSION                   ${PROJECT_VERSION}
        COMPANY_NAME              "Plydrm"
        PLUGIN_MANUFACTURER_CODE  Plyd
        PLUGIN_CODE               Pld1
        PRODUCT_NAME              "Plydrm"
        FORMATS                   ${PLYDRM_FORMATS}
        IS_SYNTH                  TRUE
        NEEDS_MIDI_INPUT          FALSE
        NEEDS_MIDI_OUTPUT         TRUE
        IS_MIDI_EFFECT            FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD   FALSE     # build.sh install copies (and re-signs); Program Files needs admin on Windows
        VST3_CATEGORIES           Instrument Drum
        AU_MAIN_TYPE              kAudioUnitType_MusicDevice)

    target_sources(Plydrm PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Synth/DrumSynth.cpp)

    target_compile_definitions(Plydrm PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(Plydrm
        PRIVATE
            plydrm_engine
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Integration test: drives the real processor with a fake play head.
    if(PLYDRM_BUILD_TESTS)
        # Links the plugin's shared-code library (which already contains the
        # compiled JUCE modules) and borrows its include paths and defines.
        # ProcessorTests: targeted checks of the JUCE glue. SoakTests: fuzz, block-layout determinism and cost
        # (`SoakTests full` is the long version).
        foreach(test_name ProcessorTests SoakTests)
            add_executable(${test_name} Tests/${test_name}.cpp)
            target_link_libraries(${test_name} PRIVATE Plydrm plydrm_engine)
            target_include_directories(${test_name} PRIVATE
                $<TARGET_PROPERTY:Plydrm,INCLUDE_DIRECTORIES>)
            target_compile_definitions(${test_name} PRIVATE
                $<TARGET_PROPERTY:Plydrm,COMPILE_DEFINITIONS>)
            add_test(NAME ${test_name} COMMAND ${test_name})
        endforeach()
    endif()
endif()
