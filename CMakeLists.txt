cmake_minimum_required(VERSION 3.24)

project(XYBass VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "Enable JUCE module source groups" FORCE)

FetchContent_Declare(
    JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.13.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    DOWNLOAD_NO_PROGRESS FALSE
)

FetchContent_Declare(
    clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG c1a5ad025f95d01e03267857fa8276ebeed16500
    GIT_SUBMODULES_RECURSE TRUE
)

FetchContent_MakeAvailable(JUCE clap-juce-extensions)

set(XYBASS_FORMATS Standalone VST3)

if(APPLE)
    list(APPEND XYBASS_FORMATS AU)
endif()

juce_add_plugin(XYBass
    COMPANY_NAME "23DSP"
    BUNDLE_ID com.23dsp.xybass
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE TDS3
    PLUGIN_CODE XyBs
    FORMATS ${XYBASS_FORMATS}
    PRODUCT_NAME "XY Bass"
    VST3_CATEGORIES Fx Dynamics Distortion
)

set(XYBASS_DSP_SOURCES
    src/BassEngine.cpp
    src/PitchTracker.cpp
    src/SourceAnalyser.cpp
    src/SpectralBalance.cpp
    src/SubEngine.cpp
    src/TranslateEngine.cpp
    src/XYMapping.cpp
)

target_sources(XYBass
    PRIVATE
        ${XYBASS_DSP_SOURCES}
        src/PluginEditor.cpp
        src/PluginProcessor.cpp
        src/Presets.cpp
        src/XYPad.cpp
)

target_compile_definitions(XYBass
    PUBLIC
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(XYBass
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

clap_juce_extensions_plugin(TARGET XYBass
    CLAP_ID "com.23dsp.xybass"
    CLAP_FEATURES audio-effect distortion mastering
)

if(MSVC)
    target_compile_options(XYBass PRIVATE /utf-8)
endif()

enable_testing()

juce_add_console_app(XYBassTests
    PRODUCT_NAME "XY Bass Tests"
)

target_sources(XYBassTests
    PRIVATE
        ${XYBASS_DSP_SOURCES}
        tests/EngineTests.cpp
)

target_include_directories(XYBassTests PRIVATE src)

target_compile_definitions(XYBassTests
    PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(XYBassTests
    PRIVATE
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(XYBassTests PRIVATE /utf-8)
endif()

add_test(NAME xy-bass-engine COMMAND XYBassTests)
