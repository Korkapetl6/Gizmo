// Offline checks for Gizmo: wavetable import, rendering, state and a screenshot of the editor.
// Usage: GizmoTests <output folder>

#include "Params.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/HalfbandDecimator.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <functional>
#include <iostream>

using namespace gizmo;

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;

    if (! ok)
        ++failures;
}

constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Test sources
std::vector<float> sawThroughSweepingFilter (double sr, double seconds)
{
    std::vector<float> out ((size_t) (sr * seconds));
    double phase = 0.0, s1 = 0.0, s2 = 0.0;

    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto t = (double) i / (double) out.size();
        const auto cutoff = 150.0 * std::pow (60.0, t);
        const auto a = 1.0 - std::exp (-2.0 * kPi * cutoff / sr);
        phase += 110.0 / sr;
        phase -= std::floor (phase);
        const auto saw = 2.0 * phase - 1.0;
        s1 += a * (saw - s1);
        s2 += a * (s1 - s2);
        out[i] = (float) (0.6 * s2);
    }

    return out;
}

std::vector<float> vowels (double sr, double seconds)
{
    // a, e, i, o, u formants
    const double f1[] = { 800, 400, 300, 450, 325 }, f2[] = { 1150, 1700, 2300, 800, 700 }, f3[] = { 2900, 2600, 2900, 2830, 2530 };
    std::vector<float> out ((size_t) (sr * seconds));
    const double f0 = 140.0;
    std::vector<double> amps (40);

    for (size_t i = 0; i < out.size(); ++i)
    {
        if (i % 64 == 0)
        {
            const auto t = (double) i / (double) out.size() * 4.0;
            const auto k = std::min (3, (int) t);
            const auto u = t - k;

            for (int h = 1; h <= 40; ++h)
            {
                const auto f = h * f0;
                auto formant = [f] (double fc, double bw) { const auto d = (f - fc) / bw; return 1.0 / (1.0 + d * d); };
                const auto F1 = f1[k] + u * (f1[k + 1] - f1[k]), F2 = f2[k] + u * (f2[k + 1] - f2[k]), F3 = f3[k] + u * (f3[k + 1] - f3[k]);
                amps[(size_t) h - 1] = (formant (F1, 90) + 0.7 * formant (F2, 110) + 0.4 * formant (F3, 160)) / std::sqrt ((double) h);
            }
        }

        double v = 0.0;
        const auto ph = 2.0 * kPi * f0 * (double) i / sr;

        for (int h = 1; h <= 40; ++h)
            v += amps[(size_t) h - 1] * std::sin (h * ph);

        out[i] = (float) (0.2 * v);
    }

    return out;
}

std::vector<float> noiseBurst (double sr, double seconds)
{
    juce::Random r (1);
    std::vector<float> out ((size_t) (sr * seconds));

    for (size_t i = 0; i < out.size(); ++i)
        out[i] = (r.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-3.0 * (double) i / sr);

    return out;
}

std::vector<float> sineGlide (double sr, double seconds)
{
    std::vector<float> out ((size_t) (sr * seconds));
    double phase = 0.0;

    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto t = (double) i / (double) out.size();
        phase += 200.0 * std::pow (4.0, t) / sr;
        out[i] = (float) (0.5 * std::sin (2.0 * kPi * phase));
    }

    return out;
}

//==============================================================================
std::unique_ptr<Wavetable> tableFrom (const std::vector<float>& audio, double sr, ImportMode mode, const juce::String& name)
{
    auto result = buildWavetableFromAudio (audio, sr, mode);

    if (result.table == nullptr)
    {
        check (false, "import " + name + ": " + result.error);
        return makeDefaultWavetable();
    }

    result.table->name = name;
    return std::move (result.table);
}

void checkTable (const Wavetable& t, const juce::String& name, int minPitched, int maxPitched)
{
    bool finite = true;
    float peak = 0.0f, worstDc = 0.0f, worstWrapRatio = 0.0f;

    for (int f = 0; f < kNumFrames; ++f)
    {
        const auto* x = t.getFrame (f);
        double sum = 0.0;
        float maxStep = 1.0e-6f;

        for (int i = 0; i < kFrameSize; ++i)
        {
            finite = finite && std::isfinite (x[i]);
            peak = std::max (peak, std::abs (x[i]));
            sum += x[i];

            if (i > 0)
                maxStep = std::max (maxStep, std::abs (x[i] - x[i - 1]));
        }

        worstDc = std::max (worstDc, (float) std::abs (sum / kFrameSize));
        worstWrapRatio = std::max (worstWrapRatio, std::abs (x[0] - x[kFrameSize - 1]) / maxStep);
    }

    std::cout << "  " << name << ": " << t.pitchedFrames << " of 64 frames cut as cycles, peak " << peak
              << ", worst DC " << worstDc << ", worst loop jump " << worstWrapRatio << "x the largest step" << std::endl;
    check (finite, name + ": all samples finite");
    check (std::abs (peak - 1.0f) < 1.0e-3f, name + ": normalised to peak 1");
    check (worstDc < 1.0e-3f, name + ": no DC offset");
    check (t.pitchedFrames >= minPitched && t.pitchedFrames <= maxPitched,
           name + ": pitched frames within " + juce::String (minPitched) + ".." + juce::String (maxPitched));
}

