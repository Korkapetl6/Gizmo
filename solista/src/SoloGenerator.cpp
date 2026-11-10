#include "SoloGenerator.h"

#include <algorithm>
#include <cmath>

namespace solista
{
namespace
{
constexpr int TPB = 24; // ticks per beat: divides into sixteenths, triplets and sextuplets
constexpr int BAR = TPB * 4;
constexpr int kLow = 55, kHigh = 86; // G3 to E6

inline int pc (int pitch) noexcept { return ((pitch % 12) + 12) % 12; }

//==============================================================================
struct ChordDef
{
    int semitones;
    bool minor, seventh;
};

struct ProgressionDef
{
    const char* name;
    std::vector<ChordDef> chords; // one per bar, looped
    bool altScale;                // dorian in minor keys, mixolydian in major keys
};

const std::vector<ProgressionDef>& progressions (bool minorKey)
{
    static const std::vector<ProgressionDef> minorOnes {
        { "i - VII - VI - VII", { { 0, true, false }, { 10, false, false }, { 8, false, false }, { 10, false, false } }, false },
        { "i - VI - III - VII", { { 0, true, false }, { 8, false, false }, { 3, false, false }, { 10, false, false } }, false },
        { "i - iv - VII - III", { { 0, true, false }, { 5, true, false }, { 10, false, false }, { 3, false, false } }, false },
        { "i7 - IV7 (groove)", { { 0, true, true }, { 0, true, true }, { 5, false, true }, { 5, false, true } }, true },
        { "Blues molowy (12 t.)", { { 0, true, true }, { 0, true, true }, { 0, true, true }, { 0, true, true },
                                    { 5, true, true }, { 5, true, true }, { 0, true, true }, { 0, true, true },
                                    { 8, false, false }, { 7, false, true }, { 0, true, true }, { 7, false, true } }, false },
        { "Jeden akord (i)", { { 0, true, false } }, false },
    };

    static const std::vector<ProgressionDef> majorOnes {
        { "I - V - vi - IV", { { 0, false, false }, { 7, false, false }, { 9, true, false }, { 5, false, false } }, false },
        { "I - IV - V - IV", { { 0, false, false }, { 5, false, false }, { 7, false, false }, { 5, false, false } }, false },
        { "I - vi - IV - V", { { 0, false, false }, { 9, true, false }, { 5, false, false }, { 7, false, false } }, false },
        { "I - bVII - IV - I", { { 0, false, false }, { 10, false, false }, { 5, false, false }, { 0, false, false } }, true },
        { "Blues (12 t.)", { { 0, false, true }, { 5, false, true }, { 0, false, true }, { 0, false, true },
                             { 5, false, true }, { 5, false, true }, { 0, false, true }, { 0, false, true },
                             { 7, false, true }, { 5, false, true }, { 0, false, true }, { 7, false, true } }, true },
        { "Jeden akord (I)", { { 0, false, false } }, false },
    };

    return minorKey ? minorOnes : majorOnes;
}

//==============================================================================
struct Style
{
    float wStatement, wMotif, wRun, wLick, wRiff, wSing, wStops, wArp, wBends; // how often each kind of phrase appears
    float diatonic, chromatic, legato, staccato;
    float bendSlow, preBend, curl, slide, blue;
    float vibLo, vibHi, rateLo, rateHi, vibLate, vibProb; // vibrato depth in cents, rate in Hz
    float jitterMs, laybackMs;
    float energyStart, energyPeak, energyEnd, peakAt, wave;
    float rest; // beats of rest between phrases
    float velBase, velRange;
    int registerShift;
    bool altScale;
};

const Style styles[numStyles] = {
    // epic
    { 1.0f, 0.8f, 1.0f, 0.9f, 0.0f, 0.25f, 0.0f, 0.1f, 0.8f,
      0.25f, 0.10f, 0.55f, 0.15f,
      0.30f, 0.10f, 0.10f, 0.25f, 0.3f,
      22.0f, 45.0f, 5.4f, 6.8f, 0.2f, 0.85f,
      8.0f, 0.0f,
      0.30f, 1.0f, 0.8f, 0.72f, 0.0f,
      1.3f, 70.0f, 36.0f, 0, false },
    // singing
    { 0.7f, 0.7f, 0.3f, 0.1f, 0.0f, 1.6f, 0.0f, 0.4f, 0.5f,
      0.95f, 0.05f, 0.30f, 0.05f,
      0.55f, 0.45f, 0.05f, 0.50f, 0.1f,
      32.0f, 58.0f, 5.5f, 6.3f, 0.25f, 1.0f,
      5.0f, 0.0f,
      0.30f, 0.8f, 0.5f, 0.72f, 0.0f,
      1.6f, 70.0f, 32.0f, 0, false },
    // groove
    { 0.35f, 0.9f, 0.45f, 0.3f, 1.9f, 0.1f, 0.5f, 0.2f, 0.3f,
      0.45f, 0.15f, 0.35f, 0.65f,
      0.15f, 0.10f, 0.30f, 0.45f, 0.4f,
      14.0f, 30.0f, 5.8f, 7.0f, 0.1f, 0.6f,
      3.0f, 0.0f,
      0.45f, 0.75f, 0.6f, 0.70f, 0.08f,
      0.8f, 70.0f, 30.0f, 2, true },
    // swing
    { 0.6f, 1.0f, 0.5f, 0.15f, 0.2f, 0.7f, 0.35f, 1.0f, 0.25f,
      0.75f, 0.40f, 0.40f, 0.30f,
      0.25f, 0.15f, 0.15f, 0.40f, 0.3f,
      14.0f, 30.0f, 5.0f, 6.2f, 0.3f, 0.7f,
      6.0f, 8.0f,
      0.35f, 0.7f, 0.5f, 0.70f, 0.05f,
      1.0f, 68.0f, 30.0f, 0, true },
    // blues
    { 1.2f, 0.9f, 0.6f, 0.3f, 0.1f, 0.5f, 0.7f, 0.15f, 1.1f,
      0.35f, 0.20f, 0.40f, 0.20f,
      0.65f, 0.30f, 0.45f, 0.40f, 0.7f,
      28.0f, 62.0f, 4.6f, 5.9f, 0.5f, 0.9f,
      9.0f, 14.0f,
      0.30f, 0.9f, 0.55f, 0.72f, 0.14f,
      2.0f, 58.0f, 48.0f, -1, true },
};

enum Kind { kStatement = 0, kMotif, kRun, kLick, kRiff, kSing, kStops, kArp, kBends, numKinds };

//==============================================================================
class Generator
{
public:
    explicit Generator (const GeneratorParams& params)
        : p (params), style (styles[juce::jlimit (0, (int) numStyles - 1, params.style)]),
          random ((juce::int64) params.seed * 2654435761LL + 12345)
    {
        const auto& progs = progressions (p.minorKey);
        const auto& prog = progs[(size_t) juce::jlimit (0, (int) progs.size() - 1, p.progression)];
        chords = makeChords (p.keyPc, p.minorKey, p.progression, p.bars);
        total = p.bars * BAR;

        const bool alt = prog.altScale || (prog.chords.size() == 1 && style.altScale);

        if (p.minorKey)
            scale = alt ? std::array<int, 7> { 0, 2, 3, 5, 7, 9, 10 } : std::array<int, 7> { 0, 2, 3, 5, 7, 8, 10 };
        else
            scale = alt ? std::array<int, 7> { 0, 2, 4, 5, 7, 9, 10 } : std::array<int, 7> { 0, 2, 4, 5, 7, 9, 11 };

        for (auto s : scale)
            scalePcs[(size_t) s] = true;

        for (auto s : p.minorKey ? std::array<int, 5> { 0, 3, 5, 7, 10 } : std::array<int, 5> { 0, 2, 4, 7, 9 })
            pentaPcs[(size_t) s] = true;

        for (int pitch = kLow - 12; pitch <= kHigh + 6; ++pitch)
        {
            if (scalePcs[(size_t) pc (pitch - p.keyPc)])
                full.push_back (pitch);

            if (pentaPcs[(size_t) pc (pitch - p.keyPc)])
                penta.push_back (pitch);
        }
    }

