// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "Distance.h"
#include <cmath>

namespace concert7
{
using namespace juce;

namespace
{
    constexpr double kPi = MathConstants<double>::pi;

    // near field timbre (see Distance.h)
    constexpr float kPresenceDbPerDoubling = 1.5f;
    constexpr float kPresenceMinDb = -6.0f, kPresenceMaxDb = 3.0f;
    constexpr float kPresenceQ = 0.6f;
    constexpr int kPresenceStep = 64;       // samples between coefficient updates while it moves
}

double airAbsorptionDbPerMetre (double f, double humidity, double temperatureC)
{
    // ISO 9613-1:1993, equations (3) - (5) and annex B
    constexpr double pr = 101.325, pa = 101.325, T0 = 293.15, T01 = 273.16;
    const double T = temperatureC + 273.15;
    const double psat = pr * std::pow (10.0, -6.8346 * std::pow (T01 / T, 1.261) + 4.6151);
    const double h = jlimit (0.0, 100.0, humidity) * (psat / pr) * (pr / pa);
    const double frO = (pa / pr) * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h));
    const double frN = (pa / pr) * std::pow (T / T0, -0.5) * (9.0 + 280.0 * h * std::exp (-4.170 * (std::pow (T / T0, -1.0 / 3.0) - 1.0)));
    const double f2 = f * f;
    return 8.686 * f2 * (1.84e-11 * (pr / pa) * std::sqrt (T / T0)
                         + std::pow (T / T0, -2.5) * (0.01275 * std::exp (-2239.1 / T) / (frO + f2 / frO)
                                                     + 0.1068 * std::exp (-3352.0 / T) / (frN + f2 / frN)));
}

float estimateHallVolume (float rt60)
{
    // fits typical rooms and halls: 0.5 s ~ 250 m^3, 1 s ~ 2000 m^3, 2 s ~ 16000 m^3 (Musikverein,
    // Concertgebouw: 15000 - 19000 m^3 at 2.0 s)
    return jlimit (150.0f, 50000.0f, 2000.0f * rt60 * rt60 * rt60);
}

float hallWidthForVolume (float volume)
{
    return std::cbrt (std::max (1.0f, volume) / 1.6f);
}

float hallSurfaceForVolume (float volume)
{
    // 2 (L W + L H + W H) with L = 2 W, H = 0.8 W
    const float w = hallWidthForVolume (volume);
    return 8.8f * w * w;
}

float eyringRt60 (float volume, float absorption)
{
    const float a = jlimit (0.001f, 0.999f, absorption);
    return 0.161f * std::max (1.0f, volume) / (-hallSurfaceForVolume (volume) * std::log (1.0f - a));
}

float eyringAbsorption (float volume, float rt60)
{
    const float v = std::max (1.0f, volume);
    return 1.0f - std::exp (-0.161f * v / (hallSurfaceForVolume (v) * std::max (0.01f, rt60)));
}

const char* describeAbsorption (float a)
{
    // mean (Eyring) coefficients of whole rooms, occupied where it applies
    if (a < 0.10f) return "stone church";
    if (a < 0.17f) return "bare hall";
    if (a < 0.23f) return "empty hall";
    if (a < 0.32f) return "concert hall";
    if (a < 0.42f) return "opera house";
    if (a < 0.60f) return "studio";
    return "very dry";
}

namespace
{
    float directEnergy (float r) { return 100.0f / (r * r); }
    float reflectedEnergy (float r, float T, float V) { return 31200.0f * T / V * std::exp (-0.04f * r / T); }
}

float barronDrrDb (float distance, float rt60, float volume)
{
    const float T = jlimit (0.1f, 30.0f, rt60);
    const float r = std::max (0.01f, distance);
    return 10.0f * std::log10 (directEnergy (r) / reflectedEnergy (r, T, std::max (1.0f, volume)));
}

float distanceForDrr (float drrDb, float rt60, float volume)
{
    // the ratio falls monotonically with distance
    float lo = 0.1f, hi = 1000.0f;
    for (int i = 0; i < 60; ++i)
    {
        const float mid = std::sqrt (lo * hi);
        (barronDrrDb (mid, rt60, volume) > drrDb ? lo : hi) = mid;
    }
    return std::sqrt (lo * hi);
}

float initialGapMs (float distance, float volume)
{
    const float r = std::max (0.0f, distance);
    const float lateral = 2.0f * kNearestReflectorFraction * hallWidthForVolume (volume);
    return 1000.0f * (std::sqrt (r * r + lateral * lateral) - r) / kSpeedOfSound;
}

