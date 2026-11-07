// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Libraries of split M7 captures and other impulse responses: folder names, scan, early reflections and late reverb as
// blocks of their own, the SAM / LS / OTHER switch. Synthetic captures always; real ones with --split / CONCERT7_SPLIT_DIR.

#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "dsp/IRLibrary.h"
#include <set>

namespace concert7::test
{
using namespace juce;

namespace
{
    constexpr double kRate = 48000.0;

    /** Reflections: a few spikes and a short noise burst, then silence (as a "head" file). */
    std::array<std::vector<float>, 4> syntheticEarly (int seed)
    {
        std::array<std::vector<float>, 4> ch;
        Random rng (seed);
        const int len = (int) (0.4 * kRate), onset = (int) (0.01 * kRate);
        for (auto& c : ch)
        {
            c.assign ((size_t) len, 0.0f);
            for (int i = onset; i < onset + (int) (0.08 * kRate); ++i)
                c[(size_t) i] = (rng.nextFloat() * 2.0f - 1.0f) * 0.3f * (float) std::exp (-(i - onset) / (0.02 * kRate));
            for (int k = 0; k < 6; ++k)
                c[(size_t) (onset + rng.nextInt ((int) (0.06 * kRate)))] += 0.8f;
        }
        return ch;
    }

    /** Late reverb: noise that builds up from the onset and decays with rt60 (as a "tail" file). */
    std::array<std::vector<float>, 4> syntheticLate (float rt60, int seed)
    {
        std::array<std::vector<float>, 4> ch;
        Random rng (seed);
        const int len = (int) (2.5 * rt60 * kRate), onset = (int) (0.01 * kRate);
        const double k = std::log (1000.0) / (rt60 * kRate);
        for (auto& c : ch)
        {
            c.assign ((size_t) len, 0.0f);
            for (int i = onset; i < len; ++i)
                c[(size_t) i] = (rng.nextFloat() * 2.0f - 1.0f) * 0.2f * (float) ((1.0 - std::exp (-(i - onset) / (0.03 * kRate)))
                                                                                  * std::exp (-k * (i - onset)));
        }
        return ch;
    }

    bool writeWav (const File& f, const std::array<std::vector<float>, 4>& ch)
    {
        f.getParentDirectory().createDirectory();
        AudioBuffer<float> buf (4, (int) ch[0].size());
        for (int c = 0; c < 4; ++c)
            buf.copyFrom (c, 0, ch[(size_t) c].data(), (int) ch[(size_t) c].size());
        return test::writeWav (f, buf, kRate);
    }

    /** A capture folder: bank/NN Name/head-N.wav and tail-N.wav (take 2's tail: its own fine structure or a copy). */
    void writeCapture (const File& root, const String& bank, const String& preset, float rt60, int seed, bool ownTake)
    {
        const auto dir = root.getChildFile (bank).getChildFile (preset);
        const auto early = syntheticEarly (seed);
        const auto late = syntheticLate (rt60, seed + 1);
        writeWav (dir.getChildFile ("head-1.wav"), early);
        writeWav (dir.getChildFile ("tail-1.wav"), late);
        writeWav (dir.getChildFile ("head-2.wav"), early);
        writeWav (dir.getChildFile ("tail-2.wav"), ownTake ? syntheticLate (rt60, seed + 2) : late);
    }

    float maxDiff (const std::vector<float>& a, const std::vector<float>& b)
    {
        float d = 0.0f;
        for (size_t i = 0; i < std::max (a.size(), b.size()); ++i)
            d = std::max (d, std::abs ((i < a.size() ? a[i] : 0.0f) - (i < b.size() ? b[i] : 0.0f)));
        return d;
    }
}

class SplitLibraryTest : public UnitTest
{
public:
    SplitLibraryTest() : UnitTest ("Split captures and other libraries", "concert7-library") {}

