#include "SolistaProcessor.h"
#include "SolistaEditor.h"

using namespace solista;

namespace solista
{
juce::StringArray getKeyNames()
{
    return { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
}

juce::StringArray getStyleNames()
{
    return { "Epicki", pl ("Śpiewny"), "Groove", "Swing", "Blues" };
}
} // namespace solista

namespace
{
bool sameParams (const GeneratorParams& a, const GeneratorParams& b)
{
    auto close = [] (double x, double y) { return std::abs (x - y) < 1.0e-4; };

    return a.keyPc == b.keyPc && a.minorKey == b.minorKey && a.progression == b.progression && a.bars == b.bars
        && a.style == b.style && a.shuffle == b.shuffle && a.seed == b.seed && close (a.density, b.density)
        && close (a.bends, b.bends) && close (a.vibrato, b.vibrato) && close (a.syncopation, b.syncopation)
        && close (a.space, b.space) && close (a.bpm, b.bpm);
}

size_t firstEventAt (const std::vector<Event>& events, double beat)
{
    return (size_t) (std::lower_bound (events.begin(), events.end(), beat,
                                       [] (const Event& e, double b) { return e.beat < b; })
                     - events.begin());
}

constexpr int kAllNotesOff = 3;
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SolistaProcessor::createLayout()
{
    using Choice = juce::AudioParameterChoice;
    using Float = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto choice = [&layout] (const char* id, const juce::String& name, const juce::StringArray& options, int initial)
    {
        layout.add (std::make_unique<Choice> (juce::ParameterID { id, 1 }, name, options, initial));
    };

    auto percent = [&layout] (const char* id, const juce::String& name, float initial)
    {
        layout.add (std::make_unique<Float> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), initial,
                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                                 [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));
    };

    choice (ParamID::key, "Tonacja", getKeyNames(), 9);
    choice (ParamID::mode, "Tryb", { "Moll", "Dur" }, 0);
    choice (ParamID::progression, "Akordy", { "1", "2", "3", "4", "5", "6" }, 0);
    choice (ParamID::bars, pl ("Długość"), { "4 takty", pl ("8 taktów"), pl ("12 taktów"), pl ("16 taktów") }, 1);
    choice (ParamID::style, "Styl", getStyleNames(), 0);
    choice (ParamID::feel, "Puls", { "Prosto", "Shuffle" }, 0);

    percent (ParamID::density, pl ("Gęstość"), 0.5f);
    percent (ParamID::bends, "Bendy", 0.6f);
    percent (ParamID::vibrato, "Vibrato", 0.6f);
    percent (ParamID::syncopation, "Synkopy", 0.5f);
    percent (ParamID::space, "Oddech", 0.5f);

    choice (ParamID::harmony, "Druga gitara", { "Brak", pl ("Tercja wyżej"), pl ("Tercja niżej") }, 0);
    choice (ParamID::bendRange, "Zakres pitch bend", { pl ("±2"), pl ("±12"), pl ("±24") }, 0);

    percent (ParamID::drive, "Przester", 0.55f);
    percent (ParamID::tone, "Barwa", 0.5f);
    percent (ParamID::ambience, pl ("Pogłos"), 0.35f);
    layout.add (std::make_unique<Float> (juce::ParameterID { ParamID::volume, 1 }, pl ("Głośność"),
                                         juce::NormalisableRange<float> (-30.0f, 6.0f, 0.1f), -6.0f,
                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                             [] (float v, int) { return juce::String (v, 1) + " dB"; })));
    return layout;
}

SolistaProcessor::SolistaProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SOLISTA", createLayout())
{
    seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (0x7ffffffe) + 1u;
    queue.reserve (16384);
    regenerateIfNeeded();
    startTimerHz (20);
}

SolistaProcessor::~SolistaProcessor()
{
    stopTimer();
    active.store (nullptr);
}

bool SolistaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
GeneratorParams SolistaProcessor::readParams() const
{
    auto index = [this] (const char* id) { return juce::roundToInt (raw (id)->load()); };

    GeneratorParams p;
    p.keyPc = juce::jlimit (0, 11, index (ParamID::key));
    p.minorKey = index (ParamID::mode) == 0;
    p.progression = juce::jlimit (0, kNumProgressions - 1, index (ParamID::progression));
    p.bars = kBarChoices[juce::jlimit (0, 3, index (ParamID::bars))];
    p.style = juce::jlimit (0, (int) numStyles - 1, index (ParamID::style));
    p.shuffle = index (ParamID::feel) == 1;
    p.density = raw (ParamID::density)->load();
    p.bends = raw (ParamID::bends)->load();
    p.vibrato = raw (ParamID::vibrato)->load();
    p.syncopation = raw (ParamID::syncopation)->load();
    p.space = raw (ParamID::space)->load();
    p.bpm = std::round (hostBpm.load() * 100.0) / 100.0;
    p.seed = seed.load();
    return p;
}

