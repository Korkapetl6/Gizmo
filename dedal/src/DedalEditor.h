#pragma once

#include "DedalProcessor.h"
#include "ui/Controls.h"
#include "ui/GizmoLookAndFeel.h"

namespace dedal
{
/** A plain button in the plugin's style; with 'toggle' it shows its state in the accent colour. */
class Button : public juce::Button
{
public:
    Button (const juce::String& text, juce::Colour accent, bool filled = false);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    juce::Colour accent;
    bool filled;
};

/** The bits of SEQ1 or SEQ2 (8 cells) or SEQ C (16 cells), with the play and write heads. */
class StepGrid : public juce::Component
{
public:
    StepGrid (DedalProcessor&, int row);

    void setSnapshot (const Snapshot& s) { snapshot = s; repaint(); }
    void setEditMode (int mode) { editMode = mode; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int cellAt (juce::Point<float>) const;
    juce::Rectangle<float> cellBounds (int i) const;
    int numCells() const noexcept { return row == 2 ? kStepsC : kSteps; }

    DedalProcessor& processor;
    const int row; // 0 SEQ1, 1 SEQ2, 2 SEQ C
    Snapshot snapshot;
    int editMode = 0; // SEQ C: 0 steps and chance, 1 accents, 2 values
    int pressed = -1;
    float startValue = 0.0f;
    bool dragged = false;
};

/** A handle you drag out of the plugin onto a track: it carries a MIDI file. */
class DragHandle : public juce::Component
{
public:
    DragHandle (const juce::String& text, juce::Colour accent);

    std::function<void()> onDrag;

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String text;
    juce::Colour accent;
    bool dragging = false;
};
} // namespace dedal

//==============================================================================
class DedalEditor : public juce::AudioProcessorEditor,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    static constexpr int kDesignWidth = 1200;
    static constexpr int kDesignHeight = 952;

    explicit DedalEditor (DedalProcessor&);
    ~DedalEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showPage (int page);
    void updateDisplay();

private:
    class Content;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override { updateDisplay(); }

    DedalProcessor& processor;
    gizmo::GizmoLookAndFeel lookAndFeel;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DedalEditor)
};
