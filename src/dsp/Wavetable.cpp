#include "Wavetable.h"
#include "../Params.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gizmo
{
namespace
{
constexpr int kFftOrder = 11;
static_assert ((1 << kFftOrder) == kFrameSize, "kFrameSize must match kFftOrder");

inline float sampleAt (const std::vector<float>& x, int i) noexcept
{
    return (i >= 0 && i < (int) x.size()) ? x[(size_t) i] : 0.0f;
}

inline double sinc (double v) noexcept
{
    if (std::abs (v) < 1.0e-9)
        return 1.0;

    const auto p = juce::MathConstants<double>::pi * v;
    return std::sin (p) / p;
}

/** Band-limited read at a fractional position (Lanczos kernel, 16 taps). */
float readInterpolated (const std::vector<float>& x, double pos) noexcept
{
    constexpr int a = 8;
    const auto base = (int) std::floor (pos);
    const auto frac = pos - (double) base;

    if (frac < 1.0e-7)
        return sampleAt (x, base);

    double sum = 0.0, weights = 0.0;

    for (int k = -a + 1; k <= a; ++k)
    {
        const auto d = (double) k - frac;
        const auto w = sinc (d) * sinc (d / a);
        sum += w * (double) sampleAt (x, base + k);
        weights += w;
    }

    return (float) (sum / weights);
}

/** Zero-phase one-pole high-pass at about 8 Hz, so zero crossings are reliable. */
void removeDcDrift (std::vector<float>& x, double sampleRate)
{
    const auto r = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 8.0 / sampleRate);

    auto pass = [r] (auto begin, auto end)
    {
        float prevIn = 0.0f, prevOut = 0.0f;

        for (auto it = begin; it != end; ++it)
        {
            const auto in = *it;
            const auto out = in - prevIn + r * prevOut;
            prevIn = in;
            prevOut = out;
            *it = out;
        }
    };

    pass (x.begin(), x.end());
    pass (x.rbegin(), x.rend());
}

/** The part of the recording that is louder than -60 dB below its peak. */
std::pair<int, int> findAudibleRegion (const std::vector<float>& x)
{
    const auto n = (int) x.size();
    float peak = 0.0f;

    for (auto v : x)
        peak = std::max (peak, std::abs (v));

    if (peak < 1.0e-6f)
        return { 0, 0 };

    const auto threshold = peak * 0.001f;
    constexpr int block = 256;
    int first = -1, last = -1;

    for (int b = 0; b < n; b += block)
    {
        float m = 0.0f;

        for (int i = b; i < std::min (n, b + block); ++i)
            m = std::max (m, std::abs (x[(size_t) i]));

        if (m >= threshold)
        {
            if (first < 0)
                first = b;

            last = std::min (n, b + block);
        }
    }

    return first < 0 ? std::pair<int, int> { 0, 0 } : std::pair<int, int> { first, last };
}

/** YIN period detector; the correlation part is computed with an FFT. */
class PeriodDetector
{
public:
    explicit PeriodDetector (double sampleRate)
    {
        minLag = std::max (2, (int) std::floor (sampleRate / 2000.0)); // up to 2 kHz
        maxLag = std::max (minLag + 8, (int) std::ceil (sampleRate / 40.0)); // down to 40 Hz
        window = juce::nextPowerOfTwo (maxLag);

        int order = 1;

        while ((1 << order) < window + maxLag)
            ++order;

        fftSize = 1 << order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        bufA.resize ((size_t) fftSize * 2);
        bufB.resize ((size_t) fftSize * 2);
        prefix.resize ((size_t) (window + maxLag + 1));
        diff.resize ((size_t) maxLag + 2);
        cmnd.resize ((size_t) maxLag + 2);
    }

    int getWindowSize() const noexcept { return window; }

    struct Estimate
    {
        double period = 0.0;     // in samples, 0 if the audio isn't periodic
        double aperiodicity = 1.0; // YIN's normalised difference at the period: 0 = perfectly periodic
        double rms = 0.0;
    };

    /** Period of the audio starting at 'start'. */
    Estimate detect (const std::vector<float>& x, int start)
    {
        Estimate result;
        const int span = window + maxLag;
        std::fill (bufA.begin(), bufA.end(), 0.0f);
        std::fill (bufB.begin(), bufB.end(), 0.0f);

        for (int i = 0; i < span; ++i)
        {
            const auto v = sampleAt (x, start + i);
            bufB[(size_t) i] = v;

            if (i < window)
                bufA[(size_t) i] = v;
        }

        double e0 = 0.0;

        for (int i = 0; i < window; ++i)
            e0 += (double) bufA[(size_t) i] * (double) bufA[(size_t) i];

        result.rms = std::sqrt (e0 / window);

        if (e0 < 1.0e-9 * window)
            return result;

        prefix[0] = 0.0;

        for (int i = 0; i < span; ++i)
            prefix[(size_t) i + 1] = prefix[(size_t) i] + (double) bufB[(size_t) i] * (double) bufB[(size_t) i];

        fft->performRealOnlyForwardTransform (bufA.data(), true);
        fft->performRealOnlyForwardTransform (bufB.data(), true);

        for (int k = 0; k <= fftSize / 2; ++k)
        {
            const auto ar = bufA[(size_t) (2 * k)], ai = bufA[(size_t) (2 * k + 1)];
            const auto br = bufB[(size_t) (2 * k)], bi = bufB[(size_t) (2 * k + 1)];
            bufB[(size_t) (2 * k)]     = ar * br + ai * bi; // conj(A) * B
            bufB[(size_t) (2 * k + 1)] = ar * bi - ai * br;
        }

        fft->performRealOnlyInverseTransform (bufB.data()); // bufB[tau] = sum a[j] b[j + tau]

        diff[0] = 0.0;

        for (int tau = 1; tau <= maxLag; ++tau)
        {
            const auto energyAtTau = prefix[(size_t) (tau + window)] - prefix[(size_t) tau];
            diff[(size_t) tau] = std::max (0.0, e0 + energyAtTau - 2.0 * (double) bufB[(size_t) tau]);
        }

        cmnd[0] = 1.0;
        double running = 0.0;

        for (int tau = 1; tau <= maxLag; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0 ? diff[(size_t) tau] * tau / running : 1.0;
        }

        constexpr double threshold = 0.15, weakThreshold = 0.3;
        int best = -1;

        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 < maxLag && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                    ++tau;

                best = tau;
                break;
            }
        }

        if (best < 0)
        {
            int arg = minLag;

            for (int tau = minLag; tau < maxLag; ++tau)
                if (cmnd[(size_t) tau] < cmnd[(size_t) arg])
                    arg = tau;

            if (cmnd[(size_t) arg] > weakThreshold)
                return result;

            best = arg;
        }

        auto period = (double) best;

        if (best > 1 && best < maxLag)
        {
            const auto a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
            const auto denom = a - 2.0 * b + c;

            if (std::abs (denom) > 1.0e-12)
                period += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom);
        }

        result.period = period;
        result.aperiodicity = cmnd[(size_t) best];
        return result;
    }

