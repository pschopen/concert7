// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>

namespace concert7
{
using namespace juce;

/** Background thread that (re)builds impulse responses whenever something changed. */
class Concert7Processor::Builder : public Thread
{
public:
    explicit Builder (Concert7Processor& p) : Thread ("Concert7 IR builder"), owner (p) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            wake.wait (40);
            if (threadShouldExit()) break;
            if (owner.rebuildRequested.load())
            {
                // set "building" before clearing the request so observers never see both false mid-way
                owner.building = true;
                owner.rebuildRequested = false;
                owner.buildNow();
            }
        }
    }

    void trigger() { wake.signal(); }

private:
    Concert7Processor& owner;
    WaitableEvent wake;
};

//==============================================================================
Concert7Processor::Concert7Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "Concert7", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            apvts.addParameterListener (withId->paramID, this);

    history = std::make_unique<EditHistory> (*this);
    shared->addChangeListener (this);
    presetId = shared->getDefaultPresetId();
    userPresetChange = true; // first preset starts at its native decay

    builder = std::make_unique<Builder> (*this);
    builder->startThread (Thread::Priority::normal);
}

Concert7Processor::~Concert7Processor()
{
    cancelPendingUpdate();
    shared->removeChangeListener (this);
    abortBuild = true;
    builder->signalThreadShouldExit();
    builder->trigger();
    builder->stopThread (5000);
    history.reset();
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            apvts.removeParameterListener (withId->paramID, this);
}

//==============================================================================
bool Concert7Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo()) return false;
    if (in != AudioChannelSet::stereo() && in != AudioChannelSet::mono()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != AudioChannelSet::stereo() && sc != AudioChannelSet::mono())
            return false;
    }
    return true;
}

void Concert7Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    abortBuild = true;
    {
        std::lock_guard<std::mutex> lk (buildMutex);
        abortBuild = false;
        prepared = false;

        const int block = std::max (16, samplesPerBlock);
        currentSampleRate = sampleRate;
        currentBlockSize = block;
        engine.prepare (sampleRate, block);
        eq.prepare (sampleRate);
        modulator.prepare (sampleRate);
        tailModulator.prepare (sampleRate);
        ducker.prepare (sampleRate);
        distanceRenderer.prepare (sampleRate, block);
        wet.setSize (2, block);
        wetLate.setSize (4, block);
        dry.setSize (4, block); // 0/1 dry input, 2/3 convolver input
        dryGainSmooth.reset (sampleRate, 0.03);
        wetGainSmooth.reset (sampleRate, 0.03);
        gainSmooth.reset (sampleRate, 0.03);
        dryGainSmooth.setCurrentAndTargetValue (1.0f - param (ParamID::mix) * 0.01f);
        wetGainSmooth.setCurrentAndTargetValue (param (ParamID::mix) * 0.01f);
        gainSmooth.setCurrentAndTargetValue (Decibels::decibelsToGain (param (ParamID::gain)));
        lastBuiltRate = 0.0;
        prepared = true;
    }

    // build synchronously so the very first block already has the reverb (offline bounces!)
    rebuildRequested = false;
    buildNow();
    setLatencySamples (0);
}

void Concert7Processor::releaseResources() {}

double Concert7Processor::getTailLengthSeconds() const
{
    const auto direct = getDistanceState (false).direct;
    const double extra = 0.001 * (direct.delayMs + direct.gapMs);
    if (auto ir = engine.getActiveIR())
        return ir->length() / std::max (1.0, ir->sampleRate) + extra;
    return 2.0 + extra;
}

