# Solista: a lead-guitar solo generator. Lives next to Gizmo and shares its look and its build setup.

set(SOLISTA_CORE
    src/Solo.cpp
    src/SoloGenerator.cpp
    src/GuitarSynth.cpp)

set(SOLISTA_SOURCES
    ${SOLISTA_CORE}
    src/SolistaProcessor.cpp
    src/SolistaEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/GizmoLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/Controls.cpp)

juce_add_plugin(Solista
    COMPANY_NAME "Korkapetl6"
    BUNDLE_ID "com.korkapetl6.solista"
    PLUGIN_MANUFACTURER_CODE Kork
    PLUGIN_CODE Sol1
    FORMATS ${GIZMO_FORMATS}
    PRODUCT_NAME "Solista"
    DESCRIPTION "Lead guitar solo generator with bends, vibrato and MIDI drag-out"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(Solista PRIVATE ${SOLISTA_SOURCES})
target_include_directories(Solista PRIVATE src ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(Solista PUBLIC ${GIZMO_DEFINITIONS})
target_link_libraries(Solista
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(Solista PRIVATE /utf-8)
endif()

if(GIZMO_BUILD_TESTS)
    juce_add_console_app(SolistaDump PRODUCT_NAME "SolistaDump")
    target_sources(SolistaDump PRIVATE src/Solo.cpp src/SoloGenerator.cpp tests/Dump.cpp)
    target_include_directories(SolistaDump PRIVATE src)
    target_link_libraries(SolistaDump
        PRIVATE
            juce::juce_audio_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    juce_add_console_app(SolistaTone PRODUCT_NAME "SolistaTone")
    target_sources(SolistaTone PRIVATE src/GuitarSynth.cpp tests/Tone.cpp)
    target_include_directories(SolistaTone PRIVATE src ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(SolistaTone
        PRIVATE
            juce::juce_audio_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    juce_add_console_app(SolistaTests PRODUCT_NAME "SolistaTests")
    target_sources(SolistaTests PRIVATE ${SOLISTA_SOURCES} tests/SolistaTests.cpp)
    target_include_directories(SolistaTests PRIVATE src ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(SolistaTests PRIVATE ${GIZMO_DEFINITIONS})
    target_link_libraries(SolistaTests
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
