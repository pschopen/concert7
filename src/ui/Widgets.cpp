// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "Widgets.h"

namespace concert7
{
using namespace juce;

Knob::Knob (AudioProcessorValueTreeState& s, const String& id, const String& cap, bool bipolar)
    : state (s), paramID (id), caption (cap)
{
    parameter = state.getParameter (id);
    jassert (parameter != nullptr);

    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.22f, MathConstants<float>::pi * 2.78f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.addListener (this);
    slider.addMouseListener (this, false);
    slider.setTooltip (parameter != nullptr ? parameter->getName (64) : String());
    addAndMakeVisible (slider);

    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
    if (parameter != nullptr)
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
}

void Knob::setTooltip (const String& text)
{
    SettableTooltipClient::setTooltip (text);
    slider.setTooltip (text);
}

String Knob::getValueText() const
{
    if (overridden) return overrideText;
    if (parameter == nullptr) return {};
    return parameter->getCurrentValueAsText();
}

void Knob::setDisplayOverride (float value, const String& text)
{
    const float pos = parameter != nullptr ? parameter->convertTo0to1 (parameter->getNormalisableRange().snapToLegalValue (value)) : 0.0f;
    if (overridden && text == overrideText && (float) slider.getProperties()["displayPos"] == pos) return;
    overridden = true;
    overrideText = text;
    slider.getProperties().set ("displayPos", pos);
    slider.repaint();
    repaint();
}

void Knob::clearDisplayOverride()
{
    if (! overridden) return;
    overridden = false;
    slider.getProperties().remove ("displayPos");
    slider.repaint();
    repaint();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop ((int) captionHeight);
    r.removeFromBottom ((int) valueHeight);
    const int size = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (isEnabled() ? Colours::textDim : Colours::textFaint);
    g.setFont (labelFont (std::max (9.0f, captionHeight * 0.68f)));
    g.drawFittedText (caption, r.removeFromTop (captionHeight).toNearestInt(), Justification::centred, 1);
    g.setColour (isEnabled() ? Colours::text : Colours::textFaint);
    g.setFont (uiFont (std::max (10.0f, valueHeight * 0.78f)));
    g.drawFittedText (getValueText(), r.removeFromBottom (valueHeight).toNearestInt(), Justification::centred, 1);
}

void Knob::sliderValueChanged (Slider*)
{
    repaint();
    if (onTouched && (slider.isMouseButtonDown() || slider.isMouseOver())) onTouched (*this);
}

void Knob::sliderDragStarted (Slider*)
{
    if (onTouched) onTouched (*this);
}

void Knob::mouseEnter (const MouseEvent&)
{
    if (onTouched) onTouched (*this);
}

//==============================================================================
ParamToggle::ParamToggle (AudioProcessorValueTreeState& state, const String& paramID, const String& text)
    : TextButton (text)
{
    setClickingTogglesState (true);
    getProperties().set ("led", true);
    attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, paramID, *this);
    if (auto* p = state.getParameter (paramID))
        setTooltip (p->getName (64));
}

ParamCombo::ParamCombo (AudioProcessorValueTreeState& state, const String& paramID)
{
    if (auto* p = dynamic_cast<AudioParameterChoice*> (state.getParameter (paramID)))
    {
        addItemList (p->choices, 1);
        setTooltip (p->getName (64));
    }
    attachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramID, *this);
}

//==============================================================================
void StereoMeter::setLevels (float l, float r)
{
    auto toNorm = [] (float v) { return jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (v, -60.0f) + 60.0f) / 66.0f); };
    const float nl = toNorm (l), nr = toNorm (r);
    levelL = std::max (nl, levelL - 0.025f);
    levelR = std::max (nr, levelR - 0.025f);
    if (nl >= holdL) { holdL = nl; holdCountL = 45; } else if (--holdCountL < 0) holdL = std::max (0.0f, holdL - 0.01f);
    if (nr >= holdR) { holdR = nr; holdCountR = 45; } else if (--holdCountR < 0) holdR = std::max (0.0f, holdR - 0.01f);
    repaint();
}

void StereoMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::textDim);
    g.setFont (labelFont (9.5f));
    g.drawText (caption, r.removeFromTop (14.0f), Justification::centred);
    r.removeFromBottom (2.0f);

    const float barW = (r.getWidth() - 4.0f) * 0.5f;
    auto drawBar = [&] (Rectangle<float> bar, float level, float hold) {
        g.setColour (Colours::lcdBack);
        g.fillRoundedRectangle (bar, 2.0f);
        const float zeroDb = 60.0f / 66.0f;
        auto filled = bar.withTop (bar.getBottom() - bar.getHeight() * level);
        g.setGradientFill (ColourGradient (Colours::accent.darker (0.4f), bar.getX(), bar.getBottom(),
                                           Colours::lcdOn, bar.getX(), bar.getY() + bar.getHeight() * (1.0f - zeroDb), false));
        g.fillRoundedRectangle (filled, 2.0f);
        if (level > zeroDb)
        {
            g.setColour (Colours::warning);
            g.fillRect (filled.withBottom (bar.getY() + bar.getHeight() * (1.0f - zeroDb)));
        }
        if (hold > 0.01f)
        {
            g.setColour (hold > zeroDb ? Colours::warning : Colours::lcdOn);
            g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * hold, bar.getWidth(), 1.5f);
        }
        // scale ticks every 12 dB
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        for (int db = 0; db >= -48; db -= 12)
        {
            const float y = bar.getBottom() - bar.getHeight() * ((db + 60.0f) / 66.0f);
            g.fillRect (bar.getX(), y, bar.getWidth(), 1.0f);
        }
    };
    drawBar (r.withWidth (barW), levelL, holdL);
    drawBar (r.withTrimmedLeft (barW + 4.0f), levelR, holdR);
}

void drawGroup (Graphics& g, Rectangle<float> area, const String& title)
{
    g.setColour (Colours::panel.brighter (0.03f));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (Colours::outline.withAlpha (0.7f));
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (Colours::accent.withAlpha (0.85f));
        g.setFont (labelFont (10.0f));
        g.drawText (title, area.reduced (10.0f, 6.0f).removeFromTop (12.0f), Justification::topLeft);
    }
}
} // namespace concert7
