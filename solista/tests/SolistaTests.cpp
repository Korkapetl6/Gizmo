// Offline checks for Solista, plus a demo mode that renders solos to audio and MIDI files:
//   SolistaTests                 runs the checks
//   SolistaTests --demo <dir>    also writes demo WAVs, MIDI files and a picture of the editor

#include "SolistaEditor.h"
#include "SolistaProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>
#include <set>

using namespace solista;

namespace
{
int failures = 0, passes = 0;

void check (bool ok, const juce::String& what)
{
    if (ok)
    {
        ++passes;
    }
    else
    {
        ++failures;
        std::cout << "FAIL  " << what << std::endl;
    }
}

struct Head : public juce::AudioPlayHead
{
    double bpm = 120.0, seconds = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (seconds * bpm / 60.0);
        info.setIsPlaying (playing);
        return info;
    }
};

struct Audio
{
    std::vector<float> left, right;

    float peak() const
    {
        float p = 0.0f;

        for (size_t i = 0; i < left.size(); ++i)
            p = std::max (p, std::max (std::abs (left[i]), std::abs (right[i])));

        return p;
    }

    float rms (double fromSeconds, double toSeconds, double sr) const
    {
        const auto a = (size_t) (fromSeconds * sr), b = std::min (left.size(), (size_t) (toSeconds * sr));
        double sum = 0.0;

        for (auto i = a; i < b; ++i)
            sum += (double) left[i] * left[i];

        return b > a ? (float) std::sqrt (sum / (double) (b - a)) : 0.0f;
    }

    bool finite() const
    {
        for (size_t i = 0; i < left.size(); ++i)
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                return false;

        return true;
    }
};

void setParam (SolistaProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

struct MidiNote
{
    double start, length;
    int pitch;
    float bendTo; // semitones the wheel moves to, half way through the note
};

Audio render (SolistaProcessor& p, Head& head, double seconds, const std::vector<MidiNote>& midiNotes = {},
              double sr = 48000.0, int block = 256)
{
    p.setPlayHead (&head);
    head.seconds = 0.0;
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);

    {
        // One block, so the plugin learns the tempo, as it would from a running host.
        juce::AudioBuffer<float> warmUp (2, block);
        juce::MidiBuffer none;
        warmUp.clear();
        p.processBlock (warmUp, none);
        p.regenerateIfNeeded();
        p.prepareToPlay (sr, block);
    }

    Audio out;
    const auto total = (int) (seconds * sr);
    juce::AudioBuffer<float> buffer (2, block);
    const auto range = (float) kBendRanges[juce::roundToInt (p.apvts.getRawParameterValue (ParamID::bendRange)->load())];

    for (int done = 0; done < total; done += block)
    {
        juce::MidiBuffer midi;
        const auto t0 = (double) done / sr, t1 = (double) (done + block) / sr;

        for (const auto& n : midiNotes)
        {
            auto offsetOf = [&] (double t) { return juce::jlimit (0, block - 1, (int) ((t - t0) * sr)); };

            if (n.start >= t0 && n.start < t1)
                midi.addEvent (juce::MidiMessage::noteOn (1, n.pitch, 0.8f), offsetOf (n.start));

            const auto middle = n.start + n.length * 0.5, end = n.start + n.length;

            if (std::abs (n.bendTo) > 0.0f && middle >= t0 && middle < t1)
                midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + juce::roundToInt (n.bendTo / range * 8191.0f)), offsetOf (middle));

            if (end >= t0 && end < t1)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, n.pitch), offsetOf (end));
                midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192), offsetOf (end));
            }
        }

        head.seconds = t0;
        buffer.clear();
        p.processBlock (buffer, midi);
        out.left.insert (out.left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
        out.right.insert (out.right.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + block);
    }

    return out;
}

/** Fundamental frequency of a steady stretch of signal, from its autocorrelation. */
double pitchOf (const std::vector<float>& x, double fromSeconds, double sr, double expected)
{
    const auto start = (size_t) (fromSeconds * sr);
    const auto window = (size_t) (0.25 * sr);
    const auto centre = sr / expected;
    const auto lo = (int) (centre * 0.8), hi = (int) (centre * 1.25);
    std::vector<double> r ((size_t) hi + 2, 0.0);

    for (int lag = lo - 1; lag <= hi + 1; ++lag)
    {
        double sum = 0.0;

        for (size_t i = 0; i < window; ++i)
            sum += (double) x[start + i] * x[start + i + (size_t) lag];

        r[(size_t) lag] = sum;
    }

    int best = lo;

    for (int lag = lo; lag <= hi; ++lag)
        if (r[(size_t) lag] > r[(size_t) best])
            best = lag;

    const auto a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
    const auto shift = std::abs (a - 2.0 * b + c) > 1.0e-12 ? 0.5 * (a - c) / (a - 2.0 * b + c) : 0.0;
    return sr / ((double) best + shift);
}

