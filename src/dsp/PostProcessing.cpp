// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "PostProcessing.h"
#include <complex>

namespace concert7
{
using namespace juce;

namespace
{
    constexpr double kPi = MathConstants<double>::pi;

    /** 4 point Hermite interpolation in a ring buffer, delay >= 1 sample (0 = newest sample). */
    float hermiteRead (const std::vector<float>& buf, int writePos, double delay) noexcept
    {
        const int size = (int) buf.size();
        const double pos = (double) writePos - delay;
        const int i1 = (int) std::floor (pos);
        const float t = (float) (pos - i1);
        auto at = [&] (int i) { return buf[(size_t) ((i % size + size) % size)]; };
        const float xm1 = at (i1 - 1), x0 = at (i1), x1 = at (i1 + 1), x2 = at (i1 + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    struct Proto { double w0, cw, sw, alpha, A; };
    Proto proto (double sr, double f, double q, double gainDb = 0.0)
    {
        f = jlimit (5.0, 0.49 * sr, f);
        Proto p;
        p.w0 = 2.0 * kPi * f / sr;
        p.cw = std::cos (p.w0);
        p.sw = std::sin (p.w0);
        p.alpha = p.sw / (2.0 * std::max (0.05, q));
        p.A = std::pow (10.0, gainDb / 40.0);
        return p;
    }
}

void Biquad::lowPass (double sr, double f, double q)
{
    auto p = proto (sr, f, q);
    const double a0 = 1.0 + p.alpha;
    b0 = (1.0 - p.cw) * 0.5 / a0; b1 = (1.0 - p.cw) / a0; b2 = b0;
    a1 = -2.0 * p.cw / a0; a2 = (1.0 - p.alpha) / a0;
}

void Biquad::highPass (double sr, double f, double q)
{
    auto p = proto (sr, f, q);
    const double a0 = 1.0 + p.alpha;
    b0 = (1.0 + p.cw) * 0.5 / a0; b1 = -(1.0 + p.cw) / a0; b2 = b0;
    a1 = -2.0 * p.cw / a0; a2 = (1.0 - p.alpha) / a0;
}

void Biquad::peak (double sr, double f, double q, double gainDb)
{
    auto p = proto (sr, f, q, gainDb);
    const double a0 = 1.0 + p.alpha / p.A;
    b0 = (1.0 + p.alpha * p.A) / a0; b1 = -2.0 * p.cw / a0; b2 = (1.0 - p.alpha * p.A) / a0;
    a1 = -2.0 * p.cw / a0; a2 = (1.0 - p.alpha / p.A) / a0;
}

void Biquad::lowShelf (double sr, double f, double q, double gainDb)
{
    auto p = proto (sr, f, q, gainDb);
    const double A = p.A, s = 2.0 * std::sqrt (A) * p.alpha;
    const double a0 = (A + 1) + (A - 1) * p.cw + s;
    b0 = A * ((A + 1) - (A - 1) * p.cw + s) / a0;
    b1 = 2 * A * ((A - 1) - (A + 1) * p.cw) / a0;
    b2 = A * ((A + 1) - (A - 1) * p.cw - s) / a0;
    a1 = -2 * ((A - 1) + (A + 1) * p.cw) / a0;
    a2 = ((A + 1) + (A - 1) * p.cw - s) / a0;
}

void Biquad::highShelf (double sr, double f, double q, double gainDb)
{
    auto p = proto (sr, f, q, gainDb);
    const double A = p.A, s = 2.0 * std::sqrt (A) * p.alpha;
    const double a0 = (A + 1) - (A - 1) * p.cw + s;
    b0 = A * ((A + 1) + (A - 1) * p.cw + s) / a0;
    b1 = -2 * A * ((A - 1) + (A + 1) * p.cw) / a0;
    b2 = A * ((A + 1) + (A - 1) * p.cw - s) / a0;
    a1 = 2 * ((A - 1) - (A + 1) * p.cw) / a0;
    a2 = ((A + 1) - (A - 1) * p.cw - s) / a0;
}

double Biquad::magnitude (double freq, double sr) const
{
    const std::complex<double> z = std::polar (1.0, -2.0 * kPi * freq / sr);
    const auto num = b0 + b1 * z + b2 * z * z;
    const auto den = 1.0 + a1 * z + a2 * z * z;
    return std::abs (num / den);
}

//==============================================================================
bool EQSettings::operator== (const EQSettings& o) const
{
    return on == o.on && hpOn == o.hpOn && hpFreq == o.hpFreq && lowType == o.lowType && lowFreq == o.lowFreq
        && lowGain == o.lowGain && lowQ == o.lowQ && midFreq == o.midFreq && midGain == o.midGain && midQ == o.midQ
        && highType == o.highType && highFreq == o.highFreq && highGain == o.highGain && highQ == o.highQ
        && lpOn == o.lpOn && lpFreq == o.lpFreq;
}

void PostEQ::prepare (double sr)
{
    sampleRate = sr;
    initialised = false;
    reset();
}

void PostEQ::reset()
{
    for (auto& b : bands) b.reset();
}

void PostEQ::design (const EQSettings& s, double sr, std::array<Biquad, kNumBands>& out, std::array<bool, kNumBands>& act)
{
    act = {};
    for (auto& b : out) b.setIdentity();
    if (! s.on) return;

    if (s.hpOn) { out[0].highPass (sr, s.hpFreq, 0.7071); act[0] = true; }
    if (std::abs (s.lowGain) > 0.01f)
    {
        if (s.lowType == 0) out[1].lowShelf (sr, s.lowFreq, s.lowQ, s.lowGain);
        else                out[1].peak (sr, s.lowFreq, s.lowQ, s.lowGain);
        act[1] = true;
    }
    if (std::abs (s.midGain) > 0.01f) { out[2].peak (sr, s.midFreq, s.midQ, s.midGain); act[2] = true; }
    if (std::abs (s.highGain) > 0.01f)
    {
        if (s.highType == 0) out[3].highShelf (sr, s.highFreq, s.highQ, s.highGain);
        else                 out[3].peak (sr, s.highFreq, s.highQ, s.highGain);
        act[3] = true;
    }
    if (s.lpOn && s.lpFreq < 0.49 * sr) { out[4].lowPass (sr, s.lpFreq, 0.7071); act[4] = true; }
}

void PostEQ::setSettings (const EQSettings& s)
{
    if (initialised && s == settings) return;

    std::array<Biquad, kNumBands> fresh;
    std::array<bool, kNumBands> freshActive;
    design (s, sampleRate, fresh, freshActive);
    for (int i = 0; i < kNumBands; ++i)
    {
        // keep the filter state so coefficient changes do not click
        fresh[(size_t) i].z1 = bands[(size_t) i].z1;
        fresh[(size_t) i].z2 = bands[(size_t) i].z2;
        if (! active[(size_t) i] && freshActive[(size_t) i]) fresh[(size_t) i].reset();
    }
    bands = fresh;
    active = freshActive;
    settings = s;
    initialised = true;
}

void PostEQ::process (float* l, float* r, int n) noexcept
{
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! active[(size_t) b]) continue;
        auto& bq = bands[(size_t) b];
        for (int i = 0; i < n; ++i)
        {
            l[i] = bq.process (l[i], 0);
            r[i] = bq.process (r[i], 1);
        }
    }
}

double PostEQ::responseDb (const EQSettings& s, double sr, double freq)
{
    std::array<Biquad, kNumBands> b;
    std::array<bool, kNumBands> act;
    design (s, sr, b, act);
    double mag = 1.0;
    for (int i = 0; i < kNumBands; ++i)
        if (act[(size_t) i]) mag *= b[(size_t) i].magnitude (freq, sr);
    return 20.0 * std::log10 (std::max (mag, 1e-9));
}

//==============================================================================
void Modulator::prepare (double sr)
{
    sampleRate = sr;
    const int size = (int) (kMaxDelayMs * 0.001 * sr) + 8;
    bufL.assign ((size_t) size, 0.0f);
    bufR.assign ((size_t) size, 0.0f);
    depth.reset (sr, 0.2);
    depth.setCurrentAndTargetValue (0.0f);
    reset();
}

void Modulator::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    writePos = 0;
    phase = 0.0;
}

