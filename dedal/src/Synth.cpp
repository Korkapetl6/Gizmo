#include "Synth.h"
#include "BeadsWaves.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace dedal
{
namespace
{
constexpr double pi = 3.14159265358979323846;

inline float midiToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

inline float polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        const auto x = t / dt;
        return (float) (x + x - x * x - 1.0);
    }

    if (t > 1.0 - dt)
    {
        const auto x = (t - 1.0) / dt;
        return (float) (x * x + x + x + 1.0);
    }

    return 0.0f;
}

/** Linear below 0.8, then rounds off towards 1: the mixer's overdrive. */
inline float saturate (float x) noexcept
{
    const auto a = std::abs (x);

    if (a <= 0.8f)
        return x;

    return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) * 5.0f), x);
}

inline float wrapPhase (double& phase) noexcept
{
    if (phase >= 1.0)
    {
        phase -= std::floor (phase);
        return 1.0f;
    }

    if (phase < 0.0)
    {
        phase -= std::floor (phase);
        return -1.0f;
    }

    return 0.0f;
}
} // namespace

//==============================================================================
const WaveBanks& WaveBanks::get()
{
    static const WaveBanks banks;
    return banks;
}

juce::StringArray WaveBanks::getBankNames()
{
    return { "A Harmoniczne", juce::String::fromUTF8 ("B Trójkąty"), "C Organy", juce::String::fromUTF8 ("D Trójkąt / piła"),
             "E Impulsy", "F Formanty", "G Cyfrowe", "H Braids", "I FM 2:1", "J FM 1:2", "K FM 1:3", "L FM 1:8" };
}

WaveBanks::WaveBanks()
{
    constexpr int stride = kTableSize + 1;
    tables.assign ((size_t) (kBanks * kWaves * kLevels * stride), 0.0f);

    juce::dsp::FFT small (8), large (10);
    std::vector<juce::dsp::Complex<float>> in (256), spectrum (256), full (kTableSize), out (kTableSize);

    for (int w = 0; w < kBanks * kWaves; ++w)
    {
        for (int i = 0; i < 256; ++i)
            in[(size_t) i] = { (float) beads::waves[w * 256 + i] / 32767.0f, 0.0f };

        small.perform (in.data(), spectrum.data(), false);
        float scale = 0.0f;

        for (int level = 0; level < kLevels; ++level)
        {
            const auto harmonics = 128 >> level;
            std::fill (full.begin(), full.end(), juce::dsp::Complex<float> {});

            for (int h = 1; h <= harmonics && h < 128; ++h)
            {
                full[(size_t) h] = spectrum[(size_t) h];
                full[(size_t) (kTableSize - h)] = std::conj (spectrum[(size_t) h]);
            }

            if (harmonics >= 128)
                full[128] = spectrum[128]; // Nyquist of the source table (as a cosine)

            large.perform (full.data(), out.data(), true);
            auto* table = tables.data() + (size_t) ((w * kLevels + level) * stride);

            for (int i = 0; i < kTableSize; ++i)
                table[i] = out[(size_t) i].real();

            // One loudness for all the octaves of a wave, set by its full version.
            if (level == 0)
            {
                for (int i = 0; i < kTableSize; ++i)
                    scale = std::max (scale, std::abs (table[i]));

                scale = scale > 1.0e-9f ? 1.0f / scale : 0.0f;
            }

            for (int i = 0; i < kTableSize; ++i)
                table[i] *= scale;

            table[kTableSize] = table[0];
        }
    }
}

float WaveBanks::sample (int bank, float position, double phase, int level) const noexcept
{
    constexpr int stride = kTableSize + 1;
    const auto p = juce::jlimit (0.0f, 1.0f, position) * (float) (kWaves - 1);
    const auto w0 = std::min ((int) p, kWaves - 2);
    const auto mix = p - (float) w0;
    const auto x = phase * kTableSize;
    const auto i = std::min ((int) x, kTableSize - 1);
    const auto t = (float) (x - i);
    const auto* a = tables.data() + (size_t) (((juce::jlimit (0, kBanks - 1, bank) * kWaves + w0) * kLevels + level) * stride);
    const auto* b = a + (size_t) (kLevels * stride);
    const auto va = a[i] + t * (a[i + 1] - a[i]);
    const auto vb = b[i] + t * (b[i + 1] - b[i]);
    return va + mix * (vb - va);
}

