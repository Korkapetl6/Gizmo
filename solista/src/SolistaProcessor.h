#pragma once

#include "GuitarSynth.h"
#include "SoloGenerator.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace solista
{
namespace ParamID
{
inline constexpr const char* key = "key";
inline constexpr const char* mode = "mode";
inline constexpr const char* progression = "progression";
inline constexpr const char* bars = "bars";
inline constexpr const char* style = "style";
inline constexpr const char* feel = "feel";
inline constexpr const char* density = "density";
inline constexpr const char* bends = "bends";
inline constexpr const char* vibrato = "vibrato";
inline constexpr const char* syncopation = "syncopation";
inline constexpr const char* space = "space";
inline constexpr const char* harmony = "harmony";
inline constexpr const char* bendRange = "bendRange";
inline constexpr const char* drive = "drive";
inline constexpr const char* tone = "tone";
inline constexpr const char* ambience = "ambience";
inline constexpr const char* volume = "volume";
} // namespace ParamID

inline constexpr int kBarChoices[] = { 4, 8, 12, 16 };
inline constexpr int kBendRanges[] = { 2, 12, 24 };

juce::StringArray getKeyNames();
juce::StringArray getStyleNames();

/** One generated solo with everything derived from it. Never changes once it is published. */
struct SoloBundle
{
    GeneratorParams params;
    Solo solo, second;                 // 'second' is the harmony guitar (empty when it is off)
    std::vector<Event> events, secondEvents;
    int harmony = 0;                   // 0 none, 1 a third above, 2 a third below
};
} // namespace solista

//==============================================================================
class SolistaProcessor : public juce::AudioProcessor,
                         public juce::ChangeBroadcaster,
                         private juce::Timer
{
public:
    SolistaProcessor();
    ~SolistaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Solista"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    /** The solo that is playing and showing now. Call on the message thread. */
    std::shared_ptr<const solista::SoloBundle> getBundle() const;

    /** Composes a different solo with the same settings. */
    void newSolo();
    /** Goes back to the solo before the last "new solo". */
    void previousSolo();
    bool hasPreviousSolo() const noexcept { return ! history.empty(); }
    juce::uint32 getSeed() const noexcept { return seed.load(); }
    void setSeed (juce::uint32 newSeed);

    /** Recomposes right away if a setting or the tempo changed. Returns true if it did. */
    bool regenerateIfNeeded();

    /** Plays the solo on the built-in clock while the host is stopped (or with the host while it runs). */
    void setAudition (bool shouldPlay);
    bool isAuditioning() const noexcept { return audition.load(); }

    /** When on, the solo plays along whenever the host plays. Turn it off once the MIDI clip is on the track. */
    void setFollowHost (bool shouldFollow);
    bool isFollowingHost() const noexcept { return followHost.load(); }

    /** Where playback is, in beats from the start of the solo, or a negative number when silent. */
    double getPlayPosition() const noexcept { return uiPosition.load(); }
    double getTempo() const noexcept { return hostBpm.load(); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }

    /** The MIDI files to drag out: 'second' picks the harmony guitar. */
    juce::File writeMidiFile (bool second) const;
    juce::String describeSolo() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    solista::GeneratorParams readParams() const;
    void publish (std::shared_ptr<const solista::SoloBundle>);
    void timerCallback() override;

    struct Scheduled
    {
        int offset = 0, order = 0, target = 0;
        int type = 0; // Event::Type, or 3 = all notes off
        int pitch = 0, velocity = 0;
        bool legato = false, fromMidi = false;
        float semitones = 0.0f;
    };

    void collectSolo (const std::vector<solista::Event>& events, size_t& index, int target, double from, double to,
                      double beatsPerSample, int sampleBase, int limit);
    void apply (const Scheduled&);
    void push (const Scheduled&);
    void renderSegment (float* left, float* right, int numSamples);

    std::atomic<float>* raw (const char* id) const { return apvts.getRawParameterValue (id); }

    // Shared with the audio thread through a raw pointer; the owners below keep it alive.
    std::atomic<const solista::SoloBundle*> active { nullptr };
    juce::CriticalSection generationLock;
    std::shared_ptr<const solista::SoloBundle> current;
    std::vector<std::pair<std::shared_ptr<const solista::SoloBundle>, juce::uint32>> retired;

    std::atomic<juce::uint32> seed { 1 };
    std::vector<juce::uint32> history;
    std::atomic<bool> audition { false }, followHost { true }, restart { false };
    std::atomic<double> hostBpm { 120.0 }, uiPosition { -1.0 };
    std::atomic<bool> hostPlaying { false };

    // Audio thread only
    solista::GuitarSynth lead, secondGuitar;
    std::vector<Scheduled> queue;
    juce::AudioBuffer<float> scratch;
    const solista::SoloBundle* playing = nullptr;
    size_t leadIndex = 0, secondIndex = 0;
    double playPosition = 0.0, sampleRate = 44100.0;
    bool wasPlaying = false, wasSynced = false;
    float spread = 0.0f;
    juce::int64 sampleClock = 0, lastMidiNoteOn = -100000;
    int midiHeld = 0, secondTail = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolistaProcessor)
};
