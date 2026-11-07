// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "IRDesigner.h"
#include "../Parameters.h"
#include "AudioFFT.h"
#include "Distance.h"
#include "PostProcessing.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>

namespace concert7
{
using namespace juce;

namespace
{
    constexpr float kSplitMaxSeconds  = 0.08f;  // early/late crossover after the onset
    constexpr float kSplitFadeSeconds = 0.02f;
    constexpr float kOffsetFadeSeconds = 0.002f; // fade-in where a negative offset cuts the response
    constexpr float kVlfCrossoverHz   = 120.0f;
    constexpr float kNormTargetPower  = 0.25f;  // white noise in -> -6 dB wet out (at kNormRate)
    constexpr double kNormRate        = 48000.0; // the same frequency response at every host rate
    constexpr int   kCrossfadeFrames  = 4;      // measured -> synthesised tail
    constexpr float kMaxBoostDb       = 120.0f; // numeric guard only, the band cap does the real limiting
    constexpr float kLateFitStart     = 0.06f;  // RT is fitted on the late reverb, after the early reflections
    constexpr float kCapMarginDb      = 6.0f;   // allowed excess over the target decay line
    constexpr float kEndToleranceDb   = 1.5f;   // the valid range ends where the level leaves the decay line by more
    constexpr float kEndWindowSeconds = 0.08f;  // ... averaged over +- this
    constexpr float kEndBackoffSeconds = 0.5f;  // ... and at most this much earlier than the fade / floor detection
    constexpr float kSmoothCutDb      = 9.0f;   // Smooth: largest cut of a resonance / envelope bump
    constexpr float kSmoothBoostDb    = 3.0f;   // Smooth: largest lift of a dip
    constexpr float kSmoothRampSeconds = 0.05f; // Smooth: envelope correction fades in after the split
    constexpr float kSmoothLowHz      = 40.0f;  // Smooth: frequency range of the resonance correction
    constexpr float kSmoothHighHz     = 16000.0f;
    constexpr float kBinFitStart      = 0.12f;  // decay of every bin: fitted from here on (after the early part)
    constexpr int   kMinBinFrames     = 6;
    constexpr float kBinRTRange       = 3.0f;   // a bin's reverb time stays within this factor of its band's
    constexpr float kDeepBassFullHz   = 100.0f; // deep bass limit (limitDeepBass): in full below, fading out up to
    constexpr float kDeepBassEndHz    = 125.0f;
    constexpr float kDeepBassMarginDb = 3.0f;   // ... the band level may exceed its limited decay line by this
    constexpr float kLateLevelSlope   = 0.55f;  // DECAY: the late reverb starts this many dB lower per dB of 10 log10 (RT ratio)
    constexpr double kCoherenceRidge  = 1e-3;
    constexpr int   kPhaseTableSize   = 4096;   // synthesised tail: random phases from a table
    constexpr float kVariantMatchSeconds = 0.01f; // late variant: block length of its energy match
    constexpr float kVariantMaxGain   = 4.0f;
    constexpr int64 kVariantSeed      = 0x7a11;
    constexpr double kPi = MathConstants<double>::pi;

    int stftSizeFor (double sr) { return sr <= 50000.0 ? 2048 : (sr <= 100000.0 ? 4096 : 8192); }

    /** Short-time Fourier transform with a periodic Hann window and 75 % overlap.
        Frame m is centred on sample m * hop. */
    struct Stft
    {
        explicit Stft (int size) : N (size), hop (size / 4), bins (size / 2 + 1), window ((size_t) size), frame ((size_t) size)
        {
            for (int n = 0; n < N; ++n)
                window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * n / N));
            fft.init ((size_t) N);
        }

        int numFrames (int len) const { return (len + N / 2) / hop + 2; }

        void forward (const float* x, int len, int m, float* re, float* im)
        {
            const int start = m * hop - N / 2;
            for (int n = 0; n < N; ++n)
            {
                const int idx = start + n;
                frame[(size_t) n] = (idx >= 0 && idx < len) ? x[idx] * window[(size_t) n] : 0.0f;
            }
            fft.fft (frame.data(), re, im);
        }

        void inverseAccumulate (const float* re, const float* im, int m, float* y, float* norm, int len)
        {
            fft.ifft (frame.data(), re, im);
            const int start = m * hop - N / 2;
            for (int n = 0; n < N; ++n)
            {
                const int idx = start + n;
                if (idx >= 0 && idx < len)
                {
                    const float w = window[(size_t) n];
                    y[idx] += frame[(size_t) n] * w;
                    norm[idx] += w * w;
                }
            }
        }

