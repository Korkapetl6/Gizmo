#include "DedalEditor.h"
#include "DedalParams.h"
#include "ui/Theme.h"

using namespace dedal;
namespace theme = gizmo::theme;

namespace
{
const juce::Colour colourSeq[3] = { theme::accentFilter, theme::accentB, theme::accentFragment };
const juce::Colour colourWrite { 0xffff5a5a };

//==============================================================================
/** A row of numbers 1..N for an integer parameter (step lengths). */
class NumberBar : public juce::Component
{
public:
    NumberBar (juce::RangedAudioParameter& parameter, int count, juce::Colour accentColour) : numbers (count), accent (accentColour)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (parameter, [this] (float v)
        {
            selected = juce::roundToInt (v);
            repaint();
        });
        attachment->sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const auto w = (float) getWidth() / (float) numbers;

        for (int i = 0; i < numbers; ++i)
        {
            const auto r = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (1.5f, 0.0f);
            const auto on = i + 1 == selected, inside = i + 1 <= selected;
            g.setColour (on ? accent : (inside ? accent.withAlpha (0.16f) : theme::knobBody));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? theme::background : (inside ? theme::text : theme::textDim));
            g.setFont (theme::font (12.0f, on));
            g.drawText (juce::String (i + 1), r, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { pick (e); }
    void mouseDrag (const juce::MouseEvent& e) override { pick (e); }

private:
    void pick (const juce::MouseEvent& e)
    {
        const auto i = juce::jlimit (1, numbers, (int) ((float) e.x / (float) getWidth() * (float) numbers) + 1);

        if (i != selected)
            attachment->setValueAsCompleteGesture ((float) i);
    }

    int numbers, selected = 1;
    juce::Colour accent;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
} // namespace

//==============================================================================
dedal::Button::Button (const juce::String& label, juce::Colour accentColour, bool isFilled)
    : juce::Button (label), accent (accentColour), filled (isFilled)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void dedal::Button::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();
    auto fill = filled || on ? accent : theme::knobBody;

    if (! (filled || on))
        fill = fill.brighter (highlighted ? 0.15f : 0.0f);
    else if (highlighted)
        fill = fill.brighter (0.1f);

    if (down)
        fill = fill.darker (0.25f);

