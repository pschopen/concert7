// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "TestHelpers.h"
#include "dsp/ConvolutionEngine.h"
#include "dsp/PostProcessing.h"
#include "Parameters.h"

namespace concert7::test
{
using namespace juce;

//==============================================================================
class ResamplerTest : public UnitTest
{
public:
    ResamplerTest() : UnitTest ("Resampler", "concert7-dsp") {}

    void runTest() override
    {
        beginTest ("1 kHz sine keeps amplitude and frequency (48k -> 44.1k)");
        {
            std::vector<float> x (48000);
            for (int i = 0; i < 48000; ++i) x[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * 1000.0 * i / 48000.0);
            auto y = resample (x, 48000.0, 44100.0);
            expectWithinAbsoluteError ((int) y.size(), 44100, 1);
            double maxErr = 0.0;
            for (int i = 2000; i < 42000; ++i)
                maxErr = std::max (maxErr, std::abs (y[(size_t) i] - std::sin (2.0 * MathConstants<double>::pi * 1000.0 * i / 44100.0)));
            expectLessThan (maxErr, 2e-3, "interpolation error");
        }

        beginTest ("content above the new Nyquist is removed");
        {
            std::vector<float> x (48000);
            for (int i = 0; i < 48000; ++i) x[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * 23000.0 * i / 48000.0);
            auto y = resample (x, 48000.0, 44100.0);
            double rms = 0.0;
            for (int i = 2000; i < 42000; ++i) rms += y[(size_t) i] * y[(size_t) i];
            rms = std::sqrt (rms / 40000.0);
            expectLessThan (Decibels::gainToDecibels (rms * std::sqrt (2.0)), -60.0, "aliasing rejection");
        }

        beginTest ("upsampling 44.1k -> 96k");
        {
            std::vector<float> x (44100);
            for (int i = 0; i < 44100; ++i) x[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * 5000.0 * i / 44100.0);
            auto y = resample (x, 44100.0, 96000.0);
            double maxErr = 0.0;
            for (int i = 4000; i < 90000; ++i)
                maxErr = std::max (maxErr, std::abs (y[(size_t) i] - std::sin (2.0 * MathConstants<double>::pi * 5000.0 * i / 96000.0)));
            expectLessThan (maxErr, 3e-3);
        }
    }
};

//==============================================================================
class AnalysisTest : public UnitTest
{
public:
    AnalysisTest() : UnitTest ("IR analysis", "concert7-dsp") {}

    void runTest() override
    {
        const double sr = 48000.0;
        for (float rt : { 0.4f, 1.5f, 4.0f })
        {
            beginTest ("broadband RT60 of synthetic IR, RT = " + String (rt));
            auto ir = syntheticIR (rt, rt * 1.3f + 0.2f, sr, 11);
            const float measured = estimateRT (chans (ir), sr);
            expectWithinAbsoluteError (measured, rt, rt * 0.05f);
        }

        beginTest ("per band RT of a two band IR");
        {
            auto src = makeSourceIR (twoBandIR (3.0f, 0.8f, 4.0f, sr), true, sr, sr, "two band");
            const float lo = src->bandRT[(size_t) std::distance (src->bandFreq.begin(),
                                 std::min_element (src->bandFreq.begin(), src->bandFreq.end(), [] (float a, float b) { return std::abs (a - 125.0f) < std::abs (b - 125.0f); }))];
            const float hi = src->bandRT[(size_t) std::distance (src->bandFreq.begin(),
                                 std::min_element (src->bandFreq.begin(), src->bandFreq.end(), [] (float a, float b) { return std::abs (a - 8000.0f) < std::abs (b - 8000.0f); }))];
            logMessage ("  125 Hz: " + String (lo, 3) + " s   8 kHz: " + String (hi, 3) + " s");
            expectWithinAbsoluteError (lo, 3.0f, 0.3f);
            expectWithinAbsoluteError (hi, 0.8f, 0.1f);
        }

        beginTest ("onset detection");
        {
            auto src = makeSourceIR (syntheticIR (1.0f, 1.5f, sr, 5, 0.05f), true, sr, sr, "late onset");
            expectWithinAbsoluteError (src->onset, (int) (0.05 * sr), (int) (0.002 * sr));
        }

        beginTest ("normalisation: white noise in gives -6 dB out");
        {
            auto src = makeSourceIR (syntheticIR (2.0f, 3.0f, sr, 9), true, sr, sr, "norm");
            double p = 0.0;
            for (auto* c : chans (src->ch)) p += energy (*c);
            expectWithinAbsoluteError (p * 0.5, 0.25, 1e-3);
        }
    }
};

//==============================================================================
class DesignerTest : public UnitTest
{
public:
    DesignerTest() : UnitTest ("IR designer", "concert7-dsp") {}

