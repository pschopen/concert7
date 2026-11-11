// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Tests against a real library of combined M7 captures (skipped without --irs / CONCERT7_IR_DIR).

#include "TestHelpers.h"
#include "dsp/IRLibrary.h"
#include <map>
#include <set>

namespace concert7::test
{
using namespace juce;

class LibraryParseTest : public UnitTest
{
public:
    LibraryParseTest() : UnitTest ("IR library file names", "concert7-library") {}

    void runTest() override
    {
        beginTest ("bank naming scheme");
        auto p = IRLibrary::parseFileName ("1 Halls 01 Large Hall, 48K L");
        expect (p.ok);
        expectEquals (p.categoryOrder, 1);
        expectEquals (p.category, String ("Halls"));
        expectEquals (p.number, 1);
        expectEquals (p.name, String ("Large Hall"));
        expectEquals (p.sampleRate, 48000);
        expectEquals (p.side, String ("L"));

        p = IRLibrary::parseFileName ("4 Chambers 11 A&M Chamber, 44K");
        expect (p.ok);
        expectEquals (p.name, String ("A&M Chamber"));
        expectEquals (p.sampleRate, 44100);
        expect (p.side.isEmpty());

        p = IRLibrary::parseFileName ("3 Rooms 02 Studio B Close, 44K R");
        expectEquals (p.name, String ("Studio B Close"));
        expectEquals (p.side, String ("R"));

        expect (! IRLibrary::parseFileName ("My Room IR").ok);
        expect (! IRLibrary::parseFileName ("Hall Quad").ok);

        beginTest ("variant choice for host rates");
        IRPreset preset;
        preset.variants[44100].quad = File ("/tmp/a.wav");
        preset.variants[48000].quad = File ("/tmp/b.wav");
        expectEquals (preset.bestVariantFor (44100.0).first, 44100);
        expectEquals (preset.bestVariantFor (48000.0).first, 48000);
        expectEquals (preset.bestVariantFor (88200.0).first, 44100);
        expectEquals (preset.bestVariantFor (96000.0).first, 48000);
        expectEquals (preset.bestVariantFor (192000.0).first, 48000);
        expectEquals (preset.bestVariantFor (32000.0).first, 44100);
    }
};

class LibraryScanTest : public UnitTest
{
public:
    LibraryScanTest() : UnitTest ("Bricasti library", "concert7-library") {}

