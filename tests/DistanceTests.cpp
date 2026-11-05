// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "TestHelpers.h"
#include "dsp/Distance.h"
#include "dsp/PostProcessing.h"
#include "Parameters.h"
#include <complex>

namespace concert7::test
{
using namespace juce;

namespace
{
    /** Magnitude in dB of an FIR at a frequency. */
    double firResponseDb (const std::vector<float>& h, double freq, double sr)
    {
        std::complex<double> acc;
        for (size_t n = 0; n < h.size(); ++n)
            acc += (double) h[n] * std::polar (1.0, -2.0 * MathConstants<double>::pi * freq * (double) n / sr);
        return 20.0 * std::log10 (std::abs (acc) + 1e-30);
    }
}

class DistanceTest : public UnitTest
{
public:
    DistanceTest() : UnitTest ("Distance", "concert7-dsp") {}

    void runTest() override
    {
        beginTest ("air absorption follows ISO 9613-1 (reference table at 20 C, 70 %)");
        {
            const std::pair<double, double> table[] = { { 500.0, 2.8 }, { 1000.0, 5.0 }, { 2000.0, 9.0 },
                                                        { 4000.0, 22.9 }, { 8000.0, 76.6 } };
            for (auto [f, dbPerKm] : table)
                expectWithinAbsoluteError (airAbsorptionDbPerMetre (f, 70.0) * 1000.0, dbPerKm, dbPerKm * 0.03,
                                           String (f) + " Hz");
            expectGreaterThan (airAbsorptionDbPerMetre (8000.0, 20.0), airAbsorptionDbPerMetre (8000.0, 70.0),
                               "dry air absorbs more at high frequencies");
        }

        beginTest ("Barron: direct to reverberant ratio, critical distance, first reflection gap");
        {
            expectWithinAbsoluteError (barronDrrDb (3.0f, 2.0f, 18000.0f), 5.3f, 0.2f);
            expectWithinAbsoluteError (barronDrrDb (15.0f, 2.0f, 18000.0f), -7.6f, 0.2f);
            expectWithinAbsoluteError (barronDrrDb (30.0f, 2.0f, 18000.0f), -12.3f, 0.2f);
            // classical estimate 0.057 sqrt (V / T) = 5.4 m; the revised theory is a little larger
            const float rc = distanceForDrr (0.0f, 2.0f, 18000.0f);
            expectWithinAbsoluteError (rc, 5.7f, 0.4f);
            expectWithinAbsoluteError (barronDrrDb (rc, 2.0f, 18000.0f), 0.0f, 0.01f, "equal energies at rc");
            expectWithinAbsoluteError (distanceForDrr (barronDrrDb (11.0f, 1.7f, 9000.0f), 1.7f, 9000.0f), 11.0f, 0.01f);
            const float g3 = initialGapMs (3.0f, 18000.0f), g15 = initialGapMs (15.0f, 18000.0f), g30 = initialGapMs (30.0f, 18000.0f);
            expect (g3 > g15 && g15 > g30, "first reflection gap shrinks with distance");
            expect (g15 > 10.0f && g15 < 35.0f, "typical gap in the stalls: " + String (g15));
        }

        beginTest ("hall model: Eyring reverb time and its inverse");
        {
            // a 16 000 m^3 hall at 2 s is a typical occupied concert hall; a 10 000 m^3 room at 4 s a church
            expectWithinAbsoluteError (eyringAbsorption (16000.0f, 2.0f), 0.27f, 0.02f);
            expectWithinAbsoluteError (eyringAbsorption (10000.0f, 4.0f), 0.13f, 0.02f);
            expectEquals (String (describeAbsorption (0.27f)), String ("concert hall"));
            expectEquals (String (describeAbsorption (0.06f)), String ("stone church"));
            for (float v : { 300.0f, 4000.0f, 30000.0f })
                for (float t : { 0.5f, 2.0f, 6.0f })
                    expectWithinAbsoluteError (eyringRt60 (v, eyringAbsorption (v, t)), t, t * 1.0e-3f);
            expectGreaterThan (eyringRt60 (20000.0f, 0.25f), eyringRt60 (10000.0f, 0.25f), "larger: longer");
            expectLessThan (eyringRt60 (10000.0f, 0.35f), eyringRt60 (10000.0f, 0.25f), "more absorption: shorter");
            // the volume estimate is a hall of constant absorption
            expectWithinAbsoluteError (eyringAbsorption (estimateHallVolume (1.0f), 1.0f),
                                       eyringAbsorption (estimateHallVolume (2.5f), 2.5f), 0.005f);
        }

        beginTest ("at the reference distance nothing changes");
        {
            DistanceModel m;
            m.volume = 12000.0f;
            m.rt60 = 1.8f;
            m.distance = m.referenceDistance = 7.3f;
            m.sourceWidth = 3.0f;
            const auto d = computeDistance (m);
            expectWithinAbsoluteError (d.drrChangeDb, 0.0f, 1.0e-5f);
            expectWithinAbsoluteError (d.gapChangeMs, 0.0f, 1.0e-4f);
            expectEquals (d.airMetres, 0.0f);
            expectWithinAbsoluteError (d.widthFactor, 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (d.presenceDb, 0.0f, 1.0e-6f);
        }

        beginTest ("mix law: the distance moves MIX by the ratio change");
        {
            DistanceModel m;
            m.volume = 12000.0f;
            m.rt60 = 1.8f;
            const float energy = 0.37f;
            // a mix whose ratio puts the source at the reference
            m.referenceDistance = 6.0f;
            const float drr0 = barronDrrDb (6.0f, 1.8f, 12000.0f);
            const float k = Decibels::decibelsToGain (drr0) * std::sqrt (energy); // (1 - mix) / mix
            const float mix = 1.0f / (1.0f + k);
            for (float r : { 1.0f, 4.0f, 12.0f, 40.0f })
            {
                m.distance = r;
                const auto d = computeDistance (m);
                const float moved = mixForDrrChange (mix, d.drrChangeDb);
                expectWithinAbsoluteError (Decibels::gainToDecibels ((1.0f - moved) / moved) - 10.0f * std::log10 (energy),
                                           barronDrrDb (r, 1.8f, 12000.0f), 0.01f, "ratio at " + String (r) + " m");
                expectWithinAbsoluteError (d.drrChangeDb, barronDrrDb (r, 1.8f, 12000.0f) - drr0, 1.0e-4f);
            }
            expectWithinAbsoluteError (mixForDrrChange (0.3f, 0.0f), 0.3f, 1.0e-6f);
            expectEquals (mixForDrrChange (0.0f, -10.0f), 0.0f, "direct only stays");
            expectEquals (mixForDrrChange (1.0f, 10.0f), 1.0f, "reverb only stays");
            m.volume = 0.0f;
            expectWithinAbsoluteError (computeDistance (m).volume, estimateHallVolume (1.8f), 1.0f, "auto volume");
        }

        beginTest ("distance moves the DIRECT settings relative to where they started");
        {
            DirectSettings start;
            start.mix = 0.4f;
            start.highsDb = -2.0f;
            start.airDb = 1.0f;
            start.width = 0.8f;
            start.delayMs = 5.0f;
            start.gapMs = 3.0f;
            DistanceResult none;
            const auto same = applyDistance (start, none);
            expectWithinAbsoluteError (same.mix, start.mix, 1.0e-6f);
            expectEquals (same.highsDb, start.highsDb);
            expectEquals (same.airDb, start.airDb);
            expectEquals (same.width, start.width);
            expectEquals (same.delayMs, start.delayMs);
            expectEquals (same.gapMs, start.gapMs);

            DistanceResult d;
            d.drrChangeDb = -6.0f;
            d.presenceDb = -3.0f;
            d.airMetres = 20.0f;
            d.widthFactor = 0.5f;
            d.directDelayMs = 10.0f;
            d.gapChangeMs = -4.0f;
            const auto moved = applyDistance (start, d);
            expectGreaterThan (moved.mix, start.mix, "less direct: more reverb in the mix");
            expectEquals (moved.highsDb, -5.0f);
            expectWithinAbsoluteError (moved.airDb, 1.0f + (float) airAbsorptionDbPerMetre (10000.0, kAirHumidity) * 20.0f, 1.0e-4f);
            expectEquals (moved.width, 0.4f);
            expectEquals (moved.delayMs, 15.0f);
            expectEquals (moved.gapMs, 3.0f, "further away the gap stays");
            d.presenceDb = -20.0f;
            expectEquals (applyDistance (start, d).highsDb, DirectSettings::kHighsMinDb, "limited to the knob's range");
        }

        beginTest ("near field timbre: about 1.5 dB per doubling, -6 .. +3 dB");
        {
            DistanceModel m;
            m.volume = 12000.0f;
            m.referenceDistance = 4.0f;
            const std::pair<float, float> expected[] = { { 8.0f, -1.5f }, { 16.0f, -3.0f }, { 60.0f, -5.86f },
                                                         { 2.0f, 1.5f }, { 0.5f, 3.0f } };
            for (auto [r, db] : expected)
            {
                m.distance = r;
                expectWithinAbsoluteError (computeDistance (m).presenceDb, db, 0.02f, String (r) + " m");
            }
            m.referenceDistance = 1.0f;
            m.distance = 60.0f;
            expectEquals (computeDistance (m).presenceDb, -6.0f, "limited");
        }

        beginTest ("width, propagation delay and air path relative to the reference");
        {
            DistanceModel m;
            m.volume = 10000.0f;
            m.referenceDistance = 2.0f;
            m.micDistance = 1.0f;
            m.distance = 20.0f;
            m.sourceWidth = 0.0f;
            expectEquals (computeDistance (m).widthFactor, 1.0f, "no width set: image unchanged");
            m.sourceWidth = 4.0f;
            const auto d = computeDistance (m);
            expectWithinAbsoluteError (d.widthFactor, std::atan (0.1f) / std::atan (1.0f), 1.0e-4f);
            expectWithinAbsoluteError (d.airMetres, 18.0f, 1.0e-4f);
            expectEquals (d.directDelayMs, 0.0f);
            expectLessThan (d.gapChangeMs, 0.0f, "further away: the first reflection comes sooner");
            m.timeAlign = true;
            expectWithinAbsoluteError (computeDistance (m).directDelayMs, 19000.0f / kSpeedOfSound, 0.01f, "from the microphone");
            m.distance = 1.0f;
            expectEquals (computeDistance (m).airMetres, 0.0f, "closer than the reference: no air to remove");
            expectEquals (computeDistance (m).widthFactor, 1.0f, "closer: the image is not widened");
        }

        beginTest ("air absorption filter matches the ISO curve");
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            AirAbsorptionFilter f;
            f.prepare (sr, 512);
            for (float metres : { 5.0f, 30.0f, 60.0f })
            {
                f.setPath (metres, 50.0f);
                double worst = 0.0;
                for (double freq : { 250.0, 1000.0, 4000.0, 8000.0, 12000.0, 16000.0 })
                    worst = std::max (worst, std::abs (firResponseDb (f.getCoefficients(), freq, sr)
                                                       + airAbsorptionDbPerMetre (freq, 50.0) * metres));
                expectLessThan (worst, 0.15, String (metres) + " m at " + String (sr) + " Hz");
            }
            f.setPath (0.0f, 50.0f);
            expect (f.isBypassed());
        }

        beginTest ("air absorption filter: minimum phase, no latency, smooth changes");
        {
            const double sr = 48000.0;
            AirAbsorptionFilter f;
            f.prepare (sr, 256);
            f.setPath (40.0f, 50.0f);
            const auto& h = f.getCoefficients();
            const auto peak = std::max_element (h.begin(), h.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); });
            expect (peak - h.begin() <= 1, "energy at the start");

