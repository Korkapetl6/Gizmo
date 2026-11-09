#include "SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace gizmo
{
namespace
{
constexpr double twoPi = 6.283185307179586;

inline double noteToFrequency (double note) noexcept
{
    return 440.0 * std::exp2 ((note - 69.0) / 12.0);
}

inline void panGains (float pan, float level, float& left, float& right) noexcept
{
    const auto angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    left = std::cos (angle) * level;
    right = std::sin (angle) * level;
}

inline float readFrame (const float* m, int i, float frac) noexcept
{
    return m[i] + frac * (m[i + 1] - m[i]);
}

/** Reads one sample of an oscillator and advances its phase.
    In stepped mode the frame only changes when the cycle wraps around, like an Ensoniq transwave.
    The new frame is faded in over at most one cycle (and at most 'maxFade' samples), so the jump
    between two different waveforms doesn't click. */
inline float readOscillator (const Wavetable& table, int level, float framePos, SynthEngine::OscState& s,
                             double increment, bool stepped, int maxFade) noexcept
{
    const auto x = s.phase * (double) kFrameSize;
    const auto i = (int) x;
    const auto frac = (float) (x - (double) i);
    float out;

    if (stepped)
    {
        if (s.frame < 0)
            s.frame = juce::roundToInt (framePos);

        out = readFrame (table.getMip (level, s.frame), i, frac);

        if (s.fadeLeft > 0)
        {
            const auto previous = readFrame (table.getMip (level, s.prevFrame), i, frac);
            out += (previous - out) * (float) s.fadeLeft / (float) s.fadeLength;
            --s.fadeLeft;
        }
    }
    else
    {
        const auto k0 = std::min ((int) framePos, kNumFrames - 2);
        const auto t = framePos - (float) k0;
        const auto* m0 = table.getMip (level, k0);
        const auto a = readFrame (m0, i, frac);
        const auto b = readFrame (m0 + kMipStride, i, frac);
        out = a + t * (b - a);
    }

    s.phase += increment;

    if (s.phase >= 1.0)
    {
        s.phase -= std::floor (s.phase);

        if (stepped && s.fadeLeft == 0)
        {
            const auto next = juce::roundToInt (framePos);

            if (next != s.frame)
            {
                s.prevFrame = s.frame;
                s.frame = next;
                s.fadeLength = s.fadeLeft = std::max (1, std::min (maxFade, (int) (1.0 / increment)));
            }
        }
    }

    return out;
}

/** Reads the original sample at a fractional position (4-point Hermite), wrapping around its ends. */
inline float readSource (const std::vector<float>& s, double pos) noexcept
{
    const auto n = (int) s.size();
    pos = std::fmod (pos, (double) n);

    if (pos < 0.0)
        pos += (double) n;

    const auto i = (int) pos;
    const auto t = (float) (pos - (double) i);
    auto at = [&s, n] (int k) { return s[(size_t) ((k % n + n) % n)]; };

    const auto xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const auto c1 = 0.5f * (x1 - xm1);
    const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

inline bool isStepB (juce::uint32 pattern, juce::int64 stepIndex, int length) noexcept
{
    const auto step = (int) (((stepIndex % length) + length) % length);
    return ((pattern >> step) & 1u) != 0;
}

/** Coefficients of a state-variable filter (Andrew Simper's trapezoidal form). */
struct SvfCoefficients
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    void set (float g, float q) noexcept
    {
        const auto k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

inline float svfLowpass (float in, float* s, const SvfCoefficients& c) noexcept
{
    const auto v3 = in - s[1];
    const auto v1 = c.a1 * s[0] + c.a2 * v3;
    const auto v2 = s[1] + c.a2 * s[0] + c.a3 * v3;
    s[0] = 2.0f * v1 - s[0];
    s[1] = 2.0f * v2 - s[1];
    return v2;
}

/** One envelope step. 'inc' and the coefficients are already scaled for the step length. */
template <typename StageType>
inline void advanceEnvelope (StageType& stage, float& level, float attackInc, float decayCoef, float sustain,
                             float releaseCoef) noexcept
{
    switch (stage)
    {
        case StageType::attack:
            level += attackInc;

            if (level >= 1.0f)
            {
                level = 1.0f;
                stage = StageType::decay;
            }
            break;

        case StageType::decay:
            level = sustain + (level - sustain) * decayCoef;

            if (std::abs (level - sustain) < 1.0e-4f)
            {
                level = sustain;
                stage = StageType::sustain;
            }
            break;

        case StageType::sustain:
            level = sustain + (level - sustain) * decayCoef;
            break;

        case StageType::release:
            level *= releaseCoef;

            if (level < 1.0e-5f)
            {
                level = 0.0f;
                stage = StageType::idle;
            }
            break;

        case StageType::idle:
            break;
    }
}
} // namespace

SynthEngine::SynthEngine()
{
    for (auto& p : voicePositions)
        p.store (-1.0f);
}

void SynthEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    internalRate = newSampleRate * 2.0;
    maxBlock = std::max (16, maxBlockSize);

    posBuffer.assign ((size_t) maxBlock * 2, 0.0f);
    workL.assign ((size_t) maxBlock * 2, 0.0f);
    workR.assign ((size_t) maxBlock * 2, 0.0f);
    bendBuffer.assign ((size_t) (maxBlock * 2 / kSub + 2), 0.0f);
    lfoBuffer.assign ((size_t) (maxBlock * 2 / kSub + 2), 0.0f);

    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices)
        v = Voice {};

    for (auto& p : voicePositions)
        p.store (-1.0f);

    heldNotes.clear();
    sustainPedal = false;
    pitchBend = bendSmoothed = 0.0f;
    wheel = wheelSmoothed = 0.0f;
    firstBlock = true;
    lastNote = -1.0f;
    globalLfoPhase = 0.0;
    globalLfoRandom = 0.0f;
    seqClockGlobal = 0.0;
    lastGlobalStep = -1;
    displayStep.store (-1);
    decimL.reset();
    decimR.reset();
}

int SynthEngine::mipLevelFor (double frequency) const noexcept
{
    // After the 2x decimator everything up to ~1.4 x the output rate is either kept clean
    // or filtered away, so the top harmonic may go that high.
    const auto ratio = frequency * (double) (kFrameSize / 2) / (1.4 * sampleRate);

    if (ratio <= 1.0)
        return 0;

    return std::min ((int) std::ceil (std::log2 (ratio)), kNumMipLevels - 1);
}

float SynthEngine::lfoShapeValue (int shape, double phase, float randomValue) noexcept
{
    switch (shape)
    {
        case 0:  return (float) std::sin (twoPi * phase);
        case 1:  return (float) (phase < 0.25 ? 4.0 * phase : (phase < 0.75 ? 2.0 - 4.0 * phase : 4.0 * phase - 4.0));
        case 2:  { auto q = phase + 0.5; q -= std::floor (q); return (float) (2.0 * q - 1.0); }
        case 3:  return phase < 0.5 ? 1.0f : -1.0f;
        default: return randomValue;
    }
}

double SynthEngine::lfoFrequency (const EngineParams& p) const noexcept
{
    if (p.lfoSync)
        return (std::max (1.0, p.bpm) / 60.0) / std::max (1.0e-3, p.lfoBeats);

    return (double) p.lfoRate;
}

void SynthEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const EngineParams& p,
                           const Wavetable* tableA, const Wavetable* tableB)
{
    const auto numSamples = buffer.getNumSamples();
    buffer.clear();

    if (tableA == nullptr || numSamples == 0)
        return;

    if (tableB == nullptr)
        tableB = tableA;

    if (p.mono != monoMode)
    {
        releaseAll();
        heldNotes.clear();
        monoMode = p.mono;
    }

    // A synced, free-running LFO follows the host's bar grid.
    if (p.lfoSync && ! p.lfoRetrig && p.transportKnown)
    {
        const auto cycles = p.ppqAtBlockStart / std::max (1.0e-3, p.lfoBeats);
        globalLfoPhase = cycles - std::floor (cycles);
    }

    // The A/B sequence: a shared clock follows the host's bar grid while it plays.
    stepsPerSample = (std::max (1.0, p.bpm) / 60.0) / std::max (1.0e-3, p.seqStepBeats) / internalRate;

    if (p.seqOn && ! p.seqPerNote && p.transportKnown)
        seqClockGlobal = p.ppqAtBlockStart / std::max (1.0e-3, p.seqStepBeats);

    positionStart = firstBlock ? p.position : lastPosition;
    positionEnd = p.position;
    lastPosition = p.position;

    if (firstBlock)
        gainSmoothed = juce::Decibels::decibelsToGain (p.gainDb, -100.0f);

    firstBlock = false;

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    int rendered = 0;

    for (const auto meta : midi)
    {
        const auto eventPos = juce::jlimit (0, numSamples, meta.samplePosition);

        if (eventPos > rendered)
        {
            renderRange (outL, outR, rendered, eventPos, numSamples, p, *tableA, *tableB);
            rendered = eventPos;
        }

        handleMidi (meta.getMessage(), p);
    }

    if (rendered < numSamples)
        renderRange (outL, outR, rendered, numSamples, numSamples, p, *tableA, *tableB);

    // State for the UI
    const auto basePos = positionEnd + wheelSmoothed * p.wheelToPos;
    const auto globalLfo = p.lfoRetrig ? 0.0f : lfoShapeValue (p.lfoShape, globalLfoPhase, globalLfoRandom);
    displayPosition.store (juce::jlimit (0.0f, 1.0f, basePos + globalLfo * p.lfoToPos * 0.5f), std::memory_order_relaxed);

    {
        auto step = -1;

        if (p.seqOn)
        {
            const auto length = juce::jlimit (1, 16, p.seqLength);
            juce::int64 index = (juce::int64) std::floor (seqClockGlobal);

            if (p.seqPerNote)
            {
                const Voice* newest = nullptr;

                for (const auto& v : voices)
                    if (v.stage != Stage::idle && v.stage != Stage::release && (newest == nullptr || v.age > newest->age))
                        newest = &v;

                index = newest != nullptr ? (juce::int64) std::floor (newest->seqClock) : -1;
            }

            if (index >= 0)
                step = (int) (index % length);
        }

        displayStep.store (step, std::memory_order_relaxed);
    }

    for (size_t i = 0; i < voices.size(); ++i)
    {
        const auto& v = voices[i];
        const auto pos = v.stage == Stage::idle
                             ? -1.0f
                             : juce::jlimit (0.0f, 1.0f, basePos + p.velToPos * v.velocity + v.lastLfo * p.lfoToPos * 0.5f);
        voicePositions[i].store (pos, std::memory_order_relaxed);
    }
}

