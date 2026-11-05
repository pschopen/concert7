// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Distance mode: moves a dry source nearer or further away in the hall. Everything it does
// is expressed in ordinary controls - MIX and the DIRECT settings (tone, air, width, delay,
// reverb gap) - so it can be reproduced by hand and is handed back when it is switched off.
//
// The Bricasti responses contain no direct sound, so the dry input is the direct
// sound and the convolution is everything the hall adds. Switching the mode on keeps
// the sound as it is: the current mix is translated into the distance at which the
// model gives the same direct to reverberant ratio (the reference distance), and every
// change is applied relative to it. The cues that matter in a concert hall:
//
//  - direct to reverberant ratio: Barron's revised theory (Barron & Lee 1988), which
//    matches measurements in concert halls better than the classical diffuse field:
//    direct energy 100 / r^2, reflected energy (31200 T / V) e^(-0.04 r / T),
//    both relative to the direct sound at 10 m
//  - level: the mix moves like turning MIX by hand (the mix law, nothing hidden)
//  - air absorption of the direct sound over the extra path: ISO 9613-1 at 20 degrees C
//    and 50 % humidity, rendered with a minimum phase FIR (no latency)
//  - near field timbre: a close microphone hears more presence and air than a seat in
//    the hall; a gentle high shelf (about 1.5 dB per doubling of the distance) follows
//    what engineers do by ear - a heuristic, not physics
//  - initial time delay gap between the direct sound and the first reflection, from
//    a simple shoebox model of the hall derived from its volume
//  - apparent width of the source (optional): the angle it subtends shrinks with
//    distance; the image narrows around the source's own panorama position
//  - propagation delay (optional, to time align spot microphones with a main pair)

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include "AudioFFT.h"
#include "PostProcessing.h"

namespace concert7
{
inline constexpr float kSpeedOfSound = 343.0f;   // m/s at 20 degrees C
inline constexpr float kAirTemperature = 20.0f;  // degrees C
inline constexpr float kAirHumidity = 50.0f;     // %, typical for concert halls

// The first reflection usually comes from a side wall or balcony front; in concert halls these
// sit at roughly a third of the hall width from the centre line (initial time delay gaps of
// 10 - 30 ms in the middle of the stalls, Beranek).
inline constexpr float kNearestReflectorFraction = 0.35f;

/** Air absorption in dB per metre (ISO 9613-1, normal pressure). */
double airAbsorptionDbPerMetre (double freqHz, double humidityPercent, double temperatureC = kAirTemperature);

/** Typical hall volume in m^3 for a reverb time, used when the volume is set to auto. */
float estimateHallVolume (float rt60);

/** Hall width in m of a shoebox hall (length 2 W, height 0.8 W) with the given volume. */
float hallWidthForVolume (float volume);

/** Inner surface in m^2 of that shoebox hall. */
float hallSurfaceForVolume (float volume);

/** Reverb time after Eyring, T = 0.161 V / (-S ln (1 - a)), for a mean absorption coefficient a. */
float eyringRt60 (float volume, float absorption);

/** Mean absorption coefficient that gives the reverb time (inverse of eyringRt60). */
float eyringAbsorption (float volume, float rt60);

/** Short description of a mean absorption coefficient ("concert hall", "stone church" ...). */
const char* describeAbsorption (float absorption);

/** Direct to reverberant ratio in dB after Barron's revised theory. */
float barronDrrDb (float distance, float rt60, float volume);

/** Distance at which barronDrrDb reaches the given ratio (clamped to 0.1 .. 1000 m). */
float distanceForDrr (float drrDb, float rt60, float volume);

/** Initial time delay gap in ms at a distance (shoebox hall of the given volume). */
float initialGapMs (float distance, float volume);

struct DistanceModel
{
    float distance = 8.0f;           // source to listener, m
    float referenceDistance = 8.0f;  // distance at which the sound is the unchanged preset, m
    float micDistance = 1.0f;        // distance the dry signal was recorded at (time align), m
    float volume = 0.0f;             // hall volume, m^3 (<= 0: estimated from rt60)
    float rt60 = 2.0f;               // mid band reverb time, s
    float sourceWidth = 0.0f;        // m; <= 0 keeps the stereo image of the input
    bool timeAlign = false;          // delay the result by the propagation time
};

struct DistanceResult
{
    float volume = 0.0f;              // effective hall volume, m^3
    float drrChangeDb = 0.0f;         // change of the direct to reverberant ratio against the reference
    float criticalDistance = 0.0f;    // m, where direct and reflected energy are equal (model)
    float gapChangeMs = 0.0f;         // change of the gap before the first reflection
    float directDelayMs = 0.0f;       // propagation delay (time align only)
    float widthFactor = 1.0f;         // side signal gain of the direct sound
    float airMetres = 0.0f;           // extra path the air absorption applies to
    float presenceDb = 0.0f;          // near field timbre: high shelf gain of the direct sound
};

inline constexpr float kPresenceShelfHz = 3000.0f;
inline constexpr float kAirReferenceHz = 10000.0f;   // AIR is given as the loss at this frequency

/** Processing of the dry (direct) signal and the timing of the reverb against it. */
struct DirectSettings
{
    float mix = 0.3f;        // 0 .. 1, the mix law of MIX
    float highsDb = 0.0f;    // high shelf at kPresenceShelfHz
    float airDb = 0.0f;      // air absorption, loss at kAirReferenceHz (>= 0)
    float width = 1.0f;      // 0 .. 1, narrowed around the source's own panorama position
    float delayMs = 0.0f;    // delay of the direct sound and the reverb (time alignment)
    float gapMs = 0.0f;      // extra delay of the whole reverb against the direct sound

