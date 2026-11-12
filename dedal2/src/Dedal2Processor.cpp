#include "Dedal2Processor.h"
#include "Dedal2Editor.h"
#include "Dedal2Params.h"

using namespace dedal2;

namespace
{
const juce::Identifier seqType ("SEQ"), bufferType ("BUFFER");
juce::String percent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String bipolar (float v, int) { return (v > 0.004f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

juce::String hertz (float note, int)
{
    const auto f = 440.0f * std::exp2 ((note - 69.0f) / 12.0f);
    const auto name = juce::MidiMessage::getMidiNoteName (juce::roundToInt (note), true, true, 3);
    return (f < 100.0f ? juce::String (f, 1) : juce::String (juce::roundToInt (f))) + " Hz  " + (note >= 0.0f ? name : juce::String());
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout Dedal2Processor::createLayout()
{
    using Float = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&layout] (const char* id, const juce::String& name, juce::NormalisableRange<float> range, float initial,
                          std::function<juce::String (float, int)> text)
    {
        layout.add (std::make_unique<Float> (juce::ParameterID { id, 1 }, name, range, initial,
                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
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
    auto amount = [] (float v, int) { return v >= 0.995f ? juce::String ("QTZ") : juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    add (ID::vcoFreq, "VCO FREQUENCY", { 16.0f, 111.0f, 0.01f }, 48.0f, hertz);   // about 20 Hz to 5 kHz
    signedUnit (ID::vcoEg1, "VCO EG1 AMT", 0.0f);
    add (ID::vcoSeq1, "VCO SEQ1 AMT", { 0.0f, 1.0f, 0.001f }, 1.0f, amount);
    add (ID::modFreq, "MOD VCO FREQUENCY", { -60.0f, 88.0f, 0.01f }, 48.0f, hertz); // sub-audio to about 1.3 kHz
    signedUnit (ID::modEg1, "MOD VCO EG1 AMT", 0.0f);
    add (ID::modSeq2, "MOD VCO SEQ2 AMT", { 0.0f, 1.0f, 0.001f }, 1.0f, amount);
    unit (ID::fm, "FM AMT", 0.0f);
    unit (ID::lvlVco, "VCO LVL", 0.5f);
    unit (ID::lvlRing, "U MIX 1 LVL (RING)", 0.0f);
    unit (ID::lvlMod, "MOD VCO LVL", 0.0f);
    unit (ID::lvlNoise, "NOISE LVL", 0.0f);
    unit (ID::noiseTone, "NOISE TONE", 0.5f);
    unit (ID::fold, "VCW FOLD", 0.0f);
    signedUnit (ID::bias, "VCW BIAS", 0.0f);
    signedUnit (ID::foldEg, "VCW EG1/CV AMT", 0.0f);
    unit (ID::foldSeq, "VCW SEQ1 AMT", 0.0f);
    add (ID::cutoff, "VCF CUTOFF", { 0.0f, 1.0f, 0.001f }, 0.6f, [] (float v, int)
    {
        const auto f = 20.0f * std::exp2 (v * 10.0f);
        return f < 1000.0f ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f / 1000.0f, 1) + " kHz";
    });
    signedUnit (ID::cutEg, "VCF EG1/CV AMT", 0.3f);
    unit (ID::cutSeq, "VCF SEQ2 AMT", 0.0f);
    unit (ID::resonance, "RESONANCE", 0.2f);
    add (ID::mode, "FILTER MODE", { 0.0f, 1.0f, 0.001f }, 0.0f, [] (float v, int)
    {
        return v < 0.02f ? juce::String ("LP") : (v > 0.98f ? juce::String ("BP") : juce::String (juce::roundToInt (v * 100.0f)) + " % BP");
    });
    add (ID::blend, "BLEND", { 0.0f, 1.0f, 0.001f }, 0.5f, [] (float v, int)
    {
        return v < 0.02f ? juce::String ("VCW") : (v > 0.98f ? juce::String ("VCF") : juce::String (juce::roundToInt (v * 100.0f)) + " % VCF");
    });
    choice (ID::order, "ORDER", { "PARALLEL", "VCW > VCF", "VCF > VCW" }, 0);
    add (ID::eg1Decay, "EG1 DECAY", { 0.01f, 5.0f, 0.001f, 0.3f }, 0.3f, seconds);
    add (ID::eg2Decay, "EG2 (VCA) DECAY", { 0.01f, 5.0f, 0.001f, 0.3f }, 0.45f, seconds);
    add (ID::trigMix, "EG TRIG MIX", { 0.0f, 1.0f, 0.001f }, 0.0f, [] (float v, int)
    {
        return v < 0.02f ? juce::String ("SEQ1") : (v > 0.98f ? juce::String ("SEQ2") : juce::String (juce::roundToInt ((1.0f - v) * 100.0f)) + " / " + juce::String (juce::roundToInt (v * 100.0f)));
    });
    add (ID::volume, "VOLUME", { -40.0f, 6.0f, 0.1f }, -6.0f, [] (float v, int) { return juce::String (v, 1) + " dB"; });

    for (int k = 0; k < 2; ++k)
    {
        const auto n = juce::String (k + 1);
        unit (ID::corrupt[k], "CORRUPT " + n, 0.0f);
        add (ID::range[k], "SEQ" + n + " CV RANGE", { 0.0f, 1.0f, 0.001f }, 0.2f,
             [] (float v, int) { return pl ("±") + juce::String (v * 5.0f, 2) + " V"; });
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ID::length[k], 1 }, "LENGTH " + n, 1, 8, 8));
        toggle (ID::flip[k], "BIT FLIP " + n, false);
    }

    toggle (ID::chain, "CHAIN SEQ", false);
    choice (ID::scale, "Quantize", getScaleNames(), 2);
    choice (ID::tempo, "TEMPO", getRateNames(), 4);
    toggle (ID::trigger, "TRIGGER", false);
    toggle (ID::midiSend, "MIDI out", true);
    choice (ID::midiSeq, "MIDI notes", { "SEQ1", "SEQ2", "SEQ1+2" }, 0);
    unit (ID::gate, "MIDI gate", 0.5f);
    toggle (ID::sound, "Sound", true);
    return layout;
}

Dedal2Processor::Dedal2Processor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DEDAL2", createLayout())
{
    for (auto& m : jackMeters)
        m = 0.0f;

    offAt.fill (-1);
    loadFactoryPreset (0);
    startTimerHz (10);
}

void Dedal2Processor::setParameter (const char* id, float plain)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

Layout Dedal2Processor::readLayout() const
{
    Layout l;
    l.length[0] = juce::jlimit (1, 8, index (ID::length[0]));
    l.length[1] = juce::jlimit (1, 8, index (ID::length[1]));
    l.chain = value (ID::chain) > 0.5f;
    return l;
}

Params Dedal2Processor::readParams() const
{
    Params p;
    const auto t = transposeNote.load();
    p.vcoNote = t >= 0 ? (float) t : value (ID::vcoFreq);
    p.vcoEg1 = value (ID::vcoEg1);
    p.vcoSeq1 = value (ID::vcoSeq1);
    p.modNote = value (ID::modFreq);
    p.modEg1 = value (ID::modEg1);
    p.modSeq2 = value (ID::modSeq2);
    p.fm = value (ID::fm);
    p.lvlVco = value (ID::lvlVco);
    p.lvlUmix1 = value (ID::lvlRing);
    p.lvlMod = value (ID::lvlMod);
    p.lvlNoise = value (ID::lvlNoise);
    p.noiseTone = value (ID::noiseTone);
    p.fold = value (ID::fold);
    p.bias = value (ID::bias);
    p.foldEg = value (ID::foldEg);
    p.foldSeq = value (ID::foldSeq);
    p.cutoff = value (ID::cutoff);
    p.cutEg = value (ID::cutEg);
    p.cutSeq = value (ID::cutSeq);
    p.resonance = value (ID::resonance);
    p.mode = value (ID::mode);
    p.blend = value (ID::blend);
    p.order = index (ID::order);
    p.eg1Decay = value (ID::eg1Decay);
    p.eg2Decay = value (ID::eg2Decay);
    p.trigMix = value (ID::trigMix);
    p.gain = juce::Decibels::decibelsToGain (value (ID::volume), -40.0f);
    p.scale = index (ID::scale);

    for (int k = 0; k < 2; ++k)
    {
        p.range[k] = value (ID::range[k]);
        p.corrupt[k] = value (ID::corrupt[k]);
    }

    p.layout = readLayout();
    return p;
}

//==============================================================================
Snapshot Dedal2Processor::getSnapshot() const
{
    Snapshot s;
    s.layout = readLayout();
    const juce::SpinLock::ScopedLockType lock (seqLock);
    s.state = sequencers.state;

    for (int k = 0; k < 2; ++k)
    {
        s.head[(size_t) k] = sequencers.currentHead (s.layout, k);
        s.writeHead[(size_t) k] = sequencers.writeHead (s.layout, k);
    }

    return s;
}

void Dedal2Processor::command (Command c, int k)
{
    const auto layout = readLayout();

    if (c == Command::length)
    {
        const auto length = layout.length[k];
        setParameter (ID::length[k], (float) (length <= 1 ? 8 : length - 1));
        return;
    }

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        auto& s = sequencers;

        switch (c)
        {
            case Command::bitFlip:      s.bitFlip (layout, k); break;
            case Command::bitShift:     s.bitShift (layout, k); break;
            case Command::unshift:      s.unshift (layout, k); break;
            case Command::advance:      s.advance (layout, 0); s.advance (layout, 1); break;
            case Command::reset:        s.reset (layout, 0); s.reset (layout, 1); break;
            case Command::writeAdvance: s.moveWriteHead (layout, k, false); break;
            case Command::writeReset:   s.moveWriteHead (layout, k, true); break;
            case Command::randomise:    s.randomise (k); break;
            case Command::clear:        s.clear (k); break;
            case Command::length:       break;
        }
    }

    sendChangeMessage();
}

void Dedal2Processor::toggleSlot (int slot)
{
    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        sequencers.flipSlot ((size_t) juce::jlimit (0, 15, slot));
    }
    sendChangeMessage();
}

void Dedal2Processor::connect (int output, int input)
{
    if (output >= 0 && output < numOuts && input >= 0 && input < numIns)
        engine.setCable (input, output);

    sendChangeMessage();
}

void Dedal2Processor::disconnect (int input)
{
    if (input >= 0 && input < numIns)
        engine.setCable (input, -1);

    sendChangeMessage();
}

void Dedal2Processor::clearCables()
{
    for (int i = 0; i < numIns; ++i)
        engine.setCable (i, -1);

    sendChangeMessage();
}

void Dedal2Processor::saveBuffer()
{
    juce::ValueTree tree;
    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        tree = toValueTree (sequencers.state, bufferType);
    }

