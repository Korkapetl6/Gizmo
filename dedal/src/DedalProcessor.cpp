#include "DedalProcessor.h"
#include "DedalEditor.h"
#include "DedalParams.h"

using namespace dedal;

namespace
{
const juce::Identifier seqType ("SEQ"), bufferType ("BUFFER");

juce::String percent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String bipolar (float v, int) { return (v > 0.004f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DedalProcessor::createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&layout] (const char* id, const juce::String& name, juce::NormalisableRange<float> range, float initial,
                          std::function<juce::String (float, int)> text)
    {
        layout.add (std::make_unique<Float> (juce::ParameterID { id, 1 }, name, range, initial,
                                             Attributes().withStringFromValueFunction (std::move (text))));
    };

    auto unit = [&add] (const char* id, const juce::String& name, float initial) { add (id, name, { 0.0f, 1.0f, 0.001f }, initial, percent); };
    auto signedUnit = [&add] (const char* id, const juce::String& name, float initial) { add (id, name, { -1.0f, 1.0f, 0.001f }, initial, bipolar); };

    auto choice = [&layout] (const char* id, const juce::String& name, const juce::StringArray& options, int initial)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, options, initial));
    };

    auto toggle = [&layout] (const char* id, const juce::String& name, bool initial)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, initial));
    };

    auto seconds = [] (float v, int) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; };
    auto semis = [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + pl (" półt."); };
    auto pitchAmount = [] (float v, int) { return v >= 0.995f ? juce::String ("QTZ") : juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    for (int k = 0; k < 2; ++k)
    {
        const auto n = " " + juce::String (k + 1);
        unit (ID::corrupt[k], "Corrupt" + n, 0.0f);
        add (ID::range[k], "Zakres" + n, { 0.0f, 36.0f, 0.01f }, k == 0 ? 12.0f : 7.0f,
             [] (float v, int) { return pl ("±") + juce::String (v, v < 10.0f ? 1 : 0) + pl (" półt."); });
        choice (ID::rate[k], "Krok" + n, getRateNames(), 4);
        unit (ID::swing[k], "Swing" + n, 0.0f);
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ID::length[k], 1 }, pl ("Długość") + n, 1, 8, 8));
        toggle (ID::flip[k], "Bit flip" + n, false);
    }

    toggle (ID::chain, "Chain", false);
    choice (ID::scale, "Skala", getScaleNames(), 2);
    choice (ID::key, "Tonacja", { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 0);
    choice (ID::octave, "Oktawa", { "0", "1", "2", "3", "4", "5", "6" }, 2);
    choice (ID::polarity, "Polaryzacja", { pl ("±"), "+" }, 0);
    add (ID::trigMix, "EG trig mix", { 0.0f, 1.0f, 0.001f }, 0.5f, [] (float v, int)
    {
        if (v < 0.02f) return juce::String ("SEQ1");
        if (v > 0.98f) return juce::String ("SEQ2");
        return juce::String (juce::roundToInt ((1.0f - v) * 100.0f)) + " / " + juce::String (juce::roundToInt (v * 100.0f));
    });
    toggle (ID::midiTranspose, "Transpozycja MIDI", true);
    toggle (ID::trigger, "Trigger", false);

    choice (ID::vcoType, "VCO typ", { "Analog", "Beads" }, 0);
    choice (ID::vcoBank, "VCO bank", WaveBanks::getBankNames(), 0);
    unit (ID::vcoWave, "VCO fala", 0.0f);
    add (ID::vcoPw, "VCO PW", { 0.05f, 0.95f, 0.001f }, 0.5f, percent);
    add (ID::vcoTune, pl ("VCO strój"), { -24.0f, 24.0f, 1.0f }, 0.0f, semis);
    add (ID::vcoFine, pl ("VCO dostrój"), { -50.0f, 50.0f, 0.1f }, 0.0f, [] (float v, int) { return juce::String (v, 1) + " ct"; });
    add (ID::seq1Pitch, pl ("SEQ1 → VCO"), { 0.0f, 1.0f, 0.001f }, 1.0f, pitchAmount);
    unit (ID::seq1Wave, pl ("SEQ1 → fala"), 0.0f);
    signedUnit (ID::eg1Vco, pl ("EG1 → VCO"), 0.0f);
    add (ID::glide, pl ("Poślizg"), { 0.0f, 1.0f, 0.001f, 0.35f }, 0.0f, seconds);

    choice (ID::modType, "MOD typ", { "Analog", "Beads" }, 0);
    choice (ID::modBank, "MOD bank", WaveBanks::getBankNames(), 0);
    unit (ID::modWave, "MOD fala", 1.0f / 3.0f);
    add (ID::modPw, "MOD PW", { 0.05f, 0.95f, 0.001f }, 0.5f, percent);
    add (ID::modTune, pl ("MOD strój"), { -36.0f, 36.0f, 1.0f }, 0.0f, semis);
    choice (ID::modMode, "MOD tryb", { "Audio", "LFO" }, 0);
    add (ID::modRate, "MOD LFO", { 0.02f, 40.0f, 0.001f, 0.3f }, 2.0f, [] (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; });
    toggle (ID::modSync, "MOD sync", false);
    add (ID::seq2Pitch, pl ("SEQ2 → MOD"), { 0.0f, 1.0f, 0.001f }, 1.0f, pitchAmount);
    unit (ID::seq2Wave, pl ("SEQ2 → fala"), 0.0f);
    signedUnit (ID::eg1Mod, pl ("EG1 → MOD"), 0.0f);

    unit (ID::lvlVco, "Poziom VCO", 0.5f);
    unit (ID::lvlMod, "Poziom MOD", 0.0f);
    unit (ID::lvlRing, "Ring mod", 0.0f);
    unit (ID::lvlNoise, "Szum", 0.0f);
    unit (ID::noiseTone, "Barwa szumu", 0.5f);
    unit (ID::fm, "FM", 0.0f);

    unit (ID::fold, "Fold", 0.15f);
    signedUnit (ID::bias, "Bias", 0.0f);
    unit (ID::seq1Fold, pl ("SEQ1 → fold"), 0.0f);
    signedUnit (ID::eg1Fold, pl ("EG1 → fold"), 0.3f);

    add (ID::cutoff, pl ("Odcięcie"), { 0.0f, 1.0f, 0.001f }, 0.55f, [] (float v, int)
    {
        const auto hz = 20.0f * std::exp2 (v * 10.0f);
        return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, 1) + " kHz";
    });
    unit (ID::resonance, "Rezonans", 0.3f);
    add (ID::filterType, pl ("LP ↔ BP"), { 0.0f, 1.0f, 0.001f }, 0.0f, [] (float v, int)
    {
        return v < 0.02f ? juce::String ("LP") : (v > 0.98f ? juce::String ("BP") : juce::String (juce::roundToInt (v * 100.0f)) + " % BP");
    });
    unit (ID::seq2Cutoff, pl ("SEQ2 → filtr"), 0.0f);
    signedUnit (ID::eg1Cutoff, pl ("EG1 → filtr"), 0.4f);

    choice (ID::routing, pl ("Kolejność"), { pl ("Równolegle"), pl ("Fold → filtr"), pl ("Filtr → fold") }, 0);
    add (ID::blend, "Blend", { 0.0f, 1.0f, 0.001f }, 0.5f, [] (float v, int)
    {
        return v < 0.02f ? juce::String ("Fold") : (v > 0.98f ? juce::String ("Filtr") : juce::String (juce::roundToInt (v * 100.0f)) + " % filtr");
    });
    add (ID::eg1Decay, "EG1 decay", { 0.01f, 5.0f, 0.001f, 0.3f }, 0.25f, seconds);
    add (ID::eg2Decay, "EG2 decay", { 0.01f, 5.0f, 0.001f, 0.3f }, 0.45f, seconds);
    choice (ID::vcaW, "VCA fold", { "EG2", "EG1", "Otwarty" }, 0);
    choice (ID::vcaF, "VCA filtr", { "EG2", "EG1", "Otwarty" }, 0);
    add (ID::volume, pl ("Głośność"), { -30.0f, 6.0f, 0.1f }, -6.0f, [] (float v, int) { return juce::String (v, 1) + " dB"; });
    toggle (ID::sound, pl ("Własne brzmienie"), true);

    choice (ID::midiSeq, "Nuty MIDI z", { "SEQ1", "SEQ2", "Oba" }, 2);
    toggle (ID::midiSend, pl ("Wysyłaj MIDI"), true);
    add (ID::gate, pl ("Długość nut"), { 0.05f, 1.0f, 0.001f }, 0.5f, percent);
    toggle (ID::ccSend, "CC z SEQ2", false);
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ID::ccNumber, 1 }, "Numer CC", 1, 119, 74));

    for (int k = 0; k < 2; ++k)
    {
        const auto n = " " + juce::String (k + 1);
        choice (ID::direction[k], "Kierunek" + n, { pl ("Przód"), pl ("Tył"), pl ("Wahadło"), "Losowo" }, 0);
        unit (ID::chance[k], "Szansa" + n, 1.0f);
    }

    // SEQ C: the rhythm sequencer and the logic it drives
    const juce::StringArray targets { pl ("Wył."), "SEQ1", "SEQ2", "Oba" };
    const juce::StringArray when { "Start C", "Akcent C", pl ("Każdy krok C") };
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ID::lengthC, 1 }, pl ("Długość C"), 1, 16, 16));
    choice (ID::rateC, "Krok C", getRateNames(), 7);
    unit (ID::swingC, "Swing C", 0.0f);
    unit (ID::corruptC, "Corrupt C", 0.0f);
    unit (ID::cEg, pl ("C → EG"), 0.0f);
    choice (ID::cReset, pl ("C → reset"), targets, 0);
    choice (ID::cResetOn, "Reset przy", when, 0);
    choice (ID::cDir, pl ("C → kierunek"), targets, 0);
    choice (ID::cDirOn, "Kierunek przy", when, 1);
    choice (ID::cLength, pl ("C → długość"), targets, 0);
    unit (ID::cLengthAmt, pl ("Ile długości"), 0.5f);
    choice (ID::cChance, pl ("C → szansa"), targets, 0);
    unit (ID::cChanceAmt, "Ile szansy", 0.5f);
    choice (ID::cTrans, pl ("C → transpozycja"), targets, 0);
    add (ID::cTransAmt, "Ile transpozycji", { 0.0f, 24.0f, 0.01f }, 7.0f,
         [] (float v, int) { return pl ("±") + juce::String (v, v < 10.0f ? 1 : 0) + pl (" półt."); });
    toggle (ID::cMidi, "Nuty C w MIDI", false);
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ID::cNote, 1 }, "Nuta C", 0, 127, 36));
    return layout;
}

