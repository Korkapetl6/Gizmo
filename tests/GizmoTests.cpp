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

/** One frame after another, each repeated for 60 ms at 110 Hz: lets you hear the table. */
std::vector<float> tablePreview (const Wavetable& t, double sr)
{
    std::vector<float> out;
    double phase = 0.0;

    for (int f = 0; f < kNumFrames; ++f)
    {
        for (int i = 0; i < (int) (0.06 * sr); ++i)
        {
            const auto* m = t.getMip (3, f);
            const auto x = phase * kFrameSize;
            const auto k = (int) x;
            out.push_back (0.5f * (m[k] + (float) (x - k) * (m[k + 1] - m[k])));
            phase += 110.0 / sr;
            phase -= std::floor (phase);
        }
    }

    return out;
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

Render render (GizmoProcessor& p, double seconds, const std::vector<Note>& notes,
               std::function<void (double)> automation = {}, double sr = 48000.0, int block = 512)
{
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
} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
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

    writeWav (outDir.getChildFile ("table_vowels.wav"), tablePreview (*vowelTable, sr), nullptr, sr);
    writeWav (outDir.getChildFile ("table_saw_sweep.wav"), tablePreview (*sawTable, sr), nullptr, sr);

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

        if (! stepped)
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

    std::cout << "State" << std::endl;
    {
        GizmoProcessor a;
        a.replaceTable (GizmoProcessor::slotA, tableFrom (voice, sr, ImportMode::cycles, "vowels.wav"));
        a.replaceTable (GizmoProcessor::slotB, tableFrom (saw, sr, ImportMode::cycles, "saw.wav"));
        setParam (a, ParamID::cutoff, 1234.0f);
        setParam (a, ParamID::lfoShape, 3.0f);
        setParam (a, ParamID::position, 0.37f);

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
