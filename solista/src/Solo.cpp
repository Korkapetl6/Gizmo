#include "Solo.h"

#include <algorithm>
#include <cmath>

namespace solista
{
namespace
{
constexpr double pi = 3.14159265358979323846;

inline float smooth (double u) noexcept
{
    u = juce::jlimit (0.0, 1.0, u);
    return (float) (0.5 - 0.5 * std::cos (pi * u));
}

/** A quick bend: the string moves fast at first and eases into the target. */
inline float snap (double u) noexcept
{
    u = juce::jlimit (0.0, 1.0, u);
    return (float) (1.0 - std::pow (1.0 - u, 2.2));
}

inline float bendShape (double u, float bendTime) noexcept
{
    return bendTime < 0.11f ? snap (u) : smooth (u);
}

inline int pitchClass (int pitch) noexcept { return ((pitch % 12) + 12) % 12; }
} // namespace

juce::String Chord::getName() const
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (names[pitchClass (rootPc)]) + (minor ? "m" : "") + (seventh ? "7" : "");
}

bool Chord::contains (int pitch) const noexcept
{
    const auto interval = pitchClass (pitch - rootPc);
    return interval == 0 || interval == (minor ? 3 : 4) || interval == 7 || (seventh && interval == 10);
}

float Note::pitchOffset (double t, double len) const noexcept
{
    const auto& a = art;
    float value = 0.0f;
    double settled = 0.0;    // when the pitch has stopped moving and a vibrato may start
    double vibratoEnd = len; // a vibrato stops when a release begins
    bool bentHold = false;

    switch (a.bend)
    {
        case BendKind::none:
            break;

        case BendKind::up:
            value = a.bendSemis * bendShape ((t - a.bendDelay) / a.bendTime, a.bendTime);
            settled = a.bendDelay + a.bendTime;
            bentHold = true;
            break;

        case BendKind::upAndRelease:
        {
            const auto top = (double) a.bendDelay + a.bendTime;
            const auto releaseStart = std::max (top + 0.03, (double) a.releaseAt * len);
            value = a.bendSemis * bendShape ((t - a.bendDelay) / a.bendTime, a.bendTime)
                  * (1.0f - smooth ((t - releaseStart) / a.releaseTime));
            settled = top;
            vibratoEnd = releaseStart;
            bentHold = true;
            break;
        }

        case BendKind::preBendRelease:
        {
            const auto releaseStart = std::max (0.03, (double) a.releaseAt * len);
            value = a.bendSemis * (1.0f - smooth ((t - releaseStart) / a.releaseTime));
            settled = releaseStart + a.releaseTime; // any vibrato comes once the string is back down
            break;
        }

        case BendKind::twoStage:
        {
            const auto first = a.bendSemis * 0.5f;
            const auto secondStart = (double) a.bendDelay + a.bendTime + std::max (0.12, 0.22 * len);
            const auto secondTime = (double) a.bendTime * 1.3;
            value = first * bendShape ((t - a.bendDelay) / a.bendTime, a.bendTime)
                  + (a.bendSemis - first) * smooth ((t - secondStart) / secondTime);
            settled = secondStart + secondTime;
            bentHold = true;
            break;
        }

        case BendKind::curl:
            value = a.bendSemis * smooth ((t - a.bendDelay) / std::max (0.05f, a.bendTime));
            settled = len; // a curl has no vibrato
            break;
    }

    if (a.slideIn != 0.0f && t < a.slideTime)
        value += a.slideIn * (1.0f - smooth (t / a.slideTime));

    if (a.fallOff != 0.0f)
    {
        const auto fallTime = juce::jlimit (0.06, 0.22, len * 0.35);

        if (t > len - fallTime)
            value += a.fallOff * (float) std::pow (std::min (1.2, (t - (len - fallTime)) / fallTime), 1.6);
    }

    const auto& v = a.vibrato;

    if (v.depth > 0.0f)
    {
        const auto begin = settled + v.delay;

        if (t > begin && begin < vibratoEnd)
        {
            const auto u = t - begin;
            const auto x = std::min (1.0, u / std::max (0.1, vibratoEnd - begin));
            auto envelope = (float) std::min (1.0, u / std::max (0.01f, v.grow)) * (1.0f + v.swell * (float) x);

            if (t > vibratoEnd)
                envelope *= (float) std::max (0.0, 1.0 - (t - vibratoEnd) / 0.06);

            const auto phase = 2.0 * pi * v.rateHz * u * (1.0 + 0.5 * v.drift * x);
            const auto wave = (float) (0.5 - 0.5 * std::cos (phase));
            value += (bentHold ? -1.0f : 1.0f) * v.depth * envelope * wave;
        }
    }

    return value;
}

