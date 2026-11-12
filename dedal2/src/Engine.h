#pragma once

#include "Sequencer.h"
#include "dsp/HalfbandDecimator.h"

#include <atomic>

namespace dedal2
{
using namespace dedal;

// The patch bay: 19 inputs and 13 outputs. The last output is our own addition: a sample player.
enum In
{
    inVcoVoct = 0, inMvcoSync, inMvcoVoct, inBlend, inVcwIn, inFold, inVcwVca, inVcfIn, inCutoff, inVcfVca,
    inUmix1, inUmix2, inEg2Trig, inClock1, inBitFlip1, inClock2, inBitFlip2, inTrigger, inReset, numIns
};

enum Out
{
    outVca = 0, outMvco, outNoise, outMixer, outEg1, outEg2, outUmix, outSeq1Cv, outSeq1Trig, outSeq2Cv, outSeq2Trig,
    outClock, outSample, numOuts
};

inline const char* inName (int i)
{
    static const char* names[] = { "VCO 1V/OCT", "M VCO SYNC", "M VCO 1V/OCT", "BLEND", "VCW IN", "FOLD", "VCW VCA CV", "VCF IN",
                                   "CUTOFF", "VCF VCA CV", "U MIX 1 (RING)", "U MIX 2", "EG2 TRIG", "CLOCK 1", "BIT FLIP 1",
                                   "CLOCK 2", "BIT FLIP 2", "TRIGGER", "RESET" };
    return names[i];
}

inline const char* outName (int i)
{
    static const char* names[] = { "VCA", "M VCO", "NOISE", "MIXER", "EG1", "EG2", "U MIX 1+2", "SEQ1 CV", "SEQ1 TRIG", "SEQ2 CV",
                                   "SEQ2 TRIG", "CLOCK", "SAMPLE" };
    return names[i];
}

struct Params
{
    float vcoNote = 48.0f, vcoEg1 = 0.0f, vcoSeq1 = 1.0f;
    float modNote = 48.0f, modEg1 = 0.0f, modSeq2 = 1.0f, fm = 0.0f;
    float lvlVco = 0.5f, lvlUmix1 = 0.0f, umixLvl = 0.0f, lvlMod = 0.0f, lvlNoise = 0.0f, noiseTone = 0.5f;
    float fold = 0.0f, bias = 0.0f, foldEg = 0.0f, foldSeq = 0.0f;
    float cutoff = 0.6f, cutEg = 0.0f, cutSeq = 0.0f, resonance = 0.2f, mode = 0.0f;
    float blend = 0.5f;
    int order = 0; // 0 parallel, 1 VCW into VCF, 2 VCF into VCW
    float eg1Decay = 0.3f, eg2Decay = 0.5f, trigMix = 0.5f, gain = 0.5f;
    int scale = 2;
    float range[2] = { 0.2f, 0.2f }, corrupt[2] = { 0.0f, 0.0f };
    Layout layout;

    // Sample player behind the SAMPLE jack
    float smpTune = 0.0f, smpStart = 0.0f; // semitones, 0..1 of the length
    bool smpRetrig = false;                // restart on every EG1 trigger instead of looping
    int smpFollow = 0;                     // 0 fixed pitch, 1 follows SEQ1, 2 follows SEQ2
};

/** A mono sample; owned by the processor, read by the audio thread. */
struct Sample
{
    std::vector<float> data;
    double rate = 44100.0;
};

/** A note the sequencers played, for MIDI out. */
struct Played
{
    int sample = 0, k = 0, note = 60, velocity = 100;
};

/** The whole voice, patchable: every jack is read and written per sample, so cables carry audio and
    control alike, with one sample of delay. Unpatched inputs fall back to their normalled source. */
class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** Which output feeds each input (-1 = nothing patched). Safe to call from the editor. */
    void setCable (int input, int output) noexcept { source[(size_t) input].store (output); }
    int getCable (int input) const noexcept { return source[(size_t) input].load(); }

    /** Renders a block. 'clocks' are the samples at which the internal clock ticks. */
    void render (float* out, int numSamples, const Params&, Sequencers&, const int* clocks, int numClocks,
                 bool manualTrigger, Played* played, int& numPlayed, int maxPlayed);

    float jack (int output) const noexcept { return outs[(size_t) output]; }

    /** The caller keeps the sample alive for as long as it is set here. */
    void setSample (const Sample* s) noexcept { sample.store (s); }
    float samplePosition() const noexcept { return smpShown.load(); }

private:
    void stepSequencer (int k, const Params&, Sequencers&, int sample, Played* played, int& numPlayed, int maxPlayed);
    float seqSemis (int k, const Params&, bool quantized) const noexcept;

    double rate = 96000.0;
    std::array<std::atomic<int>, numIns> source;
    std::array<float, numOuts> outs {};
    std::array<bool, numIns> high {};
    double phase1 = 0.0, phase2 = 0.0;
    float eg[2] = { 0.0f, 0.0f }, egTarget[2] = { 0.0f, 0.0f };
    bool egAttack[2] = { false, false };
    float cv[2] = { 0.0f, 0.0f };
    int trigLeft[2] = { 0, 0 }, clockLeft = 0;
    float noiseLp = 0.0f, ic1 = 0.0f, ic2 = 0.0f, dc = 0.0f, dcIn = 0.0f, level = 0.0f;
    float foldSmooth = 0.0f, cutSmooth = 0.0f;
    juce::uint32 noiseState = 22222;
    std::atomic<const Sample*> sample { nullptr };
    const Sample* smpCurrent = nullptr;
    double smpPos = 0.0, smpInc = 1.0;
    float smpKey = -1000.0f;
    bool smpPlaying = false, smpEgWas = false;
    std::atomic<float> smpShown { 0.0f };
    gizmo::HalfbandDecimator decimator;
    std::vector<float> work;

public:
    Engine() { for (auto& s : source) s.store (-1); }
};
} // namespace dedal2