bool SolistaProcessor::regenerateIfNeeded()
{
    {
        const juce::ScopedLock lock (generationLock);
        const auto params = readParams();
        const auto harmony = juce::jlimit (0, 2, juce::roundToInt (raw (ParamID::harmony)->load()));

        if (current != nullptr && sameParams (current->params, params) && current->harmony == harmony)
            return false;

        auto bundle = std::make_shared<SoloBundle>();
        bundle->params = params;
        bundle->harmony = harmony;
        bundle->solo = generateSolo (params);
        bundle->events = renderEvents (bundle->solo);

        if (harmony != 0)
        {
            bundle->second = makeHarmony (bundle->solo, harmony == 1 ? 2 : -2, params.seed ^ 0x9e3779b9u);
            bundle->secondEvents = renderEvents (bundle->second);
        }

        publish (std::move (bundle));
    }

    sendChangeMessage();
    return true;
}

void SolistaProcessor::publish (std::shared_ptr<const SoloBundle> bundle)
{
    // The audio thread may still be reading the old solo: keep it for a while.
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const auto& r) { return now - r.second > 3000u; }),
                   retired.end());

    if (current != nullptr)
        retired.emplace_back (current, now);

    current = std::move (bundle);
    active.store (current.get(), std::memory_order_release);
}

std::shared_ptr<const SoloBundle> SolistaProcessor::getBundle() const
{
    const juce::ScopedLock lock (generationLock);
    return current;
}

void SolistaProcessor::timerCallback()
{
    regenerateIfNeeded();
}

void SolistaProcessor::newSolo()
{
    history.push_back (seed.load());

    if (history.size() > 64)
        history.erase (history.begin());

    seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (0x7ffffffe) + 1u;
    restart = true;
    regenerateIfNeeded();
}

void SolistaProcessor::previousSolo()
{
    if (history.empty())
        return;

    seed = history.back();
    history.pop_back();
    restart = true;
    regenerateIfNeeded();
}

void SolistaProcessor::setSeed (juce::uint32 newSeed)
{
    seed = newSeed;
    regenerateIfNeeded();
}

void SolistaProcessor::setAudition (bool shouldPlay)
{
    if (shouldPlay)
        restart = true;

    audition = shouldPlay;
    sendChangeMessage();
}

void SolistaProcessor::setFollowHost (bool shouldFollow)
{
    followHost = shouldFollow;
    sendChangeMessage();
}

//==============================================================================
juce::String SolistaProcessor::describeSolo() const
{
    const auto bundle = getBundle();

    if (bundle == nullptr)
        return "Solista";

    const auto& p = bundle->params;
    return getKeyNames()[p.keyPc] + (p.minorKey ? " moll" : " dur") + " - " + getStyleNames()[p.style] + " - "
         + juce::String (juce::roundToInt (p.bpm)) + " BPM - " + juce::String::toHexString ((int) (p.seed & 0xffff)).toUpperCase();
}

juce::File SolistaProcessor::writeMidiFile (bool second) const
{
    const auto bundle = getBundle();

    if (bundle == nullptr || (second && bundle->harmony == 0))
        return {};

    const auto range = kBendRanges[juce::jlimit (0, 2, juce::roundToInt (raw (ParamID::bendRange)->load()))];
    const auto name = "Solista - " + describeSolo() + (second ? " - gitara 2" : "");
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Solista");
    folder.createDirectory();

    const auto file = folder.getChildFile (juce::File::createLegalFileName (name.replaceCharacter ('#', 's')) + ".mid");
    file.deleteFile();
    juce::FileOutputStream out (file);

    if (! out.openedOk())
        return {};

    makeMidiFile (second ? bundle->second : bundle->solo, range, name).writeTo (out);
    out.flush();
    return file;
}

//==============================================================================
void SolistaProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = apvts.copyState();
    state.setProperty ("seed", (juce::int64) seed.load(), nullptr);
    state.setProperty ("followHost", followHost.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void SolistaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);

        if (state.isValid() && state.hasType (apvts.state.getType()))
        {
            seed = (juce::uint32) (juce::int64) state.getProperty ("seed", (juce::int64) seed.load());
            followHost = (bool) state.getProperty ("followHost", true);
            history.clear();
            apvts.replaceState (state);
            regenerateIfNeeded();
            sendChangeMessage();
        }
    }
}