DistanceResult computeDistance (const DistanceModel& m)
{
    DistanceResult res;
    const float T = jlimit (0.1f, 30.0f, m.rt60);
    const float r = std::max (0.1f, m.distance);
    const float r0 = std::max (0.1f, m.referenceDistance);
    res.volume = m.volume > 0.0f ? m.volume : estimateHallVolume (T);
    const float V = res.volume;

    res.drrChangeDb = barronDrrDb (r, T, V) - barronDrrDb (r0, T, V);
    res.criticalDistance = distanceForDrr (0.0f, T, V);
    res.gapChangeMs = initialGapMs (r, V) - initialGapMs (r0, V);
    res.airMetres = std::max (0.0f, r - r0);
    if (m.micDistance > 0.0f)
        res.directDelayMs = 1000.0f * std::max (0.0f, r - m.micDistance) / kSpeedOfSound;

    res.presenceDb = jlimit (kPresenceMinDb, kPresenceMaxDb, -kPresenceDbPerDoubling * std::log2 (r / r0));

    if (m.sourceWidth > 0.0f)
    {
        auto angle = [&] (float d) { return 2.0f * std::atan (m.sourceWidth / (2.0f * d)); };
        res.widthFactor = jlimit (0.0f, 1.0f, angle (r) / angle (r0));
    }
    return res;
}

float mixForDrrChange (float mix, float drrChangeDb)
{
    if (mix <= 0.0f || mix >= 1.0f) return mix;   // direct only / reverb only stay so
    const float ratio = (1.0f - mix) / mix * Decibels::decibelsToGain (drrChangeDb);
    return 1.0f / (1.0f + ratio);
}

DirectSettings applyDistance (DirectSettings s, const DistanceResult& d)
{
    s.mix = mixForDrrChange (s.mix, d.drrChangeDb);
    s.highsDb = jlimit (DirectSettings::kHighsMinDb, DirectSettings::kHighsMaxDb, s.highsDb + d.presenceDb);
    s.airDb = jlimit (0.0f, DirectSettings::kAirMaxDb,
                      s.airDb + (float) airAbsorptionDbPerMetre (kAirReferenceHz, kAirHumidity) * d.airMetres);
    s.width = jlimit (0.0f, 1.0f, s.width * d.widthFactor);
    s.delayMs = jlimit (0.0f, DirectSettings::kDelayMaxMs, s.delayMs + d.directDelayMs);
    // closer: the first reflection comes later. Further away the gap stays (the response is never cut)
    s.gapMs = jlimit (0.0f, DirectSettings::kGapMaxMs, s.gapMs + std::max (0.0f, d.gapChangeMs));
    return s;
}

//==============================================================================
void AirAbsorptionFilter::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    maxBlock = std::max (16, maxBlockSize);
    const int factor = (int) nextPowerOfTwo ((int) std::ceil (sr / 48000.0 - 1.0e-6));
    fftSize = 1024 * std::max (1, factor);
    firLength = fftSize / 32;   // < 0.05 dB from the ISO curve below 16 kHz
    fft.init ((size_t) fftSize);

    const size_t bins = (size_t) fftSize / 2 + 1;
    timeBuf.assign ((size_t) fftSize, 0.0f);
    re.assign (bins, 0.0f);
    im.assign (bins, 0.0f);
    cepstrumPerMetre.assign ((size_t) fftSize, 0.0f);
    for (auto& c : coeffs) c.assign ((size_t) firLength, 0.0f);
    const int tw = firLength / 4;
    tailWindow.assign ((size_t) firLength, 1.0f);
    for (int n = 0; n < tw; ++n)
        tailWindow[(size_t) (firLength - tw + n)] = (float) (0.5 + 0.5 * std::cos (kPi * n / tw));
    histL.assign ((size_t) (firLength - 1 + maxBlock), 0.0f);
    histR = histL;
    humidityNow = -1.0f;
    metresNow = 0.0f;
    bypass = { true, true };
    current = 0;
    fading = false;
}

void AirAbsorptionFilter::reset()
{
    std::fill (histL.begin(), histL.end(), 0.0f);
    std::fill (histR.begin(), histR.end(), 0.0f);
}

void AirAbsorptionFilter::setPath (float metres, float humidity) noexcept
{
    if (cepstrumPerMetre.empty()) return;
    metres = std::max (0.0f, metres);
    const bool humidityChanged = std::abs (humidity - humidityNow) > 0.05f;
    if (! humidityChanged && std::abs (metres - metresNow) <= 0.01f + 0.002f * metresNow)
        return;

    if (humidityChanged)
    {
        // real cepstrum of the log magnitude of one metre of air, folded for minimum phase
        humidityNow = humidity;
        const size_t bins = re.size();
        for (size_t k = 0; k < bins; ++k)
        {
            const double f = std::max (1.0, (double) k * sampleRate / fftSize);
            re[k] = (float) (-airAbsorptionDbPerMetre (f, humidity) * std::log (10.0) / 20.0);
            im[k] = 0.0f;
        }
        fft.ifft (timeBuf.data(), re.data(), im.data());
        const int half = fftSize / 2;
        std::fill (cepstrumPerMetre.begin(), cepstrumPerMetre.end(), 0.0f);
        cepstrumPerMetre[0] = timeBuf[0];
        for (int n = 1; n < half; ++n)
            cepstrumPerMetre[(size_t) n] = 2.0f * timeBuf[(size_t) n];
        cepstrumPerMetre[(size_t) half] = timeBuf[(size_t) half];
    }

    metresNow = metres;
    const int target = 1 - current;
    design (metres, target);
    current = target;
    fading = true;
}