DedalProcessor::DedalProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DEDAL", createLayout())
{
    cv[0] = cv[1] = 0.0f;
    offAt.fill (-1);
    loadFactoryPreset (0);
    startTimerHz (10);
}

DedalProcessor::~DedalProcessor()
{
    stopTimer();
}

bool DedalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DedalProcessor::setParameter (const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

Layout DedalProcessor::readLayout() const
{
    Layout layout;
    layout.length[0] = juce::jlimit (1, 8, index (ID::length[0]));
    layout.length[1] = juce::jlimit (1, 8, index (ID::length[1]));
    layout.chain = value (ID::chain) > 0.5f;
    return layout;
}

StepParams DedalProcessor::readStepParams() const
{
    StepParams p;
    p.layout = readLayout();
    p.lengthC = juce::jlimit (1, kStepsC, index (ID::lengthC));
    p.timing[0] = { rateToBeats (index (ID::rate[0])), value (ID::swing[0]) };
    p.timing[1] = { rateToBeats (index (ID::rate[1])), value (ID::swing[1]) };
    p.timing[2] = { rateToBeats (index (ID::rateC)), value (ID::swingC) };
    p.corrupt[0] = value (ID::corrupt[0]);
    p.corrupt[1] = value (ID::corrupt[1]);
    p.corrupt[2] = value (ID::corruptC);

    for (int k = 0; k < 2; ++k)
    {
        p.direction[k] = index (ID::direction[k]);
        p.chance[k] = value (ID::chance[k]);
    }

    p.cReset = index (ID::cReset);
    p.cResetOn = index (ID::cResetOn);
    p.cDir = index (ID::cDir);
    p.cDirOn = index (ID::cDirOn);
    p.cLength = index (ID::cLength);
    p.cLengthAmt = value (ID::cLengthAmt);
    p.cChance = index (ID::cChance);
    p.cChanceAmt = value (ID::cChanceAmt);
    p.cTrans = index (ID::cTrans);
    p.cTransAmt = value (ID::cTransAmt);
    return p;
}

namespace
{
Layout effectiveLayout (const Layout& layout, const SeqState& state)
{
    auto l = layout;

    for (int k = 0; k < 2; ++k)
        if (state.lengthOverride[(size_t) k] > 0)
            l.length[k] = state.lengthOverride[(size_t) k];

    return l;
}

/** Steps all three sequencers through [.., beat1) in time order, SEQ C first when they meet, so its
    logic (reset, direction, length, chance, transposition) acts on the steps that fall with it. */
template <typename Emit>
void runSteps (Sequencers& s, const StepParams& p, std::array<juce::int64, 3>& next, double beat1, Emit&& emit)
{
    for (;;)
    {
        int k = -1;
        double start = beat1;

        for (const int i : { 2, 0, 1 })
        {
            const auto t = p.timing[i].startOf (next[(size_t) i]);

            if (t < start - 1.0e-12)
            {
                start = t;
                k = i;
            }
        }

        if (k < 0)
            return;

        StepEvent e;
        e.k = k;
        e.beat = start;
        e.stepBeats = p.timing[k].stepBeats;

        if (k == 2)
        {
            e.r = s.stepC (p.lengthC, p.corrupt[2]);
            const auto& r = e.r;
            auto when = [&r] (int on) { return on == 0 ? r.cycleStart : (on == 1 ? r.accent : r.on); };
            auto hits = [] (int target, int seq) { return target == 3 || target == seq + 1; };

            for (int seq = 0; seq < 2; ++seq)
            {
                if (hits (p.cReset, seq) && when (p.cResetOn))
                    s.reset (p.layout, seq);

                if (hits (p.cDir, seq) && when (p.cDirOn))
                    s.reverse (seq);

                if (! r.on)
                    continue;

                const auto amount01 = (r.value + 1.0f) * 0.5f;

                if (hits (p.cLength, seq) && p.cLengthAmt > 0.0f)
                {
                    const auto base = (float) p.layout.length[seq];
                    const auto goal = 1.0f + std::round (amount01 * 7.0f);
                    s.state.lengthOverride[(size_t) seq] = juce::jlimit (1, kSteps, juce::roundToInt (base + (goal - base) * p.cLengthAmt));
                }

                if (hits (p.cChance, seq))
                    s.state.chanceMod[(size_t) seq] = r.value * p.cChanceAmt;

                if (hits (p.cTrans, seq))
                {
                    e.setsTranspose[seq] = true;
                    e.transpose[seq] = r.value * p.cTransAmt;
                }
            }
        }
        else
        {
            const auto layout = effectiveLayout (p.layout, s.state);
            const auto chance = juce::jlimit (0.0f, 1.0f, p.chance[k] + s.state.chanceMod[(size_t) k]);
            e.r = s.step (layout, k, p.corrupt[k], p.direction[k], chance);
        }

        emit (e);
        ++next[(size_t) k];
    }
}
} // namespace

//==============================================================================
Snapshot DedalProcessor::getSnapshot() const
{
    Snapshot s;
    s.lengthC = juce::jlimit (1, kStepsC, index (ID::lengthC));
    const auto layout = readLayout();
    const juce::SpinLock::ScopedLockType lock (seqLock);
    s.state = sequencers.state;
    s.layout = effectiveLayout (layout, s.state);

    for (int k = 0; k < 2; ++k)
    {
        s.head[(size_t) k] = sequencers.currentHead (s.layout, k);
        s.writeHead[(size_t) k] = sequencers.writeHead (s.layout, k);
    }

    s.head[2] = wrap (s.state.played[2] ? s.state.head[2] : s.state.nextHead[2], s.lengthC);
    s.running = runningNow.load();
    return s;
}

void DedalProcessor::command (Command c, int k)
{
    const auto layout = readLayout();

    if (c == Command::length)
    {
        // As on the panel: one shorter per press, and from 1 back to the longest.
        const auto* id = k == 2 ? ID::lengthC : ID::length[k];
        const auto longest = k == 2 ? kStepsC : kSteps;
        const auto length = index (id);
        setParameter (id, (float) (length <= 1 ? longest : length - 1));
        return;
    }

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        auto& s = sequencers;
        const auto playing = effectiveLayout (layout, s.state);

        switch (c)
        {
            case Command::bitFlip:      if (k < 2) s.bitFlip (playing, k); break;
            case Command::bitShift:     if (k < 2) s.bitShift (playing, k); break;
            case Command::unshift:      if (k < 2) s.unshift (playing, k); break;
            case Command::advance:      s.advance (playing, k, juce::jlimit (1, kStepsC, index (ID::lengthC))); break;
            case Command::reset:        s.reset (playing, k); break;
            case Command::reverse:      if (k < 2) s.reverse (k); break;
            case Command::writeAdvance: if (k < 2) s.moveWriteHead (playing, k, false); break;
            case Command::writeReset:   if (k < 2) s.moveWriteHead (playing, k, true); break;
            case Command::randomise:    if (k < 2) s.randomise (k); else s.randomiseC(); break;
            case Command::clear:        s.clear (k); break;
            case Command::length:       break;
        }
    }

    sendChangeMessage();
}

