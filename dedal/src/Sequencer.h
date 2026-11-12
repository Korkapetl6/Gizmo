#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <cmath>
#include <vector>

namespace dedal
{
inline juce::String pl (const char* utf8) { return juce::String::fromUTF8 (utf8); }

constexpr int kSteps = 8;     // SEQ1 and SEQ2
constexpr int kStepsC = 16;   // SEQ C, the rhythm sequencer

inline int wrap (juce::int64 value, int modulo) noexcept
{
    if (modulo <= 0)
        return 0;

    const auto r = value % modulo;
    return (int) (r < 0 ? r + modulo : r);
}

inline juce::int64 floorDiv (juce::int64 a, juce::int64 b) noexcept
{
    const auto q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

//==============================================================================
// Quantizer: the sixteen modes of the original instrument.

struct Scale
{
    const char* name;
    std::vector<int> notes; // inside one period, ascending, starting at 0
    int period;             // 0 = no quantizing
};

inline const std::array<Scale, 16>& getScales()
{
    static const std::array<Scale, 16> scales { {
        { "Bez kwantyzacji", {}, 0 },
        { "Chromatyczna", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 },
        { "Durowa", { 0, 2, 4, 5, 7, 9, 11 }, 12 },
        { "Pentatonika", { 0, 2, 4, 7, 9 }, 12 },
        { "Moll melodyczna", { 0, 2, 3, 5, 7, 9, 11 }, 12 },
        { "Moll harmoniczna", { 0, 2, 3, 5, 7, 8, 11 }, 12 },
        { "Zmniejszona 6", { 0, 2, 4, 5, 7, 8, 9, 11 }, 12 },
        { "Całotonowa", { 0, 2, 4, 6, 8, 10 }, 12 },
        { "Hirajoshi", { 0, 2, 3, 7, 8 }, 12 },
        { "7sus4", { 0, 5, 7, 10 }, 12 },
        { "Maj7", { 0, 4, 7, 11 }, 12 },
        { "Maj13", { 0, 2, 4, 7, 9, 11 }, 12 },
        { "Moll7", { 0, 3, 7, 10 }, 12 },
        { "Moll11", { 0, 2, 3, 5, 7, 10 }, 12 },
        { "Hang drum", { 0, 7, 8, 10, 12, 14, 15, 17, 19 }, 24 },
        { "Tercje małe", { 0, 3, 6, 9 }, 12 },
    } };
    return scales;
}

inline juce::StringArray getScaleNames()
{
    juce::StringArray names;

    for (const auto& s : getScales())
        names.add (pl (s.name));

    return names;
}

/** Snaps 'semitones' (relative to the root) to the nearest note of the scale. */
inline float quantize (int scaleIndex, float semitones) noexcept
{
    const auto& scale = getScales()[(size_t) juce::jlimit (0, 15, scaleIndex)];

    if (scale.period == 0)
        return semitones;

    const auto period = (float) scale.period;
    const auto octave = std::floor (semitones / period);
    const auto r = semitones - octave * period;
    auto best = 0.0f, distance = 1.0e9f;

    for (const auto n : scale.notes)
    {
        if (std::abs ((float) n - r) < distance)
        {
            distance = std::abs ((float) n - r);
            best = (float) n;
        }
    }

    if (std::abs (period - r) < distance)
        best = period;

    return octave * period + best;
}

//==============================================================================
// Clock

inline const juce::StringArray& getRateNames()
{
    static const juce::StringArray names { "1/1", "1/2", "1/4", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32" };
    return names;
}

inline double rateToBeats (int index) noexcept
{
    static constexpr double beats[] = { 4.0, 2.0, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125 };
    return beats[juce::jlimit (0, 9, index)];
}

/** Where steps fall in beats. Swing pushes every second step late, 0 = straight, 1 = MPC-style 75 %. */
struct Timing
{
    double stepBeats = 0.25;
    float swing = 0.0f;

    double oddAt() const noexcept { return 2.0 * stepBeats * (0.5 + 0.25 * (double) swing); }

    double startOf (juce::int64 n) const noexcept
    {
        const auto pair = floorDiv (n, 2);
        return (double) pair * 2.0 * stepBeats + ((n - pair * 2) != 0 ? oddAt() : 0.0);
    }

    juce::int64 firstAtOrAfter (double beat) const noexcept
    {
        const auto pair = (juce::int64) std::floor (beat / (2.0 * stepBeats));

        for (auto n = pair * 2 - 1; n <= pair * 2 + 3; ++n)
            if (startOf (n) >= beat - 1.0e-9)
                return n;

        return pair * 2 + 4;
    }
};

//==============================================================================
// Sequencer memory and the operations of the panel

struct Layout
{
    int length[2] = { 8, 8 };
    bool chain = false;
    int capacity = kSteps; // buffer slots per sequencer: 8, or 16 where the long mode exists

    int lengthOf (int k) const noexcept { return chain ? length[0] + length[1] : length[k]; }

    /** Buffer slot of position 'h' (0 .. lengthOf (k) - 1) of sequencer k. */
    int slot (int k, int h) const noexcept
    {
        if (chain)
            return h < length[0] ? h : capacity + (h - length[0]);

        return k * capacity + h;
    }
};

enum Direction { forward = 0, backward, pendulum, randomOrder };

struct SeqState
{
    // SEQ1 (slots 0-7) and SEQ2 (slots 8-15)
    std::array<bool, 32> bits {};
    std::array<float, 32> values {}; // -1 .. 1, the random voltage of each bit
    std::array<int, 2> writeOffset {}, rotation {};
    int chainOffset = 0;

    // SEQ C: triggers with accents and a chance each, plus a value for the logic it drives
    std::array<bool, kStepsC> bitsC {}, accentC {};
    std::array<float, kStepsC> chanceC {}, valueC {};

    // Playing: index 2 is SEQ C
    std::array<int, 3> nextHead { 0, 0, 0 }, head { 0, 0, 0 };
    std::array<bool, 3> played { false, false, false };
    std::array<int, 2> dir { 1, 1 };
    std::array<bool, 2> reversed { false, false };
    std::array<int, 2> lengthOverride { 0, 0 }; // set by SEQ C, 0 = the knob's length
    std::array<float, 2> chanceMod { 0.0f, 0.0f };
};

struct StepResult
{
    bool on = false, accent = false, cycleStart = false;
    float value = 0.0f;
    int head = 0;
};

inline float corruptNote (float c) noexcept { return c <= 0.5f ? 0.5f * c : 0.25f + 0.5f * (c - 0.5f); }
inline float corruptFlip (float c) noexcept { return c <= 0.5f ? 0.0f : (c - 0.5f); }

class Sequencers
{
public:
    SeqState state;
    juce::Random random { 12345 };

    float newValue() { return random.nextFloat() * 2.0f - 1.0f; }

    int currentHead (const Layout& layout, int k) const noexcept
    {
        return wrap (state.played[(size_t) k] ? state.head[(size_t) k] : state.nextHead[(size_t) k], layout.lengthOf (k));
    }

    int writeHead (const Layout& layout, int k) const noexcept
    {
        return wrap (currentHead (layout, k) + state.writeOffset[(size_t) k], layout.lengthOf (k));
    }

    /** Plays the next step of SEQ1 or SEQ2, lets CORRUPT work under the write head, and moves on. */
    StepResult step (const Layout& layout, int k, float corrupt, int direction, float chance)
    {
        const auto length = layout.lengthOf (k);
        const auto h = wrap (state.nextHead[(size_t) k], length);
        const auto index = (size_t) layout.slot (k, h);

        StepResult result;
        result.head = h;
        result.value = state.values[index];
        result.on = state.bits[index] && (chance >= 0.999f || random.nextFloat() < chance);
        result.cycleStart = h == 0;

        state.head[(size_t) k] = h;
        state.played[(size_t) k] = true;

        // Up to noon: notes change (to ~25 %). Past noon: up to ~50 %, and bits start to flip, up to ~50 %.
        const auto c = juce::jlimit (0.0f, 1.0f, corrupt);

        if (c > 0.0f)
        {
            const auto w = (size_t) layout.slot (k, wrap (h + state.writeOffset[(size_t) k], length));

            if (state.bits[w] && random.nextFloat() < corruptNote (c))
                state.values[w] = newValue();

            if (random.nextFloat() < corruptFlip (c))
                flipSlot (w);
        }

        // Where to go next
        const auto sign = state.reversed[(size_t) k] ? -1 : 1;
        int next;

        switch (direction)
        {
            case backward:
                next = h - sign;
                break;

            case pendulum:
            {
                auto& d = state.dir[(size_t) k];
                next = h + d;

                if (next >= length)
                {
                    d = -1;
                    next = std::max (0, length - 2);
                }
                else if (next < 0)
                {
                    d = 1;
                    next = std::min (length - 1, 1);
                }

                break;
            }

            case randomOrder:
                next = random.nextInt (std::max (1, length));
                break;

            default:
                next = h + sign;
                break;
        }

        state.nextHead[(size_t) k] = wrap (next, length);
        return result;
    }

    /** Plays the next step of SEQ C: a trigger if its bit is on and its chance comes up. */
    StepResult stepC (int length, float corrupt)
    {
        const auto h = wrap (state.nextHead[2], length);
        const auto i = (size_t) h;

        StepResult result;
        result.head = h;
        result.cycleStart = h == 0;
        result.on = state.bitsC[i] && random.nextFloat() < state.chanceC[i];
        result.accent = result.on && state.accentC[i];
        result.value = state.valueC[i];

        state.head[2] = h;
        state.played[2] = true;

        const auto c = juce::jlimit (0.0f, 1.0f, corrupt);

        if (c > 0.0f)
        {
            // Rhythmic changes: chances and values drift, past noon triggers and accents come and go.
            if (random.nextFloat() < corruptNote (c))
            {
                state.valueC[i] = newValue();
                state.chanceC[i] = juce::jlimit (0.2f, 1.0f, state.chanceC[i] + (random.nextFloat() - 0.5f) * 0.6f);
            }

            if (random.nextFloat() < corruptFlip (c))
                flipC (i);

            if (random.nextFloat() < corruptFlip (c) * 0.5f)
                state.accentC[i] = ! state.accentC[i];
        }

        state.nextHead[2] = wrap (h + 1, length);
        return result;
    }

    void flipSlot (size_t index)
    {
        state.bits[index] = ! state.bits[index];

        if (state.bits[index])
            state.values[index] = newValue();
    }

    void flipC (size_t index)
    {
        state.bitsC[index] = ! state.bitsC[index];

        if (state.bitsC[index])
        {
            state.valueC[index] = newValue();
            state.chanceC[index] = std::max (state.chanceC[index], 0.6f);
        }
    }

    void bitFlip (const Layout& layout, int k) { flipSlot ((size_t) layout.slot (k, writeHead (layout, k))); }

    /** Rotates right by one step. In CHAIN mode SEQ2's shift moves its play head instead: one step ahead of SEQ1. */
    void bitShift (const Layout& layout, int k)
    {
        if (layout.chain && k == 1)
        {
            const auto length = layout.lengthOf (1);
            state.chainOffset = wrap (state.chainOffset + 1, length);
            state.nextHead[1] = wrap (state.nextHead[1] + 1, length);
            return;
        }

        rotate (layout, k, 1);
        state.rotation[(size_t) k] = wrap (state.rotation[(size_t) k] + 1, layout.lengthOf (k));
    }

    /** Turns the bits back to where they were before any BIT SHIFT (in CHAIN, SEQ2 snaps back to SEQ1). */
    void unshift (const Layout& layout, int k)
    {
        const auto length = layout.lengthOf (k);

        if (layout.chain && k == 1)
        {
            state.nextHead[1] = wrap (state.nextHead[1] - state.chainOffset, length);
            state.chainOffset = 0;
            return;
        }

        rotate (layout, k, length - wrap (state.rotation[(size_t) k], length));
        state.rotation[(size_t) k] = 0;
    }

    void setChainOffset (const Layout& layout, int offset)
    {
        const auto length = layout.lengthOf (1);
        offset = wrap (offset, length);
        state.nextHead[1] = wrap (state.nextHead[1] + offset - state.chainOffset, length);
        state.chainOffset = offset;
    }

    void rotate (const Layout& layout, int k, int by)
    {
        const auto length = layout.lengthOf (k);
        std::array<bool, 32> bits {};
        std::array<float, 32> values {};

        for (int h = 0; h < length; ++h)
        {
            const auto from = (size_t) layout.slot (k, h);
            bits[(size_t) h] = state.bits[from];
            values[(size_t) h] = state.values[from];
        }

        for (int h = 0; h < length; ++h)
        {
            const auto to = (size_t) layout.slot (k, wrap (h + by, length));
            state.bits[to] = bits[(size_t) h];
            state.values[to] = values[(size_t) h];
        }
    }

    /** Back to bit 1 on the next step (in CHAIN, SEQ2 keeps its distance from SEQ1). */
    void reset (const Layout& layout, int k)
    {
        if (k == 2)
            state.nextHead[2] = 0;
        else
            state.nextHead[(size_t) k] = layout.chain && k == 1 ? wrap (state.chainOffset, layout.lengthOf (1)) : 0;

        if (k < 2)
            state.dir[(size_t) k] = 1;
    }

    void advance (const Layout& layout, int k, int lengthC = kStepsC)
    {
        const auto length = k == 2 ? lengthC : layout.lengthOf (k);
        state.nextHead[(size_t) k] = wrap (state.nextHead[(size_t) k] + 1, length);
        state.head[(size_t) k] = wrap (state.head[(size_t) k] + 1, length);
    }

    void reverse (int k)
    {
        state.reversed[(size_t) k] = ! state.reversed[(size_t) k];
        state.dir[(size_t) k] = -state.dir[(size_t) k];
    }

    void moveWriteHead (const Layout& layout, int k, bool resetIt)
    {
        state.writeOffset[(size_t) k] = resetIt ? 0 : wrap (state.writeOffset[(size_t) k] + 1, layout.lengthOf (k));
    }

    void randomise (int k, float density = 0.65f, int capacity = kSteps)
    {
        for (int i = 0; i < capacity; ++i)
        {
            const auto index = (size_t) (k * capacity + i);
            state.bits[index] = random.nextFloat() < density;
            state.values[index] = newValue();
        }
    }

    void randomiseC (float density = 0.5f)
    {
        for (size_t i = 0; i < (size_t) kStepsC; ++i)
        {
            state.bitsC[i] = random.nextFloat() < density || i == 0;
            state.accentC[i] = random.nextFloat() < 0.28f;
            state.chanceC[i] = random.nextFloat() < 0.65f ? 1.0f : 0.3f + 0.6f * random.nextFloat();
            state.valueC[i] = newValue();
        }
    }

    void clear (int k, int capacity = kSteps)
    {
        if (k == 2)
        {
            state.bitsC.fill (false);
            state.accentC.fill (false);
            return;
        }

        for (int i = 0; i < capacity; ++i)
            state.bits[(size_t) (k * capacity + i)] = false;
    }

    /** What MIDI Start does: all play heads to bit 1, the logic of SEQ C forgotten. */
    void restart()
    {
        state.nextHead = { 0, state.chainOffset, 0 };
        state.played = { false, false, false };
        state.dir = { 1, 1 };
        state.reversed = { false, false };
        state.lengthOverride = { 0, 0 };
        state.chanceMod = { 0.0f, 0.0f };
    }
};

//==============================================================================
// Saving

namespace detail
{
template <typename T, size_t N>
juce::String join (const std::array<T, N>& values)
{
    juce::StringArray parts;

    for (const auto v : values)
    {
        if constexpr (std::is_same_v<T, bool>)
            parts.add (v ? "1" : "0");
        else if constexpr (std::is_floating_point_v<T>)
            parts.add (juce::String (v, 5));
        else
            parts.add (juce::String (v));
    }

    return parts.joinIntoString (",");
}

template <typename T, size_t N>
void split (const juce::var& text, std::array<T, N>& values, T fallback)
{
    const auto parts = juce::StringArray::fromTokens (text.toString(), ",", {});

    for (size_t i = 0; i < N; ++i)
    {
        if ((int) i >= parts.size())
            values[i] = fallback;
        else if constexpr (std::is_same_v<T, bool>)
            values[i] = parts[(int) i] == "1";
        else if constexpr (std::is_floating_point_v<T>)
            values[i] = (T) parts[(int) i].getDoubleValue();
        else
            values[i] = (T) parts[(int) i].getIntValue();
    }
}
} // namespace detail

inline juce::ValueTree toValueTree (const SeqState& s, const juce::Identifier& type)
{
    juce::ValueTree tree (type);
    tree.setProperty ("bits", detail::join (s.bits), nullptr);
    tree.setProperty ("values", detail::join (s.values), nullptr);
    tree.setProperty ("write", detail::join (s.writeOffset), nullptr);
    tree.setProperty ("rotation", detail::join (s.rotation), nullptr);
    tree.setProperty ("chainOffset", s.chainOffset, nullptr);
    tree.setProperty ("bitsC", detail::join (s.bitsC), nullptr);
    tree.setProperty ("accentC", detail::join (s.accentC), nullptr);
    tree.setProperty ("chanceC", detail::join (s.chanceC), nullptr);
    tree.setProperty ("valueC", detail::join (s.valueC), nullptr);
    return tree;
}

inline bool fromValueTree (const juce::ValueTree& tree, SeqState& s)
{
    if (! tree.isValid())
        return false;

    detail::split (tree.getProperty ("bits"), s.bits, false);
    detail::split (tree.getProperty ("values"), s.values, 0.0f);
    detail::split (tree.getProperty ("write"), s.writeOffset, 0);
    detail::split (tree.getProperty ("rotation"), s.rotation, 0);
    s.chainOffset = tree.getProperty ("chainOffset", 0);
    detail::split (tree.getProperty ("bitsC"), s.bitsC, false);
    detail::split (tree.getProperty ("accentC"), s.accentC, false);
    detail::split (tree.getProperty ("chanceC"), s.chanceC, 1.0f);
    detail::split (tree.getProperty ("valueC"), s.valueC, 0.0f);

    for (auto& v : s.values)
        v = juce::jlimit (-1.0f, 1.0f, v);

    return true;
}
} // namespace dedal