    Solo run()
    {
        compose();
        return finish();
    }

private:
    struct TNote
    {
        int start = 0, length = 1, pitch = 60;
        int velocityOffset = 0;
        float energy = 0.5f;
        bool legato = false, chordNote = false, ghost = false, accent = false, expressive = false, staccato = false;
        bool phraseStart = false, phraseEnd = false;
        Articulation art;
    };

    //==============================================================================
    // Randomness
    float rf() { return random.nextFloat(); }
    float rf (float lo, float hi) { return lo + (hi - lo) * rf(); }
    int ri (int lo, int hi) { return lo + random.nextInt (hi - lo + 1); }
    bool chance (float probability) { return rf() < probability; }

    int pick (std::initializer_list<float> weights)
    {
        float sum = 0.0f;

        for (auto w : weights)
            sum += std::max (0.0f, w);

        auto r = rf() * sum;
        int index = 0;

        for (auto w : weights)
        {
            r -= std::max (0.0f, w);

            if (r <= 0.0f)
                return index;

            ++index;
        }

        return (int) weights.size() - 1;
    }

    //==============================================================================
    // Pitch helpers
    bool inScale (int pitch) const { return scalePcs[(size_t) pc (pitch - p.keyPc)]; }
    bool inPenta (int pitch) const { return pentaPcs[(size_t) pc (pitch - p.keyPc)]; }

    static int nearestIndex (const std::vector<int>& ladder, int pitch)
    {
        int best = 0;

        for (int i = 1; i < (int) ladder.size(); ++i)
            if (std::abs (ladder[(size_t) i] - pitch) < std::abs (ladder[(size_t) best] - pitch))
                best = i;

        return best;
    }

    static int moveOn (const std::vector<int>& ladder, int pitch, int steps)
    {
        return ladder[(size_t) juce::jlimit (0, (int) ladder.size() - 1, nearestIndex (ladder, pitch) + steps)];
    }

    const std::vector<int>& ladderFor (float diatonicBias = 1.0f)
    {
        return chance (style.diatonic * diatonicBias) ? full : penta;
    }

    const Chord& chordAt (int tick) const
    {
        return chords[(size_t) juce::jlimit (0, (int) chords.size() - 1, tick / BAR)];
    }

    float seconds (int ticks) const { return (float) ticks / (float) TPB * 60.0f / (float) p.bpm; }

    float energyAt (int tick) const
    {
        const auto x = juce::jlimit (0.0f, 1.0f, (float) tick / (float) total);
        auto ease = [] (float u) { return u * u * (3.0f - 2.0f * u); };
        auto e = x < style.peakAt
                     ? juce::jmap (ease (x / style.peakAt), style.energyStart, style.energyPeak)
                     : juce::jmap (ease ((x - style.peakAt) / (1.0f - style.peakAt)), style.energyPeak, style.energyEnd);
        e += style.wave * std::sin (juce::MathConstants<float>::twoPi * 1.5f * x);
        return juce::jlimit (0.05f, 1.0f, e * (0.75f + 0.5f * p.density));
    }

    int centreAt (float energy) const
    {
        return juce::roundToInt (juce::jmap (std::pow (energy, 1.15f), 63.0f, 79.0f)) + style.registerShift;
    }

    /** A chord tone to aim for: near the register, reachable from the current note, stable. */
    int chooseTarget (int tick, int centre, int avoid = -1)
    {
        const auto& chord = chordAt (juce::jlimit (0, total - 1, tick));
        std::vector<std::pair<int, float>> options;
        float sum = 0.0f;

        for (int pitch = std::max (kLow + 3, centre - 8); pitch <= std::min (kHigh - 2, centre + 8); ++pitch)
        {
            if (! chord.contains (pitch))
                continue;

            const auto interval = pc (pitch - chord.rootPc);
            auto w = std::exp (-(float) std::abs (pitch - centre) / 4.5f) * std::exp (-(float) std::abs (pitch - current) / 6.5f);
            w *= interval == 0 ? 1.0f : (interval == 7 ? 0.8f : (interval == 10 ? 0.45f : 0.95f));

            if (! inScale (pitch))
                w *= 0.12f;
            else if (! inPenta (pitch))
                w *= juce::jmap (style.diatonic, 0.35f, 1.0f);

            if (pitch == avoid)
                w *= 0.2f;

            options.emplace_back (pitch, w);
            sum += w;
        }

        if (options.empty())
            return moveOn (penta, centre, 0);

        auto r = rf() * sum;

        for (const auto& o : options)
        {
            r -= o.second;

            if (r <= 0.0f)
                return o.first;
        }

        return options.back().first;
    }

    /** The fretted note a bend to 'target' starts from, or -1 if nothing sensible lies below it. */
    int bendSource (int target, float& semis)
    {
        const auto whole = inScale (target - 2), half = inScale (target - 1);

        // In a major key the minor third pushed up to the major third is the classic blues bend.
        if (! p.minorKey && pc (target - p.keyPc) == 4 && chance (style.blue))
        {
            semis = 1.0f;
            return target - 1;
        }

        if (whole && (! half || inPenta (target - 2) || chance (0.7f)))
        {
            semis = 2.0f;
            return target - 2;
        }

        if (half)
        {
            semis = 1.0f;
            return target - 1;
        }

        return -1;
    }

    //==============================================================================
    // Rhythm grid. Level 0: eighths, 1: sixteenths (triplets when shuffled), 2: sextuplets.
    bool onGrid (int tick, int level) const
    {
        const auto r = ((tick % TPB) + TPB) % TPB;

        if (level == 0)
            return p.shuffle ? (r == 0 || r == 16) : (r % 12 == 0);

        if (level == 1)
            return p.shuffle ? (r % 8 == 0) : (r % 6 == 0);

        return r % 4 == 0;
    }

    int snapUp (int tick, int level) const
    {
        while (! onGrid (tick, level))
            ++tick;

        return tick;
    }

    int stepForward (int tick, int level) const
    {
        do
            ++tick;
        while (! onGrid (tick, level));

        return tick;
    }

    int stepBack (int tick, int level) const
    {
        do
            --tick;
        while (! onGrid (tick, level));

        return tick;
    }

    static int nextStrong (int tick) { return ((tick + 2 * TPB - 1) / (2 * TPB)) * 2 * TPB; } // beats 1 and 3

    //==============================================================================
    TNote& add (int start, int length, int pitch)
    {
        TNote n;
        n.start = start;
        n.length = std::max (1, length);
        n.pitch = juce::jlimit (kLow, kHigh, pitch);
        n.energy = energyAt (start);
        notes.push_back (n);
        current = notes.back().pitch;
        return notes.back();
    }

    /** Turns a note into a bend that arrives on 'target'. */
    bool makeBend (TNote& n, int target, float presetSemis = 0.0f)
    {
        auto semis = presetSemis;
        const auto source = presetSemis > 0.0f ? target - juce::roundToInt (presetSemis) : bendSource (target, semis);

        if (source < kLow)
            return false;

        const auto duration = seconds (n.length);
        auto& a = n.art;
        n.pitch = source;
        n.expressive = true;
        a.bend = BendKind::up;
        a.bendSemis = semis;

        if (duration > 0.55f && chance (style.bendSlow * (1.25f - 0.6f * n.energy)))
        {
            a.bendTime = std::min (rf (0.28f, 0.8f), duration * 0.55f);
            a.bendDelay = chance (0.35f) ? rf (0.04f, std::min (0.25f, duration * 0.2f)) : 0.0f;

            if (semis >= 2.0f && duration > 1.0f && chance (0.2f))
                a.bend = BendKind::twoStage;
        }
        else if (chance (0.55f))
        {
            a.bendTime = rf (0.045f, 0.09f);
        }
        else
        {
            a.bendTime = rf (0.11f, 0.2f);
        }

        current = target;
        return true;
    }

    float bendChance (float factor = 1.0f) const
    {
        return p.bends < 0.02f ? 0.0f : juce::jlimit (0.0f, 1.0f, (0.2f + 0.8f * p.bends) * factor);
    }

