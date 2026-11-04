// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "Logo.h"
#include "Theme.h"

namespace concert7
{
using namespace juce;

namespace
{
    constexpr float kSize = 34.0f;      // word mark font size
    constexpr float kTracking = 0.18f;  // letter spacing of the word mark

    // The 7: bold, 100/68 of the word size, 6 px after the word's own letter spacing,
    // its ink centred on the capitals (values from the logo design, made at 2x)
    constexpr float kSevenFontScale = 100.0f / 68.0f;
    constexpr float kSevenMargin = 6.0f;
    constexpr float kSevenOffset = 0.0f;   // vertical fine tuning, + = down

    /** Bounds of the actual outlines of a range of glyphs (JUCE's glyph bounds are font cells). */
    Rectangle<float> inkBounds (const GlyphArrangement& glyphs, int start, int num)
    {
        GlyphArrangement copy (glyphs);
        if (start + num < copy.getNumGlyphs()) copy.removeRangeOfGlyphs (start + num, -1);
        if (start > 0) copy.removeRangeOfGlyphs (0, start);
        Path p;
        copy.createPath (p);
        return p.getBounds();
    }
}

void drawLogo (Graphics& g, float x, float baseline)
{
    // "CONCERT7" is laid out as one word so the 7 starts after the T's letter spacing
    GlyphArrangement word;
    word.addLineOfText (Font (FontOptions (kSize, Font::plain)).withExtraKerningFactor (kTracking), "CONCERT7", x, baseline);
    const float cap = baseline - inkBounds (word, 4, 1).getY(); // "E": flat top, no overshoot
    const float sevenOrigin = word.getGlyph (7).getLeft();
    word.removeRangeOfGlyphs (7, -1);

    g.setColour (Colours::text);
    word.draw (g);

    GlyphArrangement seven;
    seven.addLineOfText (Font (FontOptions (kSize * kSevenFontScale, Font::bold)), "7", sevenOrigin + kSevenMargin, 0.0f);
    seven.moveRangeOfGlyphs (0, -1, 0.0f, baseline - cap * 0.5f + kSevenOffset - inkBounds (seven, 0, 1).getCentreY());
    g.setColour (Colours::accent);
    seven.draw (g);
}
} // namespace concert7
