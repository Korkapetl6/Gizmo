// Offline checks for Dedal, plus a demo mode:
//   DedalTests                 runs the checks
//   DedalTests --demo <dir>    also renders every factory preset to WAV and MIDI, and pictures of both pages

#include "DedalEditor.h"
#include "DedalParams.h"
#include "DedalProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>
#include <map>
#include <set>

using namespace dedal;

namespace
{
int failures = 0, passes = 0;

void check (bool ok, const juce::String& what)
{
    if (ok)
        ++passes;
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
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};

void setParam (DedalProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

struct Render
{
    std::vector<float> audio;
    std::vector<std::pair<double, juce::MidiMessage>> midi; // seconds, message
    float peak() const
    {
        float m = 0.0f;
        for (auto v : audio) m = std::max (m, std::abs (v));
        return m;
    }
    bool finite() const
    {
        for (auto v : audio) if (! std::isfinite (v)) return false;
        return true;
    }
    int noteOns() const
    {
        int n = 0;
        for (auto& m : midi) n += m.second.isNoteOn() ? 1 : 0;
        return n;
    }
};

Render render (DedalProcessor& p, Head& head, double seconds, bool prepare = true, double sr = 48000.0, int block = 256)
{
    p.setPlayHead (&head);

    if (prepare)
    {
        head.seconds = 0.0;
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
    }

    Render out;
    juce::AudioBuffer<float> buffer (2, block);
    const auto total = (int) (seconds * sr);

    for (int done = 0; done < total; done += block)
    {
        juce::MidiBuffer midi;
        buffer.clear();
        p.processBlock (buffer, midi);

        for (const auto meta : midi)
            out.midi.push_back ({ head.seconds + meta.samplePosition / sr, meta.getMessage() });

        out.audio.insert (out.audio.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
        head.seconds += block / sr;
    }

    return out;
}

void writeWav (const juce::File& file, const std::vector<float>& audio, double sr)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));

    if (writer == nullptr)
        return;