double cents (double frequency, double reference)
{
    return 1200.0 * std::log2 (frequency / reference);
}

void writeWav (const juce::File& file, const Audio& audio, double sr)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());

    if (stream == nullptr)
        return;

    auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
        return;

    juce::AudioBuffer<float> buffer (2, (int) audio.left.size());
    buffer.copyFrom (0, 0, audio.left.data(), (int) audio.left.size());
    buffer.copyFrom (1, 0, audio.right.data(), (int) audio.right.size());
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

//==============================================================================
void checkSolo (const Solo& solo, const GeneratorParams& p, const juce::String& name, float& highestOffset, float& lowestOffset)
{
    check (solo.notes.size() >= (size_t) p.bars, name + ": has enough notes (" + juce::String ((int) solo.notes.size()) + ")");
    check ((int) solo.chords.size() == p.bars, name + ": one chord per bar");
    check (std::abs (solo.lengthBeats - p.bars * 4.0) < 1.0e-9, name + ": length");

    auto sorted = true, inRange = true, inTime = true, sane = true, monophonic = true;
    const Note* previous = nullptr;
    const auto spb = solo.secondsPerBeat();

    for (const auto& n : solo.notes)
    {
        if (previous != nullptr && n.start < previous->start - 1.0e-9)
            sorted = false;

        inRange = inRange && n.pitch >= 45 && n.pitch <= 96 && n.velocity >= 1 && n.velocity <= 127;
        inTime = inTime && n.start >= -1.0e-9 && n.length > 0.02 && n.start + n.length <= solo.lengthBeats + 1.0e-6;

        for (int i = 0; i <= 64; ++i)
        {
            const auto offset = n.pitchOffset (n.length * spb * i / 64.0, n.length * spb);
            sane = sane && std::isfinite (offset);
            highestOffset = std::max (highestOffset, offset);
            lowestOffset = std::min (lowestOffset, offset);
        }

        if (previous != nullptr && ! n.chordNote && n.start < previous->start + previous->length - 0.05)
            monophonic = false;

        if (! n.chordNote)
            previous = &n;
    }

    check (sorted, name + ": notes in time order");
    check (inRange, name + ": pitches and velocities in range");
    check (inTime, name + ": notes inside the solo");
    check (sane, name + ": pitch curves are finite");
    check (monophonic, name + ": one note at a time (except double stops)");

    // The same note hammered over and over is not a solo.
    int longestRepeat = 1, repeat = 1;

    for (size_t i = 1; i < solo.notes.size(); ++i)
    {
        repeat = solo.notes[i].pitch == solo.notes[i - 1].pitch && ! solo.notes[i].chordNote ? repeat + 1 : 1;
        longestRepeat = std::max (longestRepeat, repeat);
    }

    check (longestRepeat <= 6, name + ": no note repeated more than six times in a row (" + juce::String (longestRepeat) + ")");

    const auto events = renderEvents (solo);
    auto ordered = true;
    int on = 0, off = 0;

    for (size_t i = 0; i < events.size(); ++i)
    {
        if (i > 0 && events[i].beat < events[i - 1].beat - 1.0e-9)
            ordered = false;

        on += events[i].type == Event::noteOn ? 1 : 0;
        off += events[i].type == Event::noteOff ? 1 : 0;
    }

    check (ordered, name + ": events in time order");
    check (on == (int) solo.notes.size() && on == off, name + ": every note starts and stops");
    check (events.empty() || events.back().beat <= solo.lengthBeats + 1.0e-6, name + ": events end with the solo");
}