private:
    int minLag = 0, maxLag = 0, window = 0, fftSize = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> bufA, bufB;
    std::vector<double> prefix, diff, cmnd;
};

/** Fractional position of the rising zero crossing closest to 'target', or 'target' if there is none. */
double findRisingZeroCrossing (const std::vector<float>& x, double target, double radius)
{
    const auto lo = std::max (1, (int) std::floor (target - radius));
    const auto hi = std::min ((int) x.size() - 1, (int) std::ceil (target + radius));
    auto best = target;
    auto bestDistance = std::numeric_limits<double>::max();

    for (int i = lo; i <= hi; ++i)
    {
        const auto a = x[(size_t) i - 1], b = x[(size_t) i];

        if (a < 0.0f && b >= 0.0f)
        {
            const auto pos = (double) (i - 1) + (double) (a / (a - b));
            const auto distance = std::abs (pos - target);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = pos;
            }
        }
    }

    return best;
}

/** Makes the end of a frame meet its start, so it loops without a jump. */
void removeLoopDrift (float* frame, float valueAfterEnd)
{
    const auto drift = valueAfterEnd - frame[0];

    for (int i = 0; i < kFrameSize; ++i)
        frame[i] -= drift * (float) i / (float) kFrameSize;
}

/** One cycle of the recording, resampled to kFrameSize samples. */
void extractCycle (const std::vector<float>& x, double centre, double period, float* frame)
{
    const auto start = findRisingZeroCrossing (x, centre - period * 0.5, period * 0.5);
    const auto step = period / (double) kFrameSize;

    for (int i = 0; i < kFrameSize; ++i)
        frame[i] = readInterpolated (x, start + step * i);

    removeLoopDrift (frame, readInterpolated (x, start + period));
}

/** A fixed piece of the recording, kFrameSize samples long. */
void extractChunk (const std::vector<float>& x, double centre, float* frame)
{
    const auto n = (int) x.size();
    const auto start = findRisingZeroCrossing (x, centre - kFrameSize * 0.5, kFrameSize * 0.25);
    auto s = (int) std::round (start);
    s = n >= kFrameSize ? juce::jlimit (0, n - kFrameSize, s) : 0;

    for (int i = 0; i < kFrameSize; ++i)
        frame[i] = sampleAt (x, s + i);

    removeLoopDrift (frame, sampleAt (x, s + kFrameSize));
}