void writeWav (const juce::File& file, const std::vector<float>& left, const std::vector<float>* right, double sr)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());

    if (stream == nullptr)
        return;

    const auto channels = right != nullptr ? 2 : 1;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (channels).withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
        return;

    juce::AudioBuffer<float> buffer (channels, (int) left.size());
    buffer.copyFrom (0, 0, left.data(), (int) left.size());

    if (right != nullptr)
        buffer.copyFrom (1, 0, right->data(), (int) right->size());

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

//==============================================================================
void setParam (GizmoProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

struct Note
{
    double start, length;
    int note;
    float velocity;
};

struct Render
{
    std::vector<float> left, right;
};

/** A host transport that is always playing at a fixed tempo. */
struct DemoPlayHead : public juce::AudioPlayHead
{
    double bpm = 120.0, seconds = 0.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (seconds * bpm / 60.0);
        info.setIsPlaying (true);
        return info;
    }
};

Render render (GizmoProcessor& p, double seconds, const std::vector<Note>& notes,
               std::function<void (double)> automation = {}, double sr = 48000.0, int block = 512,
               DemoPlayHead* playHead = nullptr)
{
    p.setPlayHead (playHead);
    p.prepareToPlay (sr, block);
    Render out;
    const auto total = (int) (seconds * sr);
    juce::AudioBuffer<float> buffer (2, block);

    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);
        buffer.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;

        for (const auto& note : notes)
        {
            const auto on = (int) (note.start * sr), off = (int) ((note.start + note.length) * sr);

            if (on >= pos && on < pos + n)
                midi.addEvent (juce::MidiMessage::noteOn (1, note.note, note.velocity), on - pos);

            if (off >= pos && off < pos + n)
                midi.addEvent (juce::MidiMessage::noteOff (1, note.note), off - pos);
        }

        if (automation)
            automation ((double) pos / sr);

        if (playHead != nullptr)
            playHead->seconds = (double) pos / sr;

        p.processBlock (buffer, midi);
        out.left.insert (out.left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + n);
        out.right.insert (out.right.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + n);
    }

    return out;
}

struct Stats
{
    bool finite = true;
    float peak = 0.0f, rms = 0.0f, clickRatio = 0.0f;
};

/** clickRatio: largest second difference compared with the 99.9th percentile of all of them. */
Stats analyse (const std::vector<float>& x)
{
    Stats s;
    std::vector<float> d;
    double sum = 0.0;

    for (size_t i = 0; i < x.size(); ++i)
    {
        s.finite = s.finite && std::isfinite (x[i]);
        s.peak = std::max (s.peak, std::abs (x[i]));
        sum += (double) x[i] * x[i];

        if (i >= 2)
            d.push_back (std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
    }

    s.rms = (float) std::sqrt (sum / (double) std::max<size_t> (1, x.size()));

    if (! d.empty())
    {
        auto sorted = d;
        std::nth_element (sorted.begin(), sorted.begin() + (long) (sorted.size() * 999 / 1000), sorted.end());
        const auto p999 = std::max (1.0e-6f, sorted[sorted.size() * 999 / 1000]);
        s.clickRatio = *std::max_element (d.begin(), d.end()) / p999;
    }

    return s;
}

/** Fraction (in dB) of the energy that is not near a harmonic of f0. */
double inharmonicDb (const std::vector<float>& x, size_t start, double sr, double f0)
{
    constexpr int order = 16;
    constexpr int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2, 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const auto a = 2.0 * kPi * i / (n - 1); // 4-term Blackman-Harris
        const auto w = 0.35875 - 0.48829 * std::cos (a) + 0.14128 * std::cos (2.0 * a) - 0.01168 * std::cos (3.0 * a);
        buf[(size_t) i] = x[start + (size_t) i] * (float) w;
    }

    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
    double harmonic = 0.0, other = 0.0;

    for (int k = 2; k < n / 2; ++k)
    {
        const auto f = k * sr / n;
        const auto nearest = std::round (f / f0) * f0;
        const auto e = (double) buf[(size_t) k] * buf[(size_t) k];

        if (std::abs (f - nearest) < 10.0 * sr / n && nearest > 0.0)
            harmonic += e;
        else if (f < 20000.0)
            other += e;
    }

    return 10.0 * std::log10 ((other + 1.0e-30) / (harmonic + 1.0e-30));
}

double zeroCrossingFrequency (const std::vector<float>& x, size_t start, size_t length, double sr)
{
    double first = -1.0, last = -1.0;
    int count = 0;

    for (size_t i = start + 1; i < start + length; ++i)
    {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const auto pos = (double) (i - 1) + (double) (x[i - 1] / (x[i - 1] - x[i]));

            if (first < 0.0)
                first = pos;
            else
                ++count;

            last = pos;
        }
    }

    return count > 0 ? sr * count / (last - first) : 0.0;
}