//==============================================================================
void Concert7Processor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);

    if (! prepared || mainOut.getNumChannels() < 2)
    {
        return;
    }

    if (n > wet.getNumSamples())
    {
        // hosts should not exceed the prepared size, but some do
        wet.setSize (2, n, false, false, true);
        wetLate.setSize (4, n, false, false, true);
        dry.setSize (4, n, false, false, true);
    }

    // tempo for synced pre-delay / delay
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm())
                if (*hostBpm > 1.0 && std::abs (*hostBpm - bpm.load()) > 0.01)
                {
                    bpm = *hostBpm;
                    if (param (ParamID::predelaySync) > 0.5f || param (ParamID::delaySync) > 0.5f)
                        rebuildRequested = true;
                }

    // offline rendering: wait for pending IR work instead of rendering a dry gap
    if (isNonRealtime())
    {
        for (int guard = 0; guard < 5000 && (rebuildRequested.load() || building.load()) && ! engine.hasHandover(); ++guard)
        {
            builder->trigger();
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    const int numIn = mainIn.getNumChannels();
    const float* inL = numIn > 0 ? mainIn.getReadPointer (0) : nullptr;
    const float* inR = numIn > 1 ? mainIn.getReadPointer (1) : inL;

    float* dryL = dry.getWritePointer (0);
    float* dryR = dry.getWritePointer (1);
    float* sendL = dry.getWritePointer (2);
    float* sendR = dry.getWritePointer (3);
    if (inL != nullptr)
    {
        FloatVectorOperations::copy (dryL, inL, n);
        FloatVectorOperations::copy (dryR, inR, n);
    }
    else
    {
        FloatVectorOperations::clear (dryL, n);
        FloatVectorOperations::clear (dryR, n);
    }
    inMeterL.push (dryL, n);
    inMeterR.push (dryR, n);

    const auto crossfeed = (Crossfeed) roundToInt (param (ParamID::crossfeed));
    if (crossfeed == Crossfeed::MonoIn)
    {
        for (int i = 0; i < n; ++i)
            sendL[i] = sendR[i] = 0.5f * (dryL[i] + dryR[i]);
    }
    else
    {
        FloatVectorOperations::copy (sendL, dryL, n);
        FloatVectorOperations::copy (sendR, dryR, n);
    }

    float* wetL = wet.getWritePointer (0);
    float* wetR = wet.getWritePointer (1);
    WetStreams streams;
    streams.ch = { wetL, wetR, wetLate.getWritePointer (0), wetLate.getWritePointer (1),
                   wetLate.getWritePointer (2), wetLate.getWritePointer (3) };
    engine.process (sendL, sendR, streams, n, crossfeed != Crossfeed::NoCrossfeed);

    // modulation: Vibrato moves the whole reverb, the other modes only the late reverb (split off in the IR)
    const auto modMode = (ModMode) roundToInt (param (ParamID::modMode));
    const float modDepth = param (ParamID::modDepth), modRate = param (ParamID::modRate);
    const bool drift = modMode == ModMode::Drift || modMode == ModMode::DriftChorus;
    const float chorus = modMode == ModMode::Chorus ? modDepth : (modMode == ModMode::DriftChorus ? 0.5f * modDepth : 0.0f);
    tailModulator.process (wetL, wetR, streams.l (1), streams.r (1), streams.l (2), streams.r (2), n,
                           drift ? modDepth : 0.0f, chorus, modRate);
    modulator.process (wetL, wetR, n, modMode == ModMode::Vibrato ? modDepth : 0.0f, modRate);

    eq.setSettings (readEQSettings());
    eq.process (wetL, wetR, n);

    // ducking keyed by the input or the sidechain bus
    const float* keyL = dryL;
    const float* keyR = dryR;
    if (param (ParamID::duckSidechain) > 0.5f && getBusCount (true) > 1)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            keyL = sc.getReadPointer (0);
            keyR = sc.getReadPointer (std::min (1, sc.getNumChannels() - 1));
        }
    }
    ducker.process (keyL, keyR, wetL, wetR, n, param (ParamID::duckAmount), param (ParamID::duckThreshold),
                    param (ParamID::duckRelease));
    wetMeter.push (wetL, n);
    wetMeter.push (wetR, n);

    // direct sound (tone, air, width, delay) and the reverb gap; in distance mode they follow the distance
    const auto direct = getDistanceState (false).direct;
    distanceRenderer.process (dryL, dryR, wetL, wetR, n, direct);
    const float dryTarget = 1.0f - direct.mix;
    const float wetTarget = direct.mix;

    // mix + output gain
    dryGainSmooth.setTargetValue (dryTarget);
    wetGainSmooth.setTargetValue (wetTarget);
    gainSmooth.setTargetValue (Decibels::decibelsToGain (param (ParamID::gain)));
    float* outL = mainOut.getWritePointer (0);
    float* outR = mainOut.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float a = dryGainSmooth.getNextValue();
        const float b = wetGainSmooth.getNextValue();
        const float g = gainSmooth.getNextValue();
        outL[i] = (dryL[i] * a + wetL[i] * b) * g;
        outR[i] = (dryR[i] * a + wetR[i] * b) * g;
    }
    outMeterL.push (outL, n);
    outMeterR.push (outR, n);

    for (int ch = 2; ch < mainOut.getNumChannels(); ++ch)
        mainOut.clear (ch, 0, n);
}

//==============================================================================
float Concert7Processor::getEffectivePredelayMs() const
{
    if (param (ParamID::predelaySync) > 0.5f)
    {
        const int note = jlimit (0, kNumNoteValues - 1, roundToInt (param (ParamID::predelayNote)));
        return (float) jlimit (0.0, 2000.0, kNoteValues[note].quarters * 60000.0 / std::max (20.0, bpm.load()));
    }
    return param (ParamID::predelay);
}

float Concert7Processor::getEffectiveDelayMs() const
{
    if (param (ParamID::delaySync) > 0.5f)
    {
        const int note = jlimit (0, kNumNoteValues - 1, roundToInt (param (ParamID::delayNote)));
        return (float) jlimit (20.0, 2000.0, kNoteValues[note].quarters * 60000.0 / std::max (20.0, bpm.load()));
    }
    return param (ParamID::delayTime);
}

float Concert7Processor::getHallVolume() const
{
    if (! isHallOn()) return estimateHallVolume (getEffectiveDecay());
    const float pending = pendingHallVolume.load();
    return pending > 0.0f ? pending : param (ParamID::hallVolume);
}

float Concert7Processor::getHallAbsorption() const
{
    if (! isHallOn()) return eyringAbsorption (getHallVolume(), getEffectiveDecay());
    const float pending = pendingHallAbsorption.load();
    return pending > 0.0f ? pending : param (ParamID::hallAbsorption);
}

