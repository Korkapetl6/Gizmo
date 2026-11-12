#include "Dedal2Editor.h"
#include "Dedal2Params.h"
#include "ui/Theme.h"

using namespace dedal2;
namespace theme = gizmo::theme;

namespace
{
const juce::Colour colourSeq[2] = { theme::accentFilter, theme::accentB };
const juce::Colour cableColours[] = { juce::Colour (0xffff6b6b), juce::Colour (0xffffc04d), juce::Colour (0xff6bd68f), juce::Colour (0xff5ab4ff),
                                      juce::Colour (0xffc58bff), juce::Colour (0xffff8fd0) };

class Btn : public juce::Button
{
public:
    Btn (const juce::String& label, juce::Colour accentColour, bool isFilled = false) : juce::Button (label), accent (accentColour), filled (isFilled)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto lit = filled || getToggleState();
        auto fill = lit ? accent : theme::knobBody.brighter (highlighted ? 0.15f : 0.0f);

        if (down)
            fill = fill.darker (0.25f);

        g.setColour (fill.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle (bounds, 6.0f);

        if (! lit)
        {
            g.setColour (theme::panelEdge.brighter (0.2f));
            g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
        }

        g.setColour (lit ? theme::background : theme::text);
        g.setFont (theme::font (11.5f, true));
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
    }

    juce::Colour accent;
    bool filled;
};

/** Eight bits with the play head (bar on top) and the write head (red frame). */
class Bits : public juce::Component
{
public:
    Bits (Dedal2Processor& p, int rowIndex) : processor (p), row (rowIndex) {}

    void set (const Snapshot& s) { snapshot = s; repaint(); }

    juce::Rectangle<float> cell (int i) const
    {
        const auto w = ((float) getWidth() - 6.0f * 7.0f) / 8.0f;
        return { (float) i * (w + 6.0f), 8.0f, w, (float) getHeight() - 8.0f };
    }

