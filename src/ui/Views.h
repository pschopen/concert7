// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include "Widgets.h"
#include "DotMatrix.h"

namespace concert7
{
/** The blue dot matrix display: preset, parameter readout and decay envelope. */
class PresetDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setStatus (const Concert7Processor::Status& s, bool hasLibrary);
    void setReadout (const juce::String& caption, const juce::String& value);
    void setIR (std::shared_ptr<const BuiltIR> ir);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    std::function<void()> onClick;

private:
    void paintGraph (juce::Graphics&, juce::Rectangle<float> area);

    Concert7Processor::Status status;
    bool hasLibrary = false;
    juce::String readCaption = "DECAY", readValue;
    std::shared_ptr<const BuiltIR> ir;
};

/** Reverb time over frequency: native preset (dashed) and the current target. */
class DecayView : public juce::Component
{
public:
    explicit DecayView (juce::AudioProcessorValueTreeState& s) : state (s) {}
    void setIR (std::shared_ptr<const BuiltIR> newIR) { ir = std::move (newIR); repaint(); }
    void paint (juce::Graphics&) override;

private:
    juce::AudioProcessorValueTreeState& state;
    std::shared_ptr<const BuiltIR> ir;
};

/** Distance mode: top view of the hall with source, listener, critical distance and the
    path of the first reflection, plus the values of the distance model. */
class DistanceView : public juce::Component
{
public:
    explicit DistanceView (Concert7Processor& p) : processor (p) {}
    void paint (juce::Graphics&) override;

private:
    Concert7Processor& processor;
};

/** Interactive response curve of the post EQ. Nodes can be dragged; the wheel changes Q. */
class EQView : public juce::Component, private juce::Timer
{
public:
    EQView (juce::AudioProcessorValueTreeState& state, Concert7Processor& proc);
    ~EQView() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    struct Node { const char* freq; const char* gain; const char* q; const char* enable; const char* label; };
    static const Node nodes[5];

    void timerCallback() override;
    EQSettings read() const;
    juce::Rectangle<float> plotArea() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float gainToY (float db) const;
    float yToGain (float y) const;
    juce::Point<float> nodePosition (int i, const EQSettings&) const;
    int nodeAt (juce::Point<float> p) const;
    void setParam (const char* id, float value);

    juce::AudioProcessorValueTreeState& state;
    Concert7Processor& processor;
    EQSettings last;
    int hover = -1, dragging = -1;
};

/**
    First run / setup panel. The impulse responses are not part of Concert7: the user
    opens Samplicity's page, downloads the ZIP and drops it here (or picks it); the
    plugin checks and extracts it. An already extracted folder can be chosen as well.
*/
class WelcomeOverlay : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit WelcomeOverlay (Concert7Processor& p);
    ~WelcomeOverlay() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void chooseZip();
    void chooseFolder();
    /** Same as dropping a file: a ZIP is installed, a folder is used as the IR folder. */
    void acceptFile (const juce::File& f);
    void setDragHighlight (bool on) { dragOver = on; repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { setDragHighlight (true); }
    void fileDragExit (const juce::StringArray&) override { setDragHighlight (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    std::function<void()> onClose;

private:
    enum class ZoneState { Idle, Busy, Done, Failed };

    void timerCallback() override;
    juce::Rectangle<int> cardBounds() const;
    juce::Rectangle<int> dropZone() const;
    ZoneState zoneState() const;
    void paintStep (juce::Graphics&, juce::Rectangle<float> row, int number, bool done,
                    const juce::String& title, const juce::String& text);
    void paintDropZone (juce::Graphics&, juce::Rectangle<float> zone);

    Concert7Processor& processor;
    juce::TextButton pageButton, zipButton, folderButton, closeButton;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false, pageOpened = false;
    juce::String localError;   // problems detected before the installer runs
    int lastHash = 0;
};
} // namespace concert7