void DedalProcessor::toggleSlot (int slot)
{
    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        sequencers.flipSlot ((size_t) juce::jlimit (0, 15, slot));
    }
    sendChangeMessage();
}

void DedalProcessor::setSlotValue (int slot, float newValue)
{
    const juce::SpinLock::ScopedLockType lock (seqLock);
    sequencers.state.values[(size_t) juce::jlimit (0, 15, slot)] = juce::jlimit (-1.0f, 1.0f, newValue);
}

void DedalProcessor::toggleC (int step)
{
    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        sequencers.flipC ((size_t) juce::jlimit (0, kStepsC - 1, step));
    }
    sendChangeMessage();
}

void DedalProcessor::toggleAccentC (int step)
{
    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        auto& accent = sequencers.state.accentC[(size_t) juce::jlimit (0, kStepsC - 1, step)];
        accent = ! accent;
    }
    sendChangeMessage();
}

void DedalProcessor::setChanceC (int step, float chance)
{
    const juce::SpinLock::ScopedLockType lock (seqLock);
    sequencers.state.chanceC[(size_t) juce::jlimit (0, kStepsC - 1, step)] = juce::jlimit (0.0f, 1.0f, chance);
}

void DedalProcessor::setValueC (int step, float newValue)
{
    const juce::SpinLock::ScopedLockType lock (seqLock);
    sequencers.state.valueC[(size_t) juce::jlimit (0, kStepsC - 1, step)] = juce::jlimit (-1.0f, 1.0f, newValue);
}

