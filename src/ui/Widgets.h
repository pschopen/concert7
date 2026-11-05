// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "Theme.h"

namespace concert7
{
/** Rotary knob with a caption above and the value below, bound to a parameter. */
class Knob : public juce::Component, private juce::Slider::Listener
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& caption,
          bool bipolar = false);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() { return slider; }

    /** Shows a value other than the parameter's, e.g. on a greyed knob another mode is driving.
        The parameter itself is left alone (no automation is written). */
    void setDisplayOverride (float value, const juce::String& text);
    void clearDisplayOverride();
    const juce::String& getParamID() const { return paramID; }
    juce::String getValueText() const;
    juce::String getCaption() const { return caption; }

    std::function<void (Knob&)> onTouched; // value changed or hovered by the user
    float captionHeight = 16.0f;
    float valueHeight = 16.0f;

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void mouseEnter (const juce::MouseEvent&) override;

    juce::AudioProcessorValueTreeState& state;
    juce::String paramID, caption;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::Slider slider;
    bool overridden = false;
    juce::String overrideText;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** LED toggle button bound to a bool parameter. */
class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& text);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Combo box bound to a choice parameter. */
class ParamCombo : public juce::ComboBox
{
public:
    ParamCombo (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Vertical stereo peak meter with decay. */
class StereoMeter : public juce::Component
{
public:
    explicit StereoMeter (juce::String text) : caption (std::move (text)) {}
    void setLevels (float l, float r);
    void paint (juce::Graphics&) override;

private:
    juce::String caption;
    float levelL = 0.0f, levelR = 0.0f, holdL = 0.0f, holdR = 0.0f;
    int holdCountL = 0, holdCountR = 0;
};

/** Titled group frame drawn behind a set of controls. */
void drawGroup (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title);
} // namespace concert7