    /** A long, important note: bent into, pre-bent, slid into, or simply held.
        With 'presetSemis' the bend is already decided (the notes before it lead to the fretted note). */
    void expressiveNote (int start, int length, int target, float bendScale = 1.0f, float presetSemis = 0.0f)
    {
        auto& n = add (start, length, target);
        n.expressive = true;
        n.accent = true;
        const auto duration = seconds (length);

        if (presetSemis > 0.0f ? makeBend (n, target, presetSemis) : (chance (bendChance (bendScale)) && makeBend (n, target)))
        {
            // Sometimes the bend is let back down before the note ends.
            if (duration > 0.6f && chance (0.22f))
            {
                n.art.bend = BendKind::upAndRelease;
                n.art.releaseAt = rf (0.6f, 0.82f);
                n.art.releaseTime = rf (0.08f, 0.25f);
                current = n.pitch;
            }

            return;
        }

        if (duration > 0.5f && chance (style.preBend * bendChance()))
        {
            // Pre-bend: the note is already pushed up when picked, then sinks to its pitch.
            const auto above = inScale (target + 2) ? 2.0f : (inScale (target + 1) ? 1.0f : 0.0f);

            if (above > 0.0f)
            {
                n.art.bend = BendKind::preBendRelease;
                n.art.bendSemis = above;
                n.art.releaseAt = rf (0.12f, 0.35f);
                n.art.releaseTime = std::min (rf (0.12f, 0.5f), duration * 0.4f);
                return;
            }
        }

        if (chance (style.slide))
        {
            n.art.slideIn = inScale (target - 2) ? -2.0f : -1.0f;
            n.art.slideTime = rf (0.04f, 0.1f);
        }
    }

    int restTicks (float energy)
    {
        const auto beats = style.rest * (0.2f + 1.6f * p.space) * (1.25f - 0.95f * energy) * rf (0.7f, 1.3f);
        return std::max (TPB / 2, juce::roundToInt (beats * 2.0f) * TPB / 2);
    }

    //==============================================================================
    // Phrase builders. Each starts no earlier than 'from', ends no later than 'limit'
    // and returns the tick where its last note ends.

    /** A few pickup notes, one long expressive note, and a short tail that comes back down. */
    int statement (int from, int limit)
    {
        const auto e = energyAt (from);
        const auto level = e > 0.6f && chance (0.5f) ? 1 : 0;
        auto pickups = pick ({ 0.1f, 0.3f, 0.35f, 0.25f });
        auto anchor = nextStrong (from + pickups * (TPB / (level == 0 ? 2 : 4)));

        if (chance (0.05f + 0.45f * p.syncopation) && stepBack (anchor, 0) >= from)
            anchor = stepBack (anchor, 0); // pushed: lands early and rings over the beat
        else if (chance (0.3f * p.syncopation))
            anchor = stepForward (anchor, 0); // late: lands on the "and"

        if (anchor + TPB > limit)
            return from;

        const auto target = chooseTarget (anchor + TPB / 2, centreAt (e), lastTarget);
        const auto holdBeats = std::array<float, 5> { 1.5f, 2.0f, 2.5f, 3.0f, 4.0f }[(size_t) pick ({ 0.6f + p.density, 1.0f, 0.8f, 1.2f - p.density, 0.9f - p.density })];
        auto hold = std::min (juce::roundToInt (holdBeats * TPB), limit - anchor);

        // Pickups walk up to the note the phrase is aiming for.
        float semis = 0.0f;
        const auto approachTo = bendSource (target, semis) >= kLow && p.bends > 0.3f ? target - 2 : target;
        const auto& ladder = ladderFor (0.6f);
        std::vector<int> times;
        auto tick = anchor;

        for (int i = 0; i < pickups; ++i)
        {
            tick = stepBack (tick, level);

            if (tick < from)
                break;

            times.insert (times.begin(), tick);
        }

        pickups = (int) times.size();
        const auto fromBelow = chance (0.7f);

        for (int i = 0; i < pickups; ++i)
        {
            const auto steps = (pickups - i) * (fromBelow ? -1 : 1);
            const auto end = i + 1 < pickups ? times[(size_t) i + 1] : anchor;
            auto& n = add (times[(size_t) i], end - times[(size_t) i], moveOn (ladder, approachTo, steps));
            n.legato = i > 0 && chance (style.legato);
        }

        expressiveNote (anchor, hold, target);
        lastTarget = target;
        auto end = anchor + hold;

        // Tail: a few notes falling away from the long one.
        if (end + TPB < limit && chance (0.75f))
        {
            const auto tailLevel = chance (0.3f + 0.4f * e) ? 1 : 0;
            const auto count = ri (2, 4);
            const auto& tailLadder = ladderFor (0.8f);
            auto pitch = current;
            auto t = snapUp (end, tailLevel);

            for (int i = 0; i < count && t + TPB / 2 < limit; ++i)
            {
                const auto last = i == count - 1;
                const auto next = last ? t + ri (1, 3) * TPB / 2 * (tailLevel == 0 ? 2 : 1) : stepForward (t, tailLevel);
                pitch = moveOn (tailLadder, pitch, chance (0.8f) ? -1 : 1);

                if (last)
                    pitch = snapToChord (pitch, t);

                auto& n = add (t, std::min (next, limit) - t, pitch);
                n.legato = i == 0 ? notes[notes.size() - 2].art.bend == BendKind::upAndRelease : chance (style.legato);
                n.staccato = ! last && chance (style.staccato);
                t = next;
            }

            end = std::min (t, limit);
        }

        return end;
    }

    /** The scale note closest to 'pitch' that belongs to the chord sounding at 'tick'. */
    int snapToChord (int pitch, int tick) const
    {
        const auto& chord = chordAt (juce::jlimit (0, total - 1, tick));

        for (int d = 0; d <= 4; ++d)
            for (auto candidate : { pitch - d, pitch + d })
                if (chord.contains (candidate) && inScale (candidate) && candidate >= kLow && candidate <= kHigh)
                    return candidate;

        return pitch;
    }

    /** A melody that could be sung: mostly steps on the scale, an arch up to a peak and back,
        chord tones on the strong beats, and room for the long notes to speak. */
    int sing (int from, int limit)
    {
        const auto e = energyAt (from);
        auto tick = nextStrong (from);

        if (chance (0.4f * p.syncopation) && stepBack (tick, 0) >= from)
            tick = stepBack (tick, 0);

        // Rhythm in eighths: quarters and dotted quarters carry it, eighths connect, a long note ends it.
        const auto span = ri (5, 8) * 2;
        std::vector<int> lengths;

        for (int used = 0; used < span - 3;)
        {
            const auto d = 1 + pick ({ 0.6f + 1.2f * p.density, 1.0f, 0.5f + 0.5f * p.syncopation, 0.5f - 0.4f * p.density });
            lengths.push_back (d);
            used += d;
        }

        lengths.push_back (ri (3, 6));
        const auto count = (int) lengths.size();
        const auto peak = juce::jlimit (1, std::max (1, count - 2), juce::roundToInt ((float) count * rf (0.35f, 0.7f)));
        auto pitch = chooseTarget (tick + TPB / 2, centreAt (e) - 2, lastTarget);
        auto end = from;

        for (int i = 0; i < count; ++i)
        {
            auto next = tick;

            for (int k = 0; k < lengths[(size_t) i]; ++k)
                next = stepForward (next, 0);

            if (next > limit)
                next = limit;

            if (next - tick < TPB / 3)
                break;

            const auto last = i == count - 1 || next == limit;
            const auto strong = tick % (2 * TPB) == 0 || lengths[(size_t) i] >= 3;
            const auto note = strong || last ? snapToChord (pitch, tick + TPB / 2) : pitch;

            const auto previousBent = ! notes.empty() && notes.back().art.bend != BendKind::none;

            if (last || (lengths[(size_t) i] >= 3 && chance (0.65f)))
            {
                expressiveNote (tick, next - tick, note, previousBent ? 0.25f : 0.8f);
            }
            else
            {
                auto& n = add (tick, next - tick, note);
                n.legato = i > 0 && lengths[(size_t) i] == 1 && chance (style.legato);
            }

            end = next;
            tick = next;

            if (last)
                break;

            // Up towards the peak, then back down; mostly by step, now and then a third.
            const auto direction = i < peak ? 1 : -1;
            const auto step = chance (0.72f) ? direction : (chance (0.55f) ? 2 * direction : -direction);
            pitch = juce::jlimit (kLow + 2, kHigh - 2, moveOn (full, current, step));
        }

        lastTarget = current;
        return end;
    }

