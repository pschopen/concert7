// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace concert7
{
namespace Colours
{
    inline const juce::Colour background   { 0xff121418 };
    inline const juce::Colour panel        { 0xff1b1e23 };
    inline const juce::Colour panelLight   { 0xff23272d };
    inline const juce::Colour faceTop      { 0xff2c3036 };
    inline const juce::Colour faceBottom   { 0xff17191d };
    inline const juce::Colour outline      { 0xff2f343b };
    inline const juce::Colour text         { 0xffd4d9e0 };
    inline const juce::Colour textDim      { 0xff8b929c };
    inline const juce::Colour textFaint    { 0xff5d636c };
    inline const juce::Colour accent       { 0xff4cb8ff };
    inline const juce::Colour accentDim    { 0xff1f5a80 };
    inline const juce::Colour lcdBack      { 0xff03111a };
    inline const juce::Colour lcdOn        { 0xff7fd6ff };
    inline const juce::Colour lcdOff       { 0xff0a2230 };
    inline const juce::Colour ledOn        { 0xff57c7ff };
    inline const juce::Colour ledOff       { 0xff1a2a35 };
    inline const juce::Colour warning      { 0xffffb347 };
    inline const juce::Colour tray         { 0xff131519 };   // recessed box in the faceplate (with folder tabs on it)
    inline const juce::Colour trayEdge     { 0xff0a0b0d };
}

juce::Font uiFont (float height, bool bold = false);
juce::Font labelFont (float height); // spaced capitals for panel labels

class Concert7LookAndFeel : public juce::LookAndFeel_V4
{
public:
    Concert7LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (12.5f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                     const juce::Rectangle<float>& body) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
};

/** Small padlock: a kept group (it stays when another preset is chosen). */
void drawLock (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour);

/** Draws a machined screw head (purely decorative). */
void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius);
} // namespace concert7