float Concert7Processor::getEffectiveDecay() const
{
    if (! isHallOn())
    {
        const float pending = pendingDecay.load();
        return pending > 0.0f ? pending : param (ParamID::decay);
    }
    const auto range = apvts.getParameter (ParamID::decay)->getNormalisableRange();
    return jlimit (range.start, range.end, eyringRt60 (getHallVolume(), getHallAbsorption()));
}

DistanceModel Concert7Processor::makeDistanceModel (bool distanceOn) const
{
    DistanceModel m;
    m.rt60 = getEffectiveDecay();
    m.volume = getHallVolume();
    m.micDistance = param (ParamID::distMic);
    m.sourceWidth = param (ParamID::distWidth);
    if (distanceOn)
    {
        const float pending = pendingDistance.load();
        m.distance = pending > 0.0f ? pending : param (ParamID::distance);
        m.referenceDistance = distanceReference.load();
    }
    else
    {
        m.distance = m.referenceDistance = computeReferenceDistance (m.rt60, m.volume);
    }
    return m;
}

DirectSettings Concert7Processor::readDirectSettings() const
{
    DirectSettings d;
    if (handbackPending.load())
    {
        d.mix = pendingDirect[0]; d.highsDb = pendingDirect[1]; d.airDb = pendingDirect[2];
        d.width = pendingDirect[3]; d.delayMs = pendingDirect[4]; d.gapMs = pendingDirect[5];
        return d;
    }
    d.mix = param (ParamID::mix) * 0.01f;
    d.highsDb = param (ParamID::directHighs);
    d.airDb = param (ParamID::directAir);
    d.width = param (ParamID::directWidth) * 0.01f;
    d.delayMs = param (ParamID::directDelay);
    d.gapMs = param (ParamID::directGap);
    return d;
}

Concert7Processor::DistanceState Concert7Processor::makeDistanceState (bool distanceOn, bool withReadouts) const
{
    DistanceState ds;
    ds.model = makeDistanceModel (distanceOn);
    ds.result = computeDistance (ds.model);
    ds.direct = readDirectSettings();
    if (distanceOn)
        ds.direct = applyDistance (ds.direct, ds.result);
    if (! withReadouts) return ds;

    ds.firstReflectionMs = irOnsetMs.load() + ds.direct.gapMs;
    const auto crossfeed = (Crossfeed) roundToInt (param (ParamID::crossfeed));
    const float energy = crossfeed == Crossfeed::NoCrossfeed ? irMidEnergyDiagonal.load() : irMidEnergy.load();
    const float mix = ds.direct.mix;
    ds.criticalDistance = ds.result.criticalDistance;
    if (mix >= 1.0f)
        ds.drrDb = -99.0f;
    else if (mix <= 0.0f || energy <= 1.0e-9f)
        ds.drrDb = 99.0f;
    else
    {
        ds.drrDb = Decibels::gainToDecibels ((1.0f - mix) / mix) - 10.0f * std::log10 (energy);
        // the ratio follows the model's slope, offset by the preset's own balance
        const float offset = ds.drrDb - barronDrrDb (ds.model.distance, ds.model.rt60, ds.result.volume);
        ds.criticalDistance = distanceForDrr (-offset, ds.model.rt60, ds.result.volume);
    }
    return ds;
}

float Concert7Processor::computeReferenceDistance (float rt60, float volume) const
{
    // the distance at which the model gives the direct to reverberant ratio of the current mix
    const float mix = param (ParamID::mix) * 0.01f;
    const auto range = apvts.getParameter (ParamID::distance)->getNormalisableRange();
    if (mix <= 0.001f) return range.start;
    if (mix >= 0.999f) return range.end;
    const auto crossfeed = (Crossfeed) roundToInt (param (ParamID::crossfeed));
    const float energy = std::max (1.0e-9f, crossfeed == Crossfeed::NoCrossfeed ? irMidEnergyDiagonal.load() : irMidEnergy.load());
    const float drr = Decibels::gainToDecibels ((1.0f - mix) / mix) - 10.0f * std::log10 (energy);
    return jlimit (range.start, range.end, distanceForDrr (drr, rt60, volume));
}

DesignParams Concert7Processor::readDesignParams() const
{
    DesignParams d;
    d.decay = getEffectiveDecay();
    d.predelayMs = getEffectivePredelayMs();
    d.offsetMs = param (ParamID::irOffset);
    d.earlyDb = param (ParamID::early);
    d.lateDb = param (ParamID::late);
    d.vlfDb = param (ParamID::vlf);
    d.erRolloffHz = param (ParamID::erRolloff);
    d.lateRolloffHz = param (ParamID::lateRolloff);
    d.delayMs = getEffectiveDelayMs();
    d.delayDb = param (ParamID::delayLevel);
    d.lowFreq = param (ParamID::lowFreq);
    d.lowMult = param (ParamID::lowMult);
    d.highFreq = param (ParamID::highFreq);
    d.highMult = param (ParamID::highMult);
    d.smooth = param (ParamID::smooth) * 0.01f;
    const int layout = lateLayout();
    d.splitLate = layout >= 1;
    d.lateVariant = layout >= 2;
    return d;
}

