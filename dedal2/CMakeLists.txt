# Dedal 2: the same two-sequencer generative voice laid out module by module, with a patch bay and cables.

set(DEDAL2_SOURCES
    src/Engine.cpp
    src/Dedal2Processor.cpp
    src/Dedal2Editor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/GizmoLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/Controls.cpp)

juce_add_plugin(Dedal2
    COMPANY_NAME "Korkapetl6"
    BUNDLE_ID "com.korkapetl6.dedal2"
    PLUGIN_MANUFACTURER_CODE Kork
    PLUGIN_CODE Ddl2
    FORMATS ${GIZMO_FORMATS}
    PRODUCT_NAME "Dedal 2"
    DESCRIPTION "Generative patchable synth: two mutating sequencers, wavefolder, filter, patch bay"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(Dedal2 PRIVATE ${DEDAL2_SOURCES})
target_include_directories(Dedal2 PRIVATE src ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/dedal/src)
target_compile_definitions(Dedal2 PUBLIC ${GIZMO_DEFINITIONS})
target_link_libraries(Dedal2
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(Dedal2 PRIVATE /utf-8)
endif()

if(GIZMO_BUILD_TESTS)
    juce_add_console_app(Dedal2Tests PRODUCT_NAME "Dedal2Tests")
    target_sources(Dedal2Tests PRIVATE ${DEDAL2_SOURCES} tests/Dedal2Tests.cpp)
    target_include_directories(Dedal2Tests PRIVATE src ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/dedal/src)
    target_compile_definitions(Dedal2Tests PRIVATE ${GIZMO_DEFINITIONS})
    target_link_libraries(Dedal2Tests
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