            // a 1 kHz sine passes with the expected level, changes do not click
            std::vector<float> l (48000), r (48000);
            for (int i = 0; i < 48000; ++i) l[(size_t) i] = r[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * 1000.0 * i / sr);
            for (int b = 0; b < 48000 / 256; ++b)
            {
                f.setPath (b % 2 == 0 ? 40.0f : 10.0f, 50.0f);
                f.process (l.data() + b * 256, r.data() + b * 256, 256);
            }
            float maxStep = 0.0f, prev = l[999];
            for (int i = 1000; i < 48000; ++i)
            {
                maxStep = std::max (maxStep, std::abs (l[(size_t) i] - prev));
                prev = l[(size_t) i];
            }
            expectLessThan (maxStep, 0.14f, "no discontinuities (1 kHz sine steps at most 0.131)");
        }

        beginTest ("renderer: DELAY moves direct sound and reverb, REVERB GAP the reverb only");
        {
            const double sr = 48000.0;
            DistanceRenderer rend;
            rend.prepare (sr, 512);
            DirectSettings d;
            d.delayMs = 10.0f;
            d.gapMs = 20.0f;
            std::vector<float> dl (4800, 0.0f), dr (4800, 0.0f), wl (4800, 0.0f), wr (4800, 0.0f);
            dl[0] = dr[0] = wl[0] = wr[0] = 1.0f;
            for (int b = 0; b < 4800 / 480; ++b)
                rend.process (dl.data() + b * 480, dr.data() + b * 480, wl.data() + b * 480, wr.data() + b * 480, 480,
                              d, b == 0);
            auto first = [] (const std::vector<float>& x) {
                for (size_t i = 0; i < x.size(); ++i) if (std::abs (x[i]) > 0.5f) return (int) i;
                return -1;
            };
            expectEquals (first (dl), 480);
            expectEquals (first (wl), 1440);
            expectWithinAbsoluteError (dl[480], 1.0f, 1.0e-6f, "whole sample delay, no interpolation");
        }

