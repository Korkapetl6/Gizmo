# Dedal: a generative synth after a two-sequencer, self-mutating semi-modular design, with a third
# rhythm sequencer, Beads wavetables, MIDI out and MIDI drag-out. Shares Gizmo's look and build setup.

set(DEDAL_SOURCES
    src/Synth.cpp
    src/DedalProcessor.cpp
    src/DedalEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/GizmoLookAndFeel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/Controls.cpp)

juce_add_plugin(Dedal
    COMPANY_NAME "Korkapetl6"
    BUNDLE_ID "com.korkapetl6.dedal"
    PLUGIN_MANUFACTURER_CODE Kork
    PLUGIN_CODE Ddl1
    FORMATS ${GIZMO_FORMATS}
    PRODUCT_NAME "Dedal"
    DESCRIPTION "Generative synth: three mutating sequencers, wavefolder, filter, MIDI out"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(Dedal PRIVATE ${DEDAL_SOURCES})
target_include_directories(Dedal PRIVATE src ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(Dedal PUBLIC ${GIZMO_DEFINITIONS})
target_link_libraries(Dedal
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(Dedal PRIVATE /utf-8 /bigobj)
endif()

if(GIZMO_BUILD_TESTS)
    juce_add_console_app(DedalTests PRODUCT_NAME "DedalTests")
    target_sources(DedalTests PRIVATE ${DEDAL_SOURCES} tests/DedalTests.cpp)
    target_include_directories(DedalTests PRIVATE src ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(DedalTests PRIVATE ${GIZMO_DEFINITIONS})
    target_link_libraries(DedalTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
