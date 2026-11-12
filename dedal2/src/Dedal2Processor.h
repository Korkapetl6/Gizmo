#pragma once

#include "Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <deque>

namespace dedal2
{
struct Snapshot
{
    SeqState state;
    Layout layout;
    std::array<int, 2> head {}, writeHead {};
};

enum class Command { bitFlip, bitShift, unshift, length, advance, reset, writeAdvance, writeReset, randomise, clear };

struct Captured
{
    double beat = 0.0, length = 0.0;
    int note = 60, velocity = 100, seq = 0, session = 0;
};
} // namespace dedal2

class Dedal2Processor : public juce::AudioProcessor, public juce::ChangeBroadcaster, private juce::Timer
{
public:
    Dedal2Processor();
    ~Dedal2Processor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Dedal 2"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    dedal2::Snapshot getSnapshot() const;
    void command (dedal2::Command, int sequencer);
    void toggleSlot (int slot);

    // Patch bay
    void connect (int output, int input);
    void disconnect (int input);
    int cableAt (int input) const noexcept { return engine.getCable (input); }
    void clearCables();
    float jackLevel (int output) const noexcept { return jackMeters[(size_t) output].load(); }

    void saveBuffer();
    void recallBuffer();
    bool hasBuffer() const noexcept { return buffer.isValid(); }

    void setRunning (bool shouldRun) { run = shouldRun; sendChangeMessage(); }
    bool isRunningInternally() const noexcept { return run.load(); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }
    double getTempo() const noexcept { return hostBpm.load(); }

    static juce::File getPresetFolder();
    juce::StringArray getFactoryPresetNames() const;
    juce::Array<juce::File> getUserPresets() const;
    void loadFactoryPreset (int index);
    bool loadPresetFile (const juce::File&);
    bool savePreset (const juce::String& name);
    juce::String getPresetName() const { return presetName; }

    int dragBars = 4;
    juce::MidiFile makeClip (int bars, bool& fromHistory);
    juce::File writeMidiFile (int bars, bool& fromHistory);
    void collectCapture();

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override { collectCapture(); }
    float value (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    int index (const char* id) const { return juce::roundToInt (value (id)); }
    void setParameter (const char* id, float plain);
    dedal2::Layout readLayout() const;
    dedal2::Params readParams() const;
    juce::ValueTree createState();
    void applyState (const juce::ValueTree&);

    mutable juce::SpinLock seqLock;
    dedal2::Sequencers sequencers;
    dedal2::Engine engine;
    juce::ValueTree buffer;

    std::atomic<bool> run { false }, hostPlaying { false };
    std::atomic<double> hostBpm { 120.0 }, currentBeat { 0.0 }, sessionStart { 0.0 }, barBeats { 4.0 };
    std::atomic<int> session { 0 }, transposeNote { -1 };
    std::array<std::atomic<float>, dedal2::numOuts> jackMeters;

    std::vector<float> mono;
    std::vector<int> clocks;
    std::vector<dedal2::Played> played = std::vector<dedal2::Played> (512);
    double sampleRate = 44100.0, internalBeat = 0.0, expectedBeat = 0.0, lastStepBeats = 0.0;
    bool wasRunning = false, wasHostPlaying = false, lastTrigger = false;
    std::array<bool, 2> lastFlip { false, false };
    juce::int64 nextStep = 0, sampleClock = 0;
    std::array<juce::int64, 128> offAt;
    float lastVcoParam = -1000.0f;

    juce::AbstractFifo fifo { 4096 };
    std::vector<dedal2::Captured> fifoData = std::vector<dedal2::Captured> (4096);
    std::deque<dedal2::Captured> captured;
    int capturedSession = -1;
    juce::String presetName { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Dedal2Processor)
};
