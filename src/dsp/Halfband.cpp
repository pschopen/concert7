// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "Halfband.h"
#include <cmath>

namespace concert7
{
namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x * 0.5 / k) * (x * 0.5 / k);
            sum += term;
        }
        return sum;
    }
}

HalfbandFilter::HalfbandFilter()
{
    // ideal halfband sin (pi k / 2) / (pi k) under a Kaiser window; the even taps are zero, the centre 0.5
    constexpr double beta = 8.0, pi = 3.14159265358979323846;
    double sum = 0.0;
    for (int m = 0; m < kPairs; ++m)
    {
        const int k = 2 * m + 1;
        const double ratio = (double) k / kCentre;
        const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - ratio * ratio))) / besselI0 (beta);
        const double h = std::sin (pi * k * 0.5) / (pi * k) * window;
        pairs[(size_t) m] = (float) h;
        sum += 2.0 * h;
    }
    // unity gain at DC: 0.5 + the pairs = 1
    for (auto& p : pairs)
        p = (float) (p * 0.5 / sum);
}

//==============================================================================
void HalfbandDecimator::reset()
{
    line.fill (0.0f);
    pos = 0;
    phase = 0;
}

int HalfbandDecimator::process (const float* in, float* out, int n)
{
    int written = 0;
    for (int i = 0; i < n; ++i)
    {
        // newest sample at line[pos], line[pos + k] is k samples older
        pos = pos == 0 ? kLength - 1 : pos - 1;
        line[(size_t) pos] = line[(size_t) (pos + kLength)] = in[i];
        if (phase == 0)
        {
            const float* w = line.data() + pos;
            constexpr int c = HalfbandFilter::kCentre;
            float y = 0.5f * w[c];
            for (int m = 0; m < HalfbandFilter::kPairs; ++m)
                y += filter.pair (m) * (w[c - 2 * m - 1] + w[c + 2 * m + 1]);
            out[written++] = y;
        }
        phase ^= 1;
    }
    return written;
}

//==============================================================================
void HalfbandInterpolator::reset()
{
    line.fill (0.0f);
    pos = 0;
    phase = 0;
}

void HalfbandInterpolator::process (const float* in, float* out, int n)
{
    // newest half rate sample at line[pos], line[pos + k] is k samples older
    constexpr int mid = (HalfbandFilter::kCentre - 1) / 2;   // 15: the centre tap at odd positions
    int taken = 0;
    for (int i = 0; i < n; ++i)
    {
        if (phase == 0)
        {
            pos = pos == 0 ? kLength - 1 : pos - 1;
            line[(size_t) pos] = line[(size_t) (pos + kLength)] = in[taken++];
            const float* w = line.data() + pos;
            float y = 0.0f;
            for (int m = 0; m < HalfbandFilter::kPairs; ++m)
                y += filter.pair (m) * (w[mid - m] + w[mid + 1 + m]);
            out[i] = 2.0f * y;
        }
        else
        {
            out[i] = line[(size_t) (pos + mid)];
        }
        phase ^= 1;
    }
}
} // namespace concert7
