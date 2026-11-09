#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace gizmo
{
/** A rotary knob with a title above and the value below, attached to a parameter.
    An optional badge shows which Fizmo knob it corresponds to. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& title,
          juce::Colour accent, const juce::String& badge = {});

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::Label label;
    juce::String badge;
    juce::Colour accent;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** A row of mutually exclusive buttons, either bound to a choice parameter or used directly. */
class Segmented : public juce::Component
{
public:
    Segmented (const juce::StringArray& options, juce::Colour accent);

    void attach (juce::RangedAudioParameter& parameter);
    void setSelected (int index);
    int getSelected() const noexcept { return selected; }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray options;
    juce::Colour accent;
    int selected = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
};

/** Sixteen A/B steps: click to flip a step, drag to paint, the playing step lights up. */
class PatternEditor : public juce::Component
{
public:
    PatternEditor (juce::Colour colourA, juce::Colour colourB);

    void setPattern (juce::uint32 bits, int length);
    void setPlayingStep (int step);
    void setActive (bool isActive);

    std::function<void (juce::uint32)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    int stepAt (float x) const;
    void paintStep (int step);

    juce::uint32 bits = 0;
    int length = 8, playing = -1, lastPainted = -1;
    bool active = true, paintAsB = false;
    juce::Colour colourA, colourB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternEditor)
};
} // namespace gizmo
