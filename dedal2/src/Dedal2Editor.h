#pragma once

#include "Dedal2Processor.h"
#include "ui/Controls.h"
#include "ui/GizmoLookAndFeel.h"

class Dedal2Editor : public juce::AudioProcessorEditor, private juce::ChangeListener, private juce::Timer
{
public:
    static constexpr int kDesignWidth = 1280;
    static constexpr int kDesignHeight = 752;

    explicit Dedal2Editor (Dedal2Processor&);
    ~Dedal2Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void updateDisplay();

private:
    class Content;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateDisplay(); }
    void timerCallback() override { updateDisplay(); }

    Dedal2Processor& processor;
    gizmo::GizmoLookAndFeel lookAndFeel;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Dedal2Editor)
};
