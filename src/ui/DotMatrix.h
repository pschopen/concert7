// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace concert7
{
/** Classic 5x8 dot matrix font renderer (ASCII 32..126). */
namespace DotMatrix
{
    /** Width in pixels of the text for a dot pitch (6 columns per character). */
    float textWidth (const juce::String& text, float pitch);

    /** Draws text; unlit dots of every cell are drawn with offColour (can be transparent). */
    void drawText (juce::Graphics& g, const juce::String& text, float x, float y, float pitch,
                   juce::Colour on, juce::Colour off, bool glow = true);
}
} // namespace concert7
