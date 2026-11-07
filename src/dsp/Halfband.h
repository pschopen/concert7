// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <array>
#include <vector>

namespace concert7
{
/**
    Halving and doubling the sample rate with a linear phase halfband FIR (63 taps, Kaiser window, about
    80 dB stopband, flat to 0.21 of the sample rate: 20 kHz at 96 kHz). Used to convolve at half the host
    rate ("under-sampling"): the impulse responses hold nothing above 22/24 kHz anyway.

    Both run at the full rate's clock: the decimator emits a sample at every even full rate sample (counted
    across blocks), the interpolator takes one there. So a block of n samples yields as many half rate
    samples as it has even positions, and the decimator's output of a block is all the interpolator needs
    for the same block. Decimator and interpolator together delay by kDelay full rate samples.
*/
class HalfbandFilter
{
public:
    static constexpr int kCentre = 31;              // taps 0 .. 2 * kCentre
    static constexpr int kDelay = 2 * kCentre;      // decimate + interpolate, full rate samples (even)
    static constexpr int kPairs = (kCentre + 1) / 2; // non-zero taps on each side of the centre

    HalfbandFilter();
    /** Coefficient of the taps kCentre +- (2 m + 1). */
    float pair (int m) const { return pairs[(size_t) m]; }

private:
    std::array<float, kPairs> pairs {};
};

class HalfbandDecimator
{
public:
    void reset();
    /** Number of half rate samples the next n full rate samples give. */
    int outputCount (int n) const { return (n + 1 - phase) / 2; }
    /** Returns the number of samples written to out (outputCount (n)). */
    int process (const float* in, float* out, int n);

private:
    static constexpr int kLength = 2 * HalfbandFilter::kCentre + 1;
    HalfbandFilter filter;
    std::array<float, 2 * kLength> line {};   // the delay line twice, so a window is always contiguous
    int pos = 0, phase = 0;
};

class HalfbandInterpolator
{
public:
    void reset();
    /** Writes n full rate samples; takes a sample of in at every even position (outputCount (n) of them). */
    void process (const float* in, float* out, int n);

private:
    static constexpr int kLength = HalfbandFilter::kCentre + 1;   // half rate samples the filter spans
    HalfbandFilter filter;
    std::array<float, 2 * kLength> line {};
    int pos = 0, phase = 0;
};
} // namespace concert7
