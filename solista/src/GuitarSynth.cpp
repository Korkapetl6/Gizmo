#include "GuitarSynth.h"

#include <algorithm>
#include <cmath>

namespace solista
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr float kDamping = 0.2f; // one-pole low-pass inside the string loop
constexpr int kBurstMask = 2047;

inline double frequencyOf (double pitch) noexcept
{
    return 440.0 * std::exp2 ((pitch - 69.0) / 12.0);
}

inline float feedbackFor (double seconds, double frequency) noexcept
{
    return (float) std::exp (-6.908 / (seconds * frequency)); // -60 dB after 'seconds'
}
} // namespace

//==============================================================================
void GuitarSynth::Biquad::lowpass (double rate, double frequency, double q)
{
    const auto w = 2.0 * pi * frequency / rate, alpha = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + alpha;
    b0 = (float) ((1.0 - c) * 0.5 / a0);
    b1 = (float) ((1.0 - c) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * c / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void GuitarSynth::Biquad::highpass (double rate, double frequency, double q)
{
    const auto w = 2.0 * pi * frequency / rate, alpha = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + alpha;
    b0 = (float) ((1.0 + c) * 0.5 / a0);
    b1 = (float) (-(1.0 + c) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * c / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void GuitarSynth::Biquad::peak (double rate, double frequency, double q, double gainDb)
{
    const auto a = std::pow (10.0, gainDb / 40.0);
    const auto w = 2.0 * pi * frequency / rate, alpha = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + alpha / a;
    b0 = (float) ((1.0 + alpha * a) / a0);
    b1 = (float) (-2.0 * c / a0);
    b2 = (float) ((1.0 - alpha * a) / a0);
    a1 = b1;
    a2 = (float) ((1.0 - alpha / a) / a0);
}

//==============================================================================
void GuitarSynth::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    internalRate = newSampleRate * 2.0;

    for (auto& v : voices)
    {
        v.line.assign ((size_t) kLineSize, 0.0f);
        v.burst.assign ((size_t) kBurstMask + 1, 0.0f);
    }

    work.assign ((size_t) std::max (16, maxBlockSize) * 2, 0.0f);
    mono.assign ((size_t) std::max (16, maxBlockSize), 0.0f);
    delayL.assign ((size_t) (sampleRate * 0.6) + 8, 0.0f);
    delayR.assign (delayL.size(), 0.0f);

    bendCoef = (float) (1.0 - std::exp (-1.0 / (0.004 * internalRate)));
    reverb.setSampleRate (sampleRate);
    lastTone = lastAmbience = -1.0f;
    updateFilters();
    reset();
}

void GuitarSynth::reset()
{
    for (auto& v : voices)
    {
        std::fill (v.line.begin(), v.line.end(), 0.0f);
        std::fill (v.burst.begin(), v.burst.end(), 0.0f);
        v.write = v.burstWrite = v.exciteLeft = 0;
        v.lowpass = v.pickLowpass = v.energy = v.pickup = v.pulse = 0.0f;
        v.note = -1;
        v.held = false;
    }

    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    delayWrite = 0;
    delayDampL = delayDampR = preHighpass = interLowpass = 0.0f;
    bendNow = bendTarget = 0.0f;
    volumeNow = volume;

    for (auto* f : { &cabLow1, &cabLow2, &cabHigh, &presence, &body })
        f->clear();

    decimator.reset();
    reverb.reset();
}

void GuitarSynth::setSound (float newDrive, float newTone, float newAmbience, float volumeDb) noexcept
{
    drive = juce::jlimit (0.0f, 1.0f, newDrive);
    tone = juce::jlimit (0.0f, 1.0f, newTone);
    ambience = juce::jlimit (0.0f, 1.0f, newAmbience);
    volume = juce::Decibels::decibelsToGain (volumeDb, -80.0f);
}

void GuitarSynth::updateFilters()
{
    if (std::abs (tone - lastTone) > 0.002f)
    {
        lastTone = tone;
        const auto cutoff = 1700.0 * std::exp2 ((double) tone * 1.8); // speaker roll-off, 1.7 to 5.9 kHz
        cabLow1.lowpass (internalRate, cutoff, 0.9);
        cabLow2.lowpass (internalRate, cutoff * 1.15, 0.6);
        cabHigh.highpass (internalRate, 85.0, 0.7);
        presence.peak (internalRate, 2300.0, 1.1, 2.0 + 3.0 * tone);
        body.peak (internalRate, 420.0, 0.9, 2.5);
    }

    if (std::abs (ambience - lastAmbience) > 0.002f)
    {
        lastAmbience = ambience;
        juce::Reverb::Parameters parameters;
        parameters.roomSize = 0.62f;
        parameters.damping = 0.5f;
        parameters.width = 1.0f;
        parameters.wetLevel = 0.33f * ambience;
        parameters.dryLevel = 1.0f - 0.2f * ambience;
        reverb.setParameters (parameters);
    }
}

double GuitarSynth::delayFor (double pitch) const noexcept
{
    // The loop filter adds a little delay of its own, which would make high notes flat.
    return juce::jlimit (8.0, (double) kLineSize - 8.0, internalRate / frequencyOf (pitch) - kDamping / (1.0 - kDamping));
}

//==============================================================================
GuitarSynth::Voice& GuitarSynth::allocate()
{
    Voice* best = nullptr;

    for (auto& v : voices)
        if (! v.held && (best == nullptr || v.energy < best->energy))
            best = &v;

    if (best == nullptr)
        for (auto& v : voices)
            if (best == nullptr || v.age < best->age)
                best = &v;

    return *best;
}

void GuitarSynth::pluck (Voice& v, int pitch, float velocity, float amount)
{
    const auto frequency = frequencyOf (pitch);
    const auto sustain = juce::jlimit (5.5, 10.0, 10.0 - 4.5 * (pitch - 50) / 40.0);
    const auto period = (int) delayFor (pitch);

    v.note = pitch;
    v.held = true;
    v.age = ++counter;
    v.feedback = feedbackFor (sustain, frequency);

    // The pickup and the guitar's tone control round the string off before the amp, so the amp sings
    // on the note itself instead of buzzing on its overtones.
    const auto openness = amount >= 1.0f ? 1.6 + 1.2 * velocity : 1.9;
    v.pickupCutoff = (float) (1.0 - std::exp (-2.0 * pi * juce::jlimit (200.0, 6000.0, frequency * openness) / internalRate));

    if (amount >= 1.0f)
    {
        // A new pick stroke stops what the string was doing.
        for (auto& s : v.line)
            s *= 0.1f;

        v.lowpass *= 0.1f;
        v.delay = delayFor (pitch + bendNow);
        std::fill (v.burst.begin(), v.burst.end(), 0.0f);
        v.burstWrite = 0;
        v.exciteLeft = std::min (period, kBurstMask);
        v.combDelay = juce::jlimit (1, kBurstMask - 4, (int) (period * 0.27)); // where the pick hits the string
        // The pick: one sharp pull on the string plus a little scrape. Low-passed near the note itself, so
        // every harmonic is a little quieter than the one below it; harder picking is brighter.
        const auto g = 1.0 - std::exp (-2.0 * pi * juce::jlimit (150.0, 5000.0, frequency * (0.6 + 1.6 * velocity)) / internalRate);
        const auto level = 0.2 + 0.6 * std::pow ((double) velocity, 1.5);
        v.pickCutoff = (float) g;
        v.pulse = (float) (level * std::sqrt ((double) period * (2.0 - g) / g) * 0.8) * (random.nextBool() ? 1.0f : -1.0f);
        v.exciteGain = (float) (level * std::sqrt (3.0 * (2.0 - g) / g) * 0.45);
    }
    else
    {
        // Hammer-on or pull-off: the string keeps ringing and just gets a nudge.
        const auto g = 1.0 - std::exp (-2.0 * pi * juce::jlimit (150.0, 4000.0, frequency * 1.2) / internalRate);
        v.exciteLeft = std::min (period / 3, kBurstMask);
        v.pickCutoff = (float) g;
        v.pulse = 0.0f;
        v.exciteGain = (float) (amount * (0.4 + 0.6 * velocity) * std::sqrt (3.0 * (2.0 - g) / g) * 0.5);
    }
}

void GuitarSynth::noteOn (int pitch, int velocity, bool legato)
{
    const auto level = (float) juce::jlimit (1, 127, velocity) / 127.0f;

    if (legato)
    {
        Voice* latest = nullptr;

        for (auto& v : voices)
            if (v.held && (latest == nullptr || v.age > latest->age))
                latest = &v;

        if (latest != nullptr)
        {
            pluck (*latest, pitch, level, 0.16f);
            return;
        }
    }

    pluck (allocate(), pitch, level, 1.0f);
}

void GuitarSynth::noteOff (int pitch)
{
    for (auto& v : voices)
    {
        if (v.held && v.note == pitch)
        {
            v.held = false;
            v.feedback = feedbackFor (0.09, frequencyOf (pitch)); // the finger lifts and the string is muted
        }
    }
}

void GuitarSynth::allNotesOff()
{
    for (auto& v : voices)
    {
        if (v.held)
        {
            v.held = false;
            v.feedback = feedbackFor (0.09, frequencyOf (std::max (0, v.note)));
        }
    }
}

bool GuitarSynth::isAnyNoteHeld() const noexcept
{
    for (const auto& v : voices)
        if (v.held)
            return true;

    return false;
}

float GuitarSynth::stringSample (Voice& v, float bendSemis) noexcept
{
    // Legato steps and bends move the string length smoothly, never in a jump.
    v.delay += (delayFor ((double) v.note + bendSemis) - v.delay) * 0.004;

    const auto position = (double) v.write - v.delay;
    const auto base = std::floor (position);
    const auto t = (float) (position - base);
    const auto i = (int) base;
    auto at = [&v, i] (int k) { return v.line[(size_t) ((i + k) & (kLineSize - 1))]; };

    const auto xm1 = at (-1), x0 = at (0), x1 = at (1), x2 = at (2);
    const auto c1 = 0.5f * (x1 - xm1);
    const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    const auto x = ((c3 * t + c2) * t + c1) * t + x0;

    v.lowpass = x + kDamping * (v.lowpass - x);
    const auto y = v.lowpass * v.feedback;
    float in = 0.0f;

    if (v.exciteLeft > 0)
    {
        // The pick: a burst of filtered noise, with a notch that depends on where it strikes.
        const auto strike = (random.nextFloat() * 2.0f - 1.0f) * v.exciteGain + v.pulse;
        v.pulse = 0.0f;
        v.pickLowpass += v.pickCutoff * (strike - v.pickLowpass);
        v.burst[(size_t) (v.burstWrite & kBurstMask)] = v.pickLowpass;
        in = v.pickLowpass - v.burst[(size_t) ((v.burstWrite - v.combDelay) & kBurstMask)];
        ++v.burstWrite;
        --v.exciteLeft;
    }

    v.line[(size_t) v.write] = y + in;
    v.write = (v.write + 1) & (kLineSize - 1);
    v.energy += 0.0005f * (y * y - v.energy);
    v.pickup += v.pickupCutoff * (y - v.pickup);
    return v.pickup;
}

//==============================================================================
void GuitarSynth::render (float* left, float* right, int numSamples)
{
    updateFilters();

    const auto maxChunk = (int) mono.size();
    const auto preCoef = (float) (1.0 - std::exp (-2.0 * pi * 90.0 / internalRate));
    const auto interCoef = (float) (1.0 - std::exp (-2.0 * pi * 6500.0 / internalRate));
    const auto inputGain = 3.0f + 110.0f * drive * drive;
    const auto secondGain = 1.4f + 2.6f * drive;
    const auto outputGain = 0.19f / (0.62f + 0.6f * drive);
    const auto bias = 0.07f, biasOut = std::tanh (bias);
    const auto volumeCoef = (float) (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));

    const auto delaySize = (int) delayL.size();
    const auto tapL = (int) (sampleRate * 0.37), tapR = (int) (sampleRate * 0.41);
    const auto echo = 0.3f * ambience;

    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const auto n = std::min (maxChunk, numSamples - start);

        for (int j = 0; j < n * 2; ++j)
        {
            bendNow += (bendTarget - bendNow) * bendCoef;
            float s = 0.0f;

            for (auto& v : voices)
                if (v.held || v.energy > 1.0e-9f || v.exciteLeft > 0)
                    s += stringSample (v, bendNow);

            // Amp: tighten the lows, two clipping stages, then the speaker.
            preHighpass += preCoef * (s - preHighpass);
            auto x = std::tanh ((s - preHighpass) * inputGain + bias) - biasOut;
            interLowpass += interCoef * (x - interLowpass);
            x = std::tanh (interLowpass * secondGain);
            x = body.tick (x);
            x = presence.tick (x);
            x = cabHigh.tick (x);
            x = cabLow2.tick (cabLow1.tick (x));
            work[(size_t) j] = x * outputGain;
        }

        decimator.process (work.data(), mono.data(), n);

        for (int i = 0; i < n; ++i)
        {
            // Two echoes, slightly apart, darker with every repeat.
            const auto dry = mono[(size_t) i];
            const auto echoL = delayL[(size_t) ((delayWrite - tapL + delaySize) % delaySize)];
            const auto echoR = delayR[(size_t) ((delayWrite - tapR + delaySize) % delaySize)];
            delayDampL += 0.35f * (echoL - delayDampL);
            delayDampR += 0.35f * (echoR - delayDampR);
            delayL[(size_t) delayWrite] = dry + delayDampR * 0.32f;
            delayR[(size_t) delayWrite] = dry + delayDampL * 0.32f;
            delayWrite = (delayWrite + 1) % delaySize;

            left[start + i] = dry + echo * delayDampL;
            right[start + i] = dry + echo * delayDampR;
        }

        reverb.processStereo (left + start, right + start, n);

        for (int i = 0; i < n; ++i)
        {
            volumeNow += (volume - volumeNow) * volumeCoef;
            left[start + i] *= volumeNow;
            right[start + i] *= volumeNow;
        }
    }
}
} // namespace solista