void testGenerator()
{
    auto highest = 0.0f, lowest = 0.0f;
    int totalNotes = 0, totalBends = 0, totalVibrato = 0, count = 0;
    std::set<int> vibratoRates;

    for (int style = 0; style < numStyles; ++style)
    {
        for (juce::uint32 seed = 1; seed <= 24; ++seed)
        {
            GeneratorParams p;
            p.style = style;
            p.seed = seed * 7919u;
            p.keyPc = (int) (seed * 5) % 12;
            p.minorKey = seed % 3 != 0;
            p.progression = (int) seed % kNumProgressions;
            p.bars = kBarChoices[seed % 4];
            p.shuffle = seed % 4 == 1;
            p.bpm = 70.0 + 9.0 * (double) (seed % 9);
            p.density = 0.1f + 0.2f * (float) (seed % 5);
            p.bends = 0.2f + 0.2f * (float) (seed % 4);
            p.vibrato = 0.25f * (float) (seed % 5);
            p.syncopation = 0.33f * (float) (seed % 4);
            p.space = 0.25f * (float) ((seed / 2) % 5);

            const auto name = getStyleNames()[style] + " #" + juce::String ((int) seed);
            const auto solo = generateSolo (p);
            checkSolo (solo, p, name, highest, lowest);

            const auto again = generateSolo (p);
            auto same = again.notes.size() == solo.notes.size();

            for (size_t i = 0; same && i < solo.notes.size(); ++i)
                same = again.notes[i].pitch == solo.notes[i].pitch && std::abs (again.notes[i].start - solo.notes[i].start) < 1.0e-12;

            check (same, name + ": the same seed gives the same solo");

            auto other = p;
            other.seed += 1;
            const auto different = generateSolo (other);
            auto differs = different.notes.size() != solo.notes.size();

            for (size_t i = 0; ! differs && i < solo.notes.size(); ++i)
                differs = different.notes[i].pitch != solo.notes[i].pitch || std::abs (different.notes[i].start - solo.notes[i].start) > 1.0e-6;

            check (differs, name + ": another seed gives another solo");

            for (const auto steps : { 2, -2 })
            {
                auto h = 0.0f, l = 0.0f;
                const auto second = makeHarmony (solo, steps, p.seed);
                check (second.notes.size() == solo.notes.size(), name + ": the second guitar plays every note");
                checkSolo (second, p, name + " (second guitar)", h, l);
                highest = std::max (highest, h);
                lowest = std::min (lowest, l);
            }

            for (const auto& n : solo.notes)
            {
                totalBends += n.art.bend != BendKind::none ? 1 : 0;

                if (n.art.vibrato.depth > 0.0f)
                {
                    ++totalVibrato;
                    vibratoRates.insert (juce::roundToInt (n.art.vibrato.rateHz * 20.0f) * 1000 + juce::roundToInt (n.art.vibrato.depth * 100.0f));
                }
            }

            totalNotes += (int) solo.notes.size();
            ++count;

            // The MIDI file
            for (const auto range : kBendRanges)
            {
                juce::MemoryOutputStream stream;
                makeMidiFile (solo, range, "test").writeTo (stream);
                juce::MemoryInputStream input (stream.getData(), stream.getDataSize(), false);
                juce::MidiFile file;
                const auto readable = file.readFrom (input);
                int ons = 0, wheel = 0;
                auto tempoOk = false, wheelOk = true;

                for (int t = 0; readable && t < file.getNumTracks(); ++t)
                {
                    for (const auto* holder : *file.getTrack (t))
                    {
                        const auto& m = holder->message;
                        ons += m.isNoteOn() ? 1 : 0;

                        if (m.isPitchWheel())
                        {
                            ++wheel;
                            wheelOk = wheelOk && m.getPitchWheelValue() >= 0 && m.getPitchWheelValue() <= 16383;
                        }

                        if (m.isTempoMetaEvent())
                            tempoOk = std::abs (60.0 / m.getTempoSecondsPerQuarterNote() - p.bpm) < 0.01;
                    }
                }

                check (readable && ons == (int) solo.notes.size() && tempoOk && wheelOk && wheel > 0,
                       name + ": MIDI file for a bend range of " + juce::String (range));
            }
        }
    }

    std::cout << count << " solos: " << totalNotes / count << " notes on average, " << juce::String (100.0 * totalBends / totalNotes, 1)
              << " % bent, " << juce::String (100.0 * totalVibrato / totalNotes, 1) << " % with vibrato, "
              << (int) vibratoRates.size() << " different vibratos; pitch offsets from " << lowest << " to " << highest
              << " semitones" << std::endl;

    check (highest <= 3.6f && lowest >= -3.6f, "bends stay within a minor third or so");
    check ((double) totalBends / totalNotes > 0.08, "bends are common");
    check ((int) vibratoRates.size() > totalVibrato / 3, "vibrato differs from note to note");
}

