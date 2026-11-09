#include "Controls.h"
#include "Theme.h"

namespace gizmo
{
Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& title,
            juce::Colour accentColour, const juce::String& badgeText)
    : badge (badgeText), accent (accentColour)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (slider);

    if (auto* parameter = state.getParameter (paramId))
    {
        const auto range = parameter->getNormalisableRange();
        slider.getProperties().set ("bipolar", range.start < 0.0f && range.end > 0.0f);
    }

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    label.setText (title, juce::dontSendNotification);
    label.setFont (theme::font (12.5f));
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, theme::text);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (16));
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, area.getWidth(), 16);
    slider.setBounds (area);
}

void Knob::paint (juce::Graphics& g)
{
    if (badge.isEmpty())
        return;

    // The Fizmo letter sits at the top right of the dial.
    const auto dial = slider.getBounds().withTrimmedBottom (16);
    const auto size = (float) juce::jmin (dial.getWidth(), dial.getHeight());
    const auto badgeSize = juce::jlimit (13.0f, 18.0f, size * 0.22f);
    const auto centre = dial.toFloat().getCentre();
    const juce::Rectangle<float> r (centre.x + size * 0.5f - badgeSize * 0.4f, (float) dial.getY(), badgeSize, badgeSize);

    g.setColour (accent);
    g.fillEllipse (r);
    g.setColour (theme::background);
    g.setFont (theme::font (badgeSize * 0.7f, true));
    g.drawText (badge, r, juce::Justification::centred, false);
}

//==============================================================================
Segmented::Segmented (const juce::StringArray& opts, juce::Colour accentColour)
    : options (opts), accent (accentColour)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Segmented::attach (juce::RangedAudioParameter& parameter)
{
    attachment = std::make_unique<juce::ParameterAttachment> (parameter, [this] (float value)
    {
        selected = juce::jlimit (0, options.size() - 1, juce::roundToInt (value));
        repaint();

        if (onChange)
            onChange (selected);
    });

    attachment->sendInitialUpdate();
}

void Segmented::setSelected (int index)
{
    selected = juce::jlimit (0, options.size() - 1, index);
    repaint();
}

void Segmented::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = bounds.getHeight() * 0.5f;

    g.setColour (theme::knobBody);
    g.fillRoundedRectangle (bounds, corner);

    const auto segmentWidth = bounds.getWidth() / (float) options.size();
    g.setFont (theme::font (juce::jmin (12.5f, bounds.getHeight() * 0.55f)));

    for (int i = 0; i < options.size(); ++i)
    {
        const auto r = juce::Rectangle<float> (bounds.getX() + segmentWidth * (float) i, bounds.getY(), segmentWidth,
                                               bounds.getHeight());

        if (i == selected)
        {
            g.setColour (accent.withAlpha (0.22f));
            g.fillRoundedRectangle (r.reduced (1.5f), corner - 1.5f);
            g.setColour (accent);
            g.drawRoundedRectangle (r.reduced (1.5f), corner - 1.5f, 1.0f);
        }

        g.setColour (i == selected ? theme::text : theme::textDim);
        g.drawText (options[i], r, juce::Justification::centred, false);
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const auto index = juce::jlimit (0, options.size() - 1, (int) ((float) e.x / (float) getWidth() * (float) options.size()));

    if (attachment != nullptr)
    {
        attachment->setValueAsCompleteGesture ((float) index);
        return;
    }

    if (index != selected)
    {
        selected = index;
        repaint();

        if (onChange)
            onChange (selected);
    }
}
//==============================================================================
PatternEditor::PatternEditor (juce::Colour a, juce::Colour b) : colourA (a), colourB (b)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PatternEditor::setPattern (juce::uint32 newBits, int newLength)
{
    if (newBits != bits || newLength != length)
    {
        bits = newBits;
        length = newLength;
        repaint();
    }
}

void PatternEditor::setPlayingStep (int step)
{
    if (step != playing)
    {
        playing = step;
        repaint();
    }
}

void PatternEditor::setActive (bool isActive)
{
    if (isActive != active)
    {
        active = isActive;
        repaint();
    }
}

int PatternEditor::stepAt (float x) const
{
    return juce::jlimit (0, 15, (int) (x / (float) getWidth() * 16.0f));
}

void PatternEditor::paint (juce::Graphics& g)
{
    const auto cellWidth = (float) getWidth() / 16.0f;
    const auto height = (float) getHeight();

    for (int i = 0; i < 16; ++i)
    {
        const auto isB = ((bits >> i) & 1u) != 0;
        const auto inUse = i < length;
        const auto colour = isB ? colourB : colourA;
        const auto r = juce::Rectangle<float> (cellWidth * (float) i, 0.0f, cellWidth, height).reduced (2.0f, 1.0f);
        const auto alpha = (inUse ? 1.0f : 0.25f) * (active ? 1.0f : 0.5f);

        g.setColour (colour.withAlpha ((isB ? 0.32f : 0.14f) * alpha));
        g.fillRoundedRectangle (r, 5.0f);

        const auto isPlaying = active && inUse && i == playing;
        g.setColour (colour.withAlpha ((isPlaying ? 1.0f : 0.55f) * alpha));
        g.drawRoundedRectangle (r, 5.0f, isPlaying ? 2.0f : 1.0f);

        g.setColour ((isPlaying ? juce::Colours::white : colour).withAlpha (alpha));
        g.setFont (theme::font (14.0f, true));
        g.drawText (isB ? "B" : "A", r, juce::Justification::centred, false);

        if (i % 4 == 0)
        {
            g.setColour (theme::textDim.withAlpha (0.5f * alpha));
            g.fillRect (r.getX(), height - 1.0f, 1.5f, 1.0f);
        }
    }
}

void PatternEditor::paintStep (int step)
{
    if (step == lastPainted)
        return;

    lastPainted = step;
    const auto mask = 1u << step;
    const auto newBits = paintAsB ? (bits | mask) : (bits & ~mask);

    if (newBits != bits)
    {
        bits = newBits;
        repaint();

        if (onChange)
            onChange (bits);
    }
}

void PatternEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto step = stepAt (e.position.x);
    paintAsB = ((bits >> step) & 1u) == 0; // flip the clicked step, then paint the same value while dragging
    lastPainted = -1;
    paintStep (step);
}

void PatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    paintStep (stepAt (e.position.x));
}
} // namespace gizmo