void DedalProcessor::nudgeChainOffset (int by)
{
    const auto layout = readLayout();
    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        sequencers.setChainOffset (effectiveLayout (layout, sequencers.state), by == 0 ? 0 : sequencers.state.chainOffset + by);
    }
    sendChangeMessage();
}

void DedalProcessor::saveBuffer()
{
    juce::ValueTree tree;
    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        tree = toValueTree (sequencers.state, bufferType);
    }

    for (const auto* id : { ID::length[0], ID::length[1], ID::lengthC, ID::chain, ID::scale })
        tree.setProperty (id, value (id), nullptr);

    buffer = tree;
    sendChangeMessage();
}

void DedalProcessor::recallBuffer()
{
    if (! buffer.isValid())
        return;

    for (const auto* id : { ID::length[0], ID::length[1], ID::lengthC, ID::chain, ID::scale })
        if (buffer.hasProperty (id))
            setParameter (id, (float) buffer.getProperty (id));

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        fromValueTree (buffer, sequencers.state);
    }
    sendChangeMessage();
}

//==============================================================================
int DedalProcessor::rootNote() const
{
    const auto t = transposeNote.load();

    if (t >= 0 && value (ID::midiTranspose) > 0.5f)
        return t;

    return 12 * (index (ID::octave) + 2) + index (ID::key);
}

float DedalProcessor::semitonesOf (int k, float v) const
{
    if (index (ID::polarity) == 1)
        v = (v + 1.0f) * 0.5f;

    return v * value (ID::range[k]);
}

int DedalProcessor::midiNoteOf (int k, float cvValue, int root, float transposition) const
{
    return juce::jlimit (0, 127, root + juce::roundToInt (quantize (index (ID::scale), semitonesOf (k, cvValue) + transposition)));
}

SynthTargets DedalProcessor::computeTargets (int root) const
{
    const auto scale = index (ID::scale);

    auto pitch = [&] (int k, const char* amountId)
    {
        const auto amount = value (amountId), s = semitonesOf (k, cv[(size_t) k].load()) + transpose[(size_t) k];
        return amount >= 0.995f ? quantize (scale, s) : amount * s;
    };

    SynthTargets t;
    t.vcoNote = (float) root + value (ID::vcoTune) + value (ID::vcoFine) * 0.01f + pitch (0, ID::seq1Pitch);
    t.modNote = index (ID::modMode) == 1 ? 60.0f + pitch (1, ID::seq2Pitch)
                                         : (float) root + value (ID::modTune) + pitch (1, ID::seq2Pitch);

    // Sequencer CV for the other destinations, after CV RANGE: full range is 1.
    const auto n1 = semitonesOf (0, cv[0].load()) / 36.0f, n2 = semitonesOf (1, cv[1].load()) / 36.0f;
    t.foldMod = value (ID::seq1Fold) * n1;
    t.cutoffMod = value (ID::seq2Cutoff) * n2 * 5.0f;
    t.waveMod1 = value (ID::seq1Wave) * n1;
    t.waveMod2 = value (ID::seq2Wave) * n2;
    return t;
}

SynthParams DedalProcessor::readSynthParams() const
{
    SynthParams p;
    p.vco = { index (ID::vcoType), index (ID::vcoBank), value (ID::vcoWave), value (ID::vcoPw) };
    p.mod = { index (ID::modType), index (ID::modBank), value (ID::modWave), value (ID::modPw) };
    p.modLfo = index (ID::modMode) == 1;
    p.modSync = value (ID::modSync) > 0.5f;
    p.modLfoHz = value (ID::modRate);
    p.fm = value (ID::fm);
    p.lvlVco = value (ID::lvlVco);
    p.lvlMod = value (ID::lvlMod);
    p.lvlRing = value (ID::lvlRing);
    p.lvlNoise = value (ID::lvlNoise);
    p.noiseTone = value (ID::noiseTone);
    p.fold = value (ID::fold);
    p.bias = value (ID::bias);
    p.eg1Fold = value (ID::eg1Fold);
    p.cutoff = value (ID::cutoff);
    p.resonance = value (ID::resonance);
    p.filterType = value (ID::filterType);
    p.eg1Cutoff = value (ID::eg1Cutoff);
    p.eg1Vco = value (ID::eg1Vco);
    p.eg1Mod = value (ID::eg1Mod);
    p.routing = index (ID::routing);
    p.blend = value (ID::blend);
    p.eg1Decay = value (ID::eg1Decay);
    p.eg2Decay = value (ID::eg2Decay);
    p.vcaW = index (ID::vcaW);
    p.vcaF = index (ID::vcaF);
    p.glide = value (ID::glide);
    p.gain = juce::Decibels::decibelsToGain (value (ID::volume), -80.0f);
    return p;
}