void SynthEngine::renderRange (float* outL, float* outR, int from, int to, int blockLength, const EngineParams& p,
                               const Wavetable& a, const Wavetable& b)
{
    const auto wheelCoef = 1.0f - std::exp (-1.0f / (0.010f * (float) internalRate));
    const auto bendCoef = 1.0f - std::exp (-(float) kSub / (0.004f * (float) internalRate));
    const auto gainTarget = juce::Decibels::decibelsToGain (p.gainDb, -100.0f);
    const auto gainCoef = 1.0f - std::exp (-1.0f / (0.020f * (float) sampleRate));
    const auto bendTarget = pitchBend * (float) p.bendRange;
    const auto lfoInc = lfoFrequency (p) * (double) kSub / internalRate;

    for (int start = from; start < to; start += maxBlock)
    {
        const auto n = std::min (maxBlock, to - start);
        const auto n2 = n * 2;

        // Position for every internal sample: host value ramped across the block, plus the mod wheel.
        for (int j = 0; j < n2; ++j)
        {
            const auto s = (float) start + (float) (j + 1) * 0.5f;
            const auto pos = positionStart + (positionEnd - positionStart) * s / (float) blockLength;
            wheelSmoothed += (wheel - wheelSmoothed) * wheelCoef;
            posBuffer[(size_t) j] = pos + wheelSmoothed * p.wheelToPos;
        }

        const auto numSubBlocks = (n2 + kSub - 1) / kSub;

        for (int sb = 0; sb < numSubBlocks; ++sb)
        {
            bendSmoothed += (bendTarget - bendSmoothed) * bendCoef;
            bendBuffer[(size_t) sb] = bendSmoothed;

            lfoBuffer[(size_t) sb] = lfoShapeValue (p.lfoShape, globalLfoPhase, globalLfoRandom);
            globalLfoPhase += lfoInc;

            if (globalLfoPhase >= 1.0)
            {
                globalLfoPhase -= std::floor (globalLfoPhase);
                globalLfoRandom = random.nextFloat() * 2.0f - 1.0f;
            }
        }

        std::fill (workL.begin(), workL.begin() + n2, 0.0f);
        std::fill (workR.begin(), workR.begin() + n2, 0.0f);

        seqClockChunkStart = seqClockGlobal;

        for (auto& v : voices)
            if (v.stage != Stage::idle)
                renderVoice (v, n2, p, a, b);

        seqClockGlobal += (double) n2 * stepsPerSample;

        decimL.process (workL.data(), outL + start, n);

        if (outR != nullptr)
            decimR.process (workR.data(), outR + start, n);

        for (int i = 0; i < n; ++i)
        {
            gainSmoothed += (gainTarget - gainSmoothed) * gainCoef;
            outL[start + i] *= gainSmoothed;

            if (outR != nullptr)
                outR[start + i] *= gainSmoothed;
        }
    }
}