        int N, hop, bins;
        std::vector<float> window, frame;
        audiofft::AudioFFT fft;
    };

    std::vector<float> thirdOctaveCentres (double sr)
    {
        std::vector<float> f;
        for (int k = -16; k <= 12; ++k)
        {
            const double fc = 1000.0 * std::pow (2.0, k / 3.0);
            if (fc * std::pow (2.0, 1.0 / 6.0) < 0.48 * sr)
                f.push_back ((float) fc);
        }
        return f;
    }

    /** Linear interpolation over log frequency, clamped at both ends. */
    float interpLogFreq (const std::vector<float>& freqs, const std::vector<float>& values, float f)
    {
        if (freqs.empty()) return 0.0f;
        if (f <= freqs.front()) return values.front();
        if (f >= freqs.back()) return values.back();
        auto it = std::upper_bound (freqs.begin(), freqs.end(), f);
        const size_t i = (size_t) (it - freqs.begin());
        const float a = std::log (freqs[i - 1]), b = std::log (freqs[i]);
        const float t = (std::log (f) - a) / (b - a);
        return values[i - 1] + t * (values[i] - values[i - 1]);
    }

    /** RT60 from a frame energy series via Schroeder integration and a T20 (or T10) fit.
        Integration stops at endFrame (exclusive); tailEnergy accounts for the decay that
        continues below the noise floor (Lundeby style compensation). */
    float fitRT (const std::vector<double>& energy, int m0, double frameSeconds, int endFrame = -1, double tailEnergy = 0.0)
    {
        const int M = endFrame < 0 ? (int) energy.size() : std::min ((int) energy.size(), endFrame);
        if (m0 < 0 || m0 >= M - 3) return std::numeric_limits<float>::quiet_NaN();

        std::vector<double> S ((size_t) M + 1, 0.0);
        S[(size_t) M] = tailEnergy;
        for (int m = M - 1; m >= 0; --m)
            S[(size_t) m] = S[(size_t) m + 1] + energy[(size_t) m];
        const double ref = S[(size_t) m0];
        if (ref <= 1e-30) return std::numeric_limits<float>::quiet_NaN();

        auto edc = [&] (int m) { return 10.0 * std::log10 (std::max (S[(size_t) m] / ref, 1e-30)); };

        int m5 = -1, mEnd = -1;
        for (int m = m0; m < M; ++m)
            if (edc (m) <= -5.0) { m5 = m; break; }
        if (m5 < 0) return std::numeric_limits<float>::quiet_NaN();

        for (const double limit : { -25.0, -15.0 })
        {
            for (int m = m5; m < M; ++m)
                if (edc (m) <= limit) { mEnd = m; break; }
            if (mEnd >= 0) break;
        }
        if (mEnd < 0 || mEnd - m5 < 2) return std::numeric_limits<float>::quiet_NaN();

        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const int n = mEnd - m5 + 1;
        for (int m = m5; m <= mEnd; ++m)
        {
            const double x = (m - m5) * frameSeconds, y = edc (m);
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double slope = (n * sxy - sx * sy) / std::max (1e-12, n * sxx - sx * sx);
        if (slope >= -1e-3) return std::numeric_limits<float>::quiet_NaN();
        return (float) (-60.0 / slope);
    }

    int detectOnset (const std::array<std::vector<float>, 4>& ch, double sr)
    {
        float peak = 0.0f;
        for (auto& c : ch)
            for (float v : c)
                peak = std::max (peak, std::abs (v));
        if (peak <= 0.0f) return 0;

        const float thr = peak * 0.05f; // -26 dB
        int first = std::numeric_limits<int>::max();
        for (auto& c : ch)
            for (size_t i = 0; i < c.size(); ++i)
                if (std::abs (c[i]) >= thr) { first = std::min (first, (int) i); break; }
        if (first == std::numeric_limits<int>::max()) return 0;
        return std::max (0, first - (int) (0.001 * sr));
    }

    /** Frame energies (sum over channels) per third-octave band. */
    struct BandEnergies
    {
        std::vector<float> centres;
        std::vector<std::vector<double>> energy; // [band][frame]
        int hop = 512;
        int frames = 0;
    };

    BandEnergies computeBandEnergies (const std::vector<const std::vector<float>*>& channels, double sr,
                                      const std::vector<std::pair<float, float>>& bands)
    {
        BandEnergies be;
        const int len = channels.empty() ? 0 : (int) channels.front()->size();
        Stft stft (stftSizeFor (sr));
        be.hop = stft.hop;
        be.frames = stft.numFrames (len);
        be.energy.assign (bands.size(), std::vector<double> ((size_t) be.frames, 0.0));

        std::vector<std::pair<int, int>> binRanges;
        for (auto& [lo, hi] : bands)
        {
            int a = (int) std::ceil (lo * stft.N / sr), b = (int) std::floor (hi * stft.N / sr);
            a = jlimit (1, stft.bins - 1, a);
            b = jlimit (a, stft.bins - 1, b);
            binRanges.push_back ({ a, b });
        }

        std::vector<float> re ((size_t) stft.bins), im ((size_t) stft.bins), power ((size_t) stft.bins);
        for (int m = 0; m < be.frames; ++m)
        {
            std::fill (power.begin(), power.end(), 0.0f);
            for (auto* c : channels)
            {
                if (c->empty()) continue;
                stft.forward (c->data(), len, m, re.data(), im.data());
                for (int k = 0; k < stft.bins; ++k)
                    power[(size_t) k] += re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k];
            }
            for (size_t b = 0; b < bands.size(); ++b)
            {
                double e = 0.0;
                for (int k = binRanges[b].first; k <= binRanges[b].second; ++k)
                    e += power[(size_t) k];
                be.energy[b][(size_t) m] = e;
            }
        }
        return be;
    }

    std::vector<const std::vector<float>*> activeChannels (const std::array<std::vector<float>, 4>& ch, bool trueStereo)
    {
        if (trueStereo) return { &ch[0], &ch[1], &ch[2], &ch[3] };
        return { &ch[0], &ch[3] };
    }

    /** RBJ low pass, Butterworth Q, applied in place. */
    void lowPassInPlace (std::vector<float>& x, double sr, double fc)
    {
        if (fc >= std::min ((double) kRolloffBypassHz, 0.45 * sr)) return;
        const double w0 = 2.0 * kPi * fc / sr, cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + alpha;
        const double b0 = (1.0 - cw) * 0.5 / a0, b1 = (1.0 - cw) / a0, b2 = b0;
        const double a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
        double z1 = 0, z2 = 0;
        for (auto& v : x)
        {
            const double in = v, out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            v = (float) out;
        }
    }

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum) break;
        }
        return sum;
    }

    float deepBassWeight (float f)
    {
        return jlimit (0.0f, 1.0f, (kDeepBassEndHz - f) / (kDeepBassEndHz - kDeepBassFullHz));
    }

    /** A native reverb time with the deep bass limit of the Bricasti library applied (limitDeepBass). */
    float limitedRT (const SourceIR& src, float rn, float f)
    {
        const float w = src.deepBassRT > 0.0f ? deepBassWeight (f) : 0.0f;
        if (w <= 0.0f || rn <= src.deepBassRT) return rn;
        return std::pow (rn, 1.0f - w) * std::pow (src.deepBassRT, w);
    }

    float decayMultiplier (float f, const DesignParams& p)
    {
        const float wl = 1.0f / (1.0f + (f / p.lowFreq) * (f / p.lowFreq));
        const float wh = 1.0f / (1.0f + (p.highFreq / f) * (p.highFreq / f));
        return std::pow (p.lowMult, wl) * std::pow (p.highMult, wh);
    }

    /** Native band RT -> target RT for DECAY (before the multi-band multipliers).
        Sabine: 1/RT = A / 0.161 V + 4 m / 0.161, the decay rate of the walls plus that of the air. A longer or
        shorter DECAY is a room with more or less wall absorption (or larger / smaller), so only the wall part
        is scaled. The air part is the same in any room: the highs, where it dominates, grow less than the mids
        in a larger hall, and the decay gets flatter over frequency in a smaller room.
        The recording only gives the sum, so it is split: the walls absorb the highs at least as much as the
        mids, whatever shortens the highs beyond that is air - at most as much as air can (ISO 9613-1). */
    struct DecayScaling
    {
        DecayScaling (const SourceIR& src, float decay)
            : wallMid (std::max (0.0f, 1.0f / std::max (0.01f, src.nativeRT) - airRate (1000.0f)))
        {
            // wall factor k so that the mid bands (as in nativeRT) still give DECAY on average
            auto midRT = [&] (float kk) {
                double logSum = 0.0; int count = 0;
                for (size_t b = 0; b < src.bandFreq.size(); ++b)
                    if (src.bandFreq[b] >= 490.0f && src.bandFreq[b] <= 2100.0f)
                    { logSum += std::log (target (src.bandRT[b], src.bandFreq[b], kk)); ++count; }
                return count > 0 ? (float) std::exp (logSum / count) : src.nativeRT / kk;
            };
            float lo = 1e-3f, hi = 1e3f;
            for (int i = 0; i < 50; ++i)
            {
                k = std::sqrt (lo * hi);
                (midRT (k) > decay ? lo : hi) = k;
            }
        }

        float operator() (float rn, float f) const { return target (rn, f, k); }

    private:
        float target (float rn, float f, float kk) const
        {
            const float air = jlimit (0.0f, airRate (f), 1.0f / rn - wallMid);
            return 1.0f / ((1.0f / rn - air) * kk + air);
        }

        static float airRate (float f)
        {
            // 4 m / 0.161 in 1/s, m = energy attenuation per metre
            return (float) (4.0 * airAbsorptionDbPerMetre (f, kAirHumidity) / 4.343 / 0.161);
        }

        float wallMid, k = 1.0f;
    };

    std::vector<float> envelopeDb (const std::vector<const std::vector<float>*>& channels, int points, int totalSamples,
                                   double refPower)
    {
        std::vector<float> env ((size_t) points, -120.0f);
        const int seg = std::max (1, totalSamples / points);
        for (int p = 0; p < points; ++p)
        {
            double acc = 0.0;
            const int a = p * seg;
            for (auto* c : channels)
                for (int i = a; i < std::min ((int) c->size(), a + seg); ++i)
                    acc += (double) (*c)[(size_t) i] * (*c)[(size_t) i];
            acc /= seg;
            env[(size_t) p] = (float) (10.0 * std::log10 (std::max (acc / refPower, 1e-12)));
        }
        return env;
    }

    double maxSegmentPower (const std::vector<const std::vector<float>*>& channels, int points, int totalSamples)
    {
        const int seg = std::max (1, totalSamples / points);
        double best = 1e-30;
        for (int p = 0; p < points; ++p)
        {
            double acc = 0.0;
            for (auto* c : channels)
                for (int i = p * seg; i < std::min ((int) c->size(), (p + 1) * seg); ++i)
                    acc += (double) (*c)[(size_t) i] * (*c)[(size_t) i];
            best = std::max (best, acc / seg);
        }
        return best;
    }

    /** The late reverb with an independent fine structure: every STFT frame gets new random phases (the same
        for all channels, so their relation stays), then a short-time energy match restores the envelope. */
    std::array<std::vector<float>, 4> makeVariant (const std::array<std::vector<float>, 4>& late,
                                                   const std::vector<int>& chans, double sr)
    {
        std::array<std::vector<float>, 4> out;
        const int len = (int) late[(size_t) chans.front()].size();
        if (len == 0) return out;

        Stft stft (stftSizeFor (sr));
        const int bins = stft.bins, frames = stft.numFrames (len);
        const float synthGain = std::sqrt ((float) stft.N / (float) stft.hop); // incoherent overlap-add compensation
        std::vector<float> re ((size_t) bins), im ((size_t) bins), norm ((size_t) len);
        for (int c : chans)
        {
            auto& y = out[(size_t) c];
            y.assign ((size_t) len, 0.0f);
            std::fill (norm.begin(), norm.end(), 0.0f);
            for (int m = 0; m < frames; ++m)
            {
                stft.forward (late[(size_t) c].data(), len, m, re.data(), im.data());
                Random rng (kVariantSeed + 7919 * (int64) m); // per frame: the same phases for every channel
                for (int k = 0; k < bins; ++k)
                {
                    const float phase = rng.nextFloat() * MathConstants<float>::twoPi;
                    const float g = (k == 0 || k == bins - 1) ? 0.0f : synthGain;
                    const float c0 = std::cos (phase) * g, s0 = std::sin (phase) * g;
                    const float r = re[(size_t) k], i = im[(size_t) k];
                    re[(size_t) k] = r * c0 - i * s0;
                    im[(size_t) k] = r * s0 + i * c0;
                }
                stft.inverseAccumulate (re.data(), im.data(), m, y.data(), norm.data(), len);
            }
            for (int n = 0; n < len; ++n)
                y[(size_t) n] = norm[(size_t) n] > 1e-3f ? y[(size_t) n] / norm[(size_t) n] : 0.0f;
        }

        // energy match in short blocks (all channels together), interpolated between the block centres
        const int block = std::max (16, (int) (kVariantMatchSeconds * sr));
        const int numBlocks = (len + block - 1) / block;
        std::vector<float> gain ((size_t) numBlocks);
        for (int b = 0; b < numBlocks; ++b)
        {
            double eL = 0.0, eV = 0.0;
            for (int c : chans)
                for (int n = b * block; n < std::min (len, (b + 1) * block); ++n)
                {
                    eL += (double) late[(size_t) c][(size_t) n] * late[(size_t) c][(size_t) n];
                    eV += (double) out[(size_t) c][(size_t) n] * out[(size_t) c][(size_t) n];
                }
            gain[(size_t) b] = eV > 1e-30 ? std::min (kVariantMaxGain, (float) std::sqrt (eL / eV)) : 0.0f;
        }
        for (int n = 0; n < len; ++n)
        {
            const float pos = jlimit (0.0f, (float) (numBlocks - 1), ((float) n + 0.5f) / (float) block - 0.5f);
            const int b0 = (int) pos, b1 = std::min (numBlocks - 1, b0 + 1);
            const float g = gain[(size_t) b0] + (pos - (float) b0) * (gain[(size_t) b1] - gain[(size_t) b0]);
            for (int c : chans)
                out[(size_t) c][(size_t) n] *= g;
        }
        return out;
    }
} // namespace