    static float rtOf (const BuiltIR& ir, float lo = 300.0f, float hi = 3000.0f)
    {
        return estimateRT (chans (ir.ch, ir.trueStereo), ir.sampleRate, lo, hi);
    }

    void runTest() override
    {
        const double sr = 48000.0;
        auto src = makeSourceIR (syntheticIR (1.6f, 2.5f, sr, 21), true, sr, sr, "synthetic");
        DesignParams neutral;
        neutral.decay = src->nativeRT;

        beginTest ("neutral parameters reproduce the source exactly");
        {
            auto ir = designIR (*src, nullptr, neutral);
            expectEquals (ir->length(), src->length());
            float maxDiff = 0.0f;
            for (int c = 0; c < 4; ++c)
                for (int i = 0; i < src->length(); ++i)
                    maxDiff = std::max (maxDiff, std::abs (ir->ch[(size_t) c][(size_t) i] - src->ch[(size_t) c][(size_t) i]));
            expectLessThan (maxDiff, 1e-6f);
        }

        for (float factor : { 0.35f, 0.6f, 1.8f, 3.0f })
        {
            beginTest ("decay scaling x" + String (factor));
            auto p = neutral;
            p.decay = src->nativeRT * factor;
            auto ir = designIR (*src, nullptr, p);
            const float rt = rtOf (*ir);
            logMessage ("  target " + String (p.decay, 3) + " s, measured " + String (rt, 3) + " s, length " + String (ir->seconds, 2) + " s");
            expectWithinAbsoluteError (rt, p.decay, p.decay * 0.1f);
        }

        beginTest ("extending a truncated recording far beyond its length");
        {
            auto shortSrc = makeSourceIR (syntheticIR (1.0f, 0.8f, sr, 4), true, sr, sr, "truncated");
            auto p = neutral;
            p.decay = 5.0f;
            auto ir = designIR (*shortSrc, nullptr, p);
            expectGreaterThan (ir->seconds, 4.5f);
            const float rt = rtOf (*ir);
            logMessage ("  measured " + String (rt, 3) + " s");
            expectWithinAbsoluteError (rt, 5.0f, 0.6f);
            for (auto& c : ir->ch) for (float v : c) if (! std::isfinite (v)) { expect (false, "non finite sample"); break; }
        }

        beginTest ("multi-band decay multipliers");
        {
            auto two = makeSourceIR (twoBandIR (1.5f, 1.5f, 3.0f, sr), true, sr, sr, "two band");
            DesignParams p;
            p.decay = two->nativeRT;
            p.lowFreq = 400.0f;  p.lowMult = 2.0f;
            p.highFreq = 2500.0f; p.highMult = 0.5f;
            auto ir = designIR (*two, nullptr, p);
            const float lo = rtOf (*ir, 60.0f, 200.0f);
            const float hi = rtOf (*ir, 6000.0f, 12000.0f);
            logMessage ("  low band " + String (lo, 3) + " s (target ~3.0), high band " + String (hi, 3) + " s (target ~0.75)");
            expectWithinAbsoluteError (lo, 3.0f, 0.45f);
            expectWithinAbsoluteError (hi, 0.75f, 0.15f);
        }

        beginTest ("pre-delay moves only the late reverb");
        {
            auto p = neutral;
            p.predelayMs = 100.0f;
            auto ir = designIR (*src, nullptr, p);
            const int pd = (int) (0.1 * sr);
            const int splitEnd = src->onset + (int) (ir->splitSeconds * sr) + (int) (0.011 * sr);
            // early part identical
            float maxDiff = 0.0f;
            for (int i = 0; i < src->onset + (int) (0.02 * sr); ++i)
                maxDiff = std::max (maxDiff, std::abs (ir->ch[0][(size_t) i] - src->ch[0][(size_t) i]));
            expectLessThan (maxDiff, 1e-6f, "early part unchanged");
            // gap between the early part and the delayed late part
            expectLessThan (energy (ir->ch[0], splitEnd, splitEnd + pd - (int) (0.025 * sr)), 1e-6, "silent gap");
            expectEquals (ir->length(), src->length() + pd);
        }

        beginTest ("early / late levels");
        {
            auto p = neutral;
            p.earlyDb = kLevelOffDb;
            auto noEarly = designIR (*src, nullptr, p);
            expectLessThan (energy (noEarly->ch[0], 0, src->onset + (int) (0.03 * sr)), 1e-9, "early off");
            p = neutral;
            p.lateDb = kLevelOffDb;
            auto noLate = designIR (*src, nullptr, p);
            expectLessThan (energy (noLate->ch[0], src->onset + (int) (0.2 * sr)), 1e-9, "late off");
            p = neutral;
            p.lateDb = -6.0f;
            auto quieter = designIR (*src, nullptr, p);
            const double ratio = energy (quieter->ch[0], src->onset + (int) (0.2 * sr)) / energy (src->ch[0], src->onset + (int) (0.2 * sr));
            expectWithinAbsoluteError (Decibels::gainToDecibels (std::sqrt (ratio)), -6.0, 0.05);
        }

        beginTest ("delay voices add echoes of the late reverb");
        {
            auto p = neutral;
            p.delayDb = 0.0f;
            p.delayMs = 500.0f;
            auto ir = designIR (*src, nullptr, p);
            expectGreaterThan (ir->length(), src->length() + (int) (1.4 * sr));
            const int at = src->onset + (int) (0.6 * sr);
            const double withDelay = energy (ir->ch[0], at, at + 4800);
            const double without = energy (src->ch[0], at, at + 4800);
            expectGreaterThan (withDelay, without * 3.0);
        }

        beginTest ("late roll-off and VLF");
        {
            auto p = neutral;
            p.lateRolloffHz = 1000.0f;
            auto ir = designIR (*src, nullptr, p);
            const float hfSrc = bandEnergyDb (src->ch[0], sr, 6000.0f, 12000.0f, src->onset + (int) (0.15 * sr));
            const float hfIr = bandEnergyDb (ir->ch[0], sr, 6000.0f, 12000.0f, src->onset + (int) (0.15 * sr));
            logMessage ("  6-12 kHz late energy: " + String (hfSrc - hfIr, 1) + " dB lower");
            expectGreaterThan (hfSrc - hfIr, 20.0f);

            p = neutral;
            p.vlfDb = kVlfOffDb;
            auto noVlf = designIR (*src, nullptr, p);
            const float lfSrc = bandEnergyDb (src->ch[0], sr, 25.0f, 70.0f, src->onset + (int) (0.15 * sr));
            const float lfIr = bandEnergyDb (noVlf->ch[0], sr, 25.0f, 70.0f, src->onset + (int) (0.15 * sr));
            logMessage ("  25-70 Hz late energy without VLF: " + String (lfSrc - lfIr, 1) + " dB lower");
            expectGreaterThan (lfSrc - lfIr, 12.0f);
        }

        beginTest ("early reflection pattern swap");
        {
            auto other = makeSourceIR (syntheticIR (0.5f, 1.0f, sr, 99, 0.03f), true, sr, sr, "pattern");
            auto ir = designIR (*src, other.get(), neutral);
            // the first 20 ms after the onset come from the pattern (aligned at the onset)
            double diff = 0.0, ref = 0.0;
            for (int i = 0; i < (int) (0.02 * sr); ++i)
            {
                const float a = ir->ch[0][(size_t) (src->onset + i)];
                const float b = other->ch[0][(size_t) (other->onset + i)];
                diff += (a - b) * (a - b);
                ref += b * b;
            }
            expectLessThan (diff / ref, 1e-6);
        }
    }