    /** The same bend played several ways: quick ones that answer each other, then one that takes its time. */
    int bendPlay (int from, int limit)
    {
        const auto e = energyAt (from);
        auto tick = snapUp (from, 0);

        if (chance (0.5f * p.syncopation))
            tick = stepForward (tick, 0);

        const auto target = chooseTarget (tick + TPB, centreAt (e), lastTarget);
        float semis = 0.0f;
        const auto source = bendSource (target, semis);

        if (source < kLow || p.bends < 0.02f)
            return from;

        const auto hot = e > 0.7f;
        const auto count = hot ? ri (3, 5) : ri (2, 4);
        auto end = from;

        for (int i = 0; i < count && tick + TPB < limit; ++i)
        {
            const auto last = i == count - 1;
            auto length = last ? ri (4, hot ? 6 : 8) * TPB / 2
                               : (chance (hot ? 0.7f : 0.5f) ? TPB / 2 : (chance (hot ? 1.0f : 0.6f) ? TPB : 3 * TPB / 2));
            length = std::min (length, limit - tick);
            const auto duration = seconds (length);

            auto& n = add (tick, length, target);
            makeBend (n, target, semis);
            n.accent = true;

            if (! last)
            {
                n.art.bend = chance (0.45f) ? BendKind::upAndRelease : BendKind::up;
                n.art.bendDelay = 0.0f;
                n.art.bendTime = std::min (rf (0.045f, 0.13f), duration * 0.5f);
                n.art.releaseAt = rf (0.5f, 0.7f);
                n.art.releaseTime = std::min (rf (0.06f, 0.15f), duration * 0.3f);
            }
            else
            {
                n.art.bend = semis >= 2.0f && duration > 1.2f && chance (0.25f) ? BendKind::twoStage : BendKind::up;
                n.art.bendTime = std::min (rf (0.3f, 0.9f), duration * 0.5f);
                n.art.bendDelay = chance (0.4f) ? rf (0.03f, 0.15f) : 0.0f;
            }

            const auto released = n.art.bend == BendKind::upAndRelease;
            end = tick + length;
            tick = snapUp (end, 0);

            if (last)
                break;

            if (chance (hot ? 0.15f : 0.4f))
            {
                tick = stepForward (tick, 0); // a breath between two bends
            }
            else if (chance (0.4f) && tick + TPB < limit)
            {
                const auto next = stepForward (tick, 0);
                auto& between = add (tick, next - tick, moveOn (penta, source, -1));
                between.legato = released;
                end = next;
                tick = next;
            }
        }

        lastTarget = target;
        current = target;
        return end;
    }

    /** A short rhythmic idea, then the same rhythm again with a different ending. */
    int motif (int from, int limit)
    {
        const auto e = energyAt (from);
        auto start = snapUp (from, 0);

        if (chance (0.5f * p.syncopation))
            start = stepForward (nextStrong (from), 0) - (chance (0.5f) ? 0 : TPB); // off the beat

        start = std::max (snapUp (from, 0), start);

        std::vector<int> offsets, steps;
        const auto recall = memory.valid && chance (0.45f);

        if (recall)
        {
            offsets = memory.offsets;
            steps = memory.steps;
        }
        else
        {
            // Onsets on the fine grid; syncopation favours the off-beats.
            const auto span = ri (3, 5) * TPB / 2;
            offsets.push_back (0);

            for (auto t = stepForward (start, 1); t < start + span; t = stepForward (t, 1))
            {
                const auto r = ((t % TPB) + TPB) % TPB;
                const auto onBeat = r == 0, onEighth = onGrid (t, 0) && ! onBeat;
                const auto probability = onBeat ? 0.7f - 0.4f * p.syncopation
                                                : (onEighth ? 0.45f + 0.25f * p.syncopation
                                                            : (0.1f + 0.35f * p.density) * (0.6f + 0.8f * p.syncopation));

                if (chance (probability))
                    offsets.push_back (t - start);
            }

            while (offsets.size() < 3)
                offsets.push_back (offsets.back() + TPB / 2);

            while (offsets.size() > 6)
                offsets.erase (offsets.begin() + ri (1, (int) offsets.size() - 2));

            auto direction = chance (0.5f) ? 1 : -1;
            steps.push_back (0);

            for (size_t i = 1; i < offsets.size(); ++i)
            {
                if (chance (0.4f))
                    direction = -direction;

                steps.push_back (steps.back() + direction * (chance (0.75f) ? 1 : 2));
            }

            memory.offsets = offsets;
            memory.steps = steps;
            memory.valid = true;
        }

        const auto& ladder = ladderFor();
        const auto base = chooseTarget (start, centreAt (e), -1);
        auto end = from;

        auto play = [&] (int at, int shift, bool answer)
        {
            for (size_t i = 0; i < offsets.size(); ++i)
            {
                const auto t = at + offsets[i];
                const auto last = i + 1 == offsets.size();
                auto length = last ? ri (2, answer ? 5 : 3) * TPB / 2 : offsets[i + 1] - offsets[i];

                if (t + TPB / 4 > limit)
                    break;

                length = std::min (length, limit - t);
                auto step = steps[i] + shift;

                if (answer && i + 2 >= offsets.size())
                    step = steps[i] - (steps.back() > 0 ? 1 : -1) * (int) (i + 2 - offsets.size() + 1) + shift; // new ending

                auto pitch = moveOn (ladder, base, step);

                if (last)
                {
                    pitch = snapToChord (pitch, t + TPB / 2);

                    if (answer)
                    {
                        expressiveNote (t, length, pitch);
                        end = t + length;
                        continue;
                    }
                }

                auto& n = add (t, length, pitch);
                n.legato = i > 0 && length <= TPB / 4 && chance (style.legato);
                n.staccato = ! last && chance (style.staccato);
                n.accent = i == 0;
                end = t + length;
            }
        };

        play (start, 0, false);

        // The answer keeps the rhythm; sometimes it is displaced by an eighth to play against the beat.
        auto answerAt = start + ((end + TPB / 2 - start + 2 * TPB - 1) / (2 * TPB)) * 2 * TPB;

        if (chance (0.35f * p.syncopation))
            answerAt += chance (0.5f) ? TPB / 2 : -TPB / 2;

        answerAt = std::max (answerAt, snapUp (end + TPB / 4, 0));

        if (answerAt + offsets.back() + TPB < limit)
            play (answerAt, chance (0.5f) ? 1 : (chance (0.5f) ? -1 : 2), true);

        return end;
    }