    g.setColour (fill.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (bounds, 6.0f);

    if (! (filled || on))
    {
        g.setColour (theme::panelEdge.brighter (0.2f));
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
    }

    g.setColour ((filled || on) ? theme::background : theme::text);
    g.setFont (theme::font (getHeight() < 26 ? 11.5f : 12.5f, true));
    g.drawFittedText (getButtonText(), getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
}

//==============================================================================
StepGrid::StepGrid (DedalProcessor& p, int rowIndex) : processor (p), row (rowIndex)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> StepGrid::cellBounds (int i) const
{
    const auto gap = 6.0f;
    const auto w = ((float) getWidth() - gap * (float) (numCells() - 1)) / (float) numCells();
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

int StepGrid::cellAt (juce::Point<float> p) const
{
    for (int i = 0; i < numCells(); ++i)
        if (cellBounds (i).expanded (3.0f, 0.0f).contains (p))
            return i;

    return -1;
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto& s = snapshot.state;
    const auto& layout = snapshot.layout;

    for (int i = 0; i < numCells(); ++i)
    {
        const auto r = cellBounds (i);
        auto inside = true;
        bool on;
        float value, chance = 1.0f;
        bool accent = false;

        if (row == 2)
        {
            inside = i < snapshot.lengthC;
            on = s.bitsC[(size_t) i];
            value = s.valueC[(size_t) i];
            chance = s.chanceC[(size_t) i];
            accent = s.accentC[(size_t) i];
        }
        else
        {
            inside = i < layout.length[row];
            on = s.bits[(size_t) (row * kSteps + i)];
            value = s.values[(size_t) (row * kSteps + i)];
        }

        const auto colour = colourSeq[row];
        g.setColour (theme::knobBody.withMultipliedAlpha (inside ? 1.0f : 0.45f));
        g.fillRoundedRectangle (r, 6.0f);

        if (on)
        {
            const auto alpha = inside ? 1.0f : 0.3f;

            if (row == 2)
            {
                // Chance as the filled height, accents brighter, the value as a thin line.
                const auto filled = r.reduced (3.0f).withTrimmedTop ((1.0f - chance) * (r.getHeight() - 6.0f));
                g.setColour ((accent ? colour.brighter (0.35f) : colour.withAlpha (0.62f)).withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (filled, 4.0f);
                const auto y = r.getCentreY() - value * (r.getHeight() * 0.5f - 6.0f);
                g.setColour (theme::background.withAlpha (0.8f * alpha));
                g.fillRect (r.getX() + 5.0f, y - 1.0f, r.getWidth() - 10.0f, 2.0f);

                if (accent)
                {
                    g.setColour (theme::text.withAlpha (alpha));
                    g.fillEllipse (r.getCentreX() - 3.0f, r.getY() + 5.0f, 6.0f, 6.0f);
                }
            }
            else
            {
                // The bit's voltage drawn up or down from the middle, like a bipolar CV.
                const auto mid = r.getCentreY();
                const auto h = value * (r.getHeight() * 0.5f - 6.0f);
                g.setColour (colour.withAlpha (0.22f * alpha));
                g.fillRoundedRectangle (r.reduced (2.0f), 5.0f);
                g.setColour (colour.withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 6.0f, std::min (mid, mid - h), r.getWidth() - 12.0f, std::max (2.0f, std::abs (h))), 3.0f);
            }
        }

        g.setColour (theme::panelEdge.brighter (0.3f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (theme::textDim.withAlpha (inside ? 0.9f : 0.35f));
        g.setFont (theme::font (10.5f));
        g.drawText (juce::String (i + 1), r.withHeight (14.0f).translated (0.0f, r.getHeight() - 15.0f), juce::Justification::centred, false);
    }

    // Heads: SEQ1 and SEQ2 may both run over these cells when they are chained.
    auto marker = [&] (int cell, juce::Colour c, bool write)
    {
        if (cell < 0 || cell >= numCells())
            return;

        const auto r = cellBounds (cell);

        if (write)
        {
            g.setColour (c);
            g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.0f);
        }
        else
        {
            g.setColour (c);
            g.fillRoundedRectangle (r.getX() + 4.0f, r.getY() - 7.0f, r.getWidth() - 8.0f, 4.0f, 2.0f);
        }
    };

    if (row == 2)
    {
        marker (snapshot.head[2], colourSeq[2].brighter (0.4f), false);
        return;
    }

    for (int k = 0; k < 2; ++k)
    {
        if (! layout.chain && k != row)
            continue;

        const auto playSlot = layout.slot (k, snapshot.head[(size_t) k]);
        const auto writeSlot = layout.slot (k, snapshot.writeHead[(size_t) k]);

        if (playSlot / kSteps == row)
            marker (playSlot % kSteps, colourSeq[k].brighter (0.4f), false);

        if (writeSlot / kSteps == row && k == row)
            marker (writeSlot % kSteps, colourWrite, true);
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    pressed = cellAt (e.position);
    dragged = false;

    if (pressed < 0)
        return;

    const auto s = processor.getSnapshot().state;

    if (row == 2)
        startValue = editMode == 2 ? s.valueC[(size_t) pressed] : s.chanceC[(size_t) pressed];
    else
        startValue = s.values[(size_t) (row * kSteps + pressed)];
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || std::abs (e.getDistanceFromDragStartY()) < 4)
        return;

    dragged = true;
    const auto change = (float) -e.getDistanceFromDragStartY() / (float) getHeight();

    if (row == 2)
    {
        if (editMode == 2)
            processor.setValueC (pressed, startValue + change * 2.0f);
        else if (editMode == 0)
            processor.setChanceC (pressed, startValue + change);
    }
    else
    {
        processor.setSlotValue (row * kSteps + pressed, startValue + change * 2.0f);
    }

    setSnapshot (processor.getSnapshot());
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (pressed >= 0 && ! dragged)
    {
        if (row == 2)
        {
            if (editMode == 1)
                processor.toggleAccentC (pressed);
            else
                processor.toggleC (pressed);
        }
        else
        {
            processor.toggleSlot (row * kSteps + pressed);
        }

        setSnapshot (processor.getSnapshot());
    }

    pressed = -1;
}

//==============================================================================
DragHandle::DragHandle (const juce::String& textToShow, juce::Colour accentColour) : text (textToShow), accent (accentColour)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragHandle::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto hot = isMouseOver() || dragging;

    g.setColour (accent.withAlpha (hot ? 0.3f : 0.16f));
    g.fillRoundedRectangle (bounds, 8.0f);

    juce::Path outline, dashed;
    outline.addRoundedRectangle (bounds, 8.0f);
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (accent.withAlpha (hot ? 1.0f : 0.75f));
    g.fillPath (dashed);

    for (int column = 0; column < 2; ++column)
        for (int r = 0; r < 3; ++r)
            g.fillEllipse (bounds.getX() + 13.0f + (float) column * 5.0f, bounds.getCentreY() - 6.5f + (float) r * 5.0f, 2.6f, 2.6f);

    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText (text, getLocalBounds().withTrimmedLeft (28).withTrimmedRight (6), juce::Justification::centred, false);
}

void DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 5)
    {
        dragging = true;
        repaint();

        if (onDrag != nullptr)
            onDrag();
    }
}

void DragHandle::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

//==============================================================================
class DedalEditor::Content : public juce::Component
{
public:
    explicit Content (DedalProcessor& p) : processor (p)
    {
        auto& state = p.apvts;

        // Header
        for (auto* b : { &tabSeq, &tabSound })
        {
            b->setClickingTogglesState (false);
            addAndMakeVisible (b);
        }

        tabSeq.onClick = [this] { showPage (0); };
        tabSound.onClick = [this] { showPage (1); };
        addAndMakeVisible (presetBox);
        presetBox.setTextWhenNothingSelected ("Preset");
        presetBox.onChange = [this] { presetChosen(); };
        addAndMakeVisible (prevPreset);
        addAndMakeVisible (nextPreset);
        addAndMakeVisible (savePreset);
        prevPreset.onClick = [this] { stepPreset (-1); };
        nextPreset.onClick = [this] { stepPreset (1); };
        savePreset.onClick = [this] { askPresetName(); };
        addAndMakeVisible (runButton);
        runButton.onClick = [this] { processor.setRunning (! processor.isRunningInternally()); };

        // Sequencer page
        for (int k = 0; k < 2; ++k)
        {
            grids[k] = std::make_unique<StepGrid> (p, k);
            lengths[k] = std::make_unique<NumberBar> (*state.getParameter (ID::length[k]), kSteps, colourSeq[k]);
            page (0, *grids[k]);
            page (0, *lengths[k]);

            const char* names[] = { "BIT FLIP", "BIT SHIFT", "LENGTH", "ADVANCE", "RESET", "↺ SHIFT",
                                    "ZAPIS +1", "ZAPIS 0", "ODWRÓĆ", "Losuj", "Wyczyść" };
            const Command commands[] = { Command::bitFlip, Command::bitShift, Command::length, Command::advance, Command::reset,
                                         Command::unshift, Command::writeAdvance, Command::writeReset, Command::reverse,
                                         Command::randomise, Command::clear };

            for (int i = 0; i < 11; ++i)
            {
                auto& b = seqButtons[k][(size_t) i];
                b = std::make_unique<dedal::Button> (juce::String::fromUTF8 (names[i]), colourSeq[k], i == 0);
                const auto command = commands[i];
                b->onClick = [this, command, k] { processor.command (command, k); };
                page (0, *b);
            }

            knob (0, ID::corrupt[k], "Corrupt", colourSeq[k]);
            knob (0, ID::range[k], "Zakres", colourSeq[k]);
            knob (0, ID::swing[k], "Swing", colourSeq[k]);
            knob (0, ID::chance[k], "Szansa", colourSeq[k]);
            combo (0, ID::rate[k], colourSeq[k]);
            combo (0, ID::direction[k], colourSeq[k]);
        }

        gridC = std::make_unique<StepGrid> (p, 2);
        lengthC = std::make_unique<NumberBar> (*state.getParameter (ID::lengthC), kStepsC, colourSeq[2]);
        page (0, *gridC);
        page (0, *lengthC);
        page (0, editModeC);
        editModeC.onChange = [this] (int m) { gridC->setEditMode (m); };

        {
            const char* names[] = { "ADVANCE", "RESET", "Losuj", "Wyczyść" };
            const Command commands[] = { Command::advance, Command::reset, Command::randomise, Command::clear };

            for (int i = 0; i < 4; ++i)
            {
                auto& b = buttonsC[(size_t) i];
                b = std::make_unique<dedal::Button> (juce::String::fromUTF8 (names[i]), colourSeq[2]);
                const auto command = commands[i];
                b->onClick = [this, command] { processor.command (command, 2); };
                page (0, *b);
            }
        }

        knob (0, ID::corruptC, "Corrupt C", colourSeq[2]);
        knob (0, ID::swingC, "Swing C", colourSeq[2]);
        knob (0, ID::cEg, pl ("C → EG"), colourSeq[2]);
        combo (0, ID::rateC, colourSeq[2]);

        for (const auto* id : { ID::cReset, ID::cDir, ID::cLength, ID::cChance, ID::cTrans })
            segmented (0, id, colourSeq[2]);

        combo (0, ID::cResetOn, colourSeq[2]);
        combo (0, ID::cDirOn, colourSeq[2]);
        bar (0, ID::cLengthAmt, colourSeq[2]);
        bar (0, ID::cChanceAmt, colourSeq[2]);
        bar (0, ID::cTransAmt, colourSeq[2]);

        toggle (0, ID::chain, "CHAIN", theme::accentA);
        for (auto* b : { &offsetDown, &offsetUp, &offsetZero })
            page (0, *b);

        offsetDown.onClick = [this] { processor.nudgeChainOffset (-1); };
        offsetUp.onClick = [this] { processor.nudgeChainOffset (1); };
        offsetZero.onClick = [this] { processor.nudgeChainOffset (0); };
        combo (0, ID::scale, theme::accentA);
        combo (0, ID::key, theme::accentA);
        combo (0, ID::octave, theme::accentA);
        segmented (0, ID::polarity, theme::accentA);
        toggle (0, ID::midiTranspose, "Transp. z MIDI", theme::accentA);
        page (0, bufferSave);
        page (0, bufferRecall);
        bufferSave.onClick = [this] { processor.saveBuffer(); };
        bufferRecall.onClick = [this] { processor.recallBuffer(); };
        page (0, triggerButton);
        triggerButton.onClick = [this]
        {
            if (auto* t = processor.apvts.getParameter (ID::trigger))
            {
                t->setValueNotifyingHost (1.0f);
                juce::Timer::callAfterDelay (60, [t] { t->setValueNotifyingHost (0.0f); });
            }
        };
        bar (0, ID::trigMix, theme::accentA);

        // Sound page
        segmented (1, ID::vcoType, theme::accentA);
        combo (1, ID::vcoBank, theme::accentA);

        for (auto [id, name] : { std::pair { ID::vcoWave, "Fala" }, { ID::vcoPw, "PW" }, { ID::vcoTune, "Strój" },
                                 { ID::vcoFine, "Dostrój" }, { ID::seq1Pitch, "SEQ1 → wys." },
                                 { ID::seq1Wave, "SEQ1 → fala" }, { ID::eg1Vco, "EG1 → wys." },
                                 { ID::glide, "Poślizg" } })
            knob (1, id, juce::String::fromUTF8 (name), theme::accentA);

        segmented (1, ID::modType, theme::accentLfo);
        combo (1, ID::modBank, theme::accentLfo);
        segmented (1, ID::modMode, theme::accentLfo);
        toggle (1, ID::modSync, "Sync do VCO", theme::accentLfo);

        for (auto [id, name] : { std::pair { ID::modWave, "Fala" }, { ID::modPw, "PW" }, { ID::modTune, "Strój" },
                                 { ID::modRate, "LFO" }, { ID::seq2Pitch, "SEQ2 → wys." },
                                 { ID::seq2Wave, "SEQ2 → fala" }, { ID::eg1Mod, "EG1 → wys." } })
            knob (1, id, juce::String::fromUTF8 (name), theme::accentLfo);

        for (auto [id, name] : { std::pair { ID::lvlVco, "VCO" }, { ID::lvlMod, "MOD" }, { ID::lvlRing, "Ring" }, { ID::lvlNoise, "Szum" },
                                 { ID::noiseTone, "Barwa szumu" }, { ID::fm, "FM" } })
            knob (1, id, name, theme::accentVoice);

        for (auto [id, name] : { std::pair { ID::fold, "Fold" }, { ID::bias, "Bias" }, { ID::seq1Fold, "SEQ1 → fold" },
                                 { ID::eg1Fold, "EG1 → fold" } })
            knob (1, id, juce::String::fromUTF8 (name), theme::accentFragment);

        for (auto [id, name] : { std::pair { ID::cutoff, "Odcięcie" }, { ID::resonance, "Rezonans" },
                                 { ID::filterType, "LP ↔ BP" }, { ID::seq2Cutoff, "SEQ2 → filtr" },
                                 { ID::eg1Cutoff, "EG1 → filtr" } })
            knob (1, id, juce::String::fromUTF8 (name), theme::accentFilter);

        segmented (1, ID::routing, theme::accentB);
        knob (1, ID::blend, "Blend", theme::accentB);
        knob (1, ID::eg1Decay, "EG1 decay", theme::accentB);
        knob (1, ID::eg2Decay, "EG2 decay", theme::accentB);
        segmented (1, ID::vcaW, theme::accentB);
        segmented (1, ID::vcaF, theme::accentB);

        // MIDI strip (both pages)
        toggle (2, ID::midiSend, pl ("Wysyłaj MIDI"), theme::accentLfo);
        toggle (2, ID::ccSend, "CC z SEQ2", theme::accentLfo);
        segmented (2, ID::midiSeq, theme::accentLfo);
        toggle (2, ID::cMidi, "Nuty C w MIDI", colourSeq[2]);
        bar (2, ID::cNote, colourSeq[2]);
        knob (2, ID::gate, pl ("Długość nut"), theme::accentLfo);
        knob (2, ID::ccNumber, "Numer CC", theme::accentLfo);
        toggle (2, ID::sound, pl ("Własne brzmienie"), theme::accentVoice);
        bar (2, ID::volume, theme::accentVoice);
        addAndMakeVisible (dragBars);
        dragBars.setSelected (2);
        dragBars.onChange = [this] (int i) { processor.setDragBars (kDragBars[i]); };
        addAndMakeVisible (dragHandle);
        addAndMakeVisible (saveMidi);

        refreshPresets();
        showPage (0);
    }