int Concert7Processor::lateLayout() const
{
    if (param (ParamID::modDepth) <= 0.0f) return 0;
    switch ((ModMode) roundToInt (param (ParamID::modMode)))
    {
        case ModMode::Chorus:      return 1;
        case ModMode::Drift:
        case ModMode::DriftChorus: return 2;
        case ModMode::Vibrato:     break;
    }
    return 0;
}

EQSettings Concert7Processor::readEQSettings() const
{
    EQSettings s;
    s.on = param (ParamID::eqOn) > 0.5f;
    s.hpOn = param (ParamID::eqHpOn) > 0.5f;
    s.hpFreq = param (ParamID::eqHpFreq);
    s.lowType = roundToInt (param (ParamID::eqLowType));
    s.lowFreq = param (ParamID::eqLowFreq);
    s.lowGain = param (ParamID::eqLowGain);
    s.lowQ = param (ParamID::eqLowQ);
    s.midFreq = param (ParamID::eqMidFreq);
    s.midGain = param (ParamID::eqMidGain);
    s.midQ = param (ParamID::eqMidQ);
    s.highType = roundToInt (param (ParamID::eqHighType));
    s.highFreq = param (ParamID::eqHighFreq);
    s.highGain = param (ParamID::eqHighGain);
    s.highQ = param (ParamID::eqHighQ);
    s.lpOn = param (ParamID::eqLpOn) > 0.5f;
    s.lpFreq = param (ParamID::eqLpFreq);
    return s;
}

//==============================================================================
void Concert7Processor::requestRebuild()
{
    rebuildRequested = true;
    if (builder != nullptr)
        builder->trigger();
}

void Concert7Processor::buildNow()
{
    std::lock_guard<std::mutex> lk (buildMutex);
    if (! prepared) return;

    building = true;
    struct Reset { std::atomic<bool>& b; ~Reset() { b = false; } } reset { building };

    if (sourcesInvalid.exchange (false))
    {
        source.reset();
        patternSource.reset();
        recentSources.clear();
        recentBuilds.clear();
        lastBuiltPreset.clear();
    }

    const double sr = currentSampleRate.load();
    auto setStatusMessage = [this] (const String& m, bool loading) {
        { const ScopedLock sl (statusLock); status.message = m; status.loading = loading; }
        triggerAsyncUpdate();
    };

    String id;
    { const ScopedLock sl (statusLock); id = presetId; }
    if (id.isEmpty() || shared->indexOfPreset (id) < 0)
    {
        const auto fallback = shared->getDefaultPresetId();
        if (fallback.isEmpty())
        {
            setStatusMessage (shared->getIRFolder() == File() ? "No IR folder - see setup" : "No impulse responses found", false);
            return;
        }
        if (id.isNotEmpty() && ! shared->hasLibrary())
            return;
        if (id.isEmpty()) userPresetChange = true;
        id = fallback;
        const ScopedLock sl (statusLock);
        presetId = id;
    }

    // ---- source impulse response
    if (source == nullptr || source->presetId != id || std::abs (source->sampleRate - sr) > 0.5)
    {
        IRPreset preset;
        if (! shared->getPreset (id, preset))
        {
            setStatusMessage ("Preset not found", false);
            return;
        }
        std::shared_ptr<SourceIR> loaded;
        for (auto& recent : recentSources)
            if (recent->presetId == id && std::abs (recent->sampleRate - sr) < 0.5)
                loaded = recent;
        if (loaded == nullptr)
        {
            setStatusMessage ("Loading...", true);
            String error;
            loaded = loadSourceIR (preset, sr, error);
            if (loaded == nullptr)
            {
                setStatusMessage (error, false);
                return;
            }
        }
        if (source != nullptr)
        {
            recentSources.erase (std::remove (recentSources.begin(), recentSources.end(), loaded), recentSources.end());
            recentSources.erase (std::remove (recentSources.begin(), recentSources.end(), source), recentSources.end());
            recentSources.insert (recentSources.begin(), source);
            if (recentSources.size() > kRecentSources) recentSources.resize (kRecentSources);
        }
        source = loaded;
        const ScopedLock sl (statusLock);
        status.presetName = preset.name;
        status.category = preset.category;
        status.number = preset.displayNumber();
        status.nativeRT = loaded->nativeRT;
        status.fileRate = loaded->fileRate;
        status.trueStereo = loaded->trueStereo;
    }

    // ---- early reflection pattern
    const int pattern = roundToInt (param (ParamID::erPattern));
    const String patternId = pattern > 0 ? shared->getPresetIdForErPattern (pattern) : String();
    if (patternId.isEmpty())
        patternSource.reset();
    else if (patternSource == nullptr || patternSource->presetId != patternId || std::abs (patternSource->sampleRate - sr) > 0.5)
    {
        IRPreset preset;
        String error;
        if (shared->getPreset (patternId, preset))
            patternSource = loadSourceIR (preset, sr, error);
    }

    // ---- parameters
    auto dp = readDesignParams();
    // a newly selected preset starts at its own decay time (the hall model keeps its own)
    const bool presetChanged = userPresetChange.exchange (false);
    if (presetChanged && ! shared->isKept (ParamGroup::Decay) && ! isHallOn())
    {
        // a newly selected preset starts at its own decay time
        auto* decayParam = apvts.getParameter (ParamID::decay);
        const float native = jlimit (0.2f, 30.0f, source->nativeRT);
        dp.decay = decayParam->convertFrom0to1 (decayParam->convertTo0to1 (native));
        { const ScopedLock sl (statusLock); pendingNativeDecay = dp.decay; }
    }

    if (engine.getActiveIR() != nullptr && dp == lastBuiltParams && id == lastBuiltPreset
        && patternId == lastBuiltPattern && std::abs (lastBuiltRate - sr) < 0.5)
    {
        setStatusMessage ({}, false);
        return;
    }

    std::shared_ptr<const BuiltIR> built;
    for (auto& recent : recentBuilds)
        if (recent.presetId == id && recent.patternId == patternId && std::abs (recent.rate - sr) < 0.5 && recent.params == dp)
            built = recent.ir;
    if (built == nullptr)
        built = designIR (*source, patternSource.get(), dp);
    if (! engine.loadIR (built, [this] { return abortBuild.load() || builder->threadShouldExit(); }))
        return;
    if (auto previous = std::find_if (recentBuilds.begin(), recentBuilds.end(), [&] (auto& r) { return r.ir == built; });
        previous != recentBuilds.end())
        recentBuilds.erase (previous);
    recentBuilds.insert (recentBuilds.begin(), { id, patternId, sr, dp, built });
    if (recentBuilds.size() > kRecentBuilds) recentBuilds.resize (kRecentBuilds);

    irMidEnergy = built->midEnergy;
    irMidEnergyDiagonal = built->midEnergyDiagonal;
    irOnsetMs = built->onsetMs;
    lastBuiltParams = dp;
    lastBuiltPreset = id;
    lastBuiltPattern = patternId;
    lastBuiltRate = sr;
    {
        const ScopedLock sl (statusLock);
        status.hasIR = true;
        status.message = {};
        status.loading = false;
    }
    triggerAsyncUpdate();
}

