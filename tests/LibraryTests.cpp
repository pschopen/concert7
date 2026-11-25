// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Tests against a real library of combined M7 captures (skipped without --irs / CONCERT7_IR_DIR).

#include "TestHelpers.h"
#include "dsp/IRLibrary.h"
#include "dsp/WIRFile.h"
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

        beginTest ("WIR library naming scheme");
        auto w = IRLibrary::parseWirName ("Example Hall_scg1v2");
        expect (w.ok && w.sampled && ! w.surround);
        expectEquals (w.position, 1);
        expectEquals (w.mic, String ("Cardioid"));
        w = IRLibrary::parseWirName ("Hillside Concert Hall_xcd4");
        expect (w.ok && w.sampled && ! w.surround);
        expectEquals (w.position, 4);
        expectEquals (IRLibrary::parseWirName ("Old Gallery_mog2v2").mic, String ("Omni"));
        expectEquals (IRLibrary::parseWirName ("Town Church_sWg3v2").mic, String ("Wide"));
        expectEquals (IRLibrary::parseWirName ("Film Stage_sdg1v2").mic, String ("Binaural"));
        expect (IRLibrary::parseWirName ("Arena 1_MBg1v2").surround);
        expect (IRLibrary::parseWirName ("Arena 1_SBg1v2").surround);
        expect (IRLibrary::parseWirName ("Arena 1_MQg1v2").surround);
        w = IRLibrary::parseWirName ("Unit7_s_Room_B");
        expect (w.ok && ! w.sampled && ! w.surround);
        expectEquals (w.name, String ("Room B"));
        expectEquals (IRLibrary::parseWirName ("Synthetic_m_Round Hall").name, String ("Round Hall"));
        expect (! IRLibrary::parseWirName ("My Room IR").ok);
    }
};

/** A .wir file: a 32 bit float WAV file with the header renamed as in the WIR library. */
static void writeWir (const File& file, const AudioBuffer<float>& buffer, double sampleRate)
{
    MemoryBlock block;
    {
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> out = std::make_unique<MemoryOutputStream> (block, false);
        auto writer = wav.createWriterFor (out, AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                    .withNumChannels (buffer.getNumChannels())
                                                                    .withBitsPerSample (32)
                                                                    .withSampleFormat (AudioFormatWriterOptions::SampleFormat::floatingPoint));
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }
    auto* d = static_cast<uint8*> (block.getData());
    std::memcpy (d, "wvIR", 4);
    std::memcpy (d + 8, "ver1", 4);
    for (size_t pos = 12; pos + 24 <= block.getSize();)
    {
        const auto size = ByteOrder::littleEndianInt (d + pos + 4);
        if (std::memcmp (d + pos, "fmt ", 4) == 0) { d[pos + 8 + 14] = 23; break; }
        pos += 8 + size + (size & 1);
    }
    file.replaceWithData (block.getData(), block.getSize());
}

class WIRFileTest : public UnitTest
{
public:
    WIRFileTest() : UnitTest ("WIR files", "concert7-library") {}

    void runTest() override
    {
        TemporaryFile tmp;
        const auto root = tmp.getFile();
        const auto space = root.getChildFile ("Lib/Sampled Acoustics V2/Concert Halls/Test Hall");
        space.createDirectory();
        const double sr = 96000.0;
        auto make = [sr] (int channels) {
            AudioBuffer<float> b (channels, (int) sr);
            Random rng (channels);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (c, i, 1000.0f * (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-6.9f * (float) i / (float) sr));
            for (int c = 0; c < channels; ++c)
                b.setSample (c, 100 + 50 * c, 3000.0f); // a marker per channel
            return b;
        };
        const auto quad = make (4);
        writeWir (space.getChildFile ("Test Hall_scg1v2.wir"), quad, sr);
        writeWir (space.getChildFile ("Test Hall_xcg1v2.wir"), make (2), sr);
        writeWir (space.getChildFile ("Test Hall_mcg1v2.wir"), make (1), sr);
        writeWir (space.getChildFile ("Test Hall_MBg1v2.wir"), make (3), sr);
        writeWir (space.getChildFile ("Test Hall_xcg2v2.wir"), make (2), sr);
        writeWir (space.getChildFile ("Test Hall_sWg1v2.wir"), quad, sr);
        // a preset names the position of the second position's files (the first has none, so keeps its number)
        space.getChildFile ("Test Hall_Pt2v2.xps").replaceWithText (
            "<?xml version=\"1.0\"?><PresetXMLTree><Preset Name=\"Test Hall (Row 18)\"><PresetData>"
            "<PluginSpecific><Descriptors><Descriptor Name=\"IRFileNameFull\">Test Hall_xcg2v2.wir</Descriptor>"
            "<Descriptor Name=\"MicLocation\">Row 18</Descriptor><Descriptor Name=\"Distance\">12.250</Descriptor>"
            "</Descriptors></PluginSpecific></PresetData></Preset></PresetXMLTree>");
        const auto synthetic = root.getChildFile ("Lib/Virtual Acoustics/Synthetic");
        synthetic.createDirectory();
        writeWir (synthetic.getChildFile ("Synthetic_m_Smooth_Hall.wir"), make (1), sr);
        writeWir (synthetic.getChildFile ("Synthetic_S_Big Hall.wir"), make (4), sr);
        const auto lx = root.getChildFile ("Lib/Virtual Acoustics/Devices/Unit7");
        lx.createDirectory();
        writeWir (lx.getChildFile ("Unit7_x_A_Plate.wir"), make (2), sr);
        const auto nested = root.getChildFile ("Lib/Sampled Acoustics V2/Recording Studios/Studio A/Absorptive");
        nested.createDirectory();
        writeWir (nested.getChildFile ("Studio A - Absorptive_xcg1v2.wir"), make (2), sr);
        writeWir (nested.getChildFile ("Studio A_x_Reverb Chamber.wir"), make (2), sr);
        writeWir (root.getChildFile ("Lib/Odd name.wir"), make (2), sr);

        beginTest ("reading a .wir file");
        expectEquals (WIRAudioFormat::numChannels (space.getChildFile ("Test Hall_scg1v2.wir")), 4);
        {
            AudioFormatManager fm;
            registerIRFormats (fm);
            std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (space.getChildFile ("Test Hall_scg1v2.wir")));
            expect (reader != nullptr);
            if (reader != nullptr)
            {
                expectEquals ((int) reader->numChannels, 4);
                expectEquals (reader->sampleRate, sr);
                AudioBuffer<float> b (4, quad.getNumSamples());
                reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                float maxDiff = 0.0f;
                for (int c = 0; c < 4; ++c)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        maxDiff = std::max (maxDiff, std::abs (b.getSample (c, i) - quad.getSample (c, i)));
                expectEquals (maxDiff, 0.0f);
            }
        }