    //==============================================================================
    void resized() override
    {
        // Header
        tabSeq.setBounds (124, 14, 110, 30);
        tabSound.setBounds (240, 14, 110, 30);
        prevPreset.setBounds (380, 14, 30, 30);
        presetBox.setBounds (414, 14, 250, 30);
        nextPreset.setBounds (668, 14, 30, 30);
        savePreset.setBounds (704, 14, 84, 30);
        runButton.setBounds (800, 14, 110, 30);

        // SEQ1 and SEQ2
        for (int k = 0; k < 2; ++k)
        {
            const auto y = 96 + k * 146;
            grids[k]->setBounds (100, y, 442, 84);
            lengths[k]->setBounds (100, y + 90, 442, 22);

            for (int i = 0; i < 11; ++i)
                seqButtons[k][(size_t) i]->setBounds (556 + (i % 3) * 93, y + (i / 3) * 30, 88, 26);

            place (ID::corrupt[k], 846, y - 8, 72, 92);
            place (ID::range[k], 918, y - 8, 72, 92);
            place (ID::swing[k], 990, y - 8, 72, 92);
            place (ID::chance[k], 1062, y - 8, 72, 92);
            place (ID::rate[k], 846, y + 88, 140, 26);
            place (ID::direction[k], 992, y + 88, 142, 26);
        }

        // SEQ C
        gridC->setBounds (100, 414, 730, 84);
        lengthC->setBounds (100, 504, 730, 22);
        editModeC.setBounds (100, 534, 330, 24);
        place (ID::corruptC, 846, 404, 72, 92);
        place (ID::swingC, 918, 404, 72, 92);
        place (ID::cEg, 990, 404, 72, 92);
        place (ID::rateC, 1066, 438, 106, 26);

        for (int i = 0; i < 4; ++i)
            buttonsC[(size_t) i]->setBounds (846 + (i % 3) * 114, 502 + (i / 3) * 30, 108, 26);

        // Logic of SEQ C
        const char* targets[] = { ID::cReset, ID::cDir, ID::cLength, ID::cChance, ID::cTrans };
        const char* second[] = { ID::cResetOn, ID::cDirOn, ID::cLengthAmt, ID::cChanceAmt, ID::cTransAmt };

        for (int i = 0; i < 5; ++i)
        {
            const auto x = 30 + i * 232;
            place (targets[i], x, 628, 216, 26);
            place (second[i], x, 660, 216, 26);
        }

        // Shared
        auto x = 30;
        auto next = [&x] (int width) { const auto at = x; x += width + 8; return at; };
        place (ID::chain, next (78), 760, 78, 28);
        offsetDown.setBounds (next (28), 760, 28, 28);
        x += 34;
        offsetUp.setBounds (next (28), 760, 28, 28);
        offsetZero.setBounds (next (28), 760, 28, 28);
        offsetX = offsetDown.getRight() + 2;
        place (ID::scale, next (150), 760, 150, 28);
        place (ID::key, next (64), 760, 64, 28);
        place (ID::octave, next (56), 760, 56, 28);
        place (ID::polarity, next (76), 760, 76, 28);
        place (ID::midiTranspose, next (112), 760, 112, 28);
        bufferSave.setBounds (next (92), 760, 92, 28);
        bufferRecall.setBounds (next (92), 760, 92, 28);
        triggerButton.setBounds (next (76), 760, 76, 28);
        place (ID::trigMix, next (1184 - x), 760, 1184 - x, 28);

        // Sound page
        place (ID::vcoType, 30, 100, 130, 26);
        place (ID::vcoBank, 30, 134, 130, 26);
        x = 170;

        for (const auto* id : { ID::vcoWave, ID::vcoPw, ID::vcoTune, ID::vcoFine, ID::seq1Pitch, ID::seq1Wave, ID::eg1Vco, ID::glide })
            place (id, next (64) , 92, 72, 100);

        x = 784;
        for (const auto* id : { ID::lvlVco, ID::lvlMod, ID::lvlRing, ID::lvlNoise, ID::noiseTone, ID::fm })
            place (id, next (56), 92, 64, 100);

        place (ID::modType, 30, 272, 130, 26);
        place (ID::modBank, 30, 306, 130, 26);
        place (ID::modMode, 170, 272, 110, 26);
        place (ID::modSync, 170, 306, 110, 26);
        x = 290;
        for (const auto* id : { ID::modWave, ID::modPw, ID::modTune, ID::modRate, ID::seq2Pitch, ID::seq2Wave, ID::eg1Mod })
            place (id, next (62), 264, 70, 100);

        x = 836;
        for (const auto* id : { ID::fold, ID::bias, ID::seq1Fold, ID::eg1Fold })
            place (id, next (76), 264, 80, 100);

        x = 30;
        for (const auto* id : { ID::cutoff, ID::resonance, ID::filterType, ID::seq2Cutoff, ID::eg1Cutoff })
            place (id, next (68), 450, 76, 100);

        place (ID::routing, 446, 456, 300, 28);
        place (ID::blend, 560, 494, 72, 92);
        place (ID::eg1Decay, 784, 450, 76, 100);
        place (ID::eg2Decay, 864, 450, 76, 100);
        place (ID::vcaW, 954, 470, 216, 26);
        place (ID::vcaF, 954, 526, 216, 26);

        // MIDI strip
        place (ID::midiSend, 30, 836, 140, 28);
        place (ID::ccSend, 30, 872, 140, 28);
        place (ID::midiSeq, 184, 852, 180, 28);
        place (ID::cMidi, 378, 836, 140, 28);
        place (ID::cNote, 378, 872, 140, 28);
        place (ID::gate, 528, 824, 72, 96);
        place (ID::ccNumber, 602, 824, 72, 96);
        place (ID::sound, 688, 836, 140, 28);
        place (ID::volume, 688, 872, 140, 28);
        dragBars.setBounds (842, 852, 150, 28);
        dragHandle.setBounds (1004, 828, 180, 44);
        saveMidi.setBounds (1004, 880, 180, 30);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::background);