//==============================================================================
void DedalProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    mono.assign ((size_t) std::max (32, samplesPerBlock), 0.0f);
    synth.setParams (readSynthParams());
    synth.setTargets (computeTargets (rootNote()));
    synth.prepare (sampleRate, samplesPerBlock);
    wasRunning = wasHostPlaying = false;
    offAt.fill (-1);
}

void DedalProcessor::pushCapture (const CapturedNote& note)
{
    const auto scope = fifo.write (1);

    if (scope.blockSize1 > 0)
        fifoData[(size_t) scope.startIndex1] = note;
}

void DedalProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = audio.getNumSamples();
    audio.clear();

    if (numSamples == 0)
        return;

    // Host tempo and position
    auto bpm = hostBpm.load();
    auto playing = false, hasPosition = false;
    double ppq = 0.0;

    if (auto* head = getPlayHead())
    {
        if (const auto position = head->getPosition())
        {
            if (const auto tempo = position->getBpm(); tempo.hasValue() && *tempo > 20.0 && *tempo < 1000.0)
                bpm = *tempo;

            playing = position->getIsPlaying();

            if (const auto quarters = position->getPpqPosition())
            {
                ppq = *quarters;
                hasPosition = true;
            }

            if (const auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    barBeats = 4.0 * signature->numerator / signature->denominator;
        }
    }

    hostBpm = bpm;
    hostPlaying = playing;

    // Incoming notes transpose the root, as on the hardware; turning Tonacja or Oktawa takes it back.
    if (const auto keyParams = index (ID::key) + 12 * index (ID::octave); keyParams != lastKeyParams)
    {
        transposeNote = -1;
        lastKeyParams = keyParams;
    }

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();

        if (m.isNoteOn())
            transposeNote = m.getNoteNumber();
    }

    midi.clear();

    // The clock
    const auto hostRun = playing && hasPosition;
    const auto running = hostRun || run.load();
    const auto beat0 = hostRun ? ppq : internalBeat;
    const auto beatsPerSample = bpm / (60.0 * sampleRate);
    const auto beat1 = beat0 + numSamples * beatsPerSample;
    const auto jumped = running && (! wasRunning || hostRun != wasHostPlaying
                                    || std::abs (beat0 - expectedBeat) > std::max (0.02, 4.0 * numSamples * beatsPerSample));

    const auto params = readStepParams();
    const auto trigMix = value (ID::trigMix);
    const float weight[2] = { trigMix <= 0.5f ? 1.0f : 2.0f * (1.0f - trigMix), trigMix >= 0.5f ? 1.0f : 2.0f * trigMix };
    const auto midiSeq = index (ID::midiSeq);
    const auto sendMidi = value (ID::midiSend) > 0.5f;
    const auto sendCc = value (ID::ccSend) > 0.5f;
    const auto ccNumber = juce::jlimit (1, 119, index (ID::ccNumber));
    const auto gate = value (ID::gate);
    const auto cEg = value (ID::cEg);
    const auto cMidi = value (ID::cMidi) > 0.5f;
    const auto cNote = juce::jlimit (0, 127, index (ID::cNote));
    int numEvents = 0;
    auto manualTrigger = false;

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);

        // BIT FLIP and TRIGGER as parameters, so they can sit on a macro or a controller.
        for (int k = 0; k < 2; ++k)
        {
            const auto down = value (ID::flip[k]) > 0.5f;

            if (down && ! lastFlip[(size_t) k])
                sequencers.bitFlip (effectiveLayout (params.layout, sequencers.state), k);

            lastFlip[(size_t) k] = down;
        }

        const auto triggerDown = value (ID::trigger) > 0.5f;
        manualTrigger = triggerDown && ! lastTrigger;
        lastTrigger = triggerDown;

        if (jumped)
        {
            // A start from the host (or from RUN) resets the play heads, like MIDI Start.
            if (! wasRunning || hostRun != wasHostPlaying)
            {
                sequencers.restart();
                transpose = { 0.0f, 0.0f };
            }

            session = session.load() + 1;
            sessionStart = beat0;
        }

        if (running)
        {
            for (int k = 0; k < 3; ++k)
            {
                const auto& timing = params.timing[k];

                if (jumped || std::abs (timing.stepBeats - lastStepBeats[(size_t) k]) > 1.0e-12 || std::abs (timing.swing - lastSwing[(size_t) k]) > 1.0e-4f)
                {
                    nextStep[(size_t) k] = timing.firstAtOrAfter (beat0);
                    lastStepBeats[(size_t) k] = timing.stepBeats;
                    lastSwing[(size_t) k] = timing.swing;
                }
            }

            runSteps (sequencers, params, nextStep, beat1, [&] (const StepEvent& e)
            {
                if (numEvents < (int) events.size())
                    events[(size_t) numEvents++] = e;
            });
        }
    }

    // Notes that should end inside this block
    auto flushNoteOffs = [&] (juce::int64 until)
    {
        for (int note = 0; note < 128; ++note)
        {
            if (offAt[(size_t) note] >= 0 && offAt[(size_t) note] <= until)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, note), (int) juce::jlimit<juce::int64> (0, numSamples - 1, offAt[(size_t) note] - sampleClock));
                offAt[(size_t) note] = -1;
            }
        }
    };

    if (! running && wasRunning)
        flushNoteOffs (std::numeric_limits<juce::int64>::max());

    const auto sound = value (ID::sound) > 0.5f;
    synth.setParams (readSynthParams());
    auto root = rootNote();
    synth.setTargets (computeTargets (root));

    if (manualTrigger)
        synth.trigger (1.0f, 1.0f);

    auto* left = audio.getWritePointer (0);
    auto* right = audio.getNumChannels() > 1 ? audio.getWritePointer (1) : nullptr;
    int cursor = 0;

    auto renderTo = [&] (int end)
    {
        while (cursor < end)
        {
            const auto n = std::min (end - cursor, (int) mono.size());

            if (sound)
            {
                synth.render (mono.data(), n);
                std::copy (mono.begin(), mono.begin() + n, left + cursor);

                if (right != nullptr)
                    std::copy (mono.begin(), mono.begin() + n, right + cursor);
            }

            cursor += n;
        }
    };

    auto playNote = [&] (int note, int velocity, int offset, double lengthBeats, double beat, int seq)
    {
        if (sendMidi)
        {
            if (offAt[(size_t) note] >= 0)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), offset);

            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), offset);
            offAt[(size_t) note] = sampleClock + offset + std::max ((juce::int64) (0.005 * sampleRate), (juce::int64) (lengthBeats / beatsPerSample));
        }

        pushCapture ({ beat, lengthBeats, note, velocity, seq, session.load() });
    };

    for (int i = 0; i < numEvents; ++i)
    {
        const auto& e = events[(size_t) i];
        const auto offset = juce::jlimit (0, numSamples - 1, (int) std::floor ((e.beat - beat0) / beatsPerSample));
        renderTo (offset);
        flushNoteOffs (sampleClock + offset);

        if (e.k == 2)
        {
            for (int k = 0; k < 2; ++k)
                if (e.setsTranspose[k])
                    transpose[(size_t) k] = e.transpose[k];

            if (e.setsTranspose[0] || e.setsTranspose[1])
                synth.setTargets (computeTargets (root));

            if (e.r.on)
            {
                const auto accent = e.r.accent ? 1.0f : 0.6f;

                if (cEg > 0.0f)
                    synth.trigger (cEg * accent, cEg * accent);

                if (cMidi)
                    playNote (cNote, e.r.accent ? 127 : 88, offset, (double) gate * e.stepBeats, e.beat, 2);
            }

            continue;
        }

        if (! e.r.on || weight[e.k] < 0.02f)
            continue;

        cv[(size_t) e.k] = e.r.value;
        synth.setTargets (computeTargets (root));
        synth.trigger (weight[e.k], weight[e.k]);

        if (midiSeq == e.k || midiSeq == 2)
            playNote (midiNoteOf (e.k, e.r.value, root, transpose[(size_t) e.k]),
                      juce::jlimit (1, 127, juce::roundToInt (30.0f + 97.0f * weight[e.k])), offset, (double) gate * e.stepBeats, e.beat, e.k);

        if (sendCc && e.k == 1)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, ccNumber, juce::jlimit (0, 127, 64 + juce::roundToInt (semitonesOf (1, e.r.value) / 36.0f * 63.0f))), offset);
    }

    renderTo (numSamples);
    flushNoteOffs (sampleClock + numSamples - 1);

    if (running)
        currentBeat = beat1;

    if (! hostRun && run.load())
        internalBeat = beat1;
    else if (! running)
        internalBeat = 0.0;

    expectedBeat = beat1;
    wasRunning = running;
    runningNow = running;
    wasHostPlaying = hostRun;
    sampleClock += numSamples;
}

