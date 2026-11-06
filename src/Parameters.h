// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace concert7
{
inline constexpr const char* kPluginName = "Concert7";

namespace ParamID
{
    // main
    inline constexpr const char* predelay      = "predelay";
    inline constexpr const char* predelaySync  = "predelaySync";
    inline constexpr const char* predelayNote  = "predelayNote";
    inline constexpr const char* decay         = "decay";
    inline constexpr const char* early         = "early";
    inline constexpr const char* late          = "late";
    inline constexpr const char* mix           = "mix";
    inline constexpr const char* gain          = "gain";

    // reverb (advanced)
    inline constexpr const char* vlf           = "vlf";
    inline constexpr const char* erRolloff     = "erRolloff";
    inline constexpr const char* lateRolloff   = "lateRolloff";
    inline constexpr const char* erPattern     = "erPattern";
    inline constexpr const char* delayTime     = "delayTime";
    inline constexpr const char* delaySync     = "delaySync";
    inline constexpr const char* delayNote     = "delayNote";
    inline constexpr const char* delayLevel    = "delayLevel";
    inline constexpr const char* crossfeed     = "crossfeed";

    // multi-band decay
    inline constexpr const char* lowFreq       = "lowFreq";
    inline constexpr const char* lowMult       = "lowMult";
    inline constexpr const char* highFreq      = "highFreq";
    inline constexpr const char* highMult      = "highMult";

    // extras: modulation, smooth, ducker
    inline constexpr const char* modDepth      = "modDepth";
    inline constexpr const char* modRate       = "modRate";
    inline constexpr const char* smooth        = "smooth";
    inline constexpr const char* duckAmount    = "duckAmount";
    inline constexpr const char* duckThreshold = "duckThreshold";
    inline constexpr const char* duckRelease   = "duckRelease";
    inline constexpr const char* duckSidechain = "duckSidechain";

    // post EQ
    inline constexpr const char* eqOn          = "eqOn";
    inline constexpr const char* eqHpOn        = "eqHpOn";
    inline constexpr const char* eqHpFreq      = "eqHpFreq";
    inline constexpr const char* eqLowType     = "eqLowType";
    inline constexpr const char* eqLowFreq     = "eqLowFreq";
    inline constexpr const char* eqLowGain     = "eqLowGain";
    inline constexpr const char* eqLowQ        = "eqLowQ";
    inline constexpr const char* eqMidFreq     = "eqMidFreq";
    inline constexpr const char* eqMidGain     = "eqMidGain";
    inline constexpr const char* eqMidQ        = "eqMidQ";
    inline constexpr const char* eqHighType    = "eqHighType";
    inline constexpr const char* eqHighFreq    = "eqHighFreq";
    inline constexpr const char* eqHighGain    = "eqHighGain";
    inline constexpr const char* eqHighQ       = "eqHighQ";
    inline constexpr const char* eqLpOn        = "eqLpOn";
    inline constexpr const char* eqLpFreq      = "eqLpFreq";

    // direct sound (the dry signal) and the gap before the reverb
    inline constexpr const char* directHighs   = "directHighs";
    inline constexpr const char* directAir     = "directAir";
    inline constexpr const char* directWidth   = "directWidth";
    inline constexpr const char* directDelay   = "directDelay";
    inline constexpr const char* directGap     = "directGap";

    // space: hall model (volume + absorption drive the decay) and distance mode
    inline constexpr const char* hallOn        = "hallOn";
    inline constexpr const char* hallVolume    = "hallVolume";
    inline constexpr const char* hallAbsorption = "hallAbsorption";
    inline constexpr const char* distOn        = "distOn";
    inline constexpr const char* distance      = "distance";
    inline constexpr const char* distMic       = "distMic";
    inline constexpr const char* distWidth     = "distWidth";
}

// Level controls use their minimum value as "off"
inline constexpr float kLevelOffDb      = -24.0f;   // early / late
inline constexpr float kVlfOffDb        = -21.0f;   // vlf
inline constexpr float kDelayOffDb      = -21.0f;   // delay voices
inline constexpr float kRolloffBypassHz = 20000.0f; // roll-off at or above is bypassed
inline constexpr int   kNumErPatterns   = 32;

enum class Crossfeed { TrueStereo = 0, NoCrossfeed, MonoIn };

/** Note values available for tempo synced pre-delay / delay, in quarter notes. */
struct NoteValue { const char* name; double quarters; };
inline constexpr NoteValue kNoteValues[] = {
    { "1/64", 1.0 / 16.0 }, { "1/32T", 1.0 / 12.0 }, { "1/32", 1.0 / 8.0 }, { "1/16T", 1.0 / 6.0 },
    { "1/16", 1.0 / 4.0 },  { "1/8T", 1.0 / 3.0 },   { "1/16D", 3.0 / 8.0 }, { "1/8", 1.0 / 2.0 },
    { "1/4T", 2.0 / 3.0 },  { "1/8D", 3.0 / 4.0 },   { "1/4", 1.0 },         { "1/2T", 4.0 / 3.0 },
    { "1/4D", 3.0 / 2.0 },  { "1/2", 2.0 },
};
inline constexpr int kNumNoteValues = (int) (sizeof (kNoteValues) / sizeof (kNoteValues[0]));

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** True for parameters that change the impulse response (and therefore need a rebuild). */
bool isIRParameter (const juce::String& paramID);

/** Human readable value strings shared by parameters, the display and tests. */
juce::String formatMs (float ms);
juce::String formatSeconds (float s);
juce::String formatHz (float hz);
juce::String formatRolloff (float hz);
juce::String formatDb (float db, float offBelowOrAt = -1000.0f);
juce::String formatMetres (float m);
juce::String formatVolume (float cubicMetres);
juce::String formatAbsorption (float absorption);
} // namespace concert7