    /** A fast line that lands on a long note. */
    int runPhrase (int from, int limit)
    {
        const auto e = energyAt (from);
        const auto level = e > 0.7f && chance (0.25f + 0.6f * p.density) ? 2 : 1;
        auto count = juce::roundToInt ((float) (level == 2 ? ri (2, 4) * 6 : ri (6, 12)) * (0.6f + 0.8f * p.density));
        count = juce::jlimit (4, 24, count);

        auto anchor = nextStrong (from + count * (level == 2 ? 4 : (p.shuffle ? 8 : 6)));

        if (anchor + TPB > limit)
            return from;

        std::vector<int> times;
        auto tick = anchor;

        for (int i = 0; i < count; ++i)
        {
            tick = stepBack (tick, level);

            if (tick < from)
                break;

            times.insert (times.begin(), tick);
        }

        count = (int) times.size();

        if (count < 3)
            return from;

        const auto centre = centreAt (e);
        const auto up = current < centre - 2 ? true : (current > centre + 4 ? false : chance (0.55f));
        const auto target = chooseTarget (anchor + TPB / 2, centre + (up ? 3 : -3), lastTarget);
        const auto holdBeats = std::array<float, 4> { 1.5f, 2.0f, 3.0f, 4.0f }[(size_t) (e > 0.75f ? pick ({ 1.2f, 1.0f, 0.4f, 0.15f })
                                                                                                    : pick ({ 0.8f, 1.0f, 0.8f, 0.5f }))];
        const auto hold = std::min (juce::roundToInt (holdBeats * TPB), limit - anchor);

        float semis = 0.0f;
        const auto willBend = chance (bendChance()) && bendSource (target, semis) >= kLow;
        const auto landing = willBend ? target - juce::roundToInt (semis) : target;

        // The shape of the run, as steps on the scale ladder.
        const auto pattern = pick ({ 1.0f, 0.8f, 0.5f, 0.5f });
        std::vector<int> shape;

        for (int j = 0; j < count; ++j)
        {
            switch (pattern)
            {
                case 0:  shape.push_back (j); break;                                 // straight
                case 1:  shape.push_back (j / 3 + j % 3); break;                     // groups of three
                case 2:  shape.push_back (j / 4 + j % 4); break;                     // groups of four
                default: shape.push_back (j % 2 == 0 ? j / 2 : (j + 3) / 2); break;  // zigzag
            }
        }

        const auto& ladder = ladderFor (0.8f);
        auto direction = up ? 1 : -1;
        const auto landingIndex = nearestIndex (ladder, landing);

        // The whole run has to stay on the neck: turn it around or shorten it if it doesn't.
        auto fits = [&] (int dir)
        {
            for (int j = 0; j < count; ++j)
            {
                const auto index = landingIndex - dir * (shape.back() + 1) + dir * shape[(size_t) j];

                if (index < 0 || index >= (int) ladder.size() || ladder[(size_t) index] < kLow || ladder[(size_t) index] > kHigh)
                    return false;
            }

            return true;
        };

        while (! fits (direction) && ! fits (-direction) && count > 3)
        {
            times.erase (times.begin());
            shape.pop_back();
            --count;
        }

        if (! fits (direction))
        {
            if (! fits (-direction))
                return from;

            direction = -direction;
        }

        const auto firstIndex = landingIndex - direction * (shape.back() + 1);

        for (int j = 0; j < count; ++j)
        {
            const auto index = juce::jlimit (0, (int) ladder.size() - 1, firstIndex + direction * shape[(size_t) j]);
            auto pitch = juce::jlimit (kLow, kHigh, ladder[(size_t) index]);
            const auto end = j + 1 < count ? times[(size_t) j + 1] : anchor;

            // The blue note slips in between the fourth and the fifth.
            if (p.minorKey && pc (pitch - p.keyPc) == 5 && j > 0 && j + 1 < count && chance (style.chromatic * 0.5f))
                pitch += direction > 0 ? 1 : 0;

            auto& n = add (times[(size_t) j], end - times[(size_t) j], pitch);
            n.legato = j > 0 && chance (style.legato * (level == 2 ? 1.3f : 0.9f));
            n.velocityOffset = -10 + juce::roundToInt (14.0f * (float) j / (float) count); // grows towards the landing
            n.accent = j % (pattern == 2 ? 4 : 3) == 0 && pattern != 0;
        }

        expressiveNote (anchor, hold, target, 0.0f, willBend ? semis : 0.0f);
        lastTarget = target;
        return anchor + hold;
    }

    /** A three- or four-note figure repeated against the beat, then a long note. */
    int lick (int from, int limit)
    {
        const auto e = energyAt (from);
        const auto level = p.shuffle ? 1 : (chance (0.3f) ? 2 : 1);
        const auto unit = level == 2 ? 4 : (p.shuffle ? 8 : 6);
        const auto figure = level == 1 && ! p.shuffle && chance (0.4f) ? 4 : 3; // three sixteenths cut across the beat
        const auto wild = e > 0.75f && p.style == styleEpic;
        auto repeats = level == 2 ? ri (3, wild ? 8 : 5) : (p.shuffle ? ri (2, 3) : ri (2, wild ? 6 : 4));
        auto anchor = nextStrong (from + figure * repeats * unit);

        while (anchor + TPB > limit && repeats > 2)
        {
            --repeats;
            anchor = nextStrong (from + figure * repeats * unit);
        }

        if (anchor + TPB > limit)
            return from;

        const auto start = anchor - figure * repeats * unit;
        const auto top = chooseTarget (start, centreAt (e) + 2, lastTarget);
        const auto variant = pick ({ 1.0f, figure == 4 ? 1.0f : 0.0f, 0.6f + p.bends, 0.6f });
        float semis = 0.0f;
        const auto bendable = bendSource (top, semis) >= kLow;

        for (int r = 0; r < repeats; ++r)
        {
            for (int i = 0; i < figure; ++i)
            {
                static const int shapes[4][4] = { { 0, -1, -2, -1 }, { 0, -1, 0, -2 }, { 0, -1, -2, -1 }, { 0, -2, -1, -2 } };
                const auto t = start + (r * figure + i) * unit;
                auto& n = add (t, unit, moveOn (penta, top, shapes[variant][i]));
                n.accent = i == 0;
                n.legato = i > 0 && chance (style.legato * 1.2f);

                // Variant 2 starts every repeat with a quick bend up to the top note.
                if (variant == 2 && i == 0 && bendable && unit >= 6)
                {
                    n.pitch = top - juce::roundToInt (semis);
                    n.art.bend = BendKind::up;
                    n.art.bendSemis = semis;
                    n.art.bendTime = std::min (rf (0.04f, 0.07f), seconds (unit) * 0.6f);
                }
            }
        }

        const auto target = chance (0.5f) ? top : chooseTarget (anchor + TPB / 2, top + 3, top);
        const auto hold = std::min (ri (4, 8) * TPB / 2, limit - anchor);
        expressiveNote (anchor, hold, target, 1.3f);
        lastTarget = target;
        return anchor + hold;
    }

    /** A two-beat rhythmic cell of tight stabs, repeated, shifted and changed: the rhythm leads. */
    int riff (int from, int limit)
    {
        const auto e = energyAt (from);
        const auto start = snapUp (from, 0);
        const auto cell = 2 * TPB;
        const auto repeats = std::min (ri (2, 3), (limit - start) / cell);

        if (repeats < 1)
            return from;

        const auto home = chooseTarget (start, centreAt (e), lastTarget);

        struct Hit
        {
            int offset, step;
            bool ghost, accent;
        };

        // The cell: where the stabs fall, and a small shape of three or four notes around 'home'.
        std::vector<Hit> hits;
        int run = 0, step = 0;

        for (auto t = start; t < start + cell; t = stepForward (t, 1))
        {
            const auto r = ((t % TPB) + TPB) % TPB;
            const auto slot = p.shuffle ? r / 8 : r / 6;
            static const float straight[4] = { 0.5f, 0.3f, 0.55f, 0.4f };
            static const float swung[3] = { 0.6f, 0.2f, 0.55f };
            auto probability = (p.shuffle ? swung[slot] : straight[slot]) * (0.6f + 0.9f * p.density);

            if (slot != 0)
                probability *= 0.7f + 0.8f * p.syncopation;
            else
                probability *= 1.2f - 0.6f * p.syncopation;

            if (run >= 3)
                probability = 0.0f; // leave a hole after a burst

            if (chance (probability) || (hits.empty() && t - start >= TPB / 2))
            {
                if (! hits.empty())
                    step = juce::jlimit (-2, 3, step + pick ({ 0.5f, 1.3f, 0.6f, 1.3f, 0.5f }) - 2);

                hits.push_back ({ t - start, step, chance (0.13f), slot != 0 && chance (0.5f) });
                ++run;
            }
            else
            {
                run = 0;
            }
        }

        if (hits.size() >= 2 && hits[0].step == hits[1].step && hits.size() < 4)
            hits[1].step = hits[0].step + (chance (0.5f) ? 1 : -1);

        auto end = from;
        const auto grid = stepForward (start, 1) - start;

        for (int r = 0; r < repeats; ++r)
        {
            const auto lastRepeat = r == repeats - 1;
            // A repeat may arrive one step late or early: the same figure, displaced.
            auto shift = 0;

            if (r > 0 && chance (0.45f * p.syncopation))
                shift = chance (0.6f) ? grid : -grid;

            const auto transpose = r > 0 && chance (0.3f) ? (chance (0.5f) ? 1 : -1) : 0;

            for (size_t i = 0; i < hits.size(); ++i)
            {
                const auto& h = hits[i];
                const auto lastHit = i + 1 == hits.size();
                const auto t = start + r * cell + h.offset + (h.offset == 0 && shift < 0 && r == 0 ? 0 : shift);

                if (t < end || t >= limit - TPB / 4)
                    continue;

                if (r > 0 && ! lastHit && hits.size() > 3 && chance (0.15f))
                    continue; // drop a stab the second time round

                auto pitch = moveOn (penta, home, h.step + transpose);

                if (lastRepeat && lastHit)
                {
                    const auto length = std::min (ri (2, 4) * TPB / 2, limit - t);
                    expressiveNote (t, length, snapToChord (pitch, t), 0.8f);
                    end = t + length;
                    lastTarget = home;
                    return end;
                }

                const auto next = lastHit ? start + (r + 1) * cell : start + r * cell + hits[i + 1].offset + shift;
                const auto gap = juce::jlimit (grid / 2, TPB, next - t);
                auto& n = add (t, gap, pitch);
                n.staccato = gap <= TPB / 2 || chance (style.staccato);
                n.ghost = h.ghost;
                n.accent = h.accent;
                n.legato = false;

                if (! n.ghost && chance (0.14f * bendChance()) && gap >= TPB / 2)
                {
                    n.art.bend = BendKind::curl;
                    n.art.bendSemis = rf (0.3f, 0.5f);
                    n.art.bendTime = seconds (gap) * 0.7f;
                }

                end = t + gap;
            }
        }

        lastTarget = home;
        return end;
    }

