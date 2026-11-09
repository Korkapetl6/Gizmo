#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

using namespace gizmo;

namespace
{
constexpr int kStateMagic = 0x477a6d6f; // "Gzmo"
constexpr int kStateVersion = 2;

juce::String percentText (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

juce::String signedPercentText (float v)
{
    const auto p = juce::roundToInt (v * 100.0f);
    return (p > 0 ? "+" : "") + juce::String (p) + " %";
}

juce::String timeText (float seconds)
{
    if (seconds < 1.0f)
        return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";

    return juce::String (seconds, 2) + " s";
}

juce::String panText (float v)
{
    const auto p = juce::roundToInt (v * 100.0f);

    if (p == 0)
        return "C";

    return p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p);
}

juce::String hzText (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 2) + " kHz";

    return juce::String (hz, hz < 10.0f ? 2 : (hz < 100.0f ? 1 : 0)) + " Hz";
}

float parseNumber (const juce::String& t)
{
    return t.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue();
}

float parsePercent (const juce::String& t) { return parseNumber (t) / 100.0f; }
float parseTime (const juce::String& t) { return t.containsIgnoreCase ("ms") ? parseNumber (t) / 1000.0f : parseNumber (t); }
float parseHz (const juce::String& t) { return t.containsIgnoreCase ("k") ? parseNumber (t) * 1000.0f : parseNumber (t); }

float parsePan (const juce::String& t)
{
    const auto v = std::abs (parseNumber (t)) / 100.0f;

    if (t.trim().startsWithIgnoreCase ("L"))
        return -v;

    return t.trim().startsWithIgnoreCase ("R") ? v : parseNumber (t) / 100.0f;
}

juce::NormalisableRange<float> skewedRange (float min, float max, float centre)
{
    juce::NormalisableRange<float> r (min, max);
    r.setSkewForCentre (centre);
    return r;
}

