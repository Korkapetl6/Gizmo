#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace solista
{
inline juce::String pl (const char* utf8) { return juce::String::fromUTF8 (utf8); }

struct Chord
{
    int rootPc = 0;
    bool minor = false;
    bool seventh = false; // dominant 7th on major chords, minor 7th on minor ones

    juce::String getName() const;
    bool contains (int pitch) const noexcept;
};

/** Vibrato of one note. On an unbent string it only raises the pitch (the finger pushes the string);
    on a bent string it dips below the target, as the bend is let down a little and pushed back. */
struct Vibrato
{
    float depth = 0.0f;  // semitones, peak to peak; 0 = no vibrato
    float rateHz = 5.5f;
    float delay = 0.0f;  // seconds after the note (or its bend) has settled
    float grow = 0.15f;  // seconds to reach the full depth
    float swell = 0.0f;  // how much wider it gets towards the end of the note (0..1)
    float drift = 0.0f;  // how much the rate changes over the note (-0.2..0.2)
};

enum class BendKind
{
    none,
    up,             // pick the note, push it up to the target
    upAndRelease,   // up, hold, and let it back down within the same note
    preBendRelease, // already bent when picked, then let down
    twoStage,       // up a half step, wait, then the rest of the way
    curl            // a slow quarter-tone smear with no real target
};

struct Articulation
{
    BendKind bend = BendKind::none;
    float bendSemis = 0.0f;    // size of the bend: target minus the fretted note
    float bendDelay = 0.0f;    // seconds before the bend starts
    float bendTime = 0.1f;     // seconds to reach the target
    float releaseAt = 0.7f;    // fraction of the note where a release starts
    float releaseTime = 0.15f; // seconds
    float slideIn = 0.0f;      // semitones the note starts away from its pitch (negative: from below)
    float slideTime = 0.06f;
    float fallOff = 0.0f;      // semitones the pitch drops at the very end (negative)
    Vibrato vibrato;
};

struct Note
{
    double start = 0.0, length = 0.0; // in beats
    int pitch = 60;                   // the fretted note
    int velocity = 96;
    bool legato = false;     // hammer-on, pull-off or slide: no pick attack
    bool chordNote = false;  // second note of a double stop: starts and bends with the note before it
    Articulation art;

    /** Pitch offset in semitones 'seconds' after the note started. */
    float pitchOffset (double seconds, double lengthSeconds) const noexcept;
};

struct Solo
{
    std::vector<Note> notes;   // sorted by start
    std::vector<Chord> chords; // one per bar
    std::array<int, 7> scale {}; // pitch classes of the seven-note scale, from the tonic up
    double lengthBeats = 32.0;
    double bpm = 120.0;
    int keyPc = 9;
    bool minorKey = true;

    double secondsPerBeat() const noexcept { return 60.0 / bpm; }
};

/** Everything a player needs, in time order: note-ons, note-offs and the pitch-bend curve. */
struct Event
{
    enum Type { bend = 0, noteOff = 1, noteOn = 2 };

    double beat = 0.0;
    Type type = bend;
    int pitch = 0;
    int velocity = 0;
    bool legato = false;
    float semitones = 0.0f;
};

std::vector<Event> renderEvents (const Solo& solo);

/** A second guitar a diatonic third above (steps = 2) or below (steps = -2), with its own bends,
    vibrato and timing, like a second player. */
Solo makeHarmony (const Solo& solo, int steps, juce::uint32 seed);

/** A standard MIDI file with notes and pitch bend, for an instrument whose bend range is +-bendRange. */
juce::MidiFile makeMidiFile (const Solo& solo, int bendRange, const juce::String& trackName);
} // namespace solista
