#pragma once

#include "HalfbandDecimator.h"
#include "Wavetable.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

namespace gizmo
{
struct EngineParams
{
    float position = 0.0f;    // 0..1 across the 64 frames
    bool  steppedFrames = false;
    float velToPos = 0.0f;    // -1..1
    float wheelToPos = 0.0f;  // -1..1
    float levelA = 0.8f, panA = 0.0f;
    float levelB = 0.6f, panB = 0.0f;
    int   semiB = 0;
    float offsetB = 0.0f;     // -1..1
    float detuneCents = 8.0f; // spread between A and B
    float attack = 0.005f, decay = 0.6f, sustain = 0.8f, release = 0.5f;
    float velToAmp = 0.5f;
    bool  mono = false;
    float glide = 0.0f;       // seconds
    int   bendRange = 2;      // semitones
    bool  raw = false;        // no band-limiting
    float gainDb = -6.0f;

    // Filter: 4-pole low-pass
    bool  filterOn = true;
    float cutoff = 20000.0f;  // Hz
    float resonance = 0.0f;   // 0..1
    float keyTrack = 0.0f;    // 0..1
    float filterEnv = 0.0f;   // -1..1, times 6 octaves
    float fAttack = 0.005f, fDecay = 0.4f, fSustain = 0.0f, fRelease = 0.4f;

    // LFO
    int   lfoShape = 0;       // sine, triangle, saw, square, random
    float lfoRate = 1.0f;     // Hz
    bool  lfoSync = false;
    double lfoBeats = 1.0;    // cycle length in quarter notes when synced
    bool  lfoRetrig = false;  // restart with every note
    float lfoToPos = 0.0f;    // -1..1
    float lfoToCutoff = 0.0f; // -1..1, times 4 octaves
    float lfoToPitch = 0.0f;  // 0..1

    // Host transport
    double bpm = 120.0;
    double ppqAtBlockStart = 0.0;
    bool  transportKnown = false;
};

/** Polyphonic two-oscillator wavetable synth. Renders at twice the sample rate
    and decimates, so band-limited frames never alias. */
class SynthEngine
{
public:
    static constexpr int kMaxVoices = 16;

    SynthEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** tableB may be null, then oscillator B plays tableA. */
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const EngineParams& params,
                  const Wavetable* tableA, const Wavetable* tableB);

    int getLatencySamples() const noexcept { return HalfbandDecimator::kLatency; }

    // Safe to read from the UI thread.
    float getDisplayPosition() const noexcept { return displayPosition.load (std::memory_order_relaxed); }
    float getVoicePosition (int voice) const noexcept { return voicePositions[(size_t) voice].load (std::memory_order_relaxed); }

private:
    enum class Stage { idle, attack, decay, sustain, release };

public:
    /** Phase and frame state of one oscillator. */
    struct OscState
    {
        double phase = 0.0;
        int frame = -1;     // frame held for the current cycle (stepped mode)
        int prevFrame = -1; // frame being faded out after a switch
        int fadeLeft = 0, fadeLength = 0;
    };

private:

    struct Voice
    {
        Stage stage = Stage::idle;
        int note = -1;
        float velocity = 0.0f;
        bool keyDown = false, sustained = false;
        float env = 0.0f;
        float pitch = 60.0f, targetPitch = 60.0f;
        OscState oscA, oscB;
        juce::uint32 age = 0;

        Stage filterStage = Stage::idle;
        float filterEnvLevel = 0.0f;
        float svf1[2][2] {}; // [channel][state], Butterworth stage
        float svf2[2][2] {}; // [channel][state], resonant stage

        double lfoPhase = 0.0;
        float lfoRandom = 0.0f, lastLfo = 0.0f;
    };

    void enterRelease (Voice& v) noexcept;

    static constexpr int kSub = 16; // control-rate block, in internal samples

    static float lfoShapeValue (int shape, double phase, float randomValue) noexcept;
    double lfoFrequency (const EngineParams& p) const noexcept;

    void renderRange (float* outL, float* outR, int from, int to, int blockLength, const EngineParams& p,
                      const Wavetable& a, const Wavetable& b);
    void renderVoice (Voice& v, int numInternal, const EngineParams& p, const Wavetable& a, const Wavetable& b);
    void handleMidi (const juce::MidiMessage& m, const EngineParams& p);
    void noteOn (int note, float velocity, const EngineParams& p);
    void noteOff (int note, const EngineParams& p);
    void startVoice (Voice& v, int note, float velocity, const EngineParams& p);
    void releaseOrSustain (Voice& v);
    void releaseAll();
    int mipLevelFor (double frequency) const noexcept;

    double sampleRate = 44100.0, internalRate = 88200.0;
    int maxBlock = 512;

    std::array<Voice, kMaxVoices> voices;
    std::vector<int> heldNotes; // mono note stack
    bool sustainPedal = false, monoMode = false;
    float pitchBend = 0.0f, bendSmoothed = 0.0f;
    float wheel = 0.0f, wheelSmoothed = 0.0f;
    float positionStart = 0.0f, positionEnd = 0.0f, lastPosition = 0.0f;
    bool firstBlock = true;
    float lastNote = -1.0f;
    juce::uint32 voiceCounter = 0;
    float gainSmoothed = 0.0f;

    double globalLfoPhase = 0.0;
    float globalLfoRandom = 0.0f;

    std::vector<float> posBuffer, bendBuffer, lfoBuffer, workL, workR;
    HalfbandDecimator decimL, decimR;
    juce::Random random;

    std::atomic<float> displayPosition { 0.0f };
    std::array<std::atomic<float>, kMaxVoices> voicePositions;
};
} // namespace gizmo