void Modulator::process (float* l, float* r, int n, float depthPercent, float rateHz) noexcept
{
    depth.setTargetValue (jlimit (0.0f, 1.0f, depthPercent * 0.01f));
    if (! depth.isSmoothing() && depth.getTargetValue() <= 0.0f)
    {
        // keep the delay line filled so engaging modulation does not click
        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) writePos] = l[i];
            bufR[(size_t) writePos] = r[i];
            writePos = (writePos + 1) % (int) bufL.size();
        }
        return;
    }

    const int size = (int) bufL.size();
    const double maxDelay = 0.5 * kMaxDelayMs * 0.001 * sampleRate; // modulation swing in samples
    const double inc = rateHz / sampleRate;

    for (int i = 0; i < n; ++i)
    {
        bufL[(size_t) writePos] = l[i];
        bufR[(size_t) writePos] = r[i];
        const double d = depth.getNextValue();
        const double sL = 0.5 + 0.5 * std::sin (2.0 * kPi * phase);
        const double sR = 0.5 + 0.5 * std::cos (2.0 * kPi * phase);
        // +1 keeps the read point at least one sample behind the newest sample (needed for Hermite)
        l[i] = hermiteRead (bufL, writePos, 1.0 + d * maxDelay * 2.0 * sL);
        r[i] = hermiteRead (bufR, writePos, 1.0 + d * maxDelay * 2.0 * sR);
        writePos = (writePos + 1) % size;
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
    }
}

//==============================================================================
void RandomLfo::reset (int64 seed)
{
    rng.setSeed (seed);
    from = 0.0f;
    to = rng.nextFloat() * 2.0f - 1.0f;
    pos = 0.0;
}