    static constexpr float kHighsMinDb = -12.0f, kHighsMaxDb = 12.0f;
    static constexpr float kAirMaxDb = 24.0f;
    static constexpr float kDelayMaxMs = 250.0f;
    static constexpr float kGapMaxMs = 100.0f;
};

/** The mix that changes the direct to reverberant ratio of a mix by the given amount. */
float mixForDrrChange (float mix, float drrChangeDb);

/** Applies a distance change to the settings the source started with (all relative). */
DirectSettings applyDistance (DirectSettings start, const DistanceResult& d);

DistanceResult computeDistance (const DistanceModel& m);

//==============================================================================
/** Minimum phase FIR with the air absorption of a path length. Zero latency. */
class AirAbsorptionFilter
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Audio thread safe (no allocation). Redesigns only when the values changed noticeably. */
    void setPath (float metres, float humidityPercent) noexcept;
    /** Crossfades from the previous response over the block when it changed. */
    void process (float* l, float* r, int n) noexcept;

    const std::vector<float>& getCoefficients() const { return coeffs[(size_t) current]; }
    bool isBypassed() const { return bypass[(size_t) current]; }

private:
    void design (float metres, int target) noexcept;
    void processChunk (float* l, float* r, int n) noexcept;

    double sampleRate = 44100.0;
    int fftSize = 1024, firLength = 128, maxBlock = 512;
    audiofft::AudioFFT fft;
    std::vector<float> cepstrumPerMetre, timeBuf, re, im, tailWindow;
    std::array<std::vector<float>, 2> coeffs;
    std::array<bool, 2> bypass { true, true };
    int current = 0;
    bool fading = false;
    float metresNow = 0.0f, humidityNow = -1.0f;
    std::vector<float> histL, histR; // last firLength - 1 inputs followed by the block
};

/** Fractional delay with smoothed delay time (cubic interpolation while it moves). */
class SmoothDelay
{
public:
    void prepare (double sampleRate, double maxSeconds);
    void reset();
    void setDelaySamples (float samples, bool snap) noexcept;
    void process (float* l, float* r, int n) noexcept;

private:
    std::vector<float> bufL, bufR;
    int size = 1, writePos = 0;
    juce::SmoothedValue<float> delay;
};

/** The DIRECT settings in real time: air absorption, presence, width and delay of the direct
    sound, and the delay of the reverb (gap). The mix is applied by the mixer. */
class DistanceRenderer
{
public:
    static constexpr double kMaxDelaySeconds = 0.5;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    /** snap = jump to the new values instead of gliding. */
    void process (float* dryL, float* dryR, float* wetL, float* wetR, int n, const DirectSettings& s,
                  bool snap = false) noexcept;

private:
    void processPresence (float* l, float* r, int n) noexcept;
    void processWidth (float* l, float* r, int n) noexcept;

    double sampleRate = 44100.0;
    AirAbsorptionFilter air;
    SmoothDelay directDelay, wetDelay;
    juce::SmoothedValue<float> width, presenceDb;
    Biquad presence;
    bool presenceActive = false;
    float envL = 0.0f, envR = 0.0f, envCoeff = 0.0f;  // slow levels: the source's panorama position
};
} // namespace concert7