/** Spectral centroid in Hz (or in harmonics when sr == length, for a single frame). */
double brightness (const std::vector<float>& x, double sr = 48000.0)
{
    int order = 1;

    while ((1 << order) < (int) std::min<size_t> (x.size(), 1 << 16))
        ++order;

    if ((size_t) (1 << order) > x.size())
        --order;

    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2, 0.0f);
    const auto start = (x.size() - (size_t) n) / 2;

    for (int i = 0; i < n; ++i)
        buf[(size_t) i] = x[start + (size_t) i];

    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
    double weighted = 0.0, total = 0.0;

    for (int k = 1; k < n / 2; ++k)
    {
        const auto e = (double) buf[(size_t) k] * buf[(size_t) k];
        weighted += e * k * sr / n;
        total += e;
    }

    return weighted / std::max (1.0e-30, total);
}

//==============================================================================
// Demo mode: GizmoTests --demo <sample> <output folder>
// Renders a few musical examples with a real sample, at 75 BPM in A-flat major.

std::unique_ptr<Wavetable> copyOf (const Wavetable& t)
{
    auto c = buildWavetableFromFrames (t.frames.data());
    c->name = t.name;
    c->sourcePath = t.sourcePath;
    c->pitchedFrames = t.pitchedFrames;
    c->source = t.source;
    c->sourceRate = t.sourceRate;
    c->rootNote = t.rootNote;
    return c;
}

void setParams (GizmoProcessor& p, std::initializer_list<std::pair<const char*, float>> values)
{
    for (const auto& [id, value] : values)
        setParam (p, id, value);
}

