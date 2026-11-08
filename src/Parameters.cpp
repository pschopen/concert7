// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "Parameters.h"
#include "dsp/Distance.h"
#include <map>

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
    /** A typed value: the number and what follows it, e.g. "2,5 s" -> 2.5 and "s". Spaces are ignored
        (also inside numbers, "16 000"), a decimal comma counts as a point. ok is false without a number. */
    struct TypedValue
    {
        bool ok = false;
        double number = 0.0;
        String unit;
        bool is (const char* u) const { return unit == u; }
        bool kilo() const { return unit.startsWithChar ('k'); }
    };

    TypedValue parseTyped (const String& text)
    {
        TypedValue t;
        auto s = text.toLowerCase().removeCharacters (" \t'").replaceCharacter (',', '.');
        int i = 0;
        if (s[0] == '+' || s[0] == '-') ++i;
        const int digitsStart = i;
        while (CharacterFunctions::isDigit (s[i]) || s[i] == '.') ++i;
        if (i == digitsStart) return t;
        t.ok = true;
        t.number = s.substring (0, i).getDoubleValue();
        t.unit = s.substring (i);
        return t;
    }

    bool isOffText (const String& text)
    {
        const auto s = text.trim().toLowerCase();
        return s == "off" || s == "aus" || s == "-inf" || s == "inf";
    }

    float parseMs (const String& text)
    {
        const auto t = parseTyped (text);
        return (float) (t.is ("s") || t.is ("sec") ? t.number * 1000.0 : t.number);
    }

    float parseSeconds (const String& text)
    {
        const auto t = parseTyped (text);
        return (float) (t.is ("ms") ? t.number / 1000.0 : t.number);
    }

    float parseHz (const String& text)
    {
        const auto t = parseTyped (text);
        return (float) (t.kilo() ? t.number * 1000.0 : t.number);
    }

    float parseNumber (const String& text) { return (float) parseTyped (text).number; }

    float clampToRange (const NormalisableRange<float>& r, float v)
    {
        return std::isfinite (v) ? r.snapToLegalValue (jlimit (r.start, r.end, v)) : r.start;
    }

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
                                                     std::function<String (float, int)> toText,
                                                     std::function<float (const String&)> fromText)
    {
        auto parse = [range, def, read = std::move (fromText)] (const String& text) {
            return parseTyped (text).ok || isOffText (text) ? clampToRange (range, read (text)) : def;
        };
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes().withLabel (label).withStringFromValueFunction (std::move (toText))
                                           .withValueFromStringFunction (std::move (parse)));
    }
}

StringArray decayModeNames()
{
    return { "Sabine (natural)", "Proportional (brilliant)", "Sabine, constant level", "Stretch (room size)",
             "Original (0.1.0-beta)" };
}

String decayModeTag (int mode)
{
    static const char* tags[] = { "", "PROP", "CONST", "STRETCH", "ORIG" };
    return isPositiveAndBelow (mode, (int) std::size (tags)) ? String (tags[mode]) : String();
}

