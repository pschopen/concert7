// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "EditHistory.h"
#include "PluginProcessor.h"

namespace concert7
{
using namespace juce;

class EditHistory::Step : public UndoableAction
{
public:
    Step (EditHistory& h, std::vector<Change> c, String presetBefore, String presetAfter, float refBefore, float refAfter)
        : history (h), changes (std::move (c)), idBefore (std::move (presetBefore)), idAfter (std::move (presetAfter)),
          distanceBefore (refBefore), distanceAfter (refAfter) {}

    bool perform() override
    {
        // the first time the edit has already happened
        if (std::exchange (first, false)) return true;
        history.apply (changes, true, idAfter, distanceAfter);
        return true;
    }

    bool undo() override
    {
        history.apply (changes, false, idBefore, distanceBefore);
        return true;
    }

    int getSizeInUnits() override { return 10; }

private:
    EditHistory& history;
    std::vector<Change> changes;
    String idBefore, idAfter;
    float distanceBefore, distanceAfter;
    bool first = true;
};

//==============================================================================
EditHistory::EditHistory (Concert7Processor& p) : processor (p)
{
    for (auto* prm : processor.getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
            params.push_back (rp);
    for (auto* rp : params)
        rp->addListener (this);

    // switching the hall model or the distance mode hands values over to other parameters
    auto indexOf = [this] (const char* id) {
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i]->paramID == id) return (int) i;
        jassertfalse;
        return 0;
    };
    coupled.resize (params.size());
    coupled[(size_t) indexOf (ParamID::hallOn)] = { indexOf (ParamID::decay), indexOf (ParamID::hallVolume),
                                                    indexOf (ParamID::hallAbsorption) };
    coupled[(size_t) indexOf (ParamID::distOn)] = { indexOf (ParamID::distance), indexOf (ParamID::mix),
                                                    indexOf (ParamID::directHighs), indexOf (ParamID::directAir),
                                                    indexOf (ParamID::directWidth), indexOf (ParamID::directDelay),
                                                    indexOf (ParamID::directGap) };
    touched.assign (params.size(), false);
}

EditHistory::~EditHistory()
{
    stopTimer();
    for (auto* rp : params)
        rp->removeListener (this);
}

EditHistory::Snapshot EditHistory::take() const
{
    Snapshot s;
    s.values.reserve (params.size());
    for (auto* rp : params)
        s.values.push_back (rp->getValue());
    s.presetId = processor.getPresetId();
    s.distanceReference = processor.getDistanceReference();
    return s;
}

void EditHistory::open (const String& name)
{
    lastActivity = Time::getMillisecondCounter();
    if (recording) return;
    recording = true;
    wholeState = false;
    std::fill (touched.begin(), touched.end(), false);
    before = take();
    stepName = name;
    startTimer (40);
}

void EditHistory::touch (int index)
{
    touched[(size_t) index] = true;
    for (int c : coupled[(size_t) index])
        touched[(size_t) c] = true;
}

void EditHistory::parameterGestureChanged (int parameterIndex, bool starting)
{
    // gestures come from the user interface; anything else (a host on another thread) is not ours
    if (applying || ! MessageManager::existsAndIsCurrentThread()) return;
    int index = -1;
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i]->getParameterIndex() == parameterIndex) index = (int) i;
    if (index < 0) return;

    if (starting)
    {
        open (params[(size_t) index]->getName (32));
        touch (index);
        ++gestures;
    }
    else if (gestures > 0)
    {
        --gestures;
        lastActivity = Time::getMillisecondCounter();
    }
}

void EditHistory::beginAction (const String& name)
{
    jassert (MessageManager::existsAndIsCurrentThread());
    if (applying) return;
    if (recording && stepName != name && actions == 0 && gestures == 0)
        close(); // another kind of edit: a step of its own
    open (name);
    wholeState = true;
    ++actions;
}

void EditHistory::endAction()
{
    if (applying || actions == 0) return;
    --actions;
    lastActivity = Time::getMillisecondCounter();
}

void EditHistory::timerCallback()
{
    if (! recording)
    {
        stopTimer();
        return;
    }
    if (gestures > 0 || actions > 0) return;
    const auto idle = Time::getMillisecondCounter() - lastActivity;
    if (idle < (uint32) kQuietMs) return;
    if (processor.hasPendingHandover() && idle < (uint32) kMaxHandoverMs) return;
    close();
}

void EditHistory::close()
{
    if (! recording) return;
    recording = false;
    stopTimer();
    gestures = 0;
    actions = 0;

    const auto after = take();
    std::vector<Change> changes;
    for (size_t i = 0; i < params.size(); ++i)
        if ((wholeState || touched[i]) && before.values[i] != after.values[i])
            changes.push_back ({ (int) i, before.values[i], after.values[i] });
    const bool presetChanged = before.presetId != after.presetId;
    const bool referenceChanged = before.distanceReference != after.distanceReference;
    if (changes.empty() && ! presetChanged && ! referenceChanged) return;

    manager().beginNewTransaction (stepName);
    manager().perform (new Step (*this, std::move (changes), before.presetId, after.presetId,
                               before.distanceReference, after.distanceReference));
}

void EditHistory::flush()
{
    close();
}

void EditHistory::apply (const std::vector<Change>& changes, bool forward, const String& presetId, float distanceReference)
{
    std::vector<std::pair<RangedAudioParameter*, float>> values;
    for (auto& c : changes)
        values.push_back ({ params[(size_t) c.index], forward ? c.after : c.before });
    const ScopedValueSetter<bool> busy (applying, true);
    processor.restoreValues (values, presetId, distanceReference);
}

bool EditHistory::canUndo()
{
    return recording || manager().canUndo();
}

bool EditHistory::canRedo() const { return ! recording && manager().canRedo(); }

String EditHistory::getUndoName()
{
    return recording ? stepName : manager().getUndoDescription();
}

String EditHistory::getRedoName() const { return manager().getRedoDescription(); }

bool EditHistory::undo()
{
    flush();
    return manager().undo();
}

bool EditHistory::redo()
{
    flush();
    return manager().redo();
}

void EditHistory::clear()
{
    recording = false;
    gestures = 0;
    actions = 0;
    stopTimer();
    for (auto& m : managers)
        m.clearUndoHistory();
}

void EditHistory::useSlot (int newSlot)
{
    flush();
    slot = jlimit (0, 1, newSlot);
}
} // namespace concert7