        beginTest ("renderer: narrowing keeps a panned source in its place");
        {
            const double sr = 48000.0;
            const int n = 48000;
            auto run = [&] (std::vector<float>& l, std::vector<float>& r) {
                DistanceRenderer rend;
                rend.prepare (sr, 480);
                DirectSettings d;
                d.width = 0.0f;
                std::vector<float> wl ((size_t) n, 0.0f), wr ((size_t) n, 0.0f);
                for (int b = 0; b < n / 480; ++b)
                    rend.process (l.data() + b * 480, r.data() + b * 480, wl.data() + b * 480, wr.data() + b * 480, 480,
                                  d, b == 0);
            };
            auto energy = [&] (const std::vector<float>& x) {
                double e = 0.0;
                for (int i = n / 2; i < n; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
                return e;
            };
            Random rng (5);
            // a source panned hard left stays left
            std::vector<float> l ((size_t) n), r ((size_t) n, 0.0f);
            for (auto& v : l) v = rng.nextFloat() - 0.5f;
            run (l, r);
            expectLessThan (energy (r) / energy (l), 0.01, "nothing leaks to the right");
            // a wide (decorrelated) source becomes a centred one
            std::vector<float> l2 ((size_t) n), r2 ((size_t) n);
            for (int i = 0; i < n; ++i) { l2[(size_t) i] = rng.nextFloat() - 0.5f; r2[(size_t) i] = rng.nextFloat() - 0.5f; }
            run (l2, r2);
            double diff = 0.0;
            for (int i = n / 2; i < n; ++i) diff += std::pow ((double) l2[(size_t) i] - r2[(size_t) i], 2.0);
            expectLessThan (diff / energy (l2), 0.01, "left and right are the same");
        }

        beginTest ("renderer: presence shelf");
        {
            const double sr = 48000.0;
            DistanceRenderer rend;
            rend.prepare (sr, 512);
            DirectSettings d;
            d.highsDb = -4.0f;
            const int n = 48000;
            std::vector<float> l ((size_t) n), r ((size_t) n), wl ((size_t) n, 0.0f), wr ((size_t) n, 0.0f);
            for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * 12000.0 * i / sr);
            for (int b = 0; b < n / 480; ++b)
                rend.process (l.data() + b * 480, r.data() + b * 480, wl.data() + b * 480, wr.data() + b * 480, 480, d, b == 0);
            float peak = 0.0f;
            for (int i = n / 2; i < n; ++i) peak = std::max (peak, std::abs (l[(size_t) i]));
            expectWithinAbsoluteError (Decibels::gainToDecibels (peak), -4.0f, 0.3f, "12 kHz lowered by the shelf");
        }