    void paint (juce::Graphics& g) override
    {
        const auto& layout = snapshot.layout;

        for (int i = 0; i < kSteps; ++i)
        {
            const auto r = cell (i);
            const auto inside = i < layout.length[row];
            const auto on = snapshot.state.bits[(size_t) (row * kSteps + i)];
            const auto v = snapshot.state.values[(size_t) (row * kSteps + i)];
            g.setColour (theme::knobBody.withMultipliedAlpha (inside ? 1.0f : 0.4f));
            g.fillRoundedRectangle (r, 6.0f);

            if (on)
            {
                const auto alpha = inside ? 1.0f : 0.3f;
                g.setColour (colourSeq[row].withAlpha (0.22f * alpha));
                g.fillRoundedRectangle (r.reduced (2.0f), 5.0f);
                const auto mid = r.getCentreY(), h = v * (r.getHeight() * 0.5f - 5.0f);
                g.setColour (colourSeq[row].withMultipliedAlpha (alpha));
                g.fillRoundedRectangle ({ r.getX() + 5.0f, std::min (mid, mid - h), r.getWidth() - 10.0f, std::max (2.0f, std::abs (h)) }, 3.0f);
            }

            g.setColour (theme::panelEdge.brighter (0.3f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        }

        for (int k = 0; k < 2; ++k)
        {
            if (! layout.chain && k != row)
                continue;

            const auto play = layout.slot (k, snapshot.head[(size_t) k]), write = layout.slot (k, snapshot.writeHead[(size_t) k]);

            if (play / kSteps == row)
            {
                const auto r = cell (play % kSteps);
                g.setColour (colourSeq[k].brighter (0.4f));
                g.fillRoundedRectangle (r.getX() + 4.0f, 0.0f, r.getWidth() - 8.0f, 4.0f, 2.0f);
            }

            if (write / kSteps == row && k == row)
            {
                g.setColour (juce::Colour (0xffff5a5a));
                g.drawRoundedRectangle (cell (write % kSteps).reduced (1.0f), 6.0f, 2.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < kSteps; ++i)
            if (cell (i).expanded (3.0f, 8.0f).contains (e.position))
                processor.toggleSlot (row * kSteps + i);
    }

private:
    Dedal2Processor& processor;
    int row;
    Snapshot snapshot;
};

//==============================================================================
/** The patch bay: drag from one jack to another to lay a cable, click a patched input to pull it out. */
class PatchBay : public juce::Component
{
public:
    explicit PatchBay (Dedal2Processor& p) : processor (p)
    {
        // Panel order: the twenty inputs first (MIDI is not a patch point here), then the twelve outputs.
        for (int i = 0; i < 17; ++i)
            jacks.push_back ({ true, i });

        jacks.push_back ({ true, -1 });
        jacks.push_back ({ true, inTrigger });
        jacks.push_back ({ true, inReset });

        for (int i = 0; i < numOuts; ++i)
            jacks.push_back ({ false, i });
    }

    juce::Point<float> centre (int jack) const
    {
        const auto w = (float) getWidth() / 4.0f, h = ((float) getHeight() - 26.0f) / 8.0f;
        return { ((float) (jack % 4) + 0.5f) * w, 26.0f + ((float) (jack / 4) + 0.42f) * h };
    }

    int jackAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) jacks.size(); ++i)
            if (centre (i).getDistanceFrom (p) < 24.0f && jacks[(size_t) i].index >= 0)
                return i;

        return -1;
    }

    int find (bool input, int index) const
    {
        for (int i = 0; i < (int) jacks.size(); ++i)
            if (jacks[(size_t) i].input == input && jacks[(size_t) i].index == index)
                return i;

        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.0f);
        g.setColour (theme::accentLfo);
        g.setFont (theme::font (12.5f, true));
        g.drawText ("PATCH BAY", getLocalBounds().reduced (14, 6).removeFromTop (16), juce::Justification::centredLeft, false);

        const auto cellW = (float) getWidth() / 4.0f;

        for (int i = 0; i < (int) jacks.size(); ++i)
        {
            const auto& j = jacks[(size_t) i];
            const auto c = centre (i);
            const auto label = juce::Rectangle<float> (c.x - cellW * 0.5f + 2.0f, c.y + 16.0f, cellW - 4.0f, 22.0f);
            const auto text = j.index < 0 ? juce::String ("MIDI") : juce::String (j.input ? inName (j.index) : outName (j.index));

            if (! j.input)
            {
                // Outputs: light label, as on the hardware.
                g.setColour (theme::text.withAlpha (0.88f));
                g.fillRoundedRectangle (label.reduced (3.0f, 3.0f), 3.0f);
            }

            g.setColour (j.input ? (j.index < 0 ? theme::textDim.withAlpha (0.4f) : theme::text) : theme::background);
            g.setFont (theme::font (9.5f, true));
            g.drawFittedText (text, label.toNearestInt(), juce::Justification::centred, 1, 0.7f);

            const auto level = j.input || j.index < 0 ? 0.0f : juce::jlimit (0.0f, 1.0f, std::abs (processor.jackLevel (j.index)));
            g.setColour (theme::knobBody.brighter (0.25f));
            g.fillEllipse (c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f);
            g.setColour ((j.input ? theme::panelEdge.brighter (0.6f) : theme::accentLfo).withAlpha (j.index < 0 ? 0.25f : 1.0f));
            g.drawEllipse (c.x - 13.0f, c.y - 13.0f, 26.0f, 26.0f, 2.0f);
            g.setColour (theme::background.interpolatedWith (theme::accentLfo, level));
            g.fillEllipse (c.x - 6.5f, c.y - 6.5f, 13.0f, 13.0f);
        }

        auto cable = [&] (juce::Point<float> a, juce::Point<float> b, juce::Colour colour)
        {
            juce::Path path;
            path.startNewSubPath (a);
            const auto sag = 18.0f + a.getDistanceFrom (b) * 0.22f;
            path.cubicTo ({ a.x, a.y + sag }, { b.x, b.y + sag }, b);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.strokePath (path, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour.withAlpha (0.92f));
            g.strokePath (path, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse (a.x - 6.0f, a.y - 6.0f, 12.0f, 12.0f);
            g.fillEllipse (b.x - 6.0f, b.y - 6.0f, 12.0f, 12.0f);
        };

        for (int in = 0; in < numIns; ++in)
            if (const auto out = processor.cableAt (in); out >= 0)
                cable (centre (find (false, out)), centre (find (true, in)), cableColours[(out + in) % 6]);

        if (dragFrom >= 0)
            cable (centre (dragFrom), dragPoint, theme::text);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragFrom = jackAt (e.position);
        dragPoint = e.position;
        moved = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFrom < 0)
            return;

        moved = moved || e.getDistanceFromDragStart() > 6;
        dragPoint = e.position;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragFrom >= 0)
        {
            const auto from = jacks[(size_t) dragFrom];
            const auto target = jackAt (e.position);

            if (! moved)
            {
                if (from.input)
                    processor.disconnect (from.index); // a click pulls the cable out of an input
            }
            else if (target >= 0 && jacks[(size_t) target].input != from.input)
            {
                const auto to = jacks[(size_t) target];
                processor.connect (from.input ? to.index : from.index, from.input ? from.index : to.index);
            }
        }