    for (const auto* id : { ID::length[0], ID::length[1], ID::chain, ID::scale })
        tree.setProperty (id, value (id), nullptr);

    buffer = tree;
    sendChangeMessage();
}

void Dedal2Processor::recallBuffer()
{
    if (! buffer.isValid())
        return;

    for (const auto* id : { ID::length[0], ID::length[1], ID::chain, ID::scale })
        if (buffer.hasProperty (id))
            setParameter (id, (float) buffer.getProperty (id));

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        fromValueTree (buffer, sequencers.state);
    }
    sendChangeMessage();
}

//==============================================================================
void Dedal2Processor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    mono.assign ((size_t) std::max (32, samplesPerBlock), 0.0f);
    clocks.assign (4096, 0);
    engine.prepare (sampleRate, samplesPerBlock);
    wasRunning = wasHostPlaying = false;
    offAt.fill (-1);
}

void Dedal2Processor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = audio.getNumSamples();
    audio.clear();

    if (numSamples == 0)
        return;

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

    // A note from the keyboard sets the root; moving VCO FREQUENCY takes it back.
    if (const auto knob = value (ID::vcoFreq); std::abs (knob - lastVcoParam) > 1.0e-4f)
    {
        transposeNote = -1;
        lastVcoParam = knob;
    }

    for (const auto meta : midi)
        if (meta.getMessage().isNoteOn())
            transposeNote = meta.getMessage().getNoteNumber();

    midi.clear();

    const auto hostRun = playing && hasPosition;
    const auto running = hostRun || run.load();
    const auto beat0 = hostRun ? ppq : internalBeat;
    const auto beatsPerSample = bpm / (60.0 * sampleRate);
    const auto beat1 = beat0 + numSamples * beatsPerSample;
    const auto jumped = running && (! wasRunning || hostRun != wasHostPlaying
                                    || std::abs (beat0 - expectedBeat) > std::max (0.02, 4.0 * numSamples * beatsPerSample));
    const auto params = readParams();
    const Timing timing { rateToBeats (index (ID::tempo)), 0.0f };
    int numClocks = 0, numPlayed = 0;
    const auto midiSeq = index (ID::midiSeq);
    const auto sendMidi = value (ID::midiSend) > 0.5f;
    const auto sound = value (ID::sound) > 0.5f;
    const auto gateBeats = (double) value (ID::gate) * timing.stepBeats;

    auto flushNoteOffs = [&] (juce::int64 until)
    {
        for (int note = 0; note < 128; ++note)
            if (offAt[(size_t) note] >= 0 && offAt[(size_t) note] <= until)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, note), (int) juce::jlimit<juce::int64> (0, numSamples - 1, offAt[(size_t) note] - sampleClock));
                offAt[(size_t) note] = -1;
            }
    };

    if (! running && wasRunning)
        flushNoteOffs (std::numeric_limits<juce::int64>::max());

    for (int start = 0; start < numSamples; start += (int) mono.size())
    {
        const auto n = std::min ((int) mono.size(), numSamples - start);
        const juce::SpinLock::ScopedLockType lock (seqLock);

        if (start == 0)
        {
            for (int k = 0; k < 2; ++k)
            {
                const auto down = value (ID::flip[k]) > 0.5f;

                if (down && ! lastFlip[(size_t) k])
                    sequencers.bitFlip (params.layout, k);

                lastFlip[(size_t) k] = down;
            }

            if (jumped)
            {
                if (! wasRunning || hostRun != wasHostPlaying)
                    sequencers.restart();

                session = session.load() + 1;
                sessionStart = beat0;
            }

            if (running)
            {
                if (jumped || std::abs (timing.stepBeats - lastStepBeats) > 1.0e-12)
                {
                    nextStep = timing.firstAtOrAfter (beat0);
                    lastStepBeats = timing.stepBeats;
                }

                while (timing.startOf (nextStep) < beat1 && numClocks < (int) clocks.size())
                {
                    clocks[(size_t) numClocks++] = juce::jlimit (0, numSamples - 1, (int) std::floor ((timing.startOf (nextStep) - beat0) / beatsPerSample));
                    ++nextStep;
                }
            }
        }

        // The clock ticks of this chunk, relative to it
        int first = 0, count = 0;

        while (first < numClocks && clocks[(size_t) first] < start)
            ++first;

        while (first + count < numClocks && clocks[(size_t) (first + count)] < start + n)
        {
            clocks[(size_t) (first + count)] -= start;
            ++count;
        }

        const auto triggerDown = value (ID::trigger) > 0.5f;
        const auto manual = start == 0 && triggerDown && ! lastTrigger;

        if (start == 0)
            lastTrigger = triggerDown;

        engine.render (mono.data(), n, params, sequencers, clocks.data() + first, count, manual, played.data(), numPlayed, (int) played.size());

        for (int i = first; i < first + count; ++i)
            clocks[(size_t) i] += start;

        if (sound)
            for (int ch = 0; ch < std::min (2, audio.getNumChannels()); ++ch)
                std::copy (mono.begin(), mono.begin() + n, audio.getWritePointer (ch) + start);

        for (int i = 0; i < numPlayed; ++i)
        {
            const auto& e = played[(size_t) i];
            const auto offset = start + e.sample;
            flushNoteOffs (sampleClock + offset);

            if (! (midiSeq == 2 || midiSeq == e.k))
                continue;

            if (sendMidi)
            {
                if (offAt[(size_t) e.note] >= 0)
                    midi.addEvent (juce::MidiMessage::noteOff (1, e.note), offset);

                midi.addEvent (juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.velocity), offset);
                offAt[(size_t) e.note] = sampleClock + offset + std::max ((juce::int64) (0.005 * sampleRate), (juce::int64) (gateBeats / beatsPerSample));
            }

            const auto scope = fifo.write (1);

            if (scope.blockSize1 > 0)
                fifoData[(size_t) scope.startIndex1] = { beat0 + offset * beatsPerSample, gateBeats, e.note, e.velocity, e.k, session.load() };
        }
    }

    flushNoteOffs (sampleClock + numSamples - 1);

    for (int i = 0; i < numOuts; ++i)
        jackMeters[(size_t) i] = engine.jack (i);

    if (running)
        currentBeat = beat1;

    internalBeat = (! hostRun && run.load()) ? beat1 : (running ? internalBeat : 0.0);
    expectedBeat = beat1;
    wasRunning = running;
    wasHostPlaying = hostRun;
    sampleClock += numSamples;
}