//==============================================================================
void DedalProcessor::collectCapture()
{
    const auto ready = fifo.getNumReady();

    if (ready > 0)
    {
        const auto scope = fifo.read (ready);

        auto take = [this] (int start, int count)
        {
            for (int i = start; i < start + count; ++i)
            {
                const auto& n = fifoData[(size_t) i];

                if (n.session != capturedSession)
                {
                    captured.clear();
                    capturedSession = n.session;
                }

                captured.push_back (n);
            }
        };

        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    // Keep the last 64 bars.
    const auto keepFrom = currentBeat.load() - 64.0 * barBeats.load();

    while (! captured.empty() && captured.front().beat < keepFrom)
        captured.pop_front();
}

juce::String DedalProcessor::describeClipSource (int bars) const
{
    const auto bar = barBeats.load();
    const auto end = std::floor (currentBeat.load() / bar + 1.0e-6) * bar;
    const auto heard = capturedSession == session.load() && ! captured.empty() && end - bars * bar >= sessionStart.load() - 1.0e-6;
    return heard ? pl ("ostatnie zagrane takty") : pl ("nowe takty z sekwencerów");
}

juce::MidiFile DedalProcessor::makeClip (int bars, bool& fromHistory)
{
    collectCapture();

    const auto bar = barBeats.load();
    const auto end = std::floor (currentBeat.load() / bar + 1.0e-6) * bar;
    const auto start = end - bars * bar;
    const auto midiSeq = index (ID::midiSeq);
    std::vector<CapturedNote> notes;

    fromHistory = capturedSession == session.load() && ! captured.empty() && start >= sessionStart.load() - 1.0e-6;

    const auto cMidi = value (ID::cMidi) > 0.5f;
    auto wanted = [midiSeq, cMidi] (int seq) { return seq == 2 ? cMidi : (midiSeq == 2 || seq == midiSeq); };

    if (fromHistory)
    {
        for (const auto& n : captured)
            if (n.beat >= start - 1.0e-6 && n.beat < end - 1.0e-6 && wanted (n.seq))
                notes.push_back ({ n.beat - start, std::min (n.length, end - n.beat), n.note, n.velocity, n.seq, 0 });
    }
    else
    {
        // Nothing heard yet: run a copy of the sequencers forward, CORRUPT, SEQ C and all.
        Sequencers copy;
        {
            const juce::SpinLock::ScopedLockType lock (seqLock);
            copy.state = sequencers.state;
        }

        copy.random.setSeed (juce::Random::getSystemRandom().nextInt64());
        copy.restart();
        const auto params = readStepParams();
        const auto trigMix = value (ID::trigMix);
        const float weight[2] = { trigMix <= 0.5f ? 1.0f : 2.0f * (1.0f - trigMix), trigMix >= 0.5f ? 1.0f : 2.0f * trigMix };
        const auto root = rootNote();
        const auto gate = (double) value (ID::gate);
        const auto cNote = juce::jlimit (0, 127, index (ID::cNote));
        std::array<juce::int64, 3> next {};
        float shift[2] = { 0.0f, 0.0f };

        for (int k = 0; k < 3; ++k)
            next[(size_t) k] = params.timing[k].firstAtOrAfter (0.0);

        runSteps (copy, params, next, bars * bar, [&] (const StepEvent& e)
        {
            if (e.k == 2)
            {
                for (int k = 0; k < 2; ++k)
                    if (e.setsTranspose[k])
                        shift[k] = e.transpose[k];

                if (e.r.on && cMidi)
                    notes.push_back ({ e.beat, gate * e.stepBeats, cNote, e.r.accent ? 127 : 88, 2, 0 });

                return;
            }

            if (e.r.on && weight[e.k] >= 0.02f && wanted (e.k))
                notes.push_back ({ e.beat, gate * e.stepBeats, midiNoteOf (e.k, e.r.value, root, shift[e.k]),
                                   juce::jlimit (1, 127, juce::roundToInt (30.0f + 97.0f * weight[e.k])), e.k, 0 });
        });
    }

    constexpr int ppq = 960;
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / std::max (20.0, hostBpm.load()))), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (juce::roundToInt (bar), 4), 0.0);
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "Dedal"), 0.0);

    for (const auto& n : notes)
    {
        const auto on = std::round (n.beat * ppq);
        const auto off = std::max (on + 1.0, std::round ((n.beat + n.length) * ppq));
        track.addEvent (juce::MidiMessage::noteOn (1, n.note, (juce::uint8) n.velocity), on);
        track.addEvent (juce::MidiMessage::noteOff (1, n.note), off - 1.0);
    }

    track.addEvent (juce::MidiMessage::endOfTrack(), bars * bar * ppq);
    track.updateMatchedPairs();
    track.sort();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    file.addTrack (track);
    return file;
}