    static float bandEnergyDb (const std::vector<float>& x, double sr, float lo, float hi, int from)
    {
        // brute force DFT power in a band over the segment (decimated bins)
        const int len = std::min ((int) x.size() - from, (int) sr);
        if (len <= 0) return -200.0f;
        juce::dsp::FFT fft (16);
        std::vector<float> buf (1 << 17, 0.0f);
        for (int i = 0; i < std::min (len, 1 << 16); ++i) buf[(size_t) i] = x[(size_t) (from + i)];
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double e = 0.0;
        for (int k = (int) (lo * 65536 / sr); k <= (int) (hi * 65536 / sr); ++k) e += (double) buf[(size_t) k] * buf[(size_t) k];
        return (float) (10.0 * std::log10 (e + 1e-30));
    }
};

//==============================================================================
class EngineTest : public UnitTest
{
public:
    EngineTest() : UnitTest ("Convolution engine", "concert7-dsp") {}

    void runTest() override
    {
        const double sr = 48000.0;
        Random rng (42);
        auto built = std::make_shared<BuiltIR>();
        built->sampleRate = sr;
        built->trueStereo = true;
        for (auto& c : built->ch)
        {
            c.resize (30000);
            for (auto& v : c) v = (rng.nextFloat() * 2.0f - 1.0f) * 0.1f;
        }

        for (int block : { 1, 37, 256, 500, 4096 })
        {
            beginTest ("true stereo convolution matches direct convolution, block " + String (block));
            ConvolutionEngine engine;
            engine.prepare (sr, block);
            expect (engine.loadIR (built, {}));

            const int total = 40000;
            std::vector<float> inL (total), inR (total), outL (total), outR (total);
            for (int i = 0; i < total; ++i) { inL[(size_t) i] = rng.nextFloat() - 0.5f; inR[(size_t) i] = rng.nextFloat() - 0.5f; }
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = std::min (block, total - pos);
                engine.process (inL.data() + pos, inR.data() + pos, outL.data() + pos, outR.data() + pos, n, true);
            }
            auto ll = directConvolve (inL, built->ch[0], total), rl = directConvolve (inR, built->ch[2], total);
            auto lr = directConvolve (inL, built->ch[1], total), rr = directConvolve (inR, built->ch[3], total);
            double maxErr = 0.0, peak = 0.0;
            for (int i = 0; i < total; ++i)
            {
                maxErr = std::max (maxErr, (double) std::abs (outL[(size_t) i] - (ll[(size_t) i] + rl[(size_t) i])));
                maxErr = std::max (maxErr, (double) std::abs (outR[(size_t) i] - (lr[(size_t) i] + rr[(size_t) i])));
                peak = std::max (peak, (double) std::abs (ll[(size_t) i] + rl[(size_t) i]));
            }
            expectLessThan (maxErr / peak, 1e-4, "relative error");
        }

