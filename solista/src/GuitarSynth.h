#pragma once

#include "dsp/HalfbandDecimator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace solista
{
/** A small electric lead guitar: plucked-string models that can be bent and played legato,
    into an overdriven amp, a speaker filter, a delay and a reverb. */
class GuitarSynth
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void noteOn (int pitch, int velocity, bool legato);
    void noteOff (int pitch);
    void allNotesOff();
    void setBend (float semitones) noexcept { bendTarget = semitones; }

    /** drive, tone and ambience are 0..1. */
    void setSound (float drive, float tone, float ambience, float volumeDb) noexcept;

    /** Renders 'numSamples' and adds nothing: both channels are overwritten. */
    void render (float* left, float* right, int numSamples);

    bool isAnyNoteHeld() const noexcept;

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

        void lowpass (double rate, double frequency, double q);
        void highpass (double rate, double frequency, double q);
        void peak (double rate, double frequency, double q, double gainDb);
        void clear() noexcept { z1 = z2 = 0.0f; }

        float tick (float x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Voice
    {
        std::vector<float> line, burst;
        int write = 0, burstWrite = 0;
        double delay = 200.0;
        float lowpass = 0.0f, feedback = 0.0f, pickLowpass = 0.0f;
        float exciteGain = 0.0f, pickCutoff = 0.5f, pulse = 0.0f;
        float pickup = 0.0f, pickupCutoff = 0.2f;
        int exciteLeft = 0, combDelay = 20;
        int note = -1;
        bool held = false;
        juce::uint32 age = 0;
        float energy = 0.0f;
    };

    static constexpr int kNumVoices = 4;
    static constexpr int kLineSize = 4096;

    float stringSample (Voice& v, float bendSemis) noexcept;
    Voice& allocate();
    void pluck (Voice& v, int pitch, float velocity, float amount);
    double delayFor (double pitch) const noexcept;
    void updateFilters();

    double sampleRate = 44100.0, internalRate = 88200.0;
    std::array<Voice, kNumVoices> voices;
    juce::Random random;
    juce::uint32 counter = 0;

    float bendTarget = 0.0f, bendNow = 0.0f, bendCoef = 0.01f;
    float drive = 0.6f, tone = 0.5f, ambience = 0.3f, volume = 0.5f, volumeNow = 0.0f;
    float lastTone = -1.0f;
    float preHighpass = 0.0f, interLowpass = 0.0f;

    Biquad cabLow1, cabLow2, cabHigh, presence, body;
    gizmo::HalfbandDecimator decimator;
    std::vector<float> work, mono;

    std::vector<float> delayL, delayR;
    int delayWrite = 0;
    float delayDampL = 0.0f, delayDampR = 0.0f;
    juce::Reverb reverb;
    float lastAmbience = -1.0f;
};
} // namespace solista
