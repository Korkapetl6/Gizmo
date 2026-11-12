#include "Engine.h"

namespace dedal2
{
namespace
{
constexpr double pi = 3.14159265358979323846;

inline float saturate (float x) noexcept
{
    const auto a = std::abs (x);
    return a <= 0.8f ? x : std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) * 5.0f), x);
}

inline float hz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
} // namespace

void Engine::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate * 2.0;
    work.assign ((size_t) std::max (32, maxBlock) * 2, 0.0f);
    reset();
}

void Engine::reset()
{
    outs.fill (0.0f);
    high.fill (false);
    phase1 = phase2 = 0.0;
    eg[0] = eg[1] = egTarget[0] = egTarget[1] = 0.0f;
    egAttack[0] = egAttack[1] = false;
    trigLeft[0] = trigLeft[1] = clockLeft = 0;
    noiseLp = ic1 = ic2 = dc = dcIn = foldSmooth = cutSmooth = 0.0f;
    decimator.reset();
}

float Engine::seqSemis (int k, const Params& p, bool quantized) const noexcept
{
    // Full CV RANGE is 5 V either way: five octaves.
    const auto semis = cv[k] * p.range[k] * 60.0f;
    return quantized ? quantize (p.scale, semis) : semis;
}

void Engine::stepSequencer (int k, const Params& p, Sequencers& seq, int sample, Played* played, int& numPlayed, int maxPlayed)
{
    const auto r = seq.step (p.layout, k, p.corrupt[k], forward, 1.0f);

    if (! r.on)
        return;

    cv[k] = r.value;
    trigLeft[k] = (int) (0.005 * rate);

    // EG TRIG MIX: left is SEQ1 only, right is SEQ2 only, both in the middle.
    const auto weight = k == 0 ? (p.trigMix <= 0.5f ? 1.0f : 2.0f * (1.0f - p.trigMix)) : (p.trigMix >= 0.5f ? 1.0f : 2.0f * p.trigMix);

    if (weight > 0.02f)
    {
        egTarget[0] = weight;
        egAttack[0] = true;

        if (source[inEg2Trig].load() < 0)
        {
            egTarget[1] = weight;
            egAttack[1] = true;
        }
    }

    if (numPlayed < maxPlayed && weight > 0.02f)
    {
        const auto base = k == 0 ? p.vcoNote : p.modNote;
        played[numPlayed++] = { sample, k, juce::jlimit (0, 127, juce::roundToInt (base + seqSemis (k, p, true))),
                                juce::jlimit (1, 127, juce::roundToInt (30.0f + 97.0f * weight)) };
    }
}