        beginTest ("IR switch: warm-up + crossfade without discontinuities");
        {
            ConvolutionEngine engine;
            const int block = 256;
            engine.prepare (sr, block);
            auto irA = std::make_shared<BuiltIR> (*built);
            auto irB = std::make_shared<BuiltIR> (*built);
            for (auto& c : irB->ch) for (auto& v : c) v *= 0.5f;
            engine.loadIR (irA, {});

            std::vector<float> inL (block), inR (block), outL (block), outR (block);
            float prev = 0.0f, maxStep = 0.0f;
            double phase = 0.0;
            int blocks = 0;
            auto step = [&] {
                for (int i = 0; i < block; ++i)
                {
                    inL[(size_t) i] = inR[(size_t) i] = (float) std::sin (phase);
                    phase += 2.0 * MathConstants<double>::pi * 220.0 / sr;
                }
                engine.process (inL.data(), inR.data(), outL.data(), outR.data(), block, true);
                for (int i = 0; i < block; ++i)
                {
                    if (blocks > 150) maxStep = std::max (maxStep, std::abs (outL[(size_t) i] - prev));
                    prev = outL[(size_t) i];
                }
                ++blocks;
            };
            for (int b = 0; b < 200; ++b) step();
            float steadyStep = maxStep;

            // the worker thread loads while audio keeps running
            std::thread worker ([&] { engine.loadIR (irB, {}); });
            for (int k = 0; k < 2000 && ! engine.isCrossfading() && ! (engine.isIdle() && engine.getActiveIR() == irB); ++k)
            {
                step();
                Thread::sleep (1);
            }
            worker.join();
            const bool switched = true;
            for (int b = 0; b < 200; ++b) step();
            expect (switched);
            expect (engine.isIdle(), "crossfade finished");
            expect (engine.getActiveIR() == irB, "new IR active");
            // the largest sample-to-sample change may not exceed the steady state one (no click / dropout)
            logMessage ("  max sample step " + String (maxStep, 3) + " (steady state " + String (steadyStep, 3) + ")");
            expectLessThan (maxStep, steadyStep * 1.05f);
        }
    }
};

//==============================================================================
class PostProcessingTest : public UnitTest
{
public:
    PostProcessingTest() : UnitTest ("Post processing", "concert7-dsp") {}