/** Reads exactly 'size' bytes, or returns false. */
bool readFully (juce::InputStream& in, void* dest, int size)
{
    auto* p = static_cast<char*> (dest);

    while (size > 0)
    {
        const auto n = in.read (p, size);

        if (n <= 0)
            return false;

        p += n;
        size -= n;
    }

    return true;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout GizmoProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;

    auto addFloat = [&ps] (const char* id, const char* name, NormalisableRange<float> range, float def,
                           std::function<String (float)> toText, std::function<float (const String&)> fromText)
    {
        ps.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, pl (name), range, def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                .withValueFromStringFunction (fromText)));
    };

    auto addChoice = [&ps] (const char* id, const char* name, std::initializer_list<const char*> options, int def)
    {
        StringArray choices;

        for (auto* o : options)
            choices.add (pl (o));

        ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, pl (name), choices, def));
    };

    auto addInt = [&ps] (const char* id, const char* name, int min, int max, int def, bool showSign)
    {
        ps.push_back (std::make_unique<AudioParameterInt> (
            ParameterID { id, 1 }, pl (name), min, max, def,
            AudioParameterIntAttributes().withStringFromValueFunction ([showSign] (int v, int)
            {
                return (showSign && v > 0 ? "+" : "") + String (v);
            })));
    };

    const NormalisableRange<float> unit (0.0f, 1.0f), bipolar (-1.0f, 1.0f);

    // The most useful ones first, so they are easy to find in Ableton.
    addFloat (ParamID::position, "Pozycja", unit, 0.0f, percentText, parsePercent);
    addFloat (ParamID::cutoff, "Cutoff", skewedRange (20.0f, 20000.0f, 632.0f), 20000.0f, hzText, parseHz);
    addFloat (ParamID::resonance, "Rezonans", unit, 0.0f, percentText, parsePercent);
    addFloat (ParamID::detune, "Rozstrojenie", skewedRange (0.0f, 50.0f, 12.0f), 8.0f,
              [] (float v) { return String (v, 1) + " ct"; }, parseNumber);
    addFloat (ParamID::lfoRate, "LFO szybkość", skewedRange (0.01f, 40.0f, 1.5f), 1.0f, hzText, parseHz);
    addFloat (ParamID::lfoToPos, "LFO na pozycję", bipolar, 0.0f, signedPercentText, parsePercent);
    addFloat (ParamID::lfoToCutoff, "LFO na cutoff", bipolar, 0.0f, signedPercentText, parsePercent);
    addFloat (ParamID::lfoToPitch, "LFO na wysokość", unit, 0.0f, percentText, parsePercent);

    addChoice (ParamID::frameMode, "Przejścia", { "Płynnie", "Skoki" }, 0);
    addFloat (ParamID::velToPos, "Velocity na pozycję", bipolar, 0.0f, signedPercentText, parsePercent);
    addFloat (ParamID::wheelToPos, "Kółko na pozycję", bipolar, 0.0f, signedPercentText, parsePercent);

    addFloat (ParamID::levelA, "Poziom 1", unit, 0.8f, percentText, parsePercent);
    addFloat (ParamID::panA, "Pan 1", bipolar, -0.2f, panText, parsePan);
    addFloat (ParamID::levelB, "Poziom 2", unit, 0.6f, percentText, parsePercent);
    addFloat (ParamID::panB, "Pan 2", bipolar, 0.2f, panText, parsePan);
    addInt (ParamID::semiB, "Transpozycja 2", -24, 24, 0, true);
    addFloat (ParamID::offsetB, "Przesunięcie 2", bipolar, 0.0f, signedPercentText, parsePercent);

    addFloat (ParamID::attack, "Attack", skewedRange (0.001f, 10.0f, 0.3f), 0.005f, timeText, parseTime);
    addFloat (ParamID::decay, "Decay", skewedRange (0.001f, 20.0f, 0.6f), 0.6f, timeText, parseTime);
    addFloat (ParamID::sustain, "Sustain", unit, 0.8f, percentText, parsePercent);
    addFloat (ParamID::release, "Release", skewedRange (0.001f, 20.0f, 0.6f), 0.5f, timeText, parseTime);
    addFloat (ParamID::velToAmp, "Velocity na głośność", unit, 0.5f, percentText, parsePercent);

    addChoice (ParamID::filterOn, "Filtr", { "Wył.", "Wł." }, 1);
    addFloat (ParamID::keyTrack, "Śledzenie klawiatury", unit, 0.0f, percentText, parsePercent);
    addFloat (ParamID::filterEnv, "Obwiednia filtra", bipolar, 0.0f, signedPercentText, parsePercent);
    addFloat (ParamID::fAttack, "Filtr attack", skewedRange (0.001f, 10.0f, 0.3f), 0.005f, timeText, parseTime);
    addFloat (ParamID::fDecay, "Filtr decay", skewedRange (0.001f, 20.0f, 0.6f), 0.4f, timeText, parseTime);
    addFloat (ParamID::fSustain, "Filtr sustain", unit, 0.0f, percentText, parsePercent);
    addFloat (ParamID::fRelease, "Filtr release", skewedRange (0.001f, 20.0f, 0.6f), 0.4f, timeText, parseTime);

    addChoice (ParamID::lfoShape, "LFO kształt", { "Sinus", "Trójkąt", "Piła", "Kwadrat", "Losowy" }, 0);
    addChoice (ParamID::lfoSync, "LFO tryb", { "Hz", "Tempo" }, 0);
    {
        StringArray divisions;

        for (auto* d : kLfoDivisionNames)
            divisions.add (d);

        ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::lfoDivision, 1 },
                                                              pl ("LFO podział"), divisions, kDefaultLfoDivision));
    }
    addChoice (ParamID::lfoRetrig, "LFO start", { "Wspólne", "Od nuty" }, 0);

    addChoice (ParamID::seqOn, "Ciąg A/B", { "Wył.", "Wł." }, 0);
    addInt (ParamID::seqLength, "Ciąg długość", 1, kMaxSteps, 8, false);
    {
        StringArray rates;

        for (auto* r : kSeqRateNames)
            rates.add (r);

        ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::seqRate, 1 }, pl ("Ciąg krok"),
                                                              rates, kDefaultSeqRate));
    }
    addChoice (ParamID::seqStart, "Ciąg start", { "Wspólne", "Od nuty" }, 0);
    addFloat (ParamID::fragOffset, "Fragment przesunięcie", bipolar, 0.0f, signedPercentText, parsePercent);
    addChoice (ParamID::fragDirection, "Fragment kierunek", { "Przód", "Tył", "Przód-tył" }, 0);
    addChoice (ParamID::fragPitch, "Fragment strój", { "Klawisze", "Oryginał" }, 0);
    addInt (ParamID::fragTranspose, "Fragment transpozycja", -24, 24, 0, true);
    addFloat (ParamID::fragLevel, "Fragment poziom", unit, 0.8f, percentText, parsePercent);

    addChoice (ParamID::voiceMode, "Głosy", { "Poly", "Mono" }, 0);
    addFloat (ParamID::glide, "Glide", skewedRange (0.0f, 5.0f, 0.3f), 0.0f,
              [] (float v) { return v < 0.0005f ? pl ("wył.") : timeText (v); }, parseTime);
    addInt (ParamID::bendRange, "Zakres bendu", 0, 24, 2, false);
    addChoice (ParamID::character, "Brzmienie", { "Czysto", "Surowo" }, 0);
    addFloat (ParamID::gain, "Głośność", NormalisableRange<float> (-48.0f, 6.0f), -6.0f,
              [] (float v) { return String (v, 1) + " dB"; }, parseNumber);

    return { ps.begin(), ps.end() };
}