//==============================================================================
void Dedal2Processor::collectCapture()
{
    if (const auto ready = fifo.getNumReady(); ready > 0)
    {
        const auto scope = fifo.read (ready);

        auto take = [this] (int start, int count)
        {
            for (int i = start; i < start + count; ++i)
            {
                if (fifoData[(size_t) i].session != capturedSession)
                {
                    captured.clear();
                    capturedSession = fifoData[(size_t) i].session;
                }

                captured.push_back (fifoData[(size_t) i]);
            }
        };

        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    while (! captured.empty() && captured.front().beat < currentBeat.load() - 64.0 * barBeats.load())
        captured.pop_front();
}

juce::MidiFile Dedal2Processor::makeClip (int bars, bool& fromHistory)
{
    collectCapture();
    const auto bar = barBeats.load();
    const auto end = std::floor (currentBeat.load() / bar + 1.0e-6) * bar, start = end - bars * bar;
    fromHistory = capturedSession == session.load() && ! captured.empty() && start >= sessionStart.load() - 1.0e-6;
    std::vector<Captured> notes;

    if (fromHistory)
    {
        for (const auto& n : captured)
            if (n.beat >= start - 1.0e-6 && n.beat < end - 1.0e-6)
                notes.push_back ({ n.beat - start, std::min (n.length, end - n.beat), n.note, n.velocity, n.seq, 0 });
    }
    else
    {
        // Nothing heard yet: run a copy of the two sequencers forward on the internal clock.
        Sequencers copy;
        {
            const juce::SpinLock::ScopedLockType lock (seqLock);
            copy.state = sequencers.state;
        }

        copy.restart();
        const auto p = readParams();
        const auto step = rateToBeats (index (ID::tempo));
        const auto midiSeq = index (ID::midiSeq);
        const auto gate = (double) value (ID::gate) * step;

        for (double beat = 0.0; beat < bars * bar - 1.0e-9; beat += step)
            for (int k = 0; k < 2; ++k)
            {
                const auto r = copy.step (p.layout, k, p.corrupt[k], forward, 1.0f);

                if (r.on && (midiSeq == 2 || midiSeq == k))
                    notes.push_back ({ beat, gate, juce::jlimit (0, 127, juce::roundToInt ((k == 0 ? p.vcoNote : p.modNote)
                                           + quantize (p.scale, r.value * p.range[k] * 60.0f))), 100, k, 0 });
            }
    }

    constexpr int ppq = 960;
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / std::max (20.0, hostBpm.load()))), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (juce::roundToInt (bar), 4), 0.0);

    for (const auto& n : notes)
    {
        const auto on = std::round (n.beat * ppq);
        track.addEvent (juce::MidiMessage::noteOn (1, n.note, (juce::uint8) n.velocity), on);
        track.addEvent (juce::MidiMessage::noteOff (1, n.note), std::max (on + 1.0, std::round ((n.beat + n.length) * ppq)) - 1.0);
    }

    track.addEvent (juce::MidiMessage::endOfTrack(), bars * bar * ppq);
    track.updateMatchedPairs();
    track.sort();
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    file.addTrack (track);
    return file;
}