std::vector<Event> renderEvents (const Solo& solo)
{
    std::vector<Event> events;
    const auto& notes = solo.notes;
    const auto spb = solo.secondsPerBeat();
    constexpr double stepSeconds = 0.006;
    const auto overlap = 0.012 / spb; // legato notes overlap by 12 ms
    float lastBend = 0.0f;
    bool anyBend = false;

    auto addBend = [&] (double beat, float semitones, bool force)
    {
        if (force ? (anyBend && std::abs (semitones - lastBend) < 1.0e-4f) : std::abs (semitones - lastBend) < 0.004f)
            return;

        Event e;
        e.beat = beat;
        e.type = Event::bend;
        e.semitones = semitones;
        events.push_back (e);
        lastBend = semitones;
        anyBend = true;
    };

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const Note* next = nullptr;

        for (size_t j = i + 1; j < notes.size(); ++j)
        {
            if (! notes[j].chordNote)
            {
                next = &notes[j];
                break;
            }
        }

        auto offBeat = n.start + n.length;

        if (next != nullptr)
        {
            if (next->legato && next->start - offBeat < 0.06)
                offBeat = next->start + overlap; // hammer-ons and pull-offs run into each other
            else
                offBeat = std::min (offBeat, next->start);
        }

        Event on;
        on.beat = n.start;
        on.type = Event::noteOn;
        on.pitch = n.pitch;
        on.velocity = n.velocity;
        on.legato = n.legato;

        Event off;
        off.beat = std::max (offBeat, n.start + 0.01);
        off.type = Event::noteOff;
        off.pitch = n.pitch;

        if (n.chordNote)
        {
            events.push_back (on);
            events.push_back (off);
            continue;
        }

        const auto lengthSeconds = n.length * spb;
        const auto curveEnd = next != nullptr ? std::min (off.beat, next->start) : off.beat;

        addBend (n.start, n.pitchOffset (0.0, lengthSeconds), true);
        events.push_back (on);

        for (double s = stepSeconds; s < (curveEnd - n.start) * spb; s += stepSeconds)
            addBend (n.start + s / spb, n.pitchOffset (s, lengthSeconds), false);

        events.push_back (off);
    }

    if (! notes.empty())
        addBend (std::min (solo.lengthBeats - 0.001, notes.back().start + notes.back().length + 0.25), 0.0f, true);

    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
    {
        return a.beat < b.beat || (a.beat == b.beat && a.type < b.type);
    });

    return events;
}

Solo makeHarmony (const Solo& solo, int steps, juce::uint32 seed)
{
    Solo harmony (solo);
    juce::Random random ((juce::int64) seed * 7919 + 17);
    const auto spb = solo.secondsPerBeat();

    // The scale tone 'steps' degrees away from a pitch. Pitches outside the scale keep their offset.
    auto shifted = [&solo, steps] (int pitch)
    {
        const auto pc = pitchClass (pitch - solo.keyPc);
        int degree = 0, offset = 0;

        for (int d = 0; d < 7; ++d)
            if (solo.scale[(size_t) d] <= pc)
            {
                degree = d;
                offset = pc - solo.scale[(size_t) d];
            }

        const auto target = degree + steps;
        const auto octave = (int) std::floor ((double) target / 7.0);
        const auto wrapped = ((target % 7) + 7) % 7;
        return pitch - pc + solo.scale[(size_t) wrapped] + 12 * octave + offset;
    };

    for (auto& n : harmony.notes)
    {
        const auto original = n.pitch;
        n.pitch = shifted (original);
        auto& a = n.art;

        if (a.bend != BendKind::none && a.bend != BendKind::curl)
        {
            const auto target = original + juce::roundToInt (a.bendSemis);
            const auto bent = (float) (shifted (target) - n.pitch);
            a.bendSemis = bent >= 1.0f && bent <= 2.0f ? bent : a.bendSemis;
        }

        // A second player never lands exactly together with the first.
        n.start = std::max (0.0, n.start + (random.nextDouble() - 0.5) * 0.016 / spb);
        n.velocity = juce::jlimit (1, 127, n.velocity - 6 + random.nextInt (5));
        a.bendTime *= 0.9f + 0.25f * random.nextFloat();
        a.vibrato.rateHz *= 0.94f + 0.12f * random.nextFloat();
        a.vibrato.depth *= 0.8f + 0.35f * random.nextFloat();
        a.vibrato.delay += 0.04f * random.nextFloat();
    }

    // Keep the notes in order and one at a time, with double-stop partners locked to their note.
    Note* previous = nullptr;

    for (auto& n : harmony.notes)
    {
        if (n.chordNote && previous != nullptr)
        {
            n.start = previous->start;
            n.length = previous->length;
            continue;
        }

        if (previous != nullptr)
        {
            n.start = std::max (n.start, previous->start + 0.02);
            previous->length = std::min (previous->length, std::max (0.02, n.start - previous->start));
        }

        n.length = std::max (0.02, std::min (n.length, harmony.lengthBeats - n.start));
        previous = &n;
    }

    for (size_t i = 1; i < harmony.notes.size(); ++i)
        if (harmony.notes[i].chordNote)
            harmony.notes[i].length = harmony.notes[i - 1].length;

    return harmony;
}

juce::MidiFile makeMidiFile (const Solo& solo, int bendRange, const juce::String& trackName)
{
    constexpr int ppq = 960;
    juce::MidiMessageSequence track;

    track.addEvent (juce::MidiMessage::textMetaEvent (3, trackName), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / solo.bpm)), 0.0);

    for (const auto& e : renderEvents (solo))
    {
        const auto tick = std::round (e.beat * ppq);

        if (e.type == Event::noteOn)
        {
            track.addEvent (juce::MidiMessage::noteOn (1, e.pitch, (juce::uint8) juce::jlimit (1, 127, e.velocity)), tick);
        }
        else if (e.type == Event::noteOff)
        {
            track.addEvent (juce::MidiMessage::noteOff (1, e.pitch), tick);
        }
        else
        {
            const auto value = 8192 + juce::roundToInt (e.semitones / (float) bendRange * 8191.0f);
            track.addEvent (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, value)), tick);
        }
    }

    track.addEvent (juce::MidiMessage::endOfTrack(), std::round (solo.lengthBeats * ppq));
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    file.addTrack (track);
    return file;
}
} // namespace solista