bool Concert7Processor::waitUntilIdle (int timeoutMs)
{
    const auto end = Time::getMillisecondCounter() + (uint32) timeoutMs;
    while (Time::getMillisecondCounter() < end)
    {
        if (! rebuildRequested.load() && ! building.load())
            return true;
        builder->trigger();
        Thread::sleep (2);
    }
    return false;
}

void Concert7Processor::handleAsyncUpdate()
{
    float pending = -1.0f;
    {
        const ScopedLock sl (statusLock);
        std::swap (pending, pendingNativeDecay);
    }
    if (pending > 0.0f)
        if (auto* p = apvts.getParameter (ParamID::decay))
            p->setValueNotifyingHost (p->convertTo0to1 (pending));

    // distance mode was switched on: the distance knob starts at the reference
    float distance = pendingDistance.load();
    if (distance > 0.0f)
    {
        if (auto* p = apvts.getParameter (ParamID::distance))
            p->setValueNotifyingHost (p->convertTo0to1 (distance));
        pendingDistance.compare_exchange_strong (distance, -1.0f);
    }
    // distance was switched off: MIX and DIRECT take the values in effect
    if (handbackPending.load())
    {
        const std::pair<const char*, float> values[] = {
            { ParamID::mix, pendingDirect[0] * 100.0f }, { ParamID::directHighs, pendingDirect[1].load() },
            { ParamID::directAir, pendingDirect[2].load() }, { ParamID::directWidth, pendingDirect[3] * 100.0f },
            { ParamID::directDelay, pendingDirect[4].load() }, { ParamID::directGap, pendingDirect[5].load() } };
        for (auto [id, v] : values)
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        handbackPending = false;
    }

    // the hall model was switched off: DECAY keeps the decay the hall gave
    float decay = pendingDecay.load();
    if (decay > 0.0f)
    {
        if (auto* p = apvts.getParameter (ParamID::decay))
            p->setValueNotifyingHost (p->convertTo0to1 (decay));
        pendingDecay.compare_exchange_strong (decay, -1.0f);
    }

    // the hall model was switched on: volume and absorption start where they give the current decay
    float volume = pendingHallVolume.load(), absorption = pendingHallAbsorption.load();
    if (volume > 0.0f && absorption > 0.0f)
    {
        apvts.getParameter (ParamID::hallVolume)->setValueNotifyingHost (apvts.getParameter (ParamID::hallVolume)->convertTo0to1 (volume));
        apvts.getParameter (ParamID::hallAbsorption)->setValueNotifyingHost (apvts.getParameter (ParamID::hallAbsorption)->convertTo0to1 (absorption));
        pendingHallVolume.compare_exchange_strong (volume, -1.0f);
        pendingHallAbsorption.compare_exchange_strong (absorption, -1.0f);
    }
    sendChangeMessage();
}

