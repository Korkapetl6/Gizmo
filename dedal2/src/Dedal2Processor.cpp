#include "Dedal2Processor.h"
#include "Dedal2Editor.h"
#include "Dedal2Params.h"

#include <juce_audio_formats/juce_audio_formats.h>

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
    unit (ID::lvlRing, "RING MOD LVL", 0.0f);
    unit (ID::umixLvl, "U MIX 1 LVL", 0.0f);
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
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ID::length[k], 1 }, "LENGTH " + n, 1, 16, 8));
        toggle (ID::flip[k], "BIT FLIP " + n, false);
    }

    toggle (ID::chain, "CHAIN SEQ", false);
    toggle (ID::steps16, "16 STEPS", false);
    choice (ID::scale, "Quantize", getScaleNames(), 2);
    choice (ID::tempo, "TEMPO", getRateNames(), 4);
    toggle (ID::trigger, "TRIGGER", false);
    add (ID::smpTune, "SAMPLE TUNE", { -24.0f, 24.0f, 0.01f }, 0.0f,
         [] (float v, int) { return (v > 0.004f ? "+" : "") + juce::String (v, 2) + " st"; });
    unit (ID::smpStart, "SAMPLE START", 0.0f);
    toggle (ID::smpRetrig, "SAMPLE RETRIG", false);
    choice (ID::smpFollow, "SAMPLE PITCH", { "FIX", "SEQ1", "SEQ2" }, 0);
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
    // Every sequencer keeps 16 slots; the 16 STEPS switch decides how many of them can be used.
    const auto longest = value (ID::steps16) > 0.5f ? 16 : 8;
    Layout l;
    l.capacity = 16;
    l.length[0] = juce::jlimit (1, longest, index (ID::length[0]));
    l.length[1] = juce::jlimit (1, longest, index (ID::length[1]));
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
    p.umixLvl = value (ID::umixLvl);
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

    p.smpTune = value (ID::smpTune);
    p.smpStart = value (ID::smpStart);
    p.smpRetrig = value (ID::smpRetrig) > 0.5f;
    p.smpFollow = index (ID::smpFollow);
    p.layout = readLayout();
    return p;
}

//==============================================================================
Snapshot Dedal2Processor::getSnapshot() const
{
    Snapshot s;
    s.layout = readLayout();
    s.longSteps = value (ID::steps16) > 0.5f;
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
        setParameter (ID::length[k], (float) (length <= 1 ? (value (ID::steps16) > 0.5f ? 16 : 8) : length - 1));
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
            case Command::randomise:    s.randomise (k, 0.65f, layout.capacity); break;
            case Command::clear:        s.clear (k, layout.capacity); break;
            case Command::length:       break;
        }
    }

    sendChangeMessage();
}

void Dedal2Processor::toggleSlot (int slot)
{
    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        sequencers.flipSlot ((size_t) juce::jlimit (0, 31, slot));
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
juce::String Dedal2Processor::sampleWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"; }

bool Dedal2Processor::loadSample (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples < 128 || reader->sampleRate <= 0.0)
        return false;

    // Mono, at most a minute, peak brought to full level so it works as audio and as modulation alike.
    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
    juce::AudioBuffer<float> audio ((int) reader->numChannels, length);
    reader->read (&audio, 0, length, 0, true, true);

    auto fresh = std::make_unique<Sample>();
    fresh->rate = reader->sampleRate;
    fresh->data.assign ((size_t) length + 1, 0.0f);
    auto peak = 0.0f;

    for (int i = 0; i < length; ++i)
    {
        auto sum = 0.0f;

        for (int c = 0; c < audio.getNumChannels(); ++c)
            sum += audio.getSample (c, i);

        fresh->data[(size_t) i] = sum / (float) audio.getNumChannels();
        peak = std::max (peak, std::abs (fresh->data[(size_t) i]));
    }

    if (peak < 1.0e-5f)
        return false;

    for (auto& v : fresh->data)
        v /= peak;

    engine.setSample (fresh.get());
    sampleBefore = std::move (sampleNow);
    sampleNow = std::move (fresh);
    sampleFile = file;
    sendChangeMessage();
    return true;
}

