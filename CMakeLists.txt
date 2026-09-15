cmake_minimum_required(VERSION 3.24)

set(XYBASS_VERSION "0.1.0")

project(XYBass VERSION ${XYBASS_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# A host may carry an older Visual C++ runtime than the one this was built against, or none,
# and a plug-in sharing an older msvcp140.dll can crash on its first mutex.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The host process brings its own libstdc++, and the LTS distributions ship older ones.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -static-libstdc++ -static-libgcc")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS " -static-libstdc++ -static-libgcc")
endif()

if(DEFINED ENV{CI})
    set(XYBASS_INSTALL_DEFAULT OFF)
else()
    set(XYBASS_INSTALL_DEFAULT ON)
endif()

option(XYBASS_INSTALL_AFTER_BUILD "Copy the built plug-in into the user plug-in folders"
       ${XYBASS_INSTALL_DEFAULT})

if(MSVC)
    add_compile_options(/permissive- /W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

include(FetchContent)

set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "Enable JUCE module source groups" FORCE)

FetchContent_Declare(JUCE
    URL https://codeload.github.com/juce-framework/JUCE/tar.gz/7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2
    URL_HASH SHA256=488c4f6e2c121316d60801842dba87c44e5dbad4e661cb49bf806b49a38b6a5e
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(JUCE)

# AVX is enabled only where it measured faster: Clang gains from it, MSVC is unchanged
# and GCC is slower. Every Mac that runs the deployment target has AVX.
function(xybass_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    elseif(APPLE)
        get_target_property(architectures ${target} OSX_ARCHITECTURES)

        if(NOT architectures)
            set(architectures ${CMAKE_SYSTEM_PROCESSOR})
        endif()

        list(LENGTH architectures architectureCount)

        if(architectureCount GREATER 1 AND "x86_64" IN_LIST architectures)
            target_compile_options(${target} PRIVATE "SHELL:-Xarch_x86_64 -mavx")
        elseif(architectures STREQUAL "x86_64")
            target_compile_options(${target} PRIVATE -mavx)
        endif()
    endif()
endfunction()

set(XYBASS_FORMATS VST3)

if(APPLE)
    list(APPEND XYBASS_FORMATS AU)
endif()

# The Windows default target is under Program Files and needs an elevated shell.
set(XYBASS_COPY_ARGUMENTS)

if(XYBASS_INSTALL_AFTER_BUILD AND WIN32)
    list(APPEND XYBASS_COPY_ARGUMENTS VST3_COPY_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
endif()

juce_add_plugin(XYBass
    PRODUCT_NAME "XY Bass"
    VERSION "${XYBASS_VERSION}"
    COMPANY_NAME "23DSP"
    COMPANY_WEBSITE "https://23dsp.gumroad.com"
    COMPANY_COPYRIGHT "2026, 23DSP"
    DESCRIPTION "Make bass deeper, heavier or audible on small speakers with one gesture"
    BUNDLE_ID com.23dsp.xybass
    PLUGIN_MANUFACTURER_CODE Ttds
    PLUGIN_CODE XyBs
    FORMATS ${XYBASS_FORMATS}
    VST3_CATEGORIES Fx Dynamics Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${XYBASS_INSTALL_AFTER_BUILD}
    ${XYBASS_COPY_ARGUMENTS})

# GCC runs LTO code generation serially, and warns about it, unless it may choose the job count.
add_library(xybass_lto_flags INTERFACE)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(xybass_lto_flags INTERFACE $<$<CONFIG:Release>:-flto=auto>)
    target_link_options(xybass_lto_flags INTERFACE $<$<CONFIG:Release>:-flto=auto>)
else()
    target_link_libraries(xybass_lto_flags INTERFACE juce::juce_recommended_lto_flags)
endif()

add_library(XYBassDsp STATIC
    src/BandSplitter.cpp
    src/BassEngine.cpp
    src/PitchTracker.cpp
    src/SourceAnalyser.cpp
    src/SpectralBalance.cpp
    src/SubEngine.cpp
    src/TranslateEngine.cpp
    src/XYMapping.cpp)

target_include_directories(XYBassDsp PUBLIC src)

target_compile_definitions(XYBassDsp
    PUBLIC
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0)

# LTO is public so every consumer compiles and links the library the same way.
target_link_libraries(XYBassDsp
    PUBLIC
        juce::juce_audio_basics
        juce::juce_dsp
        xybass_lto_flags
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

set_target_properties(XYBassDsp PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
xybass_compile_options(XYBassDsp)

set(XYBASS_PLUGIN_SOURCES
    src/PluginEditor.cpp
    src/PluginProcessor.cpp
    src/Presets.cpp
    src/XYPad.cpp
    src/ui/SeriesLookAndFeel.cpp
    src/ui/Surface.cpp)

target_sources(XYBass PRIVATE ${XYBASS_PLUGIN_SOURCES})

target_link_libraries(XYBass
    PRIVATE
        XYBassDsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        xybass_lto_flags
        juce::juce_recommended_warning_flags)

xybass_compile_options(XYBass)

enable_testing()

function(xybass_add_test_app target)
    juce_add_console_app(${target})
    target_sources(${target} PRIVATE ${ARGN})
    target_link_libraries(${target}
        PRIVATE
            XYBassDsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Tests only ever run on the build machine, so one architecture slice is enough.
    if(APPLE)
        set_target_properties(${target} PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()

    xybass_compile_options(${target})
endfunction()

xybass_add_test_app(XYBassTests
    tests/TestMain.cpp
    tests/EngineTests.cpp
    tests/AnalysisTests.cpp
    tests/MaterialTests.cpp
    tests/PluginTests.cpp
    ${XYBASS_PLUGIN_SOURCES})

# The plug-in's definitions would redefine the console app's JUCE_STANDALONE_APPLICATION.
target_compile_definitions(XYBassTests
    PRIVATE
        $<FILTER:$<TARGET_PROPERTY:XYBass,INTERFACE_COMPILE_DEFINITIONS>,EXCLUDE,^JUCE_STANDALONE_APPLICATION=>)

target_link_libraries(XYBassTests PRIVATE juce::juce_audio_utils)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(XYBassTests PRIVATE
        -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
endif()

foreach(suite IN ITEMS engine analysis material plugin)
    add_test(NAME xy-bass-${suite} COMMAND XYBassTests ${suite})
endforeach()

xybass_add_test_app(XYBassPerformanceTests tests/PerformanceTests.cpp)
add_test(NAME xy-bass-performance COMMAND XYBassPerformanceTests)
set_tests_properties(xy-bass-performance PROPERTIES LABELS performance)