void removeMean (float* frame)
{
    double sum = 0.0;

    for (int i = 0; i < kFrameSize; ++i)
        sum += frame[i];

    const auto mean = (float) (sum / kFrameSize);

    for (int i = 0; i < kFrameSize; ++i)
        frame[i] -= mean;
}

/** Circular cross-correlation, used to line up the phase of neighbouring frames. */
class FrameAligner
{
public:
    FrameAligner() : fft (kFftOrder), a ((size_t) kFrameSize * 2), b ((size_t) kFrameSize * 2), tmp ((size_t) kFrameSize) {}

    void alignTo (const float* reference, float* frame)
    {
        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);
        std::copy (reference, reference + kFrameSize, a.begin());
        std::copy (frame, frame + kFrameSize, b.begin());

        fft.performRealOnlyForwardTransform (a.data(), true);
        fft.performRealOnlyForwardTransform (b.data(), true);

        for (int k = 0; k <= kFrameSize / 2; ++k)
        {
            const auto ar = a[(size_t) (2 * k)], ai = a[(size_t) (2 * k + 1)];
            const auto br = b[(size_t) (2 * k)], bi = b[(size_t) (2 * k + 1)];
            b[(size_t) (2 * k)]     = ar * br + ai * bi;
            b[(size_t) (2 * k + 1)] = ar * bi - ai * br;
        }

        fft.performRealOnlyInverseTransform (b.data());

        int shift = 0;

        for (int i = 1; i < kFrameSize; ++i)
            if (b[(size_t) i] > b[(size_t) shift])
                shift = i;

        if (shift == 0)
            return;

        for (int i = 0; i < kFrameSize; ++i)
            tmp[(size_t) i] = frame[(i + shift) & (kFrameSize - 1)];

        std::copy (tmp.begin(), tmp.end(), frame);
    }

private:
    juce::dsp::FFT fft;
    std::vector<float> a, b, tmp;
};

/** Rotates every frame by the same amount so the loop point sits where all frames are quiet.
    That keeps the jumps small when frames are switched at the loop point. */
void rotateToQuietestPoint (std::vector<float>& frames)
{
    std::vector<double> cost ((size_t) kFrameSize, 0.0);

    for (int f = 0; f < kNumFrames; ++f)
        for (int i = 0; i < kFrameSize; ++i)
            cost[(size_t) i] += std::abs (frames[(size_t) f * kFrameSize + (size_t) i]);

    const auto shift = (int) std::distance (cost.begin(), std::min_element (cost.begin(), cost.end()));

    if (shift == 0)
        return;

    std::vector<float> tmp ((size_t) kFrameSize);

    for (int f = 0; f < kNumFrames; ++f)
    {
        auto* frame = frames.data() + (size_t) f * kFrameSize;

        for (int i = 0; i < kFrameSize; ++i)
            tmp[(size_t) i] = frame[(i + shift) & (kFrameSize - 1)];

        std::copy (tmp.begin(), tmp.end(), frame);
    }
}

/** Evens out the loudness of the frames (within +-18 dB) and normalises the peak to 1. */
void equaliseAndNormalise (std::vector<float>& frames)
{
    std::vector<double> rms ((size_t) kNumFrames, 0.0);
    std::vector<double> audible;

    for (int f = 0; f < kNumFrames; ++f)
    {
        const auto* frame = frames.data() + (size_t) f * kFrameSize;
        double sum = 0.0;

        for (int i = 0; i < kFrameSize; ++i)
            sum += (double) frame[i] * (double) frame[i];

        rms[(size_t) f] = std::sqrt (sum / kFrameSize);

        if (rms[(size_t) f] > 1.0e-7)
            audible.push_back (rms[(size_t) f]);
    }

    if (audible.empty())
        return;

    std::nth_element (audible.begin(), audible.begin() + (long) audible.size() / 2, audible.end());
    const auto median = audible[audible.size() / 2];

    for (int f = 0; f < kNumFrames; ++f)
    {
        if (rms[(size_t) f] <= 1.0e-7)
            continue;

        const auto gain = (float) juce::jlimit (0.125, 8.0, median / rms[(size_t) f]);
        auto* frame = frames.data() + (size_t) f * kFrameSize;

        for (int i = 0; i < kFrameSize; ++i)
            frame[i] *= gain;
    }

    float peak = 0.0f;

    for (auto v : frames)
        peak = std::max (peak, std::abs (v));

    if (peak > 0.0f)
        for (auto& v : frames)
            v /= peak;
}