    juce::AudioBuffer<float> buffer (1, (int) audio.size());
    buffer.copyFrom (0, 0, audio.data(), (int) audio.size());
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

//==============================================================================
void testScalesAndTiming()
{
    check (quantize (2, 1.4f) == 2.0f && quantize (2, 5.6f) == 5.0f && quantize (2, -1.2f) == -1.0f, "major scale snaps");
    check (quantize (3, 5.6f) == 7.0f && quantize (3, 13.2f) == 14.0f, "pentatonic snaps");
    check (quantize (0, 3.3f) == 3.3f, "no quantizing passes through");
    check (quantize (14, 22.0f) == 24.0f || quantize (14, 22.0f) == 19.0f, "hang drum repeats every two octaves");
    check (getScales().size() == 16, "sixteen scales");

    Timing straight { 0.25, 0.0f }, swung { 0.25, 1.0f };
    check (std::abs (straight.startOf (3) - 0.75) < 1.0e-12, "straight steps");
    check (std::abs (swung.startOf (1) - 0.375) < 1.0e-12 && std::abs (swung.startOf (2) - 0.5) < 1.0e-12, "swing pushes the off-beat");
    check (swung.firstAtOrAfter (0.4) == 2 && swung.firstAtOrAfter (0.375) == 1 && straight.firstAtOrAfter (-0.1) == 0, "finding the next step");
}

void testSequencers()
{
    Sequencers s;
    Layout layout;
    s.randomise (0, 1.0f);
    const auto before = s.state;

    for (int i = 0; i < 64; ++i)
        s.step (layout, 0, 0.0f, forward, 1.0f);

    check (s.state.bits == before.bits && s.state.values == before.values, "CORRUPT at zero keeps the loop");

    int heads[8] {};
    s.restart();

    for (int i = 0; i < 8; ++i)
        heads[i] = s.step (layout, 0, 0.0f, backward, 1.0f).head;

    check (heads[0] == 0 && heads[1] == 7 && heads[2] == 6, "backward");

    s.restart();
    juce::String path;

    for (int i = 0; i < 16; ++i)
        path << s.step (layout, 0, 0.0f, pendulum, 1.0f).head;

    check (path.startsWith ("0123456765432101"), "pendulum: " + path);

    // Corruption
    auto c = s;
    int changedValues = 0, flips = 0;

    for (int i = 0; i < 800; ++i)
    {
        const auto old = c.state;
        c.step (layout, 0, 0.4f, forward, 1.0f);
        flips += old.bits != c.state.bits ? 1 : 0;
        changedValues += old.values != c.state.values ? 1 : 0;
    }

    check (flips == 0 && changedValues > 100, "below noon CORRUPT changes notes only");
    flips = 0;

    for (int i = 0; i < 800; ++i)
    {
        const auto old = c.state;
        c.step (layout, 0, 1.0f, forward, 1.0f);
        flips += old.bits != c.state.bits ? 1 : 0;
    }

    check (flips > 250 && flips < 550, "at full CORRUPT about half the steps flip (" + juce::String (flips) + ")");

    // BIT SHIFT and back
    auto r = s;
    const auto original = r.state.bits;
    r.bitShift (layout, 0);
    check (r.state.bits[1] == original[0] && r.state.bits[0] == original[7], "BIT SHIFT rotates right");
    r.bitShift (layout, 0);
    r.unshift (layout, 0);
    check (r.state.bits == original, "BIT SHIFT undone");

    // Chain: SEQ2 plays SEQ1's sequence one or more steps ahead
    Layout chained;
    chained.chain = true;
    chained.length[0] = 4;
    chained.length[1] = 4;
    Sequencers ch;
    ch.randomise (0, 1.0f);
    ch.randomise (1, 1.0f);
    ch.restart();
    ch.bitShift (chained, 1);
    ch.bitShift (chained, 1);
    auto a = ch.step (chained, 0, 0.0f, forward, 1.0f);
    auto b = ch.step (chained, 1, 0.0f, forward, 1.0f);
    check (a.head == 0 && b.head == 2, "chained SEQ2 runs two steps ahead");
    check (chained.slot (1, 5) == kSteps + 1 && chained.slot (0, 3) == 3, "chain runs through both buffers");

    // Chance
    Sequencers ch2 = s;
    ch2.randomise (0, 1.0f);
    int played = 0;

    for (int i = 0; i < 1000; ++i)
        played += ch2.step (layout, 0, 0.0f, forward, 0.3f).on ? 1 : 0;

    check (played > 230 && played < 370, "chance 30 % plays about 30 % (" + juce::String (played) + ")");

    // Saving
    auto t = toValueTree (c.state, "SEQ");
    SeqState back;
    fromValueTree (t, back);
    check (back.bits == c.state.bits && back.bitsC == c.state.bitsC && std::abs (back.values[3] - c.state.values[3]) < 1.0e-4f, "sequences survive saving");
}

void testWaveBanks()
{
    const auto& banks = WaveBanks::get();
    auto finite = true;
    float peak = 0.0f;

    for (int bank = 0; bank < WaveBanks::kBanks; ++bank)
        for (int level = 0; level < WaveBanks::kLevels; ++level)
            for (int i = 0; i < 64; ++i)
            {
                const auto v = banks.sample (bank, (float) i / 63.0f, (double) i / 64.0, level);
                finite = finite && std::isfinite (v);
                peak = std::max (peak, std::abs (v));
            }

    check (finite && peak > 0.5f && peak < 1.6f, "Beads banks are loaded (peak " + juce::String (peak, 2) + ")");

    // The top octave keeps only the fundamental: a saw-like wave turns into a sine.
    double error = 0.0;
    for (int i = 0; i < 64; ++i)
    {
        const auto phase = (double) i / 64.0;
        const auto v = banks.sample (3, 0.5f, phase, WaveBanks::kLevels - 1);
        const auto a = banks.sample (3, 0.5f, 0.25, WaveBanks::kLevels - 1), b = banks.sample (3, 0.5f, 0.0, WaveBanks::kLevels - 1);
        const auto ideal = b * std::cos (2.0 * juce::MathConstants<double>::pi * phase) + a * std::sin (2.0 * juce::MathConstants<double>::pi * phase);
        error = std::max (error, std::abs (v - ideal));
    }

    check (error < 0.02, "band-limited octaves (" + juce::String (error, 4) + ")");
}

void testProcessor()
{
    const auto sr = 48000.0;
    DedalProcessor p;
    Head head;
    head.bpm = 120.0;

    // All the factory presets play and stay in range.
    for (int i = 0; i < p.getFactoryPresetNames().size(); ++i)
    {
        p.loadFactoryPreset (i);
        const auto r = render (p, head, 4.0);
        check (r.finite() && r.peak() > 0.02f && r.peak() <= 1.0f, "preset " + p.getFactoryPresetNames()[i] + " (peak " + juce::String (r.peak(), 2) + ")");
        check (r.noteOns() > 4, "preset " + p.getFactoryPresetNames()[i] + " sends MIDI (" + juce::String (r.noteOns()) + " notes)");
    }

    // Timing: SEQ1 at 1/16 plays on the sixteenth grid
    p.loadFactoryPreset (0);
    setParam (p, ID::rate[0], 7);
    setParam (p, ID::midiSeq, 0);
    setParam (p, ID::trigMix, 0.0f);
    const auto r = render (p, head, 2.0);
    auto onGrid = true;
    for (const auto& [t, m] : r.midi)
        if (m.isNoteOn())
        {
            const auto sixteenths = t / (60.0 / head.bpm / 4.0);
            onGrid = onGrid && std::abs (sixteenths - std::round (sixteenths)) < 0.01;
        }
    check (onGrid && r.noteOns() > 8, "notes fall on the sixteenth grid");

    // Swing
    setParam (p, ID::swing[0], 1.0f);
    const auto swung = render (p, head, 2.0);
    auto lateOffbeats = 0;
    for (const auto& [t, m] : swung.midi)
        if (m.isNoteOn())
        {
            const auto eighths = t / (60.0 / head.bpm / 2.0);
            const auto frac = eighths - std::floor (eighths);
            lateOffbeats += std::abs (frac - 0.75) < 0.02 ? 1 : 0;
        }
    check (lateOffbeats > 3, "swing moves the second sixteenth to the triplet-ish spot (" + juce::String (lateOffbeats) + ")");
    setParam (p, ID::swing[0], 0.0f);

    // Polyrhythm 7 / 5 / 11: heads come back together after 385 steps
    p.loadFactoryPreset (8);
    head.bpm = 240.0;
    render (p, head, 0.05);
    const auto steps = 385;
    render (p, head, steps * 60.0 / 240.0 / 4.0 - 0.05, false);
    const auto snap = p.getSnapshot();
    check (snap.head[0] == 0 && snap.head[1] == 0 && snap.head[2] == 0, "7, 5 and 11 meet again on step 385 ("
               + juce::String (snap.head[0]) + "," + juce::String (snap.head[1]) + "," + juce::String (snap.head[2]) + ")");
    head.bpm = 120.0;

    // SEQ C resets SEQ1 at the start of each of its cycles
    p.loadFactoryPreset (0);
    setParam (p, ID::length[0], 8);
    setParam (p, ID::rate[0], 7);
    setParam (p, ID::rateC, 7);
    setParam (p, ID::lengthC, 3);
    setParam (p, ID::cReset, 1);
    setParam (p, ID::cResetOn, 0);
    render (p, head, 0.05);
    auto maxHead = 0;
    for (int i = 0; i < 40; ++i)
    {
        render (p, head, 60.0 / 120.0 / 4.0, false);
        maxHead = std::max (maxHead, p.getSnapshot().head[0]);
    }
    check (maxHead <= 2, "SEQ C reset keeps SEQ1 inside three steps (" + juce::String (maxHead) + ")");

    // SEQ C transposes
    p.loadFactoryPreset (0);
    setParam (p, ID::cTrans, 3);
    setParam (p, ID::cTransAmt, 12.0f);
    setParam (p, ID::scale, 1);
    const auto plain = render (p, head, 4.0);
    std::set<int> pitches;
    for (const auto& [t, m] : plain.midi)
        if (m.isNoteOn())
            pitches.insert (m.getNoteNumber());
    check (pitches.size() > 6, "transposition spreads the notes (" + juce::String ((int) pitches.size()) + " pitches)");

    // MIDI clip
    p.loadFactoryPreset (1);
    render (p, head, 9.0);
    p.collectCapture();
    auto fromHistory = false;
    const auto clip = p.makeClip (4, fromHistory);
    int clipNotes = 0;
    for (int t = 0; t < clip.getNumTracks(); ++t)
        for (const auto* e : *clip.getTrack (t))
            clipNotes += e->message.isNoteOn() ? 1 : 0;
    check (fromHistory && clipNotes > 8, "clip from the last four bars (" + juce::String (clipNotes) + " notes)");

    DedalProcessor fresh;
    const auto generated = fresh.makeClip (2, fromHistory);
    clipNotes = 0;
    for (const auto* e : *generated.getTrack (0))
        clipNotes += e->message.isNoteOn() ? 1 : 0;
    check (! fromHistory && clipNotes > 4, "clip made from the sequencers when nothing has played (" + juce::String (clipNotes) + ")");

    // Chain with offset gives two notes at once (an interval)
    p.loadFactoryPreset (1);
    const auto chords = render (p, head, 4.0);
    std::map<int, int> perTime;
    for (const auto& [t, m] : chords.midi)
        if (m.isNoteOn())
            perTime[juce::roundToInt (t * 1000.0)]++;
    auto together = 0;
    for (auto& [t, n] : perTime)
        together += n >= 2 ? 1 : 0;
    check (together > 3, "chained heads play intervals (" + juce::String (together) + ")");

    // State and presets
    p.loadFactoryPreset (4);
    setParam (p, ID::cutoff, 0.123f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    DedalProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (q.apvts.getRawParameterValue (ID::cutoff)->load() - 0.123f) < 1.0e-3f
               && q.getSnapshot().state.bits == p.getSnapshot().state.bits
               && q.getSnapshot().state.bitsC == p.getSnapshot().state.bitsC
               && q.getPresetName() == p.getPresetName(), "state round trip");

    check (p.savePreset ("Test Dedal"), "save a preset");
    setParam (p, ID::cutoff, 0.9f);
    const auto files = p.getUserPresets();
    juce::File saved;
    for (auto& f : files)
        if (f.getFileNameWithoutExtension() == "Test Dedal")
            saved = f;
    check (saved.existsAsFile() && p.loadPresetFile (saved) && std::abs (p.apvts.getRawParameterValue (ID::cutoff)->load() - 0.123f) < 1.0e-3f,
           "load the preset back");
    saved.deleteFile();

    // Buffer
    p.saveBuffer();
    const auto kept = p.getSnapshot().state.bits;
    p.command (Command::clear, 0);
    p.recallBuffer();
    check (p.getSnapshot().state.bits == kept, "BUFFER recalls the sequence");

    // Own sound off: silence, but MIDI still flows
    setParam (p, ID::sound, 0.0f);
    const auto quiet = render (p, head, 2.0);
    check (quiet.peak() == 0.0f && quiet.noteOns() > 2, "own sound off still sends MIDI");
}

void renderDemos (const juce::File& dir)
{
    dir.createDirectory();
    const auto sr = 48000.0;
    DedalProcessor p;
    Head head;
    head.bpm = 112.0;

    for (int i = 0; i < p.getFactoryPresetNames().size(); ++i)
    {
        p.loadFactoryPreset (i);
        const auto r = render (p, head, 16.0 * 60.0 / head.bpm * 2.0, true, sr);
        const auto name = juce::String (i + 1) + "_" + juce::File::createLegalFileName (p.getFactoryPresetNames()[i]).replaceCharacter (' ', '_');
        writeWav (dir.getChildFile (name + ".wav"), r.audio, sr);
        p.collectCapture();
        auto fromHistory = false;
        p.writeMidiFile (4, fromHistory).copyFileTo (dir.getChildFile (name + ".mid"));
        std::cout << name << ": peak " << r.peak() << ", " << r.noteOns() << " MIDI notes" << std::endl;
    }

    p.loadFactoryPreset (8);
    render (p, head, 3.0, true, sr);

    for (int page = 0; page < 2; ++page)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (DedalEditor::kDesignWidth, DedalEditor::kDesignHeight);

        if (auto* e = dynamic_cast<DedalEditor*> (editor.get()))
        {
            e->showPage (page);
            e->updateDisplay();
        }

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = dir.getChildFile (page == 0 ? "sekwencje.png" : "brzmienie.png");
        file.deleteFile();

        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (image, *stream);
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    testScalesAndTiming();
    testSequencers();
    testWaveBanks();
    testProcessor();

    std::cout << passes << " checks passed, " << failures << " failed" << std::endl;

    if (argc >= 3 && juce::String (argv[1]) == "--demo")
        renderDemos (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    return failures == 0 ? 0 : 1;
}
