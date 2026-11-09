#include "WavetableView.h"
#include "../Params.h"
#include "Theme.h"

namespace gizmo
{
WavetableView::WavetableView (const juce::String& name, juce::Colour accentColour, const juce::String& hint)
    : slotName (name), emptyHint (hint), accent (accentColour)
{
    voices.fill (-1.0f);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void WavetableView::setTable (const Wavetable* table, bool followsOtherSlot)
{
    followsOther = followsOtherSlot;
    hasTable = table != nullptr;
    preview.assign ((size_t) kNumFrames * kPoints, 0.0f);

    if (table != nullptr)
    {
        tableName = table->name;
        pitchedFrames = table->pitchedFrames;

        for (int f = 0; f < kNumFrames; ++f)
        {
            const auto* frame = table->getFrame (f);

            for (int i = 0; i < kPoints; ++i)
                preview[(size_t) (f * kPoints + i)] = frame[i * kFrameSize / kPoints];
        }
    }
    else
    {
        tableName.clear();
        pitchedFrames = 0;
    }

    cacheDirty = true;
    repaint();
}

void WavetableView::setStatus (bool isLoading, const juce::String& errorText)
{
    if (loading != isLoading || error != errorText)
    {
        loading = isLoading;
        error = errorText;
        repaint();
    }
}

void WavetableView::setPosition (float newPosition, bool isStepped)
{
    if (std::abs (newPosition - position) > 1.0e-4f || stepped != isStepped)
    {
        position = newPosition;
        stepped = isStepped;
        repaint();
    }
}

void WavetableView::setVoicePositions (const std::array<float, kMaxMarkers>& positions)
{
    if (positions != voices)
    {
        voices = positions;
        repaint();
    }
}

void WavetableView::setDragHover (bool shouldHighlight)
{
    if (dragHover != shouldHighlight)
    {
        dragHover = shouldHighlight;
        repaint();
    }
}

void WavetableView::resized()
{
    cacheDirty = true;
}

juce::Rectangle<float> WavetableView::getPlotArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 10.0f).withTrimmedTop (22.0f).withTrimmedBottom (14.0f);
}

juce::Rectangle<float> WavetableView::getClearButtonBounds() const
{
    return { (float) getWidth() - 30.0f, 7.0f, 20.0f, 20.0f };
}

juce::Path WavetableView::framePath (float framePosition) const
{
    const auto area = getPlotArea();
    const auto lastFrame = (float) (kNumFrames - 1);
    const auto f = juce::jlimit (0.0f, lastFrame, framePosition);
    const auto k0 = juce::jmin ((int) f, kNumFrames - 2);
    const auto t = f - (float) k0;

    const auto width = area.getWidth() * 0.74f;
    const auto amplitude = area.getHeight() * 0.2f;
    const auto dx = (area.getWidth() - width) / lastFrame;
    const auto dy = (area.getHeight() - amplitude * 2.0f) / lastFrame;
    const auto x0 = area.getX() + f * dx;
    const auto y0 = area.getBottom() - amplitude - f * dy;

    juce::Path p;

    for (int i = 0; i <= kPoints; ++i)
    {
        const auto idx = i % kPoints;
        const auto a = preview[(size_t) (k0 * kPoints + idx)];
        const auto b = preview[(size_t) ((k0 + 1) * kPoints + idx)];
        const auto v = a + t * (b - a);
        const auto x = x0 + width * (float) i / (float) kPoints;
        const auto y = y0 - v * amplitude;

        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }

    return p;
}

void WavetableView::renderCache (float scale)
{
    const auto w = juce::jmax (1, juce::roundToInt ((float) getWidth() * scale));
    const auto h = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));
    cache = juce::Image (juce::Image::ARGB, w, h, true);
    cacheScale = scale;
    cacheDirty = false;

    if (! hasTable || preview.empty())
        return;

    juce::Graphics g (cache);
    g.addTransform (juce::AffineTransform::scale (scale));

    for (int f = kNumFrames - 1; f >= 0; --f)
    {
        const auto depth = (float) f / (float) (kNumFrames - 1);
        g.setColour (theme::waveDim.interpolatedWith (theme::panel, depth * 0.55f));
        g.strokePath (framePath ((float) f), juce::PathStrokeType (1.0f));
    }
}

