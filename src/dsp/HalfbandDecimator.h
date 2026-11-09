#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace gizmo
{
/** Halves the sample rate with a 125-tap linear-phase half-band FIR
    (Kaiser window, about 80 dB of stop-band attenuation). */
class HalfbandDecimator
{
public:
    static constexpr int kHalfLength = 62;
    static constexpr int kLength = 2 * kHalfLength + 1;
    static constexpr int kLatency = kHalfLength / 2; // in output samples

    HalfbandDecimator()
    {
        constexpr double beta = 8.0;
        constexpr double pi = 3.14159265358979323846;

        auto besselI0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;

            for (int k = 1; k < 64; ++k)
            {
                const auto t = x / (2.0 * k);
                term *= t * t;
                sum += term;

                if (term < 1.0e-14 * sum)
                    break;
            }

            return sum;
        };

        const auto i0Beta = besselI0 (beta);
        std::array<double, kLength> h {};
        double sum = 0.0;

        for (int k = 0; k < kLength; ++k)
        {
            const auto n = k - kHalfLength;
            const auto ideal = n == 0 ? 0.5 : std::sin (pi * n / 2.0) / (pi * n);
            const auto r = (double) n / (double) kHalfLength;
            const auto w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0Beta;
            h[(size_t) k] = ideal * w;
            sum += h[(size_t) k];
        }

        for (int k = 0; k < kLength; ++k)
        {
            if (std::abs (h[(size_t) k]) > 1.0e-12)
            {
                taps[(size_t) numTaps] = (float) (h[(size_t) k] / sum);
                tapIndex[(size_t) numTaps] = k;
                ++numTaps;
            }
        }

        reset();
    }

    void reset() noexcept
    {
        history.fill (0.0f);
        writePos = 0;
    }

    /** 'in' holds 2 * numOut samples at the high rate. */
    void process (const float* in, float* out, int numOut) noexcept
    {
        for (int i = 0; i < numOut; ++i)
        {
            push (in[2 * i]);
            push (in[2 * i + 1]);

            const auto* window = history.data() + writePos; // oldest ... newest
            float acc = 0.0f;

            for (int t = 0; t < numTaps; ++t)
                acc += taps[(size_t) t] * window[tapIndex[(size_t) t]];

            out[i] = acc;
        }
    }

private:
    void push (float x) noexcept
    {
        history[(size_t) writePos] = x;
        history[(size_t) (writePos + kLength)] = x;

        if (++writePos == kLength)
            writePos = 0;
    }

    std::array<float, 2 * kLength> history {};
    std::array<float, kLength> taps {};
    std::array<int, kLength> tapIndex {};
    int numTaps = 0, writePos = 0;
};
} // namespace gizmo