bool isValueText (const String& text)
{
    const auto s = text.trim().toLowerCase();
    return parseTyped (s).ok || isOffText (s) || s == "preset";
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

    // keyboard / host text entry: units may be typed or left out ("2.5", "2,5 s", "250 ms", "1.2k", "off")
    auto msIn   = [] (const String& t) { return parseMs (t); };
    auto secIn  = [] (const String& t) { return parseSeconds (t); };
    auto hzIn   = [] (const String& t) { return parseHz (t); };
    auto rollIn = [] (const String& t) { return isOffText (t) ? kRolloffBypassHz + 2000.0f : parseHz (t); };
    auto numIn  = [] (const String& t) { return parseNumber (t); };
    auto offIn  = [] (float off) { return [off] (const String& t) { return isOffText (t) ? off : parseNumber (t); }; };
    // amounts shown as a cut ("-6.0 dB") may be typed with or without the sign
    auto cutIn  = [] (const String& t) { return isOffText (t) ? 0.0f : std::abs (parseNumber (t)); };
    auto metresIn = [] (const String& t) {
        if (isOffText (t)) return 0.0f;
        const auto v = parseTyped (t);
        return (float) (v.is ("cm") ? v.number / 100.0 : v.number);
    };

    // ---- main
    layout.add (floatParam (ParamID::predelay, "Pre-Delay", skewedRange (0.0f, 500.0f, 80.0f), 0.0f, "ms", ms, msIn));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::predelaySync, 1 }, "Pre-Delay Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::predelayNote, 1 }, "Pre-Delay Note", noteNames(), 4));
    // start of the whole impulse response, finer around 0
    layout.add (floatParam (ParamID::irOffset, "IR Offset", NormalisableRange<float> (-kMaxOffsetMs, kMaxOffsetMs, 0.0f, 0.4f, true), 0.0f, "ms",
                            [] (float v, int) { return formatOffsetMs (v); }, msIn));
    layout.add (floatParam (ParamID::decay, "Decay", skewedRange (0.2f, 30.0f, 3.0f), 2.0f, "s", sec, secIn));
    // how DECAY gets to its reverb time (DecayMode), from the right-click menu of DECAY
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::decayMode, 1 }, "Decay Mode", decayModeNames(), 0,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (floatParam (ParamID::early, "Early", NormalisableRange<float> (kLevelOffDb, 6.0f, 0.0f), 0.0f, "dB",
                            [] (float v, int) { return formatDb (v, kLevelOffDb); }, offIn (kLevelOffDb)));
    layout.add (floatParam (ParamID::late, "Late", NormalisableRange<float> (kLevelOffDb, 6.0f, 0.0f), 0.0f, "dB",
                            [] (float v, int) { return formatDb (v, kLevelOffDb); }, offIn (kLevelOffDb)));
    layout.add (floatParam (ParamID::mix, "Mix", NormalisableRange<float> (0.0f, 100.0f), 30.0f, "%", pct, numIn));
    layout.add (floatParam (ParamID::gain, "Gain", NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", db, numIn));

    // ---- reverb
    layout.add (floatParam (ParamID::vlf, "VLF", NormalisableRange<float> (kVlfOffDb, 0.0f), 0.0f, "dB",
                            [] (float v, int) { return formatDb (v, kVlfOffDb); }, offIn (kVlfOffDb)));
    layout.add (floatParam (ParamID::erRolloff, "Early Roll-Off", logRange (80.0f, 22000.0f), 22000.0f, "Hz", roll, rollIn));
    layout.add (floatParam (ParamID::lateRolloff, "Late Roll-Off", logRange (80.0f, 22000.0f), 22000.0f, "Hz", roll, rollIn));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamID::erPattern, 1 }, "Early Pattern", 0, kNumErPatterns, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? String ("Preset") : String (v); })
                                      .withValueFromStringFunction ([] (const String& t) { return jlimit (0, kNumErPatterns, roundToInt (parseNumber (t))); })));
    layout.add (floatParam (ParamID::delayTime, "Delay Time", skewedRange (100.0f, 1000.0f, 400.0f), 250.0f, "ms", ms, msIn));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delaySync, 1 }, "Delay Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::delayNote, 1 }, "Delay Note", noteNames(), 7));
    layout.add (floatParam (ParamID::delayLevel, "Delay Level", NormalisableRange<float> (kDelayOffDb, 0.0f), kDelayOffDb, "dB",
                            [] (float v, int) { return formatDb (v, kDelayOffDb); }, offIn (kDelayOffDb)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::crossfeed, 1 }, "Crossfeed",
                                                        StringArray { "True Stereo", "No Crossfeed", "Mono In" }, 0));

    // ---- multi-band decay
    layout.add (floatParam (ParamID::lowFreq, "Low Freq", logRange (80.0f, 4800.0f), 400.0f, "Hz", hz, hzIn));
    layout.add (floatParam (ParamID::lowMult, "Low Mult", logRange (0.2f, 4.0f), 1.0f, "x", mult, numIn));
    layout.add (floatParam (ParamID::highFreq, "High Freq", logRange (200.0f, 16000.0f), 4000.0f, "Hz", hz, hzIn));
    layout.add (floatParam (ParamID::highMult, "High Mult", logRange (0.2f, 4.0f), 1.0f, "x", mult, numIn));

    // ---- extras: modulation, smooth, ducker
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::modMode, 1 }, "Mod Mode",
                                                        StringArray { "Vibrato", "Chorus", "Drift (most natural)", "Drift + Chorus" },
                                                        (int) ModMode::Drift));
    layout.add (floatParam (ParamID::modDepth, "Mod Depth", NormalisableRange<float> (0.0f, 100.0f), 0.0f, "%", pct, numIn));
    layout.add (floatParam (ParamID::modRate, "Mod Rate", logRange (0.05f, 4.0f), 0.3f, "Hz",
                            [] (float v, int) { return String (v, 2) + " Hz"; }, hzIn));
    layout.add (floatParam (ParamID::smooth, "Smooth", NormalisableRange<float> (0.0f, 100.0f), 0.0f, "%",
                            [] (float v, int) { return v < 0.5f ? String ("Off") : String (roundToInt (v)) + " %"; }, offIn (0.0f)));
    layout.add (floatParam (ParamID::duckAmount, "Duck Amount", NormalisableRange<float> (0.0f, 40.0f), 0.0f, "dB",
                            [] (float v, int) { return v < 0.05f ? String ("Off") : "-" + String (v, 1) + " dB"; }, cutIn));
    layout.add (floatParam (ParamID::duckThreshold, "Duck Threshold", NormalisableRange<float> (-60.0f, 0.0f), -30.0f, "dB", db, numIn));
    layout.add (floatParam (ParamID::duckRelease, "Duck Release", skewedRange (20.0f, 3000.0f, 300.0f), 300.0f, "ms", ms, msIn));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::duckSidechain, 1 }, "Duck Sidechain", false));

    // ---- split captures: the takes (TakeMode, same order) and the late block against the reflections
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::takeMode, 1 }, "Take Mode",
                                                        StringArray { "Off", "Late", "Early + Late" }, 0));
    layout.add (floatParam (ParamID::takeRate, "Take Rate", logRange (0.05f, 2.0f), 0.3f, "Hz",
                            [] (float v, int) { return String (v, 2) + " Hz"; }, hzIn));
    layout.add (floatParam (ParamID::lateOffset, "Late Offset",
                            NormalisableRange<float> (-kMaxLateOffsetMs, kMaxLateOffsetMs, 0.0f, 0.5f, true), 0.0f, "ms",
                            [] (float v, int) { return formatOffsetMs (v); }, msIn));

    // ---- post EQ
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::eqOn, 1 }, "EQ On", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::eqHpOn, 1 }, "EQ HP On", false));
    layout.add (floatParam (ParamID::eqHpFreq, "EQ HP Freq", logRange (20.0f, 2000.0f), 80.0f, "Hz", hz, hzIn));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::eqLowType, 1 }, "EQ Low Type", StringArray { "Shelf", "Bell" }, 0));
    layout.add (floatParam (ParamID::eqLowFreq, "EQ Low Freq", logRange (20.0f, 1000.0f), 200.0f, "Hz", hz, hzIn));
    layout.add (floatParam (ParamID::eqLowGain, "EQ Low Gain", NormalisableRange<float> (-18.0f, 18.0f), 0.0f, "dB", db, numIn));
    layout.add (floatParam (ParamID::eqLowQ, "EQ Low Q", logRange (0.3f, 6.0f), 0.71f, "", [] (float v, int) { return String (v, 2); }, numIn));
    layout.add (floatParam (ParamID::eqMidFreq, "EQ Mid Freq", logRange (100.0f, 10000.0f), 1000.0f, "Hz", hz, hzIn));
    layout.add (floatParam (ParamID::eqMidGain, "EQ Mid Gain", NormalisableRange<float> (-18.0f, 18.0f), 0.0f, "dB", db, numIn));
    layout.add (floatParam (ParamID::eqMidQ, "EQ Mid Q", logRange (0.3f, 6.0f), 1.0f, "", [] (float v, int) { return String (v, 2); }, numIn));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::eqHighType, 1 }, "EQ High Type", StringArray { "Shelf", "Bell" }, 0));
    layout.add (floatParam (ParamID::eqHighFreq, "EQ High Freq", logRange (1000.0f, 20000.0f), 6000.0f, "Hz", hz, hzIn));
    layout.add (floatParam (ParamID::eqHighGain, "EQ High Gain", NormalisableRange<float> (-18.0f, 18.0f), 0.0f, "dB", db, numIn));
    layout.add (floatParam (ParamID::eqHighQ, "EQ High Q", logRange (0.3f, 6.0f), 0.71f, "", [] (float v, int) { return String (v, 2); }, numIn));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::eqLpOn, 1 }, "EQ LP On", false));
    layout.add (floatParam (ParamID::eqLpFreq, "EQ LP Freq", logRange (1000.0f, 22000.0f), 12000.0f, "Hz", hz, hzIn));

    // ---- direct sound
    layout.add (floatParam (ParamID::directHighs, "Direct Highs", NormalisableRange<float> (DirectSettings::kHighsMinDb, DirectSettings::kHighsMaxDb),
                            0.0f, "dB", db, numIn));
    layout.add (floatParam (ParamID::directAir, "Direct Air", NormalisableRange<float> (0.0f, DirectSettings::kAirMaxDb), 0.0f, "dB",
                            [] (float v, int) { return v < 0.05f ? String ("Off") : "-" + String (v, 1) + " dB"; }, cutIn));
    layout.add (floatParam (ParamID::directWidth, "Direct Width", NormalisableRange<float> (0.0f, 100.0f), 100.0f, "%", pct, numIn));
    layout.add (floatParam (ParamID::directDelay, "Direct Delay", skewedRange (0.0f, DirectSettings::kDelayMaxMs, 30.0f), 0.0f, "ms", ms, msIn));
    layout.add (floatParam (ParamID::directGap, "Reverb Gap", skewedRange (0.0f, DirectSettings::kGapMaxMs, 20.0f), 0.0f, "ms", ms, msIn));

    // ---- space: hall model and distance mode
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::hallOn, 1 }, "Hall On", false));
    layout.add (floatParam (ParamID::hallVolume, "Hall Volume", logRange (150.0f, 50000.0f), 16000.0f, "m3",
                            [] (float v, int) { return formatVolume (v); },
                            [] (const String& t) { const auto v = parseTyped (t); return (float) (v.kilo() ? v.number * 1000.0 : v.number); }));
    layout.add (floatParam (ParamID::hallAbsorption, "Hall Absorption", skewedRange (0.03f, 0.9f, 0.27f), 0.27f, "",
                            [] (float v, int) { return formatAbsorption (v); }, numIn));
    auto metres = [] (float v, int) { return formatMetres (v); };
    auto metresOrOff = [] (float v, int) { return v <= 0.0f ? String ("Off") : formatMetres (v); };
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::distOn, 1 }, "Distance On", false));
    layout.add (floatParam (ParamID::distance, "Distance", logRange (0.5f, 60.0f), 8.0f, "m", metres, metresIn));
    // optional, off at the start of the knob: the real width of the source, the distance of a spot microphone
    layout.add (floatParam (ParamID::distWidth, "Source Size", logRangeWithOff (0.2f, 40.0f), 0.0f, "m", metresOrOff, metresIn));
    layout.add (floatParam (ParamID::distMic, "Mic Distance", logRangeWithOff (0.1f, 10.0f), 0.0f, "m", metresOrOff, metresIn));

    return layout;
}