    void runTest() override
    {
        beginTest ("folder names");
        {
            auto p = IRLibrary::parseSplitPath ("x/Halls2/05 Small Hall/head-1.flac");
            expect (p.ok);
            expect (p.head);
            expectEquals (p.take, 1);
            expectEquals (p.category, String ("Halls"));
            expectEquals (p.bank, 2);
            expectEquals (p.number, 5);
            expectEquals (p.name, String ("Small Hall"));
            expectEquals (p.id, String ("Split/Halls2/05 Small Hall"));

            p = IRLibrary::parseSplitPath ("Ambience1\\14 Bass XXL\\tail-8.flac");
            expect (p.ok && ! p.head);
            expectEquals (p.take, 8);
            expectEquals (p.category, String ("Ambiences"));
            expectEquals (p.categoryOrder, 5);

            p = IRLibrary::parseSplitPath ("Nonlin/02 Nonlin B/tail-3.flac");
            expect (p.ok);
            expectEquals (p.category, String ("Nonlin"));
            expectEquals (p.categoryOrder, 7);

            expect (! IRLibrary::parseSplitPath ("Halls3/01 Large Hall/head-1.flac").ok);
            expect (! IRLibrary::parseSplitPath ("Halls1/Large Hall/head-1.flac").ok);
            expect (! IRLibrary::parseSplitPath ("Halls1/01 Large Hall/readme.flac").ok);
            expect (! IRLibrary::parseSplitPath ("01 Large Hall/head-1.flac").ok);

            expect (IRLibrary::originOfId ("Split/Halls1/01 Large Hall") == IROrigin::Split);
            expect (IRLibrary::originOfId ("Halls/01 Large Hall") == IROrigin::Combined);
            expect (IRLibrary::originOfId ("User/room.wav") == IROrigin::User);

            IRPreset preset;
            preset.number = 5;
            expectEquals (preset.displayNumber(), String ("05"));
            preset.bank = 2;
            expectEquals (preset.displayNumber(), String ("2.05"));
        }

        const auto root = File::getSpecialLocation (File::tempDirectory).getChildFile ("concert7-ls-test");
        root.deleteRecursively();
        const auto samFolder = root.getChildFile ("sam"), splitFolder = root.getChildFile ("ls");

        beginTest ("scan: both libraries, counterparts");
        {
            // the scan reads names only: empty files will do
            for (auto name : { "1 Halls 01 Large Hall, 48K.wav", "6 Spaces 15 Stone Quarry, 48K.wav", "6 Spaces 16 Europa, 48K.wav",
                               "6 Spaces 17 Gated Space, 48K.wav" })
                samFolder.getChildFile (name).create();
            samFolder.getChildFile ("my room.wav").create();
            for (auto path : { "Halls1/01 Large Hall", "Halls2/01 Large Hall", "Spaces1/15 Stone Quarry", "Spaces1/16 Gated Space",
                               "Spaces1/17 Reflect Chapel", "Nonlin/01 Nonlin A" })
                for (auto file : { "head-1.flac", "tail-1.flac", "head-2.flac", "tail-2.flac" })
                    splitFolder.getChildFile (path).getChildFile (file).create();
            splitFolder.getChildFile ("Rooms1/01 Studio A/head-1.flac").create(); // no late reverb: incomplete
            splitFolder.getChildFile ("readme.txt").create();

            IRLibrary lib;
            lib.scan (samFolder, splitFolder);
            expectEquals (lib.count (IROrigin::Combined), 4);
            expectEquals (lib.count (IROrigin::Split), 6, "the incomplete capture is left out");
            expectEquals (lib.count (IROrigin::User), 1);
            expectEquals (lib.getCategories (IROrigin::Split).joinIntoString (","), String ("Halls,Spaces,Nonlin"));
            expectEquals (lib.getCategories (IROrigin::Combined).joinIntoString (","), String ("Halls,Spaces"));
            expectEquals (lib.getCategories (IROrigin::User).joinIntoString (","), String ("User"));
            expectEquals (lib.firstPresetId (IROrigin::Split), String ("Split/Halls1/01 Large Hall"));
            if (const auto* p = lib.find ("Split/Halls2/01 Large Hall"))
            {
                expectEquals (p->bank, 2);
                expectEquals ((int) p->variants.at (0).heads.size(), 2, "two takes");
                expect (p->variants.at (0).hasBlocks());
            }
            else
                expect (false, "second bank found");

            expectEquals (lib.counterpart ("Halls/01 Large Hall", IROrigin::Split), String ("Split/Halls1/01 Large Hall"));
            expectEquals (lib.counterpart ("Split/Halls1/01 Large Hall", IROrigin::Combined), String ("Halls/01 Large Hall"));
            expectEquals (lib.counterpart ("Spaces/15 Stone Quarry", IROrigin::Split), String ("Split/Spaces1/15 Stone Quarry"));
            expectEquals (lib.counterpart ("Spaces/17 Gated Space", IROrigin::Split), String ("Split/Spaces1/16 Gated Space"));
            expectEquals (lib.counterpart ("Split/Spaces1/16 Gated Space", IROrigin::Combined), String ("Spaces/17 Gated Space"));
            expect (lib.counterpart ("Spaces/16 Europa", IROrigin::Split).isEmpty());
            expect (lib.counterpart ("Split/Spaces1/17 Reflect Chapel", IROrigin::Combined).isEmpty());
            expect (lib.counterpart ("Split/Halls2/01 Large Hall", IROrigin::Combined).isEmpty(), "second banks: none");
            expect (lib.counterpart ("Split/Nonlin/01 Nonlin A", IROrigin::Combined).isEmpty());

            // captures inside the IR folder count as split captures too
            IRLibrary inside;
            splitFolder.copyDirectoryTo (samFolder.getChildFile ("split"));
            inside.scan (samFolder);
            expectEquals (inside.count (IROrigin::Split), 6);
            expectEquals (inside.count (IROrigin::User), 1, "no capture file becomes a user IR");
            root.deleteRecursively();
        }

        beginTest ("the early reflections and the late reverb are blocks of their own");
        {
            writeCapture (splitFolder, "Halls1", "01 Large Hall", 1.6f, 11, false);
            writeCapture (splitFolder, "Halls2", "01 Large Hall", 1.8f, 21, true);
            IRLibrary lib;
            lib.scan ({}, splitFolder);
            expectEquals (lib.count (IROrigin::Split), 2);

            String err;
            const auto* p1 = lib.find ("Split/Halls1/01 Large Hall");
            const auto* p2 = lib.find ("Split/Halls2/01 Large Hall");
            auto src = p1 != nullptr ? loadSourceIR (*p1, kRate, err) : nullptr;
            auto own = p2 != nullptr ? loadSourceIR (*p2, kRate, err) : nullptr;
            expect (src != nullptr && own != nullptr, err);
            if (src != nullptr && own != nullptr)
            {
                expect (src->hasBlocks() && src->trueStereo);
                expect (! src->hasLateTake(), "a take that is a copy is no variant");
                expect (own->hasLateTake(), "a take of its own is the variant");
                float sumDiff = 0.0f;
                for (int c = 0; c < 4; ++c)
                    for (size_t i = 0; i < src->ch[(size_t) c].size(); ++i)
                        sumDiff = std::max (sumDiff, std::abs (src->ch[(size_t) c][i] - (src->earlyBlock[(size_t) c][i] + src->lateBlock[(size_t) c][i])));
                expectEquals (sumDiff, 0.0f, "the response is the sum of the blocks");
                expectWithinAbsoluteError (src->nativeRT, 1.6f, 0.1f, "reverb time of the late block");

                // at its own decay the response is the recording, exactly
                DesignParams d;
                d.decay = src->nativeRT;
                auto ir = designIR (*src, nullptr, d);
                float diff = 0.0f;
                for (size_t c = 0; c < 4; ++c)
                    diff = std::max (diff, maxDiff (ir->ch[c], src->ch[c]));
                expectEquals (diff, 0.0f, "a just loaded preset is the recording");

                // EARLY off: the late block alone, from its start (nothing is cut at a crossover)
                d.earlyDb = kLevelOffDb;
                ir = designIR (*src, nullptr, d);
                diff = 0.0f;
                for (size_t c = 0; c < 4; ++c)
                    diff = std::max (diff, maxDiff (ir->ch[c], src->lateBlock[c]));
                expectEquals (diff, 0.0f, "EARLY off leaves the late block");

                // a shorter or longer decay leaves the reflections as they are (a block of its own, as on the M7)
                for (float factor : { 0.5f, 2.0f })
                {
                    DesignParams m;
                    m.decay = src->nativeRT * factor;
                    m.splitLate = true;
                    auto split = designIR (*src, nullptr, m);
                    diff = 0.0f;
                    for (size_t c = 0; c < 4; ++c)
                        diff = std::max (diff, maxDiff (split->ch[c], src->earlyBlock[c]));
                    expectLessThan (diff, 1.0e-7f, "reflections unchanged at decay x" + String (factor));
                    const float rt = estimateRT (chans (split->late), kRate, 500.0f, 2000.0f);
                    expectWithinAbsoluteError (rt, m.decay, 0.15f * m.decay, "late reverb at decay x" + String (factor));
                }

                // the modulation's variant: the other take, unchanged at the preset's own decay
                DesignParams v;
                v.decay = own->nativeRT;
                v.splitLate = v.lateVariant = true;
                auto withTake = designIR (*own, nullptr, v);
                diff = 0.0f;
                for (size_t c = 0; c < 4; ++c)
                    diff = std::max (diff, maxDiff (withTake->variant[c], own->lateTake[c]));
                expectEquals (diff, 0.0f, "the variant is the second take");
                // ... and shaped like the late reverb when the decay changes
                v.decay = own->nativeRT * 1.5f;
                withTake = designIR (*own, nullptr, v);
                const float rtLate = estimateRT (chans (withTake->late), kRate, 500.0f, 2000.0f);
                const float rtVariant = estimateRT (chans (withTake->variant), kRate, 500.0f, 2000.0f);
                expectWithinAbsoluteError (rtVariant, rtLate, 0.08f * rtLate, "variant decays like the late reverb");
                const double eL = energy (withTake->late[0]) + energy (withTake->late[3]);
                const double eV = energy (withTake->variant[0]) + energy (withTake->variant[3]);
                expectWithinAbsoluteError ((float) (10.0 * std::log10 (eV / eL)), 0.0f, 1.0f, "variant as loud as the late reverb");
                double cross = 0.0;
                for (size_t i = 0; i < withTake->late[0].size() && i < withTake->variant[0].size(); ++i)
                    cross += (double) withTake->late[0][i] * withTake->variant[0][i];
                expectLessThan ((float) std::abs (cross / std::sqrt (energy (withTake->late[0]) * energy (withTake->variant[0]))), 0.3f,
                                "variant with its own fine structure");

                // the early pattern of another capture: its reflection block replaces this one's
                DesignParams pat;
                pat.decay = src->nativeRT;
                pat.splitLate = true;
                auto patterned = designIR (*src, own.get(), pat);
                diff = 0.0f;
                for (size_t c = 0; c < 4; ++c)
                    diff = std::max (diff, maxDiff (patterned->ch[c], own->earlyBlock[c]));
                expectEquals (diff, 0.0f, "pattern: the other capture's reflections");
            }
        }

        beginTest ("other impulse responses: categories by subfolder, a folder of their own");
        {
            root.deleteRecursively();
            const auto own = root.getChildFile ("own");
            for (auto name : { "plain.wav", "Churches/a.wav", "Churches/b.wav", "Rooms/deep/x.wav", "Bunkers/1.wav" })
                own.getChildFile (name).create();
            samFolder.getChildFile ("1 Halls 01 Large Hall, 48K.wav").create();
            samFolder.getChildFile ("old own.wav").create();

            IRLibrary lib;
            lib.scan (samFolder, {}, own);
            expectEquals (lib.count (IROrigin::User), 6);
            expectEquals (lib.getCategories (IROrigin::User).joinIntoString (","), String ("User,Bunkers,Churches,Rooms"));
            expectEquals (lib.count (IROrigin::Combined), 1);
            if (const auto* p = lib.find ("User/Churches/b.wav"))
            {
                expectEquals (p->category, String ("Churches"));
                expectEquals (p->number, 2);
            }
            else
                expect (false, "own IR found");
            expect (lib.counterpart ("User/Churches/a.wav", IROrigin::Combined).isEmpty());
            root.deleteRecursively();
        }

        beginTest ("SAM / LS / OTHER switch");
        {
            // a combined quad file, two split captures (one with a counterpart) and one other impulse response
            root.deleteRecursively();
            writeWav (samFolder.getChildFile ("1 Halls 01 Large Hall, 48K.wav"), [] {
                auto e = syntheticEarly (5);
                auto l = syntheticLate (1.5f, 6);
                for (int c = 0; c < 4; ++c)
                    for (size_t i = 0; i < e[(size_t) c].size(); ++i) l[(size_t) c][i] += e[(size_t) c][i];
                return l;
            }());
            writeCapture (splitFolder, "Halls1", "01 Large Hall", 1.6f, 11, false);
            writeCapture (splitFolder, "Halls2", "03 Small Hall", 1.0f, 31, true);
            const auto own = root.getChildFile ("own");
            writeWav (own.getChildFile ("Cellars").getChildFile ("cellar.wav"), syntheticLate (0.8f, 41));

            Concert7Processor proc;
            auto& shared = proc.getShared();
            const auto oldIR = shared.getIRFolder(), oldSplit = shared.getSplitFolder(), oldOwn = shared.getOwnFolder();
            const auto oldKept = shared.getKeptGroups();
            shared.setKeptGroups (0);
            shared.setIRFolder (samFolder);
            shared.setSplitFolder (splitFolder);
            shared.setOwnFolder (own);
            proc.selectPreset ("Halls/01 Large Hall", true);
            proc.prepareToPlay (kRate, 512);
            auto settle = [&proc] {
                AudioBuffer<float> buf (2, 512);
                MidiBuffer midi;
                for (int round = 0; round < 20; ++round)
                {
                    proc.waitUntilIdle (20000);
                    MessageManager::getInstance()->runDispatchLoopUntil (20);
                    for (int i = 0; i < 10; ++i) { buf.clear(); proc.processBlock (buf, midi); }
                    if (! proc.getStatus().loading && ! proc.hasPendingHandover()) break;
                }
            };
            auto value = [&proc] (const char* id) { return proc.getAPVTS().getRawParameterValue (id)->load(); };
            auto set = [&proc] (const char* id, float v) {
                auto* prm = proc.getAPVTS().getParameter (id);
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            };
            settle();
            expect (proc.getLibrary() == IROrigin::Combined);
            const float samNative = proc.getStatus().nativeRT;
            expectWithinAbsoluteError (value (ParamID::decay), samNative, 0.02f);

            // the same preset in the other library, with its own settings: like choosing it
            set (ParamID::early, -3.0f);
            set (ParamID::decay, 3.0f);
            expect (proc.switchLibrary (IROrigin::Split));
            settle();
            expect (proc.getLibrary() == IROrigin::Split);
            expectEquals (proc.getPresetId(), String ("Split/Halls1/01 Large Hall"));
            expect (proc.getStatus().origin == IROrigin::Split);
            expectWithinAbsoluteError (value (ParamID::decay), proc.getStatus().nativeRT, 0.02f, "the preset's own decay");
            expectWithinAbsoluteError (value (ParamID::early), 0.0f, 0.01f, "the preset's own settings");

            // ... except for the kept groups (each switch is a step of its own: closed after a moment of quiet)
            proc.getHistory().flush();
            shared.setKeptGroups (groupBit (ParamGroup::Levels) | groupBit (ParamGroup::Decay));
            set (ParamID::early, -3.0f);
            set (ParamID::decay, 3.0f);
            expect (proc.switchLibrary (IROrigin::Combined));
            settle();
            expectEquals (proc.getPresetId(), String ("Halls/01 Large Hall"));
            expectWithinAbsoluteError (value (ParamID::early), -3.0f, 0.01f, "kept levels");
            expectWithinAbsoluteError (value (ParamID::decay), 3.0f, 0.01f, "kept decay");
            shared.setKeptGroups (0);

            // one undo step back to the other library
            proc.getHistory().flush();
            proc.getHistory().undo();
            settle();
            expectEquals (proc.getPresetId(), String ("Split/Halls1/01 Large Hall"), "undo");

            // the library comes with the preset in the project
            MemoryBlock state;
            proc.getStateInformation (state);
            expect (proc.switchLibrary (IROrigin::Combined));
            proc.setStateInformation (state.getData(), (int) state.getSize());
            expect (proc.getLibrary() == IROrigin::Split, "library restored");
            settle();

            // stepping stays in the library
            proc.selectPresetByOffset (1);
            expectEquals (proc.getPresetId(), String ("Split/Halls2/03 Small Hall"));
            proc.selectPresetByOffset (1);
            expectEquals (proc.getPresetId(), String ("Split/Halls1/01 Large Hall"));

            // a preset without a counterpart: the first of its category in the other library
            proc.selectPreset ("Split/Halls2/03 Small Hall", true);
            settle();
            expect (proc.switchLibrary (IROrigin::Combined));
            settle();
            expectEquals (proc.getPresetId(), String ("Halls/01 Large Hall"));
            expectWithinAbsoluteError (value (ParamID::decay), samNative, 0.02f);

            // OTHER: the own impulse responses, stepping among them only
            expect (proc.switchLibrary (IROrigin::User));
            settle();
            expect (proc.getLibrary() == IROrigin::User);
            expectEquals (proc.getPresetId(), String ("User/Cellars/cellar.wav"));
            expect (proc.getStatus().hasIR);
            expectEquals (proc.getStatus().category, String ("Cellars"));
            proc.selectPresetByOffset (1);
            expectEquals (proc.getPresetId(), String ("User/Cellars/cellar.wav"), "the only one of OTHER");

            // a library that is not there cannot be chosen
            shared.setOwnFolder ({});
            settle();
            expect (! proc.switchLibrary (IROrigin::User), "OTHER without impulse responses");

            shared.setIRFolder (oldIR);
            shared.setSplitFolder (oldSplit);
            shared.setOwnFolder (oldOwn);
            shared.setKeptGroups (oldKept);
            proc.releaseResources();
        }
        root.deleteRecursively();

        realCaptures();
    }