void Engine::render (float* out, int numSamples, const Params& p, Sequencers& seq, const int* clocks, int numClocks,
                     bool manualTrigger, Played* played, int& numPlayed, int maxPlayed)
{
    numPlayed = 0;
    int src[numIns];

    for (int i = 0; i < numIns; ++i)
        src[i] = source[(size_t) i].load();

    const auto smooth = (float) (1.0 - std::exp (-1.0 / (0.002 * rate)));
    const float decay[2] = { (float) std::exp (-6.9 / (std::max (0.005, (double) p.eg1Decay) * rate)),
                             (float) std::exp (-6.9 / (std::max (0.005, (double) p.eg2Decay) * rate)) };
    const auto attack = (float) (1.0 / (0.001 * rate));
    const auto noiseCoef = (float) (1.0 - std::exp (-2.0 * pi * std::min (60.0 * std::exp2 ((double) p.noiseTone * 8.5), 0.45 * rate) / rate));
    const auto noiseGain = std::min (6.0f, std::sqrt (0.25f / noiseCoef));
    const auto k = 2.0f - 1.96f * juce::jlimit (0.0f, 1.0f, p.resonance);
    int clockIndex = 0;

    // Sample player: a newly loaded sample starts from its start point.
    const auto* smp = sample.load();

    if (smp != smpCurrent)
    {
        smpCurrent = smp;
        smpPos = 0.0;
        smpPlaying = smp != nullptr && ! p.smpRetrig;
        smpKey = -1000.0f;
    }

    const auto smpLength = smp != nullptr ? (double) smp->data.size() : 0.0;
    const auto smpFrom = std::min ((double) p.smpStart, 0.98) * smpLength;
    auto smpFire = manualTrigger && p.smpRetrig;

    if (smp != nullptr && ! p.smpRetrig && ! smpPlaying)
    {
        smpPlaying = true;
        smpPos = smpFrom;
    }

    if (manualTrigger)
    {
        egTarget[0] = egTarget[1] = 1.0f;
        egAttack[0] = egAttack[1] = true;
    }

    auto in = [&] (int i) { return src[i] >= 0 ? outs[(size_t) src[i]] : 0.0f; };
    auto rising = [&] (int i)
    {
        const auto now = src[i] >= 0 && outs[(size_t) src[i]] > 0.2f;
        const auto edge = now && ! high[(size_t) i];
        high[(size_t) i] = now;
        return edge;
    };

    for (int start = 0; start < numSamples; start += (int) work.size() / 2)
    {
        const auto n = std::min ((int) work.size() / 2, numSamples - start);

        for (int j = 0; j < 2 * n; ++j)
        {
            const auto sample = start + j / 2;

            // Clocks: the internal one, or whatever is patched into CLOCK 1 / CLOCK 2.
            auto tick = false;

            while ((j & 1) == 0 && clockIndex < numClocks && clocks[clockIndex] <= sample)
            {
                tick = true;
                ++clockIndex;
            }

            if (tick)
                clockLeft = (int) (0.005 * rate);

            const auto clock1 = src[inClock1] >= 0 ? rising (inClock1) : tick;
            const auto clock2 = src[inClock2] >= 0 ? rising (inClock2) : clock1;

            if (rising (inReset))
            {
                seq.reset (p.layout, 0);
                seq.reset (p.layout, 1);
            }

            if (rising (inBitFlip1)) seq.bitFlip (p.layout, 0);
            if (rising (inBitFlip2)) seq.bitFlip (p.layout, 1);
            if (clock1) stepSequencer (0, p, seq, sample, played, numPlayed, maxPlayed);
            if (clock2) stepSequencer (1, p, seq, sample, played, numPlayed, maxPlayed);

            // The sample restarts with EG1: a sequencer step that fired, or a pulse into TRIGGER.
            if (egAttack[0] && ! smpEgWas && p.smpRetrig)
                smpFire = true;

            if (rising (inTrigger))
            {
                smpFire = smpFire || p.smpRetrig;
                egTarget[0] = 1.0f;
                egAttack[0] = true;

                if (src[inEg2Trig] < 0)
                {
                    egTarget[1] = 1.0f;
                    egAttack[1] = true;
                }
            }

            if (rising (inEg2Trig))
            {
                egTarget[1] = 1.0f;
                egAttack[1] = true;
            }

            smpEgWas = egAttack[0];

            for (int e = 0; e < 2; ++e)
            {
                if (egAttack[e])
                {
                    eg[e] += attack;

                    if (eg[e] >= egTarget[e])
                    {
                        eg[e] = egTarget[e];
                        egAttack[e] = false;
                    }
                }
                else
                {
                    eg[e] *= decay[e];
                }
            }

            // Sequencer CV: AMT fully up follows the quantized sequence, lower it is free.
            const auto seq1Pitch = p.vcoSeq1 >= 0.995f ? seqSemis (0, p, true) : p.vcoSeq1 * seqSemis (0, p, false);
            const auto seq2Pitch = p.modSeq2 >= 0.995f ? seqSemis (1, p, true) : p.modSeq2 * seqSemis (1, p, false);
            const auto seq1Volts = seqSemis (0, p, true) / 60.0f, seq2Volts = seqSemis (1, p, true) / 60.0f;

            // SAMPLE: linear interpolation, looping or one-shot, optionally tracking a sequencer.
            auto sampled = 0.0f;

            if (smp != nullptr && smpLength > 64.0)
            {
                if (smpFire)
                {
                    smpPos = smpFrom;
                    smpPlaying = true;
                    smpFire = false;
                }

                const auto key = p.smpTune + (p.smpFollow == 1 ? seqSemis (0, p, true) : (p.smpFollow == 2 ? seqSemis (1, p, true) : 0.0f));

                if (key != smpKey)
                {
                    smpKey = key;
                    smpInc = smp->rate / rate * std::exp2 ((double) key / 12.0);
                }

                if (smpPlaying)
                {
                    if (smpPos >= smpLength - 1.0)
                    {
                        if (p.smpRetrig)
                            smpPlaying = false;
                        else
                            smpPos = smpFrom + std::fmod (smpPos - smpFrom, std::max (1.0, smpLength - 1.0 - smpFrom));
                    }

                    if (smpPlaying)
                    {
                        const auto at = (size_t) smpPos;
                        const auto frac = (float) (smpPos - (double) at);
                        sampled = smp->data[at] + frac * (smp->data[at + 1] - smp->data[at]);
                        smpPos += smpInc;
                    }
                }
            }

            // MOD VCO: triangle. Jack CVs are 1 V/oct with 5 V = 1.0.
            const auto f2 = hz (p.modNote + seq2Pitch + p.modEg1 * 24.0f * eg[0] + in (inMvcoVoct) * 60.0f);
            phase2 += juce::jlimit (0.0, 0.45, (double) f2 / rate);
            phase2 -= std::floor (phase2);

            // Hard sync on a rising edge near the top of the signal, so the M VCO patched into its own
            // SYNC resets just before its peak: an almost full-height ramp a little above the set pitch.
            if (src[inMvcoSync] >= 0)
            {
                const auto syncHigh = outs[(size_t) src[inMvcoSync]] > 0.9f;

                if (syncHigh && ! high[inMvcoSync])
                    phase2 = 0.0;

                high[inMvcoSync] = syncHigh;
            }

            const auto mod = (float) (phase2 < 0.5 ? 4.0 * phase2 - 1.0 : 3.0 - 4.0 * phase2);

            // VCO: sine, with thru-zero FM from the MOD VCO.
            const auto f1 = hz (p.vcoNote + seq1Pitch + p.vcoEg1 * 24.0f * eg[0] + in (inVcoVoct) * 60.0f);
            phase1 += juce::jlimit (-0.45, 0.45, (double) f1 / rate * (1.0 + 4.0 * (double) p.fm * (double) mod));
            phase1 -= std::floor (phase1);
            const auto vco = (float) std::sin (2.0 * pi * phase1);

            noiseState ^= noiseState << 13;
            noiseState ^= noiseState >> 17;
            noiseState ^= noiseState << 5;
            noiseLp += noiseCoef * ((float) (int) noiseState * (1.0f / 2147483648.0f) - noiseLp);
            const auto noise = noiseLp * noiseGain;

            // Utility mixer: channel 1 is the ring modulator unless something is patched in.
            const auto umix1 = src[inUmix1] >= 0 ? in (inUmix1) : vco * mod;
            const auto umix = juce::jlimit (-1.5f, 1.5f, umix1 * p.umixLvl * 2.0f + in (inUmix2));
            const auto mixer = 0.6f * (saturate (vco * p.lvlVco * 2.0f) + saturate (mod * p.lvlMod * 2.0f)
                                       + juce::jlimit (-1.0f, 1.0f, vco * mod * p.lvlUmix1 * 2.0f) + saturate (noise * p.lvlNoise * 2.0f));

            // FOLD and CUTOFF: EG1 is normalled, a patched CV replaces it and goes through the same AMT knob.
            foldSmooth += (p.fold + p.foldEg * (src[inFold] >= 0 ? in (inFold) : eg[0]) + p.foldSeq * seq1Volts - foldSmooth) * smooth;
            cutSmooth += (p.cutoff * 10.0f + p.cutEg * 6.0f * (src[inCutoff] >= 0 ? in (inCutoff) : eg[0]) + p.cutSeq * 5.0f * seq2Volts - cutSmooth) * smooth;

            auto folder = [&] (float x)
            {
                return std::sin (juce::MathConstants<float>::halfPi * ((1.0f + 7.0f * juce::jlimit (0.0f, 1.0f, foldSmooth)) * x + p.bias));
            };

            auto filter = [&] (float x)
            {
                const auto g = (float) std::tan (pi * juce::jlimit (20.0, 0.45 * rate, 20.0 * std::exp2 ((double) cutSmooth)) / rate);
                const auto a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                const auto v3 = saturate (x) - ic2;
                const auto v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                return v2 + p.mode * (1.4f * v1 - v2);
            };

            float a, b;
            const auto vcwPatched = src[inVcwIn] >= 0, vcfPatched = src[inVcfIn] >= 0;

            if (p.order == 2)
            {
                b = filter (vcfPatched ? in (inVcfIn) : mixer);
                a = folder (vcwPatched ? in (inVcwIn) : b);
            }
            else
            {
                a = folder (vcwPatched ? in (inVcwIn) : mixer);
                b = filter (vcfPatched ? in (inVcfIn) : (p.order == 1 ? a : mixer));
            }

            // VCAs: EG2 is normalled; a patched CV opens fully at 8 V.
            const auto vcaW = src[inVcwVca] >= 0 ? juce::jlimit (0.0f, 1.0f, in (inVcwVca) * 0.625f) : eg[1];
            const auto vcaF = src[inVcfVca] >= 0 ? juce::jlimit (0.0f, 1.0f, in (inVcfVca) * 0.625f) : eg[1];
            const auto blend = juce::jlimit (0.0f, 1.0f, p.blend + 0.5f * in (inBlend));
            const auto y = a * vcaW * (1.0f - blend) + b * vcaF * blend;
            dc = y - dcIn + 0.9995f * dc;
            dcIn = y;

            outs[outVca] = dc;
            outs[outMvco] = mod;
            outs[outNoise] = noise;
            outs[outMixer] = mixer;
            outs[outEg1] = eg[0] * 1.6f;
            outs[outEg2] = eg[1] * 1.6f;
            outs[outUmix] = umix;
            outs[outSeq1Cv] = seq1Volts;
            outs[outSeq2Cv] = seq2Volts;
            outs[outSeq1Trig] = trigLeft[0] > 0 ? 1.0f : 0.0f;
            outs[outSeq2Trig] = trigLeft[1] > 0 ? 1.0f : 0.0f;
            outs[outClock] = clockLeft > 0 ? 1.0f : 0.0f;
            outs[outSample] = sampled;
            trigLeft[0] = std::max (0, trigLeft[0] - 1);
            trigLeft[1] = std::max (0, trigLeft[1] - 1);
            clockLeft = std::max (0, clockLeft - 1);

            level += (p.gain - level) * smooth;
            work[(size_t) j] = dc * level;
        }

        decimator.process (work.data(), out + start, n);
        smpShown = smpPlaying && smpLength > 0.0 ? (float) (smpPos / smpLength) : -1.0f;

        // Soft ceiling instead of a hard clip: loud patches round off like an output stage.
        for (int i = 0; i < n; ++i)
        {
            const auto x = out[start + i];
            out[start + i] = std::abs (x) < 0.7f ? x : std::copysign (0.7f + 0.3f * std::tanh ((std::abs (x) - 0.7f) / 0.3f), x);
        }
    }
}
} // namespace dedal2