void testGuitar()
{
    SolistaProcessor p;
    Head head;
    head.playing = false;
    p.setFollowHost (false);
    setParam (p, ParamID::ambience, 0.0f);
    setParam (p, ParamID::drive, 0.3f);
    const auto sr = 48000.0;

    for (const auto pitch : { 52, 64, 76, 86 })
    {
        const auto reference = 440.0 * std::exp2 ((pitch - 69) / 12.0);
        const auto audio = render (p, head, 1.6, { { 0.05, 1.3, pitch, 0.0f } }, sr);
        const auto f = pitchOf (audio.left, 0.5, sr, reference);
        check (std::abs (cents (f, reference)) < 6.0, "guitar in tune at note " + juce::String (pitch) + " (" + juce::String (cents (f, reference), 1) + " cents)");
        check (audio.finite() && audio.peak() < 1.0f && audio.rms (0.2, 1.0, sr) > 0.01f,
               "guitar level at note " + juce::String (pitch) + " (peak " + juce::String (audio.peak(), 2) + ", rms " + juce::String (audio.rms (0.2, 1.0, sr), 3) + ")");
        check (audio.rms (1.52, 1.6, sr) < 0.25f * audio.rms (1.2, 1.3, sr), "note stops after note-off at " + juce::String (pitch));
    }

    // Bend up a whole step with the wheel
    const auto bent = render (p, head, 1.6, { { 0.05, 1.4, 64, 2.0f } }, sr);
    const auto target = 440.0 * std::exp2 ((66 - 69) / 12.0);
    check (std::abs (cents (pitchOf (bent.left, 1.05, sr, target), target)) < 8.0, "a whole-step bend lands on the note");

    // Silence without notes
    const auto silent = render (p, head, 0.5, {}, sr);
    check (silent.peak() < 1.0e-4f, "silent without notes");
}

void testPlayback()
{
    SolistaProcessor p;
    p.setSeed (4242);
    Head head;
    head.bpm = 110.0;
    setParam (p, ParamID::bars, 0.0f); // 4 bars
    const auto sr = 48000.0;
    const auto loop = 16.0 * 60.0 / head.bpm;

    const auto audio = render (p, head, loop * 2.0, {}, sr);
    check (std::abs (p.getBundle()->params.bpm - 110.0) < 0.01, "the solo follows the host tempo");
    check (audio.finite() && audio.peak() > 0.05f && audio.peak() < 1.0f, "solo plays with the host (peak " + juce::String (audio.peak(), 2) + ")");

    // The second pass through the loop sounds like the first
    const auto first = audio.rms (0.0, loop, sr), second = audio.rms (loop, loop * 2.0, sr);
    check (std::abs (first - second) < 0.35f * first, "the loop repeats");

    p.setFollowHost (false);
    check (render (p, head, 2.0, {}, sr).peak() < 1.0e-4f, "quiet when not following the host");

    head.playing = false;
    p.setAudition (true);
    check (render (p, head, 3.0, {}, sr).peak() > 0.05f, "audition plays while the host is stopped");
    p.setAudition (false);
    check (render (p, head, 1.0, {}, sr).rms (0.6, 1.0, sr) < 0.01f, "audition stops");

    // Second guitar
    head.playing = true;
    p.setFollowHost (true);
    setParam (p, ParamID::harmony, 1.0f);
    p.regenerateIfNeeded();
    check (p.getBundle()->harmony == 1 && ! p.getBundle()->secondEvents.empty(), "second guitar is composed");
    const auto two = render (p, head, loop, {}, sr);
    check (two.finite() && two.peak() < 1.0f && two.peak() > 0.05f, "two guitars play without clipping (peak " + juce::String (two.peak(), 2) + ")");

    // State
    juce::MemoryBlock state;
    p.getStateInformation (state);
    SolistaProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    render (q, head, 0.1, {}, sr);
    check (q.getSeed() == 4242 && q.getBundle()->solo.notes.size() == p.getBundle()->solo.notes.size()
               && q.getBundle()->harmony == 1 && q.getBundle()->params.bars == 4,
           "state round trip (seed " + juce::String ((int) q.getSeed()) + ", notes " + juce::String ((int) q.getBundle()->solo.notes.size())
               + " vs " + juce::String ((int) p.getBundle()->solo.notes.size()) + ", harmony " + juce::String (q.getBundle()->harmony)
               + ", bars " + juce::String (q.getBundle()->params.bars) + ")");

    const auto before = p.getSeed();
    p.newSolo();
    check (p.getSeed() != before && p.hasPreviousSolo(), "new solo");
    p.previousSolo();
    check (p.getSeed() == before, "previous solo");

    const auto file = p.writeMidiFile (false);
    check (file.existsAsFile() && file.getSize() > 200, "MIDI file written for dragging");
    check (p.writeMidiFile (true).existsAsFile(), "MIDI file for the second guitar");
}