    /** The real captures (--ls). */
    void realCaptures()
    {
        beginTest ("a real library of split captures");
        if (! splitFolder().isDirectory())
        {
            logMessage ("  (skipped: no folder of split captures - use --split <folder> or CONCERT7_SPLIT_DIR)");
            return;
        }
        IRLibrary lib;
        lib.scan (irFolder(), splitFolder());
        expectEquals (lib.count (IROrigin::Split), 221);
        expectEquals (lib.getCategories (IROrigin::Split).joinIntoString (","), String ("Halls,Plates,Rooms,Chambers,Ambiences,Spaces,Nonlin"));
        const auto& patterns = lib.getErPatternPresets (IROrigin::Split);
        expectEquals ((int) patterns.size(), 32);
        expectEquals ((int) std::set<int> (patterns.begin(), patterns.end()).size(), 32, "distinct early reflection patterns");
        if (lib.has (IROrigin::Combined))
        {
            int pairs = 0;
            for (auto& p : lib.getPresets())
                if (p.origin == IROrigin::Combined && lib.counterpart (p.id, IROrigin::Split).isNotEmpty())
                {
                    ++pairs;
                    const auto* q = lib.find (lib.counterpart (p.id, IROrigin::Split));
                    expect (lib.counterpart (q->id, IROrigin::Combined) == p.id, p.id + " both ways");
                }
            expectEquals (pairs, 133, "presets in both libraries");
        }

        beginTest ("every capture loads, is the recording at its own decay, and is analysed plausibly");
        int ownTakes = 0;
        double loadMs = 0.0;
        for (auto& p : lib.getPresets())
        {
            if (p.origin != IROrigin::Split) continue;
            String err;
            const auto t0 = Time::getMillisecondCounterHiRes();
            auto src = loadSourceIR (p, kRate, err);
            loadMs += Time::getMillisecondCounterHiRes() - t0;
            expect (src != nullptr, p.id + ": " + err);
            if (src == nullptr) continue;
            expect (src->hasBlocks() && src->trueStereo, p.id);
            expect (src->nativeRT > 0.1f && src->nativeRT < 15.0f, p.id + " RT " + String (src->nativeRT));
            if (src->hasLateTake()) ++ownTakes;

            DesignParams d;
            d.decay = src->nativeRT;
            auto ir = designIR (*src, nullptr, d);
            float diff = 0.0f;
            for (size_t c = 0; c < 4; ++c)
                diff = std::max (diff, maxDiff (ir->ch[c], src->ch[c]));
            expectEquals (diff, 0.0f, p.id + " is the recording");

            // the highs end in the recording's noise floor, which must not count as reverb
            for (size_t b = 0; b < src->bandFreq.size(); ++b)
                if (src->bandFreq[b] > 9000.0f)
                    expectLessThan (src->bandRT[b], 2.5f * src->nativeRT, p.id + " " + String (roundToInt (src->bandFreq[b])) + " Hz");
        }
        logMessage ("  " + String (ownTakes) + " captures with a take of their own, average load "
                    + String (loadMs / 221.0, 0) + " ms");
        expectGreaterThan (ownTakes, 40, "the second banks' takes have their own fine structure");
    }
};

static SplitLibraryTest splitLibraryTest;
} // namespace concert7::test