void SynthEngine::renderVoice (Voice& v, int numInternal, const EngineParams& p, const Wavetable& tableA,
                               const Wavetable& tableB)
{
    const auto rate = (float) internalRate;
    const auto sub = (float) kSub;

    // Amplitude envelope runs per sample, the filter envelope per control block.
    const auto attackInc = 1.0f / std::max (1.0f, p.attack * rate);
    const auto decayCoef = std::exp (-6.9f / std::max (1.0f, p.decay * rate));
    const auto releaseCoef = std::exp (-6.9f / std::max (1.0f, p.release * rate));
    const auto sustain = juce::jlimit (0.0f, 1.0f, p.sustain);

    const auto fAttackInc = sub / std::max (sub, p.fAttack * rate);
    const auto fDecayCoef = std::exp (-6.9f * sub / std::max (sub, p.fDecay * rate));
    const auto fReleaseCoef = std::exp (-6.9f * sub / std::max (sub, p.fRelease * rate));
    const auto fSustain = juce::jlimit (0.0f, 1.0f, p.fSustain);

    const auto glideCoef = p.glide > 0.0f ? std::exp (-4.6f * sub / (p.glide * rate)) : 0.0f;
    const auto velGain = 1.0f - p.velToAmp * (1.0f - v.velocity * v.velocity);
    const auto velOffset = p.velToPos * v.velocity;
    const auto halfDetune = p.detuneCents * 0.005f; // half the spread, in semitones
    const auto lastFrame = (float) (kNumFrames - 1);
    const auto maxFrequency = internalRate * 0.45;
    const auto useA = p.levelA > 0.0f;
    const auto useB = p.levelB > 0.0f;
    const auto pitchDepth = 12.0f * p.lfoToPitch * p.lfoToPitch * p.lfoToPitch;
    const auto lfoInc = lfoFrequency (p) * (double) kSub / internalRate;
    const auto maxCutoff = (float) std::min (24000.0, internalRate * 0.45);
    const auto resonantQ = 1.3066f * (1.0f + 8.0f * p.resonance * p.resonance);
    const auto stepFade = std::max (1, (int) (0.0015 * internalRate));

    // A/B sequence: B steps play the sample of table A from the current position.
    const auto& source = tableA.source;
    const auto sequenceActive = p.seqOn && ! source.empty();
    const auto seqLength = juce::jlimit (1, 16, p.seqLength);
    const auto stepSamples = 1.0 / std::max (1.0e-12, stepsPerSample); // internal samples per step
    const auto sourceLength = (double) source.size();
    const auto mixStep = 1.0f / (0.004f * rate);                         // 4 ms crossfade between A and B
    const auto fragGain = p.fragLevel * 0.7071f;

    float aL, aR, bL, bR;
    panGains (p.panA, p.levelA, aL, aR);
    panGains (p.panB, p.levelB, bL, bR);

    SvfCoefficients c1, c2;

    for (int i = 0, sb = 0; i < numInternal && v.stage != Stage::idle; i += kSub, ++sb)
    {
        const auto n = std::min (kSub, numInternal - i);

        // LFO: the voice's own phase when retriggered per note, otherwise the shared one.
        float lfo;

        if (p.lfoRetrig)
        {
            lfo = lfoShapeValue (p.lfoShape, v.lfoPhase, v.lfoRandom);
            v.lfoPhase += lfoInc;

            if (v.lfoPhase >= 1.0)
            {
                v.lfoPhase -= std::floor (v.lfoPhase);
                v.lfoRandom = random.nextFloat() * 2.0f - 1.0f;
            }
        }
        else
        {
            lfo = lfoBuffer[(size_t) sb];
        }

        v.lastLfo = lfo;

        // Pitch
        v.pitch = glideCoef > 0.0f ? v.targetPitch + (v.pitch - v.targetPitch) * glideCoef : v.targetPitch;
        const auto basePitch = (double) v.pitch + (double) bendBuffer[(size_t) sb] + (double) (lfo * pitchDepth);
        const auto freqA = std::min (noteToFrequency (basePitch - halfDetune), maxFrequency);
        const auto freqB = std::min (noteToFrequency (basePitch + p.semiB + halfDetune), maxFrequency);
        const auto incA = freqA / internalRate;
        const auto incB = freqB / internalRate;
        const auto levelA = p.raw ? 0 : mipLevelFor (freqA);
        const auto levelB = p.raw ? 0 : mipLevelFor (freqB);
        const auto posOffset = velOffset + lfo * p.lfoToPos * 0.5f;

        // Which step are we on, and does a run of B steps start here?
        auto wantB = false;
        auto fragSpeed = 0.0;

        if (sequenceActive)
        {
            const auto clock = p.seqPerNote ? v.seqClock : seqClockChunkStart + (double) i * stepsPerSample;
            const auto stepIndex = (juce::int64) std::floor (clock);
            wantB = isStepB (p.seqPattern, stepIndex, seqLength);

            const auto keyOffset = p.fragFollowKeys ? basePitch - (double) tableA.rootNote : 0.0;
            fragSpeed = juce::jlimit (1.0 / 16.0, 8.0, tableA.sourceRate / internalRate
                                                          * std::exp2 ((keyOffset + p.fragTranspose) / 12.0));

            if (stepIndex != v.lastStep)
            {
                const auto wasB = v.lastStep >= 0 && isStepB (p.seqPattern, v.lastStep, seqLength);

                if (wantB && ! wasB)
                {
                    // The run lasts as many steps as there are B's in a row (a pattern of only B never ends).
                    int run = 0;

                    while (run < seqLength && isStepB (p.seqPattern, stepIndex + run, seqLength))
                        ++run;

                    const auto remaining = ((double) run - (clock - std::floor (clock))) * stepSamples;
                    const auto pos = juce::jlimit (0.0f, 1.0f, posBuffer[(size_t) i] + posOffset);
                    auto start = (((double) pos * (kNumFrames - 1) + 0.5) / kNumFrames + (double) p.fragOffset) * sourceLength;
                    start = std::fmod (start, sourceLength);

                    v.fragStart = start < 0.0 ? start + sourceLength : start;
                    v.fragSpan = remaining * fragSpeed;
                    v.fragTravel = 0.0;
                    v.fragActive = true;
                }

                v.lastStep = stepIndex;
            }

            if (p.seqPerNote)
                v.seqClock += (double) n * stepsPerSample;
        }
        else
        {
            v.lastStep = -1;
        }

        const auto mixTarget = wantB ? 1.0f : 0.0f;

        if (v.abMix < 0.0f)
            v.abMix = mixTarget;

        // Filter
        advanceEnvelope (v.filterStage, v.filterEnvLevel, fAttackInc, fDecayCoef, fSustain, fReleaseCoef);

        if (p.filterOn)
        {
            const auto octaves = p.keyTrack * (v.pitch - 60.0f) / 12.0f
                               + p.filterEnv * 6.0f * v.filterEnvLevel
                               + p.lfoToCutoff * 4.0f * lfo;
            const auto cutoff = juce::jlimit (16.0f, maxCutoff, p.cutoff * std::exp2 (octaves));
            const auto g = std::tan (juce::MathConstants<float>::pi * cutoff / rate);
            c1.set (g, 0.5412f);
            c2.set (g, resonantQ);
        }

        for (int j = i; j < i + n; ++j)
        {
            advanceEnvelope (v.stage, v.env, attackInc, decayCoef, sustain, releaseCoef);

            if (v.stage == Stage::idle)
                break;

            const auto posA = juce::jlimit (0.0f, 1.0f, posBuffer[(size_t) j] + posOffset);
            float outA = 0.0f, outB = 0.0f;

            if (useA)
                outA = readOscillator (tableA, levelA, posA * lastFrame, v.oscA, incA, p.steppedFrames, stepFade);

            if (useB)
            {
                const auto posB = juce::jlimit (0.0f, 1.0f, posA + p.offsetB);
                outB = readOscillator (tableB, levelB, posB * lastFrame, v.oscB, incB, p.steppedFrames, stepFade);
            }

            auto left = outA * aL + outB * bL;
            auto right = outA * aR + outB * bR;

            if (v.fragActive)
            {
                double srcPos;

                if (p.fragDirection == 0)
                {
                    srcPos = v.fragStart + v.fragTravel;
                }
                else if (p.fragDirection == 1)
                {
                    srcPos = v.fragStart + v.fragSpan - v.fragTravel; // the same stretch, backwards
                }
                else
                {
                    const auto t = v.fragSpan > 0.0 ? std::fmod (v.fragTravel, v.fragSpan) : v.fragTravel;
                    srcPos = v.fragStart + (t < v.fragSpan * 0.5 ? t : v.fragSpan - t);
                }

                const auto fragment = readSource (source, srcPos) * fragGain;
                v.fragTravel += fragSpeed;

                if (v.abMix != mixTarget)
                    v.abMix = juce::jlimit (0.0f, 1.0f, v.abMix + juce::jlimit (-mixStep, mixStep, mixTarget - v.abMix));

                const auto angle = v.abMix * juce::MathConstants<float>::halfPi;
                const auto gainA = v.abMix <= 0.0f ? 1.0f : (v.abMix >= 1.0f ? 0.0f : std::cos (angle));
                const auto gainB = v.abMix <= 0.0f ? 0.0f : (v.abMix >= 1.0f ? 1.0f : std::sin (angle));

                left = left * gainA + fragment * gainB;
                right = right * gainA + fragment * gainB;

                if (v.abMix <= 0.0f && ! wantB)
                    v.fragActive = false;
            }

            if (p.filterOn)
            {
                left = svfLowpass (svfLowpass (left, v.svf1[0], c1), v.svf2[0], c2);
                right = svfLowpass (svfLowpass (right, v.svf1[1], c1), v.svf2[1], c2);
            }

            const auto amp = v.env * velGain;
            workL[(size_t) j] += amp * left;
            workR[(size_t) j] += amp * right;
        }
    }
}

