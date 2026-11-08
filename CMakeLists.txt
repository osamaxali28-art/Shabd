cmake_minimum_required(VERSION 3.22)
project(Shabd VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # whisper/ggml are linked into a plugin (shared library)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(SHABD_CORE_ONLY    "Build only the Roman Urdu engine + tests (no whisper, no JUCE)" OFF)
option(SHABD_BUILD_CLI    "Build shabd-cli (offline transcription of a WAV)" ON)
option(SHABD_BUILD_PLUGIN "Build the VST3 / AU / Standalone plugin" ON)

if(MSVC)
    add_compile_options(/utf-8) # source files contain Urdu text
endif()

# ------------------------------------------------------------------ core
# Embed the lexicon so the plugin works without any extra files.
set(SHABD_LEXICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/lexicon/roman_urdu.tsv)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SHABD_LEXICON_FILE})
file(READ ${SHABD_LEXICON_FILE} SHABD_LEXICON_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," SHABD_LEXICON_BYTES "${SHABD_LEXICON_HEX}")
configure_file(cmake/LexiconData.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/LexiconData.cpp @ONLY)

add_library(shabd_core STATIC
    Source/core/RomanUrdu.cpp
    Source/core/Subtitles.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/LexiconData.cpp)
target_include_directories(shabd_core PUBLIC Source/core)

enable_testing()
add_executable(shabd_core_tests tests/core_tests.cpp)
target_link_libraries(shabd_core_tests PRIVATE shabd_core)
add_test(NAME core COMMAND shabd_core_tests ${SHABD_LEXICON_FILE})

if(SHABD_CORE_ONLY)
    return()
endif()

include(FetchContent)

# ------------------------------------------------------------------ whisper.cpp
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
# Portable builds: don't tune for the CI machine's CPU, but keep the fast paths
# every recent desktop CPU has.
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64" AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(GGML_AVX ON CACHE BOOL "" FORCE)
    set(GGML_AVX2 ON CACHE BOOL "" FORCE)
    set(GGML_FMA ON CACHE BOOL "" FORCE)
    set(GGML_F16C ON CACHE BOOL "" FORCE)
endif()
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE) # no separate .metal file to ship
endif()

FetchContent_Declare(whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG v1.9.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(whisper)

add_library(shabd_engine STATIC
    Source/engine/Detector.cpp
    Source/engine/LiveSession.cpp)
target_include_directories(shabd_engine PUBLIC Source)
target_link_libraries(shabd_engine PUBLIC shabd_core whisper)
find_package(Threads REQUIRED)
target_link_libraries(shabd_engine PUBLIC Threads::Threads)

if(SHABD_BUILD_CLI)
    add_executable(shabd-cli Source/cli/main.cpp)
    target_link_libraries(shabd-cli PRIVATE shabd_engine)
endif()

# ------------------------------------------------------------------ plugin
if(SHABD_BUILD_PLUGIN)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    set(SHABD_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND SHABD_FORMATS AU)
    endif()

    juce_add_plugin(Shabd
        COMPANY_NAME "Itsthemoji"
        BUNDLE_ID com.itsthemoji.shabd
        PLUGIN_MANUFACTURER_CODE Moji
        PLUGIN_CODE Shbd
        PRODUCT_NAME "Shabd"
        DESCRIPTION "Real-time Urdu rap transcription and subtitle export"
        FORMATS ${SHABD_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Fx Analyzer
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(Shabd PRIVATE
        Source/plugin/PluginProcessor.cpp
        Source/plugin/PluginEditor.cpp
        Source/plugin/TranscriptView.cpp)

    target_compile_definitions(Shabd PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    if(UNIX AND NOT APPLE)
        # Linux builds are only used to compile-check in CI.
        target_compile_definitions(Shabd PUBLIC
            JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0
            JUCE_ALSA=0 JUCE_JACK=0)
    endif()

    target_link_libraries(Shabd
        PRIVATE
            shabd_engine
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