bool DesignParams::operator== (const DesignParams& o) const
{
    return decay == o.decay && predelayMs == o.predelayMs && offsetMs == o.offsetMs && earlyDb == o.earlyDb && lateDb == o.lateDb
        && vlfDb == o.vlfDb && erRolloffHz == o.erRolloffHz && lateRolloffHz == o.lateRolloffHz
        && delayMs == o.delayMs && delayDb == o.delayDb && lowFreq == o.lowFreq && lowMult == o.lowMult
        && highFreq == o.highFreq && highMult == o.highMult && smooth == o.smooth
        && splitLate == o.splitLate && lateVariant == o.lateVariant;
}

//==============================================================================
std::vector<float> resample (const std::vector<float>& in, double srcRate, double dstRate)
{
    if (in.empty() || srcRate <= 0.0 || dstRate <= 0.0 || std::abs (srcRate - dstRate) < 1e-6)
        return in;

    const double ratio = dstRate / srcRate;
    const double fc = 0.5 * std::min (1.0, ratio) * (ratio < 1.0 ? 0.92 : 0.95); // cycles per input sample
    const int zeroCrossings = 48;
    const double halfWidth = zeroCrossings / (2.0 * fc);           // in input samples
    const int oversample = 512;
    const int tableSize = (int) std::ceil (halfWidth * oversample) + 2;
    const double beta = 9.0, i0beta = besselI0 (beta);

    std::vector<float> table ((size_t) tableSize);
    for (int i = 0; i < tableSize; ++i)
    {
        const double t = (double) i / oversample;
        const double x = 2.0 * fc * t;
        const double sinc = x == 0.0 ? 1.0 : std::sin (kPi * x) / (kPi * x);
        const double r = t / halfWidth;
        const double w = r >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0beta;
        table[(size_t) i] = (float) (2.0 * fc * sinc * w);
    }

    const int n = (int) in.size();
    const int outLen = (int) std::floor (n * ratio);
    std::vector<float> out ((size_t) outLen);
    for (int j = 0; j < outLen; ++j)
    {
        const double x = j / ratio;
        const int k0 = (int) std::ceil (x - halfWidth), k1 = (int) std::floor (x + halfWidth);
        double acc = 0.0;
        for (int k = std::max (0, k0); k <= std::min (n - 1, k1); ++k)
        {
            const double pos = std::abs (x - k) * oversample;
            const int ip = (int) pos;
            const double frac = pos - ip;
            if (ip + 1 >= tableSize) continue;
            acc += in[(size_t) k] * (table[(size_t) ip] + frac * (table[(size_t) ip + 1] - table[(size_t) ip]));
        }
        out[(size_t) j] = (float) acc;
    }
    return out;
}