void SynthEngine::handleMidi (const juce::MidiMessage& m, const EngineParams& p)
{
    if (m.isNoteOn())
    {
        noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
    }
    else if (m.isNoteOff())
    {
        noteOff (m.getNoteNumber(), p);
    }
    else if (m.isPitchWheel())
    {
        pitchBend = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    }
    else if (m.isController())
    {
        const auto number = m.getControllerNumber();
        const auto value = m.getControllerValue();

        if (number == 1)
        {
            wheel = (float) value / 127.0f;
        }
        else if (number == 64)
        {
            const auto down = value >= 64;

            if (! down && sustainPedal)
            {
                for (auto& v : voices)
                {
                    if (v.sustained)
                    {
                        v.sustained = false;

                        if (! v.keyDown)
                            enterRelease (v);
                    }
                }
            }

            sustainPedal = down;
        }
        else if (number == 123 || number == 120)
        {
            if (number == 120)
            {
                for (auto& v : voices)
                {
                    v.stage = Stage::idle;
                    v.env = 0.0f;
                }
            }

            releaseAll();
            heldNotes.clear();
        }
    }
}

void SynthEngine::noteOn (int note, float velocity, const EngineParams& p)
{
    if (p.mono)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);

        auto& v = voices[0];
        const auto legato = heldNotes.size() > 1 && (v.stage == Stage::attack || v.stage == Stage::decay
                                                     || v.stage == Stage::sustain);

        if (legato)
        {
            v.note = note;
            v.targetPitch = (float) note;
            v.keyDown = true;

            if (p.glide <= 0.0f)
                v.pitch = (float) note;

            lastNote = (float) note;
        }
        else
        {
            startVoice (v, note, velocity, p);
        }

        return;
    }

    Voice* target = nullptr;

    for (auto& v : voices)
        if (v.stage != Stage::idle && v.note == note)
            target = &v;

    if (target == nullptr)
        for (auto& v : voices)
            if (v.stage == Stage::idle)
            {
                target = &v;
                break;
            }

    if (target == nullptr)
        for (auto& v : voices)
            if (v.stage == Stage::release && (target == nullptr || v.env < target->env))
                target = &v;

    if (target == nullptr)
        for (auto& v : voices)
            if (target == nullptr || v.age < target->age)
                target = &v;

    startVoice (*target, note, velocity, p);
}