void AirAbsorptionFilter::design (float metres, int target) noexcept
{
    auto& h = coeffs[(size_t) target];
    if (metres < 0.01f)
    {
        bypass[(size_t) target] = true;
        std::fill (h.begin(), h.end(), 0.0f);
        h[0] = 1.0f;
        return;
    }
    bypass[(size_t) target] = false;

    // the cepstrum scales with the path length: log|H| = metres * log|H_1m|
    for (int n = 0; n < fftSize; ++n)
        timeBuf[(size_t) n] = cepstrumPerMetre[(size_t) n] * metres;
    fft.fft (timeBuf.data(), re.data(), im.data());
    for (size_t k = 0; k < re.size(); ++k)
    {
        const float mag = std::exp (re[k]);
        const float ph = im[k];
        re[k] = mag * std::cos (ph);
        im[k] = mag * std::sin (ph);
    }
    fft.ifft (timeBuf.data(), re.data(), im.data());
    for (int n = 0; n < firLength; ++n)
        h[(size_t) n] = timeBuf[(size_t) n] * tailWindow[(size_t) n];
}

void AirAbsorptionFilter::process (float* l, float* r, int n) noexcept
{
    for (int done = 0; done < n;)
    {
        const int chunk = std::min (maxBlock, n - done);
        processChunk (l + done, r + done, chunk);
        done += chunk;
    }
}

void AirAbsorptionFilter::processChunk (float* l, float* r, int n) noexcept
{
    const int L = firLength;
    const bool both = fading;
    const auto& hNew = coeffs[(size_t) current];
    const auto& hOld = coeffs[(size_t) (1 - current)];
    const bool bypassNew = bypass[(size_t) current], bypassOld = bypass[(size_t) (1 - current)];
    fading = false;

    for (auto [x, hist] : { std::pair<float*, std::vector<float>*> { l, &histL }, { r, &histR } })
    {
        float* hb = hist->data();
        std::copy (x, x + n, hb + L - 1);
        if (bypassNew && (! both || bypassOld))
        {
            // nothing to filter; keep the history for the next change
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                const float* xi = hb + L - 1 + i;
                float yNew = xi[0];
                if (! bypassNew)
                {
                    yNew = 0.0f;
                    for (int k = 0; k < L; ++k) yNew += hNew[(size_t) k] * xi[-k];
                }
                if (both)
                {
                    float yOld = xi[0];
                    if (! bypassOld)
                    {
                        yOld = 0.0f;
                        for (int k = 0; k < L; ++k) yOld += hOld[(size_t) k] * xi[-k];
                    }
                    const float a = (float) (i + 1) / (float) n;
                    yNew = yOld + a * (yNew - yOld);
                }
                x[i] = yNew;
            }
        }
        std::copy (hb + n, hb + n + L - 1, hb);
    }
}

//==============================================================================
void SmoothDelay::prepare (double sr, double maxSeconds)
{
    size = (int) std::ceil (maxSeconds * sr) + 8;
    bufL.assign ((size_t) size, 0.0f);
    bufR = bufL;
    writePos = 0;
    delay.reset (sr, 0.25);
    delay.setCurrentAndTargetValue (0.0f);
}

void SmoothDelay::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
}

void SmoothDelay::setDelaySamples (float samples, bool snap) noexcept
{
    // whole samples as targets: once settled the delay is exact and does not filter the signal
    const float t = jlimit (0.0f, (float) (size - 4), std::round (samples));
    if (snap) delay.setCurrentAndTargetValue (t);
    else delay.setTargetValue (t);
}