//==============================================================================
// A plain backing band for the demos, so the solo can be heard against its chords.
void addBacking (Audio& audio, const Solo& solo, bool shuffle, double sr, float level)
{
    const auto spb = solo.secondsPerBeat();
    const auto total = (int) audio.left.size();
    juce::Random random (7);

    auto add = [&] (int index, float l, float r)
    {
        if (index >= 0 && index < total)
        {
            audio.left[(size_t) index] += l * level;
            audio.right[(size_t) index] += r * level;
        }
    };

    for (int bar = 0; bar < (int) solo.chords.size(); ++bar)
    {
        const auto& chord = solo.chords[(size_t) bar];
        const auto barStart = bar * 4.0 * spb;
        const auto third = chord.minor ? 3 : 4;
        const int tones[4] = { 48 + chord.rootPc, 48 + chord.rootPc + third, 48 + chord.rootPc + 7,
                               48 + chord.rootPc + (chord.seventh ? 10 : 12) };

        // Pad: soft, slow attack
        const auto padLength = (int) (4.0 * spb * sr);

        for (int i = 0; i < padLength; ++i)
        {
            const auto t = (double) i / sr;
            const auto envelope = (float) (std::min (1.0, t / 0.08) * std::min (1.0, (padLength - i) / (0.1 * sr)));
            float l = 0.0f, r = 0.0f;

            for (int k = 0; k < 4; ++k)
            {
                const auto f = 440.0 * std::exp2 ((tones[k] - 69) / 12.0);
                const auto phase = juce::MathConstants<double>::twoPi * f * t;
                const auto v = (float) (std::sin (phase) + 0.35 * std::sin (2.0 * phase + 0.4) + 0.12 * std::sin (3.0 * phase));
                (k % 2 == 0 ? l : r) += v * 0.07f;
                (k % 2 == 0 ? r : l) += v * 0.04f;
            }

            add ((int) (barStart * sr) + i, l * envelope, r * envelope);
        }

        for (int eighth = 0; eighth < 8; ++eighth)
        {
            const auto swing = shuffle && eighth % 2 == 1 ? spb / 6.0 : 0.0;
            const auto start = (int) ((barStart + eighth * 0.5 * spb + swing) * sr);

            // Bass on the root, with the fifth as a pickup
            if (eighth == 0 || eighth == 3 || eighth == 4 || eighth == 7)
            {
                const auto note = 36 + chord.rootPc + (eighth == 7 ? 7 : 0) - (chord.rootPc > 7 ? 12 : 0);
                const auto f = 440.0 * std::exp2 ((note - 69) / 12.0);
                const auto length = (int) (0.42 * spb * sr * (eighth == 0 || eighth == 4 ? 1.8 : 1.0));

                for (int i = 0; i < length; ++i)
                {
                    const auto t = (double) i / sr;
                    const auto envelope = std::min (1.0, t / 0.004) * std::exp (-t * 2.5) * std::min (1.0, (length - i) / (0.01 * sr));
                    const auto phase = juce::MathConstants<double>::twoPi * f * t;
                    const auto v = (float) ((std::sin (phase) + 0.4 * std::sin (2.0 * phase) + 0.15 * std::sin (3.0 * phase)) * envelope * 0.3);
                    add (start + i, v, v);
                }
            }

            // Kick on one and three, snare on two and four, hat on every eighth
            if (eighth == 0 || eighth == 4)
            {
                for (int i = 0; i < (int) (0.16 * sr); ++i)
                {
                    const auto t = (double) i / sr;
                    const auto v = (float) (std::sin (juce::MathConstants<double>::twoPi * (48.0 * t + 14.0 * (1.0 - std::exp (-t * 38.0)))) * std::exp (-t * 22.0) * 0.5);
                    add (start + i, v, v);
                }
            }

            if (eighth == 2 || eighth == 6)
            {
                float lp = 0.0f;

                for (int i = 0; i < (int) (0.14 * sr); ++i)
                {
                    const auto t = (double) i / sr;
                    const auto noise = random.nextFloat() * 2.0f - 1.0f;
                    lp += 0.45f * (noise - lp);
                    const auto v = (float) ((lp * 0.7 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 190.0 * t)) * std::exp (-t * 30.0) * 0.28);
                    add (start + i, v, v);
                }
            }

            float previous = 0.0f;

            for (int i = 0; i < (int) (0.03 * sr); ++i)
            {
                const auto noise = random.nextFloat() * 2.0f - 1.0f;
                const auto v = (noise - previous) * (float) std::exp (-(double) i / sr * 140.0) * (eighth % 2 == 0 ? 0.07f : 0.045f);
                previous = noise;
                add (start + i, v * 0.8f, v);
            }
        }
    }
}