        beginTest ("scan: one preset per space, position and microphones, named by the positions the presets give");
        IRLibrary lib;
        lib.scan ({}, {}, root);
        StringArray ids;
        for (auto& p : lib.getPresets()) ids.add (p.id + "|" + p.category + "|" + p.name);
        expectEquals (ids.joinIntoString (";"),
                      String ("User/Lib/Sampled Acoustics V2/Concert Halls/Test Hall/1 Cardioid|Concert Halls|Test Hall 1 Cardioid;"
                              "User/Lib/Sampled Acoustics V2/Concert Halls/Test Hall/1 Wide|Concert Halls|Test Hall 1 Wide;"
                              "User/Lib/Sampled Acoustics V2/Concert Halls/Test Hall/2 Cardioid|Concert Halls|Test Hall - Row 18 (12 m) Cardioid;"
                              "User/Lib/Virtual Acoustics/Devices/Unit7/A Plate|Devices|Unit7 A Plate;"
                              "User/Lib/Odd name.wir|Lib|Odd name;"
                              "User/Lib/Sampled Acoustics V2/Recording Studios/Studio A/Absorptive/1 Cardioid|Recording Studios|Studio A Absorptive 1 Cardioid;"
                              "User/Lib/Sampled Acoustics V2/Recording Studios/Studio A/Absorptive/Reverb Chamber|Recording Studios|Studio A Absorptive Reverb Chamber;"
                              "User/Lib/Virtual Acoustics/Synthetic/Big Hall|Synthetic|Big Hall;"
                              "User/Lib/Virtual Acoustics/Synthetic/Smooth Hall|Synthetic|Smooth Hall"));
        const auto* cardioid = lib.find ("User/Lib/Sampled Acoustics V2/Concert Halls/Test Hall/1 Cardioid");
        expect (cardioid != nullptr && cardioid->variants.at (0).single.getFileName() == "Test Hall_scg1v2.wir");

        beginTest ("loading: true stereo, wide microphones with their outputs swapped");
        auto marker = [] (const std::vector<float>& x) {
            return (int) std::distance (x.begin(), std::max_element (x.begin(), x.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }));
        };
        String err;
        auto a = loadSourceIR (*cardioid, sr, err);
        auto w = loadSourceIR (*lib.find ("User/Lib/Sampled Acoustics V2/Concert Halls/Test Hall/1 Wide"), sr, err);
        expect (a != nullptr && w != nullptr, err);
        if (a != nullptr && w != nullptr)
        {
            expect (a->trueStereo && w->trueStereo);
            const int order[4] = { 1, 0, 3, 2 };
            for (int c = 0; c < 4; ++c)
            {
                expectEquals (marker (a->ch[(size_t) c]), marker (a->ch[0]) + 50 * c);
                expectEquals (marker (w->ch[(size_t) c]), marker (a->ch[0]) + 50 * order[c]);
            }
        }
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
                if (excess > 3.0f)
                {
                    ++jumps;
                    logMessage ("  " + p.id + " x" + String (factor) + ": level jump +" + String (excess, 1) + " dB");
                }

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
static WIRFileTest wirFileTest;
static LibraryScanTest libraryScanTest;
} // namespace concert7::test