void Dedal2Processor::clearSample()
{
    engine.setSample (nullptr);
    sampleBefore = std::move (sampleNow);
    sampleFile = juce::File();
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
    state.setProperty ("stride", 16, nullptr);

    if (sampleFile != juce::File())
        state.setProperty ("sample", sampleFile.getFullPathName(), nullptr);

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

        // Older saves kept SEQ2 right after SEQ1's eight slots; it now starts at slot 16.
        if ((int) tree.getProperty ("stride", 8) != 16)
        {
            for (int i = 0; i < 8; ++i)
            {
                fresh.bits[(size_t) (16 + i)] = fresh.bits[(size_t) (8 + i)];
                fresh.values[(size_t) (16 + i)] = fresh.values[(size_t) (8 + i)];
                fresh.bits[(size_t) (8 + i)] = false;
            }
        }

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

    // The sample is kept as a path; a state without one leaves the loaded sample alone.
    if (const auto path = tree.getProperty ("sample").toString(); path.isNotEmpty() && juce::File (path) != sampleFile)
        loadSample (juce::File (path));

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

namespace
{
/** The example patches of the hardware manual. Knob positions are measured from its drawings
    (0 = fully counter-clockwise, 1 = fully clockwise); scale, cables and switches come from the
    drawings and the notes. */
struct ManualPatch
{
    const char* name;
    std::vector<std::pair<const char*, float>> knobs;
    std::vector<std::pair<int, int>> cables; // output, input
    const char* bits1;
    const char* bits2;
    int scale, tempo, order;
    bool chain;
    int length2;
    bool modOctaveAbove;
};

const std::vector<ManualPatch>& manualPatches()
{
    static const std::vector<ManualPatch> patches {
        { "A Simple Start", { { ID::vcoFreq, 0.50f }, { ID::vcoEg1, 0.49f }, { ID::vcoSeq1, 1.00f }, { ID::fm, 0.00f }, { ID::lvlVco, 1.00f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.00f }, { ID::noiseTone, 0.50f }, { ID::modFreq, 0.50f }, { ID::modEg1, 0.49f }, { ID::modSeq2, 0.00f }, { ID::lvlMod, 0.00f }, { ID::fold, 0.29f }, { ID::foldEg, 0.50f }, { ID::foldSeq, 1.00f }, { ID::cutoff, 0.99f }, { ID::cutEg, 0.50f }, { ID::cutSeq, 0.00f }, { ID::bias, 0.00f }, { ID::blend, 0.50f }, { ID::volume, 0.79f }, { ID::resonance, 0.00f }, { ID::mode, 0.00f }, { ID::umixLvl, 0.00f }, { ID::trigMix, 0.50f }, { ID::eg1Decay, 0.20f }, { ID::eg2Decay, 0.20f }, { ID::corrupt[0], 0.00f }, { ID::corrupt[1], 0.00f }, { ID::range[0], 1.00f }, { ID::range[1], 0.00f } },
          {  }, "00111010", "01111010", 12, 7, 0, false, 8, false },
        { "Lost in the Labyrinth", { { ID::vcoFreq, 0.51f }, { ID::vcoEg1, 0.49f }, { ID::vcoSeq1, 1.00f }, { ID::fm, 0.00f }, { ID::lvlVco, 0.80f }, { ID::lvlNoise, 0.35f }, { ID::lvlRing, 0.00f }, { ID::noiseTone, 0.15f }, { ID::modFreq, 0.30f }, { ID::modEg1, 0.49f }, { ID::modSeq2, 0.00f }, { ID::lvlMod, 0.36f }, { ID::fold, 0.13f }, { ID::foldEg, 0.45f }, { ID::foldSeq, 0.00f }, { ID::cutoff, 0.39f }, { ID::cutEg, 0.50f }, { ID::cutSeq, 1.00f }, { ID::bias, 0.69f }, { ID::blend, 0.99f }, { ID::volume, 0.79f }, { ID::resonance, 0.75f }, { ID::mode, 0.94f }, { ID::umixLvl, 0.00f }, { ID::trigMix, 0.63f }, { ID::eg1Decay, 0.37f }, { ID::eg2Decay, 0.50f }, { ID::corrupt[0], 0.35f }, { ID::corrupt[1], 0.35f }, { ID::range[0], 0.65f }, { ID::range[1], 0.64f } },
          { { outSeq1Trig, inBitFlip2 } }, "10110101", "00000010", 11, 7, 1, false, 8, false },
        { "Dry Brushes", { { ID::vcoFreq, 0.50f }, { ID::vcoEg1, 0.45f }, { ID::vcoSeq1, 1.00f }, { ID::fm, 0.00f }, { ID::lvlVco, 1.00f }, { ID::lvlNoise, 0.80f }, { ID::lvlRing, 0.00f }, { ID::noiseTone, 1.00f }, { ID::modFreq, 0.61f }, { ID::modEg1, 0.43f }, { ID::modSeq2, 0.52f }, { ID::lvlMod, 1.00f }, { ID::fold, 0.44f }, { ID::foldEg, 0.41f }, { ID::foldSeq, 0.00f }, { ID::cutoff, 0.00f }, { ID::cutEg, 1.00f }, { ID::cutSeq, 0.61f }, { ID::bias, 0.50f }, { ID::blend, 0.50f }, { ID::volume, 0.65f }, { ID::resonance, 0.00f }, { ID::mode, 0.00f }, { ID::umixLvl, 0.00f }, { ID::trigMix, 0.39f }, { ID::eg1Decay, 0.36f }, { ID::eg2Decay, 0.20f }, { ID::corrupt[0], 1.00f }, { ID::corrupt[1], 1.00f }, { ID::range[0], 0.41f }, { ID::range[1], 0.80f } },
          { { outEg1, inBlend }, { outMvco, inVcwIn } }, "01011011", "01000101", 12, 7, 0, false, 8, false },
        { "Celestial Conversations", { { ID::vcoFreq, 0.74f }, { ID::vcoEg1, 0.49f }, { ID::vcoSeq1, 1.00f }, { ID::fm, 0.00f }, { ID::lvlVco, 0.90f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.66f }, { ID::noiseTone, 0.00f }, { ID::modFreq, 0.50f }, { ID::modEg1, 0.49f }, { ID::modSeq2, 0.43f }, { ID::lvlMod, 0.58f }, { ID::fold, 0.50f }, { ID::foldEg, 0.65f }, { ID::foldSeq, 0.65f }, { ID::cutoff, 0.19f }, { ID::cutEg, 1.00f }, { ID::cutSeq, 1.00f }, { ID::bias, 0.19f }, { ID::blend, 0.15f }, { ID::volume, 0.70f }, { ID::resonance, 0.59f }, { ID::mode, 0.50f }, { ID::umixLvl, 0.00f }, { ID::trigMix, 0.79f }, { ID::eg1Decay, 0.32f }, { ID::eg2Decay, 0.65f }, { ID::corrupt[0], 1.00f }, { ID::corrupt[1], 1.00f }, { ID::range[0], 1.00f }, { ID::range[1], 0.50f } },
          { { outEg2, inBlend }, { outMvco, inUmix1 }, { outUmix, inVcwVca }, { outEg1, inBitFlip1 } }, "", "", 14, 4, 1, false, 8, false },
        { "Spiral Enigma", { { ID::vcoFreq, 0.74f }, { ID::vcoEg1, 1.00f }, { ID::vcoSeq1, 0.35f }, { ID::fm, 1.00f }, { ID::lvlVco, 0.90f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.00f }, { ID::noiseTone, 0.00f }, { ID::modFreq, 0.67f }, { ID::modEg1, 1.00f }, { ID::modSeq2, 0.00f }, { ID::lvlMod, 1.00f }, { ID::fold, 0.39f }, { ID::foldEg, 0.65f }, { ID::foldSeq, 0.35f }, { ID::cutoff, 0.28f }, { ID::cutEg, 1.00f }, { ID::cutSeq, 1.00f }, { ID::bias, 0.23f }, { ID::blend, 0.50f }, { ID::volume, 0.69f }, { ID::resonance, 0.99f }, { ID::mode, 0.50f }, { ID::umixLvl, 0.00f }, { ID::trigMix, 0.59f }, { ID::eg1Decay, 0.65f }, { ID::eg2Decay, 1.00f }, { ID::corrupt[0], 1.00f }, { ID::corrupt[1], 0.50f }, { ID::range[0], 1.00f }, { ID::range[1], 0.50f } },
          { { outEg1, inVcoVoct }, { outMvco, inMvcoVoct }, { outMixer, inVcwIn } }, "10110100", "01101000", 0, 2, 1, false, 8, false },
        { "Swirling Magenta", { { ID::vcoFreq, 0.50f }, { ID::vcoEg1, 0.49f }, { ID::vcoSeq1, 1.00f }, { ID::fm, 0.00f }, { ID::lvlVco, 1.00f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.00f }, { ID::noiseTone, 0.00f }, { ID::modFreq, 0.10f }, { ID::modEg1, 0.60f }, { ID::modSeq2, 0.81f }, { ID::lvlMod, 1.00f }, { ID::fold, 0.39f }, { ID::foldEg, 0.50f }, { ID::foldSeq, 0.35f }, { ID::cutoff, 0.09f }, { ID::cutEg, 0.66f }, { ID::cutSeq, 0.00f }, { ID::bias, 0.00f }, { ID::blend, 0.50f }, { ID::volume, 0.99f }, { ID::resonance, 0.39f }, { ID::mode, 0.59f }, { ID::umixLvl, 0.50f }, { ID::trigMix, 0.50f }, { ID::eg1Decay, 0.65f }, { ID::eg2Decay, 0.65f }, { ID::corrupt[0], 0.50f }, { ID::corrupt[1], 0.65f }, { ID::range[0], 0.35f }, { ID::range[1], 0.00f } },
          { { outMvco, inUmix2 }, { outUmix, inVcwIn }, { outMixer, inUmix1 }, { outEg2, inTrigger }, { outSeq2Trig, inBitFlip1 } }, "11010000", "10011000", 8, 7, 2, false, 8, false },
        { "New Frontiers", { { ID::vcoFreq, 0.61f }, { ID::vcoEg1, 0.49f }, { ID::vcoSeq1, 0.00f }, { ID::fm, 0.11f }, { ID::lvlVco, 1.00f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.00f }, { ID::noiseTone, 1.00f }, { ID::modFreq, 0.10f }, { ID::modEg1, 0.49f }, { ID::modSeq2, 0.36f }, { ID::lvlMod, 1.00f }, { ID::fold, 0.29f }, { ID::foldEg, 0.20f }, { ID::foldSeq, 0.35f }, { ID::cutoff, 0.00f }, { ID::cutEg, 1.00f }, { ID::cutSeq, 0.65f }, { ID::bias, 0.58f }, { ID::blend, 0.50f }, { ID::volume, 0.69f }, { ID::resonance, 0.00f }, { ID::mode, 0.39f }, { ID::umixLvl, 0.00f }, { ID::trigMix, 0.53f }, { ID::eg1Decay, 0.35f }, { ID::eg2Decay, 0.35f }, { ID::corrupt[0], 0.35f }, { ID::corrupt[1], 1.00f }, { ID::range[0], 0.87f }, { ID::range[1], 0.80f } },
          { { outMixer, inVcwIn }, { outMvco, inFold }, { outSeq2Trig, inBitFlip1 } }, "01100101", "10000100", 3, 7, 1, false, 8, false },
        { "Syndrums", { { ID::vcoFreq, 0.50f }, { ID::vcoEg1, 0.00f }, { ID::vcoSeq1, 0.00f }, { ID::fm, 0.00f }, { ID::lvlVco, 1.00f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.00f }, { ID::noiseTone, 1.00f }, { ID::modFreq, 0.74f }, { ID::modEg1, 0.65f }, { ID::modSeq2, 0.00f }, { ID::lvlMod, 1.00f }, { ID::fold, 0.09f }, { ID::foldEg, 0.65f }, { ID::foldSeq, 0.00f }, { ID::cutoff, 0.00f }, { ID::cutEg, 0.81f }, { ID::cutSeq, 1.00f }, { ID::bias, 0.00f }, { ID::blend, 0.59f }, { ID::volume, 0.99f }, { ID::resonance, 0.99f }, { ID::mode, 0.39f }, { ID::umixLvl, 0.00f }, { ID::trigMix, 0.59f }, { ID::eg1Decay, 0.65f }, { ID::eg2Decay, 0.35f }, { ID::corrupt[0], 0.35f }, { ID::corrupt[1], 1.00f }, { ID::range[0], 0.00f }, { ID::range[1], 0.35f } },
          { { outMvco, inVcwIn }, { outClock, inFold } }, "11010000", "01100101", 2, 7, 2, false, 8, false },
        { "Telemechanical Birds", { { ID::vcoFreq, 0.87f }, { ID::vcoEg1, 0.37f }, { ID::vcoSeq1, 0.20f }, { ID::fm, 0.00f }, { ID::lvlVco, 0.50f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.20f }, { ID::noiseTone, 0.50f }, { ID::modFreq, 0.50f }, { ID::modEg1, 0.62f }, { ID::modSeq2, 1.00f }, { ID::lvlMod, 0.00f }, { ID::fold, 0.59f }, { ID::foldEg, 0.19f }, { ID::foldSeq, 0.00f }, { ID::cutoff, 0.79f }, { ID::cutEg, 0.34f }, { ID::cutSeq, 0.20f }, { ID::bias, 0.50f }, { ID::blend, 0.99f }, { ID::volume, 0.79f }, { ID::resonance, 0.59f }, { ID::mode, 0.99f }, { ID::umixLvl, 0.79f }, { ID::trigMix, 0.50f }, { ID::eg1Decay, 0.25f }, { ID::eg2Decay, 0.50f }, { ID::corrupt[0], 1.00f }, { ID::corrupt[1], 1.00f }, { ID::range[0], 0.50f }, { ID::range[1], 0.50f } },
          { { outUmix, inVcoVoct }, { outMvco, inClock1 }, { outMixer, inVcwVca } }, "", "", 2, 4, 1, false, 8, false },
        { "Where You Lead Kicks Follow", { { ID::vcoFreq, 0.75f }, { ID::vcoEg1, 0.61f }, { ID::vcoSeq1, 1.00f }, { ID::fm, 0.00f }, { ID::lvlVco, 0.50f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.80f }, { ID::noiseTone, 0.50f }, { ID::modFreq, 0.62f }, { ID::modEg1, 0.63f }, { ID::modSeq2, 1.00f }, { ID::lvlMod, 0.00f }, { ID::fold, 0.79f }, { ID::foldEg, 0.62f }, { ID::foldSeq, 0.00f }, { ID::cutoff, 0.19f }, { ID::cutEg, 0.62f }, { ID::cutSeq, 0.00f }, { ID::bias, 0.79f }, { ID::blend, 0.59f }, { ID::volume, 0.79f }, { ID::resonance, 0.79f }, { ID::mode, 0.00f }, { ID::umixLvl, 0.98f }, { ID::trigMix, 0.98f }, { ID::eg1Decay, 0.28f }, { ID::eg2Decay, 0.27f }, { ID::corrupt[0], 1.00f }, { ID::corrupt[1], 0.00f }, { ID::range[0], 0.50f }, { ID::range[1], 0.50f } },
          { { outMvco, inVcfIn }, { outEg1, inVcfVca }, { outSeq1Trig, inEg2Trig } }, "", "10000000", 2, 7, 0, false, 4, false },
        { "Twinkle Toes", { { ID::vcoFreq, 0.55f }, { ID::vcoEg1, 0.49f }, { ID::vcoSeq1, 1.00f }, { ID::fm, 0.00f }, { ID::lvlVco, 0.50f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.00f }, { ID::noiseTone, 0.50f }, { ID::modFreq, 0.93f }, { ID::modEg1, 0.49f }, { ID::modSeq2, 1.00f }, { ID::lvlMod, 0.00f }, { ID::fold, 0.13f }, { ID::foldEg, 0.50f }, { ID::foldSeq, 0.00f }, { ID::cutoff, 0.33f }, { ID::cutEg, 0.58f }, { ID::cutSeq, 1.00f }, { ID::bias, 0.51f }, { ID::blend, 0.73f }, { ID::volume, 0.79f }, { ID::resonance, 0.23f }, { ID::mode, 0.98f }, { ID::umixLvl, 0.00f }, { ID::trigMix, 0.00f }, { ID::eg1Decay, 0.66f }, { ID::eg2Decay, 0.65f }, { ID::corrupt[0], 0.00f }, { ID::corrupt[1], 1.00f }, { ID::range[0], 0.43f }, { ID::range[1], 0.43f } },
          { { outMvco, inVcfIn }, { outEg1, inVcwVca }, { outEg2, inCutoff }, { outSeq1Trig, inClock2 }, { outSeq2Trig, inEg2Trig } }, "10101010", "", 2, 7, 0, false, 8, true },
        { "The Ooze", { { ID::vcoFreq, 0.50f }, { ID::vcoEg1, 0.49f }, { ID::vcoSeq1, 1.00f }, { ID::fm, 0.00f }, { ID::lvlVco, 1.00f }, { ID::lvlNoise, 0.00f }, { ID::lvlRing, 0.00f }, { ID::noiseTone, 0.00f }, { ID::modFreq, 0.23f }, { ID::modEg1, 0.49f }, { ID::modSeq2, 0.00f }, { ID::lvlMod, 0.26f }, { ID::fold, 0.45f }, { ID::foldEg, 0.50f }, { ID::foldSeq, 1.00f }, { ID::cutoff, 0.13f }, { ID::cutEg, 0.65f }, { ID::cutSeq, 1.00f }, { ID::bias, 0.50f }, { ID::blend, 0.75f }, { ID::volume, 0.79f }, { ID::resonance, 0.79f }, { ID::mode, 0.55f }, { ID::umixLvl, 0.07f }, { ID::trigMix, 0.00f }, { ID::eg1Decay, 0.38f }, { ID::eg2Decay, 0.65f }, { ID::corrupt[0], 1.00f }, { ID::corrupt[1], 0.00f }, { ID::range[0], 1.00f }, { ID::range[1], 1.00f } },
          { { outUmix, inVcoVoct }, { outNoise, inMvcoVoct }, { outSeq2Cv, inBlend }, { outMvco, inUmix1 }, { outSeq1Trig, inBitFlip2 } }, "", "", 2, 7, 1, true, 8, false },
    };
    return patches;
}
} // namespace

juce::StringArray Dedal2Processor::getFactoryPresetNames() const
{
    juce::StringArray names { "Init", "Fold + filtr", "Polirytm 5 : 7", "Kable: SEQ2 > BLEND, EG2 > FOLD", "Kable: M VCO taktuje SEQ2" };

    for (const auto& p : manualPatches())
        names.add (p.name);

    names.addArray ({ pl ("Akordy: dwa głosy z CHAIN"), "Akordy: kwinty", "Akordy: 32 kroki", "Flip: SEQ1 zmienia SEQ2",
                      pl ("Flip: wzajemnie"), "Flip: 5 na 16",
                      "West: bongo (LPG)", pl ("West: FM dzwon"), pl ("West: fałdowany bas"), pl ("West: niepewność"),
                      "West: pulser", "West: krople", "West: oddech", "Saw: M VCO przez VCW i VCF" });

    return names;
}

void Dedal2Processor::loadFactoryPreset (int presetIndex)
{
    struct Setting { const char* id; float value; };
    struct Cable { int out, in; };
    std::vector<Setting> settings;
    std::vector<Cable> cables;
    juce::String bits[2];
    int chainOffset = 0;
    const auto firstOwn = 5 + (int) manualPatches().size();

    // Two voices at once: the VCO plays SEQ1, the MOD VCO SEQ2, both heard through the mixer and
    // fired together, so every step is an interval; with CHAIN both read one sequence, apart.
    const std::vector<Setting> twoVoices { { ID::lvlVco, 0.45f }, { ID::lvlMod, 0.45f }, { ID::modFreq, 48.0f }, { ID::trigMix, 0.5f },
                                           { ID::midiSeq, 2 }, { ID::fold, 0.1f }, { ID::foldEg, 0.2f } };

    if (presetIndex == firstOwn) // chords from one chained sequence read by two heads
    {
        settings = twoVoices;
        settings.insert (settings.end(), { { ID::chain, 1 }, { ID::length[0], 4 }, { ID::length[1], 4 }, { ID::scale, 10 },
                                           { ID::range[0], 0.3f }, { ID::range[1], 0.3f }, { ID::eg2Decay, 1.2f }, { ID::tempo, 4 },
                                           { ID::cutoff, 0.62f } });
        chainOffset = 2;
    }
    else if (presetIndex == firstOwn + 1) // the MOD VCO a fifth above, both following their own sequence
    {
        settings = twoVoices;
        settings.insert (settings.end(), { { ID::modFreq, 55.0f }, { ID::scale, 3 }, { ID::range[0], 0.25f }, { ID::range[1], 0.15f },
                                           { ID::length[1], 6 }, { ID::tempo, 7 }, { ID::eg2Decay, 0.4f } });
    }
    else if (presetIndex == firstOwn + 2) // 16 + 16 chained: 32 steps, two heads eight steps apart
    {
        settings = twoVoices;
        settings.insert (settings.end(), { { ID::steps16, 1 }, { ID::chain, 1 }, { ID::length[0], 16 }, { ID::length[1], 16 },
                                           { ID::scale, 13 }, { ID::range[0], 0.35f }, { ID::range[1], 0.35f }, { ID::tempo, 4 },
                                           { ID::eg1Decay, 1.0f }, { ID::eg2Decay, 2.0f }, { ID::cutoff, 0.55f }, { ID::corrupt[0], 0.15f } });
        chainOffset = 8;
    }
    else if (presetIndex == firstOwn + 3) // every SEQ1 trigger flips a bit of SEQ2
    {
        settings = { { ID::lvlMod, 0.4f }, { ID::trigMix, 0.5f }, { ID::scale, 12 }, { ID::modFreq, 60.0f }, { ID::tempo, 7 },
                     { ID::range[1], 0.25f }, { ID::cutSeq, 0.4f }, { ID::eg2Decay, 0.3f }, { ID::length[1], 7 } };
        cables = { { outSeq1Trig, inBitFlip2 } };
    }
    else if (presetIndex == firstOwn + 4) // each rewrites the other
    {
        settings = { { ID::lvlMod, 0.4f }, { ID::trigMix, 0.5f }, { ID::scale, 8 }, { ID::modFreq, 55.0f }, { ID::tempo, 7 },
                     { ID::length[0], 7 }, { ID::length[1], 5 }, { ID::range[0], 0.2f }, { ID::range[1], 0.2f }, { ID::fold, 0.25f },
                     { ID::foldSeq, 0.3f }, { ID::eg2Decay, 0.25f } };
        cables = { { outSeq1Trig, inBitFlip2 }, { outSeq2Trig, inBitFlip1 } };
    }
    // West-coast patches in the spirit of the classic suitcase synths: a sine core with FM, folding for
    // timbre, and EG2 opening the filter and the VCA together like a low-pass gate.
    else if (presetIndex == firstOwn + 6) // bongo: a struck, resonant low-pass gate
    {
        settings = { { ID::order, 1 }, { ID::blend, 1.0f }, { ID::cutoff, 0.22f }, { ID::cutEg, 0.75f }, { ID::resonance, 0.55f },
                     { ID::fold, 0.2f }, { ID::foldEg, 0.3f }, { ID::eg1Decay, 0.14f }, { ID::eg2Decay, 0.16f }, { ID::scale, 3 },
                     { ID::range[0], 0.18f }, { ID::tempo, 7 }, { ID::corrupt[0], 0.15f }, { ID::trigMix, 0.0f } };
        cables = { { outEg2, inCutoff } };
    }
    else if (presetIndex == firstOwn + 7) // bell: audio-rate FM at a non-integer ratio, EG1 folds the strike
    {
        settings = { { ID::vcoFreq, 60.0f }, { ID::modFreq, 79.0f }, { ID::modSeq2, 0.0f }, { ID::fm, 0.45f }, { ID::fold, 0.1f },
                     { ID::foldEg, 0.6f }, { ID::eg1Decay, 0.6f }, { ID::eg2Decay, 1.4f }, { ID::blend, 0.3f }, { ID::cutoff, 0.7f },
                     { ID::scale, 14 }, { ID::range[0], 0.2f }, { ID::tempo, 4 }, { ID::trigMix, 0.0f } };
        cables = { { outEg2, inCutoff } };
    }
    else if (presetIndex == firstOwn + 8) // folded bass: low sine, all timbre from the folder
    {
        settings = { { ID::vcoFreq, 33.0f }, { ID::blend, 0.0f }, { ID::fold, 0.35f }, { ID::foldEg, 0.7f }, { ID::bias, 0.25f },
                     { ID::foldSeq, 0.4f }, { ID::eg1Decay, 0.25f }, { ID::eg2Decay, 0.35f }, { ID::scale, 12 }, { ID::range[0], 0.12f },
                     { ID::tempo, 7 }, { ID::trigMix, 0.0f } };
    }
    else if (presetIndex == firstOwn + 9) // uncertainty: random voltages steer the fold, a slow MOD VCO the filter
    {
        settings = { { ID::scale, 0 }, { ID::corrupt[0], 0.6f }, { ID::corrupt[1], 0.7f }, { ID::range[0], 0.25f }, { ID::range[1], 0.6f },
                     { ID::modFreq, -30.0f }, { ID::modSeq2, 0.0f }, { ID::fold, 0.2f }, { ID::foldEg, 0.6f }, { ID::cutEg, 0.4f },
                     { ID::blend, 0.5f }, { ID::eg1Decay, 0.4f }, { ID::eg2Decay, 0.6f }, { ID::tempo, 4 }, { ID::trigMix, 0.5f } };
        cables = { { outSeq2Cv, inFold }, { outMvco, inCutoff } };
    }
    else if (presetIndex == firstOwn + 10) // pulser: the MOD VCO is the clock, SEQ2 bends its speed
    {
        settings = { { ID::modFreq, -12.0f }, { ID::modSeq2, 0.0f }, { ID::range[1], 0.3f }, { ID::scale, 3 }, { ID::range[0], 0.2f },
                     { ID::cutoff, 0.25f }, { ID::cutEg, 0.7f }, { ID::resonance, 0.45f }, { ID::blend, 0.8f }, { ID::order, 1 },
                     { ID::eg1Decay, 0.15f }, { ID::eg2Decay, 0.2f }, { ID::trigMix, 0.0f } };
        cables = { { outMvco, inClock1 }, { outSeq2Cv, inMvcoVoct }, { outEg2, inCutoff } };
    }
    else if (presetIndex == firstOwn + 11) // drops: high, falling pitch, short bright band-pass
    {
        settings = { { ID::vcoFreq, 84.0f }, { ID::vcoEg1, 0.35f }, { ID::eg1Decay, 0.07f }, { ID::eg2Decay, 0.12f }, { ID::mode, 1.0f },
                     { ID::resonance, 0.7f }, { ID::cutoff, 0.6f }, { ID::cutEg, 0.5f }, { ID::blend, 0.75f }, { ID::scale, 0 },
                     { ID::range[0], 0.3f }, { ID::corrupt[0], 0.5f }, { ID::tempo, 8 }, { ID::trigMix, 0.0f } };
    }
    else if (presetIndex == firstOwn + 12) // breath: a slow MOD VCO opens the folder's VCA, long sequence steps
    {
        settings = { { ID::modFreq, -40.0f }, { ID::modSeq2, 0.0f }, { ID::blend, 0.35f }, { ID::fold, 0.3f }, { ID::foldSeq, 0.5f },
                     { ID::foldEg, 0.3f }, { ID::eg1Decay, 2.5f }, { ID::eg2Decay, 4.0f }, { ID::tempo, 1 }, { ID::scale, 13 },
                     { ID::range[0], 0.25f } };
        cables = { { outMvco, inVcwVca } };
    }
    else if (presetIndex == firstOwn + 13)
    {
        // A saw voice as read from a video frame: the M VCO synced to itself, sequenced through its
        // 1V/OCT jack by SEQ2, sent straight into VCW IN and on through the filter (ORDER VCW > VCF),
        // so the mixer stays free. Small knobs could not be read; those are set by ear.
        settings = { { ID::order, 1 }, { ID::blend, 0.9f }, { ID::fold, 0.12f }, { ID::bias, 0.75f }, { ID::foldEg, 0.2f },
                     { ID::cutoff, 0.45f }, { ID::cutEg, 0.45f }, { ID::resonance, 0.55f }, { ID::mode, 0.0f }, { ID::umixLvl, 0.85f },
                     { ID::modFreq, 36.0f }, { ID::modSeq2, 0.0f }, { ID::lvlVco, 0.0f }, { ID::range[1], 0.25f }, { ID::range[0], 0.2f },
                     { ID::scale, 12 }, { ID::tempo, 7 }, { ID::trigMix, 1.0f }, { ID::eg1Decay, 0.35f }, { ID::eg2Decay, 0.45f },
                     { ID::midiSeq, 1 } };
        cables = { { outMvco, inMvcoSync }, { outMvco, inVcwIn }, { outSeq2Cv, inMvcoVoct } };
        bits[0] = "11101110";
        bits[1] = "11111111";
    }
    else if (presetIndex == firstOwn + 5) // a short SEQ1 keeps rewriting a long SEQ2 in ever new places
    {
        settings = { { ID::steps16, 1 }, { ID::length[0], 5 }, { ID::length[1], 16 }, { ID::lvlMod, 0.45f }, { ID::trigMix, 0.7f },
                     { ID::scale, 3 }, { ID::modFreq, 60.0f }, { ID::tempo, 7 }, { ID::range[1], 0.3f }, { ID::eg2Decay, 0.35f },
                     { ID::cutSeq, 0.5f }, { ID::resonance, 0.45f } };
        cables = { { outSeq1Trig, inBitFlip2 } };
    }

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
        default:
            break;
    }

    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& s : settings)
        setParameter (s.id, s.value);

    const ManualPatch* manual = presetIndex >= 5 && presetIndex - 5 < (int) manualPatches().size() ? &manualPatches()[(size_t) (presetIndex - 5)] : nullptr;

    if (manual != nullptr)
    {
        for (const auto& [id, position] : manual->knobs)
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (position);

        setParameter (ID::scale, (float) manual->scale);
        setParameter (ID::tempo, (float) manual->tempo);
        setParameter (ID::order, (float) manual->order);
        setParameter (ID::chain, manual->chain ? 1.0f : 0.0f);
        setParameter (ID::length[1], (float) manual->length2);

        if (manual->modOctaveAbove)
            setParameter (ID::modFreq, value (ID::vcoFreq) + 12.0f);

        for (const auto& [out, in] : manual->cables)
            cables.push_back ({ out, in });
    }

    for (int i = 0; i < numIns; ++i)
        engine.setCable (i, -1);

    for (const auto& c : cables)
        engine.setCable (c.in, c.out);

    {
        const juce::SpinLock::ScopedLockType lock (seqLock);
        sequencers.random.setSeed (41 + presetIndex * 7);
        sequencers.state = {};
        sequencers.randomise (0, 0.7f, 16);
        sequencers.randomise (1, 0.7f, 16);
        sequencers.state.bits[0] = sequencers.state.bits[16] = true;
        sequencers.state.chainOffset = chainOffset;

        if (manual != nullptr)
        {
            bits[0] = manual->bits1;
            bits[1] = manual->bits2;
        }

        for (int k = 0; k < 2; ++k)
            for (int i = 0; i < bits[k].length(); ++i)
                sequencers.state.bits[(size_t) (k * 16 + i)] = bits[k][i] == '1';
        sequencers.random.setSeed (juce::Random::getSystemRandom().nextInt64());
    }

    presetName = getFactoryPresetNames()[presetIndex];
    buffer = {};
    transposeNote = -1;
    sendChangeMessage();
}

juce::AudioProcessorEditor* Dedal2Processor::createEditor() { return new Dedal2Editor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Dedal2Processor(); }
