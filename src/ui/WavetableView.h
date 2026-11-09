#pragma once

#include "../dsp/Wavetable.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace gizmo
{
/** Draws the 64 frames as a waterfall, highlights the current position, and accepts clicks. */
class WavetableView : public juce::Component
{
public:
    static constexpr int kMaxMarkers = 16;

    WavetableView (const juce::String& slotName, juce::Colour accent, const juce::String& emptyHint);

    /** Copies what is needed for drawing, so the table may be freed afterwards. */
    void setTable (const Wavetable* table, bool followsOtherSlot);
    void setStatus (bool isLoading, const juce::String& errorText);
    void setPosition (float position, bool stepped);
    void setVoicePositions (const std::array<float, kMaxMarkers>& positions);
    void setDragHover (bool shouldHighlight);

    std::function<void()> onBrowse;
    std::function<void()> onClear;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    static constexpr int kPoints = 128;

    juce::Rectangle<float> getPlotArea() const;
    juce::Path framePath (float framePosition) const;
    juce::Rectangle<float> getClearButtonBounds() const;
    void renderCache (float scale);

    juce::String slotName, emptyHint, tableName, error;
    juce::Colour accent;
    std::vector<float> preview; // kNumFrames * kPoints
    bool hasTable = false, followsOther = false, loading = false, dragHover = false, clearHover = false;
    int pitchedFrames = 0;
    float position = 0.0f;
    bool stepped = false;
    std::array<float, kMaxMarkers> voices {};

    juce::Image cache;
    float cacheScale = 0.0f;
    bool cacheDirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableView)
};
} // namespace gizmo