GizmoProcessor::GizmoProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GizmoState", createLayout())
{
    formats.registerBasicFormats();

    auto get = [this] (const char* id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    params.position = get (ParamID::position);
    params.frameMode = get (ParamID::frameMode);
    params.velToPos = get (ParamID::velToPos);
    params.wheelToPos = get (ParamID::wheelToPos);
    params.levelA = get (ParamID::levelA);
    params.panA = get (ParamID::panA);
    params.levelB = get (ParamID::levelB);
    params.panB = get (ParamID::panB);
    params.semiB = get (ParamID::semiB);
    params.offsetB = get (ParamID::offsetB);
    params.detune = get (ParamID::detune);
    params.attack = get (ParamID::attack);
    params.decay = get (ParamID::decay);
    params.sustain = get (ParamID::sustain);
    params.release = get (ParamID::release);
    params.velToAmp = get (ParamID::velToAmp);
    params.voiceMode = get (ParamID::voiceMode);
    params.glide = get (ParamID::glide);
    params.bendRange = get (ParamID::bendRange);
    params.character = get (ParamID::character);
    params.gain = get (ParamID::gain);
    params.filterOn = get (ParamID::filterOn);
    params.cutoff = get (ParamID::cutoff);
    params.resonance = get (ParamID::resonance);
    params.keyTrack = get (ParamID::keyTrack);
    params.filterEnv = get (ParamID::filterEnv);
    params.fAttack = get (ParamID::fAttack);
    params.fDecay = get (ParamID::fDecay);
    params.fSustain = get (ParamID::fSustain);
    params.fRelease = get (ParamID::fRelease);
    params.lfoShape = get (ParamID::lfoShape);
    params.lfoRate = get (ParamID::lfoRate);
    params.lfoSync = get (ParamID::lfoSync);
    params.lfoDivision = get (ParamID::lfoDivision);
    params.lfoRetrig = get (ParamID::lfoRetrig);
    params.lfoToPos = get (ParamID::lfoToPos);
    params.lfoToCutoff = get (ParamID::lfoToCutoff);
    params.lfoToPitch = get (ParamID::lfoToPitch);
    params.seqOn = get (ParamID::seqOn);
    params.seqLength = get (ParamID::seqLength);
    params.seqRate = get (ParamID::seqRate);
    params.seqStart = get (ParamID::seqStart);
    params.fragOffset = get (ParamID::fragOffset);
    params.fragDirection = get (ParamID::fragDirection);
    params.fragPitch = get (ParamID::fragPitch);
    params.fragTranspose = get (ParamID::fragTranspose);
    params.fragLevel = get (ParamID::fragLevel);
    apvts.state.setProperty ("pattern", (int) kDefaultPattern, nullptr);

    owned[slotA] = makeDefaultWavetable();
    live[slotA].store (owned[slotA].get());
    live[slotB].store (nullptr);
}

GizmoProcessor::~GizmoProcessor()
{
    loader.removeAllJobs (true, 10000);
}

void GizmoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.getLatencySamples());
}

