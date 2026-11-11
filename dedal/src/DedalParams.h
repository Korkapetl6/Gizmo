#pragma once

namespace dedal::ID
{
// Sequencers (index 1 and 2)
inline constexpr const char* corrupt[] = { "corrupt1", "corrupt2" };
inline constexpr const char* range[] = { "range1", "range2" };
inline constexpr const char* rate[] = { "rate1", "rate2" };
inline constexpr const char* swing[] = { "swing1", "swing2" };
inline constexpr const char* length[] = { "length1", "length2" };
inline constexpr const char* flip[] = { "flip1", "flip2" };

inline constexpr const char* chain = "chain";
inline constexpr const char* scale = "scale";
inline constexpr const char* key = "key";
inline constexpr const char* octave = "octave";
inline constexpr const char* polarity = "polarity";
inline constexpr const char* trigMix = "trigMix";
inline constexpr const char* midiTranspose = "midiTranspose";
inline constexpr const char* trigger = "trigger";

// VCO and MOD VCO
inline constexpr const char* vcoType = "vcoType";
inline constexpr const char* vcoBank = "vcoBank";
inline constexpr const char* vcoWave = "vcoWave";
inline constexpr const char* vcoPw = "vcoPw";
inline constexpr const char* vcoTune = "vcoTune";
inline constexpr const char* vcoFine = "vcoFine";
inline constexpr const char* seq1Pitch = "seq1Pitch";
inline constexpr const char* seq1Wave = "seq1Wave";
inline constexpr const char* eg1Vco = "eg1Vco";
inline constexpr const char* glide = "glide";

inline constexpr const char* modType = "modType";
inline constexpr const char* modBank = "modBank";
inline constexpr const char* modWave = "modWave";
inline constexpr const char* modPw = "modPw";
inline constexpr const char* modTune = "modTune";
inline constexpr const char* modMode = "modMode";
inline constexpr const char* modRate = "modRate";
inline constexpr const char* modSync = "modSync";
inline constexpr const char* seq2Pitch = "seq2Pitch";
inline constexpr const char* seq2Wave = "seq2Wave";
inline constexpr const char* eg1Mod = "eg1Mod";

// Mixer, folder, filter
inline constexpr const char* lvlVco = "lvlVco";
inline constexpr const char* lvlMod = "lvlMod";
inline constexpr const char* lvlRing = "lvlRing";
inline constexpr const char* lvlNoise = "lvlNoise";
inline constexpr const char* noiseTone = "noiseTone";
inline constexpr const char* fm = "fm";

inline constexpr const char* fold = "fold";
inline constexpr const char* bias = "bias";
inline constexpr const char* seq1Fold = "seq1Fold";
inline constexpr const char* eg1Fold = "eg1Fold";

inline constexpr const char* cutoff = "cutoff";
inline constexpr const char* resonance = "resonance";
inline constexpr const char* filterType = "filterType";
inline constexpr const char* seq2Cutoff = "seq2Cutoff";
inline constexpr const char* eg1Cutoff = "eg1Cutoff";

inline constexpr const char* routing = "routing";
inline constexpr const char* blend = "blend";
inline constexpr const char* eg1Decay = "eg1Decay";
inline constexpr const char* eg2Decay = "eg2Decay";
inline constexpr const char* vcaW = "vcaW";
inline constexpr const char* vcaF = "vcaF";
inline constexpr const char* volume = "volume";
inline constexpr const char* sound = "sound";

// MIDI out
inline constexpr const char* midiSeq = "midiSeq";
inline constexpr const char* midiSend = "midiSend";
inline constexpr const char* gate = "gate";
inline constexpr const char* ccSend = "ccSend";
inline constexpr const char* ccNumber = "ccNumber";
} // namespace dedal::ID

namespace dedal::ID
{
// Direction and chance of SEQ1 and SEQ2
inline constexpr const char* direction[] = { "dir1", "dir2" };
inline constexpr const char* chance[] = { "chance1", "chance2" };

// SEQ C: rhythm and probability
inline constexpr const char* lengthC = "lengthC";
inline constexpr const char* rateC = "rateC";
inline constexpr const char* swingC = "swingC";
inline constexpr const char* corruptC = "corruptC";
inline constexpr const char* cEg = "cEg";
inline constexpr const char* cReset = "cReset";
inline constexpr const char* cResetOn = "cResetOn";
inline constexpr const char* cDir = "cDir";
inline constexpr const char* cDirOn = "cDirOn";
inline constexpr const char* cLength = "cLength";
inline constexpr const char* cLengthAmt = "cLengthAmt";
inline constexpr const char* cChance = "cChance";
inline constexpr const char* cChanceAmt = "cChanceAmt";
inline constexpr const char* cTrans = "cTrans";
inline constexpr const char* cTransAmt = "cTransAmt";
inline constexpr const char* cMidi = "cMidi";
inline constexpr const char* cNote = "cNote";
} // namespace dedal::ID
