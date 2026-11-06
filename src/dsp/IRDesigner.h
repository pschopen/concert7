// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later
//
// Turns static impulse responses into a parametric reverb.
//
// Controls like decay time, multi-band decay, early/late
// balance, roll-offs, VLF, pre-delay, delay voices and swappable early
// reflection patterns) are realised by re-synthesising the impulse response:
//
//  1. The source IR is split into an early part and a late part around
//     onset + ~80 ms (raised cosine crossover).
//  2. The late part is analysed in third-octave bands (Schroeder integration)
//     to find the native reverb time per band and the point where each band
//     reaches its noise floor / end of file.
//  3. In the STFT domain every bin receives a time-varying gain that turns the
//     native decay slope into the target slope. Beyond the valid region the
//     tail is continued with random-phase noise that follows the target decay,
//     so the reverb can be made longer than the recording. "Smooth" pulls each
//     bin towards its third-octave neighbourhood (resonances, ringing) and each
//     band envelope towards its fitted decay line (bumps, late echoes).
//  4. Early part (optionally from another preset = "early pattern"), late part
//     (shifted by the pre-delay) and delay voices are summed with their levels.
//     The offset finally moves the whole response: later (leading silence) or
//     earlier (its start is cut, with a short fade-in).
//  5. For the modulation the late reverb can be handed over separately from the
//     early part (so it can move on its own), together with a variant: the same
//     late reverb with new random phases in every STFT frame - same envelope,
//     spectrum and channel relation, but an independent fine structure.

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <vector>
#include "IRLibrary.h"

namespace concert7
{
inline constexpr double kMaxIRSeconds = 30.0;

/** A true stereo impulse response at the host sample rate.
    Channels: 0 = L->L, 1 = L->R, 2 = R->L, 3 = R->R. For plain stereo
    responses only channels 0 and 3 are used. */
struct SourceIR
{
    juce::String presetId;
    juce::String name;
    double sampleRate = 0.0;     // rate of the data in ch (host rate)
    int fileRate = 0;            // rate of the file on disk
    bool trueStereo = false;
    std::array<std::vector<float>, 4> ch;

    // analysis results
    int onset = 0;               // first sample of the direct sound / first reflection
    float nativeRT = 1.0f;       // RT60 in the 500 Hz .. 2 kHz bands
    std::vector<float> bandFreq;
    std::vector<float> bandRT;
    std::vector<float> bandValidEnd; // seconds after onset where each band hits its floor
    std::vector<float> bandIntercept;  // dB at the onset of the fitted decay line (same scale as bandLevelDb)
    std::vector<std::vector<float>> bandLevelDb; // smoothed band level per STFT frame (sum of channels)
    std::vector<float> binColourDb;  // late reverb level of each STFT bin relative to its third-octave neighbourhood

    int length() const { return (int) ch[0].size(); }
};

struct DesignParams
{
    float decay = 2.0f;          // target mid-band RT60 in seconds
    float predelayMs = 0.0f;     // extra delay of the late reverb
    float offsetMs = 0.0f;       // start of the whole response: > 0 later, < 0 earlier (the start is cut)
    float earlyDb = 0.0f;
    float lateDb = 0.0f;
    float vlfDb = 0.0f;          // <= kVlfOffDb means off
    float erRolloffHz = 22000.0f;
    float lateRolloffHz = 22000.0f;
    float delayMs = 250.0f;
    float delayDb = -21.0f;      // <= kDelayOffDb means off
    float lowFreq = 400.0f;
    float lowMult = 1.0f;
    float highFreq = 4000.0f;
    float highMult = 1.0f;
    float smooth = 0.0f;         // 0 .. 1: evens out resonances and envelope bumps of the late reverb
    bool splitLate = false;      // modulation: the late reverb as a response of its own (BuiltIR::late)
    bool lateVariant = false;    // modulation: plus its variant (BuiltIR::variant), needs splitLate

    bool operator== (const DesignParams& o) const;
    bool operator!= (const DesignParams& o) const { return ! (*this == o); }
};

struct BuiltIR
{
    double sampleRate = 0.0;
    bool trueStereo = false;
    std::array<std::vector<float>, 4> ch;

    // With DesignParams::splitLate, ch holds only the early part and late the late reverb (with pre-delay,
    // delay voices and offset): ch + late is the whole response. variant has the same envelope and spectrum
    // as late, but an independent fine structure. Empty when not requested.
    std::array<std::vector<float>, 4> late, variant;
    bool hasLate() const { return ! late[0].empty(); }
    bool hasVariant() const { return ! variant[0].empty(); }

    int length() const { return (int) std::max (ch[0].size(), late[0].size()); }

    // information for the user interface
    juce::String presetId;
    juce::String name;
    float nativeRT = 1.0f;
    float targetRT = 1.0f;
    float splitSeconds = 0.08f;
    float seconds = 0.0f;
    std::vector<float> envelopeDb;       // energy envelope of the result, 0 dB = max
    std::vector<float> nativeEnvelopeDb; // same time axis, unmodified source
    std::vector<float> rtFreqs, rtNative, rtTarget;

    // distance mode: energy relative to a unit impulse, 500 Hz - 2 kHz, per output channel
    float midEnergy = 0.0f;          // all four paths
    float midEnergyDiagonal = 0.0f;  // L->L and R->R only (no crossfeed)
    float onsetMs = 0.0f;            // first reflection of the preset, moved by the offset (for the distance display)
};

/** Loads the files of a preset into a SourceIR at the given host rate. */
std::shared_ptr<SourceIR> loadSourceIR (const IRPreset& preset, double hostRate, juce::String& error);

/** Builds a SourceIR from raw channels (used for user files and tests). */
std::shared_ptr<SourceIR> makeSourceIR (std::array<std::vector<float>, 4> channels, bool trueStereo,
                                        double sourceRate, double hostRate, const juce::String& name);

/** Fills the analysis fields of a SourceIR. Called by loadSourceIR / makeSourceIR. */
void analyseSourceIR (SourceIR& src);

/** Windowed sinc resampler (offline, high quality). */
std::vector<float> resample (const std::vector<float>& in, double srcRate, double dstRate);

/** Creates the impulse response for the given parameters.
    @param pattern  optional source whose early part replaces the preset's early reflections */
std::shared_ptr<BuiltIR> designIR (const SourceIR& src, const SourceIR* pattern, const DesignParams& params);

/** Broadband RT60 estimate (T20 fit on the Schroeder curve) of channel energy sum. Used by tests. */
float estimateRT (const std::vector<const std::vector<float>*>& channels, double sampleRate,
                  float lowHz = 0.0f, float highHz = 0.0f);
} // namespace concert7