juce::File DedalProcessor::writeMidiFile (int bars, bool& fromHistory)
{
    const auto clip = makeClip (bars, fromHistory);
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Dedal");
    folder.createDirectory();
    const auto file = folder.getChildFile ("Dedal - " + juce::File::createLegalFileName (presetName) + " - " + juce::String (bars)
                                           + (bars == 1 ? " takt" : (bars < 5 ? " takty" : pl (" taktów"))) + ".mid");
    file.deleteFile();
    juce::FileOutputStream out (file);

    if (! out.openedOk())
        return {};

    clip.writeTo (out);
    out.flush();
    return file;
}

//==============================================================================
juce::ValueTree DedalProcessor::createState()
{
    auto state = apvts.copyState();
    state.removeAllChildren (nullptr);

    for (int i = 0; i < apvts.state.getNumChildren(); ++i)
        if (apvts.state.getChild (i).hasType ("PARAM"))
            state.appendChild (apvts.state.getChild (i).createCopy(), nullptr);

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        state.appendChild (toValueTree (sequencers.state, seqType), nullptr);
    }

    if (buffer.isValid())
        state.appendChild (buffer.createCopy(), nullptr);

    state.setProperty ("preset", presetName, nullptr);
    state.setProperty ("dragBars", dragBars, nullptr);
    return state;
}

void DedalProcessor::applyState (const juce::ValueTree& tree)
{
    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return;

    auto params = tree.createCopy();
    params.removeChild (params.getChildWithName (seqType), nullptr);
    params.removeChild (params.getChildWithName (bufferType), nullptr);
    apvts.replaceState (params);

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        auto fresh = sequencers.state; // keeps where the heads are
        fromValueTree (tree.getChildWithName (seqType), fresh);
        sequencers.state = fresh;
    }

    const auto savedBuffer = tree.getChildWithName (bufferType);
    buffer = savedBuffer.isValid() ? savedBuffer.createCopy() : juce::ValueTree();
    presetName = tree.getProperty ("preset", presetName).toString();
    dragBars = tree.getProperty ("dragBars", dragBars);
    transposeNote = -1;
    sendChangeMessage();
}

void DedalProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = createState().createXml())
        copyXmlToBinary (*xml, destination);
}

void DedalProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        applyState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::File DedalProcessor::getPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Dedal").getChildFile ("Presety");
}

juce::Array<juce::File> DedalProcessor::getUserPresets() const
{
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, false, "*.dedal");
    files.sort();
    return files;
}

bool DedalProcessor::savePreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());

    if (clean.isEmpty())
        return false;

    const auto folder = getPresetFolder();
    folder.createDirectory();
    presetName = clean;
    const auto xml = createState().createXml();
    const auto ok = xml != nullptr && xml->writeTo (folder.getChildFile (clean + ".dedal"));
    sendChangeMessage();
    return ok;
}

bool DedalProcessor::loadPresetFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        tree.setProperty ("preset", file.getFileNameWithoutExtension(), nullptr);
        tree.setProperty ("dragBars", dragBars, nullptr);
        applyState (tree);
        return true;
    }

    return false;
}

juce::StringArray DedalProcessor::getFactoryPresetNames() const
{
    return { "Start", pl ("Akordy z przesunięcia"), pl ("Zachodnie wybrzeże"), "Polirytm 5 : 7", "Dzwony (Beads)", "Dryf",
             pl ("Bas z kroków"), "Formanty (Beads)", pl ("Polirytm 7 · 5 · 11"), pl ("C prowadzi") };
}