juce::File Dedal2Processor::writeMidiFile (int bars, bool& fromHistory)
{
    const auto clip = makeClip (bars, fromHistory);
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Dedal2");
    folder.createDirectory();
    const auto file = folder.getChildFile ("Dedal 2 - " + juce::File::createLegalFileName (presetName) + " - " + juce::String (bars) + ".mid");
    file.deleteFile();
    juce::FileOutputStream out (file);

    if (! out.openedOk())
        return {};

    clip.writeTo (out);
    out.flush();
    return file;
}

//==============================================================================
juce::ValueTree Dedal2Processor::createState()
{
    auto state = apvts.copyState();
    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        state.appendChild (toValueTree (sequencers.state, seqType), nullptr);
    }

    if (buffer.isValid())
        state.appendChild (buffer.createCopy(), nullptr);

    juce::StringArray cables;

    for (int i = 0; i < numIns; ++i)
        if (engine.getCable (i) >= 0)
            cables.add (juce::String (engine.getCable (i)) + ">" + juce::String (i));

    state.setProperty ("cables", cables.joinIntoString (","), nullptr);
    state.setProperty ("preset", presetName, nullptr);
    return state;
}

void Dedal2Processor::applyState (const juce::ValueTree& tree)
{
    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return;

    auto params = tree.createCopy();
    params.removeChild (params.getChildWithName (seqType), nullptr);
    params.removeChild (params.getChildWithName (bufferType), nullptr);
    apvts.replaceState (params);

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        auto fresh = sequencers.state;
        fromValueTree (tree.getChildWithName (seqType), fresh);
        sequencers.state = fresh;
    }

    for (int i = 0; i < numIns; ++i)
        engine.setCable (i, -1);

    for (const auto& cable : juce::StringArray::fromTokens (tree.getProperty ("cables").toString(), ",", {}))
    {
        const auto out = cable.upToFirstOccurrenceOf (">", false, false).getIntValue();
        const auto in = cable.fromFirstOccurrenceOf (">", false, false).getIntValue();

        if (cable.contains (">") && out >= 0 && out < numOuts && in >= 0 && in < numIns)
            engine.setCable (in, out);
    }

    const auto saved = tree.getChildWithName (bufferType);
    buffer = saved.isValid() ? saved.createCopy() : juce::ValueTree();
    presetName = tree.getProperty ("preset", presetName).toString();
    transposeNote = -1;
    sendChangeMessage();
}