int runDemo (const juce::File& sample, const juce::File& outDir)
{
    outDir.createDirectory();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto cycles = loadWavetableFromFile (sample, ImportMode::cycles, formats);
    auto chunks = loadWavetableFromFile (sample, ImportMode::chunks, formats);

    if (cycles.table == nullptr || chunks.table == nullptr)
    {
        std::cout << "Import failed: " << cycles.error << " " << chunks.error << std::endl;
        return 1;
    }

    std::cout << sample.getFileName() << ": " << cycles.table->pitchedFrames << " of 64 frames cut as cycles" << std::endl;

    constexpr double sr = 48000.0, bpm = 75.0, beat = 60.0 / bpm, bar = 4.0 * beat;
    DemoPlayHead playHead;
    playHead.bpm = bpm;

    // The table itself, played by the plugin: one held note while the position goes from frame 1 to 64.
    for (auto stepped : { false, true })
    {
        GizmoProcessor p;
        p.replaceTable (GizmoProcessor::slotA, copyOf (*cycles.table));
        setParams (p, { { ParamID::position, 0.0f }, { ParamID::frameMode, stepped ? 1.0f : 0.0f },
                        { ParamID::levelB, 0.0f }, { ParamID::detune, 0.0f }, { ParamID::panA, 0.0f },
                        { ParamID::filterOn, 0.0f }, { ParamID::attack, 0.01f }, { ParamID::sustain, 1.0f },
                        { ParamID::release, 0.3f }, { ParamID::velToAmp, 0.0f } });

        const auto r = render (p, 5.0, { { 0.0, 4.6, 56, 0.8f } },
                               [&p] (double t) { setParam (p, ParamID::position, (float) juce::jlimit (0.0, 1.0, (t - 0.2) / 4.0)); },
                               sr, 512, &playHead);
        const auto s = analyse (r.left);
        std::cout << "  sweep " << (stepped ? "stepped" : "smooth") << ": click ratio " << s.clickRatio << std::endl;
        writeWav (outDir.getChildFile (stepped ? "sweep_stepped.wav" : "sweep_smooth.wav"), r.left, &r.right, sr);
    }

    // Abmaj7, Fm7, Dbmaj7, Eb6
    const std::vector<std::vector<int>> progression { { 56, 63, 67, 72 }, { 53, 60, 63, 68 }, { 49, 56, 60, 65 }, { 51, 58, 63, 67 } };

    auto report = [] (const char* name, const Render& r)
    {
        const auto s = analyse (r.left);
        std::cout << "  " << name << ": peak " << s.peak << ", rms " << s.rms << (s.finite ? "" : "  NOT FINITE") << std::endl;
    };

    // 1. Pad: cycles, smooth, a slow tempo-synced LFO sweeping the position
    {
        GizmoProcessor p;
        p.replaceTable (GizmoProcessor::slotA, copyOf (*cycles.table));
        setParams (p, { { ParamID::position, 0.45f }, { ParamID::levelA, 0.8f }, { ParamID::panA, -0.35f },
                        { ParamID::levelB, 0.65f }, { ParamID::panB, 0.35f }, { ParamID::offsetB, 0.3f },
                        { ParamID::detune, 12.0f }, { ParamID::attack, 0.7f }, { ParamID::decay, 1.5f },
                        { ParamID::sustain, 0.85f }, { ParamID::release, 2.0f }, { ParamID::velToAmp, 0.3f },
                        { ParamID::cutoff, 4500.0f }, { ParamID::resonance, 0.15f }, { ParamID::keyTrack, 0.3f },
                        { ParamID::lfoShape, 0.0f }, { ParamID::lfoSync, 1.0f }, { ParamID::lfoDivision, 1.0f },
                        { ParamID::lfoToPos, 0.8f }, { ParamID::gain, -12.0f } });

        std::vector<Note> notes;

        for (size_t c = 0; c < progression.size(); ++c)
            for (auto n : progression[c])
                notes.push_back ({ (double) c * bar + 0.02, bar - 0.05, n, 0.7f });

        const auto r = render (p, 4.0 * bar + 2.5, notes, {}, sr, 512, &playHead);
        report ("pad", r);
        writeWav (outDir.getChildFile ("demo1_pad.wav"), r.left, &r.right, sr);
    }

    // 2. Arpeggio: chunks, stepped, raw, a random LFO jumping on every sixteenth
    {
        GizmoProcessor p;
        p.replaceTable (GizmoProcessor::slotA, copyOf (*chunks.table));
        setParams (p, { { ParamID::position, 0.5f }, { ParamID::frameMode, 1.0f }, { ParamID::character, 1.0f },
                        { ParamID::levelA, 0.8f }, { ParamID::panA, -0.25f }, { ParamID::levelB, 0.45f },
                        { ParamID::panB, 0.25f }, { ParamID::semiB, 12.0f }, { ParamID::offsetB, 0.4f },
                        { ParamID::detune, 7.0f }, { ParamID::attack, 0.002f }, { ParamID::decay, 0.22f },
                        { ParamID::sustain, 0.25f }, { ParamID::release, 0.15f }, { ParamID::velToAmp, 0.6f },
                        { ParamID::cutoff, 2500.0f }, { ParamID::resonance, 0.45f }, { ParamID::keyTrack, 0.4f },
                        { ParamID::filterEnv, 0.45f }, { ParamID::fAttack, 0.001f }, { ParamID::fDecay, 0.18f },
                        { ParamID::fSustain, 0.0f }, { ParamID::fRelease, 0.15f }, { ParamID::lfoShape, 4.0f },
                        { ParamID::lfoSync, 1.0f }, { ParamID::lfoDivision, 10.0f }, { ParamID::lfoToPos, 1.0f },
                        { ParamID::velToPos, 0.3f }, { ParamID::gain, -8.0f } });

        const int pattern[] = { 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, 1 };
        std::vector<Note> notes;
        const auto sixteenth = beat / 4.0;

        for (size_t c = 0; c < progression.size(); ++c)
        {
            std::vector<int> tones (progression[c]);

            for (auto n : progression[c])
                tones.push_back (n + 12);

            for (int k = 0; k < 16; ++k)
            {
                const auto velocity = k % 4 == 0 ? 0.95f : 0.55f + 0.1f * (float) (k % 3);
                notes.push_back ({ (double) c * bar + k * sixteenth + 0.01, sixteenth * 0.6, tones[(size_t) pattern[k]], velocity });
            }
        }

        const auto r = render (p, 4.0 * bar + 1.0, notes, {}, sr, 512, &playHead);
        report ("arpeggio", r);
        writeWav (outDir.getChildFile ("demo2_arpeggio.wav"), r.left, &r.right, sr);
    }

    // 3. Lead: cycles, mono with glide, the LFO restarting with every note so each one sweeps the table
    {
        GizmoProcessor p;
        p.replaceTable (GizmoProcessor::slotA, copyOf (*cycles.table));
        setParams (p, { { ParamID::position, 0.15f }, { ParamID::voiceMode, 1.0f }, { ParamID::glide, 0.07f },
                        { ParamID::levelA, 0.85f }, { ParamID::panA, -0.15f }, { ParamID::levelB, 0.4f },
                        { ParamID::panB, 0.15f }, { ParamID::semiB, -12.0f }, { ParamID::offsetB, 0.25f },
                        { ParamID::detune, 9.0f }, { ParamID::attack, 0.01f }, { ParamID::decay, 0.6f },
                        { ParamID::sustain, 0.75f }, { ParamID::release, 0.35f }, { ParamID::velToAmp, 0.4f },
                        { ParamID::cutoff, 1200.0f }, { ParamID::resonance, 0.25f }, { ParamID::keyTrack, 0.5f },
                        { ParamID::filterEnv, 0.35f }, { ParamID::fDecay, 0.5f }, { ParamID::fSustain, 0.3f },
                        { ParamID::fRelease, 0.3f }, { ParamID::lfoShape, 2.0f }, { ParamID::lfoSync, 0.0f },
                        { ParamID::lfoRate, 0.35f }, { ParamID::lfoRetrig, 1.0f }, { ParamID::lfoToPos, 0.9f },
                        { ParamID::gain, -8.0f } });

        const std::pair<int, double> melody[] = { { 63, 1.0 }, { 65, 0.5 }, { 68, 1.5 }, { 72, 1.0 },
                                                  { 70, 0.5 }, { 68, 0.5 }, { 65, 1.0 }, { 63, 2.0 },
                                                  { 60, 1.0 }, { 63, 0.5 }, { 65, 1.5 }, { 68, 1.0 },
                                                  { 67, 0.5 }, { 65, 0.5 }, { 63, 1.0 }, { 68, 2.0 } };
        std::vector<Note> notes;
        double t = 0.02;
        int i = 0;

        for (const auto& [note, beats] : melody)
        {
            notes.push_back ({ t, beats * beat * 0.9, note, 0.6f + 0.1f * (float) (i++ % 4) });
            t += beats * beat;
        }

        const auto r = render (p, t + 1.5, notes, {}, sr, 512, &playHead);
        report ("lead", r);
        writeWav (outDir.getChildFile ("demo3_lead.wav"), r.left, &r.right, sr);
    }

    // 4. Chords with the A/B sequence A A B B B B B A: the wavetable, then the flute itself
    const auto transpose = 12.0f * (float) std::round ((cycles.table->rootNote - 62.0f) / 12.0f);
    std::cout << "  flute pitch " << cycles.table->rootNote << ", B transposed by " << transpose << std::endl;

    {
        GizmoProcessor p;
        p.replaceTable (GizmoProcessor::slotA, copyOf (*cycles.table));
        p.setPattern (kDefaultPattern);
        setParams (p, { { ParamID::position, 0.45f }, { ParamID::levelA, 0.8f }, { ParamID::panA, -0.35f },
                        { ParamID::levelB, 0.65f }, { ParamID::panB, 0.35f }, { ParamID::offsetB, 0.3f },
                        { ParamID::detune, 12.0f }, { ParamID::attack, 0.02f }, { ParamID::decay, 1.5f },
                        { ParamID::sustain, 0.85f }, { ParamID::release, 1.2f }, { ParamID::velToAmp, 0.3f },
                        { ParamID::cutoff, 6000.0f }, { ParamID::keyTrack, 0.3f },
                        { ParamID::lfoSync, 1.0f }, { ParamID::lfoDivision, 1.0f }, { ParamID::lfoToPos, 0.8f },
                        { ParamID::seqOn, 1.0f }, { ParamID::seqLength, 8.0f }, { ParamID::seqRate, 3.0f },
                        { ParamID::fragTranspose, transpose }, { ParamID::fragLevel, 0.9f }, { ParamID::gain, -12.0f } });

        std::vector<Note> notes;

        for (size_t c = 0; c < progression.size(); ++c)
            for (auto n : progression[c])
                notes.push_back ({ (double) c * bar + 0.02, bar - 0.05, n, 0.7f });

        const auto r = render (p, 4.0 * bar + 2.0, notes, {}, sr, 512, &playHead);
        report ("sequence chords", r);
        writeWav (outDir.getChildFile ("demo4_sequence_chords.wav"), r.left, &r.right, sr);
    }

    // 5. Lead with A B A B on eighths, B played backwards
    {
        GizmoProcessor p;
        p.replaceTable (GizmoProcessor::slotA, copyOf (*cycles.table));
        p.setPattern (0xaa);
        setParams (p, { { ParamID::position, 0.3f }, { ParamID::voiceMode, 1.0f }, { ParamID::glide, 0.05f },
                        { ParamID::levelA, 0.85f }, { ParamID::levelB, 0.4f }, { ParamID::semiB, -12.0f },
                        { ParamID::detune, 9.0f }, { ParamID::attack, 0.01f }, { ParamID::decay, 0.6f },
                        { ParamID::sustain, 0.8f }, { ParamID::release, 0.35f }, { ParamID::cutoff, 3000.0f },
                        { ParamID::resonance, 0.2f }, { ParamID::keyTrack, 0.5f },
                        { ParamID::seqOn, 1.0f }, { ParamID::seqLength, 8.0f }, { ParamID::seqRate, 1.0f },
                        { ParamID::seqStart, 1.0f }, { ParamID::fragDirection, 1.0f }, { ParamID::fragTranspose, transpose },
                        { ParamID::fragLevel, 0.9f }, { ParamID::gain, -8.0f } });

        const std::pair<int, double> melody[] = { { 63, 2.0 }, { 68, 1.0 }, { 72, 1.0 }, { 70, 2.0 }, { 65, 2.0 },
                                                  { 60, 2.0 }, { 63, 1.0 }, { 65, 1.0 }, { 68, 4.0 } };
        std::vector<Note> notes;
        double t = 0.02;

        for (const auto& [note, beats] : melody)
        {
            notes.push_back ({ t, beats * beat * 0.95, note, 0.75f });
            t += beats * beat;
        }

        const auto r = render (p, t + 1.5, notes, {}, sr, 512, &playHead);
        report ("sequence lead, B reversed", r);
        writeWav (outDir.getChildFile ("demo5_sequence_reverse.wav"), r.left, &r.right, sr);
    }

    // Editor with the sample loaded
    {
        GizmoProcessor p;
        p.replaceTable (GizmoProcessor::slotA, copyOf (*cycles.table));
        setParams (p, { { ParamID::position, 0.45f }, { ParamID::offsetB, 0.3f }, { ParamID::detune, 12.0f },
                        { ParamID::cutoff, 4500.0f }, { ParamID::lfoSync, 1.0f }, { ParamID::lfoDivision, 1.0f },
                        { ParamID::lfoToPos, 0.8f }, { ParamID::seqOn, 1.0f }, { ParamID::fragTranspose, transpose } });
        render (p, 1.0, { { 0.0, 2.0, 56, 0.7f }, { 0.0, 2.0, 63, 0.7f }, { 0.0, 2.0, 67, 0.7f } }, {}, sr, 512, &playHead);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (GizmoEditor::kDesignWidth, GizmoEditor::kDesignHeight);

        if (auto* e = dynamic_cast<GizmoEditor*> (editor.get()))
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

    return 0;
}
} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc > 3 && juce::String (argv[1]) == "--demo")
        return runDemo (juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])));

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    constexpr double sr = 44100.0;

    std::cout << "Wavetable import" << std::endl;
    const auto saw = sawThroughSweepingFilter (sr, 3.0);
    const auto voice = vowels (sr, 3.0);
    const auto noise = noiseBurst (sr, 1.0);
    const auto glide = sineGlide (sr, 2.0);

    auto sawTable = tableFrom (saw, sr, ImportMode::cycles, "saw sweep");
    auto vowelTable = tableFrom (voice, sr, ImportMode::cycles, "vowels");
    auto noiseTable = tableFrom (noise, sr, ImportMode::cycles, "noise");
    auto glideTable = tableFrom (glide, sr, ImportMode::cycles, "sine glide");
    auto chunkTable = tableFrom (voice, sr, ImportMode::chunks, "vowels as chunks");

    std::cout << "  detected pitch: vowels " << vowelTable->rootNote << " (140 Hz is 49.1), saw " << sawTable->rootNote
              << " (110 Hz is 45.0)" << std::endl;
    check (std::abs (vowelTable->rootNote - 49.13f) < 0.3f && std::abs (sawTable->rootNote - 45.0f) < 0.3f, "sample pitch detected");
    checkTable (*sawTable, "saw sweep", 60, 64);
    checkTable (*vowelTable, "vowels", 60, 64);
    checkTable (*noiseTable, "noise", 0, 12);
    checkTable (*glideTable, "sine glide", 60, 64);
    checkTable (*chunkTable, "vowels as chunks", 0, 0);
    checkTable (*makeDefaultWavetable(), "default table", 0, 0);

    {
        // Brightness should rise across the table made from the opening filter.
        const auto first = std::vector<float> (sawTable->getFrame (2), sawTable->getFrame (2) + kFrameSize);
        const auto last = std::vector<float> (sawTable->getFrame (61), sawTable->getFrame (61) + kFrameSize);
        std::cout << "  saw sweep: centroid frame 3 " << brightness (first, kFrameSize) << ", frame 62 " << brightness (last, kFrameSize) << " (in harmonics)" << std::endl;
        check (brightness (last, kFrameSize) > brightness (first, kFrameSize) * 2.0, "saw sweep: later frames are brighter");
    }


    std::cout << "Decimator" << std::endl;
    {
        auto gainAt = [] (double freq)
        {
            HalfbandDecimator d;
            std::vector<float> in (2 * 8192), out (8192);

            for (size_t i = 0; i < in.size(); ++i)
                in[i] = (float) std::sin (2.0 * kPi * freq * (double) i / 96000.0);

            d.process (in.data(), out.data(), (int) out.size());
            float peak = 0.0f;

            for (size_t i = 1000; i < out.size(); ++i)
                peak = std::max (peak, std::abs (out[i]));

            return peak;
        };

        check (std::abs (gainAt (1000.0) - 1.0f) < 0.01f, "passes 1 kHz unchanged");
        check (gainAt (19000.0) > 0.97f, "passes 19 kHz (at 48 kHz)");
        check (gainAt (30000.0) < 1.0e-3f, "blocks 30 kHz by more than 60 dB");
    }

    std::cout << "Rendering" << std::endl;
    const double rsr = 48000.0;

    {
        GizmoProcessor p;
        setParam (p, ParamID::levelB, 0.0f);
        setParam (p, ParamID::detune, 0.0f);
        const auto r = render (p, 1.5, { { 0.0, 1.4, 69, 0.8f } }, {}, rsr);
        const auto f = zeroCrossingFrequency (r.left, (size_t) (0.3 * rsr), (size_t) rsr, rsr);
        std::cout << "  A4 measures " << f << " Hz" << std::endl;
        check (std::abs (f - 440.0) < 0.3, "A4 plays at 440 Hz");
    }

    {
        // A sawtooth at C7: clean mode must not alias, raw mode should.
        auto aliasing = [] (bool raw)
        {
            GizmoProcessor p;
            setParam (p, ParamID::levelB, 0.0f);
            setParam (p, ParamID::detune, 0.0f);
            setParam (p, ParamID::position, 21.0f / 63.0f);
            setParam (p, ParamID::character, raw ? 1.0f : 0.0f);
            const auto r = render (p, 2.0, { { 0.0, 2.0, 96, 0.9f } }, {}, 48000.0);
            return inharmonicDb (r.left, (size_t) (0.5 * 48000.0), 48000.0, 440.0 * std::pow (2.0, 27.0 / 12.0));
        };

        const auto clean = aliasing (false), raw = aliasing (true);
        std::cout << "  inharmonic energy at C7: clean " << clean << " dB, raw " << raw << " dB" << std::endl;
        check (clean < -60.0, "clean mode: aliasing below -60 dB");
        check (raw > clean + 10.0, "raw mode: clearly more aliasing");
    }

    const std::vector<Note> chord { { 0.05, 6.0, 48, 0.8f }, { 0.05, 6.0, 55, 0.7f }, { 0.05, 6.0, 60, 0.75f }, { 0.05, 6.0, 64, 0.7f } };
    auto sweep = [] (GizmoProcessor& p) { return [&p] (double t) { setParam (p, ParamID::position, (float) (t < 3.0 ? t / 3.0 : std::max (0.0, 2.0 - t / 3.0))); }; };

    for (auto stepped : { false, true })
    {
        GizmoProcessor p;
        p.replaceTable (GizmoProcessor::slotA, tableFrom (voice, sr, ImportMode::cycles, "vowels"));
        setParam (p, ParamID::frameMode, stepped ? 1.0f : 0.0f);
        setParam (p, ParamID::attack, 0.02f);
        const auto r = render (p, 7.5, chord, sweep (p), rsr);
        const auto s = analyse (r.left);
        const auto name = juce::String (stepped ? "stepped" : "smooth") + " sweep";
        std::cout << "  " << name << ": peak " << s.peak << ", rms " << s.rms << ", click ratio " << s.clickRatio << std::endl;
        check (s.finite && s.peak > 0.05f && s.peak < 1.5f, name + ": sane level");

        check (s.clickRatio < 8.0f, name + ": no clicks");

        writeWav (outDir.getChildFile (stepped ? "demo_stepped_sweep.wav" : "demo_smooth_sweep.wav"), r.left, &r.right, rsr);
    }

    {
        // Filter: a low cutoff must make the sound darker; LFO and envelope must not blow up.
        auto renderFiltered = [&chord] (bool filtered)
        {
            GizmoProcessor p;
            setParam (p, ParamID::position, 21.0f / 63.0f);

            if (filtered)
            {
                setParam (p, ParamID::cutoff, 400.0f);
                setParam (p, ParamID::resonance, 0.6f);
                setParam (p, ParamID::filterEnv, 0.5f);
                setParam (p, ParamID::lfoToCutoff, 0.4f);
                setParam (p, ParamID::lfoRate, 3.0f);
                setParam (p, ParamID::lfoToPos, 0.6f);
            }

            return render (p, 4.0, { { 0.05, 3.0, 48, 0.9f }, { 0.05, 3.0, 55, 0.9f } }, {}, 48000.0);
        };

        const auto open = renderFiltered (false), closed = renderFiltered (true);
        const auto s = analyse (closed.left);
        std::cout << "  filter: brightness open " << brightness (open.left) << ", filtered " << brightness (closed.left)
                  << ", peak " << s.peak << std::endl;
        check (s.finite && s.peak < 2.0f, "filter + LFO: sane level");
        check (brightness (closed.left) < brightness (open.left) * 0.6, "filter: low cutoff is darker");
        writeWav (outDir.getChildFile ("demo_filter_lfo.wav"), closed.left, &closed.right, 48000.0);
    }

    {
        // More notes than voices: stealing must not click.
        GizmoProcessor p;
        std::vector<Note> notes;

        for (int i = 0; i < 24; ++i)
            notes.push_back ({ 0.05 + i * 0.06, 2.0, 48 + i, 0.8f });

        const auto r = render (p, 4.0, notes, {}, rsr);
        const auto s = analyse (r.left);
        std::cout << "  voice stealing: peak " << s.peak << ", click ratio " << s.clickRatio << std::endl;
        check (s.finite && s.peak < 3.0f, "voice stealing: sane level");
    }

    {
        // Mono glide: halfway through the glide the pitch is between the two notes.
        GizmoProcessor p;
        setParam (p, ParamID::voiceMode, 1.0f);
        setParam (p, ParamID::glide, 0.4f);
        setParam (p, ParamID::levelB, 0.0f);
        setParam (p, ParamID::detune, 0.0f);
        const auto r = render (p, 2.0, { { 0.0, 1.8, 57, 0.8f }, { 0.8, 1.0, 69, 0.8f } }, {}, rsr);
        const auto before = zeroCrossingFrequency (r.left, (size_t) (0.4 * rsr), (size_t) (0.3 * rsr), rsr);
        const auto during = zeroCrossingFrequency (r.left, (size_t) (0.84 * rsr), (size_t) (0.05 * rsr), rsr);
        const auto after = zeroCrossingFrequency (r.left, (size_t) (1.5 * rsr), (size_t) (0.25 * rsr), rsr);
        std::cout << "  glide: " << before << " -> " << during << " -> " << after << " Hz" << std::endl;
        check (std::abs (before - 220.0) < 1.0 && std::abs (after - 440.0) < 1.5 && during > 230.0 && during < 430.0,
               "mono glide moves from A3 to A4");
    }

    std::cout << "A/B sequence" << std::endl;
    {
        // Pattern A B at 1/16 and 120 BPM: 125 ms per step. With the fragment silent, B steps must be quiet.
        auto sequence = [&] (int direction, float fragLevel)
        {
            GizmoProcessor p;
            p.replaceTable (GizmoProcessor::slotA, tableFrom (voice, sr, ImportMode::cycles, "vowels"));
            p.setPattern (0x2);
            setParams (p, { { ParamID::seqOn, 1.0f }, { ParamID::seqLength, 2.0f }, { ParamID::seqRate, 3.0f },
                            { ParamID::fragDirection, (float) direction }, { ParamID::fragLevel, fragLevel },
                            { ParamID::levelB, 0.0f }, { ParamID::attack, 0.001f }, { ParamID::sustain, 1.0f } });
            DemoPlayHead head;
            head.bpm = 120.0;
            return render (p, 2.0, { { 0.0, 2.0, 57, 0.9f } }, {}, 48000.0, 512, &head);
        };

        const auto gated = sequence (0, 0.0f);
        auto windowRms = [] (const std::vector<float>& x, double from, double to)
        {
            double sum = 0.0;
            const auto a = (size_t) (from * 48000.0), b = (size_t) (to * 48000.0);

            for (auto i = a; i < b; ++i)
                sum += (double) x[i] * x[i];

            return std::sqrt (sum / (double) (b - a));
        };

        double loudA = 0.0, loudB = 0.0;

        for (int step = 2; step < 14; ++step)
        {
            const auto rms = windowRms (gated.left, step * 0.125 + 0.03, step * 0.125 + 0.095);
            (step % 2 == 0 ? loudA : loudB) += rms / 6.0;
        }

        std::cout << "  A steps rms " << loudA << ", silent B steps rms " << loudB << std::endl;
        check (loudA > 0.02 && loudB < loudA * 0.01, "steps switch on the 1/16 grid");

        const auto forward = sequence (0, 0.8f), backward = sequence (1, 0.8f), pingPong = sequence (2, 0.8f);

        for (const auto* r : { &forward, &backward, &pingPong })
        {
            const auto st = analyse (r->left);
            check (st.finite && st.clickRatio < 8.0f && st.peak < 1.5f,
                   "sequence " + juce::String (r == &forward ? "forward" : (r == &backward ? "reverse" : "forward-and-back"))
                       + ": no clicks (ratio " + juce::String (st.clickRatio, 2) + ")");
        }

        double difference = 0.0;

        for (size_t i = 0; i < forward.left.size(); ++i)
            difference += std::abs (forward.left[i] - backward.left[i]);

        check (difference / (double) forward.left.size() > 1.0e-3, "reverse plays something different");
    }

    std::cout << "State" << std::endl;
    {
        GizmoProcessor a;
        a.replaceTable (GizmoProcessor::slotA, tableFrom (voice, sr, ImportMode::cycles, "vowels.wav"));
        a.replaceTable (GizmoProcessor::slotB, tableFrom (saw, sr, ImportMode::cycles, "saw.wav"));
        setParam (a, ParamID::cutoff, 1234.0f);
        setParam (a, ParamID::lfoShape, 3.0f);
        setParam (a, ParamID::position, 0.37f);
        a.setPattern (0x5a5u);

        juce::MemoryBlock state;
        a.getStateInformation (state);
        std::cout << "  saved state is " << state.getSize() / 1024 << " KB" << std::endl;

        GizmoProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());

        auto maxDiff = [] (const Wavetable* x, const Wavetable* y)
        {
            if (x == nullptr || y == nullptr)
                return 1.0f;

            float d = 0.0f;

            for (size_t i = 0; i < x->frames.size(); ++i)
                d = std::max (d, std::abs (x->frames[i] - y->frames[i]));

            return d;
        };

        check (maxDiff (a.getTable (GizmoProcessor::slotA), b.getTable (GizmoProcessor::slotA)) < 1.0e-4f, "table A survives save and load");
        check (maxDiff (a.getTable (GizmoProcessor::slotB), b.getTable (GizmoProcessor::slotB)) < 1.0e-4f, "table B survives save and load");
        check (b.getTable (GizmoProcessor::slotA)->name == "vowels.wav", "table name survives");
        check (b.getTable (GizmoProcessor::slotA)->source.size() == a.getTable (GizmoProcessor::slotA)->source.size()
                   && std::abs (b.getTable (GizmoProcessor::slotA)->rootNote - a.getTable (GizmoProcessor::slotA)->rootNote) < 0.01f,
               "sample and its pitch survive");
        check (b.getPattern() == 0x5a5u, "A/B pattern survives");
        check (std::abs (b.apvts.getRawParameterValue (ParamID::cutoff)->load() - 1234.0f) < 1.0f, "cutoff survives");
        check (juce::roundToInt (b.apvts.getRawParameterValue (ParamID::lfoShape)->load()) == 3, "LFO shape survives");
    }

    std::cout << "Editor" << std::endl;
    {
        GizmoProcessor p;
        p.replaceTable (GizmoProcessor::slotA, tableFrom (voice, sr, ImportMode::cycles, "wokal_aeiou.wav"));
        p.replaceTable (GizmoProcessor::slotB, tableFrom (saw, sr, ImportMode::cycles, pl ("piła_filtr.wav")));
        setParam (p, ParamID::position, 0.42f);
        setParam (p, ParamID::cutoff, 2400.0f);
        setParam (p, ParamID::resonance, 0.35f);
        setParam (p, ParamID::lfoToPos, 0.3f);
        setParam (p, ParamID::offsetB, 0.25f);
        setParam (p, ParamID::velToPos, 0.3f);
        render (p, 0.5, { { 0.0, 1.0, 48, 0.5f }, { 0.0, 1.0, 55, 0.9f }, { 0.0, 1.0, 60, 0.2f } }, {}, rsr);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (GizmoEditor::kDesignWidth, GizmoEditor::kDesignHeight);

        if (auto* e = dynamic_cast<GizmoEditor*> (editor.get()))
            e->updateDisplay();

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = outDir.getChildFile ("editor.png");
        file.deleteFile();

        if (auto stream = file.createOutputStream())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, *stream);
        }

        check (file.existsAsFile(), "editor screenshot written");
    }

    std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " check(s) failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
