#include "SolistaEditor.h"
#include "ui/Theme.h"

using namespace solista;
namespace theme = gizmo::theme;

namespace
{
const juce::Colour leadColour = theme::accentFilter;
const juce::Colour secondColour = theme::accentB;

juce::String styleHint (int style)
{
    switch (style)
    {
        case styleEpic:    return pl ("od długich bendów do szybkich biegów i powtarzanych zagrywek");
        case styleSinging: return pl ("melodia jak głos: slajdy, pre-bendy, szerokie vibrato");
        case styleGroove:  return pl ("krótkie, rytmiczne figury z pauzami i akcentami obok bitu");
        case styleSwing:   return pl ("melodie po dźwiękach akordów, luźno i śpiewnie");
        default:           return pl ("wolne bendy, spóźnione vibrato, granie za bitem, dwudźwięki");
    }
}
} // namespace

//==============================================================================
void SoloView::setBundle (std::shared_ptr<const SoloBundle> newBundle)
{
    bundle = std::move (newBundle);

    if (bundle != nullptr)
    {
        auto low = 127.0f, high = 0.0f;

        for (const auto* solo : { &bundle->solo, &bundle->second })
        {
            for (const auto& n : solo->notes)
            {
                low = std::min (low, (float) n.pitch + std::min (0.0f, std::min (n.art.slideIn, n.art.fallOff)));
                high = std::max (high, (float) n.pitch + std::max (0.0f, n.art.bendSemis));
            }
        }

        if (low <= high)
        {
            lowest = (int) std::floor (low) - 2;
            highest = (int) std::ceil (high) + 2;

            if (highest - lowest < 14)
            {
                const auto extra = (14 - (highest - lowest) + 1) / 2;
                lowest -= extra;
                highest += extra;
            }
        }
    }

    repaint();
}

void SoloView::setPlayPosition (double beats)
{
    if (std::abs (beats - position) > 1.0e-4)
    {
        position = beats;
        repaint();
    }
}