void Dedal2Processor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = createState().createXml())
        copyXmlToBinary (*xml, destination);
}

void Dedal2Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        applyState (juce::ValueTree::fromXml (*xml));
}

juce::File Dedal2Processor::getPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Dedal 2").getChildFile ("Presety");
}

juce::Array<juce::File> Dedal2Processor::getUserPresets() const
{
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, false, "*.dedal2");
    files.sort();
    return files;
}

bool Dedal2Processor::savePreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());

    if (clean.isEmpty())
        return false;

    getPresetFolder().createDirectory();
    presetName = clean;
    const auto xml = createState().createXml();
    const auto ok = xml != nullptr && xml->writeTo (getPresetFolder().getChildFile (clean + ".dedal2"));
    sendChangeMessage();
    return ok;
}

bool Dedal2Processor::loadPresetFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        tree.setProperty ("preset", file.getFileNameWithoutExtension(), nullptr);
        applyState (tree);
        return true;
    }

    return false;
}

juce::StringArray Dedal2Processor::getFactoryPresetNames() const
{
    // The last twelve follow the notes of the example patches in the hardware manual (scale, cables, which
    // controls matter); the knob positions there are drawings only, so these are set by ear of the description.
    return { "Init", "Fold + filtr", "Polirytm 5 : 7", "Kable: SEQ2 > BLEND, EG2 > FOLD", "Kable: M VCO taktuje SEQ2",
             "A Simple Start", "Lost in the Labyrinth", "Dry Brushes", "Celestial Conversations", "Spiral Enigma",
             "Swirling Magenta", "Syndrums", "New Frontiers", "Telemechanical Birds", "Where You Lead Kicks Follow",
             "Twinkle Toes", "The Ooze" };
}