    void runTest() override
    {
        beginTest ("scan");
        if (! irFolder().isDirectory())
        {
            logMessage ("  (skipped: no IR folder - use --irs <folder> or CONCERT7_IR_DIR)");
            return;
        }

        IRLibrary lib;
        lib.scan (irFolder());
        const auto& presets = lib.getPresets();
        logMessage ("  " + String ((int) presets.size()) + " presets, " + String (lib.numBricastiPresets()) + " Bricasti");
        expectEquals (lib.numBricastiPresets(), 134);
        expectEquals (lib.getCategories (IROrigin::Combined).joinIntoString (","), String ("Halls,Plates,Rooms,Chambers,Ambiences,Spaces"));
        expectEquals ((int) lib.getErPatternPresets().size(), 32);
        std::set<int> unique (lib.getErPatternPresets().begin(), lib.getErPatternPresets().end());
        expectEquals ((int) unique.size(), 32, "distinct early reflection patterns");

        int complete = 0;
        for (auto& p : presets)
            if (p.categoryOrder < 100)
                for (auto& [rate, v] : p.variants)
                    if (v.isTrueStereo()) { ++complete; break; }
        expectEquals (complete, 134, "every preset has a true stereo variant");

        beginTest ("quad files and L/R pairs contain the same data");
        {
            const auto* hall = lib.find ("Halls/01 Large Hall");
            expect (hall != nullptr);
            if (hall != nullptr && hall->variants.count (48000) > 0)
            {
                const auto& v = hall->variants.at (48000);
                if (v.quad != File() && v.left != File() && v.right != File())
                {
                    IRPreset quadOnly = *hall, pairOnly = *hall;
                    quadOnly.variants = { { 48000, IRVariant { v.quad, {}, {}, {}, {}, {} } } };
                    pairOnly.variants = { { 48000, IRVariant { {}, v.left, v.right, {}, {}, {} } } };
                    String err;
                    auto a = loadSourceIR (quadOnly, 48000.0, err);
                    auto b = loadSourceIR (pairOnly, 48000.0, err);
                    expect (a != nullptr && b != nullptr);
                    if (a && b)
                    {
                        float maxDiff = 0.0f;
                        for (int c = 0; c < 4; ++c)
                            for (size_t i = 0; i < a->ch[(size_t) c].size(); ++i)
                                maxDiff = std::max (maxDiff, std::abs (a->ch[(size_t) c][i] - b->ch[(size_t) c][i]));
                        expectLessThan (maxDiff, 1e-6f);
                        expect (a->trueStereo && b->trueStereo);
                    }
                }
                else
                    logMessage ("  (pair/quad comparison skipped: only one format present)");
            }
        }

        beginTest ("every preset loads and analyses plausibly (48 kHz)");
        Array<double> loadTimes;
        for (auto& p : presets)
        {
            if (p.categoryOrder >= 100) continue;
            String err;
            const auto t0 = Time::getMillisecondCounterHiRes();
            auto src = loadSourceIR (p, 48000.0, err);
            loadTimes.add (Time::getMillisecondCounterHiRes() - t0);
            expect (src != nullptr, p.id + ": " + err);
            if (src == nullptr) continue;
            expect (src->trueStereo, p.id + " true stereo");
            expect (src->nativeRT > 0.1f && src->nativeRT < 15.0f, p.id + " RT " + String (src->nativeRT));
            expect (src->onset < (int) (0.25 * 48000), p.id + " onset");
        }
        double sum = 0; for (auto t : loadTimes) sum += t;
        logMessage ("  average load + analysis time " + String (sum / std::max (1, loadTimes.size()), 1) + " ms");

        beginTest ("every preset at its own decay is the recording itself");
        for (auto& p : presets)
        {
            if (p.categoryOrder >= 100) continue;
            String err;
            auto src = loadSourceIR (p, 48000.0, err);
            if (src == nullptr) continue;
            DesignParams d;
            d.decay = src->nativeRT;
            auto ir = designIR (*src, nullptr, d);
            float maxDiff = 0.0f;
            for (size_t c = 0; c < 4; ++c)
                for (size_t i = 0; i < std::max (ir->ch[c].size(), src->ch[c].size()); ++i)
                    maxDiff = std::max (maxDiff, std::abs ((i < ir->ch[c].size() ? ir->ch[c][i] : 0.0f)
                                                           - (i < src->ch[c].size() ? src->ch[c][i] : 0.0f)));
            expectLessThan (maxDiff, 1e-6f, p.id);
        }

        beginTest ("DECAY changes the bass at most as much as the mids");
        for (float factor : { 2.0f, 0.5f })
        {
            // per band: level against the recording stretched in time by factor, less that of 1 kHz
            std::map<int, std::vector<float>> excess;
            for (auto& p : presets)
            {
                if (p.categoryOrder >= 100) continue;
                String err;
                auto src = loadSourceIR (p, 48000.0, err);
                if (src == nullptr) continue;
                DesignParams d;
                d.decay = jlimit (0.2f, 30.0f, src->nativeRT * factor);
                const float f = d.decay / src->nativeRT;
                auto ir = designIR (*src, nullptr, d);
                auto above = [&] (float fc) { return aboveStretchedDb (bandBlocks (ir->ch, 48000.0, fc), bandBlocks (src->ch, 48000.0, fc), f); };
                const float mids = above (1000.0f);
                for (int fc : { 50, 63, 80, 100, 125, 160 })
                {
                    const float bass = above ((float) fc) - mids;
                    excess[fc].push_back (bass);
                    if (factor > 1.0f ? bass > 3.0f : bass < -3.0f)
                        logMessage ("  x" + String (factor) + " " + p.id + ": " + String (fc) + " Hz " + String (bass, 1) + " dB");
                }
            }
            String line ("  x" + String (factor) + ", bass against the mids (median / " + (factor > 1.0f ? "max" : "min") + "):");
            for (auto& [fc, v] : excess)
            {
                std::sort (v.begin(), v.end());
                const float median = v[v.size() / 2], extreme = factor > 1.0f ? v.back() : v.front();
                line << " " << fc << " Hz " << String (median, 1) << " / " << String (extreme, 1);
                if (factor > 1.0f) expectLessThan (median, 1.0f, String (fc) + " Hz grows more than DECAY");
                else               expectGreaterThan (median, -1.0f, String (fc) + " Hz shrinks more than DECAY");
            }
            logMessage (line + " dB");
        }

        beginTest ("decay control on every preset: reaches the target, no level jumps, finite output");
        const double sr = 48000.0;
        int exponential = 0, onTarget = 0, designs = 0, jumps = 0;
        float worstExcess = 0.0f, loudestEnd = -1000.0f;
        String worstPreset, loudestEndPreset;
        for (auto& p : presets)
        {
            if (p.categoryOrder >= 100) continue;
            String err;
            auto src = loadSourceIR (p, sr, err);
            if (src == nullptr) continue;

            auto lateOnly = [&] (const std::array<std::vector<float>, 4>& ch, float splitSeconds) {
                auto late = ch;
                const int split = src->onset + (int) ((splitSeconds + 0.02f) * sr);
                for (auto& c : late) for (int k = 0; k < std::min ((int) c.size(), split); ++k) c[(size_t) k] = 0.0f;
                return late;
            };
            // largest rise of the 50 ms broadband level after the onset region (a jump = audible artefact)
            auto maxRise = [&] (const std::array<std::vector<float>, 4>& ch, bool ts) {
                const int seg = (int) (0.05 * sr);
                float prev = -1000.0f, rise = 0.0f;
                for (int pos = src->onset + (int) (0.3 * sr); pos + seg < (int) ch[0].size(); pos += seg)
                {
                    double e = 0.0;
                    for (auto* c : chans (ch, ts)) e += energy (*c, pos, pos + seg);
                    const float db = (float) (10.0 * std::log10 (e / seg + 1e-20));
                    if (prev > -900.0f && db > -100.0f) rise = std::max (rise, db - prev);
                    prev = db;
                }
                return rise;
            };

            // presets whose late decay is a clean exponential can be checked against the RT target
            const float nativeT20 = estimateRT (chans (lateOnly (src->ch, 0.08f), src->trueStereo), sr, 500.0f, 2000.0f);
            const bool isExponential = std::isfinite (nativeT20) && std::abs (nativeT20 - src->nativeRT) < 0.15f * src->nativeRT;
            const float nativeRise = maxRise (src->ch, src->trueStereo);

            for (float factor : { 0.5f, 2.0f, 4.0f })
            {
                DesignParams d;
                d.decay = jlimit (0.2f, 30.0f, src->nativeRT * factor);
                auto ir = designIR (*src, nullptr, d);
                ++designs;
                bool finite = true;
                for (auto& c : ir->ch) for (float v : c) finite = finite && std::isfinite (v) && std::abs (v) < 10.0f;
                expect (finite, p.id + " finite");

                // the end is not heard: before the fade-out over its last 10 % the response lies far below its loudest
                // 50 ms (a response cut while it still rings would end there)
                expectLessOrEqual (ir->length(), (int) (kMaxBuiltIRSeconds * sr), p.id + " length");
                {
                    const int seg = (int) (0.05 * sr), n = ir->length();
                    double loudest = 0.0, end = 0.0;
                    for (int pos = 0; pos + seg <= n; pos += seg)
                    {
                        double e = 0.0;
                        for (auto* c : chans (ir->ch, ir->trueStereo)) e += energy (*c, pos, pos + seg);
                        loudest = std::max (loudest, e / seg);
                    }
                    const int a = (int) (0.85 * n), b = (int) (0.9 * n);
                    for (auto* c : chans (ir->ch, ir->trueStereo)) end += energy (*c, a, b);
                    const float endDb = (float) (10.0 * std::log10 ((end / std::max (1, b - a) + 1e-30) / (loudest + 1e-30)));
                    if (endDb > loudestEnd) { loudestEnd = endDb; loudestEndPreset = p.id + " x" + String (factor); }
                }

                const float excess = maxRise (ir->ch, ir->trueStereo) - std::max (3.0f, nativeRise);
                if (excess > worstExcess) { worstExcess = excess; worstPreset = p.id + " x" + String (factor); }
                if (excess > 3.0f) ++jumps;

                if (isExponential)
                {
                    ++exponential;
                    const float rt = estimateRT (chans (lateOnly (ir->ch, ir->splitSeconds), ir->trueStereo), sr, 500.0f, 2000.0f);
                    if (std::isfinite (rt) && std::abs (rt - d.decay) < d.decay * 0.2f) ++onTarget;
                    else logMessage ("  " + p.id + " (exponential): target " + String (d.decay, 2) + " measured " + String (rt, 2));
                }
            }
        }
        logMessage ("  " + String (designs) + " designs, " + String (onTarget) + " / " + String (exponential)
                    + " exponential decays within 20 % of the target, " + String (jumps) + " with level jumps > 3 dB"
                    + (worstPreset.isNotEmpty() ? " (worst: " + worstPreset + ", +" + String (worstExcess, 1) + " dB)" : String()));
        logMessage ("  loudest end of a response: " + String (-loudestEnd, 1) + " dB below its loudest 50 ms (" + loudestEndPreset + ")");
        expectLessThan (loudestEnd, -75.0f, "no response ends while it can still be heard");
        expectGreaterOrEqual (onTarget, exponential * 9 / 10, "90 % of exponential presets on target");
        expectLessOrEqual (jumps, designs / 50, "at most 2 % of designs with level jumps");
    }
};

static LibraryParseTest libraryParseTest;
static LibraryScanTest libraryScanTest;
} // namespace concert7::test