        g.setColour (theme::accentA);
        g.setFont (theme::font (30.0f, true));
        g.drawText ("dedal", juce::Rectangle<int> (16, 8, 100, 42), juce::Justification::centredLeft, false);

        g.setColour (theme::text);
        g.setFont (theme::font (13.0f, true));
        g.drawText (tempoText, juce::Rectangle<int> (920, 14, 264, 30), juce::Justification::centredRight, false);

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

        auto label = [&g] (int x, int y, int w, const juce::String& text, juce::Justification j = juce::Justification::centredLeft)
        {
            g.setColour (theme::textDim);
            g.setFont (theme::font (12.0f));
            g.drawText (text, juce::Rectangle<int> (x, y, w, 16), j, false);
        };

        if (currentPage == 0)
        {
            section ({ 16, 64, 1168, 314 }, "SEKWENCERY 1 I 2", colourSeq[0]);
            section ({ 16, 386, 1168, 180 }, pl ("SEKWENCER C: RYTM I SZANSA"), colourSeq[2]);
            section ({ 16, 574, 1168, 124 }, pl ("LOGIKA C: CO TRZECI SEKWENCER ROBI Z DWOMA POZOSTAŁYMI"), colourSeq[2]);
            section ({ 16, 706, 1168, 94 }, pl ("WSPÓLNE"), theme::accentA);

            for (int k = 0; k < 3; ++k)
            {
                const auto y = k < 2 ? 96 + k * 146 : 414;
                g.setColour (colourSeq[k]);
                g.setFont (theme::font (17.0f, true));
                g.drawText (k < 2 ? "SEQ " + juce::String (k + 1) : juce::String ("SEQ C"), juce::Rectangle<int> (30, y, 66, 24),
                            juce::Justification::centredLeft, false);
                g.setColour (theme::textDim);
                g.setFont (theme::font (11.5f));
                g.drawText (status[k], juce::Rectangle<int> (30, y + 26, 68, 56), juce::Justification::topLeft, false);
            }

            label (440, 538, 400, pl ("Kroki: klik włącza, przeciągnij w pionie = szansa"));
            const char* titles[] = { "Reset", "Kierunek", "Długość", "Szansa 1 / 2", "Transpozycja" };
            const char* hints[] = { "kiedy", "kiedy", "ile od wartości kroku C", "ile od wartości kroku C",
                                    "zakres, w skali" };

            for (int i = 0; i < 5; ++i)
            {
                g.setColour (theme::text);
                g.setFont (theme::font (12.5f, true));
                g.drawText (juce::String::fromUTF8 (titles[i]), juce::Rectangle<int> (30 + i * 232, 604, 120, 18), juce::Justification::centredLeft, false);
                label (130 + i * 232, 605, 116, juce::String::fromUTF8 (hints[i]), juce::Justification::centredRight);
            }

            const auto labelY = 740;
            label (30, labelY, 80, "Chain");
            label (offsetDown.getX(), labelY, 140, pl ("Przesunięcie SEQ2"));
            g.setColour (theme::text);
            g.setFont (theme::font (14.0f, true));
            g.drawText (juce::String (chainOffset), juce::Rectangle<int> (offsetX, 760, 32, 28), juce::Justification::centred, false);

            for (auto [id, text] : { std::pair { ID::scale, "Skala" }, { ID::key, "Tonacja" }, { ID::octave, "Oktawa" },
                                     { ID::polarity, "CV" }, { ID::midiTranspose, "Klawiatura" }, { ID::trigMix, "EG trig mix: SEQ1 / SEQ2" } })
                if (auto* c = find (id))
                    label (c->getX(), labelY, c->getWidth() + 40, text);

            label (bufferSave.getX(), labelY, 180, "Bufor (BUFFER)");
        }
        else
        {
            section ({ 16, 64, 752, 172 }, "VCO", theme::accentA);
            section ({ 776, 64, 408, 172 }, "MIKSER", theme::accentVoice);
            section ({ 16, 244, 804, 172 }, "MOD VCO", theme::accentLfo);
            section ({ 828, 244, 356, 172 }, "WAVEFOLDER", theme::accentFragment);
            section ({ 16, 424, 412, 172 }, "FILTR", theme::accentFilter);
            section ({ 436, 424, 320, 172 }, pl ("KOLEJNOŚĆ"), theme::accentB);
            section ({ 764, 424, 420, 172 }, "OBWIEDNIE I VCA", theme::accentB);
            section ({ 16, 604, 1168, 196 }, "O BRZMIENIU", theme::accentVoice);

            label (954, 452, 216, "VCA wavefoldera");
            label (954, 508, 216, "VCA filtra");

            g.setColour (theme::textDim);
            g.setFont (theme::font (13.0f));
            g.drawMultiLineText (
                pl ("VCO gra SEQ1, MOD VCO gra SEQ2. Fala płynnie przechodzi sinus → trójkąt → piła → kwadrat (Analog) "
                    "albo przesuwa się po 16 falach wybranego banku (Beads). FM jest thru-zero: przy dużej głębokości faza "
                    "VCO cofa się, a wysokość zostaje.\n\n"
                    "Wavefolder i filtr mogą grać obok siebie albo jeden w drugi. Blend miesza ich wyjścia, każde ma "
                    "własny VCA otwierany przez EG1, EG2 albo stale (dron). EG1 i EG2 to obwiednie zaniku, wyzwalane przez "
                    "sekwencery (EG trig mix) i przez SEQ C (C → EG).\n\n"
                    "Banki Beads pochodzą z otwartego firmware'u modułu Beads (Emilie Gillet, licencja MIT)."),
                32, 640, 1130);
        }

