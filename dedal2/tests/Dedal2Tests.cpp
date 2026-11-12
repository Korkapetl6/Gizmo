// Offline checks for Dedal 2:  Dedal2Tests [--demo <dir>]

#include "Dedal2Editor.h"
#include "Dedal2Params.h"
#include "Dedal2Processor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

using namespace dedal2;

namespace
{
int failures = 0, passes = 0;

void check (bool ok, const juce::String& what)
{
    if (ok) ++passes;
    else { ++failures; std::cout << "FAIL  " << what << std::endl; }
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

void setParam (Dedal2Processor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

struct Render
{
    std::vector<float> audio;
    std::vector<double> noteTimes;
    float peak() const { float m = 0; for (auto v : audio) m = std::max (m, std::abs (v)); return m; }
    float rms() const { double s = 0; for (auto v : audio) s += v * v; return (float) std::sqrt (s / std::max<size_t> (1, audio.size())); }
    bool finite() const { for (auto v : audio) if (! std::isfinite (v)) return false; return true; }
};

Render render (Dedal2Processor& p, Head& head, double seconds, bool prepare = true)
{
    const auto sr = 48000.0;
    const auto block = 256;
    p.setPlayHead (&head);

    if (prepare)
    {
        head.seconds = 0.0;
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
    }

    Render out;
    juce::AudioBuffer<float> buffer (2, block);

    for (int done = 0; done < (int) (seconds * sr); done += block)
    {
        juce::MidiBuffer midi;
        buffer.clear();
        p.processBlock (buffer, midi);

        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                out.noteTimes.push_back (head.seconds + meta.samplePosition / sr);

        out.audio.insert (out.audio.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
        head.seconds += block / sr;
    }

    return out;
}

void tests()
{
    Dedal2Processor p;
    Head head;

    for (int i = 0; i < p.getFactoryPresetNames().size(); ++i)
    {
        p.loadFactoryPreset (i);
        const auto r = render (p, head, 4.0);
        check (r.finite() && r.peak() > 0.02f && r.peak() <= 1.0f, "preset " + juce::String (i) + " plays (peak " + juce::String (r.peak(), 2) + ")");
    }

    // Normalled: the VCAs follow EG2, so without triggers there is silence
    p.loadFactoryPreset (0);
    head.playing = false;
    check (render (p, head, 1.0).peak() < 1.0e-4f, "silent while stopped");

    // EG1 out patched into the VCW VCA (and blend fully to the folder) still plays; a cable with nothing behind it mutes
    head.playing = true;
    setParam (p, ID::blend, 0.0f);
    const auto normal = render (p, head, 2.0).rms();
    p.connect (outSeq1Trig, inVcwVca);
    const auto gated = render (p, head, 2.0).rms();
    check (gated > 0.0f && gated < normal * 0.7f, "a trigger into VCW VCA CV replaces EG2 (" + juce::String (normal, 3) + " -> " + juce::String (gated, 3) + ")");
    p.connect (outEg2, inVcwVca);
    const auto same = render (p, head, 2.0).rms();
    check (std::abs (same - normal) < normal * 0.25f, "EG2 patched back by cable sounds like the normal");
    p.clearCables();

    // CLOCK 1 patched from a slow MOD VCO: fewer steps than the internal clock
    p.loadFactoryPreset (0);
    setParam (p, ID::tempo, 7);
    const auto internal = render (p, head, 4.0).noteTimes.size();
    setParam (p, ID::modFreq, -29.0f); // about 1.5 Hz
    setParam (p, ID::modSeq2, 0.0f);
    p.connect (outMvco, inClock1);
    const auto external = render (p, head, 4.0).noteTimes.size();
    check (internal > 12 && external > 1 && external < internal / 2, "M VCO into CLOCK 1 clocks SEQ1 ("
               + juce::String ((int) internal) + " -> " + juce::String ((int) external) + " notes)");
    p.clearCables();

    // RESET jack holds the sequencer on bit 1
    p.loadFactoryPreset (0);
    p.connect (outClock, inReset);
    render (p, head, 2.0);
    check (p.getSnapshot().head[0] == 0, "CLOCK into RESET keeps SEQ1 on bit 1");
    p.clearCables();

    // Audio through a cable: noise into VCF IN
    p.loadFactoryPreset (0);
    setParam (p, ID::blend, 1.0f);
    setParam (p, ID::lvlVco, 0.0f);
    check (render (p, head, 2.0).rms() < 1.0e-3f, "nothing in the mixer, nothing out");
    p.connect (outNoise, inVcfIn);
    check (render (p, head, 2.0).rms() > 0.005f, "NOISE into VCF IN is heard");
    p.clearCables();

    // Tuning: VCO at A4 without sequence
    p.loadFactoryPreset (0);
    setParam (p, ID::vcoFreq, 69.0f);
    setParam (p, ID::vcoSeq1, 0.0f);
    setParam (p, ID::blend, 0.0f);
    setParam (p, ID::eg2Decay, 5.0f);
    setParam (p, ID::tempo, 7);
    const auto tone = render (p, head, 1.0).audio;
    int crossings = 0;
    for (size_t i = 24000; i + 1 < 48000; ++i)
        crossings += tone[i] <= 0.0f && tone[i + 1] > 0.0f ? 1 : 0;
    check (std::abs (crossings * 2 - 440) <= 4, "VCO in tune (" + juce::String (crossings * 2) + " Hz)");

    // M VCO into its own SYNC: a saw a little higher than the set pitch, still following the pitch
    {
        auto pitchOf = [&] (float note)
        {
            p.loadFactoryPreset (0);
            setParam (p, ID::modFreq, note);
            setParam (p, ID::modSeq2, 0.0f);
            setParam (p, ID::lvlVco, 0.0f);
            setParam (p, ID::lvlMod, 0.5f);
            setParam (p, ID::blend, 0.0f);
            setParam (p, ID::foldEg, 0.0f);
            setParam (p, ID::eg2Decay, 5.0f);
            p.connect (outMvco, inMvcoSync);
            const auto a = render (p, head, 1.0).audio;
            int up = 0;
            for (size_t i = 24000; i + 1 < 48000; ++i)
                up += a[i] <= 0.0f && a[i + 1] > 0.0f ? 1 : 0;
            p.clearCables();
            return up * 2.0;
        };

        const auto low = pitchOf (45.0f), high = pitchOf (57.0f);
        std::cout << "self-sync: " << low << " Hz and " << high << " Hz (set 110 and 220)" << std::endl;
        check (low > 110.0 && low < 330.0 && std::abs (high / low - 2.0) < 0.1, "self-patched sync stays in tune and tracks the octave");
    }

    // State keeps the cables
    p.loadFactoryPreset (3);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    Dedal2Processor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    check (q.cableAt (inBlend) == outSeq2Cv && q.cableAt (inFold) == outEg2 && q.getSnapshot().state.bits == p.getSnapshot().state.bits,
           "state keeps cables and sequences");

    check (p.savePreset ("Test Dedal 2"), "save preset");
    p.clearCables();
    juce::File saved;
    for (auto& f : p.getUserPresets())
        if (f.getFileNameWithoutExtension() == "Test Dedal 2")
            saved = f;
    check (saved.existsAsFile() && p.loadPresetFile (saved) && p.cableAt (inBlend) == outSeq2Cv, "load preset with cables");
    saved.deleteFile();

    auto fromHistory = false;
    render (p, head, 9.0);
    const auto clip = p.makeClip (4, fromHistory);
    int notes = 0;
    for (const auto* e : *clip.getTrack (0))
        notes += e->message.isNoteOn() ? 1 : 0;
    check (fromHistory && notes > 4, "MIDI clip from what played (" + juce::String (notes) + ")");
}

void demos (const juce::File& dir)
{
    dir.createDirectory();
    Dedal2Processor p;
    Head head;
    head.bpm = 112.0;

    for (int i = 0; i < p.getFactoryPresetNames().size(); ++i)
    {
        p.loadFactoryPreset (i);
        const auto r = render (p, head, 16.0 * 60.0 / head.bpm);
        const auto file = dir.getChildFile ("Dedal2_" + juce::String (i + 1) + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());

        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24)))
        {
            juce::AudioBuffer<float> buffer (1, (int) r.audio.size());
            buffer.copyFrom (0, 0, r.audio.data(), (int) r.audio.size());
            writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }

        std::cout << p.getFactoryPresetNames()[i] << ": peak " << r.peak() << std::endl;
    }

    p.loadFactoryPreset (3);
    render (p, head, 2.0);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (Dedal2Editor::kDesignWidth, Dedal2Editor::kDesignHeight);

    if (auto* e = dynamic_cast<Dedal2Editor*> (editor.get()))
        e->updateDisplay();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const auto file = dir.getChildFile ("dedal2.png");
    file.deleteFile();

    if (auto stream = file.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (image, *stream);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    tests();
    std::cout << passes << " checks passed, " << failures << " failed" << std::endl;

    if (argc >= 3 && juce::String (argv[1]) == "--demo")
        demos (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    return failures == 0 ? 0 : 1;
}