void SynthEngine::noteOff (int note, const EngineParams& p)
{
    if (p.mono)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        auto& v = voices[0];

        if (v.stage == Stage::idle || v.note != note)
            return;

        if (! heldNotes.empty())
        {
            const auto previous = heldNotes.back();
            v.note = previous;
            v.targetPitch = (float) previous;

            if (p.glide <= 0.0f)
                v.pitch = (float) previous;

            lastNote = (float) previous;
        }
        else
        {
            releaseOrSustain (v);
        }

        return;
    }

    for (auto& v : voices)
        if (v.stage != Stage::idle && v.note == note && v.keyDown)
            releaseOrSustain (v);
}

void SynthEngine::startVoice (Voice& v, int note, float velocity, const EngineParams& p)
{
    const auto wasIdle = v.stage == Stage::idle;

    v.note = note;
    v.velocity = velocity;
    v.keyDown = true;
    v.sustained = false;
    v.targetPitch = (float) note;
    v.pitch = (p.glide > 0.0f && lastNote >= 0.0f) ? lastNote : (float) note;

    if (wasIdle)
    {
        // A fresh voice starts from silence, so resetting phases and filter can't click.
        v.env = 0.0f;
        v.filterEnvLevel = 0.0f;
        v.oscA.phase = 0.0;
        v.oscB.phase = random.nextDouble();
        std::fill (&v.svf1[0][0], &v.svf1[0][0] + 4, 0.0f);
        std::fill (&v.svf2[0][0], &v.svf2[0][0] + 4, 0.0f);
    }

    // A stolen voice keeps its phase and envelope level, so it doesn't click either.
    v.lfoPhase = 0.0;
    v.lfoRandom = random.nextFloat() * 2.0f - 1.0f;
    v.seqClock = 0.0;
    v.lastStep = -1;

    if (wasIdle)
    {
        v.abMix = -1.0f; // take A or B straight away; the envelope starts from silence anyway
        v.fragActive = false;
    }
    for (auto* osc : { &v.oscA, &v.oscB })
    {
        osc->frame = -1;
        osc->fadeLeft = 0;
    }

    v.stage = Stage::attack;
    v.filterStage = Stage::attack;
    v.age = ++voiceCounter;
    lastNote = (float) note;
}

void SynthEngine::enterRelease (Voice& v) noexcept
{
    if (v.stage != Stage::idle)
    {
        v.stage = Stage::release;
        v.filterStage = Stage::release;
    }
}

void SynthEngine::releaseOrSustain (Voice& v)
{
    v.keyDown = false;

    if (sustainPedal)
        v.sustained = true;
    else
        enterRelease (v);
}

void SynthEngine::releaseAll()
{
    for (auto& v : voices)
    {
        v.keyDown = false;
        v.sustained = false;
        enterRelease (v);
    }
}
} // namespace gizmo