ParamGroup groupOf (const String& id)
{
    using G = ParamGroup;
    static const std::map<String, G> groups {
        { ParamID::decay, G::Decay }, { ParamID::decayMode, G::Decay },
        { ParamID::predelay, G::Predelay }, { ParamID::predelaySync, G::Predelay }, { ParamID::predelayNote, G::Predelay },
        { ParamID::irOffset, G::Predelay },
        { ParamID::early, G::Levels }, { ParamID::late, G::Levels }, { ParamID::vlf, G::Levels },
        { ParamID::mix, G::Output }, { ParamID::gain, G::Output },
        { ParamID::erRolloff, G::Character }, { ParamID::lateRolloff, G::Character }, { ParamID::erPattern, G::Character },
        { ParamID::crossfeed, G::Character },
        { ParamID::lowFreq, G::MultiBand }, { ParamID::lowMult, G::MultiBand }, { ParamID::highFreq, G::MultiBand },
        { ParamID::highMult, G::MultiBand },
        { ParamID::delayTime, G::Delay }, { ParamID::delaySync, G::Delay }, { ParamID::delayNote, G::Delay },
        { ParamID::delayLevel, G::Delay },
        { ParamID::modMode, G::Modulation }, { ParamID::modDepth, G::Modulation }, { ParamID::modRate, G::Modulation },
        { ParamID::smooth, G::Modulation }, { ParamID::takeMode, G::Modulation }, { ParamID::takeRate, G::Modulation },
        { ParamID::lateOffset, G::Predelay },
        { ParamID::duckAmount, G::Ducker }, { ParamID::duckThreshold, G::Ducker }, { ParamID::duckRelease, G::Ducker },
        { ParamID::duckSidechain, G::Ducker },
        { ParamID::directHighs, G::Space }, { ParamID::directAir, G::Space }, { ParamID::directWidth, G::Space },
        { ParamID::directDelay, G::Space }, { ParamID::directGap, G::Space },
        { ParamID::hallOn, G::Space }, { ParamID::hallVolume, G::Space }, { ParamID::hallAbsorption, G::Space },
        { ParamID::distOn, G::Space }, { ParamID::distance, G::Space }, { ParamID::distMic, G::Space },
        { ParamID::distWidth, G::Space },
    };
    const auto it = groups.find (id);
    if (it != groups.end()) return it->second;
    jassert (id.startsWith ("eq"));
    return G::EQ;
}

