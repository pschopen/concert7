// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Libraries of split M7 captures and other impulse responses: folder names, scan, early reflections and late reverb as
// blocks of their own, the SAM / LS / OTHER switch. Synthetic captures always; real ones with --split / CONCERT7_SPLIT_DIR.

#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "dsp/ConvolutionEngine.h"
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

    /** A capture with four takes: reflections that differ slightly (take 3 the same as take 1), late reverbs with a
        fine structure of their own (take 3 a copy of take 1). */
    void writeTakes (const File& root, const String& bank, const String& preset, float rt60, int seed)
    {
        const auto dir = root.getChildFile (bank).getChildFile (preset);
        const auto early = syntheticEarly (seed);
        const auto late = syntheticLate (rt60, seed + 1);
        for (int take = 1; take <= 4; ++take)
        {
            auto head = early;
            if (take == 2 || take == 4)
            {
                Random rng (seed + 10 * take);
                for (auto& c : head) for (auto& v : c) v *= 1.0f + 0.05f * (rng.nextFloat() * 2.0f - 1.0f);
            }
            writeWav (dir.getChildFile ("head-" + String (take) + ".wav"), head);
            writeWav (dir.getChildFile ("tail-" + String (take) + ".wav"),
                      take == 1 || take == 3 ? late : syntheticLate (rt60, seed + 1 + take));
        }
    }

    /** First sample above -60 dB of the peak, over all channels. */
    int firstSound (const std::array<std::vector<float>, 4>& ch)
    {
        float peak = 0.0f;
        for (auto& c : ch) for (float v : c) peak = std::max (peak, std::abs (v));
        int first = std::numeric_limits<int>::max();
        for (auto& c : ch)
            for (int i = 0; i < (int) c.size(); ++i)
                if (std::abs (c[(size_t) i]) > peak * 1e-3f) { first = std::min (first, i); break; }
        return first;
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

        beginTest ("takes: loaded when needed, built like the first, the variant left to them");
        {
            root.deleteRecursively();
            writeTakes (splitFolder, "Halls2", "02 Large Hall", 1.5f, 51);
            IRLibrary lib;
            lib.scan ({}, splitFolder);
            String err;
            const auto* preset = lib.find ("Split/Halls2/02 Large Hall");
            auto src = preset != nullptr ? loadSourceIR (*preset, kRate, err) : nullptr;
            expect (src != nullptr, err);
            if (src != nullptr)
            {
                expect (! src->takesLoaded && src->lateTakes.empty(), "the takes are loaded only when needed");
                expect (loadSourceTakes (*preset, *src, err), err);
                expect (src->takesLoaded);
                expectEquals ((int) src->earlyTakes.size(), 2, "reflections like take 1's are left out");
                expectEquals ((int) src->lateTakes.size(), 2, "a late reverb that is a copy is left out");
                for (auto* takes : { &src->earlyTakes, &src->lateTakes })
                    for (auto& take : *takes)
                        for (size_t c = 0; c < 4; ++c)
                            expectEquals ((int) take[c].size(), src->length(), "a take as long as the blocks");
                float diff = 0.0f;
                for (size_t c = 0; c < 4; ++c)
                    diff = std::max (diff, maxDiff (src->lateTakes[0][c], src->lateTake[c]));
                expectEquals (diff, 0.0f, "take 2 made like the variant of the blocks: same scale");

                // off: nothing of the takes; late: the late reverbs of all takes, as recorded at the preset's own decay
                DesignParams d;
                d.decay = src->nativeRT;
                d.splitLate = d.lateVariant = true;
                auto ir = designIR (*src, nullptr, d);
                expect (ir->earlyTakes.empty() && ir->lateTakes.empty(), "no takes while off");
                expect (ir->hasVariant());
                d.takes = TakeMode::Late;
                ir = designIR (*src, nullptr, d);
                expect (ir->earlyTakes.empty(), "LATE: the reflections stay take 1");
                expectEquals ((int) ir->lateTakes.size(), 2);
                expect (! ir->hasVariant(), "the takes move the late reverb, no variant");
                diff = 0.0f;
                for (size_t t = 0; t < ir->lateTakes.size(); ++t)
                    for (size_t c = 0; c < 4; ++c)
                        diff = std::max (diff, maxDiff (ir->lateTakes[t][c], src->lateTakes[t][c]));
                expectEquals (diff, 0.0f, "every take as recorded");
                expectEquals ((int) ir->lateTakeRho.size(), 3, "neighbours in the sequence of three takes");
                for (float r : ir->lateTakeRho)
                    expectLessThan (std::abs (r), 0.3f, "late takes of their own are hardly alike");

                d.takes = TakeMode::EarlyAndLate;
                ir = designIR (*src, nullptr, d);
                expectEquals ((int) ir->earlyTakes.size(), 2);
                diff = 0.0f;
                for (size_t t = 0; t < ir->earlyTakes.size(); ++t)
                    for (size_t c = 0; c < 4; ++c)
                        diff = std::max (diff, maxDiff (ir->earlyTakes[t][c], src->earlyTakes[t][c]));
                expectEquals (diff, 0.0f, "the reflections of every take as recorded");
                for (float r : ir->earlyTakeRho)
                    expectGreaterThan (r, 0.9f, "the reflections of the takes are alike");

                // a longer decay: every take decays like the first
                d.decay = src->nativeRT * 1.5f;
                ir = designIR (*src, nullptr, d);
                const float rtLate = estimateRT (chans (ir->late), kRate, 500.0f, 2000.0f);
                for (auto& take : ir->lateTakes)
                    expectWithinAbsoluteError (estimateRT (chans (take), kRate, 500.0f, 2000.0f), rtLate, 0.08f * rtLate,
                                               "a take at decay x1.5");

                // Stretch: the takes are stretched with the blocks
                d.decayMode = DecayMode::Stretch;
                d.decay = src->nativeRT * 1.25f;
                ir = designIR (*src, nullptr, d);
                expectEquals ((int) ir->lateTakes.size(), 2, "Stretch keeps the takes");
                if (! ir->lateTakes.empty())
                    expectEquals (ir->lateTakes[0][0].size(), ir->late[0].size());
            }
        }

        beginTest ("late offset: the late block against the reflections, earlier at most to its first sound");
        {
            String err;
            IRLibrary lib;
            lib.scan ({}, splitFolder);
            const auto* preset = lib.find ("Split/Halls2/02 Large Hall");
            auto src = preset != nullptr ? loadSourceIR (*preset, kRate, err) : nullptr;
            expect (src != nullptr, err);
            if (src != nullptr)
            {
                DesignParams d;
                d.decay = src->nativeRT;
                d.splitLate = true;
                d.lateOffsetMs = 10.0f;
                auto ir = designIR (*src, nullptr, d);
                const int shift = (int) std::lround (0.01 * kRate);
                float diff = 0.0f;
                for (size_t c = 0; c < 4; ++c)
                {
                    std::vector<float> shifted ((size_t) shift, 0.0f);
                    shifted.insert (shifted.end(), src->lateBlock[c].begin(), src->lateBlock[c].end());
                    diff = std::max (diff, maxDiff (ir->late[c], shifted));
                }
                expectEquals (diff, 0.0f, "+10 ms: the late block 10 ms later");
                diff = 0.0f;
                for (size_t c = 0; c < 4; ++c)
                    diff = std::max (diff, maxDiff (ir->ch[c], src->earlyBlock[c]));
                expectEquals (diff, 0.0f, "the reflections stay");

                d.lateOffsetMs = -kMaxLateOffsetMs;
                ir = designIR (*src, nullptr, d);
                expectEquals (firstSound (ir->late), 0, "-200 ms: the late block starts with its first sound");
                const int first = firstSound (src->lateBlock);
                double kept = 0.0, all = 0.0;
                for (size_t c = 0; c < 4; ++c)
                {
                    kept += energy (ir->late[c]);
                    all += energy (src->lateBlock[c]);
                }
                expectGreaterThan (first, 0);
                expectWithinAbsoluteError ((float) (kept / all), 1.0f, 1.0e-5f, "nothing of the late reverb is cut");

                // the other libraries: no late offset (their late reverb is cut out of one response)
                auto plain = makeSourceIR (src->ch, true, kRate, kRate, "plain");
                DesignParams q;
                q.decay = plain->nativeRT;
                auto a = designIR (*plain, nullptr, q);
                q.lateOffsetMs = 50.0f;
                auto b = designIR (*plain, nullptr, q);
                diff = 0.0f;
                for (size_t c = 0; c < 4; ++c)
                    diff = std::max (diff, maxDiff (a->ch[c], b->ch[c]));
                expectEquals (diff, 0.0f, "a combined response ignores the late offset");
            }
            root.deleteRecursively();
        }

        beginTest ("the convolution fades from take to take and keeps the level");
        {
            // take k: an impulse at 10 k samples, in the early part (the late reverb works alike)
            auto ir = std::make_shared<BuiltIR>();
            ir->sampleRate = kRate;
            ir->trueStereo = false;
            auto impulseAt = [] (int pos) {
                std::array<std::vector<float>, 4> ch;
                ch[0].assign (64, 0.0f);
                ch[0][(size_t) pos] = 1.0f;
                ch[3] = ch[0];
                return ch;
            };
            ir->ch = impulseAt (0);
            ir->earlyTakes = { impulseAt (10), impulseAt (20) };
            for (float rho : { 0.0f, 1.0f })
            {
                ir->earlyTakeRho = { rho, rho, rho };
                TrueStereoConvolver conv;
                const int n = 64;
                std::vector<float> in ((size_t) n, 0.0f), zero ((size_t) n, 0.0f), scratch ((size_t) (n * TrueStereoConvolver::kScratchPerSample));
                std::vector<std::vector<float>> bufs (2 * WetStreams::kNumStreams, std::vector<float> ((size_t) n));
                WetStreams out;
                for (size_t i = 0; i < out.ch.size(); ++i) out.ch[i] = bufs[i].data();
                in[0] = 1.0f;
                auto at = [&] (double position) {
                    conv.load (ir, 64, 8192);
                    const TakeClock clock { position, 0.0 };
                    conv.process (in.data(), zero.data(), out, n, true, scratch.data(), &clock);
                    return std::array<float, 3> { out.l (0)[0], out.l (0)[10], out.l (0)[20] };
                };
                // (the convolution is exact to float rounding)
                auto only = [&] (const std::array<float, 3>& w, int take, const String& what) {
                    for (int t = 0; t < 3; ++t)
                        expectWithinAbsoluteError (w[(size_t) t], t == take ? 1.0f : 0.0f, 1.0e-5f, what);
                };
                only (at (0.0), 0, "at 0: take 0");
                only (at (1.0), 1, "at 1: take 1");
                auto w = at (0.5);
                const float half = rho > 0.5f ? 0.5f : std::sqrt (0.5f);
                expectWithinAbsoluteError (w[0], half, 1.0e-5f, "half way, rho " + String (rho));
                expectWithinAbsoluteError (w[1], half, 1.0e-5f);
                w = at (2.5);
                expectWithinAbsoluteError (w[2], half, 1.0e-5f, "the last take fades into the first");
                expectWithinAbsoluteError (w[0], half, 1.0e-5f);
                only (at (3.0), 0, "the sequence starts again");
                // the level of the mix of independent takes stays: the weights' squares add up to one
                for (double pos : { 0.2, 0.7, 1.4, 2.9 })
                {
                    w = at (pos);
                    if (rho < 0.5f)
                        expectWithinAbsoluteError (w[0] * w[0] + w[1] * w[1] + w[2] * w[2], 1.0f, 1.0e-5f, "power at " + String (pos));
                    else
                        expectWithinAbsoluteError (w[0] + w[1] + w[2], 1.0f, 1.0e-5f, "gain at " + String (pos));
                }
                conv.unload();
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

            // the takes: off when a preset is chosen; on, the late reverb moves through them (and Drift has nothing
            // to do); a library of combined responses has none
            expectEquals (value (ParamID::takeMode), 0.0f, "takes off for a chosen preset");
            expect (proc.getCurrentIR() != nullptr && proc.getCurrentIR()->lateTakes.empty());
            set (ParamID::takeMode, (float) TakeMode::Late);
            settle();
            if (auto ir = proc.getCurrentIR())
            {
                expectEquals ((int) ir->lateTakes.size(), 1, "LATE: the other take of the late reverb");
                expect (ir->hasLate() && ! ir->hasVariant());
            }
            set (ParamID::takeMode, (float) TakeMode::Off);
            settle();
            expect (proc.switchLibrary (IROrigin::Combined));
            settle();
            expectEquals (proc.getPresetId(), String ("Halls/01 Large Hall"));
            expectWithinAbsoluteError (value (ParamID::decay), samNative, 0.02f);
            set (ParamID::takeMode, (float) TakeMode::EarlyAndLate);
            settle();
            if (auto ir = proc.getCurrentIR())
                expect (ir->earlyTakes.empty() && ir->lateTakes.empty() && ! ir->hasLate(),
                        "combined responses: no takes, nothing split off");
            set (ParamID::takeMode, (float) TakeMode::Off);

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

        beginTest ("the takes of real captures: all of the second banks', the reflections of all");
        for (auto [id, lateTakes] : { std::pair { "Split/Halls2/01 Large Hall", 7 }, std::pair { "Split/Halls1/01 Large Hall", 0 } })
        {
            const auto* p = lib.find (id);
            expect (p != nullptr, id);
            if (p == nullptr) continue;
            String err;
            auto src = loadSourceIR (*p, kRate, err);
            const auto t0 = Time::getMillisecondCounterHiRes();
            expect (src != nullptr && loadSourceTakes (*p, *src, err), err);
            if (src == nullptr) continue;
            logMessage ("  " + String (id) + ": " + String ((int) src->earlyTakes.size()) + " + 1 early, "
                        + String ((int) src->lateTakes.size()) + " + 1 late takes, loaded in "
                        + String (Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
            expectEquals ((int) src->lateTakes.size(), lateTakes, String (id) + " late takes");
            expectEquals ((int) src->earlyTakes.size(), 7, String (id) + " the reflections differ a little");
            DesignParams d;
            d.decay = src->nativeRT;
            d.splitLate = true;
            d.takes = TakeMode::EarlyAndLate;
            auto ir = designIR (*src, nullptr, d);
            for (float r : ir->lateTakeRho)
                expectLessThan (std::abs (r), 0.3f, String (id) + " late neighbours");
            for (float r : ir->earlyTakeRho)
                expectGreaterThan (r, 0.85f, String (id) + " early neighbours");
        }
    }
};

static SplitLibraryTest splitLibraryTest;
} // namespace concert7::test