//==============================================================================
void SolistaProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    lead.prepare (sampleRate, samplesPerBlock);
    secondGuitar.prepare (sampleRate, samplesPerBlock);
    scratch.setSize (2, std::max (64, samplesPerBlock));
    playing = nullptr;
    wasPlaying = wasSynced = false;
    midiHeld = secondTail = 0;
    spread = 0.0f;
    uiPosition = -1.0;
    regenerateIfNeeded();
}

void SolistaProcessor::push (const Scheduled& event)
{
    if (queue.size() < queue.capacity())
    {
        queue.push_back (event);
        queue.back().order = (int) queue.size();
    }
}

void SolistaProcessor::collectSolo (const std::vector<Event>& events, size_t& index, int target, double from, double to,
                                    double beatsPerSample, int sampleBase, int limit)
{
    while (index < events.size() && events[index].beat < to)
    {
        const auto& e = events[index++];
        Scheduled s;
        s.offset = sampleBase + juce::jlimit (0, limit - 1, (int) ((e.beat - from) / beatsPerSample));
        s.target = target;
        s.type = (int) e.type;
        s.pitch = e.pitch;
        s.velocity = e.velocity;
        s.legato = e.legato;
        s.semitones = e.semitones;
        push (s);
    }
}

void SolistaProcessor::apply (const Scheduled& event)
{
    auto& synth = event.target == 0 ? lead : secondGuitar;

    switch (event.type)
    {
        case Event::bend:
            synth.setBend (event.semitones);
            break;

        case Event::noteOff:
            synth.noteOff (event.pitch);

            if (event.fromMidi)
                midiHeld = std::max (0, midiHeld - 1);

            break;

        case Event::noteOn:
        {
            auto legato = event.legato;

            if (event.fromMidi)
            {
                // Overlapping notes are hammer-ons and pull-offs; notes that start together are a chord.
                const auto now = sampleClock + event.offset;
                legato = midiHeld > 0 && now - lastMidiNoteOn > (juce::int64) (0.008 * sampleRate);
                lastMidiNoteOn = now;
                ++midiHeld;
            }

            synth.noteOn (event.pitch, event.velocity, legato);

            if (event.target == 1)
                secondTail = (int) (5.0 * sampleRate);

            break;
        }

        default:
            synth.allNotesOff();
            synth.setBend (0.0f);

            if (event.fromMidi)
                midiHeld = 0;

            break;
    }
}

void SolistaProcessor::renderSegment (float* left, float* right, int numSamples)
{
    const auto chunkSize = scratch.getNumSamples();
    const auto spreadStep = 1.0f / (float) (0.03 * sampleRate);

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const auto n = std::min (chunkSize, numSamples - start);
        auto* l = left + start;
        auto* r = right + start;
        lead.render (l, r, n);

        const auto wanted = secondTail > 0 ? 1.0f : 0.0f;

        if (wanted <= 0.0f && spread <= 0.0f)
            continue;

        // With two guitars the first one leans left and the second one right.
        auto* sl = scratch.getWritePointer (0);
        auto* sr = scratch.getWritePointer (1);
        secondGuitar.render (sl, sr, n);

        for (int i = 0; i < n; ++i)
        {
            spread = juce::jlimit (0.0f, 1.0f, spread + (wanted > spread ? spreadStep : -spreadStep));
            const auto a = l[i], b = r[i];
            l[i] = a * (1.0f - 0.12f * spread) + sl[i] * 0.6f * spread;
            r[i] = b * (1.0f - 0.4f * spread) + sr[i] * 0.88f * spread;
        }
    }
}

void SolistaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() < 2 || numSamples == 0)
    {
        buffer.clear();
        midi.clear();
        return;
    }

    // Tempo and position come from the host.
    auto bpm = hostBpm.load();
    auto hostIsPlaying = false, hasPosition = false;
    double ppq = 0.0;

    if (auto* head = getPlayHead())
    {
        if (const auto position = head->getPosition())
        {
            if (const auto tempo = position->getBpm(); tempo.hasValue() && *tempo > 20.0 && *tempo < 1000.0)
                bpm = *tempo;

            hostIsPlaying = position->getIsPlaying();

            if (const auto quarters = position->getPpqPosition())
            {
                ppq = *quarters;
                hasPosition = true;
            }
        }
    }

    hostBpm = bpm;
    hostPlaying = hostIsPlaying;

    const auto drive = raw (ParamID::drive)->load(), tone = raw (ParamID::tone)->load();
    const auto ambience = raw (ParamID::ambience)->load(), volume = raw (ParamID::volume)->load();
    lead.setSound (drive, tone, ambience, volume);
    secondGuitar.setSound (drive, juce::jlimit (0.0f, 1.0f, tone - 0.08f), ambience, volume);

    queue.clear();

    // Notes played into the plugin: a dragged clip, or a keyboard.
    const auto bendRange = (float) kBendRanges[juce::jlimit (0, 2, juce::roundToInt (raw (ParamID::bendRange)->load()))];

    for (const auto meta : midi)
    {
        const auto message = meta.getMessage();
        Scheduled s;
        s.offset = juce::jlimit (0, numSamples - 1, meta.samplePosition);
        s.fromMidi = true;

        if (message.isNoteOn())
        {
            s.type = Event::noteOn;
            s.pitch = message.getNoteNumber();
            s.velocity = message.getVelocity();
        }
        else if (message.isNoteOff())
        {
            s.type = Event::noteOff;
            s.pitch = message.getNoteNumber();
        }
        else if (message.isPitchWheel())
        {
            s.type = Event::bend;
            s.semitones = (float) (message.getPitchWheelValue() - 8192) / 8192.0f * bendRange;
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            s.type = kAllNotesOff;
        }
        else
        {
            continue;
        }

        push (s);
    }

    midi.clear();

    // The generated solo, looped in time with the host (or on our own clock while the host is stopped).
    const auto* bundle = active.load (std::memory_order_acquire);
    const auto synced = hostIsPlaying && hasPosition;
    const auto wantsToPlay = bundle != nullptr && bundle->solo.lengthBeats > 0.0
                          && (synced ? (followHost.load() || audition.load()) : audition.load());
    const auto beatsPerSample = bpm / (60.0 * sampleRate);

    auto stopBoth = [this] (int offset)
    {
        for (int target = 0; target < 2; ++target)
        {
            Scheduled s;
            s.offset = offset;
            s.target = target;
            s.type = kAllNotesOff;
            push (s);
        }
    };

    if (wantsToPlay)
    {
        const auto length = bundle->solo.lengthBeats;
        auto position = playPosition;
        auto jump = ! wasPlaying || bundle != playing || synced != wasSynced;
        const auto restartNow = restart.exchange (false);

        if (synced)
        {
            position = std::fmod (ppq, length);

            if (position < 0.0)
                position += length;

            const auto difference = std::abs (position - playPosition);

            if (std::min (difference, length - difference) > 0.02)
                jump = true;
        }
        else if (restartNow || ! wasPlaying || wasSynced)
        {
            position = 0.0;
            jump = true;
        }

        position = juce::jlimit (0.0, std::max (0.0, length - 1.0e-9), position);

        if (jump)
        {
            stopBoth (0);
            leadIndex = firstEventAt (bundle->events, position);
            secondIndex = firstEventAt (bundle->secondEvents, position);
        }

        playing = bundle;
        uiPosition = position;

        int sample = 0;

        while (sample < numSamples)
        {
            const auto untilEnd = std::ceil ((length - position) / beatsPerSample);
            const auto count = (int) std::min ((double) (numSamples - sample), std::max (1.0, untilEnd));
            const auto to = position + count * beatsPerSample;

            collectSolo (bundle->events, leadIndex, 0, position, to, beatsPerSample, sample, count);

            if (bundle->harmony != 0)
                collectSolo (bundle->secondEvents, secondIndex, 1, position, to, beatsPerSample, sample, count);

            sample += count;
            position = to;

            if (position >= length - 1.0e-9)
            {
                position = std::max (0.0, position - length);
                leadIndex = secondIndex = 0;
                stopBoth (std::min (sample, numSamples - 1));
            }
        }

        playPosition = position;
    }
    else
    {
        restart = false;

        if (wasPlaying)
            stopBoth (0);

        uiPosition = -1.0;
    }

    wasPlaying = wantsToPlay;
    wasSynced = wantsToPlay && synced;

    std::sort (queue.begin(), queue.end(), [] (const Scheduled& a, const Scheduled& b)
    {
        return a.offset != b.offset ? a.offset < b.offset : a.order < b.order;
    });

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    int cursor = 0;

    for (const auto& event : queue)
    {
        if (event.offset > cursor)
        {
            renderSegment (left + cursor, right + cursor, event.offset - cursor);
            cursor = event.offset;
        }

        apply (event);
    }

    renderSegment (left + cursor, right + cursor, numSamples - cursor);

    for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    secondTail = std::max (0, secondTail - numSamples);
    sampleClock += numSamples;
}

juce::AudioProcessorEditor* SolistaProcessor::createEditor()
{
    return new SolistaEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SolistaProcessor();
}