        section ({ 16, 808, 1168, 136 }, pl ("MIDI I INNE BRZMIENIE"), theme::accentLfo);
        label (184, 834, 180, "Nuty MIDI z");
        label (842, 834, 150, pl ("Przeciągnij takty"));
        label (842, 884, 160, sourceText);
    }

    //==============================================================================
    void showPage (int newPage)
    {
        currentPage = newPage;
        tabSeq.setToggleState (newPage == 0, juce::dontSendNotification);
        tabSound.setToggleState (newPage == 1, juce::dontSendNotification);

        for (auto& [c, onPage] : pages)
            c->setVisible (onPage == newPage || onPage == 2);

        repaint();
    }

    void update()
    {
        const auto snapshot = processor.getSnapshot();

        for (int k = 0; k < 2; ++k)
            grids[k]->setSnapshot (snapshot);

        gridC->setSnapshot (snapshot);
        chainOffset = snapshot.state.chainOffset;

        for (int k = 0; k < 3; ++k)
        {
            const auto length = k == 2 ? snapshot.lengthC : snapshot.layout.lengthOf (k);
            status[k] = "krok " + juce::String (snapshot.head[(size_t) k] + 1) + "/" + juce::String (length);

            if (k < 2 && snapshot.state.lengthOverride[(size_t) k] > 0)
                status[k] << pl ("\ndł. z C");

            if (k < 2 && snapshot.state.reversed[(size_t) k])
                status[k] << pl ("\nodwrócony");
        }

        runButton.setButtonText (processor.isRunningInternally() ? pl ("■ Stop") : pl ("▶ Graj"));
        runButton.setToggleState (processor.isRunningInternally(), juce::dontSendNotification);
        tempoText = juce::String (processor.getTempo(), 1) + " BPM" + (processor.isHostPlaying() ? pl (" · gra z Abletonem") : "");
        sourceText = processor.describeClipSource (processor.getDragBars());

        if (presetBox.getText() != processor.getPresetName())
            refreshPresets();

        bufferRecall.setEnabled (processor.hasBuffer());
        repaint (0, 0, kDesignWidth, 56);
        repaint (16, 64, 90, 520);
        repaint (16, 706, 400, 94);
        repaint (830, 880, 180, 30);
    }

