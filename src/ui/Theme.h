#pragma once

#include <juce_graphics/juce_graphics.h>

namespace gizmo::theme
{
inline const juce::Colour background   { 0xff131118 };
inline const juce::Colour panel        { 0xff1c1922 };
inline const juce::Colour panelEdge    { 0xff2b2733 };
inline const juce::Colour knobBody     { 0xff27232f };
inline const juce::Colour track        { 0xff332e3d };
inline const juce::Colour text         { 0xffece8f4 };
inline const juce::Colour textDim      { 0xff8e87a1 };
inline const juce::Colour waveDim      { 0xff4c4659 };
inline const juce::Colour error        { 0xffff6b6b };

inline const juce::Colour accentA      { 0xffa48bff }; // violet, a nod to the Fizmo's panel
inline const juce::Colour accentB      { 0xff3cc6ab }; // teal
inline const juce::Colour accentFilter { 0xffff9f5a }; // orange
inline const juce::Colour accentLfo    { 0xff5fb2ff }; // blue
inline const juce::Colour accentVoice  { 0xffb7aecb }; // soft grey-violet
inline const juce::Colour accentFragment { 0xffff7aa8 }; // pink, the B steps of the sequence

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace gizmo::theme