/** Band-limited copy of every frame for each octave, so high notes don't alias. */
void buildMips (Wavetable& table)
{
    table.mips.assign ((size_t) kNumMipLevels * kNumFrames * kMipStride, 0.0f);

    juce::dsp::FFT fft (kFftOrder);
    std::vector<float> spectrum ((size_t) kFrameSize * 2), work ((size_t) kFrameSize * 2);

    for (int f = 0; f < kNumFrames; ++f)
    {
        std::fill (spectrum.begin(), spectrum.end(), 0.0f);
        std::copy (table.getFrame (f), table.getFrame (f) + kFrameSize, spectrum.begin());
        fft.performRealOnlyForwardTransform (spectrum.data(), true);

        for (int level = 0; level < kNumMipLevels; ++level)
        {
            const auto maxHarmonic = level == 0 ? kFrameSize / 2 - 1 : (kFrameSize / 2) >> level;
            std::fill (work.begin(), work.end(), 0.0f);

            for (int k = 1; k <= maxHarmonic; ++k)
            {
                work[(size_t) (2 * k)]     = spectrum[(size_t) (2 * k)];
                work[(size_t) (2 * k + 1)] = spectrum[(size_t) (2 * k + 1)];
            }

            fft.performRealOnlyInverseTransform (work.data());

            auto* dest = const_cast<float*> (table.getMip (level, f));
            std::copy (work.begin(), work.begin() + kFrameSize, dest);
            dest[kFrameSize] = dest[0];
        }
    }
}
} // namespace

std::unique_ptr<Wavetable> buildWavetableFromFrames (const float* frames)
{
    auto table = std::make_unique<Wavetable>();
    table->frames.assign (frames, frames + (size_t) kNumFrames * kFrameSize);
    buildMips (*table);
    return table;
}

ImportResult buildWavetableFromAudio (std::vector<float> x, double sampleRate, ImportMode mode)
{
    ImportResult result;

    if (x.empty() || sampleRate <= 0.0)
    {
        result.error = pl ("Plik jest pusty.");
        return result;
    }

    removeDcDrift (x, sampleRate);
    const auto [first, last] = findAudibleRegion (x);

    if (last - first < 32)
    {
        result.error = pl ("W pliku jest sama cisza.");
        return result;
    }

    const auto regionStart = (double) first;
    const auto regionLength = (double) (last - first);

    PeriodDetector detector (sampleRate);
    FrameAligner aligner;
    std::vector<float> frames ((size_t) kNumFrames * kFrameSize, 0.0f);
    int pitched = 0;

    // Loudness of the whole region, so quiet spots (breaths, tails) can be avoided.
    double regionEnergy = 0.0;

    for (int i = first; i < last; ++i)
        regionEnergy += (double) x[(size_t) i] * (double) x[(size_t) i];

    const auto regionRms = std::sqrt (regionEnergy / regionLength);
    const auto spacing = regionLength / (double) kNumFrames;

    for (int f = 0; f < kNumFrames; ++f)
    {
        const auto centre = regionStart + ((double) f + 0.5) * spacing;
        auto* frame = frames.data() + (size_t) f * kFrameSize;
        double period = 0.0, cycleCentre = centre;

        if (mode == ImportMode::cycles)
        {
            // Look around the frame's slot for the steadiest, loudest spot, so frames don't land
            // on note changes or breaths. The search stays inside the slot to keep the order.
            auto bestScore = std::numeric_limits<double>::max();

            for (int j = -4; j <= 4; ++j)
            {
                const auto candidate = centre + (double) j * spacing / 8.0;
                const auto estimate = detector.detect (x, (int) std::round (candidate - detector.getWindowSize() * 0.5));

                if (estimate.period <= 0.0)
                    continue;

                const auto quietness = 1.0 - std::min (1.0, estimate.rms / std::max (1.0e-9, regionRms));
                const auto score = estimate.aperiodicity + 0.2 * quietness + 0.01 * std::abs ((double) j);

                if (score < bestScore)
                {
                    bestScore = score;
                    period = estimate.period;
                    cycleCentre = candidate;
                }
            }
        }

        if (period > 0.0)
        {
            extractCycle (x, cycleCentre, period, frame);
            ++pitched;
        }
        else
        {
            extractChunk (x, centre, frame);
        }

        removeMean (frame);

        if (f > 0)
            aligner.alignTo (frame - kFrameSize, frame);
    }

    rotateToQuietestPoint (frames);
    equaliseAndNormalise (frames);

    result.table = buildWavetableFromFrames (frames.data());
    result.table->pitchedFrames = pitched;

    // Keep the audible part of the sample for the B steps, peak-normalised.
    auto& source = result.table->source;
    source.assign (x.begin() + first, x.begin() + last);
    float peak = 0.0f;

    for (auto v : source)
        peak = std::max (peak, std::abs (v));

    if (peak > 0.0f)
        for (auto& v : source)
            v *= 0.9f / peak;

    result.table->sourceRate = sampleRate;

    // The sample's own pitch: the median of the periods found across it.
    std::vector<double> periods;

    for (int k = 0; k < 32; ++k)
    {
        const auto centre = regionStart + ((double) k + 0.5) / 32.0 * regionLength;
        const auto estimate = detector.detect (x, (int) std::round (centre - detector.getWindowSize() * 0.5));

        if (estimate.period > 0.0 && estimate.aperiodicity < 0.15)
            periods.push_back (estimate.period);
    }

    if (periods.size() >= 4)
    {
        std::nth_element (periods.begin(), periods.begin() + (long) periods.size() / 2, periods.end());
        const auto frequency = sampleRate / periods[periods.size() / 2];
        result.table->rootNote = (float) (69.0 + 12.0 * std::log2 (frequency / 440.0));
    }

    return result;
}

