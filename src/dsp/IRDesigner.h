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
//  3. In the STFT domain every bin receives a time-varying gain that turns its
//     own native decay slope into the target slope (a shorter decay fades the
//     early part the same way). Beyond the valid region the tail is continued
//     with random-phase noise that starts on the bin's fitted decay line, keeps
//     the measured coherence of the channels and follows the target decay,
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
//  6. Split captures with several takes: the other takes of the early reflections and the late reverb are
//     built like the first and handed over as responses of their own, the convolution moves through them
//     (DesignParams::takes).

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <complex>
#include <memory>
#include <vector>
#include "IRLibrary.h"

namespace concert7
{
inline constexpr double kMaxIRSeconds = 30.0;      // read from an impulse response file
inline constexpr double kMaxBuiltIRSeconds = 60.0; // a designed response (a long DECAY rings longer than its file)

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

    // split captures: the early reflections and the late reverb as recorded on their own, ch is their sum (all of the
    // same length). The late reverb starts together with the reflections, so nothing is cut at a crossover: the early
    // part is earlyBlock, the late part lateBlock. Empty for the other libraries.
    std::array<std::vector<float>, 4> earlyBlock, lateBlock;
    // a second take of the late reverb where the takes have their own fine structure (the M7's second banks):
    // the same envelope and spectrum, for the modulation's variant. Same scale as lateBlock, empty otherwise.
    std::array<std::vector<float>, 4> lateTake;
    bool hasBlocks() const { return ! lateBlock[0].empty(); }
    bool hasLateTake() const { return ! lateTake[0].empty(); }

    // split captures, the other takes (loaded on demand, see loadSourceTakes): early reflections that differ from
    // the first take, late reverbs with a fine structure of their own. Same scale and length as the blocks.
    std::vector<std::array<std::vector<float>, 4>> earlyTakes, lateTakes;
    bool takesLoaded = false;
    // how the blocks were made from the files: length at the file's rate (after trimming), that rate, the gain
    double blockFileRate = 0.0;
    size_t blockFileLength = 0;
    float blockGain = 1.0f;

    // analysis results (of the late block where there is one)
    int onset = 0;               // first sample of the direct sound / first reflection
    float nativeRT = 1.0f;       // RT60 in the 500 Hz .. 2 kHz bands
    std::vector<float> bandFreq;
    std::vector<float> bandRT;
    std::vector<float> bandValidEnd; // seconds after onset where each band hits its floor (or leaves its decay line)
    std::vector<float> bandValidEndRecorded; // ... where it hits its floor only (DecayMode::Original)
    std::vector<float> bandIntercept;  // dB at the onset of the fitted decay line (same scale as bandLevelDb)
    std::vector<std::vector<float>> bandLevelDb; // smoothed band level per STFT frame (sum of channels)
    std::vector<float> binColourDb;  // late reverb level of each STFT bin relative to its third-octave neighbourhood

    // every STFT bin's own late decay: its reverb time, the power of each channel on that decay line at the
    // onset, and the coherence of the channels as the lower triangular factor of their coherence matrix
    // (row i, column j <= i at binMix[k * 10 + i * (i + 1) / 2 + j], i and j count the used channels:
    // 0 1 2 3 for true stereo, 0 3 for stereo)
    std::vector<float> binRT;
    std::array<std::vector<float>, 4> binPower;
    std::vector<std::complex<float>> binMix;

    // DecayMode::Stretch: the last resampled copy of this response and its factor, kept by designIR for the next build
    mutable std::shared_ptr<const SourceIR> stretched;
    mutable float stretchedFactor = 0.0f;

    int length() const { return (int) ch[0].size(); }
};

/** How DECAY gets to its reverb time. Nobody knows which of them the M7 itself does (there are no recordings of one
    program at several decays), so the user chooses.
    Sabine: only the wall part of the decay rate scales, air absorption stays; the late reverb starts a little lower
            the longer it rings (energy ~ RT^0.45, from the split captures).
    Proportional: every frequency scales with the same factor (the M7's HF / LF RT MPY are multipliers of the reverb
            time); level as Sabine. Long settings keep the brilliance of the highs.
    ConstantLevel: Sabine, and the reverb keeps the loudness of the preset at every decay.
    Stretch: the response is resampled up to kMaxStretch (a larger or smaller room: reflections, build-up and colour
            move with it), Sabine does the rest.
    Original: the mechanism of 0.1.0-beta, for comparison: one decay per third octave scaled proportionally, the
            early reflections and the start of the late reverb as they are, the continuation from the level of the
            last frames of the recording (each channel its own noise, a short crossfade). */
enum class DecayMode { Sabine = 0, Proportional, ConstantLevel, Stretch, Original };

/** Split captures with several takes: which parts move through their takes. */
enum class TakeMode { Off = 0, Late, EarlyAndLate };
inline constexpr float kMaxStretch = 1.3f;

struct DesignParams
{
    float decay = 2.0f;          // target mid-band RT60 in seconds
    DecayMode decayMode = DecayMode::Sabine;
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
    // split captures only:
    TakeMode takes = TakeMode::Off; // the other takes as responses of their own (BuiltIR::earlyTakes, lateTakes), needs splitLate
    float lateOffsetMs = 0.0f;   // moves the late block against the reflections: < 0 earlier, at most up to its first sound

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

    // DesignParams::takes: the other takes of the early part (like ch) and of the late reverb (like late), and the
    // correlation of neighbours in the sequence of all takes: takeRho[k] of take k with take k + 1, the last with
    // the first (take 0 is ch / late)
    std::vector<std::array<std::vector<float>, 4>> earlyTakes, lateTakes;
    std::vector<float> earlyTakeRho, lateTakeRho;

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

/** Split captures: loads the other takes of a preset whose blocks src holds (once; true when there is nothing to
    load). The takes are made like the blocks; a take that equals the first is left out. */
bool loadSourceTakes (const IRPreset& preset, SourceIR& src, juce::String& error);

/** Builds a SourceIR from raw channels (used for user files and tests). Normalised to the same loudness for every
    response, gainDb (energy) on top. */
std::shared_ptr<SourceIR> makeSourceIR (std::array<std::vector<float>, 4> channels, bool trueStereo,
                                        double sourceRate, double hostRate, const juce::String& name, float gainDb = 0.0f);

/** The same from the early reflections and the late reverb recorded on their own (split captures): ch is
    their sum at the files' levels. lateTake (may be empty): another take of the late reverb. */
std::shared_ptr<SourceIR> makeSourceIR (std::array<std::vector<float>, 4> early, std::array<std::vector<float>, 4> late,
                                        std::array<std::vector<float>, 4> lateTake, bool trueStereo, double sourceRate,
                                        double hostRate, const juce::String& name, float gainDb = 0.0f);

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