        dragFrom = -1;
        repaint();
    }

private:
    struct Jack { bool input; int index; };
    Dedal2Processor& processor;
    std::vector<Jack> jacks;
    int dragFrom = -1;
    juce::Point<float> dragPoint;
    bool moved = false;
};
} // namespace

//==============================================================================
class Dedal2Editor::Content : public juce::Component
{
public:
    explicit Content (Dedal2Processor& p) : processor (p), bay (p)
    {
        addAndMakeVisible (bay);

        for (int k = 0; k < 2; ++k)
        {
            bits[k] = std::make_unique<Bits> (p, k);
            addAndMakeVisible (*bits[k]);
            const juce::String n (k + 1);
            const char* names[] = { "BIT FLIP ", "BIT SHIFT ", "LENGTH " };
            const Command commands[] = { Command::bitFlip, Command::bitShift, Command::length };

            for (int i = 0; i < 3; ++i)
            {
                seqButtons[k][i] = std::make_unique<Btn> (names[i] + n, colourSeq[k], i == 0);
                const auto command = commands[i];
                seqButtons[k][i]->onClick = [this, command, k] { processor.command (command, k); };
                addAndMakeVisible (*seqButtons[k][i]);
            }

            const char* extra[] = { "ZAPIS +1", "ZAPIS 0", "SHIFT 0", "LOSUJ" };
            const Command extraCommands[] = { Command::writeAdvance, Command::writeReset, Command::unshift, Command::randomise };

            for (int i = 0; i < 4; ++i)
            {
                extraButtons[k][i] = std::make_unique<Btn> (extra[i], colourSeq[k]);
                const auto command = extraCommands[i];
                extraButtons[k][i]->onClick = [this, command, k] { processor.command (command, k); };
                addAndMakeVisible (*extraButtons[k][i]);
            }

            knob (ID::corrupt[k], "CORRUPT " + n, colourSeq[k]);
            knob (ID::range[k], "CV RANGE " + n, colourSeq[k]);
        }

        knob (ID::vcoFreq, "FREQUENCY", theme::accentA);
        knob (ID::vcoEg1, "EG1 AMT", theme::accentA);
        knob (ID::vcoSeq1, "SEQ1 AMT", theme::accentA);
        knob (ID::modFreq, "FREQUENCY", theme::accentLfo);
        knob (ID::modEg1, "EG1 AMT", theme::accentLfo);
        knob (ID::modSeq2, "SEQ2 AMT", theme::accentLfo);
        knob (ID::fm, "FM AMT", theme::accentLfo);
        knob (ID::lvlVco, "VCO LVL", theme::accentVoice);
        knob (ID::lvlRing, "RING LVL", theme::accentVoice);
        knob (ID::lvlMod, "MOD LVL", theme::accentVoice);
        knob (ID::lvlNoise, "NOISE LVL", theme::accentVoice);
        knob (ID::noiseTone, "TONE", theme::accentVoice);
        knob (ID::fold, "FOLD", theme::accentFragment);
        knob (ID::bias, "BIAS", theme::accentFragment);
        knob (ID::foldEg, "EG1/CV AMT", theme::accentFragment);
        knob (ID::foldSeq, "SEQ1 AMT", theme::accentFragment);
        knob (ID::cutoff, "CUTOFF", theme::accentFilter);
        knob (ID::resonance, "RESONANCE", theme::accentFilter);
        knob (ID::cutEg, "EG1/CV AMT", theme::accentFilter);
        knob (ID::cutSeq, "SEQ2 AMT", theme::accentFilter);
        knob (ID::mode, "MODE", theme::accentFilter);
        knob (ID::eg1Decay, "EG1 DECAY", theme::accentB);
        knob (ID::eg2Decay, "EG2 DECAY", theme::accentB);
        knob (ID::trigMix, "TRIG MIX", theme::accentB);
        knob (ID::blend, "BLEND", theme::accentA);
        knob (ID::volume, "VOLUME", theme::accentA);
        knob (ID::gate, pl ("Długość nut"), theme::accentLfo);
        combo (ID::order);
        segmented (ID::midiSeq, theme::accentLfo);
        combo (ID::tempo);
        combo (ID::scale);
        toggle (ID::chain, "CHAIN SEQ", theme::accentA);
        toggle (ID::midiSend, pl ("Wysyłaj MIDI"), theme::accentLfo);
        toggle (ID::sound, pl ("Własne brzmienie"), theme::accentVoice);

        for (auto* b : { &runButton, &resetButton, &advanceButton, &bufferSave, &bufferRecall, &triggerButton, &prevPreset, &nextPreset,
                         &savePreset, &clearCables, &saveMidi })
            addAndMakeVisible (b);

        runButton.onClick = [this] { processor.setRunning (! processor.isRunningInternally()); };
        resetButton.onClick = [this] { processor.command (Command::reset, 0); };
        advanceButton.onClick = [this] { processor.command (Command::advance, 0); };
        bufferSave.onClick = [this] { processor.saveBuffer(); };
        bufferRecall.onClick = [this] { processor.recallBuffer(); };
        clearCables.onClick = [this] { processor.clearCables(); };
        triggerButton.onClick = [this]
        {
            if (auto* t = processor.apvts.getParameter (ID::trigger))
            {
                t->setValueNotifyingHost (1.0f);
                juce::Timer::callAfterDelay (60, [t] { t->setValueNotifyingHost (0.0f); });
            }
        };

        addAndMakeVisible (presetBox);
        presetBox.onChange = [this]
        {
            const auto id = presetBox.getSelectedId();

            if (id >= 1 && id < 1000)
                processor.loadFactoryPreset (id - 1);
            else if (id >= 1000 && id - 1000 < userFiles.size())
                processor.loadPresetFile (userFiles[id - 1000]);
        };
        prevPreset.onClick = [this] { stepPreset (-1); };
        nextPreset.onClick = [this] { stepPreset (1); };
        savePreset.onClick = [this] { askName(); };

        addAndMakeVisible (dragBars);
        dragBars.setSelected (2);
        dragBars.onChange = [this] (int i) { static const int bars[] = { 1, 2, 4, 8, 16 }; processor.dragBars = bars[i]; };
        addAndMakeVisible (dragHandle);
        refreshPresets();
    }

