// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Logo.h"
#include "ui/Theme.h"
#include "ui/Views.h"
#include "ui/Widgets.h"

namespace concert7
{
/** The whole user interface at its design size (1000 x 640); the editor scales it. */
class MainPanel : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    static constexpr int kWidth = 1000, kHeight = 640;

    explicit MainPanel (Concert7Processor& p);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showWelcome (bool show);
    void showTab (int index);
    int getCurrentTab() const { return currentTab; }
    WelcomeOverlay* getWelcome() { return welcome.get(); }
    std::function<void (float)> onScaleChange;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildCategoryButtons();
    void showPresetMenu (const juce::String& onlyCategory, juce::Component* target);
    void showSetupMenu();
    void touch (Knob& k);
    Knob& addKnob (std::vector<std::unique_ptr<Knob>>& list, juce::Component& parent, const char* id,
                   const juce::String& caption, bool bipolar = false);

    Concert7Processor& processor;
    juce::AudioProcessorValueTreeState& state;
    Concert7LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    PresetDisplay display;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, browseButton { "BROWSE" };
    std::vector<std::unique_ptr<juce::TextButton>> categoryButtons;
    juce::StringArray categories;

    std::vector<std::unique_ptr<Knob>> knobs;
    StereoMeter inMeter { "IN" }, outMeter { "OUT" };

    // tabs
    static constexpr int kNumTabs = 5, kSpaceTab = 4;
    std::array<juce::TextButton, kNumTabs> tabButtons;
    juce::TextButton setupButton { "SETUP" };
    std::array<juce::Component, kNumTabs> pages;
    int currentTab = 0;

    // reverb page
    std::unique_ptr<ParamToggle> predelaySync, delaySync, duckSidechain, eqOn, eqHpOn, eqLpOn;
    std::unique_ptr<ParamCombo> predelayNote, delayNote, crossfeed, eqLowType, eqHighType;
    juce::Label crossfeedLabel, predelayNoteLabel, delayNoteLabel;

    // decay + eq pages
    DecayView decayView;
    EQView eqView;

    // space page
    std::unique_ptr<ParamToggle> hallOn, distOn;
    DistanceView distanceView;
    juce::Label hallDecayLabel;
    bool dryAuto = false;   // the dry signal follows the distance mode
    bool hallAuto = false;  // DECAY follows the hall model

    std::unique_ptr<WelcomeOverlay> welcome;
    std::unique_ptr<juce::FileChooser> chooser;

    Knob* lastTouched = nullptr;
    std::shared_ptr<const BuiltIR> shownIR;
    juce::String lastPresetId;
    float lastNativeRT = -1.0f;
    juce::Rectangle<int> mainArea, pageArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

class Concert7Editor : public juce::AudioProcessorEditor
{
public:
    explicit Concert7Editor (Concert7Processor& p);
    ~Concert7Editor() override;

    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (Colours::background); }
    MainPanel& getPanel() { return panel; }

private:
    MainPanel panel;
};
} // namespace concert7