//==============================================================================
void Synth::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    rate = newSampleRate * 2.0;
    work.assign ((size_t) std::max (32, maxBlockSize) * 2, 0.0f);
    WaveBanks::get();
    reset();
}

void Synth::reset()
{
    phase1 = phase2 = 0.0;
    note1 = targets.vcoNote;
    note2 = targets.modNote;
    fold = targets.foldMod;
    cutoffOct = targets.cutoffMod;
    wave1 = targets.waveMod1;
    wave2 = targets.waveMod2;
    lastMod = 0.0f;
    eg[0] = eg[1] = {};
    noiseLp = ic1 = ic2 = dc = dcIn = 0.0f;
    level = params.gain;
    decimator.reset();
}

void Synth::trigger (float v1, float v2) noexcept
{
    if (v1 > 0.0f)
    {
        eg[0].target = v1;
        eg[0].attacking = true;
    }

    if (v2 > 0.0f)
    {
        eg[1].target = v2;
        eg[1].attacking = true;
    }
}

float Synth::oscillator (const SynthParams::Osc& osc, double phase, double dt, float waveMod) const noexcept
{
    const auto wave = juce::jlimit (0.0f, 1.0f, osc.wave + waveMod);

    if (osc.type == 1)
    {
        // Beads bank: pick the octave whose harmonics all stay under the band limit.
        const auto harmonics = 0.7 / std::max (1.0e-9, dt);
        auto lvl = 0;

        while (lvl < WaveBanks::kLevels - 1 && (double) (128 >> lvl) > harmonics)
            ++lvl;

        return WaveBanks::get().sample (osc.bank, wave, phase, lvl);
    }

    // Sine, triangle, saw and square, one sliding into the next.
    const auto shape = wave * 3.0f;
    const auto first = std::min (2, (int) shape);
    const auto mix = shape - (float) first;
    const auto pw = (double) juce::jlimit (0.05f, 0.95f, osc.pw);

    auto shapeAt = [&] (int s)
    {
        switch (s)
        {
            case 0: return (float) std::sin (2.0 * pi * phase);
            case 1: return (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
            case 2: return (float) (2.0 * phase - 1.0) - polyBlep (phase, dt);
            default:
            {
                auto shifted = phase - pw + 1.0;
                shifted -= std::floor (shifted);
                return (phase < pw ? 1.0f : -1.0f) + polyBlep (phase, dt) - polyBlep (shifted, dt);
            }
        }
    };

    const auto a = shapeAt (first);
    return mix > 0.0005f ? a + mix * (shapeAt (first + 1) - a) : a;
}

float Synth::processSample() noexcept
{
    const auto& p = params;
    const auto smooth = (float) (1.0 - std::exp (-1.0 / (0.002 * rate)));
    const auto glide = p.glide > 0.0005f ? (float) (1.0 - std::exp (-1.0 / ((double) p.glide * rate))) : 1.0f;

    note1 += (targets.vcoNote - note1) * glide;
    note2 += (targets.modNote - note2) * glide;
    fold += (targets.foldMod - fold) * smooth;
    cutoffOct += (targets.cutoffMod - cutoffOct) * smooth;
    wave1 += (targets.waveMod1 - wave1) * smooth;
    wave2 += (targets.waveMod2 - wave2) * smooth;

    // Envelopes: a 1 ms rise, then the decay.
    for (int i = 0; i < 2; ++i)
    {
        auto& e = eg[i];

        if (e.attacking)
        {
            e.value += (float) (1.0 / (0.001 * rate));

            if (e.value >= e.target)
            {
                e.value = e.target;
                e.attacking = false;
            }
        }
        else
        {
            const auto decay = std::max (0.005f, i == 0 ? p.eg1Decay : p.eg2Decay);
            e.value *= (float) std::exp (-6.9 / ((double) decay * rate));
        }
    }

    const auto eg1 = eg[0].value, eg2 = eg[1].value;

    // MOD VCO first: it modulates the VCO.
    const auto f2 = p.modLfo ? p.modLfoHz * std::exp2 ((note2 + p.eg1Mod * 24.0f * eg1 - 60.0f) / 12.0f)
                             : midiToHz (note2 + p.eg1Mod * 24.0f * eg1);
    const auto dt2 = juce::jlimit (0.0, 0.45, (double) f2 / rate);
    const auto mod = oscillator (p.mod, phase2, dt2, wave2);
    phase2 += dt2;
    wrapPhase (phase2);

    // VCO with thru-zero FM: the phase may run backwards, the pitch centre stays put.
    const auto f1 = midiToHz (note1 + p.eg1Vco * 24.0f * eg1);
    const auto dt1 = (double) f1 / rate * (1.0 + 4.0 * (double) p.fm * (double) mod);
    const auto vco = oscillator (p.vco, phase1, juce::jlimit (1.0e-6, 0.45, std::abs (dt1)), wave1);
    phase1 += juce::jlimit (-0.45, 0.45, dt1);

    if (wrapPhase (phase1) > 0.0f && p.modSync)
        phase2 = 0.0;

    lastMod = mod;

    // Noise, from dark to bright
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    const auto white = (float) (int) noiseState * (1.0f / 2147483648.0f);
    const auto noiseHz = 60.0 * std::exp2 ((double) p.noiseTone * 8.5);
    const auto noiseCoef = (float) (1.0 - std::exp (-2.0 * pi * std::min (noiseHz, 0.45 * rate) / rate));
    noiseLp += noiseCoef * (white - noiseLp);
    const auto noise = noiseLp * std::min (6.0f, std::sqrt (0.25f / noiseCoef));

    // Mixer: unity at noon, overdrive above it (not for the ring modulator).
    auto mixed = saturate (vco * p.lvlVco * 2.0f) + saturate (mod * p.lvlMod * 2.0f)
               + juce::jlimit (-1.0f, 1.0f, vco * mod * p.lvlRing * 2.0f) + saturate (noise * p.lvlNoise * 2.0f);
    mixed *= 0.6f;

    auto folder = [&] (float x)
    {
        const auto amount = juce::jlimit (0.0f, 1.0f, p.fold + fold + p.eg1Fold * eg1);
        return std::sin (juce::MathConstants<float>::halfPi * ((1.0f + 7.0f * amount) * x + p.bias));
    };

    auto filter = [&] (float x)
    {
        const auto octaves = p.cutoff * 10.0f + cutoffOct + p.eg1Cutoff * 6.0f * eg1;
        const auto hz = juce::jlimit (20.0, 0.45 * rate, 20.0 * std::exp2 ((double) octaves));
        const auto g = (float) std::tan (pi * hz / rate);
        const auto k = 2.0f - 1.96f * juce::jlimit (0.0f, 1.0f, p.resonance);
        const auto a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const auto v3 = saturate (x) - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2 + p.filterType * (1.4f * v1 - v2);
    };

    float a, b;

    switch (p.routing)
    {
        case 1:  a = folder (mixed); b = filter (a); break;      // fold into the filter
        case 2:  b = filter (mixed); a = folder (b); break;      // filter into the folder
        default: a = folder (mixed); b = filter (mixed); break;  // side by side
    }

    auto vca = [eg1, eg2] (int mode) { return mode == 0 ? eg2 : (mode == 1 ? eg1 : 1.0f); };
    const auto out = a * vca (p.vcaW) * (1.0f - p.blend) + b * vca (p.vcaF) * p.blend;

    level += (p.gain - level) * smooth;
    const auto y = out * level;
    dc = y - dcIn + 0.9995f * dc;
    dcIn = y;
    return dc;
}

void Synth::render (float* out, int numSamples)
{
    const auto maxChunk = (int) work.size() / 2;

    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const auto n = std::min (maxChunk, numSamples - start);

        for (int j = 0; j < 2 * n; ++j)
            work[(size_t) j] = processSample();

        decimator.process (work.data(), out + start, n);

        for (int i = 0; i < n; ++i)
            out[start + i] = juce::jlimit (-1.0f, 1.0f, out[start + i]);
    }
}
} // namespace dedal