//==============================================================================
void analyseSourceIR (SourceIR& src)
{
    const double sr = src.sampleRate;
    auto channels = activeChannels (src.ch, src.trueStereo);
    src.onset = detectOnset (src.ch, sr);

    const auto centres = thirdOctaveCentres (sr);
    std::vector<std::pair<float, float>> bands;
    for (float fc : centres)
        bands.push_back ({ fc * std::pow (2.0f, -1.0f / 6.0f), fc * std::pow (2.0f, 1.0f / 6.0f) });

    auto be = computeBandEnergies (channels, sr, bands);
    const double frameSeconds = be.hop / sr;
    const int m0 = (int) std::lround ((double) src.onset / be.hop);
    // Direct sound and early reflections would dominate the Schroeder curve of weak (high) bands,
    // so reverb times are fitted from the start of the late reverb on.
    const int mLate = (int) std::lround ((src.onset + kLateFitStart * sr) / be.hop);
    const int lastFull = std::max (m0 + 1, (src.length() - stftSizeFor (sr) / 2) / be.hop);

    src.bandFreq = centres;
    src.bandRT.assign (centres.size(), std::numeric_limits<float>::quiet_NaN());
    src.bandValidEnd.assign (centres.size(), 0.0f);
    src.bandIntercept.assign (centres.size(), 0.0f);
    src.bandLevelDb.assign (centres.size(), {});
    std::vector<std::pair<int, int>> fitRange (centres.size(), { 0, 0 });

    auto lineFit = [] (const std::vector<float>& y, int a, int b) {
        // least squares line through y[a..b]: returns (offset at a, slope per frame)
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const int n = b - a + 1;
        for (int m = a; m <= b; ++m)
        {
            const double x = m - a;
            sx += x; sy += y[(size_t) m]; sxx += x * x; sxy += x * y[(size_t) m];
        }
        const double slope = n > 1 ? (n * sxy - sx * sy) / std::max (1e-12, n * sxx - sx * sx) : 0.0;
        return std::pair<double, double> ((sy - slope * sx) / std::max (1, n), slope);
    };

    for (size_t b = 0; b < centres.size(); ++b)
    {
        const auto& e = be.energy[b];
        const int last = std::min (lastFull, be.frames - 1);

        // smoothed level in dB
        std::vector<float> level ((size_t) be.frames);
        for (int m = 0; m < be.frames; ++m)
        {
            double acc = 0.0; int cnt = 0;
            for (int j = std::max (0, m - 2); j <= std::min (be.frames - 1, m + 2); ++j) { acc += e[(size_t) j]; ++cnt; }
            level[(size_t) m] = (float) (10.0 * std::log10 (acc / cnt + 1e-30));
        }
        int peakFrame = m0;
        for (int m = m0; m <= last; ++m)
            if (level[(size_t) m] > level[(size_t) peakFrame]) peakFrame = m;
        const float peakDb = level[(size_t) peakFrame];
        const int span = last - peakFrame;

        const int mFit = std::min (mLate, std::max (m0, last - 8));
        float rt = fitRT (e, mFit, frameSeconds, last + 1);
        int validEnd = last;
        for (int iteration = 0; iteration < 2 && span >= 8; ++iteration)
        {
            const double reverbSlope = std::isfinite (rt) ? -60.0 / rt * frameSeconds : -1.0; // dB per frame

            // Recordings either end in a noise floor (flat tail) or are cut / faded while still decaying.
            const int w0 = last - std::max (4, span / 5);
            const auto tail = lineFit (level, w0, last);
            const bool hasFloor = tail.second > 0.3 * reverbSlope;
            double noisePower = 0.0;
            validEnd = last;
            if (hasFloor)
            {
                std::vector<float> t (level.begin() + w0, level.begin() + last + 1);
                std::nth_element (t.begin(), t.begin() + (long) t.size() / 2, t.end());
                const float floorDb = t[t.size() / 2];
                noisePower = std::pow (10.0, floorDb / 10.0);
                for (int m = peakFrame + 1; m <= last; ++m)
                    if (level[(size_t) m] < floorDb + 8.0f) { validEnd = std::max (peakFrame, m - 1); break; }
            }
            else
            {
                // trend before the end, then cut where the level falls clearly below it (fade-out)
                const int a = peakFrame + (int) (span * 0.45), bEnd = peakFrame + (int) (span * 0.8);
                const auto trend = lineFit (level, a, bEnd);
                for (int m = bEnd; m <= last; ++m)
                    if (level[(size_t) m] < trend.first + trend.second * (m - a) - 4.0) { validEnd = std::max (peakFrame, m - 1); break; }
            }
            for (int m = peakFrame + 1; m <= validEnd; ++m)
                if (level[(size_t) m] < peakDb - 85.0f) { validEnd = m - 1; break; }

            // noise compensated Schroeder integration with the extrapolated tail beyond validEnd
            std::vector<double> clean ((size_t) validEnd + 1);
            for (int m = 0; m <= validEnd; ++m)
                clean[(size_t) m] = std::max (0.0, e[(size_t) m] - noisePower);
            const double q = std::pow (10.0, reverbSlope / 10.0);
            const double tailEnergy = q < 1.0 ? clean[(size_t) validEnd] * q / (1.0 - q) : 0.0;
            const float refined = fitRT (clean, std::min (mFit, std::max (m0, validEnd - 4)), frameSeconds, validEnd + 1, tailEnergy);
            if (std::isfinite (refined)) rt = refined;
        }
        // The decay the designer modifies is the slope of the band level over the usable late range.
        // A straight line fit of the level gives a slope and an intercept that are consistent with each
        // other (the Schroeder value above is only used to locate the noise floor / end of the data).
        int fitA = std::max (mFit, peakFrame), fitB = validEnd;
        while (fitB > fitA && level[(size_t) fitB] < peakDb - 65.0f) --fitB;
        if (fitB - fitA >= 4)
        {
            const auto line = lineFit (level, fitA, fitB);
            if (line.second < -1e-4)
                rt = (float) (-60.0 / (line.second / frameSeconds));
        }
        fitRange[b] = { fitA, std::max (fitA, validEnd) };
        src.bandRT[b] = rt;
        src.bandValidEnd[b] = (float) ((validEnd * be.hop - src.onset) / sr);
        src.bandLevelDb[b] = std::move (level);
    }

    // fill gaps, smooth, clamp
    const float broadband = estimateRT (channels, sr);
    for (size_t b = 0; b < src.bandRT.size(); ++b)
    {
        if (std::isfinite (src.bandRT[b])) continue;
        float lo = NAN, hi = NAN;
        for (size_t j = b; j-- > 0;) if (std::isfinite (src.bandRT[j])) { lo = src.bandRT[j]; break; }
        for (size_t j = b + 1; j < src.bandRT.size(); ++j) if (std::isfinite (src.bandRT[j])) { hi = src.bandRT[j]; break; }
        src.bandRT[b] = std::isfinite (lo) && std::isfinite (hi) ? 0.5f * (lo + hi)
                      : std::isfinite (lo) ? lo : std::isfinite (hi) ? hi
                      : (std::isfinite (broadband) ? broadband : 1.0f);
    }
    auto raw = src.bandRT;
    for (size_t b = 1; b + 1 < raw.size(); ++b)
    {
        float v[3] = { raw[b - 1], raw[b], raw[b + 1] };
        std::sort (v, v + 3);
        src.bandRT[b] = v[1];
    }
    for (auto& rt : src.bandRT)
        rt = jlimit (0.05f, 60.0f, rt);

    double logSum = 0.0; int count = 0;
    for (size_t b = 0; b < centres.size(); ++b)
        if (centres[b] >= 490.0f && centres[b] <= 2100.0f) { logSum += std::log (src.bandRT[b]); ++count; }
    src.nativeRT = count > 0 ? (float) std::exp (logSum / count) : (std::isfinite (broadband) ? broadband : 1.0f);

    // bands with little energy can produce implausible values: keep them within a sane ratio to the mids
    for (auto& rt : src.bandRT)
        rt = jlimit (0.25f * src.nativeRT, 4.0f * src.nativeRT, rt);

    // intercept of each band's decay line (median: robust against build-up and fluctuations)
    for (size_t b = 0; b < centres.size(); ++b)
    {
        std::vector<float> icept;
        const auto& level = src.bandLevelDb[b];
        for (int m = fitRange[b].first; m <= fitRange[b].second && m < (int) level.size(); ++m)
        {
            const float tau = (float) ((m * be.hop - src.onset) / sr);
            icept.push_back (level[(size_t) m] + 60.0f * tau / src.bandRT[b]);
        }
        if (icept.empty())
            icept.push_back (level.empty() ? 0.0f : *std::max_element (level.begin(), level.end()));
        std::nth_element (icept.begin(), icept.begin() + (long) icept.size() / 2, icept.end());
        src.bandIntercept[b] = icept[icept.size() / 2];
    }

    // The last part of the valid range can already leave the decay line (the start of a fade-out or of the
    // noise floor, a few dB only). A longer decay would keep that dip and the synthesised tail would start on
    // the line again: the level sinks and swells. So the valid range ends where the level still follows the line.
    const int endWindow = std::max (1, (int) std::lround (kEndWindowSeconds / frameSeconds));
    const int maxBackoff = (int) std::lround (kEndBackoffSeconds / frameSeconds);
    for (size_t b = 0; b < centres.size(); ++b)
    {
        const auto& level = src.bandLevelDb[b];
        auto deviation = [&] (int m) {
            double acc = 0.0; int cnt = 0;
            for (int j = std::max (0, m - endWindow); j <= std::min ((int) level.size() - 1, m + endWindow); ++j)
            {
                const float tau = (float) ((j * be.hop - src.onset) / sr);
                acc += level[(size_t) j] - (src.bandIntercept[b] - 60.0f * tau / src.bandRT[b]);
                ++cnt;
            }
            return cnt > 0 ? (float) (acc / cnt) : 0.0f;
        };
        int validEnd = (int) std::lround ((src.onset + src.bandValidEnd[b] * sr) / be.hop);
        const int limit = std::max (fitRange[b].first + 4, validEnd - maxBackoff);
        while (validEnd > limit && std::abs (deviation (validEnd)) > kEndToleranceDb)
            --validEnd;
        src.bandValidEnd[b] = (float) ((validEnd * be.hop - src.onset) / sr);
    }

    // colouration of the late reverb (for Smooth): mean level of every STFT bin over its valid late range with
    // the native decay removed, so long ringing counts as much as loud, relative to its third-octave neighbourhood
    Stft stft (stftSizeFor (sr));
    const int len = src.length(), bins = stft.bins;
    const int lastFullFrame = std::max (1, (len - stft.N / 2) / stft.hop);
    std::vector<float> rn ((size_t) bins);
    std::vector<int> validFrame ((size_t) bins);
    int lastFrame = 0;
    for (int k = 0; k < bins; ++k)
    {
        const float f = std::max (1.0f, (float) (k * sr / stft.N));
        rn[(size_t) k] = interpLogFreq (src.bandFreq, src.bandRT, f);
        const float tv = interpLogFreq (src.bandFreq, src.bandValidEnd, f);
        validFrame[(size_t) k] = jlimit (1, lastFullFrame, (int) std::floor ((src.onset + tv * sr) / stft.hop));
        lastFrame = std::max (lastFrame, validFrame[(size_t) k]);
    }

    // the bins' own decay is fitted from where the early part has surely faded out (or from the start of the
    // late reverb on, where that leaves too few frames)
    const int mBin = std::max (mLate, (int) std::lround ((src.onset + kBinFitStart * sr) / stft.hop));
    std::vector<int> binStart ((size_t) bins);
    for (int k = 0; k < bins; ++k)
        binStart[(size_t) k] = std::min (validFrame[(size_t) k] - mBin >= kMinBinFrames ? mBin : std::max (0, mLate),
                                         std::max (0, validFrame[(size_t) k] - kMinBinFrames));

    std::vector<double> levelSum ((size_t) bins, 0.0);
    std::vector<int> levelCount ((size_t) bins, 0);
    std::vector<std::array<double, 5>> fitSums ((size_t) bins, { 0.0, 0.0, 0.0, 0.0, 0.0 }); // n, t, y, tt, ty
    std::vector<float> re ((size_t) bins), im ((size_t) bins), power ((size_t) bins);
    for (int m = std::max (0, mLate); m <= lastFrame; ++m)
    {
        std::fill (power.begin(), power.end(), 0.0f);
        for (auto* c : channels)
        {
            if (c->empty()) continue;
            stft.forward (c->data(), len, m, re.data(), im.data());
            for (int k = 0; k < bins; ++k)
                power[(size_t) k] += re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k];
        }
        const double tau = (m * stft.hop - src.onset) / sr;
        for (int k = 0; k < bins; ++k)
            if (m <= validFrame[(size_t) k])
            {
                const double y = 10.0 * std::log10 ((double) power[(size_t) k] + 1e-30);
                levelSum[(size_t) k] += y + 60.0 * tau / rn[(size_t) k];
                ++levelCount[(size_t) k];
                if (m >= binStart[(size_t) k])
                {
                    auto& s = fitSums[(size_t) k];
                    s[0] += 1.0; s[1] += tau; s[2] += y; s[3] += tau * tau; s[4] += tau * y;
                }
            }
    }

    // reverb time of every bin: the slope of its level, the decay rates averaged over 1/16 octave. The band
    // values keep it within reason (bins with little energy, the ends of the spectrum).
    {
        std::vector<double> rate ((size_t) bins, 0.0);
        std::vector<bool> fitted ((size_t) bins, false);
        for (int k = 1; k < bins - 1; ++k)
        {
            const auto& s = fitSums[(size_t) k];
            const double det = s[0] * s[3] - s[1] * s[1];
            if (s[0] < kMinBinFrames || det <= 1e-12) continue;
            const double slope = (s[0] * s[4] - s[1] * s[2]) / det; // dB per second
            if (slope >= -1e-3) continue;
            rate[(size_t) k] = -slope / 60.0;
            fitted[(size_t) k] = true;
        }
        src.binRT.assign ((size_t) bins, 0.0f);
        for (int k = 0; k < bins; ++k)
        {
            const int w = std::max (1, (int) (k * 0.0217)); // 2^(1/32) - 1
            double acc = 0.0; int cnt = 0;
            for (int j = std::max (0, k - w); j <= std::min (bins - 1, k + w); ++j)
                if (fitted[(size_t) j]) { acc += rate[(size_t) j]; ++cnt; }
            const float band = rn[(size_t) k];
            src.binRT[(size_t) k] = cnt > 0 ? jlimit (band / kBinRTRange, band * kBinRTRange, (float) (cnt / acc)) : band;
        }
    }

    // power of every channel on its bin's decay line at the onset, and the channels' cross spectra: each frame
    // with the bin's decay removed, so the late frames count as much as the early ones
    {
        const int nch = (int) channels.size();
        std::vector<std::array<std::complex<double>, 10>> cross ((size_t) bins);
        for (auto& sums : cross) sums.fill ({ 0.0, 0.0 });
        std::vector<int> frames ((size_t) bins, 0);
        std::array<std::vector<float>, 4> xr, xi;
        for (int i = 0; i < nch; ++i) { xr[(size_t) i].resize ((size_t) bins); xi[(size_t) i].resize ((size_t) bins); }
        const int first = *std::min_element (binStart.begin(), binStart.end());
        for (int m = first; m <= lastFrame; ++m)
        {
            for (int i = 0; i < nch; ++i)
                stft.forward (channels[(size_t) i]->data(), len, m, xr[(size_t) i].data(), xi[(size_t) i].data());
            const double tau = (m * stft.hop - src.onset) / sr;
            for (int k = 0; k < bins; ++k)
            {
                if (m < binStart[(size_t) k] || m > validFrame[(size_t) k]) continue;
                const double w = std::pow (10.0, std::min (250.0, 6.0 * tau / src.binRT[(size_t) k]));
                auto& cr = cross[(size_t) k];
                for (int i = 0; i < nch; ++i)
                {
                    const std::complex<double> a (xr[(size_t) i][(size_t) k], xi[(size_t) i][(size_t) k]);
                    for (int j = 0; j <= i; ++j)
                        cr[(size_t) (i * (i + 1) / 2 + j)] += w * a * std::conj (std::complex<double> (xr[(size_t) j][(size_t) k], xi[(size_t) j][(size_t) k]));
                }
                ++frames[(size_t) k];
            }
        }

        for (auto& p : src.binPower) p.clear();
        src.binMix.assign ((size_t) bins * 10, { 0.0f, 0.0f });
        for (int i = 0; i < nch; ++i)
        {
            const int c = src.trueStereo ? i : (i == 0 ? 0 : 3);
            src.binPower[(size_t) c].assign ((size_t) bins, 0.0f);
            for (int k = 0; k < bins; ++k)
                if (frames[(size_t) k] > 0)
                    src.binPower[(size_t) c][(size_t) k] = (float) (cross[(size_t) k][(size_t) (i * (i + 3) / 2)].real() / frames[(size_t) k]);
        }

        // coherence, averaged over 1/16 octave (a few frames alone would overstate it), then its Cholesky factor.
        // A little added to the diagonal keeps it positive definite.
        for (int k = 0; k < bins; ++k)
        {
            const int w = std::max (1, (int) (k * 0.0217));
            std::array<std::complex<double>, 10> s;
            s.fill ({ 0.0, 0.0 });
            for (int j = std::max (0, k - w); j <= std::min (bins - 1, k + w); ++j)
                for (size_t q = 0; q < s.size(); ++q) s[q] += cross[(size_t) j][q];

            std::array<std::complex<double>, 10> g;
            bool ok = true;
            for (int i = 0; i < nch && ok; ++i)
                ok = s[(size_t) (i * (i + 3) / 2)].real() > 1e-30;
            for (int i = 0; i < nch && ok; ++i)
                for (int j = 0; j <= i; ++j)
                {
                    const double norm = std::sqrt (s[(size_t) (i * (i + 3) / 2)].real() * s[(size_t) (j * (j + 3) / 2)].real());
                    g[(size_t) (i * (i + 1) / 2 + j)] = i == j ? std::complex<double> (1.0 + kCoherenceRidge, 0.0)
                                                               : s[(size_t) (i * (i + 1) / 2 + j)] / norm;
                }

            std::array<std::complex<double>, 10> L;
            L.fill ({ 0.0, 0.0 });
            for (int i = 0; i < nch && ok; ++i)
                for (int j = 0; j <= i && ok; ++j)
                {
                    std::complex<double> acc = g[(size_t) (i * (i + 1) / 2 + j)];
                    for (int q = 0; q < j; ++q)
                        acc -= L[(size_t) (i * (i + 1) / 2 + q)] * std::conj (L[(size_t) (j * (j + 1) / 2 + q)]);
                    if (i == j)
                    {
                        ok = acc.real() > 1e-9;
                        if (ok) L[(size_t) (i * (i + 3) / 2)] = std::sqrt (acc.real());
                    }
                    else
                        L[(size_t) (i * (i + 1) / 2 + j)] = acc / L[(size_t) (j * (j + 3) / 2)].real();
                }

            // rows of unit power (the ridge made them a little larger), or independent channels
            const double unit = 1.0 / std::sqrt (1.0 + kCoherenceRidge);
            for (int i = 0; i < nch; ++i)
                for (int j = 0; j <= i; ++j)
                    src.binMix[(size_t) k * 10 + (size_t) (i * (i + 1) / 2 + j)]
                        = ok ? std::complex<float> (L[(size_t) (i * (i + 1) / 2 + j)] * unit)
                             : std::complex<float> (i == j ? 1.0f : 0.0f, 0.0f);
        }
    }

    const int kLo = (int) std::ceil (kSmoothLowHz * stft.N / sr);
    const int kHi = std::min (bins - 2, (int) std::floor (std::min (kSmoothHighHz, (float) (0.45 * sr)) * stft.N / sr));
    std::vector<double> prefix ((size_t) bins + 1, 0.0);
    std::vector<int> prefixCount ((size_t) bins + 1, 0);
    for (int k = 0; k < bins; ++k)
    {
        const bool use = k >= kLo && k <= kHi && levelCount[(size_t) k] >= 4;
        prefix[(size_t) k + 1] = prefix[(size_t) k] + (use ? levelSum[(size_t) k] / levelCount[(size_t) k] : 0.0);
        prefixCount[(size_t) k + 1] = prefixCount[(size_t) k] + (use ? 1 : 0);
    }
    src.binColourDb.assign ((size_t) bins, 0.0f);
    for (int k = kLo; k <= kHi; ++k)
    {
        if (levelCount[(size_t) k] < 4) continue;
        const int w = std::max (2, (int) (k * 0.1225)); // 2^(1/6) - 1
        const int a = std::max (kLo, k - w), b = std::min (kHi, k + w);
        const int n = prefixCount[(size_t) b + 1] - prefixCount[(size_t) a];
        if (n < 3) continue;
        const double mean = (prefix[(size_t) b + 1] - prefix[(size_t) a]) / n;
        src.binColourDb[(size_t) k] = (float) (levelSum[(size_t) k] / levelCount[(size_t) k] - mean);
    }
}