    void resized() override
    {
        prevPreset.setBounds (150, 14, 30, 30);
        presetBox.setBounds (184, 14, 300, 30);
        nextPreset.setBounds (488, 14, 30, 30);
        savePreset.setBounds (524, 14, 84, 30);
        runButton.setBounds (624, 14, 110, 30);
        bay.setBounds (968, 64, 296, 672);

        // Seven modules side by side, each a strip of its own; rows of knobs run down the strip.
        static const juce::StringArray big { ID::vcoFreq, ID::modFreq, ID::fold, ID::cutoff, ID::eg1Decay, ID::eg2Decay, ID::blend, ID::volume };

        auto strip = [this] (int index, std::initializer_list<std::initializer_list<const char*>> rows)
        {
            const auto x = 16 + index * 136;
            auto y = 88;

            for (const auto& row : rows)
            {
                const auto isBig = big.contains (*row.begin());
                const auto w = isBig ? 104 : 60, h = isBig ? 106 : 84;
                auto cx = x + (128 - w * (int) row.size()) / 2;

                for (const auto* id : row)
                {
                    place (id, cx, y, w, h);

                    if (auto* k = dynamic_cast<gizmo::Knob*> (find (id)))
                        k->slider.getProperties().set ("light", isBig);

                    cx += w;
                }

                y += h + 2;
            }

            return y;
        };

        strip (0, { { ID::vcoFreq }, { ID::vcoEg1, ID::vcoSeq1 } });
        strip (1, { { ID::modFreq }, { ID::modEg1, ID::modSeq2 }, { ID::fm } });
        strip (2, { { ID::lvlVco, ID::lvlRing }, { ID::lvlMod, ID::lvlNoise }, { ID::noiseTone } });
        strip (3, { { ID::fold }, { ID::bias, ID::foldEg }, { ID::foldSeq } });
        strip (4, { { ID::cutoff }, { ID::resonance, ID::mode }, { ID::cutEg, ID::cutSeq } });
        const auto egEnd = strip (5, { { ID::eg1Decay }, { ID::eg2Decay }, { ID::trigMix } });
        triggerButton.setBounds (16 + 5 * 136 + 10, egEnd + 2, 108, 22);
        const auto outEnd = strip (6, { { ID::blend }, { ID::volume } });
        place (ID::order, 16 + 6 * 136 + 8, outEnd + 18, 112, 26);
        place (ID::sound, 16 + 6 * 136 + 8, outEnd + 52, 112, 26);

        // Below them, the sequencer as one wide module
        for (int k = 0; k < 2; ++k)
        {
            const auto y = 474 + k * 64;
            bits[k]->setBounds (84, y, 262, 52);

            for (int i = 0; i < 3; ++i)
                seqButtons[k][i]->setBounds (358 + i * 110, y + 2, 106, 24);

            for (int i = 0; i < 4; ++i)
                extraButtons[k][i]->setBounds (358 + i * 82, y + 30, 80, 20);

            place (ID::corrupt[k], 700 + k * 124, 462, 60, 84);
            place (ID::range[k], 760 + k * 124, 462, 60, 84);
        }

        int x = 84;
        auto next = [&x] (juce::Component& c, int w) { c.setBounds (x, 606, w, 26); x += w + 6; };
        next (resetButton, 70);
        next (advanceButton, 84);
        if (auto* c = find (ID::chain)) next (*c, 96);
        next (bufferSave, 108);
        next (bufferRecall, 108);
        place (ID::tempo, x, 606, 96, 26);
        place (ID::scale, x + 102, 606, 948 - (x + 102), 26);

        // MIDI strip
        place (ID::midiSend, 28, 678, 130, 28);
        place (ID::midiSeq, 168, 678, 200, 28);
        place (ID::gate, 376, 654, 72, 80);
        dragBars.setBounds (460, 678, 160, 28);
        dragHandle.setBounds (632, 670, 170, 44);
        saveMidi.setBounds (810, 678, 138, 28);
        clearCables.setBounds (1150, 68, 104, 20);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::background);
        g.setColour (theme::accentA);
        g.setFont (theme::font (30.0f, true));
        g.drawText ("dedal 2", juce::Rectangle<int> (16, 8, 130, 42), juce::Justification::centredLeft, false);
        g.setColour (theme::text);
        g.setFont (theme::font (13.0f, true));
        g.drawText (tempoText, juce::Rectangle<int> (744, 14, 520, 30), juce::Justification::centredRight, false);