    /** Two-note chords slid into from below, then a single-note reply. */
    int stops (int from, int limit)
    {
        const auto e = energyAt (from);
        auto tick = snapUp (from, 0);

        if (chance (0.5f * p.syncopation))
            tick = stepForward (tick, 0);

        const auto lower = moveOn (penta, centreAt (e) - 2, 0);
        const auto upper = inPenta (lower + 5) ? lower + 5 : (inPenta (lower + 4) ? lower + 4 : (inPenta (lower + 3) ? lower + 3 : lower + 7));
        const auto count = ri (2, 4);
        auto end = from;

        for (int i = 0; i < count && tick + TPB < limit; ++i)
        {
            const auto last = i == count - 1;
            const auto length = last ? ri (2, 4) * TPB / 2 : (chance (0.5f) ? TPB / 2 : TPB) - (chance (style.staccato) ? TPB / 4 : 0);
            const auto shift = i > 0 && chance (0.35f) ? (chance (0.5f) ? 1 : -1) : 0;
            const auto low = moveOn (penta, lower, shift);
            const auto high = shift == 0 ? upper : moveOn (penta, upper, shift);

            auto& a = add (tick, length, low);
            a.accent = true;

            if (chance (0.6f))
            {
                a.art.slideIn = -1.0f;
                a.art.slideTime = rf (0.05f, 0.11f);
            }

            auto& b = add (tick, length, high);
            b.chordNote = true;
            end = tick + length;

            // Stabs fall on and off the beat.
            tick = stepForward (end + (chance (0.4f + 0.4f * p.syncopation) ? 0 : TPB / 2) - 1, 0);
        }

        current = lower;

        // The reply: a few single notes down to a chord tone.
        tick = snapUp (end + TPB / 2, 0);
        const auto replies = ri (2, 4);
        auto pitch = upper;

        for (int i = 0; i < replies && tick + TPB < limit; ++i)
        {
            const auto last = i == replies - 1;
            const auto next = last ? tick + ri (2, 4) * TPB / 2 : stepForward (tick, chance (0.5f) ? 0 : 1);
            pitch = moveOn (penta, pitch, -1);

            if (last)
            {
                expressiveNote (tick, std::min (next, limit) - tick, snapToChord (pitch, tick));
            }
            else
            {
                auto& n = add (tick, next - tick, pitch);
                n.legato = i > 0 && chance (style.legato);
            }

            end = std::min (next, limit);
            tick = next;
        }

        return end;
    }

    /** The chord spelled out note by note, with a chromatic step leading in. */
    int arpeggio (int from, int limit)
    {
        const auto e = energyAt (from);
        const auto level = chance (0.3f + 0.4f * p.density) ? 1 : 0;
        const auto centre = centreAt (e);
        const auto count = ri (4, 6);
        auto anchor = nextStrong (from + (count + 1) * (TPB / (level == 0 ? 2 : (p.shuffle ? 3 : 4))));

        if (anchor + TPB > limit)
            return from;

        std::vector<int> times;
        auto tick = anchor;

        for (int i = 0; i < count; ++i)
        {
            tick = stepBack (tick, level);

            if (tick < from)
                break;

            times.insert (times.begin(), tick);
        }

        if (times.size() < 3)
            return from;

        // Chord tones in order, starting below the register.
        const auto& chord = chordAt (times.front());
        std::vector<int> tones;

        for (int pitch = centre - 10; pitch <= centre + 9; ++pitch)
            if (chord.contains (pitch) && pitch >= kLow && pitch <= kHigh)
                tones.push_back (pitch);

        if (tones.size() < 3)
            return from;

        const auto up = chance (0.7f);
        auto index = up ? ri (0, std::max (0, (int) tones.size() - (int) times.size()))
                        : ri (std::min ((int) tones.size() - 1, (int) times.size() - 1), (int) tones.size() - 1);

        for (size_t i = 0; i < times.size(); ++i)
        {
            const auto end = i + 1 < times.size() ? times[i + 1] : anchor;
            auto pitch = tones[(size_t) juce::jlimit (0, (int) tones.size() - 1, index)];

            // A half step from below leads into the first chord tone.
            if (i == 0 && chance (style.chromatic))
                pitch -= 1;
            else
                index += up ? 1 : -1;

            auto& n = add (times[i], end - times[i], pitch);
            n.legato = i == 1 && notes[notes.size() - 2].pitch == pitch - 1;
            n.staccato = chance (style.staccato * 0.5f);
        }

        const auto target = chooseTarget (anchor + TPB / 2, current + (up ? 2 : -2), lastTarget);
        const auto hold = std::min (ri (3, 6) * TPB / 2, limit - anchor);
        expressiveNote (anchor, hold, target, 0.7f);
        lastTarget = target;
        return anchor + hold;
    }