std::unique_ptr<Wavetable> makeDefaultWavetable()
{
    std::vector<float> frames ((size_t) kNumFrames * kFrameSize, 0.0f);
    juce::dsp::FFT fft (kFftOrder);
    std::vector<float> work ((size_t) kFrameSize * 2);
    constexpr int maxHarmonic = kFrameSize / 2 - 1;

    for (int f = 0; f < kNumFrames; ++f)
    {
        const auto t = (double) f / (double) (kNumFrames - 1);
        std::fill (work.begin(), work.end(), 0.0f);

        for (int h = 1; h <= maxHarmonic; ++h)
        {
            const auto saw = 1.0 / h;
            const auto square = (h % 2 == 1) ? 1.0 / h : 0.0;
            const auto buzz = 0.5 / std::sqrt ((double) h);
            double amp = 0.0;

            if (t < 1.0 / 3.0)
                amp = h == 1 ? 1.0 : t * 3.0 * saw;
            else if (t < 2.0 / 3.0)
                amp = juce::jmap (t * 3.0 - 1.0, saw, square);
            else
                amp = juce::jmap (t * 3.0 - 2.0, square, buzz);

            work[(size_t) (2 * h + 1)] = (float) (-amp * kFrameSize * 0.5); // sine phase
        }

        fft.performRealOnlyInverseTransform (work.data());
        std::copy (work.begin(), work.begin() + kFrameSize, frames.begin() + (long) f * kFrameSize);
    }

    equaliseAndNormalise (frames);
    auto table = buildWavetableFromFrames (frames.data());
    table->name = pl ("Domyślna tablica: sinus, piła, kwadrat, brzęk");
    table->isDefault = true;
    return table;
}

ImportResult loadWavetableFromFile (const juce::File& file, ImportMode mode, juce::AudioFormatManager& formats)
{
    ImportResult result;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        result.error = pl ("Nie umiem otworzyć tego pliku: ") + file.getFileName();
        return result;
    }

    const auto sampleRate = reader->sampleRate;
    const auto total = std::min (reader->lengthInSamples, (juce::int64) (kMaxImportSeconds * sampleRate));

    if (sampleRate <= 0.0 || total <= 0)
    {
        result.error = pl ("Plik jest pusty: ") + file.getFileName();
        return result;
    }

    const auto channels = (int) std::max (1u, reader->numChannels);
    std::vector<float> mono ((size_t) total, 0.0f);
    constexpr int blockSize = 65536;
    juce::AudioBuffer<float> block (channels, blockSize);

    for (juce::int64 pos = 0; pos < total; pos += blockSize)
    {
        const auto n = (int) std::min<juce::int64> (blockSize, total - pos);
        block.clear();
        reader->read (&block, 0, n, pos, true, true);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* data = block.getReadPointer (ch);

            for (int i = 0; i < n; ++i)
                mono[(size_t) (pos + i)] += data[i] / (float) channels;
        }
    }

    result = buildWavetableFromAudio (std::move (mono), sampleRate, mode);

    if (result.table != nullptr)
    {
        result.table->name = file.getFileName();
        result.table->sourcePath = file.getFullPathName();
    }

    return result;
}
} // namespace gizmo