const char* groupKey (ParamGroup g)
{
    static const char* keys[] = { "decay", "predelay", "levels", "output", "character", "multiband", "delay",
                                  "modulation", "ducker", "eq", "space" };
    static_assert (std::size (keys) == (size_t) kNumParamGroups);
    return keys[(size_t) g];
}

String groupName (ParamGroup g)
{
    static const char* names[] = { "Decay", "Pre-delay and offset", "Early, late and VLF levels", "Mix and gain",
                                   "Roll-off, early pattern, stereo", "Multi-band decay", "Delay",
                                   "Modulation and smooth", "Ducker", "Post EQ", "Space and direct sound" };
    static_assert (std::size (names) == (size_t) kNumParamGroups);
    return names[(size_t) g];
}

bool isIRParameter (const String& id)
{
    static const StringArray irParams {
        ParamID::predelay, ParamID::predelaySync, ParamID::predelayNote, ParamID::irOffset, ParamID::decay,
        ParamID::decayMode, ParamID::early, ParamID::late, ParamID::vlf, ParamID::erRolloff, ParamID::lateRolloff,
        ParamID::erPattern, ParamID::delayTime, ParamID::delaySync, ParamID::delayNote,
        ParamID::delayLevel, ParamID::lowFreq, ParamID::lowMult, ParamID::highFreq, ParamID::highMult,
        ParamID::smooth, ParamID::hallOn, ParamID::hallVolume, ParamID::hallAbsorption, ParamID::takeMode,
        ParamID::lateOffset
    };
    return irParams.contains (id);
}
} // namespace concert7