void SoloView::drawSolo (juce::Graphics& g, const Solo& solo, juce::Rectangle<float> area, juce::Colour colour,
                         float thickness, bool heads) const
{
    const auto secondsPerBeat = solo.secondsPerBeat();
    const auto pixelsPerBeat = area.getWidth() / (float) solo.lengthBeats;
    const auto range = (float) (highest - lowest);
    auto yOf = [&] (float pitch) { return area.getBottom() - (pitch - (float) lowest) / range * area.getHeight(); };

    for (const auto& n : solo.notes)
    {
        const auto x0 = area.getX() + (float) n.start * pixelsPerBeat;
        const auto width = std::max (2.0f, (float) n.length * pixelsPerBeat - 1.5f);
        const auto lengthSeconds = n.length * secondsPerBeat;
        const auto level = juce::jlimit (0.0f, 1.0f, (float) n.velocity / 127.0f);
        const auto shade = colour.withAlpha (0.4f + 0.6f * level);
        const auto bent = n.art.bend != BendKind::none;

        if (bent)
        {
            // Where the finger is: the bend leaves this line.
            g.setColour (colour.withAlpha (0.22f));
            g.fillRect (x0, yOf ((float) n.pitch) - 0.75f, width, 1.5f);
        }

        juce::Path curve;
        const auto steps = std::max (2, (int) (width / 1.5f));

        for (int i = 0; i <= steps; ++i)
        {
            const auto u = (double) i / (double) steps;
            const auto y = yOf ((float) n.pitch + n.pitchOffset (u * lengthSeconds, lengthSeconds));
            const auto x = x0 + (float) u * width;

            if (i == 0)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
        }

        g.setColour (shade);
        g.strokePath (curve, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (heads && ! n.legato)
        {
            // A picked note has a head; hammer-ons and pull-offs don't.
            const auto y = yOf ((float) n.pitch + n.pitchOffset (0.0, lengthSeconds));
            const auto size = thickness + 2.5f + 2.0f * level;
            g.setColour (colour.brighter (0.5f));
            g.fillEllipse (x0 - size * 0.35f, y - size * 0.5f, size, size);
        }
    }
}

void SoloView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (theme::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    if (bundle == nullptr || bundle->solo.lengthBeats <= 0.0)
        return;

    const auto& solo = bundle->solo;
    auto area = bounds.reduced (12.0f, 8.0f);
    const auto header = area.removeFromTop (20.0f);
    area.removeFromTop (4.0f);
    area.removeFromBottom (2.0f);

    const auto bars = (int) solo.chords.size();
    const auto barWidth = area.getWidth() / (float) std::max (1, bars);

    // The tonic, as a quiet line across each octave
    const auto range = (float) (highest - lowest);

    for (int pitch = lowest; pitch <= highest; ++pitch)
    {
        if (((pitch - solo.keyPc) % 12 + 12) % 12 == 0)
        {
            const auto y = area.getBottom() - (float) (pitch - lowest) / range * area.getHeight();
            g.setColour (theme::track.withAlpha (0.8f));
            g.fillRect (area.getX(), y - 0.5f, area.getWidth(), 1.0f);
        }
    }

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto x = area.getX() + barWidth * (float) bar;

        for (int beat = 1; beat < 4; ++beat)
        {
            g.setColour (theme::track.withAlpha (0.45f));
            g.fillRect (x + barWidth * (float) beat / 4.0f, area.getY(), 1.0f, area.getHeight());
        }

        g.setColour (theme::waveDim);
        g.fillRect (x, header.getY() + 2.0f, 1.0f, area.getBottom() - header.getY() - 2.0f);

        const auto changed = bar == 0 || solo.chords[(size_t) bar].getName() != solo.chords[(size_t) bar - 1].getName();
        g.setColour (changed ? theme::text : theme::textDim.withAlpha (0.5f));
        g.setFont (theme::font (bars > 12 ? 11.5f : 13.0f, changed));
        g.drawText (solo.chords[(size_t) bar].getName(), juce::Rectangle<float> (x + 6.0f, header.getY(), barWidth - 8.0f, header.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (theme::waveDim);
    g.fillRect (area.getRight() - 1.0f, header.getY() + 2.0f, 1.0f, area.getBottom() - header.getY() - 2.0f);

    if (bundle->harmony != 0)
        drawSolo (g, bundle->second, area, secondColour, 2.2f, false);

    drawSolo (g, solo, area, leadColour, 3.4f, true);

    if (position >= 0.0)
    {
        const auto x = area.getX() + (float) (position / solo.lengthBeats) * area.getWidth();
        g.setColour (theme::text.withAlpha (0.9f));
        g.fillRect (x - 0.75f, header.getY(), 1.5f, area.getBottom() - header.getY());
    }
}

//==============================================================================
ActionButton::ActionButton (const juce::String& label, Icon iconToUse, juce::Colour accentColour, bool isFilled)
    : juce::Button (label), icon (iconToUse), accent (accentColour), filled (isFilled)
{
    setButtonText (label);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ActionButton::setIcon (Icon newIcon)
{
    if (icon != newIcon)
    {
        icon = newIcon;
        repaint();
    }
}

void ActionButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto enabled = isEnabled();
    auto fill = filled ? accent : theme::knobBody;

    if (down)
        fill = fill.darker (0.25f);
    else if (highlighted && enabled)
        fill = fill.brighter (filled ? 0.12f : 0.2f);

    g.setColour (fill.withMultipliedAlpha (enabled ? 1.0f : 0.45f));
    g.fillRoundedRectangle (bounds, 8.0f);

    if (! filled)
    {
        g.setColour ((getToggleState() ? accent : theme::panelEdge).withMultipliedAlpha (enabled ? 1.0f : 0.5f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
    }

    const auto ink = (filled ? theme::background : (getToggleState() ? accent : theme::text)).withMultipliedAlpha (enabled ? 1.0f : 0.4f);
    const auto font = theme::font (filled ? 15.0f : 13.5f, filled);
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, getButtonText());
    const auto iconSize = icon == none ? 0.0f : 13.0f;
    const auto total = textWidth + (icon == none ? 0.0f : iconSize + 8.0f);
    auto x = bounds.getCentreX() - total * 0.5f;
    const auto cy = bounds.getCentreY();

    g.setColour (ink);

    if (icon != none)
    {
        const juce::Rectangle<float> box (x, cy - iconSize * 0.5f, iconSize, iconSize);
        juce::Path p;

        switch (icon)
        {
            case play:
                p.addTriangle (box.getX() + 1.5f, box.getY(), box.getRight(), cy, box.getX() + 1.5f, box.getBottom());
                g.fillPath (p);
                break;

            case stop:
                g.fillRoundedRectangle (box.reduced (1.0f), 2.0f);
                break;

            case back:
                p.startNewSubPath (box.getRight() - 3.0f, box.getY() + 1.0f);
                p.lineTo (box.getX() + 2.0f, cy);
                p.lineTo (box.getRight() - 3.0f, box.getBottom() - 1.0f);
                g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;

            case dice:
            {
                g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.6f);

                for (auto point : { juce::Point<float> (0.3f, 0.3f), { 0.7f, 0.7f }, { 0.7f, 0.3f }, { 0.3f, 0.7f } })
                    g.fillEllipse (box.getX() + box.getWidth() * point.x - 1.2f, box.getY() + box.getHeight() * point.y - 1.2f, 2.4f, 2.4f);

                break;
            }

            case save:
                p.startNewSubPath (box.getCentreX(), box.getY());
                p.lineTo (box.getCentreX(), box.getBottom() - 4.0f);
                p.startNewSubPath (box.getCentreX() - 4.0f, box.getBottom() - 8.0f);
                p.lineTo (box.getCentreX(), box.getBottom() - 4.0f);
                p.lineTo (box.getCentreX() + 4.0f, box.getBottom() - 8.0f);
                p.startNewSubPath (box.getX(), box.getBottom());
                p.lineTo (box.getRight(), box.getBottom());
                g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;

            case none:
                break;
        }

        x += iconSize + 8.0f;
    }

    g.setFont (font);
    g.drawText (getButtonText(), juce::Rectangle<float> (x, bounds.getY(), textWidth + 4.0f, bounds.getHeight()),
                juce::Justification::centredLeft, false);
}

//==============================================================================
DragHandle::DragHandle (const juce::String& textToShow, juce::Colour accentColour) : text (textToShow), accent (accentColour)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragHandle::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto enabled = isEnabled();
    const auto hot = enabled && (isMouseOver() || dragging);

    g.setColour (accent.withAlpha (enabled ? (hot ? 0.3f : 0.16f) : 0.05f));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (accent.withAlpha (enabled ? (hot ? 1.0f : 0.75f) : 0.25f));

    juce::Path outline;
    outline.addRoundedRectangle (bounds, 8.0f);
    const float dashes[] = { 5.0f, 4.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
    g.fillPath (dashed);

    // Grip
    const auto gripX = bounds.getX() + 13.0f;

    for (int column = 0; column < 2; ++column)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse (gripX + (float) column * 5.0f, bounds.getCentreY() - 6.5f + (float) row * 5.0f, 2.6f, 2.6f);

    g.setColour ((enabled ? theme::text : theme::textDim).withAlpha (enabled ? 1.0f : 0.5f));
    g.setFont (theme::font (13.0f, true));
    g.drawText (text, getLocalBounds().withTrimmedLeft (28).withTrimmedRight (6), juce::Justification::centred, false);
}

void DragHandle::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && isEnabled() && e.getDistanceFromDragStart() > 5)
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
SolistaEditor::Content::Content (SolistaProcessor& p)
    : processor (p),
      newSolo (pl ("Nowa solówka"), ActionButton::dice, leadColour, true),
      previous ("Poprzednia", ActionButton::back, leadColour, false),
      audition (pl ("Odsłuch"), ActionButton::play, leadColour, false),
      saveMidi ("Zapisz MIDI", ActionButton::save, theme::accentLfo, false),
      follow ({ "Tak", "Nie" }, leadColour),
      dragSolo (pl ("Przeciągnij MIDI"), leadColour),
      dragSecond ("MIDI 2. gitary", secondColour),
      key (getKeyNames(), leadColour),
      mode ({ "Moll", "Dur" }, leadColour),
      bars ({ "4", "8", "12", "16" }, leadColour),
      progressionMinor (getProgressionNames (true), theme::accentA),
      progressionMajor (getProgressionNames (false), theme::accentA),
      feel ({ "Prosto", "Shuffle" }, theme::accentA),
      style (getStyleNames(), leadColour),
      harmony ({ "Brak", pl ("Tercja wyżej"), pl ("Tercja niżej") }, secondColour),
      bendRange ({ pl ("±2"), pl ("±12"), pl ("±24") }, theme::accentLfo),
      density (p.apvts, ParamID::density, pl ("Gęstość"), theme::accentA),
      bends (p.apvts, ParamID::bends, "Bendy", theme::accentA),
      vibrato (p.apvts, ParamID::vibrato, "Vibrato", theme::accentA),
      syncopation (p.apvts, ParamID::syncopation, "Synkopy", theme::accentA),
      space (p.apvts, ParamID::space, "Oddech", theme::accentA),
      drive (p.apvts, ParamID::drive, "Przester", leadColour),
      tone (p.apvts, ParamID::tone, "Barwa", leadColour),
      ambience (p.apvts, ParamID::ambience, pl ("Pogłos"), leadColour),
      volume (p.apvts, ParamID::volume, pl ("Głośność"), leadColour)
{
    volume.slider.getProperties().set ("bipolar", false);

    auto attach = [&p] (gizmo::Segmented& control, const char* id)
    {
        if (auto* parameter = p.apvts.getParameter (id))
            control.attach (*parameter);
    };

    attach (key, ParamID::key);
    attach (mode, ParamID::mode);
    attach (bars, ParamID::bars);
    attach (progressionMinor, ParamID::progression);
    attach (progressionMajor, ParamID::progression);
    attach (feel, ParamID::feel);
    attach (style, ParamID::style);
    attach (harmony, ParamID::harmony);
    attach (bendRange, ParamID::bendRange);

    for (auto* c : std::initializer_list<juce::Component*> { &view, &newSolo, &previous, &audition, &saveMidi, &follow, &dragSolo,
                                                             &dragSecond, &key, &mode, &bars, &progressionMinor, &progressionMajor,
                                                             &feel, &style, &harmony, &bendRange, &density, &bends, &vibrato,
                                                             &syncopation, &space, &drive, &tone, &ambience, &volume })
        addAndMakeVisible (c);

    sections = { { pl ("SOLÓWKA"), { 16, 354, 968, 150 }, leadColour },
                 { "FRAZOWANIE", { 16, 512, 536, 128 }, theme::accentA },
                 { "GITARA", { 562, 512, 422, 128 }, leadColour },
                 { "MIDI I DRUGA GITARA", { 16, 648, 968, 62 }, theme::accentLfo } };
}

void SolistaEditor::Content::resized()
{
    view.setBounds (16, 58, 968, 236);

    newSolo.setBounds (16, 304, 190, 40);
    previous.setBounds (214, 304, 128, 40);
    audition.setBounds (350, 304, 118, 40);
    follow.setBounds (598, 310, 100, 28);
    dragSolo.setBounds (712, 304, 146, 40);
    dragSecond.setBounds (864, 304, 120, 40);

    key.setBounds (104, 378, 468, 28);
    mode.setBounds (584, 378, 130, 28);
    bars.setBounds (794, 378, 176, 28);
    progressionMinor.setBounds (104, 416, 690, 28);
    progressionMajor.setBounds (104, 416, 690, 28);
    feel.setBounds (806, 416, 164, 28);
    style.setBounds (104, 454, 468, 28);

    int x = 30;

    for (auto* k : { &density, &bends, &vibrato, &syncopation, &space })
    {
        k->setBounds (x, 534, 96, 100);
        x += 102;
    }

    x = 574;

    for (auto* k : { &drive, &tone, &ambience, &volume })
    {
        k->setBounds (x, 534, 96, 100);
        x += 101;
    }

    harmony.setBounds (124, 674, 300, 28);
    bendRange.setBounds (566, 674, 170, 28);
    saveMidi.setBounds (838, 670, 132, 34);
}

void SolistaEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    g.setColour (leadColour);
    g.setFont (theme::font (30.0f, true));
    g.drawText ("solista", juce::Rectangle<int> (16, 10, 120, 40), juce::Justification::centredLeft, false);
    g.setColour (theme::textDim);
    g.setFont (theme::font (13.0f));
    g.drawText (pl ("solówki gitarowe: bendy, vibrato, zabawa rytmem"), juce::Rectangle<int> (124, 20, 330, 24),
                juce::Justification::centredLeft, false);

    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText (tempoText, juce::Rectangle<int> (724, 20, 260, 24), juce::Justification::centredRight, false);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f));
    g.drawText (summaryText, juce::Rectangle<int> (454, 20, 300, 24), juce::Justification::centredLeft, false);

    for (const auto& s : sections)
    {
        const auto r = s.bounds.toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (s.accent);
        g.setFont (theme::font (12.5f, true));
        g.drawText (s.title, s.bounds.reduced (14, 6).removeFromTop (16), juce::Justification::centredLeft, false);
    }

    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f));
    g.drawText ("Graj z Abletonem", juce::Rectangle<int> (476, 310, 116, 28), juce::Justification::centredRight, false);

    g.drawText ("Tonacja", juce::Rectangle<int> (30, 378, 68, 28), juce::Justification::centredLeft, false);
    g.drawText (pl ("Długość"), juce::Rectangle<int> (722, 378, 66, 28), juce::Justification::centredRight, false);
    g.drawText ("Akordy", juce::Rectangle<int> (30, 416, 68, 28), juce::Justification::centredLeft, false);
    g.drawText ("Styl", juce::Rectangle<int> (30, 454, 68, 28), juce::Justification::centredLeft, false);
    g.drawText (styleHint (styleIndex), juce::Rectangle<int> (586, 454, 384, 28), juce::Justification::centredLeft, false);

    g.drawText ("Druga gitara", juce::Rectangle<int> (30, 674, 90, 28), juce::Justification::centredLeft, false);
    g.drawText ("Zakres pitch bend", juce::Rectangle<int> (440, 674, 120, 28), juce::Justification::centredRight, false);

    g.setColour (statusText.isNotEmpty() ? leadColour : theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText (statusText.isNotEmpty() ? statusText
                                        : pl ("Ustaw ten sam zakres pitch bend w instrumencie, który zagra klip"),
                juce::Rectangle<int> (300, 652, 670, 18), juce::Justification::centredRight, false);
}