    /** The last phrase: a way up or down into one long final note. */
    void ending (int from)
    {
        const auto rhythmic = p.style == styleGroove || p.style == styleSwing;
        const auto builds = style.wRun + style.wLick >= 1.2f; // these arrive on the last note with a run, so leave room for it
        const auto finalStart = std::max (snapUp (from + TPB, 0),
                                          total - (p.bars > 4 && ! rhythmic && ! builds && chance (0.5f) ? 6 : 4) * TPB);
        const auto anchor = chance (0.4f * p.syncopation) ? stepBack (finalStart, 0) : finalStart;
        const auto e = energyAt (from);
        const auto& chord = chordAt (total - 1);

        // End on the tonic if the last chord allows it, otherwise on a chord tone.
        auto target = chooseTarget (total - 1, centreAt (e), -1);

        for (int pitch = centreAt (e) - 7; pitch <= centreAt (e) + 7; ++pitch)
            if (pc (pitch - p.keyPc) == 0 && chord.contains (pitch) && chance (0.75f))
                target = pitch;

        // Approach: a run if there is room and energy, otherwise a few pickups. More room, longer way in.
        const auto room = anchor - from;
        const auto level = e > 0.55f && room >= 2 * TPB ? 1 : 0;
        auto count = level == 1 ? ri (4, 8) : ri (1, 3);

        if (room >= 3 * TPB)
            count = level == 1 ? ri (8, 12) : ri (4, 6);

        std::vector<int> times;
        auto tick = anchor;

        for (int i = 0; i < count; ++i)
        {
            tick = stepBack (tick, level);

            if (tick < from)
                break;

            times.insert (times.begin(), tick);
        }

        float semis = 0.0f;
        const auto willBend = chance (bendChance (1.2f)) && bendSource (target, semis) >= kLow;
        const auto landing = willBend ? target - juce::roundToInt (semis) : target;
        const auto direction = current <= landing ? 1 : -1;
        const auto landingIndex = nearestIndex (penta, landing);
        std::vector<int> shape;

        // Longer approaches climb in groups of three; all of it must stay on the neck.
        while (! times.empty())
        {
            shape.clear();

            for (int j = 0; j < (int) times.size(); ++j)
                shape.push_back (times.size() > 5 ? j / 3 + j % 3 : j);

            const auto first = landingIndex - direction * (shape.back() + 1);
            const auto far = first + direction * (*std::max_element (shape.begin(), shape.end()));
            auto onNeck = [this] (int index)
            {
                return index >= 0 && index < (int) penta.size() && penta[(size_t) index] >= kLow && penta[(size_t) index] <= kHigh;
            };

            if (onNeck (first) && onNeck (far))
                break;

            times.erase (times.begin());
        }

        for (size_t i = 0; i < times.size(); ++i)
        {
            const auto index = landingIndex - direction * (shape.back() + 1) + direction * shape[i];
            const auto end = i + 1 < times.size() ? times[i + 1] : anchor;
            auto& n = add (times[i], end - times[i], penta[(size_t) index]);
            n.legato = i > 0 && chance (style.legato);
            n.accent = times.size() > 5 && i % 3 == 0;
            n.velocityOffset = juce::roundToInt (10.0f * (float) i / (float) times.size());
        }

        expressiveNote (anchor, total - TPB / 4 - anchor, target, 0.0f, willBend ? semis : 0.0f);
        auto& last = notes.back();
        last.phraseEnd = true;

        // The last note always sings.
        last.art.vibrato.depth = rf (style.vibLo, style.vibHi) * 0.012f * (0.6f + 0.6f * p.vibrato);
        last.art.vibrato.rateHz = rf (style.rateLo, style.rateHi) * 0.95f;
        last.art.vibrato.delay = rf (0.15f, 0.45f);
        last.art.vibrato.grow = rf (0.2f, 0.4f);
        last.art.vibrato.swell = rf (0.2f, 0.7f);
    }

    //==============================================================================
    void compose()
    {
        current = moveOn (penta, centreAt (energyAt (0)), 0);
        const auto endingAt = total - (p.bars <= 4 ? 6 : 8) * TPB;
        auto t = 0;
        int phrase = 0, stuck = 0, fastPhrases = 0;
        const auto holdScale = p.style == styleGroove || p.style == styleSwing ? 0.6f : 1.0f;

        // Often the solo doesn't start right on the one.
        if (chance (0.3f + 0.3f * p.syncopation))
            t = ri (1, 3) * TPB / 2;

        while (t < endingAt && stuck < 12)
        {
            const auto e = energyAt (t);
            float w[numKinds] = {
                style.wStatement * (1.25f - 0.6f * e),
                style.wMotif,
                style.wRun * (0.3f + 1.4f * e) * (0.5f + p.density),
                style.wLick * (e > 0.55f ? 2.0f * e : 0.1f),
                style.wRiff,
                style.wSing * (1.3f - 0.5f * e),
                style.wStops * (1.1f - 0.4f * e),
                style.wArp,
                style.wBends * (0.3f + 1.2f * p.bends),
            };

            if (lastKind >= 0)
                w[lastKind] *= lastKind == kBends ? 0.0f : 0.35f;

            if (phrase == 0)
                w[kRun] = w[kLick] = 0.0f; // open with something to say, not with speed

            auto kind = pick ({ w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8] });

            // A solo that builds has to arrive somewhere: once it is hot, something fast must happen.
            const auto builds = style.wRun + style.wLick >= 1.2f && p.density > 0.2f;

            if (builds && e > 0.72f && fastPhrases == 0 && stuck == 0 && phrase > 0)
                kind = kRun;
            else if (builds && e > 0.85f && fastPhrases == 1 && stuck == 0 && lastKind != kLick && lastKind != kRun)
                kind = kLick;

            const auto first = notes.size();
            auto end = t;

            switch (kind)
            {
                case kStatement: end = statement (t, endingAt + TPB); break;
                case kMotif:     end = motif (t, endingAt + TPB); break;
                case kRun:       end = runPhrase (t, endingAt + TPB); break;
                case kLick:      end = lick (t, endingAt + TPB); break;
                case kRiff:      end = riff (t, endingAt + TPB); break;
                case kSing:      end = sing (t, endingAt + TPB); break;
                case kStops:     end = stops (t, endingAt + TPB); break;
                case kBends:     end = bendPlay (t, endingAt + TPB); break;
                default:         end = arpeggio (t, endingAt + TPB); break;
            }

            if (notes.size() == first)
            {
                ++stuck; // that phrase didn't fit here: try another kind
                continue;
            }

            notes[first].phraseStart = true;
            notes.back().phraseEnd = true;
            lastKind = kind;
            stuck = 0;
            ++phrase;

            if (kind == kRun || kind == kLick)
                ++fastPhrases;

            // Rhythmic styles don't sit on a note for bars: cut the long landing and move on.
            if (auto& landing = notes.back(); holdScale < 1.0f && landing.length > 2 * TPB && end == landing.start + landing.length)
            {
                const auto shorter = std::max (3 * TPB / 2, juce::roundToInt ((float) landing.length * holdScale / (float) (TPB / 2)) * (TPB / 2));
                end -= landing.length - shorter;
                landing.length = shorter;
            }

            t = snapUp (std::max (end, t + TPB / 2) + restTicks (energyAt (end)), 0);
        }

        const auto first = notes.size();
        const auto lastEnd = notes.empty() ? 0 : notes.back().start + notes.back().length;
        ending (std::max (lastEnd, std::min (t, total - 6 * TPB)));

        if (first < notes.size())
            notes[first].phraseStart = true;
    }

    //==============================================================================
    Solo finish()
    {
        // Double-stop partners stay right behind the note they belong to.
        std::stable_sort (notes.begin(), notes.end(), [] (const TNote& a, const TNote& b) { return a.start < b.start; });

        // Two melody notes can't start together: keep the first.
        for (size_t i = 1; i < notes.size();)
        {
            size_t previous = i - 1;

            while (previous > 0 && notes[previous].chordNote)
                --previous;

            if (! notes[i].chordNote && notes[i].start <= notes[previous].start)
                notes.erase (notes.begin() + (long) i);
            else
                ++i;
        }

        // One note at a time: a note ends where the next one starts.
        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto& n = notes[i];
            n.start = juce::jlimit (0, total - 2, n.start);
            size_t next = i + 1;

            while (next < notes.size() && (notes[next].chordNote || notes[next].start == n.start))
                ++next;

            if (next < notes.size())
                n.length = std::min (n.length, notes[next].start - n.start);

            n.length = juce::jlimit (1, total - n.start, n.length);
        }

        // A bend has to arrive well before its note ends, whatever happened to the note's length since.
        for (auto& n : notes)
        {
            if (n.art.bend != BendKind::none && n.art.bend != BendKind::curl)
            {
                const auto room = seconds (n.length) * (n.art.bend == BendKind::up ? 0.6f : 0.4f);
                const auto needed = n.art.bendDelay + n.art.bendTime;

                if (needed > room && needed > 0.0f)
                {
                    n.art.bendDelay *= room / needed;
                    n.art.bendTime *= room / needed;
                }
            }
        }

        decorate();

        Solo solo;
        solo.chords = chords;
        solo.scale = scale;
        solo.lengthBeats = (double) total / TPB;
        solo.bpm = p.bpm;
        solo.keyPc = p.keyPc;
        solo.minorKey = p.minorKey;

        const auto beatsPerSecond = p.bpm / 60.0;
        double previousStart = -1.0;

        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& t = notes[i];
            const auto duration = seconds (t.length);
            Note n;
            n.pitch = t.pitch;
            n.legato = t.legato;
            n.chordNote = t.chordNote;
            n.art = t.art;

            // Timing: nobody plays on a grid. Fast notes stay tighter, slow styles sit behind the beat.
            auto shift = 0.0;

            if (! t.chordNote)
            {
                const auto spread = style.jitterMs * (duration < 0.2f ? 0.45f : 1.0f);
                shift = ((double) (rf() + rf() + rf()) / 1.5 - 1.0) * spread * 0.001 + style.laybackMs * 0.001 * (duration < 0.2f ? 0.5 : 1.0);
            }

