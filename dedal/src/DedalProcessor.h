#pragma once

#include "Sequencer.h"
#include "Synth.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <deque>

namespace dedal
{
struct CapturedNote
{
    double beat = 0.0, length = 0.0;
    int note = 60, velocity = 100, seq = 0, session = 0;
};

/** What the editor needs to draw the sequencers. */
struct Snapshot
{
    SeqState state;
    Layout layout;      // with the lengths SEQ C may have set
    int lengthC = 16;
    std::array<int, 3> head {};
    std::array<int, 2> writeHead {};
    bool running = false;
};

enum class Command
{
    bitFlip, bitShift, unshift, length, advance, reset, reverse, writeAdvance, writeReset, randomise, clear
};

/** Everything that decides how the three sequencers step, read once per block. */
struct StepParams
{
    Layout layout;
    int lengthC = 16;
    Timing timing[3];
    float corrupt[3] = { 0.0f, 0.0f, 0.0f };
    int direction[2] = { 0, 0 };
    float chance[2] = { 1.0f, 1.0f };
    int cReset = 0, cResetOn = 0, cDir = 0, cDirOn = 1, cLength = 0, cChance = 0, cTrans = 0;
    float cLengthAmt = 0.5f, cChanceAmt = 0.5f, cTransAmt = 7.0f;
};

struct StepEvent
{
    int k = 0;
    StepResult r;
    double beat = 0.0, stepBeats = 0.25;
    bool setsTranspose[2] = { false, false };
    float transpose[2] = { 0.0f, 0.0f };
};

inline constexpr int kDragBars[] = { 1, 2, 4, 8, 16 };
} // namespace dedal

//==============================================================================
class DedalProcessor : public juce::AudioProcessor,
                       public juce::ChangeBroadcaster,
                       private juce::Timer
{
public:
    DedalProcessor();
    ~DedalProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dedal"; }
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
    dedal::Snapshot getSnapshot() const;
    void command (dedal::Command, int sequencer);
    void toggleSlot (int slot);
    void setSlotValue (int slot, float value);
    void toggleC (int step);
    void toggleAccentC (int step);
    void setChanceC (int step, float chance);
    void setValueC (int step, float value);
    void nudgeChainOffset (int by);

    void saveBuffer();
    void recallBuffer();
    bool hasBuffer() const noexcept { return buffer.isValid(); }

    /** Runs the sequencers on their own clock (at the host tempo) while the host is stopped. */
    void setRunning (bool shouldRun) { run = shouldRun; sendChangeMessage(); }
    bool isRunningInternally() const noexcept { return run.load(); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }
    double getTempo() const noexcept { return hostBpm.load(); }

    // Presets
    static juce::File getPresetFolder();
    juce::StringArray getFactoryPresetNames() const;
    juce::Array<juce::File> getUserPresets() const;
    void loadFactoryPreset (int index);
    bool loadPresetFile (const juce::File&);
    bool savePreset (const juce::String& name);
    juce::String getPresetName() const { return presetName; }

    // MIDI for dragging out
    int getDragBars() const noexcept { return dragBars; }
    void setDragBars (int bars) { dragBars = bars; }
    /** Writes the last 'bars' bars that played (or, if nothing played yet, the next ones from the
        sequencers as they are now). 'fromHistory' says which it was. */
    juce::File writeMidiFile (int bars, bool& fromHistory);
    juce::MidiFile makeClip (int bars, bool& fromHistory);
    juce::String describeClipSource (int bars) const;

    /** Pulls the captured notes off the audio thread (also done by a timer). */
    void collectCapture();

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override { collectCapture(); }

    float value (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    int index (const char* id) const { return juce::roundToInt (value (id)); }
    dedal::Layout readLayout() const;
    dedal::StepParams readStepParams() const;
    void setParameter (const char* id, float plainValue);

    juce::ValueTree createState();
    void applyState (const juce::ValueTree&);

    struct Sounding
    {
        juce::int64 offAt = -1;
    };

    dedal::SynthParams readSynthParams() const;
    dedal::SynthTargets computeTargets (int root) const;
    int rootNote() const;
    float semitonesOf (int k, float cvValue) const;
    int midiNoteOf (int k, float cvValue, int root, float transposition) const;

    void pushCapture (const dedal::CapturedNote&);

    // Sequencer memory: changed by the audio thread (CORRUPT) and the editor, always under this lock.
    mutable juce::SpinLock seqLock;
    dedal::Sequencers sequencers;
    juce::ValueTree buffer;

    std::atomic<bool> run { false }, hostPlaying { false }, runningNow { false };
    std::atomic<double> hostBpm { 120.0 }, currentBeat { 0.0 }, sessionStart { 0.0 }, barBeats { 4.0 };
    std::atomic<int> session { 0 }, transposeNote { -1 };
    std::array<std::atomic<float>, 2> cv;

    // Audio thread only
    dedal::Synth synth;
    std::vector<float> mono;
    double sampleRate = 44100.0, internalBeat = 0.0, expectedBeat = 0.0;
    bool wasRunning = false, wasHostPlaying = false;
    std::array<juce::int64, 3> nextStep {};
    std::array<double, 3> lastStepBeats { 0.0, 0.0, 0.0 };
    std::array<float, 3> lastSwing { -1.0f, -1.0f, -1.0f };
    std::array<float, 2> transpose { 0.0f, 0.0f };
    std::vector<dedal::StepEvent> events = std::vector<dedal::StepEvent> (1024);
    std::array<bool, 2> lastFlip { false, false };
    bool lastTrigger = false;
    juce::int64 sampleClock = 0;
    std::array<juce::int64, 128> offAt;
    int lastKeyParams = -1;

    // Capture: audio thread to message thread
    juce::AbstractFifo fifo { 4096 };
    std::vector<dedal::CapturedNote> fifoData = std::vector<dedal::CapturedNote> (4096);
    std::deque<dedal::CapturedNote> captured;
    int capturedSession = -1;

    juce::String presetName { "Start" };
    int dragBars = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DedalProcessor)
};
