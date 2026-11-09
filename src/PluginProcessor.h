#pragma once

#include "Params.h"
#include "dsp/SynthEngine.h"
#include "dsp/Wavetable.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <mutex>

class GizmoProcessor : public juce::AudioProcessor,
                       public juce::ChangeBroadcaster
{
public:
    enum Slot { slotA = 0, slotB = 1 };

    GizmoProcessor();
    ~GizmoProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Gizmo"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    /** Loads an audio file into a slot on a background thread. Call from the message thread. */
    void loadFile (Slot slot, const juce::File& file);

    /** Makes oscillator B play table A again. */
    void clearSlotB();

    /** Replaces a slot's table immediately. Call from the message thread. */
    void replaceTable (Slot slot, std::unique_ptr<gizmo::Wavetable> table);

    void setImportMode (gizmo::ImportMode mode);
    gizmo::ImportMode getImportMode() const noexcept { return importMode; }

    /** The table shown for a slot (B may be null, meaning it plays A). Message thread only. */
    const gizmo::Wavetable* getTable (Slot slot) const noexcept { return owned[(size_t) slot].get(); }
    bool isLoading (Slot slot) const noexcept { return loading[(size_t) slot].load() > 0; }
    juce::String getError (Slot slot) const { return errors[(size_t) slot]; }
    bool isSupportedFile (const juce::File& file) const;
    juce::String getSupportedWildcard() const { return formats.getWildcardForAllFormats(); }

    const gizmo::SynthEngine& getEngine() const noexcept { return engine; }

    /** The A/B sequence: bit i set means step i plays the sample (B). */
    void setPattern (juce::uint32 bits);
    juce::uint32 getPattern() const noexcept { return pattern.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    gizmo::EngineParams readParams() const;
    void retire (std::unique_ptr<gizmo::Wavetable> table);

    juce::AudioFormatManager formats;
    juce::ThreadPool loader { juce::ThreadPoolOptions {}.withThreadName ("Gizmo loader").withNumberOfThreads (1) };
    gizmo::SynthEngine engine;
    gizmo::ImportMode importMode = gizmo::ImportMode::cycles;

    std::array<std::unique_ptr<gizmo::Wavetable>, 2> owned;
    std::array<std::atomic<const gizmo::Wavetable*>, 2> live {};
    std::vector<std::pair<juce::uint32, std::unique_ptr<gizmo::Wavetable>>> retired;
    std::array<std::atomic<int>, 2> loading {};
    std::array<int, 2> loadTokens { 0, 0 };
    std::array<juce::String, 2> errors;
    std::mutex tableLock;

    struct ParamPointers
    {
        std::atomic<float>* position {}, * frameMode {}, * velToPos {}, * wheelToPos {};
        std::atomic<float>* levelA {}, * panA {}, * levelB {}, * panB {}, * semiB {}, * offsetB {}, * detune {};
        std::atomic<float>* attack {}, * decay {}, * sustain {}, * release {}, * velToAmp {};
        std::atomic<float>* voiceMode {}, * glide {}, * bendRange {}, * character {}, * gain {};
        std::atomic<float>* filterOn {}, * cutoff {}, * resonance {}, * keyTrack {}, * filterEnv {};
        std::atomic<float>* fAttack {}, * fDecay {}, * fSustain {}, * fRelease {};
        std::atomic<float>* lfoShape {}, * lfoRate {}, * lfoSync {}, * lfoDivision {}, * lfoRetrig {};
        std::atomic<float>* lfoToPos {}, * lfoToCutoff {}, * lfoToPitch {};
        std::atomic<float>* seqOn {}, * seqLength {}, * seqRate {}, * seqStart {};
        std::atomic<float>* fragOffset {}, * fragDirection {}, * fragPitch {}, * fragTranspose {}, * fragLevel {};
    };

    std::atomic<juce::uint32> pattern { gizmo::kDefaultPattern };

    ParamPointers params;

    JUCE_DECLARE_WEAK_REFERENCEABLE (GizmoProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GizmoProcessor)
};