struct Demo
{
    const char* file;
    int style, keyPc;
    bool minorKey;
    int progression, barsIndex;
    bool shuffle;
    double bpm;
    juce::uint32 seed;
    int harmony;
};

void renderDemos (const juce::File& outDir, juce::uint32 seedOffset)
{
    outDir.createDirectory();
    const auto sr = 48000.0;

    const Demo demos[] = {
        { "1_epicki_a-moll", styleEpic, 9, true, 0, 2, false, 84.0, 11, 0 },
        { "2_spiewny_d-dur", styleSinging, 2, false, 0, 1, false, 76.0, 23, 0 },
        { "3_groove_e-moll", styleGroove, 4, true, 3, 1, false, 104.0, 35, 0 },
        { "4_swing_a-dur", styleSwing, 9, false, 0, 1, true, 126.0, 47, 0 },
        { "5_blues_a-moll", styleBlues, 9, true, 4, 2, true, 78.0, 59, 0 },
        { "6_dwie_gitary_d-moll", styleSinging, 2, true, 1, 1, false, 88.0, 71, 1 },
    };

    for (const auto& d : demos)
    {
        SolistaProcessor p;
        Head head;
        head.bpm = d.bpm;
        setParam (p, ParamID::style, (float) d.style);
        setParam (p, ParamID::key, (float) d.keyPc);
        setParam (p, ParamID::mode, d.minorKey ? 0.0f : 1.0f);
        setParam (p, ParamID::progression, (float) d.progression);
        setParam (p, ParamID::bars, (float) d.barsIndex);
        setParam (p, ParamID::feel, d.shuffle ? 1.0f : 0.0f);
        setParam (p, ParamID::harmony, (float) d.harmony);
        p.setSeed (d.seed + seedOffset);

        const auto bars = kBarChoices[d.barsIndex];
        const auto seconds = bars * 4.0 * 60.0 / d.bpm;
        auto audio = render (p, head, seconds - 0.02, {}, sr);

        if (d.style == styleEpic)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            editor->setSize (SolistaEditor::kDesignWidth, SolistaEditor::kDesignHeight);

            if (auto* e = dynamic_cast<SolistaEditor*> (editor.get()))
                e->updateDisplay();

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            const auto file = outDir.getChildFile ("editor.png");
            file.deleteFile();

            if (auto stream = file.createOutputStream())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (image, *stream);
            }
        }

        // Let the last note ring out
        head.playing = false;
        p.setFollowHost (false);
        juce::AudioBuffer<float> tail (2, 256);

        for (int i = 0; i < (int) (2.5 * sr / 256); ++i)
        {
            juce::MidiBuffer midi;
            tail.clear();
            p.processBlock (tail, midi);
            audio.left.insert (audio.left.end(), tail.getReadPointer (0), tail.getReadPointer (0) + 256);
            audio.right.insert (audio.right.end(), tail.getReadPointer (1), tail.getReadPointer (1) + 256);
        }

        const auto bundle = p.getBundle();
        auto solo = audio;
        addBacking (audio, bundle->solo, d.shuffle, sr, 0.8f);

        for (auto* channel : { &audio.left, &audio.right })
            for (auto& sample : *channel)
                sample *= 0.8f;

        writeWav (outDir.getChildFile (juce::String (d.file) + ".wav"), audio, sr);
        writeWav (outDir.getChildFile (juce::String (d.file) + "_sama_gitara.wav"), solo, sr);
        p.writeMidiFile (false).copyFileTo (outDir.getChildFile (juce::String (d.file) + ".mid"));

        std::cout << d.file << ": " << bundle->solo.notes.size() << " notes, peak " << solo.peak() << std::endl;

    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    testGenerator();
    testGuitar();
    testPlayback();

    std::cout << passes << " checks passed, " << failures << " failed" << std::endl;

    if (argc >= 3 && juce::String (argv[1]) == "--demo")
        renderDemos (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                     argc >= 4 ? (juce::uint32) juce::String (argv[3]).getIntValue() : 0u);

    return failures == 0 ? 0 : 1;
}