void limitDeepBass (SourceIR& src)
{
    if (src.bandFreq.empty()) return;
    auto rtAt = [&] (float f) { return interpLogFreq (src.bandFreq, src.bandRT, f); };
    src.deepBassRT = std::sqrt (rtAt (125.0f) * rtAt (160.0f));

    // The limited decay line starts where the recording starts: the first part of the late reverb still follows the
    // M7, the overlong tail comes later. Median over the first 30 dB of the limited decay.
    const double frameSeconds = (double) (stftSizeFor (src.sampleRate) / 4) / src.sampleRate;
    src.deepBassIntercept.assign (src.bandFreq.size(), 0.0f);
    for (size_t b = 0; b < src.bandFreq.size(); ++b)
    {
        if (deepBassWeight (src.bandFreq[b]) <= 0.0f || b >= src.bandLevelDb.size()) continue;
        const float rl = limitedRT (src, src.bandRT[b], src.bandFreq[b]);
        const auto& level = src.bandLevelDb[b];
        std::vector<float> icept;
        for (size_t m = 0; m < level.size(); ++m)
        {
            const double tau = m * frameSeconds - src.onset / src.sampleRate;
            if (tau >= kLateFitStart && tau <= kLateFitStart + 0.5 * rl)
                icept.push_back (level[m] + (float) (60.0 * tau / rl));
        }
        if (icept.empty()) { src.deepBassIntercept[b] = src.bandIntercept[b]; continue; }
        std::nth_element (icept.begin(), icept.begin() + (long) icept.size() / 2, icept.end());
        src.deepBassIntercept[b] = icept[icept.size() / 2];
    }
}

float estimateRT (const std::vector<const std::vector<float>*>& channels, double sr, float lowHz, float highHz)
{
    if (channels.empty() || channels.front()->empty()) return NAN;
    if (highHz <= 0.0f) highHz = (float) (0.48 * sr);
    if (lowHz <= 0.0f) lowHz = 20.0f;

    std::array<std::vector<float>, 4> tmp;
    for (size_t i = 0; i < channels.size() && i < 4; ++i)
        tmp[i] = *channels[i];
    const int onset = detectOnset (tmp, sr);

    auto be = computeBandEnergies (channels, sr, { { lowHz, highHz } });
    return fitRT (be.energy[0], (int) std::lround ((double) onset / be.hop), be.hop / sr);
}

//==============================================================================
std::shared_ptr<SourceIR> makeSourceIR (std::array<std::vector<float>, 4> channels, bool trueStereo,
                                        double sourceRate, double hostRate, const String& name)
{
    auto src = std::make_shared<SourceIR>();
    src->name = name;
    src->fileRate = roundToInt (sourceRate);
    src->sampleRate = hostRate;
    src->trueStereo = trueStereo;

    if (! trueStereo)
    {
        channels[1].clear();
        channels[2].clear();
        if (channels[3].empty()) channels[3] = channels[0];
    }

    size_t maxLen = 0;
    for (auto& c : channels)
        maxLen = std::max (maxLen, c.size());

    // trim trailing silence (-120 dB re. peak) to save convolution time
    float peak = 0.0f;
    for (auto& c : channels) for (float v : c) peak = std::max (peak, std::abs (v));
    size_t used = 0;
    for (auto& c : channels)
        for (size_t i = c.size(); i-- > 0;)
            if (std::abs (c[i]) > peak * 1e-6f) { used = std::max (used, i + 1); break; }
    maxLen = std::min (maxLen, std::max<size_t> (used, 64));

    const int numUsed = trueStereo ? 4 : 2;
    const int idx[4] = { 0, 3, 1, 2 };
    for (int i = 0; i < numUsed; ++i)
    {
        auto& c = channels[(size_t) idx[i]];
        c.resize (maxLen, 0.0f);
        src->ch[(size_t) idx[i]] = resample (c, sourceRate, hostRate);
    }

    // equal loudness for all presets: average output power for white noise input. The sum of squares of the
    // same response grows with the rate it is sampled at; scaled to kNormRate the level is the same at every rate.
    double power = 0.0;
    for (auto* c : activeChannels (src->ch, trueStereo))
        for (float v : *c) power += (double) v * v;
    power *= 0.5 * hostRate / kNormRate;
    if (power > 1e-20)
    {
        const float g = (float) std::sqrt (kNormTargetPower / power);
        for (auto& c : src->ch) for (auto& v : c) v *= g;
    }

    analyseSourceIR (*src);
    return src;
}

