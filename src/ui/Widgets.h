// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "Theme.h"

namespace concert7
{
/** Rotary knob with a caption above and the value below, bound to a parameter. Double-click on the value
    (or "Enter value..." in the right-click menu) types a value; double-click on the knob resets it. */
class Knob : public juce::Component, public juce::SettableTooltipClient, private juce::Slider::Listener
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& caption,
          bool bipolar = false);

    /** Shown over the whole knob, caption and value included. */
    void setTooltip (const juce::String& text) override;

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

    /** Opens the text field for typing a value (Enter takes it, Esc cancels, Tab moves to the next knob). */
    void showValueEditor();
    bool isEditingValue() const { return valueEditor != nullptr && valueEditor->isVisible(); }
    /** Takes a typed value; false if the text is no value. */
    bool setValueFromText (const juce::String& text);

    std::function<void (Knob&)> onTouched; // value changed or hovered by the user
    std::function<void (Knob&, bool backwards)> onTabKey; // Tab in the text field
    std::function<void (Knob&, juce::PopupMenu&)> onExtendMenu; // adds items to the right-click menu
    float captionHeight = 16.0f;
    float valueHeight = 16.0f;

private:
    /** The right mouse button opens the menu instead of turning the knob. */
    struct KnobSlider : public juce::Slider
    {
        std::function<void()> onMenu;
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onMenu) onMenu(); } else Slider::mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) Slider::mouseDrag (e); }
        void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) Slider::mouseUp (e); }
    };

    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void showMenu();
    void closeValueEditor (bool apply);
    juce::Rectangle<int> getValueArea() const;

    juce::AudioProcessorValueTreeState& state;
    juce::String paramID, caption;
    juce::RangedAudioParameter* parameter = nullptr;
    KnobSlider slider;
    std::unique_ptr<juce::TextEditor> valueEditor;
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
class StereoMeter : public juce::Component, public juce::SettableTooltipClient
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
