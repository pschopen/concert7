// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Tests of the parameters and the processor state that need no impulse responses: typed values.

#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "ui/PresetBrowser.h"

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

        runHistoryTests();
        runUserPresetTests();
    }

    /** What a control does: a value set as a gesture. */
    static void edit (Concert7Processor& p, const char* id, float value)
    {
        auto* prm = p.getAPVTS().getParameter (id);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }

    void runHistoryTests()
    {
        Concert7Processor proc;
        auto& h = proc.getHistory();

        beginTest ("undo / redo: a gesture is one step");
        expect (! h.canUndo());
        edit (proc, ParamID::mix, 60.0f);
        h.flush();
        expect (h.canUndo() && ! h.canRedo());
        expectEquals (h.getUndoName(), String ("Mix"));
        h.undo();
        expectWithinAbsoluteError (get (proc, ParamID::mix), 30.0f, 0.01f);
        expect (h.canRedo());
        h.redo();
        expectWithinAbsoluteError (get (proc, ParamID::mix), 60.0f, 0.01f);

        beginTest ("undo / redo: host automation during an edit is left alone");
        {
            auto* mix = proc.getAPVTS().getParameter (ParamID::mix);
            mix->beginChangeGesture();
            mix->setValueNotifyingHost (mix->convertTo0to1 (70.0f));
            set (proc, ParamID::gain, 6.0f);     // automation: no gesture
            mix->setValueNotifyingHost (mix->convertTo0to1 (80.0f));
            mix->endChangeGesture();
            h.flush();
            h.undo();
            expectWithinAbsoluteError (get (proc, ParamID::mix), 60.0f, 0.01f);
            expectWithinAbsoluteError (get (proc, ParamID::gain), 6.0f, 0.01f);
            set (proc, ParamID::gain, 0.0f);
        }

        beginTest ("undo / redo: quick edits close as one step, after a moment of quiet");
        {
            edit (proc, ParamID::predelay, 10.0f);
            edit (proc, ParamID::predelay, 20.0f);
            MessageManager::getInstance()->runDispatchLoopUntil (EditHistory::kQuietMs + 200);
            edit (proc, ParamID::predelay, 30.0f);
            h.flush();
            h.undo();
            expectWithinAbsoluteError (get (proc, ParamID::predelay), 20.0f, 0.01f);
            h.undo();
            expectWithinAbsoluteError (get (proc, ParamID::predelay), 0.0f, 0.01f);
        }

        beginTest ("undo / redo: choosing a preset is one step with every value it reset");
        {
            edit (proc, ParamID::eqMidGain, 4.0f);
            edit (proc, ParamID::modDepth, 50.0f);
            h.flush();
            proc.selectPreset (proc.getPresetId(), true);
            h.flush();
            expectWithinAbsoluteError (get (proc, ParamID::eqMidGain), 0.0f, 0.01f);
            h.undo();
            expectWithinAbsoluteError (get (proc, ParamID::eqMidGain), 4.0f, 0.01f);
            expectWithinAbsoluteError (get (proc, ParamID::modDepth), 50.0f, 0.01f);
            h.redo();
            expectWithinAbsoluteError (get (proc, ParamID::modDepth), 0.0f, 0.01f);
        }

        beginTest ("undo / redo: switching the hall model takes back the values it handed over");
        {
            const float volume = get (proc, ParamID::hallVolume);
            set (proc, ParamID::decay, 6.0f);
            edit (proc, ParamID::hallOn, 1.0f);
            MessageManager::getInstance()->runDispatchLoopUntil (EditHistory::kQuietMs + 300);
            expect (std::abs (get (proc, ParamID::hallVolume) - volume) > 1.0f, "the hall starts at the decay");
            h.undo();
            expectEquals (get (proc, ParamID::hallOn), 0.0f);
            expectWithinAbsoluteError (get (proc, ParamID::hallVolume), volume, 0.5f);
            expectWithinAbsoluteError (get (proc, ParamID::decay), 6.0f, 0.01f);
        }

        beginTest ("A/B: two settings, the second starts as a copy, each keeps its undo");
        {
            edit (proc, ParamID::mix, 40.0f);
            h.flush();
            expectEquals (proc.getABSlot(), 0);
            proc.selectABSlot (1);
            expectEquals (proc.getABSlot(), 1);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 40.0f, 0.01f);
            expect (! h.canUndo(), "B has a history of its own");
            edit (proc, ParamID::mix, 70.0f);
            edit (proc, ParamID::decay, 9.0f);
            h.flush();
            proc.selectABSlot (0);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 40.0f, 0.01f);
            expect (h.canUndo(), "A still has its history");
            proc.selectABSlot (1);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 70.0f, 0.01f);
            expectWithinAbsoluteError (get (proc, ParamID::decay), 9.0f, 0.01f);
            h.undo();
            expectWithinAbsoluteError (get (proc, ParamID::mix), 40.0f, 0.01f);
            h.redo();

            // both slots are stored with the project
            MemoryBlock block;
            proc.getStateInformation (block);
            Concert7Processor restored;
            restored.setStateInformation (block.getData(), (int) block.getSize());
            expectEquals (restored.getABSlot(), 1);
            expectWithinAbsoluteError (get (restored, ParamID::mix), 70.0f, 0.01f);
            restored.selectABSlot (0);
            expectWithinAbsoluteError (get (restored, ParamID::mix), 40.0f, 0.01f);

            // copying to the other slot
            proc.copyToOtherABSlot();
            proc.selectABSlot (0);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 70.0f, 0.01f);
        }
    }

    void runUserPresetTests()
    {
        Concert7Processor proc;
        auto& users = proc.getShared().getUserPresets();
        UserPresetLibrary::folderOverride().deleteRecursively();
        users.rescan();
        String error;

        beginTest ("user presets: saving writes the whole sound, loading brings it back");
        set (proc, ParamID::mix, 45.0f);
        set (proc, ParamID::eqMidGain, -2.0f);
        set (proc, ParamID::decay, 3.3f);
        const auto key = proc.saveUserPreset ("My Hall", { "Vocals", " dark ", "vocals" }, "for the choir", error);
        expect (key.isNotEmpty(), error);
        expect (UserPresetLibrary::folderOverride().getChildFile ("My Hall.c7preset").existsAsFile());
        const auto* entry = users.find (key);
        expect (entry != nullptr);
        if (entry != nullptr)
        {
            expectEquals (entry->name, String ("My Hall"));
            expectEquals (entry->tags.joinIntoString (","), String ("vocals,dark"));
            expectEquals (entry->notes, String ("for the choir"));
            expect (entry->matches ("choir hall") && ! entry->matches ("plate"));
        }
        expectEquals (proc.getStatus().userPresetName, String ("My Hall"));
        expect (! proc.getStatus().userPresetModified);
        set (proc, ParamID::mix, 46.0f);
        expect (proc.getStatus().userPresetModified, "a change is marked");

        proc.selectPreset (proc.getPresetId(), true);
        expect (proc.getStatus().userPresetName.isEmpty(), "a library preset leaves the user preset");
        expectWithinAbsoluteError (get (proc, ParamID::mix), 30.0f, 0.01f);
        expect (proc.loadUserPreset (key));
        expectWithinAbsoluteError (get (proc, ParamID::mix), 45.0f, 0.01f);
        expectWithinAbsoluteError (get (proc, ParamID::eqMidGain), -2.0f, 0.01f);
        expectWithinAbsoluteError (get (proc, ParamID::decay), 3.3f, 0.01f);
        expect (! proc.getStatus().userPresetModified);

        beginTest ("user presets: loading keeps the kept groups and is one undo step");
        {
            auto& h = proc.getHistory();
            h.flush();
            proc.selectPreset (proc.getPresetId(), true);
            h.flush();
            proc.getShared().setKeptGroups (groupBit (ParamGroup::EQ));
            set (proc, ParamID::eqMidGain, 5.0f);
            proc.loadUserPreset (key);
            expectWithinAbsoluteError (get (proc, ParamID::eqMidGain), 5.0f, 0.01f);
            expectWithinAbsoluteError (get (proc, ParamID::mix), 45.0f, 0.01f);
            expect (! proc.getStatus().userPresetModified, "a kept group is no change");
            proc.getShared().setKeptGroups (0);
            h.flush();
            h.undo();
            expectWithinAbsoluteError (get (proc, ParamID::mix), 30.0f, 0.01f);
            expect (proc.getStatus().userPresetName.isEmpty());
            h.redo();
            expectEquals (proc.getStatus().userPresetName, String ("My Hall"));
        }

        beginTest ("user presets: the project remembers the user preset");
        {
            MemoryBlock block;
            proc.getStateInformation (block);
            Concert7Processor restored;
            restored.setStateInformation (block.getData(), (int) block.getSize());
            expectEquals (restored.getStatus().userPresetName, String ("My Hall"));
            expectEquals (restored.getUserPresetKey(), key);
        }

        beginTest ("user presets: rename, tags and favourites of user and library presets");
        {
            users.setFavourite (key, true);
            const auto renamed = users.edit (key, "Choir Hall", { "choir" }, "renamed", error);
            expect (renamed.isNotEmpty(), error);
            proc.userPresetRenamed (key, renamed);
            expectEquals (proc.getStatus().userPresetName, String ("Choir Hall"));
            expect (! UserPresetLibrary::folderOverride().getChildFile ("My Hall.c7preset").exists());
            expect (users.isFavourite (renamed) && ! users.isFavourite (key), "the favourite moves along");
            proc.saveUserPreset ("Other", {}, {}, error);
            expect (users.edit (renamed, "Other", {}, {}, error).isEmpty(), "no rename onto another preset");

            users.setLibraryTags ("Halls/01 Large Hall", { "Orchestra", "big" });
            expectEquals (users.getLibraryTags ("Halls/01 Large Hall").joinIntoString (","), String ("orchestra,big"));
            users.setLibraryTags ("Halls/01 Large Hall", {});
            expect (users.getLibraryTags ("Halls/01 Large Hall").isEmpty());
            expect (users.save ("  ", {}, {}, proc.createStateTree(), error).isEmpty(), "a name is needed");
        }

        beginTest ("preset browser: search, favourites, tags, < >");
        {
            PresetBrowser browser (proc);
            const auto names = [&browser] {
                StringArray n;
                for (auto& e : browser.getShown()) n.add (e.name);
                return n.joinIntoString (",");
            };
            browser.setFilter (PresetBrowser::kUser);
            expectEquals (names(), String ("Choir Hall,Other"));
            browser.setSearchText ("choir");
            expectEquals (names(), String ("Choir Hall"));
            browser.setSearchText ({});
            browser.setFilter (PresetBrowser::kFavourites);
            expectEquals (names(), String ("Choir Hall"));
            browser.setFilter (PresetBrowser::kAll);
            browser.setTagSelected ("choir", true);
            expectEquals (names(), String ("Choir Hall"));
            browser.setTagSelected ("choir", false);
            browser.setFilter (PresetBrowser::kUser);
            proc.loadUserPreset (renamedKey (users, "Choir Hall"));
            browser.step (1);
            expectEquals (proc.getStatus().userPresetName, String ("Other"));
            browser.step (1);
            expectEquals (proc.getStatus().userPresetName, String ("Choir Hall"));
        }
        UserPresetLibrary::folderOverride().deleteRecursively();
        users.rescan();
    }

    static String renamedKey (UserPresetLibrary& users, const String& name)
    {
        for (auto& p : users.getUserPresets())
            if (p.name == name) return p.key;
        return {};
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