void Concert7Processor::parameterChanged (const String& id, float value)
{
    if (id == ParamID::distOn && value > 0.5f && ! restoringState.load())
    {
        // start from the unchanged sound: the current mix defines where the source is
        const auto model = getDistanceModel();
        const float reference = computeReferenceDistance (model.rt60, model.volume);
        distanceReference = reference;
        pendingDistance = reference;
        triggerAsyncUpdate();
    }
    else if (id == ParamID::distOn && value < 0.5f && ! restoringState.load())
    {
        // hand the values in effect back to MIX and DIRECT: nothing jumps
        const auto d = makeDistanceState (true, false).direct;
        const float values[] = { d.mix, d.highsDb, d.airDb, d.width, d.delayMs, d.gapMs };
        for (size_t i = 0; i < pendingDirect.size(); ++i)
            pendingDirect[i] = values[i];
        handbackPending = true;
        triggerAsyncUpdate();
    }
    if (id == ParamID::hallOn && value > 0.5f && ! restoringState.load())
    {
        // keep the decay: the estimated volume and the absorption that gives the current decay
        const float decay = param (ParamID::decay);
        const auto absorptionRange = apvts.getParameter (ParamID::hallAbsorption)->getNormalisableRange();
        const float volume = estimateHallVolume (decay);
        pendingHallAbsorption = jlimit (absorptionRange.start, absorptionRange.end, eyringAbsorption (volume, decay));
        pendingHallVolume = volume;
        triggerAsyncUpdate();
    }
    else if (id == ParamID::hallOn && value < 0.5f && ! restoringState.load())
    {
        const auto range = apvts.getParameter (ParamID::decay)->getNormalisableRange();
        pendingDecay = jlimit (range.start, range.end, eyringRt60 (param (ParamID::hallVolume), param (ParamID::hallAbsorption)));
        triggerAsyncUpdate();
    }
    if (isIRParameter (id))
        requestRebuild();
    // the modulation modes of the late reverb need it as a response of its own: rebuild when that changes
    if (id == ParamID::modDepth || id == ParamID::modMode)
    {
        const int layout = lateLayout();
        if (requestedLateLayout.exchange (layout) != layout)
            requestRebuild();
    }
}

void Concert7Processor::changeListenerCallback (ChangeBroadcaster*)
{
    // the library was rescanned: reload the current preset (files may have moved).
    // Never lock buildMutex here: the builder may be waiting for the audio thread.
    // A missing preset falls back to the first one at its native decay; the other settings stay.
    sourcesInvalid = true;
    if (shared->indexOfPreset (getPresetId()) < 0)
    {
        { const ScopedLock sl (statusLock); presetId = shared->getDefaultPresetId(); }
        userPresetChange = true;
    }
    requestRebuild();
    sendChangeMessage();
}

//==============================================================================
void Concert7Processor::selectPreset (const String& id, bool userAction)
{
    const bool undoable = userAction && MessageManager::existsAndIsCurrentThread();
    if (undoable)
    {
        IRPreset p;
        history->beginAction (shared->getPreset (id, p) ? p.category + " " + p.displayNumber() + " " + p.name : id);
    }
    {
        const ScopedLock sl (statusLock);
        presetId = id;
    }
    if (userAction)
    {
        resetParameters();
        userPresetChange = true;
        setUserPreset ({}, {}, {}, 0);
    }
    requestRebuild();
    sendChangeMessage();
    if (undoable)
        history->endAction();
}

bool Concert7Processor::hasPendingHandover() const
{
    {
        const ScopedLock sl (statusLock);
        if (pendingNativeDecay > 0.0f) return true;
    }
    return (userPresetChange.load() && prepared.load() && shared->hasLibrary()) || pendingDistance.load() > 0.0f
           || handbackPending.load() || pendingDecay.load() > 0.0f || pendingHallVolume.load() > 0.0f || isUpdatePending();
}

void Concert7Processor::restoreValues (const std::vector<std::pair<RangedAudioParameter*, float>>& values,
                                       const String& id, float reference, const String& userKey)
{
    if (userKey != getUserPresetKey())
    {
        const auto* entry = shared->getUserPresets().find (userKey);
        setUserPreset (userKey, entry != nullptr ? entry->name : String(),
                       entry != nullptr ? shared->getUserPresets().load (userKey) : ValueTree(), 0);
    }
    pendingDistance = -1.0f;
    pendingHallVolume = -1.0f;
    pendingHallAbsorption = -1.0f;
    pendingDecay = -1.0f;
    handbackPending = false;
    restoringState = true;
    for (auto [p, v] : values)
        if (p->getValue() != v)
            p->setValueNotifyingHost (v);
    restoringState = false;
    distanceReference = reference;
    {
        const ScopedLock sl (statusLock);
        presetId = id;
        pendingNativeDecay = -1.0f;
    }
    userPresetChange = false;
    requestRebuild();
    sendChangeMessage();
}

void Concert7Processor::selectABSlot (int slot)
{
    slot = jlimit (0, 1, slot);
    if (slot == abSlot) return;
    auto current = createStateTree();
    // the first time the other slot starts as a copy; each slot has its own undo history
    const auto target = abSlots[(size_t) slot].isValid() ? abSlots[(size_t) slot] : current;
    history->flush();
    applyStateTree (target);
    abSlots[(size_t) abSlot] = current;
    abSlots[(size_t) slot] = {};
    abSlot = slot;
    history->useSlot (slot);
    sendChangeMessage();
}

void Concert7Processor::copyToOtherABSlot()
{
    abSlots[(size_t) (1 - abSlot)] = createStateTree();
}

