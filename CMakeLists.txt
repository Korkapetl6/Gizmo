cmake_minimum_required(VERSION 3.22)

project(Gizmo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime on Windows, so the plugin doesn't need the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(GIZMO_BUILD_TESTS "Build the offline test tool" OFF)
option(GIZMO_STANDALONE "Also build a standalone app" OFF)
set(GIZMO_JUCE_DIR "" CACHE PATH "Local JUCE checkout (downloaded automatically when empty)")

if(GIZMO_JUCE_DIR)
    add_subdirectory(${GIZMO_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(GIZMO_FORMATS VST3)

if(APPLE)
    list(APPEND GIZMO_FORMATS AU)
endif()

if(GIZMO_STANDALONE)
    list(APPEND GIZMO_FORMATS Standalone)
endif()

juce_add_plugin(Gizmo
    COMPANY_NAME "Korkapetl6"
    BUNDLE_ID "com.korkapetl6.gizmo"
    PLUGIN_MANUFACTURER_CODE Kork
    PLUGIN_CODE Gzm1
    FORMATS ${GIZMO_FORMATS}
    PRODUCT_NAME "Gizmo"
    DESCRIPTION "Transwave synth: any sample becomes a 64-frame wavetable"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

set(GIZMO_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/Wavetable.cpp
    src/dsp/SynthEngine.cpp
    src/ui/GizmoLookAndFeel.cpp
    src/ui/Controls.cpp
    src/ui/WavetableView.cpp)

set(GIZMO_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_XINPUT=0)

target_sources(Gizmo PRIVATE ${GIZMO_SOURCES})
target_include_directories(Gizmo PRIVATE src)
target_compile_definitions(Gizmo PUBLIC ${GIZMO_DEFINITIONS})
target_link_libraries(Gizmo
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(Gizmo PRIVATE /utf-8)
endif()

if(GIZMO_BUILD_TESTS)
    juce_add_console_app(GizmoTests PRODUCT_NAME "GizmoTests")
    target_sources(GizmoTests PRIVATE ${GIZMO_SOURCES} tests/GizmoTests.cpp)
    target_include_directories(GizmoTests PRIVATE src)
    target_compile_definitions(GizmoTests PRIVATE ${GIZMO_DEFINITIONS})
    target_link_libraries(GizmoTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

add_subdirectory(solista)
