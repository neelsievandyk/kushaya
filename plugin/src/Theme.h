// =============================================================================
// Theme.h
//
// Every colour and font the interface uses, in one place.
// To restyle the plugin, change the values here and rebuild.
//
// Colours are written as 0xAARRGGBB: two hex digits each for alpha
// (ff = solid), red, green and blue.
// =============================================================================

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{

// Off-whites: the window, the raised cards on it, and the fine lines.
inline const juce::Colour background { 0xfff6f2e9 };
inline const juce::Colour card       { 0xfffffdf9 };
inline const juce::Colour outline    { 0xffe7dfcf };
inline const juce::Colour track      { 0xffebe4d5 };

// Text.
inline const juce::Colour ink        { 0xff2e2b27 };
inline const juce::Colour muted      { 0xff9b9385 };

// Light orange: the main accent (pad flashes, knob values, the meter).
inline const juce::Colour orange     { 0xfff8a95e };
inline const juce::Colour orangeSoft { 0xfffbdfc3 };

// Blue: small highlights only (the newest pad, pointers, the threshold).
inline const juce::Colour blue       { 0xff3d7cf5 };

// The typeface. "Avenir Next" ships with every Mac.
// style is one of "Regular", "Medium", "Demi Bold", "Bold".
// spacing widens the gaps between letters (0 = normal).
inline juce::Font font (float size, const juce::String& style = "Medium", float spacing = 0.0f)
{
    return juce::Font (juce::FontOptions ("Avenir Next", style, size).withKerningFactor (spacing));
}

} // namespace theme