        auto section = [&g] (juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
        {
            g.setColour (theme::panel);
            g.fillRoundedRectangle (r.toFloat(), 10.0f);
            g.setColour (theme::panelEdge);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
            g.setColour (accent);
            g.setFont (theme::font (12.5f, true));
            g.drawText (title, r.reduced (14, 6).removeFromTop (16), juce::Justification::centredLeft, false);
        };

        const char* names[] = { "VCO", "MOD VCO", "MIXER", "VCW", "VCF", "EG", "OUTPUT" };
        const juce::Colour colours[] = { theme::accentA, theme::accentLfo, theme::accentVoice, theme::accentFragment, theme::accentFilter,
                                         theme::accentB, theme::accentA };

        for (int i = 0; i < 7; ++i)
            section ({ 16 + i * 136, 64, 128, 376 }, names[i], colours[i]);

        section ({ 16, 448, 944, 192 }, "SEQUENCER", colourSeq[0]);
        section ({ 16, 648, 944, 88 }, "MIDI", theme::accentLfo);

        for (int k = 0; k < 2; ++k)
        {
            g.setColour (colourSeq[k]);
            g.setFont (theme::font (15.0f, true));
            g.drawText ("SEQ" + juce::String (k + 1), juce::Rectangle<int> (28, 478 + k * 64, 52, 20), juce::Justification::centredLeft, false);
            g.setColour (theme::textDim);
            g.setFont (theme::font (11.5f));
            g.drawText (status[k], juce::Rectangle<int> (28, 500 + k * 64, 52, 16), juce::Justification::centredLeft, false);
        }

        g.setColour (theme::textDim);
        g.setFont (theme::font (11.5f));
        g.drawText ("ORDER", juce::Rectangle<int> (16 + 6 * 136 + 10, 306, 100, 14), juce::Justification::centredLeft, false);
        g.drawText ("Nuty z", juce::Rectangle<int> (168, 662, 100, 14), juce::Justification::centredLeft, false);
        g.drawText (pl ("Przeciągnij takty"), juce::Rectangle<int> (460, 662, 160, 14), juce::Justification::centredLeft, false);
    }