std::shared_ptr<SourceIR> loadSourceIR (const IRPreset& preset, double hostRate, String& error)
{
    const auto variant = preset.bestVariantFor (hostRate).second;
    AudioFormatManager fm;
    fm.registerBasicFormats();

    auto readFile = [&] (const File& f, AudioBuffer<float>& buf, double& sr) -> bool {
        std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            error = "Cannot read " + f.getFileName();
            return false;
        }
        const auto maxSamples = (int64) (kMaxIRSeconds * reader->sampleRate);
        buf.setSize ((int) reader->numChannels, (int) std::min (reader->lengthInSamples, maxSamples));
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        sr = reader->sampleRate;
        return true;
    };
    auto chan = [] (const AudioBuffer<float>& b, int c) {
        c = std::min (c, b.getNumChannels() - 1);
        return std::vector<float> (b.getReadPointer (c), b.getReadPointer (c) + b.getNumSamples());
    };

    std::array<std::vector<float>, 4> ch;
    bool trueStereo = false;
    double sr = 0.0;
    AudioBuffer<float> a, b;

    if (variant.quad != File() || variant.single != File())
    {
        const auto& f = variant.quad != File() ? variant.quad : variant.single;
        if (! readFile (f, a, sr)) return nullptr;
        if (a.getNumChannels() >= 4)
        {
            for (int c = 0; c < 4; ++c) ch[(size_t) c] = chan (a, c);
            trueStereo = true;
        }
        else
        {
            ch[0] = chan (a, 0);
            ch[3] = chan (a, 1);
        }
    }
    else if (variant.left != File() && variant.right != File())
    {
        double sr2 = 0.0;
        if (! readFile (variant.left, a, sr) || ! readFile (variant.right, b, sr2)) return nullptr;
        ch[0] = chan (a, 0); ch[1] = chan (a, 1);
        ch[2] = chan (b, 0); ch[3] = chan (b, 1);
        trueStereo = a.getNumChannels() >= 2 && b.getNumChannels() >= 2;
        if (! trueStereo) { ch[1].clear(); ch[2].clear(); ch[3] = chan (b, 0); }
    }
    else if (variant.left != File() || variant.right != File())
    {
        if (! readFile (variant.left != File() ? variant.left : variant.right, a, sr)) return nullptr;
        ch[0] = chan (a, 0);
        ch[3] = chan (a, 1);
    }
    else
    {
        error = "No files for " + preset.name;
        return nullptr;
    }

    auto src = makeSourceIR (std::move (ch), trueStereo, sr, hostRate, preset.name);
    src->presetId = preset.id;
    if (preset.category != "User")
        limitDeepBass (*src);
    return src;
}