            n.start = (double) t.start / TPB + shift * beatsPerSecond;

            if (t.chordNote && ! solo.notes.empty())
                n.start = solo.notes.back().start;
            else
                n.start = std::max ({ 0.0, n.start, previousStart + 0.02 });

            // Length: picked notes leave a tiny gap, staccato ones are cut short, legato ones touch.
            auto length = (double) t.length / TPB;
            const auto nextIsLegato = i + 1 < notes.size() && notes[i + 1].legato;

            if (t.ghost)
                length = std::min (length, 0.07 * beatsPerSecond);
            else if (t.staccato)
                length *= rf (0.38f, 0.6f);
            else if (! nextIsLegato && duration < 1.0f)
                length *= rf (0.86f, 0.97f);

            n.length = std::max (0.03, std::min (length, solo.lengthBeats - n.start));

            // Dynamics follow the energy of the solo, with accents and softer legato notes.
            auto velocity = style.velBase + style.velRange * t.energy + (float) t.velocityOffset + rf (-5.0f, 5.0f);
            velocity += t.accent ? 11.0f : 0.0f;
            velocity += t.expressive ? 5.0f : 0.0f;
            velocity += t.phraseStart ? 5.0f : 0.0f;
            velocity -= t.legato ? 14.0f : 0.0f;

            if (t.ghost)
                velocity *= 0.45f;

            n.velocity = juce::jlimit (18, 124, juce::roundToInt (velocity));

            if (! t.chordNote)
                previousStart = n.start;

            solo.notes.push_back (n);
        }

        // Legato notes must touch the note before them.
        for (size_t i = 1; i < solo.notes.size(); ++i)
        {
            auto& n = solo.notes[i];

            if (n.legato && ! n.chordNote)
            {
                auto& previous = solo.notes[i - 1];
                previous.length = std::max (0.03, n.start - previous.start);
            }
        }

        return solo;
    }

    /** Vibrato, quarter-tone curls, slides and fall-offs on the notes that suit them. */
    void decorate()
    {
        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto& n = notes[i];

            if (n.chordNote || n.ghost)
                continue;

            const auto duration = seconds (n.length);
            auto& a = n.art;
            const auto degree = pc (n.pitch - p.keyPc);

            // A slow smear upwards on the blue-sounding degrees.
            if (a.bend == BendKind::none && duration > 0.22f && duration < 1.3f
                && (degree == 3 || degree == 10 || (p.minorKey && degree == 5))
                && chance (style.curl * bendChance() * 0.8f))
            {
                a.bend = BendKind::curl;
                a.bendSemis = rf (0.25f, 0.55f);
                a.bendTime = duration * rf (0.5f, 0.85f);
                a.bendDelay = duration * rf (0.0f, 0.15f);
            }

            // A medium note inside a phrase is often reached by a quick bend from the scale tone below.
            const auto previousBent = i > 0 && notes[i - 1].art.bend != BendKind::none;

            if (a.bend == BendKind::none && a.slideIn == 0.0f && ! n.legato && ! n.staccato && ! previousBent
                && duration > 0.3f && duration < 1.5f && chance (0.2f * bendChance()))
            {
                float semis = 0.0f;
                const auto source = bendSource (n.pitch, semis);

                if (source >= kLow)
                {
                    n.pitch = source;
                    a.bend = duration > 0.45f && chance (0.3f) ? BendKind::upAndRelease : BendKind::up;
                    a.bendSemis = semis;
                    a.bendTime = std::min (rf (0.05f, 0.15f), duration * 0.4f);
                    a.releaseAt = rf (0.6f, 0.8f);
                    a.releaseTime = std::min (rf (0.07f, 0.16f), duration * 0.25f);
                }
            }

            // Slide into a note after a leap or at the start of a phrase.
            const auto leap = i > 0 ? std::abs (n.pitch - notes[i - 1].pitch) : 0;

            if (a.bend == BendKind::none && a.slideIn == 0.0f && ! n.legato && duration > 0.15f
                && (n.phraseStart || leap >= 5) && chance (style.slide * 0.45f))
            {
                a.slideIn = chance (0.8f) ? -(inScale (n.pitch - 2) ? 2.0f : 1.0f) : 1.0f;
                a.slideTime = rf (0.04f, 0.09f);
            }

            // Let the last note of a phrase drop away now and then.
            if (n.phraseEnd && i + 1 < notes.size() && a.bend == BendKind::none && duration > 0.25f && duration < 0.9f && chance (0.12f))
                a.fallOff = -rf (1.2f, 2.0f);

            if (a.vibrato.depth > 0.0f || a.bend == BendKind::curl || duration < 0.28f)
                continue;

            // Vibrato: the longer the note, the more likely, but never a rule.
            // Each note gets its own width, speed and moment of entry.
            const auto bendSettles = a.bend == BendKind::none ? 0.0f : a.bendDelay + a.bendTime;
            const auto room = duration - bendSettles;

            if (room < 0.25f)
                continue;

            auto probability = (room > 1.2f ? 0.92f : (room > 0.7f ? 0.7f : (room > 0.45f ? 0.4f : 0.15f)))
                             * (0.3f + 0.95f * p.vibrato) * style.vibProb;

            if (n.expressive)
                probability *= 1.35f;

            if (! chance (std::min (0.97f, probability)))
                continue;

            auto& v = a.vibrato;
            const auto character = pick ({ 0.8f, 1.6f, 1.0f, 2.0f * style.vibLate, 0.9f }); // subtle, normal, wide, late, swelling
            v.depth = rf (style.vibLo, style.vibHi) * 0.01f * (0.55f + 0.7f * p.vibrato);
            v.rateHz = rf (style.rateLo, style.rateHi);
            v.delay = room > 0.8f ? rf (0.06f, 0.3f) : rf (0.0f, 0.08f);
            v.grow = rf (0.08f, 0.3f);
            v.drift = rf (-0.08f, 0.1f);

            switch (character)
            {
                case 0: v.depth *= 0.5f; v.rateHz *= 1.06f; break;
                case 2: v.depth *= 1.4f; v.rateHz *= 0.93f; break;
                case 3: v.delay = room * rf (0.4f, 0.62f); v.grow = rf (0.15f, 0.35f); break;
                case 4: v.depth *= 0.6f; v.swell = rf (0.6f, 1.2f); v.drift = rf (0.0f, 0.12f); break;
                default: break;
            }

            if (a.bend == BendKind::up || a.bend == BendKind::twoStage)
                v.depth = std::min (v.depth, a.bendSemis * 0.3f); // stay well inside the bend
        }
    }

    //==============================================================================
    const GeneratorParams p;
    const Style& style;
    juce::Random random;

    std::vector<Chord> chords;
    std::array<int, 7> scale {};
    std::array<bool, 12> scalePcs {}, pentaPcs {};
    std::vector<int> penta, full;
    int total = 0;

    std::vector<TNote> notes;
    int current = 64, lastTarget = -1, lastKind = -1;

    struct Memory
    {
        std::vector<int> offsets, steps;
        bool valid = false;
    } memory;
};
} // namespace

//==============================================================================
Solo generateSolo (const GeneratorParams& params)
{
    auto p = params;
    p.bars = juce::jlimit (2, 32, p.bars);
    p.bpm = juce::jlimit (30.0, 300.0, p.bpm);
    return Generator (p).run();
}

juce::StringArray getProgressionNames (bool minorKey)
{
    juce::StringArray names;

    for (const auto& progression : progressions (minorKey))
        names.add (pl (progression.name));

    return names;
}

std::vector<Chord> makeChords (int keyPc, bool minorKey, int progression, int bars)
{
    const auto& all = progressions (minorKey);
    const auto& chosen = all[(size_t) juce::jlimit (0, (int) all.size() - 1, progression)];
    std::vector<Chord> chords;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& def = chosen.chords[(size_t) bar % chosen.chords.size()];
        Chord chord;
        chord.rootPc = pc (keyPc + def.semitones);
        chord.minor = def.minor;
        chord.seventh = def.seventh;
        chords.push_back (chord);
    }

    return chords;
}
} // namespace solista