bool GizmoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

EngineParams GizmoProcessor::readParams() const
{
    auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto on = [&v] (const std::atomic<float>* a) { return v (a) > 0.5f; };
    auto whole = [&v] (const std::atomic<float>* a) { return juce::roundToInt (v (a)); };

    EngineParams p;
    p.position = v (params.position);
    p.steppedFrames = on (params.frameMode);
    p.velToPos = v (params.velToPos);
    p.wheelToPos = v (params.wheelToPos);
    p.levelA = v (params.levelA);
    p.panA = v (params.panA);
    p.levelB = v (params.levelB);
    p.panB = v (params.panB);
    p.semiB = whole (params.semiB);
    p.offsetB = v (params.offsetB);
    p.detuneCents = v (params.detune);
    p.attack = v (params.attack);
    p.decay = v (params.decay);
    p.sustain = v (params.sustain);
    p.release = v (params.release);
    p.velToAmp = v (params.velToAmp);
    p.mono = on (params.voiceMode);
    p.glide = v (params.glide);
    p.bendRange = whole (params.bendRange);
    p.raw = on (params.character);
    p.gainDb = v (params.gain);
    p.filterOn = on (params.filterOn);
    p.cutoff = v (params.cutoff);
    p.resonance = v (params.resonance);
    p.keyTrack = v (params.keyTrack);
    p.filterEnv = v (params.filterEnv);
    p.fAttack = v (params.fAttack);
    p.fDecay = v (params.fDecay);
    p.fSustain = v (params.fSustain);
    p.fRelease = v (params.fRelease);
    p.lfoShape = whole (params.lfoShape);
    p.lfoRate = v (params.lfoRate);
    p.lfoSync = on (params.lfoSync);
    p.lfoBeats = kLfoDivisionBeats[juce::jlimit (0, kNumLfoDivisions - 1, whole (params.lfoDivision))];
    p.lfoRetrig = on (params.lfoRetrig);
    p.lfoToPos = v (params.lfoToPos);
    p.lfoToCutoff = v (params.lfoToCutoff);
    p.lfoToPitch = v (params.lfoToPitch);
    p.seqOn = on (params.seqOn);
    p.seqLength = whole (params.seqLength);
    p.seqPattern = pattern.load (std::memory_order_relaxed);
    p.seqStepBeats = kSeqRateBeats[juce::jlimit (0, kNumSeqRates - 1, whole (params.seqRate))];
    p.seqPerNote = on (params.seqStart);
    p.fragOffset = v (params.fragOffset);
    p.fragDirection = whole (params.fragDirection);
    p.fragFollowKeys = ! on (params.fragPitch);
    p.fragTranspose = whole (params.fragTranspose);
    p.fragLevel = v (params.fragLevel);
    return p;
}

void GizmoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    auto p = readParams();

    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                p.bpm = *bpm;

            if (auto ppq = position->getPpqPosition())
            {
                p.ppqAtBlockStart = *ppq;
                p.transportKnown = position->getIsPlaying();
            }
        }
    }

    engine.process (buffer, midi, p, live[slotA].load (std::memory_order_acquire),
                    live[slotB].load (std::memory_order_acquire));
}

juce::AudioProcessorEditor* GizmoProcessor::createEditor()
{
    return new GizmoEditor (*this);
}

//==============================================================================
void GizmoProcessor::replaceTable (Slot slot, std::unique_ptr<Wavetable> table)
{
    {
        const std::lock_guard<std::mutex> lock (tableLock);
        auto old = std::move (owned[(size_t) slot]);
        owned[(size_t) slot] = std::move (table);
        live[(size_t) slot].store (owned[(size_t) slot].get(), std::memory_order_release);

        if (old != nullptr)
            retire (std::move (old));
    }

    sendChangeMessage();
}

void GizmoProcessor::retire (std::unique_ptr<Wavetable> table)
{
    // The audio thread may still be reading the old table during the current block,
    // so it is kept alive for a couple of seconds before it is freed.
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [now] (const auto& r) { return now - r.first > 2000; }),
                   retired.end());
    retired.emplace_back (now, std::move (table));
}

void GizmoProcessor::loadFile (Slot slot, const juce::File& file)
{
    const auto token = ++loadTokens[(size_t) slot];
    ++loading[(size_t) slot];
    errors[(size_t) slot].clear();
    sendChangeMessage();

    const auto mode = importMode;
    juce::WeakReference<GizmoProcessor> weak (this);

    loader.addJob ([this, weak, slot, file, mode, token]
    {
        auto result = std::make_shared<ImportResult> (loadWavetableFromFile (file, mode, formats));

        juce::MessageManager::callAsync ([weak, slot, token, result]
        {
            auto* self = weak.get();

            if (self == nullptr)
                return;

            --self->loading[(size_t) slot];

            if (token != self->loadTokens[(size_t) slot])
            {
                self->sendChangeMessage();
                return;
            }

            if (result->table != nullptr)
            {
                self->replaceTable (slot, std::move (result->table));
            }
            else
            {
                self->errors[(size_t) slot] = result->error;
                self->sendChangeMessage();
            }
        });
    });
}

void GizmoProcessor::clearSlotB()
{
    ++loadTokens[slotB];
    errors[slotB].clear();
    replaceTable (slotB, nullptr);
}

void GizmoProcessor::setImportMode (ImportMode mode)
{
    if (mode == importMode)
        return;

    importMode = mode;

    for (auto slot : { slotA, slotB })
    {
        if (const auto* table = getTable (slot))
        {
            const juce::File source (table->sourcePath);

            if (table->sourcePath.isNotEmpty() && source.existsAsFile())
                loadFile (slot, source);
        }
    }

    sendChangeMessage();
}

void GizmoProcessor::setPattern (juce::uint32 bits)
{
    bits &= (1u << kMaxSteps) - 1u;
    pattern.store (bits);
    apvts.state.setProperty ("pattern", (int) bits, nullptr);
    sendChangeMessage();
}