//==============================================================================
std::shared_ptr<BuiltIR> designIR (const SourceIR& src, const SourceIR* pattern, const DesignParams& p)
{
    auto out = std::make_shared<BuiltIR>();
    const double sr = src.sampleRate;
    const int len = src.length();
    out->sampleRate = sr;
    out->trueStereo = src.trueStereo;
    out->presetId = src.presetId;
    out->name = src.name;
    out->nativeRT = src.nativeRT;
    out->targetRT = p.decay;
    const int shift = (int) std::lround (p.offsetMs * 0.001 * src.sampleRate);
    out->onsetMs = (float) (1000.0 * std::max (0, src.onset + shift) / src.sampleRate);
    if (len <= 0) return out;

    const std::vector<int> chans = src.trueStereo ? std::vector<int> { 0, 1, 2, 3 } : std::vector<int> { 0, 3 };

    // ---- early / late crossover
    const float splitSec = jlimit (0.015f, kSplitMaxSeconds, 0.25f * src.nativeRT);
    out->splitSeconds = splitSec;
    const int fade = std::max (8, (int) (kSplitFadeSeconds * sr));
    const int splitA = src.onset + (int) (splitSec * sr) - fade / 2;
    const int splitB = splitA + fade;
    auto earlyWeight = [&] (int n) -> float {
        if (n < splitA) return 1.0f;
        if (n >= splitB) return 0.0f;
        return (float) (0.5 + 0.5 * std::cos (kPi * (n - splitA) / (double) fade));
    };

    // ---- decay model of every STFT bin
    // Each bin keeps its own reverb time (a ringing frequency stays relatively longer); DECAY scales it.
    const float scale = p.decay / std::max (0.01f, src.nativeRT);
    const DecayScaling decayScaling (src, p.decay);
    const bool vlfOff = p.vlfDb <= kVlfOffDb + 0.01f;
    bool deepBassLimited = false; // the Bricasti library's deep bass is taken back at every DECAY, the native one too
    for (size_t b = 0; b < src.bandFreq.size(); ++b)
        deepBassLimited = deepBassLimited || limitedRT (src, src.bandRT[b], src.bandFreq[b]) < 0.995f * src.bandRT[b];
    const bool neutralLate = std::abs (scale - 1.0f) < 0.005f && std::abs (p.lowMult - 1.0f) < 0.005f
                          && std::abs (p.highMult - 1.0f) < 0.005f && p.vlfDb >= -0.05f
                          && p.lateRolloffHz >= kRolloffBypassHz && p.smooth < 0.005f && ! deepBassLimited;

    Stft stft (stftSizeFor (sr));
    const int N = stft.N, hop = stft.hop, bins = stft.bins;
    auto frameTime = [&] (int m) { return (float) ((m * hop - src.onset) / sr); };
    std::vector<float> re ((size_t) bins), im ((size_t) bins);
    std::vector<float> rtNat ((size_t) bins), rtTgt ((size_t) bins), slopeDb ((size_t) bins);
    const bool perBin = (int) src.binRT.size() == bins;
    bool shorter = false;
    for (int k = 0; k < bins && ! neutralLate; ++k)
    {
        const float f = std::max (1.0f, (float) (k * sr / N));
        const float rn = perBin ? src.binRT[(size_t) k] : interpLogFreq (src.bandFreq, src.bandRT, f);
        // the deep bass of the Bricasti library: its first part already decays like the M7, the overlong tail
        // is cut by the limit line below (deepBassDb), so the slope starts from the limited reverb time
        const float rl = limitedRT (src, rn, f);
        const float rt = jlimit (0.05f, 120.0f, decayScaling (rl, f) * decayMultiplier (f, p));
        rtNat[(size_t) k] = rn;
        rtTgt[(size_t) k] = rt;
        slopeDb[(size_t) k] = 60.0f * (1.0f / rl - 1.0f / rt);
        shorter = shorter || (k > 0 && slopeDb[(size_t) k] < -0.01f);
    }

    // DECAY and level, as on the M7: the early reflections (a block of their own) keep their level, the late reverb
    // starts a little lower the longer it rings. In the LiquidSonics captures (both blocks at fixed levels) its start
    // falls by kLateLevelSlope dB per dB of 10 log10 of the reverb time, so its energy grows with RT^0.45: twice the
    // decay is 1.35 dB louder (a start that stays would be 3 dB, the same energy 0 dB). Across presets, where the
    // size changes with the decay too, so it is an estimate. Inside the early part the change fades in from the first
    // reflection to the crossover, the late reverb there starts together with the reflections.
    const float lateLevelDb = neutralLate ? 0.0f : -kLateLevelSlope * 10.0f * std::log10 (std::max (0.01f, scale));
    const int rampLength = std::max (1, splitA - src.onset);

    // ---- early part (own or from the selected pattern)
    std::array<std::vector<float>, 4> early;
    for (int c : chans)
    {
        auto& e = early[(size_t) c];
        e.assign ((size_t) std::max (0, splitB), 0.0f);
        const SourceIR& es = pattern != nullptr ? *pattern : src;
        const int shift = pattern != nullptr ? pattern->onset - src.onset : 0;
        int esChan = c;
        if (! es.trueStereo && (c == 1 || c == 2)) esChan = -1;
        if (esChan >= 0)
        {
            const auto& x = es.ch[(size_t) esChan];
            for (int n = 0; n < (int) e.size(); ++n)
            {
                const int idx = n + shift;
                if (idx >= 0 && idx < (int) x.size())
                    e[(size_t) n] = x[(size_t) idx] * earlyWeight (n);
            }
        }

        // A shorter decay is a room that absorbs more: its early reflections fade with the same decay as the
        // late reverb, from the onset on (otherwise they keep their level and the response drops at the
        // crossover). A longer decay leaves them as they are.
        if (shorter && ! e.empty())
        {
            const int el = (int) e.size();
            std::vector<float> y ((size_t) el, 0.0f), norm ((size_t) el, 0.0f);
            for (int m = 0; m < stft.numFrames (el); ++m)
            {
                stft.forward (e.data(), el, m, re.data(), im.data());
                const float t = std::max (0.0f, frameTime (m));
                for (int k = 0; k < bins; ++k)
                {
                    const float g = Decibels::decibelsToGain (std::min (0.0f, slopeDb[(size_t) k] * t), -400.0f);
                    re[(size_t) k] *= g;
                    im[(size_t) k] *= g;
                }
                stft.inverseAccumulate (re.data(), im.data(), m, y.data(), norm.data(), el);
            }
            for (int n = 0; n < el; ++n)
                e[(size_t) n] = norm[(size_t) n] > 1e-3f ? y[(size_t) n] / norm[(size_t) n] : 0.0f;
        }
        if (std::abs (lateLevelDb) > 1e-3f)
            for (int n = std::max (0, src.onset); n < (int) e.size(); ++n)
                e[(size_t) n] *= Decibels::decibelsToGain (lateLevelDb * std::min (1.0f, (float) (n - src.onset) / (float) rampLength));
        lowPassInPlace (e, sr, p.erRolloffHz);
    }

    // ---- late part
    std::array<std::vector<float>, 4> late;
    if (neutralLate)
    {
        for (int c : chans)
        {
            late[(size_t) c].assign ((size_t) len, 0.0f);
            for (int n = std::max (0, splitA); n < len; ++n)
                late[(size_t) c][(size_t) n] = src.ch[(size_t) c][(size_t) n] * (1.0f - earlyWeight (n));
        }
    }
    else
    {
        std::vector<float> staticAmp ((size_t) bins);
        std::vector<int> validFrame ((size_t) bins);
        const float vlfLin = vlfOff ? 0.0f : Decibels::decibelsToGain (p.vlfDb);
        float tailSeconds = splitSec + 0.05f;
        const int srcFrames = stft.numFrames (len);
        const int lastFullFrame = std::max (1, (len - N / 2) / hop);

        for (int k = 0; k < bins; ++k)
        {
            const float f = std::max (1.0f, (float) (k * sr / N));
            const float wv = 1.0f / (1.0f + std::pow (f / kVlfCrossoverHz, 4.0f));
            float amp = 1.0f + (vlfLin - 1.0f) * wv;
            if (p.lateRolloffHz < kRolloffBypassHz)
                amp /= std::sqrt (1.0f + std::pow (f / p.lateRolloffHz, 4.0f));
            if (p.smooth > 0.0f && (size_t) k < src.binColourDb.size())
                amp *= Decibels::decibelsToGain (-p.smooth * jlimit (-kSmoothBoostDb, kSmoothCutDb, src.binColourDb[(size_t) k]));
            staticAmp[(size_t) k] = amp;

            const float tv = interpLogFreq (src.bandFreq, src.bandValidEnd, f);
            validFrame[(size_t) k] = jlimit (1, lastFullFrame, (int) std::floor ((src.onset + tv * sr) / hop));

            if (f >= 40.0f && f <= 16000.0f)
                tailSeconds = std::max (tailSeconds, rtTgt[(size_t) k]);
        }

        const int lateLen = std::min ((int) (kMaxIRSeconds * sr), src.onset + (int) (tailSeconds * sr) + N);
        const int outFrames = stft.numFrames (lateLen);
        const int firstFrame = std::max (0, (splitA - N / 2) / hop - 1);

        // per band and frame: largest gain that keeps the level at most kCapMarginDb above the target line
        const size_t numBands = src.bandFreq.size();
        std::vector<std::pair<int, float>> binBand ((size_t) bins, { 0, 0.0f });
        for (int k = 0; k < bins; ++k)
        {
            const float f = std::max (1.0f, (float) (k * sr / N));
            auto it = std::upper_bound (src.bandFreq.begin(), src.bandFreq.end(), f);
            const int i1 = jlimit (0, (int) numBands - 1, (int) (it - src.bandFreq.begin()));
            const int i0 = std::max (0, i1 - 1);
            float t = 0.0f;
            if (i1 != i0 && f > src.bandFreq[(size_t) i0])
                t = jlimit (0.0f, 1.0f, std::log (f / src.bandFreq[(size_t) i0]) / std::log (src.bandFreq[(size_t) i1] / src.bandFreq[(size_t) i0]));
            binBand[(size_t) k] = { i0, i1 == i0 ? 0.0f : t };
        }
        const int capFrames = numBands > 0 && ! src.bandLevelDb.empty() ? (int) src.bandLevelDb[0].size() : 0;
        std::vector<float> capDb ((size_t) capFrames * numBands, kMaxBoostDb);
        // deep bass of the Bricasti library: per band and frame, the largest gain that keeps the level at most
        // kDeepBassMarginDb above the limited decay line. Its overlong tail has nearly a fixed length, so changing the
        // slope alone would leave most of it.
        std::vector<float> deepBassDb (deepBassLimited ? (size_t) capFrames * numBands : 0, kMaxBoostDb);
        for (size_t b = 0; b < numBands && capFrames > 0; ++b)
        {
            const float f = src.bandFreq[b];
            const float rtT = jlimit (0.05f, 120.0f, decayScaling (limitedRT (src, src.bandRT[b], f), f) * decayMultiplier (f, p));
            const float w = deepBassWeight (f);
            const bool limitBand = ! deepBassDb.empty() && w > 0.0f && b < src.deepBassIntercept.size();
            for (int m = 0; m < capFrames; ++m)
            {
                const float tau = std::max (0.0f, (float) ((m * hop - src.onset) / sr));
                const float target = src.bandIntercept[b] - 60.0f * tau / rtT + kCapMarginDb;
                capDb[(size_t) m * numBands + b] = target - src.bandLevelDb[b][(size_t) m];
                if (limitBand) // fading out above kDeepBassFullHz: more room above the line
                    deepBassDb[(size_t) m * numBands + b] = src.deepBassIntercept[b] - 60.0f * tau / rtT + kDeepBassMarginDb
                                                            + (1.0f - w) * 12.0f - src.bandLevelDb[b][(size_t) m];
            }
        }

        // Smooth: per band and frame, pull the measured level towards the fitted decay line (bumps, late echoes)
        std::vector<float> smoothDb;
        if (p.smooth > 0.0f && capFrames > 0)
        {
            smoothDb.assign ((size_t) capFrames * numBands, 0.0f);
            for (size_t b = 0; b < numBands; ++b)
            {
                const int vb = jlimit (0, capFrames - 1, (int) std::floor ((src.onset + src.bandValidEnd[b] * sr) / hop));
                for (int m = 0; m <= vb; ++m)
                {
                    const float tau = (float) ((m * hop - src.onset) / sr);
                    const float ramp = jlimit (0.0f, 1.0f, (tau - splitSec) / kSmoothRampSeconds);
                    const float line = src.bandIntercept[b] - 60.0f * tau / src.bandRT[b];
                    smoothDb[(size_t) m * numBands + b] = p.smooth * ramp
                        * jlimit (-kSmoothCutDb, kSmoothBoostDb, line - src.bandLevelDb[b][(size_t) m]);
                }
            }
        }

        auto gainAt = [&] (int m, int k) {
            float db = std::min (kMaxBoostDb, slopeDb[(size_t) k] * std::max (0.0f, frameTime (m)));
            if (! capDb.empty() && m < capFrames)
            {
                const auto& bw = binBand[(size_t) k];
                const size_t i0 = (size_t) m * numBands + (size_t) bw.first;
                const size_t i1 = (size_t) m * numBands + (size_t) std::min (bw.first + 1, (int) numBands - 1);
                if (! smoothDb.empty())
                    db += smoothDb[i0] * (1.0f - bw.second) + smoothDb[i1] * bw.second;
                // never lift the measured band level above the target decay line (noise floor, fades)
                const float cap = capDb[i0] * (1.0f - bw.second) + capDb[i1] * bw.second;
                db = std::min (db, std::max (cap, std::min (db, 0.0f)));
                if (! deepBassDb.empty())
                    db = std::min (db, deepBassDb[i0] * (1.0f - bw.second) + deepBassDb[i1] * bw.second);
            }
            return Decibels::decibelsToGain (db, -400.0f) * staticAmp[(size_t) k];
        };

        // The synthesised continuation starts where each bin's measured data ends, on the bin's own decay line
        // (fitted over the whole late range, so neither the last noisy frames nor the neighbours set its level)
        // with the modification of that frame, and carries on with the target decay. Its random phases are
        // mixed per bin so that the channels keep the coherence they have in the recording.
        const bool haveLines = perBin && src.binMix.size() == (size_t) bins * 10;
        jassert (haveLines);
        std::vector<std::complex<float>> phasor ((size_t) kPhaseTableSize);
        for (int i = 0; i < kPhaseTableSize; ++i)
            phasor[(size_t) i] = std::polar (1.0f, MathConstants<float>::twoPi * (float) i / (float) kPhaseTableSize);

        std::vector<float> lateIn ((size_t) len), refAmp ((size_t) bins), refTime ((size_t) bins);
        const float synthGain = std::sqrt ((float) N / (float) hop); // incoherent overlap-add compensation

        for (size_t ci = 0; ci < chans.size(); ++ci)
        {
            const int c = chans[ci];
            for (int n = 0; n < len; ++n)
                lateIn[(size_t) n] = src.ch[(size_t) c][(size_t) n] * (1.0f - earlyWeight (n));

            // analysis of the measured late part
            const int storedFrames = std::max (0, srcFrames - firstFrame);
            std::vector<float> Xre ((size_t) storedFrames * (size_t) bins), Xim ((size_t) storedFrames * (size_t) bins);
            for (int m = firstFrame; m < srcFrames; ++m)
            {
                const size_t off = (size_t) (m - firstFrame) * (size_t) bins;
                stft.forward (lateIn.data(), len, m, Xre.data() + off, Xim.data() + off);
            }

            for (int k = 0; k < bins; ++k)
            {
                const int mv = validFrame[(size_t) k];
                const float tv = frameTime (mv);
                const float power = haveLines ? src.binPower[(size_t) c][(size_t) k] : 0.0f;
                refAmp[(size_t) k] = std::sqrt (power * std::pow (10.0f, std::max (-30.0f, -6.0f * tv / rtNat[(size_t) k])))
                                   * gainAt (mv, k);
                refTime[(size_t) k] = tv;
            }

            // resynthesis
            std::vector<float> y ((size_t) lateLen, 0.0f), norm ((size_t) lateLen, 0.0f);
            for (int m = firstFrame; m < outFrames; ++m)
            {
                const float t = frameTime (m);
                const bool haveMeasured = m < srcFrames;
                const size_t off = haveMeasured ? (size_t) (m - firstFrame) * (size_t) bins : 0;
                Random rng (0x5eed + 7919 * (int64) m); // per frame: the same draws for every channel

                for (int k = 0; k < bins; ++k)
                {
                    const int mv = validFrame[(size_t) k];
                    float yr = 0.0f, yi = 0.0f;
                    float measuredWeight = 0.0f, synthWeight = 1.0f;
                    if (haveMeasured && m <= mv)
                    {
                        const int fadeStart = mv - kCrossfadeFrames;
                        const float alpha = m <= fadeStart ? 0.0f : (float) (m - fadeStart) / kCrossfadeFrames;
                        measuredWeight = std::sqrt (1.0f - alpha);
                        synthWeight = std::sqrt (alpha);
                    }
                    if (measuredWeight > 0.0f)
                    {
                        const float g = gainAt (m, k) * measuredWeight;
                        yr += Xre[off + (size_t) k] * g;
                        yi += Xim[off + (size_t) k] * g;
                    }
                    if (synthWeight > 0.0f && k > 0 && k < bins - 1)
                    {
                        const float a = synthGain * synthWeight * refAmp[(size_t) k]
                                      * std::pow (10.0f, -3.0f * (t - refTime[(size_t) k]) / rtTgt[(size_t) k]);
                        std::complex<float> z;
                        const size_t row = (size_t) k * 10 + ci * (ci + 1) / 2;
                        for (size_t j = 0; j < chans.size(); ++j)
                        {
                            const auto& ph = phasor[(size_t) rng.nextInt (kPhaseTableSize)];
                            if (j <= ci) z += (haveLines ? src.binMix[row + j] : std::complex<float> (j == ci ? 1.0f : 0.0f)) * ph;
                        }
                        yr += a * z.real();
                        yi += a * z.imag();
                    }
                    re[(size_t) k] = yr;
                    im[(size_t) k] = yi;
                }
                stft.inverseAccumulate (re.data(), im.data(), m, y.data(), norm.data(), lateLen);
            }
            for (int n = 0; n < lateLen; ++n)
                y[(size_t) n] = norm[(size_t) n] > 1e-3f ? y[(size_t) n] / norm[(size_t) n] : 0.0f;

            // gentle fade over the last 10 % (the level there is already ~ -54 dB)
            const int fadeLen = std::max (1, lateLen / 10);
            for (int n = 0; n < fadeLen; ++n)
                y[(size_t) (lateLen - 1 - n)] *= (float) (0.5 - 0.5 * std::cos (kPi * n / fadeLen));

            late[(size_t) c] = std::move (y);
        }
    }

    // ---- assemble
    const int pd = (int) std::lround (p.predelayMs * 0.001 * sr);
    const bool delayOn = p.delayDb > kDelayOffDb + 0.01f;
    const int dd = (int) std::lround (p.delayMs * 0.001 * sr);
    const int voices = delayOn ? 3 : 0;
    const float gE = p.earlyDb <= kLevelOffDb + 0.01f ? 0.0f : Decibels::decibelsToGain (p.earlyDb);
    const float gL = p.lateDb <= kLevelOffDb + 0.01f ? 0.0f : Decibels::decibelsToGain (p.lateDb + lateLevelDb);
    const float gD = delayOn ? Decibels::decibelsToGain (p.delayDb) : 0.0f;

    const int lateLen = (int) late[(size_t) chans.front()].size();
    const int maxLen = (int) (kMaxIRSeconds * sr);
    int total = std::max ((int) early[(size_t) chans.front()].size(), pd + lateLen + voices * dd);
    // offset: leading silence, or the start is cut (keeping at least one sample)
    const int pad = std::max (0, shift);
    const int cut = std::min (std::max (0, -shift), total - 1);
    const bool capped = total - cut + pad > maxLen;
    total = std::min (total, maxLen + cut - pad);
    const int fadeIn = std::min (total - cut, std::max (8, (int) (kOffsetFadeSeconds * sr)));

    // early part plus late reverb with its delay voices, then the offset. Linear, so the early part and the
    // late reverb assembled on their own add up to the whole response.
    auto assemble = [&, total] (std::vector<float>& o, const std::vector<float>& e, float earlyGain,
                                const std::vector<float>& l, float lateGain)
    {
        o.assign ((size_t) total, 0.0f);
        for (int n = 0; n < std::min (total, (int) e.size()); ++n)
            o[(size_t) n] = e[(size_t) n] * earlyGain;

        float g = lateGain;
        for (int v = 0; v <= voices; ++v)
        {
            const int offset = pd + v * dd;
            for (int n = 0; n < (int) l.size() && n + offset < total; ++n)
                o[(size_t) (n + offset)] += l[(size_t) n] * g;
            g *= gD;
        }
        if (capped)
        {
            const int fadeLen = total / 20;
            for (int n = 0; n < fadeLen; ++n)
                o[(size_t) (total - 1 - n)] *= (float) n / fadeLen;
        }

        if (cut > 0)
        {
            o.erase (o.begin(), o.begin() + cut);
            for (int n = 0; n < fadeIn; ++n)
                o[(size_t) n] *= (float) (0.5 - 0.5 * std::cos (kPi * n / fadeIn));
        }
        o.insert (o.begin(), (size_t) pad, 0.0f);
    };
    for (int c : chans)
        assemble (out->ch[(size_t) c], early[(size_t) c], gE, late[(size_t) c], gL);
    total = total - cut + pad;

    // mid band energy for the distance mode (the level of the reverb against the direct sound)
    {
        auto midBandEnergy = [&] (const std::vector<float>& x) {
            std::array<Biquad, 4> f;
            f[0].highPass (sr, 500.0, 0.7071); f[1].highPass (sr, 500.0, 0.7071);
            f[2].lowPass (sr, 2000.0, 0.7071); f[3].lowPass (sr, 2000.0, 0.7071);
            double e = 0.0;
            for (float v : x)
            {
                for (auto& b : f) v = b.process (v, 0);
                e += (double) v * v;
            }
            // filter ringing after the end of the response
            for (int n = 0; n < (int) (0.05 * sr); ++n)
            {
                float v = 0.0f;
                for (auto& b : f) v = b.process (v, 0);
                e += (double) v * v;
            }
            return e;
        };
        std::vector<float> impulse ((size_t) (0.05 * sr), 0.0f);
        impulse[0] = 1.0f;
        const double ref = midBandEnergy (impulse);
        std::array<double, 4> e {};
        for (int c : chans)
            e[(size_t) c] = midBandEnergy (out->ch[(size_t) c]) / ref;
        // each output channel receives two paths (L->L + R->L, L->R + R->R)
        out->midEnergy = (float) ((e[0] + e[1] + e[2] + e[3]) * 0.5);
        out->midEnergyDiagonal = (float) ((e[0] + e[3]) * 0.5);
    }
    out->seconds = (float) (total / sr);

    // ---- display data
    const int points = 400;
    auto builtChans = activeChannels (out->ch, out->trueStereo);
    auto srcChans = activeChannels (src.ch, src.trueStereo);
    const int span = std::max (total, len);
    const double refPower = maxSegmentPower (srcChans, points, span);
    out->envelopeDb = envelopeDb (builtChans, points, span, refPower);
    out->nativeEnvelopeDb = envelopeDb (srcChans, points, span, refPower);
    out->seconds = (float) (span / sr);
    out->rtFreqs = src.bandFreq;
    out->rtNative = src.bandRT;
    out->rtTarget.resize (src.bandRT.size());
    for (size_t b = 0; b < src.bandRT.size(); ++b)
        out->rtTarget[b] = jlimit (0.05f, 120.0f, decayScaling (limitedRT (src, src.bandRT[b], src.bandFreq[b]), src.bandFreq[b])
                                                  * decayMultiplier (src.bandFreq[b], p));

    // ---- modulation: early part, late reverb and its variant as responses of their own
    if (p.splitLate)
    {
        std::array<std::vector<float>, 4> variantLate;
        if (p.lateVariant)
            variantLate = makeVariant (late, chans, sr);
        const std::vector<float> none;
        for (int c : chans)
        {
            assemble (out->late[(size_t) c], none, 0.0f, late[(size_t) c], gL);
            if (p.lateVariant)
                assemble (out->variant[(size_t) c], none, 0.0f, variantLate[(size_t) c], gL);
            assemble (out->ch[(size_t) c], early[(size_t) c], gE, none, 0.0f);
        }

        // the early part ends long before the late reverb: drop its trailing zeros (saves convolution)
        int end = 1;
        for (int c : chans)
        {
            const auto& e = out->ch[(size_t) c];
            for (int n = (int) e.size() - 1; n >= end; --n)
                if (e[(size_t) n] != 0.0f) { end = n + 1; break; }
        }
        for (int c : chans)
            out->ch[(size_t) c].resize ((size_t) end);
    }

    return out;
}
} // namespace concert7
