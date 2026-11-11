#pragma once

#include "dsp/HalfbandDecimator.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace dedal
{
/** The twelve Beads banks, band-limited per octave so they can be played anywhere without aliasing. */
class WaveBanks
{
public:
    static constexpr int kTableSize = 1024, kLevels = 8, kBanks = 12, kWaves = 16;

    static const WaveBanks& get();

    /** One sample of a bank, 'position' 0..1 across its 16 waves, 'phase' 0..1, 'level' 0 = all 128 harmonics. */
    float sample (int bank, float position, double phase, int level) const noexcept;

    static juce::StringArray getBankNames();

private:
    WaveBanks();
    std::vector<float> tables; // [bank][wave][level][kTableSize + 1]
};

struct SynthParams
{
    struct Osc
    {
        int type = 0;         // 0 = analog shapes, 1 = Beads bank
        int bank = 0;
        float wave = 0.0f;    // analog: sine, triangle, saw, square; bank: position
        float pw = 0.5f;
    };

    Osc vco, mod;
    bool modLfo = false, modSync = false;
    float modLfoHz = 1.0f;

    float fm = 0.0f, lvlVco = 0.5f, lvlMod = 0.0f, lvlRing = 0.0f, lvlNoise = 0.0f, noiseTone = 0.5f;
    float fold = 0.0f, bias = 0.0f, eg1Fold = 0.0f;
    float cutoff = 0.6f, resonance = 0.2f, filterType = 0.0f, eg1Cutoff = 0.0f;
    float eg1Vco = 0.0f, eg1Mod = 0.0f;
    int routing = 0;      // 0 parallel, 1 fold then filter, 2 filter then fold
    float blend = 0.5f;
    float eg1Decay = 0.3f, eg2Decay = 0.5f;
    int vcaW = 0, vcaF = 0; // 0 EG2, 1 EG1, 2 open
    float glide = 0.0f;   // seconds
    float gain = 0.5f;
};

/** What the sequencers send, changed at the exact sample of each step. */
struct SynthTargets
{
    float vcoNote = 48.0f, modNote = 48.0f; // MIDI notes, fractional
    float foldMod = 0.0f;                   // added to fold
    float cutoffMod = 0.0f;                 // octaves
    float waveMod1 = 0.0f, waveMod2 = 0.0f; // added to the wave knobs
};

/** One mono voice: VCO and MOD VCO, mixer, wavefolder and filter in parallel or in series, two decay
    envelopes and two VCAs. Runs at twice the sample rate. */
class Synth
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setParams (const SynthParams& p) noexcept { params = p; }
    void setTargets (const SynthTargets& t) noexcept { targets = t; }

    /** Fires the envelopes: EG1 with v1, EG2 with v2 (0 = leave that one alone). */
    void trigger (float v1, float v2) noexcept;

    void render (float* out, int numSamples);

    float getEg1() const noexcept { return eg[0].value; }
    float getEg2() const noexcept { return eg[1].value; }

private:
    struct Envelope
    {
        float value = 0.0f, target = 0.0f;
        bool attacking = false;
    };

    float oscillator (const SynthParams::Osc& osc, double phase, double dt, float waveMod) const noexcept;
    float processSample() noexcept;

    double sampleRate = 44100.0, rate = 88200.0;
    SynthParams params;
    SynthTargets targets;

    double phase1 = 0.0, phase2 = 0.0;
    float note1 = 48.0f, note2 = 48.0f, fold = 0.0f, cutoffOct = 0.0f, wave1 = 0.0f, wave2 = 0.0f;
    float lastMod = 0.0f;
    Envelope eg[2];
    float noiseLp = 0.0f;
    juce::uint32 noiseState = 22222;
    float ic1 = 0.0f, ic2 = 0.0f, dc = 0.0f, dcIn = 0.0f;
    float level = 0.0f;

    gizmo::HalfbandDecimator decimator;
    std::vector<float> work;
};
} // namespace dedal