void SmoothDelay::process (float* l, float* r, int n) noexcept
{
    if (! delay.isSmoothing() && delay.getCurrentValue() < 0.5f)
    {
        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) writePos] = l[i];
            bufR[(size_t) writePos] = r[i];
            if (++writePos == size) writePos = 0;
        }
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        bufL[(size_t) writePos] = l[i];
        bufR[(size_t) writePos] = r[i];
        const float d = delay.getNextValue();
        const int di = (int) d;
        const float frac = d - (float) di;
        auto at = [&] (const std::vector<float>& b, int back) {
            int idx = writePos - back;
            if (idx < 0) idx += size;
            return b[(size_t) idx];
        };
        if (frac < 1.0e-4f)
        {
            l[i] = at (bufL, di);
            r[i] = at (bufR, di);
        }
        else
        {
            // 3rd order Lagrange between d - 1 and d + 2 samples back
            const float c0 = -frac * (frac - 1.0f) * (frac - 2.0f) / 6.0f;
            const float c1 = (frac + 1.0f) * (frac - 1.0f) * (frac - 2.0f) / 2.0f;
            const float c2 = -(frac + 1.0f) * frac * (frac - 2.0f) / 2.0f;
            const float c3 = (frac + 1.0f) * frac * (frac - 1.0f) / 6.0f;
            const int b0 = std::max (0, di - 1);
            for (auto [buf, out] : { std::pair<const std::vector<float>*, float*> { &bufL, l + i }, { &bufR, r + i } })
                *out = c0 * at (*buf, b0) + c1 * at (*buf, di) + c2 * at (*buf, di + 1) + c3 * at (*buf, di + 2);
        }
        if (++writePos == size) writePos = 0;
    }
}

//==============================================================================
void DistanceRenderer::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    air.prepare (sr, maxBlockSize);
    directDelay.prepare (sr, kMaxDelaySeconds);
    wetDelay.prepare (sr, kMaxDelaySeconds);
    width.reset (sr, 0.05);
    width.setCurrentAndTargetValue (1.0f);
    presenceDb.reset (sr, 0.1);
    presenceDb.setCurrentAndTargetValue (0.0f);
    presence.reset();
    presenceActive = false;
    envCoeff = (float) (1.0 - std::exp (-1.0 / (0.5 * sr)));
    envL = envR = 0.0f;
}

void DistanceRenderer::reset()
{
    air.reset();
    directDelay.reset();
    wetDelay.reset();
    presence.reset();
    envL = envR = 0.0f;
}

void DistanceRenderer::process (float* dryL, float* dryR, float* wetL, float* wetR, int n, const DirectSettings& s,
                                bool snap) noexcept
{
    air.setPath (s.airDb / (float) airAbsorptionDbPerMetre (kAirReferenceHz, kAirHumidity), kAirHumidity);
    air.process (dryL, dryR, n);

    if (snap) presenceDb.setCurrentAndTargetValue (s.highsDb);
    else presenceDb.setTargetValue (s.highsDb);
    processPresence (dryL, dryR, n);

    if (snap) width.setCurrentAndTargetValue (s.width);
    else width.setTargetValue (s.width);
    processWidth (dryL, dryR, n);

    const float msToSamples = (float) (sampleRate * 0.001);
    directDelay.setDelaySamples (s.delayMs * msToSamples, snap);
    wetDelay.setDelaySamples ((s.delayMs + s.gapMs) * msToSamples, snap);
    directDelay.process (dryL, dryR, n);
    wetDelay.process (wetL, wetR, n);
}

void DistanceRenderer::processPresence (float* l, float* r, int n) noexcept
{
    if (! presenceDb.isSmoothing() && std::abs (presenceDb.getCurrentValue()) < 0.01f)
    {
        presenceActive = false;   // exactly transparent at the reference
        return;
    }
    if (! presenceActive)
    {
        presence.reset();
        presenceActive = true;
    }
    for (int start = 0; start < n; start += kPresenceStep)
    {
        const int len = std::min (kPresenceStep, n - start);
        presence.highShelf (sampleRate, kPresenceShelfHz, kPresenceQ, presenceDb.skip (len));
        for (int i = start; i < start + len; ++i)
        {
            l[i] = presence.process (l[i], 0);
            r[i] = presence.process (r[i], 1);
        }
    }
}

void DistanceRenderer::processWidth (float* l, float* r, int n) noexcept
{
    const bool active = width.isSmoothing() || width.getCurrentValue() < 0.9999f;
    for (int i = 0; i < n; ++i)
    {
        envL += envCoeff * (l[i] * l[i] - envL);
        envR += envCoeff * (r[i] * r[i] - envR);
        if (! active) continue;

        // narrow towards the source's own position: amplitude panning of the mono sum
        // by the left / right balance keeps a panned source where it is
        const float w = width.getNextValue();
        const float aL = std::sqrt (envL), aR = std::sqrt (envR);
        const float gL = aL + aR > 1.0e-9f ? aL / (aL + aR) : 0.5f;
        const float sum = l[i] + r[i];
        l[i] = w * l[i] + (1.0f - w) * gL * sum;
        r[i] = w * r[i] + (1.0f - w) * (1.0f - gL) * sum;
    }
}
} // namespace concert7
