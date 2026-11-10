#pragma once

#include "Solo.h"

namespace solista
{
enum StyleId
{
    styleEpic = 0, // builds from long bends to fast pentatonic runs and repeating licks
    styleSinging,  // vocal lines on the full scale, wide vibrato, pre-bends, slides
    styleGroove,   // tight, syncopated sixteenths with rests and short stabs
    styleSwing,    // arpeggios, approach notes, relaxed and melodic
    styleBlues,    // slow bends, late vibrato, behind the beat, double stops
    numStyles
};

constexpr int kNumProgressions = 6;

struct GeneratorParams
{
    int keyPc = 9; // A
    bool minorKey = true;
    int progression = 0;
    int bars = 8;
    int style = styleEpic;
    bool shuffle = false;
    float density = 0.5f;     // how many notes
    float bends = 0.6f;       // how often notes are bent
    float vibrato = 0.6f;     // how often and how wide
    float syncopation = 0.5f; // off-beat entries, pushed notes, displaced repeats
    float space = 0.5f;       // how long the rests between phrases are
    double bpm = 120.0;
    juce::uint32 seed = 1;
};

/** Composes a new solo. The same parameters and seed always give the same solo. */
Solo generateSolo (const GeneratorParams& params);

juce::StringArray getProgressionNames (bool minorKey);
std::vector<Chord> makeChords (int keyPc, bool minorKey, int progression, int bars);
} // namespace solista