void Concert7Processor::resetParameters()
{
    // every control returns to its default unless its group is kept; DECAY follows the preset in the next
    // build (or keeps the decay in effect, the hall's included, when it is kept)
    applyParameters (ValueTree (apvts.state.getType()), shared->getKeptGroups(), true);
}

void Concert7Processor::applyParameters (const ValueTree& tree, GroupMask keep, bool skipDecay)
{
    const bool keepSpace = (keep & groupBit (ParamGroup::Space)) != 0;
    // a kept DECAY or MIX keeps the value in effect when the mode that drives it is switched off here ...
    const float decayInEffect = (keep & groupBit (ParamGroup::Decay)) != 0 && ! keepSpace && isHallOn()
                                    ? getEffectiveDecay() : -1.0f;
    const float mixInEffect = (keep & groupBit (ParamGroup::Output)) != 0 && ! keepSpace && isDistanceOn()
                                  ? makeDistanceState (true, false).direct.mix * 100.0f : -1.0f;
    // ... and a kept distance mode keeps the MIX it moves
    const bool keepMix = keepSpace && isDistanceOn();
    const bool hasModMode = tree.getChildWithProperty ("id", ParamID::modMode).isValid() || tree.getNumChildren() == 0;

    // nothing switched on or off here may hand values over afterwards
    pendingDistance = -1.0f;
    pendingHallVolume = -1.0f;
    pendingHallAbsorption = -1.0f;
    pendingDecay = -1.0f;
    handbackPending = false;
    restoringState = true;
    auto setTo = [] (RangedAudioParameter& rp, float value) {
        if (rp.getValue() != value) rp.setValueNotifyingHost (value);
    };
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
        {
            if ((keep & groupBit (groupOf (rp->paramID))) != 0) continue;
            if ((skipDecay && rp->paramID == ParamID::decay) || (keepMix && rp->paramID == ParamID::mix)) continue;
            const auto child = tree.getChildWithProperty ("id", rp->paramID);
            float value = child.isValid() ? rp->convertTo0to1 ((float) child.getProperty ("value"))
                                          : rp->getDefaultValue();
            // projects from before the modulation modes keep their vibrato
            if (rp->paramID == ParamID::modMode && ! hasModMode)
                value = rp->convertTo0to1 ((float) ModMode::Vibrato);
            setTo (*rp, value);
        }
    if (decayInEffect > 0.0f)
        if (auto* p = apvts.getParameter (ParamID::decay))
            setTo (*p, p->convertTo0to1 (decayInEffect));
    if (mixInEffect >= 0.0f)
        if (auto* p = apvts.getParameter (ParamID::mix))
            setTo (*p, p->convertTo0to1 (mixInEffect));
    restoringState = false;
}

void Concert7Processor::selectPresetByOffset (int delta)
{
    const int num = shared->getNumPresets();
    if (num == 0) return;
    int idx = shared->indexOfPreset (getPresetId());
    idx = ((idx < 0 ? 0 : idx + delta) % num + num) % num;
    IRPreset p;
    if (shared->getPresetAt (idx, p))
        selectPreset (p.id, true);
}

String Concert7Processor::getPresetId() const
{
    const ScopedLock sl (statusLock);
    return presetId;
}

Concert7Processor::Status Concert7Processor::getStatus() const
{
    const ScopedLock sl (statusLock);
    auto s = status;
    s.loading = s.loading || rebuildRequested.load() || building.load();
    if (notice.isNotEmpty() && Time::getMillisecondCounter() < noticeUntil && s.message.isEmpty())
        s.message = notice;
    s.userPresetName = userPresetName;
    if (userPresetName.isNotEmpty() && ! userPresetValues.empty())
    {
        s.userPresetModified = presetId != userPresetIR;
        const auto& params = getParameters();
        for (size_t i = 0; i < userPresetValues.size() && i < (size_t) params.size() && ! s.userPresetModified; ++i)
            if (! std::isnan (userPresetValues[i]) && std::abs (params[(int) i]->getValue() - userPresetValues[i]) > 1.0e-5f)
                s.userPresetModified = true;
    }
    return s;
}

//==============================================================================
void Concert7Processor::setUserPreset (const String& key, const String& name, const ValueTree& presetState,
                                       GroupMask notCompared)
{
    std::vector<float> values;
    if (presetState.isValid())
        for (auto* prm : getParameters())
        {
            auto* rp = dynamic_cast<RangedAudioParameter*> (prm);
            const auto child = rp != nullptr ? presetState.getChildWithProperty ("id", rp->paramID) : ValueTree();
            if (rp == nullptr || (notCompared & groupBit (groupOf (rp->paramID))) != 0)
                values.push_back (std::numeric_limits<float>::quiet_NaN());
            else
                values.push_back (child.isValid() ? rp->convertTo0to1 ((float) child.getProperty ("value")) : rp->getDefaultValue());
        }
    const ScopedLock sl (statusLock);
    userPresetKey = key;
    userPresetName = key.isNotEmpty() ? name : String();
    userPresetIR = presetState.getProperty ("presetId").toString();
    userPresetValues = std::move (values);
}

String Concert7Processor::getUserPresetKey() const
{
    const ScopedLock sl (statusLock);
    return userPresetKey;
}

