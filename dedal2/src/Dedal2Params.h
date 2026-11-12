#pragma once

namespace dedal2::ID
{
#define DEDAL2_ID(name) inline constexpr const char* name = #name;
DEDAL2_ID (vcoFreq) DEDAL2_ID (vcoEg1) DEDAL2_ID (vcoSeq1) DEDAL2_ID (modFreq) DEDAL2_ID (modEg1) DEDAL2_ID (modSeq2) DEDAL2_ID (fm)
DEDAL2_ID (lvlVco) DEDAL2_ID (lvlRing) DEDAL2_ID (umixLvl) DEDAL2_ID (lvlMod) DEDAL2_ID (lvlNoise) DEDAL2_ID (noiseTone)
DEDAL2_ID (fold) DEDAL2_ID (bias) DEDAL2_ID (foldEg) DEDAL2_ID (foldSeq)
DEDAL2_ID (cutoff) DEDAL2_ID (cutEg) DEDAL2_ID (cutSeq) DEDAL2_ID (resonance) DEDAL2_ID (mode)
DEDAL2_ID (blend) DEDAL2_ID (order) DEDAL2_ID (eg1Decay) DEDAL2_ID (eg2Decay) DEDAL2_ID (trigMix) DEDAL2_ID (volume)
DEDAL2_ID (chain) DEDAL2_ID (scale) DEDAL2_ID (tempo) DEDAL2_ID (trigger)
DEDAL2_ID (midiSend) DEDAL2_ID (midiSeq) DEDAL2_ID (gate) DEDAL2_ID (sound)
#undef DEDAL2_ID
inline constexpr const char* corrupt[] = { "corrupt1", "corrupt2" };
inline constexpr const char* range[] = { "range1", "range2" };
inline constexpr const char* length[] = { "length1", "length2" };
inline constexpr const char* flip[] = { "flip1", "flip2" };
} // namespace dedal2::ID
