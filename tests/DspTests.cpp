// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "TestHelpers.h"
#include "dsp/ConvolutionEngine.h"
#include "dsp/Halfband.h"
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

        beginTest ("the synthesised tail is noise: every frequency at its level, none cancelled or ringing");
        {
            // white noise reverb cut at 0.8 s and taken to 5 s: from 1.5 s on the tail is synthesised. Per STFT bin
            // (100 Hz - 4 kHz), its level over 1.5 - 3.5 s with the decay taken out is the same everywhere up to the
            // spread of noise. Phases that only looked random (seeds a multiple of the frame apart) gave a coherent
            // tone per bin instead, its frames added up or cancelled: a spread of 7.8 dB, single bins 27 dB off. The
            // rest comes from the bins' decay lines, fitted on 0.8 s of noise.
            auto shortSrc = makeSourceIR (syntheticIR (1.0f, 0.8f, sr, 4), true, sr, sr, "truncated");
            DesignParams p;
            p.decay = 5.0f;
            auto ir = designIR (*shortSrc, nullptr, p);
            const auto& y = ir->ch[0];
            const int N = 2048, hop = 512;
            std::vector<double> level (N / 2 + 1, 0.0);
            int frames = 0;
            for (int start = (int) (1.5 * sr); start + N <= (int) (3.5 * sr) && start + N <= (int) y.size(); start += hop, ++frames)
            {
                const double tau = (start + N / 2) / sr;
                std::vector<float> buf (2 * (size_t) N, 0.0f);
                for (int n = 0; n < N; ++n)
                    buf[(size_t) n] = y[(size_t) (start + n)] * (float) (0.5 - 0.5 * std::cos (2.0 * MathConstants<double>::pi * n / N));
                dsp::FFT fft (11);
                fft.performFrequencyOnlyForwardTransform (buf.data());
                for (int k = 0; k <= N / 2; ++k)
                    level[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k] * std::pow (10.0, 6.0 * tau / p.decay);
            }
            std::vector<double> db;
            for (int k = (int) (100.0 * N / sr); k <= (int) (4000.0 * N / sr); ++k)
                db.push_back (10.0 * std::log10 (level[(size_t) k] / frames + 1e-30));
            const double mean = std::accumulate (db.begin(), db.end(), 0.0) / (double) db.size();
            double var = 0.0, worst = 0.0;
            for (double v : db) { var += (v - mean) * (v - mean); worst = std::max (worst, std::abs (v - mean)); }
            const double spread = std::sqrt (var / (double) db.size());
            logMessage ("  level per bin: spread " + String (spread, 2) + " dB, largest deviation " + String (worst, 2) + " dB");
            expectLessThan (spread, 2.0);
            expectLessThan (worst, 6.0);
        }

        beginTest ("a recording that starts to fade out: the longer decay does not sink and swell");
        {
            // the last 0.3 s of the recording sit 3 dB below the decay line (the start of a fade-out)
            auto ch = syntheticIR (2.0f, 2.4f, sr, 9);
            for (auto& c : ch)
                for (size_t n = (size_t) (2.1 * sr); n < c.size(); ++n)
                    c[n] *= Decibels::decibelsToGain (-3.0f * (float) std::min (1.0, (n / sr - 2.1) / 0.1));
            auto fading = makeSourceIR (ch, true, sr, sr, "fading");
            DesignParams p;
            p.decay = fading->nativeRT * 3.0f;
            auto ir = designIR (*fading, nullptr, p);

            // level in 100 ms blocks against the straight decay line (all of it far above -60 dB)
            const auto& y = ir->ch[0];
            const int block = (int) (0.1 * sr), first = (int) (0.3 * sr), last = (int) (4.0 * sr);
            std::vector<double> t, db;
            for (int a = first; a + block <= last && a + block <= (int) y.size(); a += block)
            {
                double e = 0.0;
                for (int n = a; n < a + block; ++n) e += (double) y[(size_t) n] * y[(size_t) n];
                t.push_back ((a + block / 2) / sr);
                db.push_back (10.0 * std::log10 (e / block + 1e-30));
            }
            double st = 0, sd = 0, stt = 0, std_ = 0;
            for (size_t i = 0; i < t.size(); ++i) { st += t[i]; sd += db[i]; stt += t[i] * t[i]; std_ += t[i] * db[i]; }
            const double k = (t.size() * std_ - st * sd) / (t.size() * stt - st * st), c0 = (sd - k * st) / t.size();
            double worst = 0.0;
            for (size_t i = 1; i + 1 < t.size(); ++i) // 300 ms average: the dip, not the noise
                worst = std::max (worst, std::abs ((db[i - 1] + db[i] + db[i + 1]) / 3.0 - (c0 + k * t[i])));
            logMessage ("  largest deviation from the decay line " + String (worst, 2) + " dB");
            expectLessThan (worst, 1.0);
        }

        beginTest ("decay keeps the air absorption: highs change less than the mids");
        {
            // a hall whose highs decay faster than its mids (air absorption)
            auto hall = makeSourceIR (twoBandIR (2.0f, 1.0f, 3.0f, sr), true, sr, sr, "hall");
            DesignParams same;
            same.decay = hall->nativeRT;
            const float nativeHi = rtOf (*designIR (*hall, nullptr, same), 6000.0f, 12000.0f);
            for (float factor : { 3.0f, 0.4f })
            {
                DesignParams p;
                p.decay = hall->nativeRT * factor;
                const float hi = rtOf (*designIR (*hall, nullptr, p), 6000.0f, 12000.0f);
                logMessage ("  x" + String (factor) + ": highs " + String (hi, 3) + " s, proportional would be "
                            + String (nativeHi * factor, 3) + " s");
                // the highs move towards the native value: less than proportional, but in the same direction
                if (factor > 1.0f) { expectGreaterThan (hi, nativeHi * 1.2f); expectLessThan (hi, nativeHi * factor * 0.8f); }
                else               { expectLessThan (hi, nativeHi * 0.8f); expectGreaterThan (hi, nativeHi * factor * 1.1f); }
            }

            // highs that decay as long as the mids cannot be air limited: they scale like the mids
            auto p = neutral;
            p.decay = src->nativeRT * 3.0f;
            const float hiFlat = rtOf (*designIR (*src, nullptr, p), 6000.0f, 12000.0f);
            logMessage ("  flat source x3: highs " + String (hiFlat, 3) + " s, target " + String (p.decay, 3) + " s");
            expectWithinAbsoluteError (hiFlat, p.decay, p.decay * 0.15f);
        }

        beginTest ("a shorter decay fades the early part too: no drop at the crossover");
        {
            auto p = neutral;
            p.decay = src->nativeRT * 0.35f;
            auto ir = designIR (*src, nullptr, p);

            // level of all channels in 5 ms blocks from 20 to 160 ms (the crossover sits at 80 ms)
            const int block = (int) (0.005 * sr);
            std::vector<double> t, db;
            for (int a = src->onset + (int) (0.02 * sr); a + block <= src->onset + (int) (0.16 * sr); a += block)
            {
                double e = 0.0;
                for (auto* c : chans (ir->ch)) e += energy (*c, a, a + block);
                t.push_back ((a + block / 2 - src->onset) / sr);
                db.push_back (10.0 * std::log10 (e / block + 1e-30));
            }
            double st = 0, sd = 0, stt = 0, std_ = 0;
            const double n = (double) t.size();
            for (size_t i = 0; i < t.size(); ++i) { st += t[i]; sd += db[i]; stt += t[i] * t[i]; std_ += t[i] * db[i]; }
            const double k = (n * std_ - st * sd) / (n * stt - st * st), c0 = (sd - k * st) / n;
            double worst = 0.0;
            for (size_t i = 1; i + 1 < t.size(); ++i) // 15 ms average: the step, not the noise
                worst = std::max (worst, std::abs ((db[i - 1] + db[i] + db[i + 1]) / 3.0 - (c0 + k * t[i])));
            logMessage ("  slope " + String (k, 1) + " dB/s (target " + String (-60.0 / p.decay, 1)
                        + "), largest deviation from the line " + String (worst, 2) + " dB");
            expectLessThan (worst, 1.5);
            expectWithinAbsoluteError (k, -60.0 / p.decay, 60.0 / p.decay * 0.15);

            // the first reflection (here the direct sound) keeps its level, a longer decay leaves the start of the early
            // part as it is (its later part takes on the lower start of the late reverb)
            auto peakDb = [&] (const BuiltIR& b) {
                float pb = 0.0f, ps = 0.0f; // onset lies 1 ms before the first reflection
                for (int i = src->onset; i < src->onset + (int) (0.003 * sr); ++i)
                {
                    pb = std::max (pb, std::abs (b.ch[0][(size_t) i]));
                    ps = std::max (ps, std::abs (src->ch[0][(size_t) i]));
                }
                return Decibels::gainToDecibels (pb / ps);
            };
            logMessage ("  first reflection at x0.35: " + String (peakDb (*ir), 2) + " dB");
            expectWithinAbsoluteError (peakDb (*ir), 0.0f, 1.0f);
            p.decay = src->nativeRT * 2.0f;
            auto longer = designIR (*src, nullptr, p);
            auto changeDb = [&] (double a, double b) {
                const int from = src->onset + (int) (a * sr), to = src->onset + (int) (b * sr);
                return 10.0 * std::log10 (energy (longer->ch[0], from, to) / energy (src->ch[0], from, to));
            };
            expectWithinAbsoluteError (changeDb (0.0, 0.005), 0.0, 0.15);
        }

        beginTest ("every frequency keeps its own decay: a ringing frequency is scaled, not stretched without limit");
        {
            // decaying 1 kHz sine that rings longer than the noise reverb
            auto ch = syntheticIR (1.0f, 3.0f, sr, 33);
            const int onset = (int) (0.01 * sr);
            const double kr = std::log (1000.0) / (2.0 * sr);
            for (size_t c = 0; c < 4; ++c)
                for (int i = onset; i < (int) ch[c].size(); ++i)
                    ch[c][(size_t) i] += 0.05f * (float) (std::exp (-kr * (i - onset))
                                                          * std::sin (2.0 * MathConstants<double>::pi * 1000.0 * i / sr + (double) c));
            auto ringing = makeSourceIR (std::move (ch), true, sr, sr, "ringing");
            auto ringRT = [&] (const BuiltIR& ir) { return rtOf (ir, 985.0f, 1015.0f); };
            DesignParams same;
            same.decay = ringing->nativeRT;
            const float native = ringRT (*designIR (*ringing, nullptr, same));
            DesignParams p;
            p.decay = ringing->nativeRT * 1.6f;
            const float longer = ringRT (*designIR (*ringing, nullptr, p));
            logMessage ("  1 kHz: " + String (native, 2) + " s -> " + String (longer, 2) + " s at x1.6 (noise "
                        + String (ringing->nativeRT, 2) + " -> " + String (p.decay, 2) + " s)");
            // the same added dB/s for the whole band would give about 7 s
            expectWithinAbsoluteError (longer / native, 1.6f, 0.35f);
        }

        beginTest ("the synthesised tail keeps the coherence of the channels");
        {
            // true stereo with correlated channels: 0.8 shared + 0.6 own noise, coherence 0.64; cut at 0.8 s
            auto shared = syntheticIR (1.0f, 0.8f, sr, 51);
            auto own = syntheticIR (1.0f, 0.8f, sr, 52);
            std::array<std::vector<float>, 4> ch;
            for (size_t c = 0; c < 4; ++c)
            {
                ch[c].resize (shared[0].size());
                for (size_t i = 0; i < ch[c].size(); ++i)
                    ch[c][i] = 0.8f * shared[0][i] + 0.6f * own[c][i];
            }
            auto correlated = makeSourceIR (std::move (ch), true, sr, sr, "correlated");
            DesignParams p;
            p.decay = 4.0f;
            auto ir = designIR (*correlated, nullptr, p);
            auto corr = [&] (const std::vector<float>& a, const std::vector<float>& b, double from, double to) {
                double ab = 0, aa = 0, bb = 0;
                for (int i = (int) (from * sr); i < std::min ((int) a.size(), (int) (to * sr)); ++i)
                {
                    ab += (double) a[(size_t) i] * b[(size_t) i];
                    aa += (double) a[(size_t) i] * a[(size_t) i];
                    bb += (double) b[(size_t) i] * b[(size_t) i];
                }
                return ab / std::sqrt (aa * bb + 1e-30);
            };
            const double measured = corr (ir->ch[0], ir->ch[3], 0.2, 0.6);
            const double tail = corr (ir->ch[0], ir->ch[3], 1.5, 3.5);
            const double cross = corr (ir->ch[1], ir->ch[2], 1.5, 3.5);
            logMessage ("  correlation LL/RR: measured part " + String (measured, 2) + ", synthesised tail " + String (tail, 2)
                        + " (LR/RL " + String (cross, 2) + ")");
            expectWithinAbsoluteError (tail, 0.64, 0.12);
            expectWithinAbsoluteError (cross, 0.64, 0.12);
        }

        beginTest ("a long deep bass stays as recorded, DECAY changes it at most by its own factor");
        {
            // a 1.2 s reverb plus a softer 3 s tail below about 60 Hz that takes over later (like some
            // recordings); times x gives the same room with every decay x times as long
            auto longBassIR = [&] (float x) {
                auto ch = syntheticIR (1.2f * x, 3.5f * x, sr, 61);
                auto tail = syntheticIR (3.0f * x, 3.5f * x, sr, 62);
                const float a = (float) std::exp (-2.0 * MathConstants<double>::pi * 60.0 / sr);
                for (size_t c = 0; c < 4; ++c)
                {
                    for (int pass = 0; pass < 6; ++pass) // one pole low pass, 6 times
                    {
                        float z = 0.0f;
                        for (auto& v : tail[c]) v = z = (1.0f - a) * v + a * z;
                    }
                    for (size_t i = 0; i < ch[c].size(); ++i) ch[c][i] += 1.5f * tail[c][i];
                }
                return ch;
            };
            auto ch = longBassIR (1.0f);
            auto longBass = makeSourceIR (std::move (ch), true, sr, sr, "long bass");

            DesignParams p;
            p.decay = longBass->nativeRT;
            auto native = designIR (*longBass, nullptr, p);
            expectEquals (native->length(), longBass->length());
            float maxDiff = 0.0f;
            for (size_t c = 0; c < 4; ++c)
                for (int i = 0; i < longBass->length(); ++i)
                    maxDiff = std::max (maxDiff, std::abs (native->ch[c][(size_t) i] - longBass->ch[c][(size_t) i]));
            expectLessThan (maxDiff, 1e-6f, "at its own decay the recording itself");

            for (float factor : { 2.0f, 3.0f, 0.5f })
            {
                p.decay = longBass->nativeRT * factor;
                auto ir = designIR (*longBass, nullptr, p);
                // the reverb time below the mids changes by DECAY's factor at most
                for (size_t b = 0; b < ir->rtFreqs.size() && ir->rtFreqs[b] < 250.0f; ++b)
                {
                    const float change = ir->rtTarget[b] / ir->rtNative[b];
                    if (factor > 1.0f) expectLessThan (change, factor * 1.001f, String (ir->rtFreqs[b]) + " Hz target");
                    else               expectGreaterThan (change, factor * 0.999f, String (ir->rtFreqs[b]) + " Hz target");
                }
                // rendered: the level against the recording stretched in time by factor; the mids are what DECAY sets
                auto above = [&] (float fc) { return aboveStretchedDb (bandBlocks (ir->ch, sr, fc), bandBlocks (longBass->ch, sr, fc), factor); };
                const float mids = above (1000.0f);
                String line ("  x" + String (factor) + ", level against the recording stretched as much, 1 kHz " + String (mids, 1) + " dB, bass:");
                for (float fc : { 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f })
                {
                    const float bass = above (fc) - mids;
                    line << " " << String (fc, 0) << " Hz " << String (bass, 1);
                    if (factor > 1.0f) expectLessThan (bass, 3.5f, String (fc) + " Hz grows more than DECAY");
                    else               expectGreaterThan (bass, -3.5f, String (fc) + " Hz shrinks more than DECAY");
                }
                logMessage (line + " dB");
            }
        }

        beginTest ("as on the M7, a longer decay is louder, its late reverb starts a little lower");
        {
            // start of the late reverb's decay line: 10 ms blocks over its first 25 dB, fitted, at the onset
            auto startDb = [&] (const BuiltIR& ir, float rt) {
                const int block = (int) (0.01 * sr);
                std::vector<double> t, db;
                for (int a = src->onset + (int) (0.15 * sr); a + block <= src->onset + (int) ((0.15 + rt * 0.4) * sr); a += block)
                {
                    double e = 0.0;
                    for (auto* c : chans (ir.ch)) e += energy (*c, a, a + block);
                    t.push_back ((a + block / 2 - src->onset) / sr);
                    db.push_back (10.0 * std::log10 (e / block + 1e-30));
                }
                double st = 0, sd = 0, stt = 0, std_ = 0;
                const double n = (double) t.size();
                for (size_t i = 0; i < t.size(); ++i) { st += t[i]; sd += db[i]; stt += t[i] * t[i]; std_ += t[i] * db[i]; }
                const double k = (n * std_ - st * sd) / (n * stt - st * st);
                return (float) ((sd - k * st) / n);
            };
            auto p = neutral;
            const float native = startDb (*designIR (*src, nullptr, p), src->nativeRT);
            const float nativeLevel = bandEnergyDb (src->ch[0], sr, 500.0f, 2000.0f, 0);
            for (float factor : { 3.0f, 0.4f })
            {
                p.decay = src->nativeRT * factor;
                auto ir = designIR (*src, nullptr, p);
                const float start = startDb (*ir, p.decay) - native;
                const float level = bandEnergyDb (ir->ch[0], sr, 500.0f, 2000.0f, 0) - nativeLevel;
                // 0.55 dB lower per dB of reverb time (split captures): the energy grows with RT^0.45
                const float expected = -0.55f * 10.0f * std::log10 (factor);
                logMessage ("  x" + String (factor) + ": late start " + String (start, 1) + " dB (expected " + String (expected, 1)
                            + "), level " + String (level, 1) + " dB");
                expectWithinAbsoluteError (start, expected, 0.75f);
                if (factor > 1.0f) expectGreaterThan (level, 1.0f); else expectLessThan (level, -1.0f);
            }
        }

        beginTest ("decay mode proportional: the highs scale like the mids");
        {
            auto hall = makeSourceIR (twoBandIR (2.0f, 1.0f, 3.0f, sr), true, sr, sr, "hall");
            DesignParams p;
            p.decay = hall->nativeRT;
            p.decayMode = DecayMode::Proportional;
            const float nativeHi = rtOf (*designIR (*hall, nullptr, p), 6000.0f, 12000.0f);
            for (float factor : { 3.0f, 0.4f })
            {
                p.decay = hall->nativeRT * factor;
                const float hi = rtOf (*designIR (*hall, nullptr, p), 6000.0f, 12000.0f);
                logMessage ("  x" + String (factor) + ": highs " + String (hi, 3) + " s, proportional " + String (nativeHi * factor, 3) + " s");
                expectWithinAbsoluteError (hi, nativeHi * factor, nativeHi * factor * 0.15f);
            }
        }

        beginTest ("decay mode original: the mechanism of 0.1.0-beta");
        {
            // every band by the same factor
            auto hall = makeSourceIR (twoBandIR (2.0f, 1.0f, 3.0f, sr), true, sr, sr, "hall");
            DesignParams p;
            p.decayMode = DecayMode::Original;
            p.decay = hall->nativeRT;
            const float nativeHi = rtOf (*designIR (*hall, nullptr, p), 6000.0f, 12000.0f);
            p.decay = hall->nativeRT * 3.0f;
            const float hi = rtOf (*designIR (*hall, nullptr, p), 6000.0f, 12000.0f);
            expectWithinAbsoluteError (hi, nativeHi * 3.0f, nativeHi * 3.0f * 0.15f);

            // the early part stays as it is when shortening; the late reverb's gain grows from the onset on (there
            // was no level rule yet)
            p.decay = src->nativeRT * 0.35f;
            auto shorter = designIR (*src, nullptr, p);
            auto changeDb = [&] (const BuiltIR& ir, double a, double b) {
                const int from = src->onset + (int) (a * sr), to = src->onset + (int) (b * sr);
                return 10.0 * std::log10 (energy (ir.ch[0], from, to) / energy (src->ch[0], from, to));
            };
            p.decay = src->nativeRT * 3.0f;
            auto longer = designIR (*src, nullptr, p);
            const float rt = rtOf (*longer);
            logMessage ("  early part at x0.35 " + String (changeDb (*shorter, 0.0, 0.04), 2) + " dB, late start at x3 "
                        + String (changeDb (*longer, 0.1, 0.15), 2) + " dB, RT x3 " + String (rt, 2) + " s (target "
                        + String (p.decay, 2) + ")");
            expectWithinAbsoluteError (changeDb (*shorter, 0.0, 0.04), 0.0, 0.2);
            const double grown = 60.0 * 0.125 * (1.0 / src->nativeRT - 1.0 / p.decay);
            expectWithinAbsoluteError (changeDb (*longer, 0.1, 0.15), grown, 1.0);
            expectWithinAbsoluteError (rt, p.decay, p.decay * 0.1f);
        }

        beginTest ("decay mode constant level: every decay keeps the loudness of the preset");
        {
            DesignParams p;
            p.decayMode = DecayMode::ConstantLevel;
            const float nativeLevel = bandEnergyDb (src->ch[0], sr, 500.0f, 2000.0f, 0);
            for (float factor : { 3.0f, 0.4f })
            {
                p.decay = src->nativeRT * factor;
                p.lateRolloffHz = 22000.0f;
                auto ir = designIR (*src, nullptr, p);
                const float rt = rtOf (*ir);
                const float level = bandEnergyDb (ir->ch[0], sr, 500.0f, 2000.0f, 0) - nativeLevel;
                // the roll-off stays the user's: with it the level is lower by about as much as it takes away
                p.lateRolloffHz = 1500.0f;
                const float darker = bandEnergyDb (designIR (*src, nullptr, p)->ch[0], sr, 500.0f, 2000.0f, 0) - nativeLevel;
                logMessage ("  x" + String (factor) + ": RT " + String (rt, 2) + " s (target " + String (p.decay, 2) + "), level "
                            + String (level, 2) + " dB, with a 1.5 kHz roll-off " + String (darker, 2) + " dB");
                expectWithinAbsoluteError (rt, p.decay, p.decay * 0.1f);
                expectWithinAbsoluteError (level, 0.0f, 0.5f);
                // (a short decay is mostly early reflections, which the late roll-off leaves alone)
                expectLessThan (darker, level - (factor > 1.0f ? 1.0f : 0.2f));
            }
        }

        beginTest ("decay mode stretch: a larger room up to x1.3, the rest like Sabine");
        {
            DesignParams p;
            p.decayMode = DecayMode::Stretch;
            for (float factor : { 1.3f, 2.5f, 0.6f })
            {
                p.decay = src->nativeRT * factor;
                auto ir = designIR (*src, nullptr, p);
                const float rt = rtOf (*ir);
                const float stretch = jlimit (1.0f / kMaxStretch, kMaxStretch, factor);
                const float onsetRatio = ir->onsetMs / (float) (1000.0 * src->onset / sr);
                logMessage ("  x" + String (factor) + ": RT " + String (rt, 2) + " s (target " + String (p.decay, 2)
                            + "), first reflection x" + String (onsetRatio, 2) + " (stretch x" + String (stretch, 2) + ")");
                expectWithinAbsoluteError (rt, p.decay, p.decay * 0.1f);
                expectWithinAbsoluteError (onsetRatio, stretch, 0.1f);
                expectEquals (ir->nativeRT, src->nativeRT); // the display compares with the preset itself
            }
            // a knob that moves a little reuses the stretched copy
            p.decay = src->nativeRT * 1.3f;
            designIR (*src, nullptr, p);
            const auto* copy = src->stretched.get();
            p.decay = src->nativeRT * 1.3005f;
            designIR (*src, nullptr, p);
            expect (copy != nullptr && src->stretched.get() == copy);
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

        beginTest ("offset moves the whole response");
        {
            auto p = neutral;
            p.offsetMs = 50.0f;
            auto later = designIR (*src, nullptr, p);
            const int sh = (int) std::lround (0.05 * sr);
            expectEquals (later->length(), src->length() + sh);
            expectLessThan (energy (later->ch[0], 0, sh), 1e-12, "leading silence");
            float maxDiff = 0.0f;
            for (int i = 0; i < src->length(); ++i)
                maxDiff = std::max (maxDiff, std::abs (later->ch[0][(size_t) (i + sh)] - src->ch[0][(size_t) i]));
            expectLessThan (maxDiff, 1e-6f, "shifted, otherwise unchanged");

            p.offsetMs = -50.0f;
            auto earlier = designIR (*src, nullptr, p);
            expectEquals (earlier->length(), src->length() - sh);
            expectWithinAbsoluteError (earlier->ch[0][0], 0.0f, 1e-6f, "fades in");
            maxDiff = 0.0f;
            for (int i = (int) (0.01 * sr); i < earlier->length(); ++i)
                maxDiff = std::max (maxDiff, std::abs (earlier->ch[0][(size_t) i] - src->ch[0][(size_t) (i + sh)]));
            expectLessThan (maxDiff, 1e-6f, "start cut, rest unchanged");
            expectWithinAbsoluteError (earlier->onsetMs, (float) std::max (0.0, 1000.0 * src->onset / sr - 50.0), 0.05f);

            // the whole range, +-5 s: longer than the response itself
            p.offsetMs = kMaxOffsetMs;
            auto latest = designIR (*src, nullptr, p);
            const int maxSh = (int) std::lround (kMaxOffsetMs * 0.001 * sr);
            expectEquals (latest->length(), src->length() + maxSh);
            expectLessThan (energy (latest->ch[0], 0, maxSh), 1e-12, "5 s of leading silence");
            p.offsetMs = -kMaxOffsetMs;
            auto earliest = designIR (*src, nullptr, p);
            expectGreaterOrEqual (earliest->length(), 1, "cut down to its last sample");
            expectLessThan (energy (earliest->ch[0]), 1e-6, "nothing left");
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

        beginTest ("smooth keeps reverb time and level");
        {
            auto p = neutral;
            p.smooth = 1.0f;
            auto ir = designIR (*src, nullptr, p);
            const float rt = rtOf (*ir);
            const int from = src->onset + (int) (0.15 * sr);
            const double ratioDb = 10.0 * std::log10 (energy (ir->ch[0], from) / energy (src->ch[0], from));
            logMessage ("  RT " + String (rt, 3) + " s (native " + String (src->nativeRT, 3) + " s), late level "
                        + String (ratioDb, 2) + " dB");
            expectWithinAbsoluteError (rt, src->nativeRT, src->nativeRT * 0.1f);
            expectWithinAbsoluteError (ratioDb, 0.0, 1.5);
        }

        beginTest ("smooth evens out a ringing resonance");
        {
            // decaying 1 kHz sine that rings longer than the noise reverb
            auto ch = syntheticIR (1.6f, 2.5f, sr, 31);
            const int onset = (int) (0.01 * sr);
            const double k = std::log (1000.0) / (3.0 * sr);
            for (size_t c = 0; c < 4; ++c)
                for (int i = onset; i < (int) ch[c].size(); ++i)
                    ch[c][(size_t) i] += 0.17f * (float) (std::exp (-k * (i - onset))
                                                          * std::sin (2.0 * MathConstants<double>::pi * 1000.0 * i / sr + (double) c));
            auto ringing = makeSourceIR (std::move (ch), true, sr, sr, "ringing");
            DesignParams p;
            p.decay = ringing->nativeRT;
            p.smooth = 1.0f;
            auto ir = designIR (*ringing, nullptr, p);

            const int from = ringing->onset + (int) (0.1 * sr);
            auto prominence = [&] (const std::vector<float>& x) {
                const float peak = bandEnergyDb (x, sr, 990.0f, 1010.0f, from) - 10.0f * std::log10 (20.0f);
                const float lo = bandEnergyDb (x, sr, 700.0f, 900.0f, from), hi = bandEnergyDb (x, sr, 1100.0f, 1300.0f, from);
                return peak - (10.0f * std::log10 (0.5f * (std::pow (10.0f, lo / 10.0f) + std::pow (10.0f, hi / 10.0f))) - 10.0f * std::log10 (200.0f));
            };
            const float before = prominence (ringing->ch[0]), after = prominence (ir->ch[0]);
            logMessage ("  1 kHz peak over its neighbourhood: " + String (before, 1) + " dB -> " + String (after, 1) + " dB");
            expectGreaterThan (before - after, 5.0f);
        }

        beginTest ("smooth flattens a late echo in the envelope");
        {
            // 40 ms noise burst 12 dB above the decay at 0.5 s
            auto ch = syntheticIR (1.6f, 2.5f, sr, 41);
            const int onset = (int) (0.01 * sr), at = onset + (int) (0.5 * sr), burst = (int) (0.04 * sr);
            const float amp = 4.0f * std::pow (10.0f, -3.0f * 0.5f / 1.6f);
            Random rng (43);
            for (auto& c : ch)
                for (int i = at; i < at + burst; ++i)
                    c[(size_t) i] += amp * (rng.nextFloat() * 2.0f - 1.0f) * (float) std::sin (MathConstants<double>::pi * (i - at) / burst);
            auto echo = makeSourceIR (std::move (ch), true, sr, sr, "echo");
            DesignParams p;
            p.decay = echo->nativeRT;
            p.smooth = 1.0f;
            auto ir = designIR (*echo, nullptr, p);

            // energy of the echo window over the decay line through the windows before and after it
            auto excessDb = [&] (const std::vector<float>& x) {
                auto e = [&] (double a, double b) { return energy (x, onset + (int) (a * sr), onset + (int) (b * sr)); };
                return 10.0 * std::log10 (e (0.49, 0.55) / std::sqrt (e (0.40, 0.46) * e (0.58, 0.64)));
            };
            const double before = excessDb (echo->ch[0]), after = excessDb (ir->ch[0]);
            logMessage ("  echo over the decay line: " + String (before, 1) + " dB -> " + String (after, 1) + " dB");
            expectGreaterThan (before - after, 3.0);
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

        beginTest ("modulation: early part and late reverb add up to the whole response");
        for (bool withDelay : { false, true })
        {
            auto p = neutral;
            p.decay = src->nativeRT * 1.3f;
            p.predelayMs = 20.0f;
            if (withDelay) p.delayDb = -6.0f;
            auto whole = designIR (*src, nullptr, p);
            p.splitLate = p.lateVariant = true;
            auto split = designIR (*src, nullptr, p);
            expect (split->hasLate() && split->hasVariant());
            expect (! whole->hasLate() && ! whole->hasVariant());
            expectEquals (split->length(), whole->length());
            expectLessThan ((int) split->ch[0].size(), src->onset + (int) (0.15 * sr), "the early part is short");
            float maxDiff = 0.0f;
            for (int c = 0; c < 4; ++c)
                for (int i = 0; i < whole->length(); ++i)
                {
                    const auto& e = split->ch[(size_t) c];
                    const float sum = (i < (int) e.size() ? e[(size_t) i] : 0.0f) + split->late[(size_t) c][(size_t) i];
                    maxDiff = std::max (maxDiff, std::abs (sum - whole->ch[(size_t) c][(size_t) i]));
                }
            expectLessThan (maxDiff, 1e-5f);
            expect (split->envelopeDb == whole->envelopeDb, "display unchanged");
            expectWithinAbsoluteError (split->midEnergy, whole->midEnergy, 1e-6f);
        }

        beginTest ("modulation: the late variant keeps envelope, spectrum and channel relation, not the fine structure");
        {
            // channels 0 / 3 and 1 / 2 identical: the variant has to keep that
            auto coupled = syntheticIR (1.6f, 2.5f, sr, 21);
            coupled[3] = coupled[0];
            coupled[2] = coupled[1];
            auto s2 = makeSourceIR (coupled, true, sr, sr, "coupled");
            auto p = neutral;
            p.decay = s2->nativeRT;
            p.splitLate = p.lateVariant = true;
            auto ir = designIR (*s2, nullptr, p);
            const auto& L = ir->late[0];
            const auto& V = ir->variant[0];
            expectEquals (V.size(), L.size());

            float relDiff = 0.0f;
            for (size_t i = 0; i < V.size(); ++i)
                relDiff = std::max ({ relDiff, std::abs (ir->variant[3][i] - V[i]), std::abs (ir->variant[2][i] - ir->variant[1][i]) });
            expectLessThan (relDiff, 1e-6f, "channel relation");

            // envelope in 50 ms blocks, down to -50 dB
            const int block = (int) (0.05 * sr);
            double peak = 0.0;
            for (int b = 0; (b + 1) * block <= (int) L.size(); ++b)
                peak = std::max (peak, energy (L, b * block, (b + 1) * block));
            float worst = 0.0f;
            for (int b = 0; (b + 1) * block <= (int) L.size(); ++b)
            {
                const double eL = energy (L, b * block, (b + 1) * block), eV = energy (V, b * block, (b + 1) * block);
                if (eL > peak * 1e-5)
                    worst = std::max (worst, std::abs ((float) (10.0 * std::log10 (eV / eL))));
            }
            logMessage ("  envelope: largest block difference " + String (worst, 2) + " dB");
            expectLessThan (worst, 1.0f);

            // spectrum in octaves over the first second of the late reverb (both independent channel pairs)
            const int from = s2->onset + (int) (ir->splitSeconds * sr);
            float worstBand = 0.0f;
            for (float fc = 125.0f; fc <= 8000.0f; fc *= 2.0f)
            {
                const float lo = fc * std::sqrt (0.5f), hi = fc * std::sqrt (2.0f);
                double eL = 0.0, eV = 0.0;
                for (int c : { 0, 1 })
                {
                    eL += std::pow (10.0, bandEnergyDb (ir->late[(size_t) c], sr, lo, hi, from) / 10.0);
                    eV += std::pow (10.0, bandEnergyDb (ir->variant[(size_t) c], sr, lo, hi, from) / 10.0);
                }
                worstBand = std::max (worstBand, std::abs ((float) (10.0 * std::log10 (eV / eL))));
            }
            logMessage ("  spectrum: largest octave band difference " + String (worstBand, 2) + " dB");
            expectLessThan (worstBand, 1.5f);

            // fine structure: no correlation with the late reverb
            double cross = 0.0, eL = 0.0, eV = 0.0;
            for (int c = 0; c < 4; ++c)
                for (size_t i = 0; i < L.size(); ++i)
                {
                    const double a = ir->late[(size_t) c][i], b = ir->variant[(size_t) c][i];
                    cross += a * b; eL += a * a; eV += b * b;
                }
            const double corr = cross / std::sqrt (eL * eV);
            logMessage ("  correlation late / variant " + String (corr, 3));
            expectLessThan (std::abs (corr), 0.1);

            auto again = designIR (*s2, nullptr, p);
            expect (std::equal (V.begin(), V.end(), again->variant[0].begin()), "deterministic");
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

        beginTest ("split response: every stream is convolved with its own part");
        {
            auto split = std::make_shared<BuiltIR>();
            split->sampleRate = sr;
            split->trueStereo = true;
            for (int c = 0; c < 4; ++c)
            {
                split->ch[(size_t) c].resize (2000);
                split->late[(size_t) c].assign (8000, 0.0f);
                split->variant[(size_t) c].assign (8000, 0.0f);
                for (auto& v : split->ch[(size_t) c]) v = (rng.nextFloat() * 2.0f - 1.0f) * 0.1f;
                for (int i = 1500; i < 8000; ++i)
                {
                    split->late[(size_t) c][(size_t) i] = (rng.nextFloat() * 2.0f - 1.0f) * 0.05f;
                    split->variant[(size_t) c][(size_t) i] = (rng.nextFloat() * 2.0f - 1.0f) * 0.05f;
                }
            }
            ConvolutionEngine engine;
            const int block = 300;
            engine.prepare (sr, block);
            expect (engine.loadIR (split, {}));

            const int total = 12000;
            std::vector<float> inL (total), inR (total);
            std::array<std::vector<float>, 6> out;
            for (auto& o : out) o.resize (total);
            for (int i = 0; i < total; ++i) { inL[(size_t) i] = rng.nextFloat() - 0.5f; inR[(size_t) i] = rng.nextFloat() - 0.5f; }
            for (int pos = 0; pos < total; pos += block)
            {
                WetStreams streams;
                for (size_t s = 0; s < out.size(); ++s) streams.ch[s] = out[s].data() + pos;
                engine.process (inL.data() + pos, inR.data() + pos, streams, std::min (block, total - pos), true);
            }
            const std::array<const std::array<std::vector<float>, 4>*, 3> parts { &split->ch, &split->late, &split->variant };
            for (int s = 0; s < 3; ++s)
            {
                const auto& h = *parts[(size_t) s];
                auto ll = directConvolve (inL, h[0], total), rl = directConvolve (inR, h[2], total);
                auto lr = directConvolve (inL, h[1], total), rr = directConvolve (inR, h[3], total);
                double maxErr = 0.0, peak = 0.0;
                for (int i = 0; i < total; ++i)
                {
                    maxErr = std::max (maxErr, (double) std::abs (out[(size_t) (2 * s)][(size_t) i] - (ll[(size_t) i] + rl[(size_t) i])));
                    maxErr = std::max (maxErr, (double) std::abs (out[(size_t) (2 * s + 1)][(size_t) i] - (lr[(size_t) i] + rr[(size_t) i])));
                    peak = std::max (peak, (double) std::abs (ll[(size_t) i] + rl[(size_t) i]));
                }
                expectLessThan (maxErr / peak, 1e-4, "stream " + String (s));
            }
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

        beginTest ("tail modulator at zero depth adds the late reverb unchanged");
        {
            TailModulator m;
            m.prepare (sr);
            const int n = 1000;
            std::vector<float> l (n), r (n), lateL (n), lateR (n), varL (n), varR (n);
            Random rng (4);
            for (int i = 0; i < n; ++i)
            {
                l[(size_t) i] = rng.nextFloat(); r[(size_t) i] = rng.nextFloat();
                lateL[(size_t) i] = rng.nextFloat(); lateR[(size_t) i] = rng.nextFloat();
                varL[(size_t) i] = rng.nextFloat(); varR[(size_t) i] = rng.nextFloat();
            }
            auto l0 = l, r0 = r;
            m.process (l.data(), r.data(), lateL.data(), lateR.data(), varL.data(), varR.data(), n, 0.0f, 0.0f, 0.5f);
            float maxDiff = 0.0f;
            for (int i = 0; i < n; ++i)
                maxDiff = std::max ({ maxDiff, std::abs (l[(size_t) i] - (l0[(size_t) i] + lateL[(size_t) i])),
                                     std::abs (r[(size_t) i] - (r0[(size_t) i] + lateR[(size_t) i])) });
            expectEquals (maxDiff, 0.0f);
        }

        beginTest ("drift keeps the level and keeps moving the late reverb");
        {
            TailModulator m;
            m.prepare (sr);
            const int n = (int) (10.0 * sr);
            std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f), lateL ((size_t) n), lateR ((size_t) n),
                               varL ((size_t) n), varR ((size_t) n);
            Random rng (6);
            for (auto* v : { &lateL, &lateR, &varL, &varR })
                for (auto& x : *v) x = rng.nextFloat() * 2.0f - 1.0f;
            for (int pos = 0; pos < n; pos += 512)
            {
                const int len = std::min (512, n - pos);
                m.process (l.data() + pos, r.data() + pos, lateL.data() + pos, lateR.data() + pos, varL.data() + pos,
                           varR.data() + pos, len, 100.0f, 0.0f, 1.0f);
            }
            const double levelDb = 10.0 * std::log10 (energy (l, n / 10) / energy (lateL, n / 10));
            expectWithinAbsoluteError (levelDb, 0.0, 0.5, "level");
            // correlation with the unmodulated late reverb in 250 ms windows
            const int win = (int) (0.25 * sr);
            double lowest = 1.0;
            for (int w = 0; (w + 1) * win <= n; ++w)
            {
                double cross = 0.0;
                for (int i = w * win; i < (w + 1) * win; ++i) cross += (double) l[(size_t) i] * lateL[(size_t) i];
                lowest = std::min (lowest, cross / std::sqrt (energy (l, w * win, (w + 1) * win) * energy (lateL, w * win, (w + 1) * win)));
            }
            logMessage ("  level " + String (levelDb, 2) + " dB, lowest correlation with the unmodulated tail " + String (lowest, 2));
            expectLessThan (lowest, 0.5);
        }

        beginTest ("chorus keeps the level, its pitch movement stays small");
        {
            TailModulator m;
            m.prepare (sr);
            const int n = (int) (4.0 * sr);
            std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f), late ((size_t) n), zero ((size_t) n, 0.0f);
            for (int i = 0; i < n; ++i) late[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * 1000.0 * i / sr);
            m.process (l.data(), r.data(), late.data(), late.data(), zero.data(), zero.data(), n, 0.0f, 100.0f, 4.0f);
            expectWithinAbsoluteError (10.0 * std::log10 (energy (l, n / 4) / energy (late, n / 4)), 0.0, 0.2, "level");

            // frequency from the spacing of rising zero crossings (interpolated)
            auto deviation = [&] (const std::vector<float>& x) {
                double last = -1.0, worst = 0.0;
                for (int i = n / 4; i < n; ++i)
                    if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
                    {
                        const double t = (double) (i - 1) + (double) (x[(size_t) i - 1] / (x[(size_t) i - 1] - x[(size_t) i]));
                        if (last > 0.0) worst = std::max (worst, std::abs (sr / (t - last) / 1000.0 - 1.0));
                        last = t;
                    }
                return worst;
            };
            const double devL = deviation (l), devR = deviation (r);
            logMessage ("  largest pitch deviation " + String (devL * 100.0, 2) + " % / " + String (devR * 100.0, 2) + " %");
            expectGreaterThan (devL, 0.003, "it moves");
            expectLessThan (std::max (devL, devR), 0.03);
            expect (l != r, "left and right move differently");
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
            expectEquals (formatOffsetMs (-kMaxOffsetMs), String ("-5.00 s"));
            expectEquals (formatOffsetMs (120.0f), String ("+120 ms"));
        }
    }
};

static ResamplerTest resamplerTest;
class HalfbandTest : public UnitTest
{
public:
    HalfbandTest() : UnitTest ("Halfband (under-sampling)", "concert7-dsp") {}

    /** Full rate signal -> half rate -> full rate, in blocks of varying (also odd) sizes. */
    static std::vector<float> roundTrip (const std::vector<float>& x, std::vector<float>* half = nullptr)
    {
        HalfbandDecimator dec;
        HalfbandInterpolator interp;
        std::vector<float> y (x.size()), h (x.size() / 2 + 8);
        Random rng (5);
        size_t pos = 0;
        while (pos < x.size())
        {
            const int n = std::min ((int) (x.size() - pos), 1 + rng.nextInt (300));
            std::vector<float> tmp ((size_t) n / 2 + 2);
            const int m = dec.process (x.data() + pos, tmp.data(), n);
            if (half != nullptr)
                for (int i = 0; i < m; ++i) half->push_back (tmp[(size_t) i]);
            interp.process (tmp.data(), y.data() + pos, n);
            pos += (size_t) n;
        }
        return y;
    }

    void runTest() override
    {
        const double sr = 96000.0;
        beginTest ("the round trip delays by kDelay and keeps the audio band");
        {
            std::vector<float> x (48000);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (0.5 * std::sin (2.0 * MathConstants<double>::pi * 1000.0 * (double) i / sr)
                                + 0.3 * std::sin (2.0 * MathConstants<double>::pi * 15000.0 * (double) i / sr));
            const auto y = roundTrip (x);
            double err = 0.0, ref = 0.0;
            for (size_t i = 2000; i < x.size(); ++i)
            {
                const double d = y[i] - x[i - (size_t) HalfbandFilter::kDelay];
                err += d * d;
                ref += (double) x[i] * x[i];
            }
            const double snr = 10.0 * std::log10 (ref / std::max (1.0e-30, err));
            logMessage ("  1 kHz + 15 kHz round trip: " + String (snr, 1) + " dB below the signal");
            expectGreaterThan (snr, 60.0);
        }

        beginTest ("the decimator removes what would alias");
        {
            std::vector<float> x (48000), half;
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) std::sin (2.0 * MathConstants<double>::pi * 30000.0 * (double) i / sr);
            roundTrip (x, &half);
            float peak = 0.0f;
            for (size_t i = 200; i < half.size(); ++i)
                peak = std::max (peak, std::abs (half[i]));
            logMessage ("  30 kHz at 96 kHz after halving: " + String (Decibels::gainToDecibels (peak), 1) + " dB");
            expectLessThan (peak, Decibels::decibelsToGain (-70.0f));
            expectEquals ((int) half.size(), (int) x.size() / 2);
        }
    }
};

static HalfbandTest halfbandTest;
static AnalysisTest analysisTest;
static DesignerTest designerTest;
static EngineTest engineTest;
static PostProcessingTest postProcessingTest;
} // namespace concert7::test
