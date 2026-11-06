// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "Parameters.h"
#include "dsp/Distance.h"

namespace concert7
{
using namespace juce;

String formatMs (float ms)
{
    if (ms >= 1000.0f)
        return String (ms / 1000.0f, 2) + " s";
    return String (ms, ms < 10.0f ? 1 : 0) + " ms";
}

String formatOffsetMs (float ms)
{
    if (std::abs (ms) < 0.05f)
        return "0 ms";
    return (ms > 0.0f ? "+" : "-") + formatMs (std::abs (ms));
}

String formatSeconds (float s)
{
    if (s < 1.0f)
        return String (roundToInt (s * 1000.0f)) + " ms";
    return String (s, s < 10.0f ? 2 : 1) + " s";
}

String formatRolloff (float hz)
{
    return hz >= kRolloffBypassHz ? String ("Off") : formatHz (hz);
}

String formatHz (float hz)
{
    if (hz >= 1000.0f)
        return String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    return String (roundToInt (hz)) + " Hz";
}

String formatDb (float db, float offBelowOrAt)
{
    if (db <= offBelowOrAt + 0.01f)
        return "Off";
    return (db > 0.05f ? "+" : "") + String (db, 1) + " dB";
}

String formatMetres (float m)
{
    return String (m, m < 10.0f ? 1 : 0) + " m";
}

String formatAbsorption (float a)
{
    return String (a, 2) + " " + describeAbsorption (a);
}

String formatVolume (float v)
{
    const int rounded = v >= 1000.0f ? roundToInt (v / 100.0f) * 100 : roundToInt (v / 10.0f) * 10;
    const auto digits = rounded >= 1000 ? String (rounded / 1000) + " " + String (rounded % 1000).paddedLeft ('0', 3)
                                        : String (rounded);
    return digits + " " + String (CharPointer_UTF8 ("m\xc2\xb3"));
}

namespace
{
    NormalisableRange<float> logRange (float lo, float hi)
    {
        return NormalisableRange<float> (lo, hi,
            [] (float start, float end, float v) { return start * std::pow (end / start, v); },
            [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
            [] (float start, float end, float v) { return jlimit (start, end, v); });
    }

    /** Off (0) over the first few percent of the travel, then logarithmic from lo to hi. */
    NormalisableRange<float> logRangeWithOff (float lo, float hi)
    {
        constexpr float offSpan = 0.04f;
        return NormalisableRange<float> (0.0f, hi,
            [lo] (float, float end, float v) { return v < offSpan ? 0.0f : lo * std::pow (end / lo, (v - offSpan) / (1.0f - offSpan)); },
            [lo] (float, float end, float v) { return v < lo ? 0.0f : offSpan + (1.0f - offSpan) * std::log (v / lo) / std::log (end / lo); },
            [lo] (float, float end, float v) { return v < 0.5f * lo ? 0.0f : jlimit (lo, end, v); });
    }

    NormalisableRange<float> skewedRange (float lo, float hi, float centre, float step = 0.0f)
    {
        NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }

    StringArray noteNames()
    {
        StringArray s;
        for (auto& n : kNoteValues)
            s.add (n.name);
        return s;
    }

    std::unique_ptr<AudioParameterFloat> floatParam (const char* id, const String& name, NormalisableRange<float> range,
                                                     float def, const String& label,
                                                     std::function<String (float, int)> toText)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes().withLabel (label).withStringFromValueFunction (std::move (toText)));
    }
}

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto ms   = [] (float v, int) { return formatMs (v); };
    auto sec  = [] (float v, int) { return formatSeconds (v); };
    auto hz   = [] (float v, int) { return formatHz (v); };
    auto roll = [] (float v, int) { return formatRolloff (v); };
    auto db   = [] (float v, int) { return formatDb (v); };
    auto pct  = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto mult = [] (float v, int) { return String (v, 2) + "x"; };

    // ---- main
    layout.add (floatParam (ParamID::predelay, "Pre-Delay", skewedRange (0.0f, 500.0f, 80.0f), 0.0f, "ms", ms));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::predelaySync, 1 }, "Pre-Delay Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::predelayNote, 1 }, "Pre-Delay Note", noteNames(), 4));
    // start of the whole impulse response, finer around 0
    layout.add (floatParam (ParamID::irOffset, "IR Offset", NormalisableRange<float> (-kMaxOffsetMs, kMaxOffsetMs, 0.0f, 0.4f, true), 0.0f, "ms",
                            [] (float v, int) { return formatOffsetMs (v); }));
    layout.add (floatParam (ParamID::decay, "Decay", skewedRange (0.2f, 30.0f, 3.0f), 2.0f, "s", sec));
    layout.add (floatParam (ParamID::early, "Early", NormalisableRange<float> (kLevelOffDb, 6.0f, 0.0f), 0.0f, "dB",
                            [] (float v, int) { return formatDb (v, kLevelOffDb); }));
    layout.add (floatParam (ParamID::late, "Late", NormalisableRange<float> (kLevelOffDb, 6.0f, 0.0f), 0.0f, "dB",
                            [] (float v, int) { return formatDb (v, kLevelOffDb); }));
    layout.add (floatParam (ParamID::mix, "Mix", NormalisableRange<float> (0.0f, 100.0f), 30.0f, "%", pct));
    layout.add (floatParam (ParamID::gain, "Gain", NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", db));

    // ---- reverb
    layout.add (floatParam (ParamID::vlf, "VLF", NormalisableRange<float> (kVlfOffDb, 0.0f), 0.0f, "dB",
                            [] (float v, int) { return formatDb (v, kVlfOffDb); }));
    layout.add (floatParam (ParamID::erRolloff, "Early Roll-Off", logRange (80.0f, 22000.0f), 22000.0f, "Hz", roll));
    layout.add (floatParam (ParamID::lateRolloff, "Late Roll-Off", logRange (80.0f, 22000.0f), 22000.0f, "Hz", roll));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamID::erPattern, 1 }, "Early Pattern", 0, kNumErPatterns, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? String ("Preset") : String (v); })));
    layout.add (floatParam (ParamID::delayTime, "Delay Time", skewedRange (100.0f, 1000.0f, 400.0f), 250.0f, "ms", ms));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delaySync, 1 }, "Delay Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::delayNote, 1 }, "Delay Note", noteNames(), 7));
    layout.add (floatParam (ParamID::delayLevel, "Delay Level", NormalisableRange<float> (kDelayOffDb, 0.0f), kDelayOffDb, "dB",
                            [] (float v, int) { return formatDb (v, kDelayOffDb); }));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::crossfeed, 1 }, "Crossfeed",
                                                        StringArray { "True Stereo", "No Crossfeed", "Mono In" }, 0));

    // ---- multi-band decay
    layout.add (floatParam (ParamID::lowFreq, "Low Freq", logRange (80.0f, 4800.0f), 400.0f, "Hz", hz));
    layout.add (floatParam (ParamID::lowMult, "Low Mult", logRange (0.2f, 4.0f), 1.0f, "x", mult));
    layout.add (floatParam (ParamID::highFreq, "High Freq", logRange (200.0f, 16000.0f), 4000.0f, "Hz", hz));
    layout.add (floatParam (ParamID::highMult, "High Mult", logRange (0.2f, 4.0f), 1.0f, "x", mult));

    // ---- extras: modulation, smooth, ducker
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::modMode, 1 }, "Mod Mode",
                                                        StringArray { "Vibrato", "Chorus", "Drift (most natural)", "Drift + Chorus" },
                                                        (int) ModMode::Drift));
    layout.add (floatParam (ParamID::modDepth, "Mod Depth", NormalisableRange<float> (0.0f, 100.0f), 0.0f, "%", pct));
    layout.add (floatParam (ParamID::modRate, "Mod Rate", logRange (0.05f, 4.0f), 0.3f, "Hz",
                            [] (float v, int) { return String (v, 2) + " Hz"; }));
    layout.add (floatParam (ParamID::smooth, "Smooth", NormalisableRange<float> (0.0f, 100.0f), 0.0f, "%",
                            [] (float v, int) { return v < 0.5f ? String ("Off") : String (roundToInt (v)) + " %"; }));
    layout.add (floatParam (ParamID::duckAmount, "Duck Amount", NormalisableRange<float> (0.0f, 40.0f), 0.0f, "dB",
                            [] (float v, int) { return v < 0.05f ? String ("Off") : "-" + String (v, 1) + " dB"; }));
    layout.add (floatParam (ParamID::duckThreshold, "Duck Threshold", NormalisableRange<float> (-60.0f, 0.0f), -30.0f, "dB", db));
    layout.add (floatParam (ParamID::duckRelease, "Duck Release", skewedRange (20.0f, 3000.0f, 300.0f), 300.0f, "ms", ms));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::duckSidechain, 1 }, "Duck Sidechain", false));

    // ---- post EQ
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::eqOn, 1 }, "EQ On", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::eqHpOn, 1 }, "EQ HP On", false));
    layout.add (floatParam (ParamID::eqHpFreq, "EQ HP Freq", logRange (20.0f, 2000.0f), 80.0f, "Hz", hz));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::eqLowType, 1 }, "EQ Low Type", StringArray { "Shelf", "Bell" }, 0));
    layout.add (floatParam (ParamID::eqLowFreq, "EQ Low Freq", logRange (20.0f, 1000.0f), 200.0f, "Hz", hz));
    layout.add (floatParam (ParamID::eqLowGain, "EQ Low Gain", NormalisableRange<float> (-18.0f, 18.0f), 0.0f, "dB", db));
    layout.add (floatParam (ParamID::eqLowQ, "EQ Low Q", logRange (0.3f, 6.0f), 0.71f, "", [] (float v, int) { return String (v, 2); }));
    layout.add (floatParam (ParamID::eqMidFreq, "EQ Mid Freq", logRange (100.0f, 10000.0f), 1000.0f, "Hz", hz));
    layout.add (floatParam (ParamID::eqMidGain, "EQ Mid Gain", NormalisableRange<float> (-18.0f, 18.0f), 0.0f, "dB", db));
    layout.add (floatParam (ParamID::eqMidQ, "EQ Mid Q", logRange (0.3f, 6.0f), 1.0f, "", [] (float v, int) { return String (v, 2); }));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::eqHighType, 1 }, "EQ High Type", StringArray { "Shelf", "Bell" }, 0));
    layout.add (floatParam (ParamID::eqHighFreq, "EQ High Freq", logRange (1000.0f, 20000.0f), 6000.0f, "Hz", hz));
    layout.add (floatParam (ParamID::eqHighGain, "EQ High Gain", NormalisableRange<float> (-18.0f, 18.0f), 0.0f, "dB", db));
    layout.add (floatParam (ParamID::eqHighQ, "EQ High Q", logRange (0.3f, 6.0f), 0.71f, "", [] (float v, int) { return String (v, 2); }));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::eqLpOn, 1 }, "EQ LP On", false));
    layout.add (floatParam (ParamID::eqLpFreq, "EQ LP Freq", logRange (1000.0f, 22000.0f), 12000.0f, "Hz", hz));

    // ---- direct sound
    layout.add (floatParam (ParamID::directHighs, "Direct Highs", NormalisableRange<float> (DirectSettings::kHighsMinDb, DirectSettings::kHighsMaxDb),
                            0.0f, "dB", db));
    layout.add (floatParam (ParamID::directAir, "Direct Air", NormalisableRange<float> (0.0f, DirectSettings::kAirMaxDb), 0.0f, "dB",
                            [] (float v, int) { return v < 0.05f ? String ("Off") : "-" + String (v, 1) + " dB"; }));
    layout.add (floatParam (ParamID::directWidth, "Direct Width", NormalisableRange<float> (0.0f, 100.0f), 100.0f, "%", pct));
    layout.add (floatParam (ParamID::directDelay, "Direct Delay", skewedRange (0.0f, DirectSettings::kDelayMaxMs, 30.0f), 0.0f, "ms", ms));
    layout.add (floatParam (ParamID::directGap, "Reverb Gap", skewedRange (0.0f, DirectSettings::kGapMaxMs, 20.0f), 0.0f, "ms", ms));

    // ---- space: hall model and distance mode
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::hallOn, 1 }, "Hall On", false));
    layout.add (floatParam (ParamID::hallVolume, "Hall Volume", logRange (150.0f, 50000.0f), 16000.0f, "m3",
                            [] (float v, int) { return formatVolume (v); }));
    layout.add (floatParam (ParamID::hallAbsorption, "Hall Absorption", skewedRange (0.03f, 0.9f, 0.27f), 0.27f, "",
                            [] (float v, int) { return formatAbsorption (v); }));
    auto metres = [] (float v, int) { return formatMetres (v); };
    auto metresOrOff = [] (float v, int) { return v <= 0.0f ? String ("Off") : formatMetres (v); };
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::distOn, 1 }, "Distance On", false));
    layout.add (floatParam (ParamID::distance, "Distance", logRange (0.5f, 60.0f), 8.0f, "m", metres));
    // optional, off at the start of the knob: the real width of the source, the distance of a spot microphone
    layout.add (floatParam (ParamID::distWidth, "Source Size", logRangeWithOff (0.2f, 40.0f), 0.0f, "m", metresOrOff));
    layout.add (floatParam (ParamID::distMic, "Mic Distance", logRangeWithOff (0.1f, 10.0f), 0.0f, "m", metresOrOff));

    return layout;
}

bool isIRParameter (const String& id)
{
    static const StringArray irParams {
        ParamID::predelay, ParamID::predelaySync, ParamID::predelayNote, ParamID::irOffset, ParamID::decay,
        ParamID::early, ParamID::late, ParamID::vlf, ParamID::erRolloff, ParamID::lateRolloff,
        ParamID::erPattern, ParamID::delayTime, ParamID::delaySync, ParamID::delayNote,
        ParamID::delayLevel, ParamID::lowFreq, ParamID::lowMult, ParamID::highFreq, ParamID::highMult,
        ParamID::smooth, ParamID::hallOn, ParamID::hallVolume, ParamID::hallAbsorption
    };
    return irParams.contains (id);
}
} // namespace concert7
