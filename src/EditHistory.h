// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>

namespace concert7
{
class Concert7Processor;

/**
    Undo / redo of what the user does, not of host automation. A step is either a gesture on a control
    (a knob turned, a button clicked: the attachments and views mark them with begin/endChangeGesture) or an
    action (a preset chosen, A/B, a user preset loaded). It records only the parameters the user touched,
    plus those the edit couples to them (the hall model writes DECAY, the distance mode MIX and DIRECT, a new
    preset its decay) - so undo never moves an automated parameter the edit left alone.

    A step closes once all gestures have ended, the processor has handed all values over and nothing changed
    for a moment (wheel steps and quick clicks become one step). Message thread only.
*/
class EditHistory : private juce::AudioProcessorParameter::Listener, private juce::Timer
{
public:
    explicit EditHistory (Concert7Processor& processor);
    ~EditHistory() override;

    /** An edit that is no gesture of a control: everything it changes, also afterwards, becomes one step. */
    void beginAction (const juce::String& name);
    void endAction();

    bool canUndo();
    bool canRedo() const;
    juce::String getUndoName();
    juce::String getRedoName() const;
    bool undo();
    bool redo();
    void clear();

    /** Closes the step being recorded now, without waiting (undo and redo do this first). */
    void flush();

    /** A and B have a history each: undo goes back within the settings in use. */
    void useSlot (int slot);

    static constexpr int kQuietMs = 300;          // a step closes after this long without changes
    static constexpr int kMaxHandoverMs = 3000;   // ... and waits at most this long for the processor

private:
    struct Snapshot
    {
        std::vector<float> values;   // every parameter, 0..1
        juce::String presetId;
        float distanceReference = 0.0f;
    };
    struct Change { int index; float before, after; };
    class Step;

    Snapshot take() const;
    void open (const juce::String& name);
    void touch (int index);
    void close();
    void apply (const std::vector<Change>& changes, bool forward, const juce::String& presetId, float distanceReference);

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool starting) override;
    void timerCallback() override;

    Concert7Processor& processor;
    std::array<juce::UndoManager, 2> managers { juce::UndoManager { 1000, 100 }, juce::UndoManager { 1000, 100 } };
    int slot = 0;
    juce::UndoManager& manager() { return managers[(size_t) slot]; }
    const juce::UndoManager& manager() const { return managers[(size_t) slot]; }
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<std::vector<int>> coupled;   // per parameter: the parameters an edit of it may move as well

    bool recording = false, wholeState = false, applying = false;
    int gestures = 0, actions = 0;
    std::vector<bool> touched;
    Snapshot before;
    juce::String stepName;
    juce::uint32 lastActivity = 0;

    JUCE_DECLARE_NON_COPYABLE (EditHistory)
};
} // namespace concert7