void DedalProcessor::loadFactoryPreset (int presetIndex)
{
    struct Setting
    {
        const char* id;
        float value;
    };

    std::vector<Setting> settings;
    auto seed = 1;
    auto density = 0.7f;
    auto chainOffset = 0;

    switch (presetIndex)
    {
        case 1: // two heads over one chained sequence: intervals and chords
            settings = { { ID::chain, 1 }, { ID::length[0], 4 }, { ID::length[1], 4 }, { ID::scale, 12 }, { ID::range[0], 14 },
                         { ID::range[1], 14 }, { ID::lvlMod, 0.42f }, { ID::modWave, 0.67f }, { ID::lvlVco, 0.45f },
                         { ID::cutoff, 0.62f }, { ID::eg2Decay, 0.9f }, { ID::eg1Decay, 0.5f }, { ID::trigMix, 0.5f } };
            seed = 7;
            density = 0.85f;
            chainOffset = 2;
            break;

        case 2: // folds everywhere
            settings = { { ID::fold, 0.35f }, { ID::seq1Fold, 0.7f }, { ID::eg1Fold, 0.55f }, { ID::routing, 1 }, { ID::bias, 0.2f },
                         { ID::cutoff, 0.78f }, { ID::scale, 3 }, { ID::rate[0], 7 }, { ID::corrupt[0], 0.22f },
                         { ID::eg2Decay, 0.28f }, { ID::eg1Decay, 0.35f }, { ID::range[0], 19 } };
            seed = 3;
            break;

        case 3: // five against seven
            settings = { { ID::length[0], 5 }, { ID::length[1], 7 }, { ID::rate[0], 7 }, { ID::rate[1], 7 }, { ID::seq2Cutoff, 0.7f },
                         { ID::lvlMod, 0.3f }, { ID::modTune, 12 }, { ID::scale, 8 }, { ID::eg2Decay, 0.22f }, { ID::eg1Decay, 0.2f },
                         { ID::resonance, 0.5f }, { ID::range[1], 18 } };
            seed = 11;
            break;

        case 4: // bells from the Beads banks
            settings = { { ID::vcoType, 1 }, { ID::vcoBank, 8 }, { ID::vcoWave, 0.45f }, { ID::modType, 1 }, { ID::modBank, 6 },
                         { ID::modWave, 0.3f }, { ID::lvlRing, 0.28f }, { ID::lvlMod, 0.15f }, { ID::eg1Decay, 1.2f },
                         { ID::eg2Decay, 1.8f }, { ID::scale, 14 }, { ID::swing[0], 0.45f }, { ID::fold, 0.05f },
                         { ID::eg1Fold, 0.15f }, { ID::cutoff, 0.82f }, { ID::eg1Cutoff, 0.2f }, { ID::range[0], 18 }, { ID::octave, 3 } };
            seed = 5;
            density = 0.55f;
            break;

        case 5: // slowly changing
            settings = { { ID::corrupt[0], 0.42f }, { ID::corrupt[1], 0.62f }, { ID::rate[0], 3 }, { ID::rate[1], 2 },
                         { ID::glide, 0.09f }, { ID::vcaF, 2 }, { ID::cutoff, 0.38f }, { ID::seq2Cutoff, 0.85f },
                         { ID::resonance, 0.62f }, { ID::blend, 0.7f }, { ID::lvlMod, 0.25f }, { ID::modTune, -12 },
                         { ID::eg2Decay, 2.4f }, { ID::range[1], 24 } };
            seed = 9;
            break;

        case 6: // bass line
            settings = { { ID::octave, 1 }, { ID::rate[0], 7 }, { ID::swing[0], 0.35f }, { ID::cutoff, 0.36f }, { ID::eg1Cutoff, 0.6f },
                         { ID::resonance, 0.5f }, { ID::vcoWave, 0.67f }, { ID::eg1Decay, 0.16f }, { ID::eg2Decay, 0.24f },
                         { ID::scale, 5 }, { ID::blend, 0.85f }, { ID::trigMix, 0.0f }, { ID::range[0], 12 } };
            seed = 13;
            density = 0.75f;
            break;

        case 7: // talking waves
            settings = { { ID::vcoType, 1 }, { ID::vcoBank, 5 }, { ID::vcoWave, 0.3f }, { ID::seq1Wave, 0.8f }, { ID::filterType, 0.5f },
                         { ID::scale, 3 }, { ID::swing[0], 0.25f }, { ID::swing[1], 0.25f }, { ID::eg2Decay, 0.6f },
                         { ID::fold, 0.0f }, { ID::eg1Fold, 0.1f }, { ID::range[0], 16 }, { ID::volume, 2.0f } };
            seed = 17;
            break;

        case 8: // three loops of 7, 5 and 11 steps that meet in a different place every time
            settings = { { ID::length[0], 7 }, { ID::length[1], 5 }, { ID::lengthC, 11 }, { ID::rate[0], 7 }, { ID::rate[1], 7 },
                         { ID::rateC, 7 }, { ID::cEg, 0.8f }, { ID::trigMix, 0.0f }, { ID::lvlMod, 0.32f }, { ID::modTune, 7 },
                         { ID::seq2Cutoff, 0.5f }, { ID::scale, 3 }, { ID::eg1Decay, 0.18f }, { ID::eg2Decay, 0.3f },
                         { ID::cTrans, 3 }, { ID::cTransAmt, 5 }, { ID::range[0], 14 }, { ID::range[1], 10 } };
            seed = 21;
            break;

        case 9: // SEQ C steers the other two: resets, turns them round, changes their length and chance
            settings = { { ID::rateC, 4 }, { ID::lengthC, 13 }, { ID::cReset, 1 }, { ID::cResetOn, 0 }, { ID::cDir, 2 },
                         { ID::cDirOn, 1 }, { ID::cLength, 3 }, { ID::cLengthAmt, 0.7f }, { ID::cChance, 3 },
                         { ID::cChanceAmt, 0.6f }, { ID::chance[0], 0.7f }, { ID::chance[1], 0.7f }, { ID::direction[1], 2 },
                         { ID::rate[0], 7 }, { ID::rate[1], 4 }, { ID::lvlMod, 0.3f }, { ID::scale, 13 }, { ID::corruptC, 0.2f },
                         { ID::eg2Decay, 0.5f }, { ID::fold, 0.25f }, { ID::seq1Fold, 0.4f } };
            seed = 33;
            break;

        default:
            break;
    }

    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& s : settings)
        setParameter (s.id, s.value);

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        auto& seq = sequencers;
        seq.random.setSeed (seed);
        seq.state = {};
        seq.randomise (0, density);
        seq.randomise (1, density);
        seq.randomiseC (0.5f);
        seq.state.bits[0] = true;
        seq.state.bits[kSteps] = true;
        seq.state.chainOffset = chainOffset;
        seq.random.setSeed (juce::Random::getSystemRandom().nextInt64());
    }

    presetName = getFactoryPresetNames()[presetIndex];
    buffer = {};
    transposeNote = -1;
    sendChangeMessage();
}

juce::AudioProcessorEditor* DedalProcessor::createEditor()
{
    return new DedalEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DedalProcessor();
}
