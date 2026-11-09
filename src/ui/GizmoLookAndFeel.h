#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace gizmo
{
class GizmoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GizmoLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
};
} // namespace gizmo