void Dedal2Processor::loadFactoryPreset (int presetIndex)
{
    struct Setting { const char* id; float value; };
    struct Cable { int out, in; };
    std::vector<Setting> settings;
    std::vector<Cable> cables;
    juce::String bits[2];

    switch (presetIndex)
    {
        case 1:
            settings = { { ID::fold, 0.3f }, { ID::foldEg, 0.5f }, { ID::foldSeq, 0.4f }, { ID::cutSeq, 0.5f }, { ID::lvlMod, 0.3f },
                         { ID::tempo, 7 }, { ID::scale, 3 }, { ID::trigMix, 0.5f }, { ID::corrupt[0], 0.2f } };
            break;
        case 2:
            settings = { { ID::length[0], 5 }, { ID::length[1], 7 }, { ID::tempo, 7 }, { ID::lvlMod, 0.35f }, { ID::trigMix, 0.5f },
                         { ID::cutSeq, 0.6f }, { ID::resonance, 0.5f }, { ID::scale, 8 }, { ID::eg2Decay, 0.25f } };
            break;
        case 3:
            settings = { { ID::tempo, 7 }, { ID::fold, 0.2f }, { ID::foldEg, 0.7f }, { ID::trigMix, 0.3f }, { ID::range[1], 0.6f },
                         { ID::scale, 3 }, { ID::lvlMod, 0.2f } };
            cables = { { outSeq2Cv, inBlend }, { outEg2, inFold } };
            break;
        case 4:
            settings = { { ID::tempo, 7 }, { ID::modFreq, -20.0f }, { ID::modSeq2, 0.0f }, { ID::trigMix, 0.5f }, { ID::scale, 13 },
                         { ID::cutSeq, 0.5f }, { ID::eg2Decay, 0.3f } };
            cables = { { outMvco, inClock2 } };
            break;
        case 5: // minor 7th, given bits, a touch of FM as vibrato
            settings = { { ID::scale, 12 }, { ID::fm, 0.08f }, { ID::modFreq, -5.0f }, { ID::modSeq2, 0.0f }, { ID::tempo, 4 },
                         { ID::cutEg, 0.35f }, { ID::range[0], 0.25f } };
            bits[0] = "00111010";
            bits[1] = "01111010";
            break;
        case 6: // major 13th, metallic from the MOD VCO
            settings = { { ID::scale, 11 }, { ID::lvlMod, 0.35f }, { ID::modFreq, 67.0f }, { ID::fm, 0.2f }, { ID::trigMix, 0.5f },
                         { ID::fold, 0.15f }, { ID::foldEg, 0.3f }, { ID::tempo, 7 } };
            break;
        case 7: // short and percussive, rows of different length, low MOD VCO
            settings = { { ID::scale, 12 }, { ID::modFreq, 30.0f }, { ID::lvlMod, 0.3f }, { ID::fm, 0.3f }, { ID::length[0], 5 },
                         { ID::length[1], 7 }, { ID::eg1Decay, 0.1f }, { ID::eg2Decay, 0.14f }, { ID::lvlNoise, 0.2f },
                         { ID::noiseTone, 0.8f }, { ID::tempo, 7 }, { ID::trigMix, 0.5f } };
            break;
        case 8: // hang drum tuning, a second timbre from U MIX 1 (ring)
            settings = { { ID::scale, 14 }, { ID::lvlRing, 0.3f }, { ID::modFreq, 55.0f }, { ID::eg1Decay, 1.2f }, { ID::eg2Decay, 1.5f },
                         { ID::foldEg, 0.3f }, { ID::tempo, 4 }, { ID::trigMix, 0.5f }, { ID::range[0], 0.35f } };
            break;
        case 9: // hirajoshi, both CORRUPTs full, short decays, CLOCK into VCO 1V/OCT
            settings = { { ID::scale, 8 }, { ID::corrupt[0], 1.0f }, { ID::corrupt[1], 1.0f }, { ID::eg1Decay, 0.06f },
                         { ID::eg2Decay, 0.09f }, { ID::lvlRing, 0.4f }, { ID::bias, 0.3f }, { ID::fold, 0.2f }, { ID::tempo, 7 },
                         { ID::trigMix, 0.5f }, { ID::modFreq, 20.0f } };
            cables = { { outClock, inVcoVoct } };
            break;
        case 10: // unquantized, slow, FM
            settings = { { ID::scale, 0 }, { ID::tempo, 1 }, { ID::length[0], 6 }, { ID::length[1], 5 }, { ID::fm, 0.4f },
                         { ID::modFreq, 40.0f }, { ID::modSeq2, 0.5f }, { ID::eg1Decay, 2.0f }, { ID::eg2Decay, 3.0f },
                         { ID::fold, 0.3f }, { ID::foldEg, 0.5f }, { ID::trigMix, 0.5f }, { ID::range[0], 0.4f } };
            break;
        case 11: // drums; EG2 into BIT FLIP 1 keeps rewriting the rhythm
            settings = { { ID::vcoFreq, 36.0f }, { ID::vcoEg1, 0.5f }, { ID::vcoSeq1, 0.3f }, { ID::corrupt[0], 0.4f },
                         { ID::eg1Decay, 0.12f }, { ID::eg2Decay, 0.2f }, { ID::lvlNoise, 0.35f }, { ID::noiseTone, 0.75f },
                         { ID::cutoff, 0.72f }, { ID::mode, 0.5f }, { ID::tempo, 7 }, { ID::trigMix, 0.5f } };
            cables = { { outEg2, inBitFlip1 } };
            break;
        case 12: // pentatonic; play a key to transpose
            settings = { { ID::scale, 3 }, { ID::fm, 0.25f }, { ID::modFreq, 60.0f }, { ID::lvlMod, 0.2f }, { ID::tempo, 7 },
                         { ID::trigMix, 0.5f }, { ID::cutSeq, 0.3f } };
            break;
        case 13: // chirps: EG1 sweeps the pitch, its decay sets how much bird
            settings = { { ID::scale, 0 }, { ID::vcoFreq, 84.0f }, { ID::vcoEg1, 0.6f }, { ID::eg1Decay, 0.4f }, { ID::fm, 0.5f },
                         { ID::modFreq, 10.0f }, { ID::modEg1, 0.5f }, { ID::corrupt[0], 0.3f }, { ID::corrupt[1], 0.3f },
                         { ID::range[0], 0.5f }, { ID::eg2Decay, 0.25f }, { ID::tempo, 5 }, { ID::trigMix, 0.5f }, { ID::cutoff, 0.85f } };
            break;
        case 14: // a kick from the VCO (EG1) under a lead from the MOD VCO (EG2)
            settings = { { ID::scale, 12 }, { ID::vcoFreq, 33.0f }, { ID::vcoEg1, 0.6f }, { ID::vcoSeq1, 0.0f }, { ID::lvlMod, 0.5f },
                         { ID::modFreq, 60.0f }, { ID::eg1Decay, 0.15f }, { ID::eg2Decay, 0.35f }, { ID::tempo, 7 },
                         { ID::trigMix, 0.5f }, { ID::range[1], 0.3f } };
            break;
        case 15: // two voices an octave apart, each with its own CV RANGE
            settings = { { ID::scale, 3 }, { ID::vcoFreq, 60.0f }, { ID::modFreq, 72.0f }, { ID::lvlMod, 0.45f }, { ID::range[0], 0.3f },
                         { ID::range[1], 0.5f }, { ID::length[1], 6 }, { ID::eg2Decay, 0.25f }, { ID::tempo, 7 }, { ID::trigMix, 0.5f } };
            break;
        case 16: // slow, resonant, unstable
            settings = { { ID::scale, 5 }, { ID::lvlRing, 0.5f }, { ID::lvlNoise, 0.15f }, { ID::eg1Decay, 1.5f }, { ID::eg2Decay, 2.0f },
                         { ID::cutoff, 0.4f }, { ID::resonance, 0.6f }, { ID::cutSeq, 0.6f }, { ID::fm, 0.3f }, { ID::modFreq, 36.0f },
                         { ID::tempo, 2 }, { ID::corrupt[0], 0.2f }, { ID::corrupt[1], 0.2f }, { ID::trigMix, 0.5f } };
            break;
        default:
            break;
    }

    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& s : settings)
        setParameter (s.id, s.value);

    for (int i = 0; i < numIns; ++i)
        engine.setCable (i, -1);

    for (const auto& c : cables)
        engine.setCable (c.in, c.out);

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        sequencers.random.setSeed (41 + presetIndex * 7);
        sequencers.state = {};
        sequencers.randomise (0, 0.7f);
        sequencers.randomise (1, 0.7f);
        sequencers.state.bits[0] = sequencers.state.bits[kSteps] = true;

        for (int k = 0; k < 2; ++k)
            for (int i = 0; i < bits[k].length(); ++i)
                sequencers.state.bits[(size_t) (k * kSteps + i)] = bits[k][i] == '1';
        sequencers.random.setSeed (juce::Random::getSystemRandom().nextInt64());
    }

    presetName = getFactoryPresetNames()[presetIndex];
    buffer = {};
    transposeNote = -1;
    sendChangeMessage();
}

juce::AudioProcessorEditor* Dedal2Processor::createEditor() { return new Dedal2Editor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Dedal2Processor(); }