float RandomLfo::next (double increment) noexcept
{
    pos += increment;
    if (pos >= 1.0)
    {
        pos -= std::floor (pos);
        from = to;
        to = rng.nextFloat() * 2.0f - 1.0f;
    }
    const float t = (float) pos;
    return from + (to - from) * t * t * (3.0f - 2.0f * t);
}

//==============================================================================
void TailModulator::prepare (double sr)
{
    sampleRate = sr;
    const int size = (int) (kChorusMaxDelayMs * 0.001 * sr) + 8;
    bufL.assign ((size_t) size, 0.0f);
    bufR.assign ((size_t) size, 0.0f);
    driftDepth.reset (sr, 0.3);
    chorusDepth.reset (sr, 0.2);
    chorusMix.reset (sr, 0.05);
    driftDepth.setCurrentAndTargetValue (0.0f);
    chorusDepth.setCurrentAndTargetValue (0.0f);
    chorusMix.setCurrentAndTargetValue (0.0f);
    reset();
}

void TailModulator::reset()
{
    // fixed seeds: a bounce sounds the same every time
    drift.reset (0x0d21f7);
    chorusL.reset (0x0c405);
    chorusR.reset (0x0c406);
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    writePos = 0;
}

void TailModulator::process (float* l, float* r, const float* lateL, const float* lateR, const float* varL,
                             const float* varR, int n, float driftPercent, float chorusPercent, float rateHz) noexcept
{
    driftDepth.setTargetValue (jlimit (0.0f, 1.0f, driftPercent * 0.01f));
    chorusDepth.setTargetValue (jlimit (0.0f, 1.0f, chorusPercent * 0.01f));
    chorusMix.setTargetValue (chorusPercent > 0.0f ? 1.0f : 0.0f);

    const bool driftOn = driftDepth.isSmoothing() || driftDepth.getTargetValue() > 0.0f;
    const bool chorusOn = chorusMix.isSmoothing() || chorusMix.getTargetValue() > 0.0f;
    const int size = (int) bufL.size();
    const double inc = rateHz / sampleRate;
    const double swing = kChorusMaxDelayMs * 0.001 * sampleRate;

    for (int i = 0; i < n; ++i)
    {
        float tl = lateL[i], tr = lateR[i];
        if (driftOn)
        {
            // rotate between the late reverb and its variant: up to +-180 degrees at 100 %
            const float angle = MathConstants<float>::pi * driftDepth.getNextValue() * drift.next (inc);
            const float c = std::cos (angle), s = std::sin (angle);
            tl = c * tl + s * varL[i];
            tr = c * tr + s * varR[i];
        }

        // the delay line always runs so the chorus can fade in without a jump
        bufL[(size_t) writePos] = tl;
        bufR[(size_t) writePos] = tr;
        if (chorusOn)
        {
            const double d = chorusDepth.getNextValue() * swing;
            const float mix = chorusMix.getNextValue();
            // +1 keeps the read point at least one sample behind the newest sample (needed for Hermite)
            const float ml = hermiteRead (bufL, writePos, 1.0 + d * (0.5 + 0.5 * chorusL.next (inc)));
            const float mr = hermiteRead (bufR, writePos, 1.0 + d * (0.5 + 0.5 * chorusR.next (inc)));
            tl += mix * (ml - tl);
            tr += mix * (mr - tr);
        }
        writePos = (writePos + 1) % size;

        l[i] += tl;
        r[i] += tr;
    }
}

//==============================================================================
void Ducker::prepare (double sr)
{
    sampleRate = sr;
    reset();
}

void Ducker::reset()
{
    env = 0.0f;
    gainState = 1.0f;
    reductionDb.store (0.0f);
}

void Ducker::process (const float* keyL, const float* keyR, float* l, float* r, int n,
                      float amountDb, float thresholdDb, float releaseMs) noexcept
{
    if (amountDb < 0.05f && gainState > 0.9999f)
    {
        reductionDb.store (0.0f);
        return;
    }

    const float atk = (float) std::exp (-1.0 / (0.005 * sampleRate));
    const float rel = (float) std::exp (-1.0 / (std::max (1.0f, releaseMs) * 0.001 * sampleRate));
    const float gainAtk = (float) std::exp (-1.0 / (0.01 * sampleRate));

    for (int i = 0; i < n; ++i)
    {
        const float x = std::max (std::abs (keyL[i]), std::abs (keyR[i]));
        env = x > env ? atk * env + (1.0f - atk) * x : rel * env + (1.0f - rel) * x;
        const float envDb = Decibels::gainToDecibels (env, -100.0f);
        const float over = jlimit (0.0f, 1.0f, (envDb - thresholdDb) / 6.0f); // 6 dB soft knee
        const float target = Decibels::decibelsToGain (-amountDb * over);
        const float coeff = target < gainState ? gainAtk : rel;
        gainState = coeff * gainState + (1.0f - coeff) * target;
        l[i] *= gainState;
        r[i] *= gainState;
    }
    reductionDb.store (-Decibels::gainToDecibels (gainState, -100.0f));
}
} // namespace concert7
