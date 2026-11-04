// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Shared helpers for the unit tests and the developer tool.

#pragma once

#include <JuceHeader.h>
#include "dsp/IRDesigner.h"
#include <array>
#include <cmath>
#include <vector>

namespace concert7::test
{
/** Folder with the Samplicity Bricasti IRs (env CONCERT7_IR_DIR or --irs). Empty = library tests are skipped. */
inline juce::File& irFolder()
{
    static juce::File f;
    return f;
}

/** Exponentially decaying white noise with a given RT60 (true stereo, decorrelated channels). */
inline std::array<std::vector<float>, 4> syntheticIR (float rt60, float seconds, double sr, int seed = 1,
                                                      float onsetSeconds = 0.01f)
{
    std::array<std::vector<float>, 4> ch;
    juce::Random rng (seed);
    const int len = (int) (seconds * sr);
    const int onset = (int) (onsetSeconds * sr);
    const double k = std::log (1000.0) / (rt60 * sr); // amplitude: -60 dB after rt60
    for (auto& c : ch)
    {
        c.assign ((size_t) len, 0.0f);
        for (int i = onset; i < len; ++i)
            c[(size_t) i] = (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-k * (i - onset));
        c[(size_t) onset] = 1.0f; // direct sound
    }
    return ch;
}

/** Two-band synthetic IR: low band (< ~400 Hz) decays with rtLow, high band (> ~3 kHz) with rtHigh. */
inline std::array<std::vector<float>, 4> twoBandIR (float rtLow, float rtHigh, float seconds, double sr, int seed = 3)
{
    auto lo = syntheticIR (rtLow, seconds, sr, seed);
    auto hi = syntheticIR (rtHigh, seconds, sr, seed + 17);
    std::array<std::vector<float>, 4> out;
    for (int c = 0; c < 4; ++c)
    {
        auto& a = lo[(size_t) c];
        auto& b = hi[(size_t) c];
        // 4th order low pass at 300 Hz for a, high pass at 4 kHz for b (cascaded RBJ biquads)
        auto biquad = [&] (std::vector<float>& x, bool highPass, double fc) {
            for (int pass = 0; pass < 2; ++pass)
            {
                const double w0 = 2.0 * juce::MathConstants<double>::pi * fc / sr, cw = std::cos (w0);
                const double alpha = std::sin (w0) / (2.0 * 0.7071), a0 = 1.0 + alpha;
                double b0 = highPass ? (1 + cw) / 2 : (1 - cw) / 2, b1 = highPass ? -(1 + cw) : (1 - cw), b2 = b0;
                double a1 = -2 * cw, a2 = 1 - alpha;
                b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
                double z1 = 0, z2 = 0;
                for (auto& v : x)
                {
                    const double y = b0 * v + z1;
                    z1 = b1 * v - a1 * y + z2;
                    z2 = b2 * v - a2 * y;
                    v = (float) y;
                }
            }
        };
        biquad (a, false, 300.0);
        biquad (b, true, 4000.0);
        out[(size_t) c].resize (a.size());
        for (size_t i = 0; i < a.size(); ++i)
            out[(size_t) c][i] = a[i] + b[i];
    }
    return out;
}

inline std::vector<const std::vector<float>*> chans (const std::array<std::vector<float>, 4>& ch, bool trueStereo = true)
{
    if (trueStereo) return { &ch[0], &ch[1], &ch[2], &ch[3] };
    return { &ch[0], &ch[3] };
}

inline double energy (const std::vector<float>& x, int from = 0, int to = -1)
{
    if (to < 0 || to > (int) x.size()) to = (int) x.size();
    double e = 0.0;
    for (int i = std::max (0, from); i < to; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
    return e;
}

inline std::vector<float> directConvolve (const std::vector<float>& x, const std::vector<float>& h, int outLen)
{
    std::vector<float> y ((size_t) outLen, 0.0f);
    for (int n = 0; n < outLen; ++n)
    {
        double acc = 0.0;
        const int kMax = std::min (n, (int) h.size() - 1);
        for (int k = 0; k <= kMax; ++k)
            if (n - k < (int) x.size()) acc += (double) h[(size_t) k] * x[(size_t) (n - k)];
        y[(size_t) n] = (float) acc;
    }
    return y;
}

inline bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& buf, double sr, int bits = 32)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return false;
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                               .withSampleRate (sr)
                                               .withNumChannels (buf.getNumChannels())
                                               .withBitsPerSample (bits)
                                               .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                             : juce::AudioFormatWriterOptions::SampleFormat::integral));
    if (writer == nullptr) return false;
    return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

inline bool readAudio (const juce::File& f, juce::AudioBuffer<float>& buf, double& sr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return false;
    buf.setSize ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&buf, 0, buf.getNumSamples(), 0, true, true);
    sr = r->sampleRate;
    return true;
}

/** Simple play head with a fixed tempo. */
struct FixedPlayHead : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (true);
        return p;
    }
};
} // namespace concert7::test