    void update()
    {
        const auto snapshot = processor.getSnapshot();

        for (int k = 0; k < 2; ++k)
        {
            bits[k]->set (snapshot);
            status[k] = juce::String (snapshot.head[(size_t) k] + 1) + "/" + juce::String (snapshot.layout.lengthOf (k));
        }

        runButton.setButtonText (processor.isRunningInternally() ? "STOP" : "RUN");
        runButton.setToggleState (processor.isRunningInternally(), juce::dontSendNotification);
        tempoText = juce::String (processor.getTempo(), 1) + " BPM" + (processor.isHostPlaying() ? pl (" · gra z Abletonem") : "");
        bufferRecall.setEnabled (processor.hasBuffer());

        if (presetBox.getText() != processor.getPresetName())
            refreshPresets();

        bay.repaint();
        repaint (0, 0, kDesignWidth, 56);
        repaint (16, 470, 70, 130);
    }

    Btn saveMidi { "Zapisz MIDI", theme::accentLfo };
    juce::Component& handle() { return dragHandle; }
    std::function<void()> onDrag;

private:
    struct Handle : public juce::Component
    {
        std::function<void()> onDrag;
        bool dragging = false;

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (theme::accentLfo.withAlpha (isMouseOver() ? 0.3f : 0.16f));
            g.fillRoundedRectangle (b, 8.0f);
            g.setColour (theme::accentLfo);
            g.drawRoundedRectangle (b, 8.0f, 1.2f);
            g.setColour (theme::text);
            g.setFont (theme::font (13.0f, true));
            g.drawText (pl ("Przeciągnij MIDI"), getLocalBounds(), juce::Justification::centred, false);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging && e.getDistanceFromDragStart() > 5)
            {
                dragging = true;

                if (onDrag != nullptr)
                    onDrag();
            }
        }

        void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    };

    juce::Component* find (const char* id)
    {
        const auto it = byId.find (id);
        return it == byId.end() ? nullptr : it->second;
    }

    void place (const char* id, int x, int y, int w, int h)
    {
        if (auto* c = find (id))
            c->setBounds (x, y, w, h);
    }

    template <typename T>
    void keep (const char* id, std::unique_ptr<T> c)
    {
        addAndMakeVisible (*c);
        byId[id] = c.get();
        owned.push_back (std::move (c));
    }

    void knob (const char* id, const juce::String& name, juce::Colour accent)
    {
        keep (id, std::make_unique<gizmo::Knob> (processor.apvts, id, name, accent));
    }

    void combo (const char* id)
    {
        auto c = std::make_unique<juce::ComboBox>();

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
            c->addItemList (choice->choices, 1);

        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, id, *c));
        keep (id, std::move (c));
    }

    void segmented (const char* id, juce::Colour accent)
    {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id));
        auto s = std::make_unique<gizmo::Segmented> (choice->choices, accent);
        s->attach (*choice);
        keep (id, std::move (s));
    }

    void toggle (const char* id, const juce::String& text, juce::Colour accent)
    {
        auto b = std::make_unique<Btn> (text, accent);
        b->setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, *b));
        keep (id, std::move (b));
    }

    void refreshPresets()
    {
        presetBox.clear (juce::dontSendNotification);
        const auto factory = processor.getFactoryPresetNames();

        for (int i = 0; i < factory.size(); ++i)
            presetBox.addItem (factory[i], i + 1);

        userFiles = processor.getUserPresets();

        for (int i = 0; i < userFiles.size(); ++i)
            presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), 1000 + i);

        presetBox.setText (processor.getPresetName(), juce::dontSendNotification);
    }

    void stepPreset (int by)
    {
        const auto count = presetBox.getNumItems();
        auto at = 0;

        for (int i = 0; i < count; ++i)
            if (presetBox.getItemText (i) == processor.getPresetName())
                at = i;

        if (count > 0)
            presetBox.setSelectedId (presetBox.getItemId ((at + by + count) % count), juce::sendNotificationSync);
    }

    void askName()
    {
        alert = std::make_unique<juce::AlertWindow> ("Zapisz preset", pl ("Nazwa presetu (Dokumenty/Dedal 2/Presety). Zapisuje też kable."),
                                                     juce::MessageBoxIconType::NoIcon, this);
        alert->addTextEditor ("name", processor.getPresetName());
        alert->addButton ("Zapisz", 1, juce::KeyPress (juce::KeyPress::returnKey));
        alert->addButton ("Anuluj", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Content> safe (this);
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
        {
            if (safe == nullptr || safe->alert == nullptr)
                return;

            if (result == 1)
            {
                safe->processor.savePreset (safe->alert->getTextEditorContents ("name"));
                safe->refreshPresets();
            }

            safe->alert->setVisible (false);
        }), false);
    }

    Dedal2Processor& processor;
    PatchBay bay;
    std::map<juce::String, juce::Component*> byId;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<Bits> bits[2];
    std::unique_ptr<Btn> seqButtons[2][3], extraButtons[2][4];
    Btn runButton { "RUN", theme::accentB }, resetButton { "RESET", theme::accentA }, advanceButton { "ADVANCE", theme::accentA };
    Btn bufferSave { "BUFFER: zapisz", theme::accentA }, bufferRecall { "BUFFER: wczytaj", theme::accentA };
    Btn triggerButton { "TRIGGER", theme::accentB };
    Btn prevPreset { "<", theme::accentA }, nextPreset { ">", theme::accentA }, savePreset { "Zapisz", theme::accentA };
    Btn clearCables { "Wyjmij kable", theme::accentLfo };
    juce::ComboBox presetBox;
    gizmo::Segmented dragBars { { "1", "2", "4", "8", "16" }, theme::accentLfo };
    Handle dragHandle;
    juce::Array<juce::File> userFiles;
    std::unique_ptr<juce::AlertWindow> alert;
    juce::String status[2], tempoText;