//==============================================================================
SolistaEditor::SolistaEditor (SolistaProcessor& p)
    : AudioProcessorEditor (p), processor (p), content (p)
{
    // Add the content first, so the look-and-feel change reaches every control inside it.
    addAndMakeVisible (content);
    setLookAndFeel (&lookAndFeel);

    content.newSolo.onClick = [this] { processor.newSolo(); };
    content.previous.onClick = [this] { processor.previousSolo(); };
    content.audition.onClick = [this] { processor.setAudition (! processor.isAuditioning()); };
    content.saveMidi.onClick = [this] { saveMidiFile(); };
    content.follow.onChange = [this] (int index) { processor.setFollowHost (index == 0); };
    content.dragSolo.onDrag = [this] { dragMidi (false, content.dragSolo); };
    content.dragSecond.onDrag = [this] { dragMidi (true, content.dragSecond); };

    processor.addChangeListener (this);
    updateDisplay();

    setResizable (true, true);
    setResizeLimits (700, 504, 1500, 1080);
    getConstrainer()->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    setSize (900, 648);
    startTimerHz (30);
}

SolistaEditor::~SolistaEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void SolistaEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void SolistaEditor::resized()
{
    const auto scale = (float) getWidth() / (float) kDesignWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, kDesignWidth, kDesignHeight);
}

void SolistaEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateDisplay();
}

void SolistaEditor::timerCallback()
{
    updateDisplay();
}

void SolistaEditor::updateDisplay()
{
    auto& c = content;
    const auto bundle = processor.getBundle();

    if (bundle != nullptr)
    {
        c.view.setBundle (bundle);
        int bent = 0, shaken = 0;

        for (const auto& n : bundle->solo.notes)
        {
            bent += n.art.bend != BendKind::none ? 1 : 0;
            shaken += n.art.vibrato.depth > 0.0f ? 1 : 0;
        }

        c.summaryText = juce::String ((int) bundle->solo.notes.size()) + " nut  /  " + juce::String (bent) + pl (" bendów  /  ")
                      + juce::String (shaken) + pl (" z vibrato");
    }

    c.view.setPlayPosition (processor.getPlayPosition());
    c.tempoText = juce::String (processor.getTempo(), 1) + " BPM z Abletona";

    const auto minor = juce::roundToInt (processor.apvts.getRawParameterValue (ParamID::mode)->load()) == 0;
    c.progressionMinor.setVisible (minor);
    c.progressionMajor.setVisible (! minor);
    c.styleIndex = juce::roundToInt (processor.apvts.getRawParameterValue (ParamID::style)->load());
    c.hasSecond = juce::roundToInt (processor.apvts.getRawParameterValue (ParamID::harmony)->load()) != 0;
    c.dragSecond.setEnabled (c.hasSecond);

    c.previous.setEnabled (processor.hasPreviousSolo());
    c.audition.setToggleState (processor.isAuditioning(), juce::dontSendNotification);
    c.audition.setIcon (processor.isAuditioning() ? ActionButton::stop : ActionButton::play);
    c.audition.setButtonText (processor.isAuditioning() ? "Stop" : pl ("Odsłuch"));
    c.follow.setSelected (processor.isFollowingHost() ? 0 : 1);

    if (messageUntil != 0 && juce::Time::getMillisecondCounter() > messageUntil)
    {
        messageUntil = 0;
        c.statusText.clear();
    }

    c.repaint (0, 0, kDesignWidth, 56);
    c.repaint (0, 440, kDesignWidth, 60);
    c.repaint (0, 648, kDesignWidth, 24);
}

