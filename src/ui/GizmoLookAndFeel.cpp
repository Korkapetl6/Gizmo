#include "GizmoLookAndFeel.h"
#include "Theme.h"

namespace gizmo
{
GizmoLookAndFeel::GizmoLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::Slider::textBoxTextColourId, theme::textDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, theme::accentA.withAlpha (0.35f));
    setColour (juce::TextEditor::textColourId, theme::text);
    setColour (juce::TextEditor::backgroundColourId, theme::knobBody);
    setColour (juce::TextEditor::highlightColourId, theme::accentA.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::accentA);
    setColour (juce::CaretComponent::caretColourId, theme::text);
    setColour (juce::PopupMenu::backgroundColourId, theme::panel);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accentA.withAlpha (0.3f));
}

void GizmoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto area = bounds.withSizeKeepingCentre (size, size);
    const auto stroke = juce::jmax (2.5f, size * 0.075f);
    const auto radius = size * 0.5f - stroke * 0.5f;
    const auto centre = area.getCentre();
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto arcStroke = juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackPath;
    trackPath.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::track);
    g.strokePath (trackPath, arcStroke);

    const auto bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
    const auto valueAngle = startAngle + sliderPos * (endAngle - startAngle);
    const auto fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (valueAngle - fromAngle) > 0.001f)
    {
        juce::Path valuePath;
        valuePath.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (fromAngle, valueAngle),
                                 juce::jmax (fromAngle, valueAngle), true);
        g.setColour (slider.isEnabled() ? accent : accent.withAlpha (0.3f));
        g.strokePath (valuePath, arcStroke);
    }

    const auto bodyRadius = radius - stroke * 1.5f;
    g.setColour (theme::knobBody);
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);

    const auto tip = centre.getPointOnCircumference (bodyRadius * 0.82f, valueAngle);
    const auto inner = centre.getPointOnCircumference (bodyRadius * 0.3f, valueAngle);
    g.setColour (theme::text);
    g.drawLine ({ inner, tip }, juce::jmax (1.6f, stroke * 0.55f));
}

juce::Label* GizmoLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (theme::font (11.5f));
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::textColourId, theme::textDim);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return label;
}

juce::Font GizmoLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}
} // namespace gizmo