void WavetableView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (dragHover ? accent : theme::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.75f), 10.0f, dragHover ? 2.0f : 1.0f);

    // Header: slot name, table name, frame counter
    auto header = bounds.reduced (14.0f, 8.0f).removeFromTop (18.0f);
    g.setFont (theme::font (14.0f, true));
    g.setColour (accent);
    g.drawText (slotName, header.removeFromLeft (22.0f), juce::Justification::centredLeft, false);

    const auto showClear = hasTable && ! followsOther && onClear != nullptr;

    if (showClear)
        header.removeFromRight (26.0f);

    const auto lastFrame = (float) (kNumFrames - 1);
    const auto framePos = juce::jlimit (0.0f, 1.0f, position) * lastFrame;
    const auto shownFrame = stepped ? (float) juce::roundToInt (framePos) : framePos;

    g.setFont (theme::font (12.0f));
    g.setColour (theme::textDim);
    const auto counter = pl ("klatka ") + juce::String (juce::roundToInt (shownFrame) + 1) + " / " + juce::String (kNumFrames);
    g.drawText (counter, header.removeFromRight (90.0f), juce::Justification::centredRight, false);

    g.setColour (followsOther ? theme::textDim : theme::text);
    g.setFont (theme::font (12.5f));
    g.drawText (followsOther ? pl ("gra tablicę 1") : tableName, header, juce::Justification::centredLeft, true);

    if (showClear)
    {
        const auto r = getClearButtonBounds();
        g.setColour (clearHover ? theme::text : theme::textDim);
        g.drawLine (r.getX() + 6.0f, r.getY() + 6.0f, r.getRight() - 6.0f, r.getBottom() - 6.0f, 1.5f);
        g.drawLine (r.getRight() - 6.0f, r.getY() + 6.0f, r.getX() + 6.0f, r.getBottom() - 6.0f, 1.5f);
    }

    // Waterfall
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();

    if (cacheDirty || std::abs (scale - cacheScale) > 0.01f || cache.isNull())
        renderCache (scale);

    g.drawImage (cache, bounds, juce::RectanglePlacement::stretchToFit);

    if (hasTable)
    {
        for (auto v : voices)
        {
            if (v < 0.0f)
                continue;

            const auto vf = v * lastFrame;

            if (std::abs (vf - framePos) > 0.5f)
            {
                g.setColour (accent.withAlpha (0.35f));
                g.strokePath (framePath (stepped ? (float) juce::roundToInt (vf) : vf), juce::PathStrokeType (1.2f));
            }
        }

        g.setColour (accent);
        g.strokePath (framePath (shownFrame), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                                    juce::PathStrokeType::rounded));
    }

    // Footer: hint, loading state or error
    auto footer = bounds.reduced (14.0f, 6.0f).removeFromBottom (16.0f);
    g.setFont (theme::font (11.5f));

    if (loading)
    {
        g.setColour (accent);
        g.drawText (pl ("Analizuję sampel…"), footer, juce::Justification::centredLeft, true);
    }
    else if (error.isNotEmpty())
    {
        g.setColour (theme::error);
        g.drawText (error, footer, juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (theme::textDim.withAlpha (0.8f));
        g.drawText (emptyHint, footer, juce::Justification::centredLeft, true);
    }
}

void WavetableView::mouseMove (const juce::MouseEvent& e)
{
    const auto over = getClearButtonBounds().contains (e.position) && hasTable && ! followsOther && onClear != nullptr;

    if (over != clearHover)
    {
        clearHover = over;
        repaint();
    }
}

void WavetableView::mouseExit (const juce::MouseEvent&)
{
    if (clearHover)
    {
        clearHover = false;
        repaint();
    }
}

void WavetableView::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;

    if (hasTable && ! followsOther && onClear != nullptr && getClearButtonBounds().contains (e.position))
    {
        onClear();
        return;
    }

    if (onBrowse)
        onBrowse();
}
} // namespace gizmo