void SolistaEditor::dragMidi (bool second, juce::Component& source)
{
    const auto file = processor.writeMidiFile (second);

    if (file == juce::File())
    {
        content.statusText = pl ("Nie udało się zapisać pliku MIDI w folderze tymczasowym");
        messageUntil = juce::Time::getMillisecondCounter() + 6000;
        return;
    }

    juce::Component::SafePointer<SolistaEditor> safe (this);

    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, &source, [safe]
    {
        if (safe != nullptr)
        {
            // The clip plays the solo from now on; the built-in preview would double it.
            safe->processor.setFollowHost (false);
            safe->processor.setAudition (false);
            safe->content.statusText = pl ("Podgląd wyłączony: teraz solówkę gra klip MIDI. Włącz „Graj z Abletonem”, by znów słuchać generatora");
            safe->messageUntil = juce::Time::getMillisecondCounter() + 12000;
        }
    });
}

void SolistaEditor::saveMidiFile()
{
    const auto source = processor.writeMidiFile (false);

    if (source == juce::File())
        return;

    const auto second = content.hasSecond ? processor.writeMidiFile (true) : juce::File();
    const auto start = juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (source.getFileName());
    chooser = std::make_unique<juce::FileChooser> ("Zapisz MIDI", start, "*.mid");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, source, second] (const juce::FileChooser& fc)
                          {
                              const auto target = fc.getResult();

                              if (target == juce::File())
                                  return;

                              const auto file = target.withFileExtension ("mid");
                              source.copyFileTo (file);

                              if (second != juce::File())
                                  second.copyFileTo (file.getSiblingFile (file.getFileNameWithoutExtension() + " - gitara 2.mid"));

                              content.statusText = "Zapisano: " + file.getFileName();
                              messageUntil = juce::Time::getMillisecondCounter() + 6000;
                          });
}
