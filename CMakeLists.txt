cmake_minimum_required(VERSION 3.24)

set(XYBASS_VERSION "0.1.0" CACHE STRING "Plugin version, normally taken from the release tag")

project(XYBass VERSION ${XYBASS_VERSION} LANGUAGES C CXX)

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

function(xybass_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /arch:AVX)
    elseif(APPLE)
        if(CMAKE_OSX_ARCHITECTURES)
            # Only the Intel slice of a universal build can take AVX.
            if("x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
                target_compile_options(${target} PRIVATE "SHELL:-Xarch_x86_64 -mavx")
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
            target_compile_options(${target} PRIVATE -mavx)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(${target} PRIVATE -mavx)
    endif()
endfunction()

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
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE TDS3
    PLUGIN_CODE XyBs
    FORMATS ${XYBASS_FORMATS}
    PRODUCT_NAME "XY Bass"
    VERSION "${XYBASS_VERSION}"
    VST3_CATEGORIES Fx Dynamics Distortion
)

set(XYBASS_UI_SOURCES
    src/PluginEditor.cpp
    src/XYPad.cpp
    src/ui/SeriesLookAndFeel.cpp
    src/ui/Surface.cpp
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
        ${XYBASS_UI_SOURCES}
        src/PluginProcessor.cpp
        src/Presets.cpp
)

target_compile_definitions(XYBass
    PUBLIC
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
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

xybass_compile_options(XYBass)

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
)

target_link_libraries(XYBassTests
    PRIVATE
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

xybass_compile_options(XYBassTests)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(XYBassTests PRIVATE
        -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
endif()

add_test(NAME xy-bass-engine COMMAND XYBassTests)

juce_add_console_app(XYBassAnalysisTests
    PRODUCT_NAME "XY Bass Analysis Tests"
)

target_sources(XYBassAnalysisTests
    PRIVATE
        ${XYBASS_DSP_SOURCES}
        tests/AnalysisTests.cpp
)

target_include_directories(XYBassAnalysisTests PRIVATE src)

target_compile_definitions(XYBassAnalysisTests
    PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
)

target_link_libraries(XYBassAnalysisTests
    PRIVATE
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

xybass_compile_options(XYBassAnalysisTests)

add_test(NAME xy-bass-analysis COMMAND XYBassAnalysisTests)

juce_add_console_app(XYBassMaterialTests
    PRODUCT_NAME "XY Bass Material Tests"
)

target_sources(XYBassMaterialTests
    PRIVATE
        ${XYBASS_DSP_SOURCES}
        tests/MaterialTests.cpp
)

target_include_directories(XYBassMaterialTests PRIVATE src)

target_compile_definitions(XYBassMaterialTests
    PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
)

target_link_libraries(XYBassMaterialTests
    PRIVATE
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

xybass_compile_options(XYBassMaterialTests)

add_test(NAME xy-bass-material COMMAND XYBassMaterialTests)

juce_add_console_app(XYBassPerformanceTests
    PRODUCT_NAME "XY Bass Performance Tests"
)

target_sources(XYBassPerformanceTests
    PRIVATE
        ${XYBASS_DSP_SOURCES}
        tests/PerformanceTests.cpp
)

target_include_directories(XYBassPerformanceTests PRIVATE src)

target_compile_definitions(XYBassPerformanceTests
    PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
)

target_link_libraries(XYBassPerformanceTests
    PRIVATE
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

xybass_compile_options(XYBassPerformanceTests)

add_test(NAME xy-bass-performance COMMAND XYBassPerformanceTests)
set_tests_properties(xy-bass-performance PROPERTIES LABELS performance)


juce_add_console_app(XYBassPluginTests
    PRODUCT_NAME "XY Bass Plugin Tests"
)

target_sources(XYBassPluginTests
    PRIVATE
        ${XYBASS_DSP_SOURCES}
        ${XYBASS_UI_SOURCES}
        src/PluginProcessor.cpp
        src/Presets.cpp
        tests/PluginTests.cpp
)

target_include_directories(XYBassPluginTests PRIVATE src)

target_compile_definitions(XYBassPluginTests
    PRIVATE
        $<TARGET_PROPERTY:XYBass,INTERFACE_COMPILE_DEFINITIONS>
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
)

target_link_libraries(XYBassPluginTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

xybass_compile_options(XYBassPluginTests)

add_test(NAME xy-bass-plugin COMMAND XYBassPluginTests)