bool GizmoProcessor::isSupportedFile (const juce::File& file) const
{
    return file.existsAsFile() && formats.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

//==============================================================================
void GizmoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream raw (destData, false);

    {
        juce::GZIPCompressorOutputStream out (raw, 6);
        out.writeInt (kStateMagic);
        out.writeInt (kStateVersion);

        auto state = apvts.copyState();
        state.setProperty ("importMode", (int) importMode, nullptr);

        if (auto xml = state.createXml())
            out.writeString (xml->toString());
        else
            out.writeString ({});

        const std::lock_guard<std::mutex> lock (tableLock);

        for (const auto& table : owned)
        {
            if (table == nullptr || table->isDefault)
            {
                out.writeByte (0);
                continue;
            }

            out.writeByte (1);
            out.writeString (table->name);
            out.writeString (table->sourcePath);
            out.writeInt (table->pitchedFrames);

            std::vector<char> bytes (table->frames.size() * 2);

            for (size_t i = 0; i < table->frames.size(); ++i)
            {
                const auto s = (juce::int16) juce::jlimit (-32767, 32767, juce::roundToInt (table->frames[i] * 32767.0f));
                const auto u = (juce::uint16) s;
                bytes[i * 2] = (char) (u & 0xff);
                bytes[i * 2 + 1] = (char) (u >> 8);
            }

            out.write (bytes.data(), bytes.size());

            // Version 2: the sample itself, for the B steps
            out.writeDouble (table->sourceRate);
            out.writeFloat (table->rootNote);
            out.writeInt ((int) table->source.size());
            std::vector<char> sourceBytes (table->source.size() * 2);

            for (size_t i = 0; i < table->source.size(); ++i)
            {
                const auto u = (juce::uint16) (juce::int16) juce::jlimit (-32767, 32767, juce::roundToInt (table->source[i] * 32767.0f));
                sourceBytes[i * 2] = (char) (u & 0xff);
                sourceBytes[i * 2 + 1] = (char) (u >> 8);
            }

            out.write (sourceBytes.data(), sourceBytes.size());
        }

        out.flush();
    }
}

void GizmoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream raw (data, (size_t) sizeInBytes, false);
    juce::GZIPDecompressorInputStream in (raw);

    if (in.readInt() != kStateMagic)
        return;

    const auto version = in.readInt();

    if (version < 1)
        return;

    if (auto xml = juce::parseXML (in.readString()))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            importMode = (int) tree.getProperty ("importMode", 0) == 1 ? ImportMode::chunks : ImportMode::cycles;
            pattern.store ((juce::uint32) (int) tree.getProperty ("pattern", (int) kDefaultPattern) & ((1u << kMaxSteps) - 1u));
            apvts.replaceState (tree);
        }
    }

    for (auto slot : { slotA, slotB })
    {
        if (in.readByte() != 1)
        {
            replaceTable (slot, slot == slotA ? makeDefaultWavetable() : nullptr);
            continue;
        }

        const auto name = in.readString();
        const auto path = in.readString();
        const auto pitched = in.readInt();

        std::vector<char> bytes ((size_t) kNumFrames * kFrameSize * 2);

        if (! readFully (in, bytes.data(), (int) bytes.size()))
            break;

        std::vector<float> frames ((size_t) kNumFrames * kFrameSize);

        for (size_t i = 0; i < frames.size(); ++i)
        {
            const auto u = (juce::uint16) ((juce::uint8) bytes[i * 2] | ((juce::uint16) (juce::uint8) bytes[i * 2 + 1] << 8));
            frames[i] = (float) (juce::int16) u / 32767.0f;
        }

        auto table = buildWavetableFromFrames (frames.data());
        table->name = name;
        table->sourcePath = path;
        table->pitchedFrames = pitched;

        if (version >= 2)
        {
            table->sourceRate = in.readDouble();
            table->rootNote = in.readFloat();
            const auto count = in.readInt();

            if (count > 0 && count <= (int) (kMaxImportSeconds * 400000.0))
            {
                std::vector<char> sourceBytes ((size_t) count * 2);

                if (readFully (in, sourceBytes.data(), (int) sourceBytes.size()))
                {
                    table->source.resize ((size_t) count);

                    for (size_t i = 0; i < table->source.size(); ++i)
                    {
                        const auto u = (juce::uint16) ((juce::uint8) sourceBytes[i * 2]
                                                       | ((juce::uint16) (juce::uint8) sourceBytes[i * 2 + 1] << 8));
                        table->source[i] = (float) (juce::int16) u / 32767.0f;
                    }
                }
            }
        }

        replaceTable (slot, std::move (table));
    }

    sendChangeMessage();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GizmoProcessor();
}
