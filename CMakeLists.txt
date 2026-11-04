cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET 10.13 CACHE STRING "Minimum macOS version")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "Runtime Library")

option(CONCERT7_BUILD_STANDALONE "Build the standalone app" ON)
option(CONCERT7_BUILD_VST3 "Build the VST3 plugin" ON)
if(UNIX AND NOT APPLE)
    set(CONCERT7_LV2_DEFAULT ON)
else()
    set(CONCERT7_LV2_DEFAULT OFF)
endif()
option(CONCERT7_BUILD_LV2 "Build the LV2 plugin" ${CONCERT7_LV2_DEFAULT})
option(CONCERT7_BUILD_DEVTOOLS "Build the test/render/snapshot tool" ON)
option(CONCERT7_COPY_AFTER_BUILD "Install plugins into the user plugin folders after building" OFF)

project(CONCERT7 VERSION 0.1.0 LANGUAGES C CXX)
set(CONCERT7_RELEASE_STAGE "beta")   # empty for a final release
if(CONCERT7_RELEASE_STAGE)
    set(CONCERT7_VERSION_STRING "${PROJECT_VERSION}-${CONCERT7_RELEASE_STAGE}")
else()
    set(CONCERT7_VERSION_STRING "${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(libs/JUCE)

set(plugin_formats "")
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
if(CONCERT7_BUILD_VST3)
    list(APPEND plugin_formats VST3)
endif()
if(CONCERT7_BUILD_STANDALONE)
    list(APPEND plugin_formats Standalone)
endif()
if(CONCERT7_BUILD_LV2)
    list(APPEND plugin_formats LV2)
endif()

juce_add_plugin(Concert7
    COMPANY_NAME "Concert7"
    COMPANY_WEBSITE "https://github.com/pschopen/concert7"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    JUCE_VST3_CAN_REPLACE_VST2 FALSE
    COPY_PLUGIN_AFTER_BUILD ${CONCERT7_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Cnc7
    PLUGIN_CODE Cc7r
    FORMATS ${plugin_formats}
    PRODUCT_NAME "Concert7"
    BUNDLE_ID "com.github.pschopen.concert7"
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    LV2URI "https://github.com/pschopen/concert7"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Concert7 processes audio input in the standalone app.")

# ---------------------------------------------------------------------------
# Sources shared between the plugin and the developer tool
# ---------------------------------------------------------------------------
file(GLOB CONCERT7_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h")
file(GLOB FFTCONVOLVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/FFTConvolver/*.cpp")

set(CONCERT7_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    CONCERT7_VERSION="${CONCERT7_VERSION_STRING}")
if(APPLE)
    list(APPEND CONCERT7_DEFINITIONS AUDIOFFT_APPLE_ACCELERATE)
endif()

set(CONCERT7_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra)

function(concert7_configure_target target)
    target_sources(${target} PRIVATE ${CONCERT7_SOURCES} ${FFTCONVOLVER_SOURCES})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_include_directories(${target} SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/FFTConvolver")
    target_compile_definitions(${target} PUBLIC ${CONCERT7_DEFINITIONS})
    target_link_libraries(${target}
        PRIVATE
            ${CONCERT7_JUCE_MODULES}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
    if(APPLE)
        target_link_libraries(${target} PRIVATE "-framework Accelerate")
    endif()
    if(NOT MSVC)
        # exact float comparisons are intentional (parameter change detection); source level options
        # come after JUCE's recommended warning flags on the command line
        set_source_files_properties(${CONCERT7_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-Wno-float-equal;-Wno-implicit-int-float-conversion")
    endif()
    # FFTConvolver is third-party code; keep its warnings out of our build log
    set_source_files_properties(${FFTCONVOLVER_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endfunction()

concert7_configure_target(Concert7)
juce_generate_juce_header(Concert7)

# ---------------------------------------------------------------------------
# Developer tool: unit tests, offline renderer, GUI snapshots, benchmarks
# ---------------------------------------------------------------------------
if(CONCERT7_BUILD_DEVTOOLS)
    juce_add_console_app(Concert7Dev PRODUCT_NAME "concert7-dev")
    concert7_configure_target(Concert7Dev)
    target_sources(Concert7Dev PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/DevMain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestHelpers.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/DspTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/LibraryTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ProcessorTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/AUOffline.cpp")
    target_compile_definitions(Concert7Dev PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        CONCERT7_DEVTOOL=1
        JucePlugin_Build_Standalone=0)
    target_include_directories(Concert7Dev PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    juce_generate_juce_header(Concert7Dev)

    enable_testing()
    add_test(NAME unit COMMAND Concert7Dev test)
endif()
