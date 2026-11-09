#pragma once

#include <juce_core/juce_core.h>

namespace gizmo
{
/** Builds a juce::String from a UTF-8 literal (needed for Polish characters). */
inline juce::String pl (const char* utf8) { return juce::String::fromUTF8 (utf8); }

namespace ParamID
{
    inline constexpr const char* position   = "position";
    inline constexpr const char* frameMode  = "frameMode";
    inline constexpr const char* velToPos   = "velToPos";
    inline constexpr const char* wheelToPos = "wheelToPos";
    inline constexpr const char* levelA     = "levelA";
    inline constexpr const char* panA       = "panA";
    inline constexpr const char* levelB     = "levelB";
    inline constexpr const char* panB       = "panB";
    inline constexpr const char* semiB      = "semiB";
    inline constexpr const char* offsetB    = "offsetB";
    inline constexpr const char* detune     = "detune";
    inline constexpr const char* attack     = "attack";
    inline constexpr const char* decay      = "decay";
    inline constexpr const char* sustain    = "sustain";
    inline constexpr const char* release    = "release";
    inline constexpr const char* velToAmp   = "velToAmp";
    inline constexpr const char* voiceMode  = "voiceMode";
    inline constexpr const char* glide      = "glide";
    inline constexpr const char* bendRange  = "bendRange";
    inline constexpr const char* character  = "character";
    inline constexpr const char* gain       = "gain";

    inline constexpr const char* filterOn    = "filterOn";
    inline constexpr const char* cutoff      = "cutoff";
    inline constexpr const char* resonance   = "resonance";
    inline constexpr const char* keyTrack    = "keyTrack";
    inline constexpr const char* filterEnv   = "filterEnv";
    inline constexpr const char* fAttack     = "fAttack";
    inline constexpr const char* fDecay      = "fDecay";
    inline constexpr const char* fSustain    = "fSustain";
    inline constexpr const char* fRelease    = "fRelease";

    inline constexpr const char* lfoShape    = "lfoShape";
    inline constexpr const char* lfoRate     = "lfoRate";
    inline constexpr const char* lfoSync     = "lfoSync";
    inline constexpr const char* lfoDivision = "lfoDivision";
    inline constexpr const char* lfoRetrig   = "lfoRetrig";
    inline constexpr const char* lfoToPos    = "lfoToPos";
    inline constexpr const char* lfoToCutoff = "lfoToCutoff";
    inline constexpr const char* lfoToPitch  = "lfoToPitch";
} // namespace ParamID

/** Tempo-synced LFO lengths, in quarter notes. Matches the lfoDivision choices. */
inline constexpr double kLfoDivisionBeats[] = { 16.0, 8.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
inline constexpr const char* kLfoDivisionNames[] = { "4 takty", "2 takty", "1 takt", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
inline constexpr int kNumLfoDivisions = 13;
inline constexpr int kDefaultLfoDivision = 5; // 1/4
} // namespace gizmo
