// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Wet signal processing after the convolution: 5 band post EQ, modulation and ducker.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <vector>

namespace concert7
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, 2> z1 {}, z2 {};

    inline float process (float x, int ch) noexcept
    {
        const double y = b0 * x + z1[(size_t) ch];
        z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
        z2[(size_t) ch] = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1 = {}; z2 = {}; }
    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    double magnitude (double freq, double sampleRate) const;

    // RBJ audio EQ cookbook designs
    void lowPass (double sr, double f, double q);
    void highPass (double sr, double f, double q);
    void peak (double sr, double f, double q, double gainDb);
    void lowShelf (double sr, double f, double q, double gainDb);
    void highShelf (double sr, double f, double q, double gainDb);
};

struct EQSettings
{
    bool on = true;
    bool hpOn = false;  float hpFreq = 80.0f;
    int lowType = 0;    float lowFreq = 200.0f,  lowGain = 0.0f,  lowQ = 0.71f;  // 0 = shelf, 1 = bell
                        float midFreq = 1000.0f, midGain = 0.0f,  midQ = 1.0f;
    int highType = 0;   float highFreq = 6000.0f, highGain = 0.0f, highQ = 0.71f;
    bool lpOn = false;  float lpFreq = 12000.0f;

    bool operator== (const EQSettings& o) const;
    bool operator!= (const EQSettings& o) const { return ! (*this == o); }
};

/** High pass (12 dB/oct), low shelf/bell, mid bell, high shelf/bell, high cut (12 dB/oct). */
class PostEQ
{
public:
    static constexpr int kNumBands = 5;

    void prepare (double sampleRate);
    void reset();
    /** Audio thread; recomputes coefficients only when the settings changed. */
    void setSettings (const EQSettings& s);
    void process (float* l, float* r, int n) noexcept;

    /** Response of the whole EQ in dB (for the user interface). */
    static double responseDb (const EQSettings& s, double sampleRate, double freq);
    static void design (const EQSettings& s, double sampleRate, std::array<Biquad, kNumBands>& out,
                        std::array<bool, kNumBands>& active);

private:
    double sampleRate = 44100.0;
    EQSettings settings;
    bool initialised = false;
    std::array<Biquad, kNumBands> bands;
    std::array<bool, kNumBands> active {};
};

/** Slow pitch modulation of the reverb (vibrato, no dry path so no comb filtering). */
class Modulator
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* l, float* r, int n, float depthPercent, float rateHz) noexcept;

private:
    static constexpr double kMaxDelayMs = 4.0;
    double sampleRate = 44100.0;
    std::vector<float> bufL, bufR;
    int writePos = 0;
    double phase = 0.0;
    juce::SmoothedValue<float> depth;
};

/** Reduces the wet signal while the key signal (input or sidechain) is above the threshold. */
class Ducker
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (const float* keyL, const float* keyR, float* l, float* r, int n,
                  float amountDb, float thresholdDb, float releaseMs) noexcept;
    float getGainReductionDb() const { return reductionDb.load(); }

private:
    double sampleRate = 44100.0;
    float env = 0.0f, gainState = 1.0f;
    std::atomic<float> reductionDb { 0.0f };
};

/** Peak meter value holder shared with the editor. */
struct PeakMeter
{
    std::atomic<float> peak { 0.0f };
    void push (const float* data, int n) noexcept
    {
        float m = 0.0f;
        for (int i = 0; i < n; ++i) m = std::max (m, std::abs (data[i]));
        float cur = peak.load (std::memory_order_relaxed);
        while (m > cur && ! peak.compare_exchange_weak (cur, m)) {}
    }
    float take() noexcept { return peak.exchange (0.0f); }
};
} // namespace concert7