void Concert7Processor::userPresetRenamed (const String& oldKey, const String& newKey)
{
    if (getUserPresetKey() != oldKey) return;
    if (newKey.isEmpty())
    {
        setUserPreset ({}, {}, {}, 0);
        return;
    }
    const ScopedLock sl (statusLock);
    userPresetKey = newKey;
    if (auto* e = shared->getUserPresets().find (newKey))
        userPresetName = e->name;
}

bool Concert7Processor::loadUserPreset (const String& key)
{
    auto& presets = shared->getUserPresets();
    const auto tree = presets.load (key);
    const auto* entry = presets.find (key);
    if (! tree.isValid() || entry == nullptr) return false;

    const auto irId = tree.getProperty ("presetId").toString();
    const bool missing = irId.isNotEmpty() && shared->indexOfPreset (irId) < 0;
    const auto kept = shared->getKeptGroups();
    history->beginAction (entry->name);
    applyStateTree (tree, kept);
    setUserPreset (key, entry->name, tree, kept);
    if (missing)
    {
        const ScopedLock sl (statusLock);
        notice = "IR not found: " + irId.fromLastOccurrenceOf ("/", false, false);
        noticeUntil = Time::getMillisecondCounter() + 5000;
    }
    history->endAction();
    sendChangeMessage();
    return true;
}

String Concert7Processor::saveUserPreset (const String& name, const StringArray& tags, const String& notes, String& error)
{
    auto tree = createStateTree();
    tree.removeProperty ("userPreset", nullptr);
    tree.removeProperty ("userPresetName", nullptr);
    const auto key = shared->getUserPresets().save (name, tags, notes, tree, error);
    if (const auto* entry = shared->getUserPresets().find (key))
        setUserPreset (key, entry->name, tree, 0);
    return key;
}

//==============================================================================
int Concert7Processor::getNumPrograms()
{
    return std::max (1, shared->getNumPresets());
}

int Concert7Processor::getCurrentProgram()
{
    return std::max (0, shared->indexOfPreset (getPresetId()));
}

void Concert7Processor::setCurrentProgram (int index)
{
    IRPreset p;
    if (shared->getPresetAt (index, p) && p.id != getPresetId())
        selectPreset (p.id, true);
}

const String Concert7Processor::getProgramName (int index)
{
    IRPreset p;
    if (shared->getPresetAt (index, p))
        return p.category + " " + p.displayNumber() + " " + p.name;
    return "Default";
}

ValueTree Concert7Processor::createStateTree()
{
    auto state = apvts.copyState();
    state.setProperty ("presetId", getPresetId(), nullptr);
    state.setProperty ("version", CONCERT7_VERSION, nullptr);
    state.setProperty ("distanceReference", distanceReference.load(), nullptr);
    {
        const ScopedLock sl (statusLock);
        if (userPresetKey.isNotEmpty())
        {
            state.setProperty ("userPreset", userPresetKey, nullptr);
            state.setProperty ("userPresetName", userPresetName, nullptr);
        }
    }
    return state;
}

void Concert7Processor::applyStateTree (const ValueTree& tree, GroupMask keep)
{
    const auto id = tree.getProperty ("presetId").toString();
    if ((keep & groupBit (ParamGroup::Space)) == 0)
        distanceReference = (float) tree.getProperty ("distanceReference", 8.0);
    applyParameters (tree, keep, false);
    {
        const ScopedLock sl (statusLock);
        presetId = id.isNotEmpty() ? id : shared->getDefaultPresetId();
        pendingNativeDecay = -1.0f; // a build that ran before the restore must not override it
    }
    userPresetChange = false; // keep the stored decay
    // the user preset it was made from (shown with its name, and marked when changed)
    const auto userKey = tree.getProperty ("userPreset").toString();
    const auto userName = tree.getProperty ("userPresetName").toString();
    if (MessageManager::existsAndIsCurrentThread() && userKey.isNotEmpty())
        setUserPreset (userKey, userName, shared->getUserPresets().load (userKey), 0);
    else
        setUserPreset (userKey, userName, {}, 0);
    requestRebuild();
    sendChangeMessage();
}

void Concert7Processor::getStateInformation (MemoryBlock& destData)
{
    auto state = createStateTree();
    state.setProperty ("abSlot", abSlot, nullptr);
    if (auto other = abSlots[(size_t) (1 - abSlot)]; other.isValid())
    {
        ValueTree slot ("OtherABSlot");
        slot.appendChild (other.createCopy(), nullptr);
        state.appendChild (slot, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Concert7Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = ValueTree::fromXml (*xml);
            abSlot = jlimit (0, 1, (int) tree.getProperty ("abSlot", 0));
            if (MessageManager::existsAndIsCurrentThread())
                history->useSlot (abSlot);
            abSlots = {};
            const auto other = tree.getChildWithName ("OtherABSlot");
            if (other.getNumChildren() > 0)
                abSlots[(size_t) (1 - abSlot)] = other.getChild (0).createCopy();
            applyStateTree (tree);
        }
}

AudioProcessorEditor* Concert7Processor::createEditor()
{
    return new Concert7Editor (*this);
}
} // namespace concert7

#if ! CONCERT7_DEVTOOL
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new concert7::Concert7Processor();
}
#endif