public:
    void setDrag (std::function<void()> f) { dragHandle.onDrag = std::move (f); }
};

//==============================================================================
Dedal2Editor::Dedal2Editor (Dedal2Processor& p) : AudioProcessorEditor (p), processor (p)
{
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, theme::knobBody);
    lookAndFeel.setColour (juce::ComboBox::textColourId, theme::text);
    lookAndFeel.setColour (juce::ComboBox::arrowColourId, theme::textDim);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, theme::panelEdge);
    lookAndFeel.setColour (juce::AlertWindow::backgroundColourId, theme::panel);
    lookAndFeel.setColour (juce::AlertWindow::textColourId, theme::text);

    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);
    setLookAndFeel (&lookAndFeel);

    content->setDrag ([this]
    {
        auto fromHistory = false;
        const auto file = processor.writeMidiFile (processor.dragBars, fromHistory);

        if (file != juce::File())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, &content->handle());
    });

    content->saveMidi.onClick = [this]
    {
        auto fromHistory = false;
        const auto source = processor.writeMidiFile (processor.dragBars, fromHistory);

        if (source == juce::File())
            return;

        auto chooser = std::make_shared<juce::FileChooser> ("Zapisz MIDI",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (source.getFileName()), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [source, chooser] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                      source.copyFileTo (fc.getResult().withFileExtension ("mid"));
                              });
    };

    processor.addChangeListener (this);
    updateDisplay();
    setResizable (true, true);
    setResizeLimits (800, 470, 1920, 1128);
    getConstrainer()->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    setSize (1120, 658);
    startTimerHz (30);
}

Dedal2Editor::~Dedal2Editor()
{
    stopTimer();
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void Dedal2Editor::paint (juce::Graphics& g) { g.fillAll (theme::background); }

void Dedal2Editor::resized()
{
    content->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kDesignWidth));
    content->setBounds (0, 0, kDesignWidth, kDesignHeight);
}

void Dedal2Editor::updateDisplay() { content->update(); }
