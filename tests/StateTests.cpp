// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Tests of the parameters and the processor state that need no impulse responses: typed values.

#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "ui/Widgets.h"

namespace concert7::test
{
using namespace juce;

namespace
{
    float valueForText (Concert7Processor& p, const char* id, const String& text)
    {
        auto* prm = p.getAPVTS().getParameter (id);
        return prm->convertFrom0to1 (prm->getValueForText (text));
    }
}

class StateTest : public UnitTest
{
public:
    StateTest() : UnitTest ("State", "concert7-state") {}

    void runTest() override
    {
        Concert7Processor proc;

        beginTest ("typed values: every parameter reads back its own value text");
        for (auto* prm : proc.getParameters())
        {
            auto* rp = dynamic_cast<RangedAudioParameter*> (prm);
            if (rp == nullptr) continue;
            for (float pos : { 0.0f, 0.03f, 0.13f, 0.5f, 0.77f, 1.0f })
            {
                const auto text = rp->getText (pos, 64);
                const auto back = rp->getText (rp->getValueForText (text), 64);
                expectEquals (back, text, rp->paramID + " at " + String (pos));
            }
        }

        beginTest ("typed values: units, decimal comma, off");
        expectWithinAbsoluteError (valueForText (proc, ParamID::decay, "2,5"), 2.5f, 0.001f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::decay, "2500 ms"), 2.5f, 0.001f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::decay, "99"), 30.0f, 0.001f);   // clamped
        expectWithinAbsoluteError (valueForText (proc, ParamID::predelay, "0.1 s"), 100.0f, 0.01f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::predelay, "35"), 35.0f, 0.01f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::irOffset, "-1.5 s"), -1500.0f, 0.5f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::eqMidFreq, "1.2k"), 1200.0f, 0.5f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::eqMidFreq, "1,2 kHz"), 1200.0f, 0.5f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::eqMidGain, "-6 dB"), -6.0f, 0.001f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::early, "off"), kLevelOffDb, 0.001f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::lateRolloff, "Off"), 22000.0f, 0.5f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::lateRolloff, "8k"), 8000.0f, 0.5f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::duckAmount, "-6"), 6.0f, 0.001f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::mix, "45 %"), 45.0f, 0.001f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::hallVolume, "16 000 m\xc2\xb3"), 16000.0f, 1.0f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::hallVolume, "8k"), 8000.0f, 1.0f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::distMic, "50 cm"), 0.5f, 0.001f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::erPattern, "Preset"), 0.0f, 0.001f);
        expectWithinAbsoluteError (valueForText (proc, ParamID::erPattern, "12"), 12.0f, 0.001f);
        expect (! isValueText ("abc"));
        expect (! isValueText (""));
        expect (isValueText ("off") && isValueText ("-3,5 dB"));

        beginTest ("typed values: a knob takes a typed value, ignores text that is no value");
        {
            Knob knob (proc.getAPVTS(), ParamID::decay, "DECAY");
            expect (knob.setValueFromText ("3.4 s"));
            expectWithinAbsoluteError (proc.getAPVTS().getRawParameterValue (ParamID::decay)->load(), 3.4f, 0.01f);
            expect (! knob.setValueFromText ("long"));
            expectWithinAbsoluteError (proc.getAPVTS().getRawParameterValue (ParamID::decay)->load(), 3.4f, 0.01f);
        }

        beginTest ("parameter groups: every parameter has its group");
        {
            std::array<int, kNumParamGroups> counts {};
            for (auto* prm : proc.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                {
                    const auto g = groupOf (rp->paramID);
                    expect (g != ParamGroup::EQ || rp->paramID.startsWith ("eq"), rp->paramID + " has no group");
                    ++counts[(size_t) g];
                }
            for (int g = 0; g < kNumParamGroups; ++g)
                expectGreaterThan (counts[(size_t) g], 0, groupKey ((ParamGroup) g));
        }

        beginTest ("state tree: applying it restores every parameter, kept groups stay");
        {
            set (proc, ParamID::decay, 4.0f);
            set (proc, ParamID::mix, 55.0f);
            set (proc, ParamID::eqMidGain, 5.0f);
            set (proc, ParamID::modMode, (float) ModMode::Chorus);
            auto tree = proc.createStateTree();
            set (proc, ParamID::decay, 1.0f);
            set (proc, ParamID::mix, 10.0f);
            set (proc, ParamID::eqMidGain, -3.0f);
            set (proc, ParamID::modMode, (float) ModMode::Drift);
            proc.applyStateTree (tree, groupBit (ParamGroup::Output));
            expectWithinAbsoluteError (get (proc, ParamID::decay), 4.0f, 0.01f);
            expectWithinAbsoluteError (get (proc, ParamID::eqMidGain), 5.0f, 0.01f);
            expectEquals (roundToInt (get (proc, ParamID::modMode)), (int) ModMode::Chorus);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 10.0f, 0.01f); // kept
            proc.applyStateTree (tree);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 55.0f, 0.01f);

            // parameters the tree lacks go to their defaults
            ValueTree empty (proc.getAPVTS().state.getType());
            empty.appendChild (tree.getChildWithProperty ("id", ParamID::modMode).createCopy(), nullptr);
            proc.applyStateTree (empty);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 30.0f, 0.01f);
            expectWithinAbsoluteError (get (proc, ParamID::eqMidGain), 0.0f, 0.01f);
        }

        beginTest ("kept groups stay when a preset is chosen, the others return to their defaults");
        {
            auto& shared = proc.getShared();
            shared.setKeptGroups (groupBit (ParamGroup::Output) | groupBit (ParamGroup::EQ));
            set (proc, ParamID::mix, 70.0f);
            set (proc, ParamID::eqMidGain, 4.0f);
            set (proc, ParamID::eqHpOn, 1.0f);
            set (proc, ParamID::predelay, 50.0f);
            set (proc, ParamID::modDepth, 40.0f);
            proc.selectPreset (proc.getPresetId(), true);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 70.0f, 0.01f);
            expectWithinAbsoluteError (get (proc, ParamID::eqMidGain), 4.0f, 0.01f);
            expectEquals (get (proc, ParamID::eqHpOn), 1.0f);
            expectWithinAbsoluteError (get (proc, ParamID::predelay), 0.0f, 0.01f);
            expectWithinAbsoluteError (get (proc, ParamID::modDepth), 0.0f, 0.01f);

            shared.setKeptGroups (kAllGroups);
            set (proc, ParamID::predelay, 50.0f);
            proc.selectPreset (proc.getPresetId(), true);
            expectWithinAbsoluteError (get (proc, ParamID::predelay), 50.0f, 0.01f);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 70.0f, 0.01f);

            // a user preset or A/B state is applied with the kept groups as well
            auto tree = proc.createStateTree();
            set (proc, ParamID::mix, 20.0f);
            proc.applyStateTree (tree, shared.getKeptGroups());
            expectWithinAbsoluteError (get (proc, ParamID::mix), 20.0f, 0.01f);

            shared.setKeptGroups (0);
            proc.selectPreset (proc.getPresetId(), true);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 30.0f, 0.01f);
        }

        beginTest ("kept groups: the old hold decay setting becomes the kept decay");
        {
            auto& settings = proc.getShared().getSettings();
            settings.removeValue ("keepOnPresetChange");
            settings.setValue ("holdDecay", true);
            expectEquals ((int) proc.getShared().getKeptGroups(), (int) groupBit (ParamGroup::Decay));
            proc.getShared().setKeptGroups (groupBit (ParamGroup::Decay) | groupBit (ParamGroup::Ducker));
            expect (! settings.containsKey ("holdDecay"));
            expect (proc.getShared().isKept (ParamGroup::Ducker) && ! proc.getShared().isKept (ParamGroup::EQ));
            proc.getShared().setKeptGroups (0);
        }
    }

    static void set (Concert7Processor& p, const char* id, float value)
    {
        auto* prm = p.getAPVTS().getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    static float get (Concert7Processor& p, const char* id) { return p.getAPVTS().getRawParameterValue (id)->load(); }
};

static StateTest stateTest;
} // namespace concert7::test
