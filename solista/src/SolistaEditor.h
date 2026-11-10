#pragma once

#include "SolistaProcessor.h"
#include "ui/Controls.h"
#include "ui/GizmoLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace solista
{
/** The solo drawn as a guitarist would hear it: every note is its real pitch curve, so bends rise,
    vibrato wobbles and slides lean into the note. */
class SoloView : public juce::Component
{
public:
    void setBundle (std::shared_ptr<const SoloBundle> newBundle);
    void setPlayPosition (double beats);
    void paint (juce::Graphics&) override;

private:
    void drawSolo (juce::Graphics&, const Solo&, juce::Rectangle<float> area, juce::Colour, float thickness, bool heads) const;

    std::shared_ptr<const SoloBundle> bundle;
    double position = -1.0;
    int lowest = 55, highest = 80;
};

/** A button drawn in the plugin's style, with an optional small icon. */
class ActionButton : public juce::Button
{
public:
    enum Icon { none, play, stop, back, dice, save };

    ActionButton (const juce::String& text, Icon, juce::Colour accent, bool filled);
    void setIcon (Icon newIcon);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    juce::Colour accent;
    bool filled;
};

/** A handle you drag out of the plugin onto a track: it carries a MIDI file. */
class DragHandle : public juce::Component
{
public:
    DragHandle (const juce::String& text, juce::Colour accent);

    std::function<void()> onDrag;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String text;
    juce::Colour accent;
    bool dragging = false;
};
} // namespace solista

//==============================================================================
class SolistaEditor : public juce::AudioProcessorEditor,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    static constexpr int kDesignWidth = 1000;
    static constexpr int kDesignHeight = 720;

    explicit SolistaEditor (SolistaProcessor&);
    ~SolistaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Pulls the solo, the tempo and the playback position from the processor (called by a timer). */
    void updateDisplay();

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (SolistaProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

        SolistaProcessor& processor;
        solista::SoloView view;
        solista::ActionButton newSolo, previous, audition, saveMidi;
        gizmo::Segmented follow;
        solista::DragHandle dragSolo, dragSecond;
        gizmo::Segmented key, mode, bars, progressionMinor, progressionMajor, feel, style, harmony, bendRange;
        gizmo::Knob density, bends, vibrato, syncopation, space;
        gizmo::Knob drive, tone, ambience, volume;
        juce::String tempoText, statusText, summaryText;
        int styleIndex = 0;
        bool hasSecond = false;

    private:
        struct Section
        {
            juce::String title;
            juce::Rectangle<int> bounds;
            juce::Colour accent;
        };

        std::vector<Section> sections;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void dragMidi (bool second, juce::Component& source);
    void saveMidiFile();

    SolistaProcessor& processor;
    gizmo::GizmoLookAndFeel lookAndFeel;
    Content content;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::uint32 messageUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolistaEditor)
};
