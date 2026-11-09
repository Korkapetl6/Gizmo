#pragma once

#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/GizmoLookAndFeel.h"
#include "ui/WavetableView.h"

#include <juce_audio_processors/juce_audio_processors.h>

class GizmoEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    static constexpr int kDesignWidth = 1000;
    static constexpr int kDesignHeight = 728;

    explicit GizmoEditor (GizmoProcessor&);
    ~GizmoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Pulls the current position and voices from the engine (called by a timer). */
    void updateDisplay();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    /** Everything is laid out at a fixed design size and scaled as a whole. */
    class Content : public juce::Component
    {
    public:
        explicit Content (GizmoProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

        GizmoProcessor& processor;
        gizmo::WavetableView viewA, viewB;
        gizmo::Segmented importMode, character, voiceMode, frameMode, filterOn, lfoShape, lfoSync, lfoRetrig;
        gizmo::Knob position, velToPos, wheelToPos;
        gizmo::Knob levelA, panA, levelB, panB, semiB, offsetB, detune;
        gizmo::Knob glide, bendRange, gain;
        gizmo::Knob cutoff, resonance, keyTrack, filterEnv, fAttack, fDecay, fSustain, fRelease;
        gizmo::Knob attack, decay, sustain, release, velToAmp;
        gizmo::Knob lfoRate, lfoDivision, lfoToPos, lfoToCutoff, lfoToPitch;

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
    void refreshTables();
    void browseFor (GizmoProcessor::Slot slot);
    GizmoProcessor::Slot slotAt (int x, int y);
    void updateDragHover (int x, int y);

    GizmoProcessor& processor;
    gizmo::GizmoLookAndFeel lookAndFeel;
    Content content;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GizmoEditor)
};