    static float sineGainDb (PostEQ& eq, double sr, float freq)
    {
        const int n = (int) sr;
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * freq * i / sr);
        eq.reset();
        eq.process (l.data(), r.data(), n);
        double e = 0.0;
        for (int i = n / 2; i < n; ++i) e += l[(size_t) i] * l[(size_t) i];
        return Decibels::gainToDecibels ((float) std::sqrt (e / (n / 2) * 2.0));
    }

    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("EQ bands hit their target gains");
        {
            PostEQ eq;
            eq.prepare (sr);
            EQSettings s;
            s.midFreq = 1000.0f; s.midGain = 6.0f; s.midQ = 1.0f;
            eq.setSettings (s);
            expectWithinAbsoluteError (sineGainDb (eq, sr, 1000.0f), 6.0f, 0.1f);
            expectWithinAbsoluteError (sineGainDb (eq, sr, 100.0f), 0.0f, 0.3f);
            expectWithinAbsoluteError ((float) PostEQ::responseDb (s, sr, 1000.0), 6.0f, 0.01f);

            s = {};
            s.hpOn = true; s.hpFreq = 200.0f;
            eq.setSettings (s);
            expectWithinAbsoluteError (sineGainDb (eq, sr, 200.0f), -3.01f, 0.1f);
            expectLessThan (sineGainDb (eq, sr, 50.0f), -20.0f);

            s = {};
            s.highGain = -9.0f; s.highFreq = 4000.0f;
            eq.setSettings (s);
            expectWithinAbsoluteError (sineGainDb (eq, sr, 16000.0f), -9.0f, 0.5f);

            s.on = false;
            eq.setSettings (s);
            expectWithinAbsoluteError (sineGainDb (eq, sr, 16000.0f), 0.0f, 0.01f);
        }

        beginTest ("modulator at zero depth is transparent");
        {
            Modulator m;
            m.prepare (sr);
            std::vector<float> l (1000), r (1000), l0, r0;
            Random rng (3);
            for (int i = 0; i < 1000; ++i) l[(size_t) i] = r[(size_t) i] = rng.nextFloat();
            l0 = l; r0 = r;
            m.process (l.data(), r.data(), 1000, 0.0f, 0.5f);
            expect (l == l0 && r == r0);
        }

        beginTest ("modulator keeps the level and stays bounded");
        {
            Modulator m;
            m.prepare (sr);
            const int n = 96000;
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * 440.0 * i / sr);
            m.process (l.data(), r.data(), n, 100.0f, 1.0f);
            double e = 0.0;
            for (int i = n / 2; i < n; ++i) e += l[(size_t) i] * l[(size_t) i];
            expectWithinAbsoluteError (Decibels::gainToDecibels ((float) std::sqrt (e / (n / 2) * 2.0)), 0.0f, 0.2f);
        }

        beginTest ("ducker reduces the wet signal while the key is loud");
        {
            Ducker d;
            d.prepare (sr);
            const int n = 48000;
            std::vector<float> key ((size_t) n, 0.5f), l ((size_t) n, 1.0f), r ((size_t) n, 1.0f);
            d.process (key.data(), key.data(), l.data(), r.data(), n, 12.0f, -30.0f, 200.0f);
            expectWithinAbsoluteError (Decibels::gainToDecibels (l.back()), -12.0f, 0.2f);
            std::vector<float> silent ((size_t) n * 3, 0.0f), l2 ((size_t) n * 3, 1.0f), r2 ((size_t) n * 3, 1.0f);
            d.process (silent.data(), silent.data(), l2.data(), r2.data(), n * 3, 12.0f, -30.0f, 200.0f);
            expectGreaterThan (l2.back(), 0.95f, "recovers after release");
        }

        beginTest ("parameter text formatting");
        {
            expectEquals (formatSeconds (2.345f), String ("2.35 s"));
            expectEquals (formatSeconds (0.25f), String ("250 ms"));
            expectEquals (formatHz (22000.0f), String ("22.0 kHz"));
            expectEquals (formatRolloff (22000.0f), String ("Off"));
            expectEquals (formatDb (-24.0f, kLevelOffDb), String ("Off"));
            expectEquals (formatMs (1500.0f), String ("1.50 s"));
        }
    }
};

static ResamplerTest resamplerTest;
static AnalysisTest analysisTest;
static DesignerTest designerTest;
static EngineTest engineTest;
static PostProcessingTest postProcessingTest;
} // namespace concert7::test