        beginTest ("design: the mid band energy is measured");
        {
            const double sr = 48000.0;
            auto src = makeSourceIR (syntheticIR (1.5f, 2.5f, sr, 31, 0.04f), true, sr, sr, "late onset");
            DesignParams p;
            p.decay = src->nativeRT;
            auto ir = designIR (*src, nullptr, p);
            // white noise: the mid band energy is about the broadband energy
            double broadband = 0.0;
            for (int c = 0; c < 4; ++c)
                for (float v : ir->ch[(size_t) c]) broadband += (double) v * v;
            broadband *= 0.5;
            expectWithinAbsoluteError (Decibels::gainToDecibels (std::sqrt (ir->midEnergy / broadband)), 0.0, 1.0);
            expectWithinAbsoluteError (ir->midEnergyDiagonal * 2.0f, ir->midEnergy, ir->midEnergy * 0.15f);
            expectWithinAbsoluteError (ir->onsetMs, (float) (1000.0 * src->onset / sr), 0.01f);
        }

        beginTest ("parameter text formatting");
        {
            expectEquals (formatMetres (8.0f), String ("8.0 m"));
            expectEquals (formatMetres (25.0f), String ("25 m"));
            const auto cubic = String (CharPointer_UTF8 ("m\xc2\xb3"));
            expectEquals (formatVolume (18040.0f), "18 000 " + cubic);
            expectEquals (formatVolume (2550.0f), "2 600 " + cubic);
            expectEquals (formatVolume (436.0f), "440 " + cubic);
            expectEquals (formatAbsorption (0.27f), String ("0.27 concert hall"));
        }
    }
};

static DistanceTest distanceTest;
} // namespace concert7::test
