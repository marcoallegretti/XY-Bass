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

FetchContent_MakeAvailable(JUCE)

set(XYBASS_DSP_SOURCES
    src/BassEngine.cpp
    src/PitchTracker.cpp
    src/SourceAnalyser.cpp
    src/SpectralBalance.cpp
    src/SubEngine.cpp
    src/TranslateEngine.cpp
    src/XYMapping.cpp
)
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