    void refreshPresets()
    {
        presetBox.clear (juce::dontSendNotification);
        const auto factory = processor.getFactoryPresetNames();
        presetBox.addSectionHeading ("Fabryczne");

        for (int i = 0; i < factory.size(); ++i)
            presetBox.addItem (factory[i], i + 1);

        userFiles = processor.getUserPresets();

        if (! userFiles.isEmpty())
        {
            presetBox.addSectionHeading ("Twoje");

            for (int i = 0; i < userFiles.size(); ++i)
                presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), 1000 + i);
        }

        presetBox.setText (processor.getPresetName(), juce::dontSendNotification);
    }

    std::function<void (bool)> onDragMidi;
    dedal::Button saveMidi { "Zapisz MIDI", theme::accentLfo };
    DragHandle dragHandle { pl ("Przeciągnij MIDI"), theme::accentLfo };

private:
    void page (int pageIndex, juce::Component& c)
    {
        addAndMakeVisible (c);
        pages.push_back ({ &c, pageIndex });
    }

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

    void knob (int pageIndex, const char* id, const juce::String& name, juce::Colour accent)
    {
        auto k = std::make_unique<gizmo::Knob> (processor.apvts, id, name, accent);
        page (pageIndex, *k);
        byId[id] = k.get();
        owned.push_back (std::move (k));
    }

    void combo (int pageIndex, const char* id, juce::Colour accent)
    {
        auto c = std::make_unique<juce::ComboBox>();
        c->setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.5f));

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
            c->addItemList (choice->choices, 1);

        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, id, *c));
        page (pageIndex, *c);
        byId[id] = c.get();
        owned.push_back (std::move (c));
    }

    void segmented (int pageIndex, const char* id, juce::Colour accent)
    {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id));
        auto s = std::make_unique<gizmo::Segmented> (choice != nullptr ? choice->choices : juce::StringArray(), accent);

        if (choice != nullptr)
            s->attach (*choice);

        page (pageIndex, *s);
        byId[id] = s.get();
        owned.push_back (std::move (s));
    }

    void toggle (int pageIndex, const char* id, const juce::String& text, juce::Colour accent)
    {
        auto b = std::make_unique<dedal::Button> (text, accent);
        b->setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, *b));
        page (pageIndex, *b);
        byId[id] = b.get();
        owned.push_back (std::move (b));
    }

    void bar (int pageIndex, const char* id, juce::Colour accent)
    {
        auto s = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxBelow);
        s->setColour (juce::Slider::trackColourId, accent.withAlpha (0.45f));
        s->setColour (juce::Slider::backgroundColourId, theme::knobBody);
        s->setColour (juce::Slider::textBoxTextColourId, theme::text);
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s->setTextBoxIsEditable (false);
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, *s));
        page (pageIndex, *s);
        byId[id] = s.get();
        owned.push_back (std::move (s));
    }

    void presetChosen()
    {
        const auto id = presetBox.getSelectedId();

        if (id >= 1 && id < 1000)
            processor.loadFactoryPreset (id - 1);
        else if (id >= 1000 && id - 1000 < userFiles.size())
            processor.loadPresetFile (userFiles[id - 1000]);
    }

    void stepPreset (int by)
    {
        juce::Array<int> ids;

        for (int i = 0; i < presetBox.getNumItems(); ++i)
            ids.add (presetBox.getItemId (i));

        if (ids.isEmpty())
            return;

        auto at = ids.indexOf (presetBox.getSelectedId());

        if (at < 0)
        {
            for (int i = 0; i < presetBox.getNumItems(); ++i)
                if (presetBox.getItemText (i) == processor.getPresetName())
                    at = i;
        }

        presetBox.setSelectedId (ids[(at + by + ids.size()) % ids.size()], juce::sendNotificationSync);
    }

    void askPresetName()
    {
        alert = std::make_unique<juce::AlertWindow> ("Zapisz preset", pl ("Nazwa presetu (zapisze się w Dokumenty/Dedal/Presety):"),
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

    DedalProcessor& processor;
    int currentPage = 0;
    std::vector<std::pair<juce::Component*, int>> pages;
    std::map<juce::String, juce::Component*> byId;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;

    dedal::Button tabSeq { "Sekwencje", theme::accentA }, tabSound { "Brzmienie", theme::accentA };
    juce::ComboBox presetBox;
    dedal::Button prevPreset { pl ("◀"), theme::accentA }, nextPreset { pl ("▶"), theme::accentA }, savePreset { "Zapisz", theme::accentA };
    dedal::Button runButton { pl ("▶ Graj"), theme::accentB };

    std::unique_ptr<StepGrid> grids[2], gridC;
    std::unique_ptr<juce::Component> lengths[2], lengthC;
    std::array<std::unique_ptr<dedal::Button>, 11> seqButtons[2];
    std::array<std::unique_ptr<dedal::Button>, 4> buttonsC;
    gizmo::Segmented editModeC { { "Kroki i szansa", "Akcenty", pl ("Wartości") }, colourSeq[2] };
    dedal::Button offsetDown { "-", theme::accentA }, offsetUp { "+", theme::accentA }, offsetZero { "0", theme::accentA };
    dedal::Button bufferSave { pl ("Zapamiętaj"), theme::accentA }, bufferRecall { pl ("Przywróć"), theme::accentA };
    dedal::Button triggerButton { "TRIGGER", theme::accentB };
    gizmo::Segmented dragBars { { "1", "2", "4", "8", "16" }, theme::accentLfo };

    juce::Array<juce::File> userFiles;
    std::unique_ptr<juce::AlertWindow> alert;
    juce::String status[3], tempoText, sourceText;
    int chainOffset = 0, offsetX = 0;
};

//==============================================================================
DedalEditor::DedalEditor (DedalProcessor& p) : AudioProcessorEditor (p), processor (p)
{
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, theme::knobBody);
    lookAndFeel.setColour (juce::ComboBox::textColourId, theme::text);
    lookAndFeel.setColour (juce::ComboBox::arrowColourId, theme::textDim);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, theme::panelEdge);
    lookAndFeel.setColour (juce::AlertWindow::backgroundColourId, theme::panel);
    lookAndFeel.setColour (juce::AlertWindow::textColourId, theme::text);
    lookAndFeel.setColour (juce::TextButton::buttonColourId, theme::knobBody);

    content = std::make_unique<Content> (p);

    // Add the content first, so the look-and-feel change reaches every control inside it.
    addAndMakeVisible (*content);
    setLookAndFeel (&lookAndFeel);

    content->saveMidi.onClick = [this]
    {
        auto fromHistory = false;
        const auto source = processor.writeMidiFile (processor.getDragBars(), fromHistory);

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

    content->dragHandle.onDrag = [this]
    {
        auto fromHistory = false;
        const auto file = processor.writeMidiFile (processor.getDragBars(), fromHistory);

        if (file != juce::File())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, &content->dragHandle);
    };

    processor.addChangeListener (this);
    updateDisplay();

    setResizable (true, true);
    setResizeLimits (720, 571, 1800, 1428);
    getConstrainer()->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    setSize (1000, 793);
    startTimerHz (30);
}

DedalEditor::~DedalEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void DedalEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void DedalEditor::resized()
{
    content->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kDesignWidth));
    content->setBounds (0, 0, kDesignWidth, kDesignHeight);
}

void DedalEditor::showPage (int page)
{
    content->showPage (page);
}

void DedalEditor::updateDisplay()
{
    content->update();
}

void DedalEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateDisplay();
}
