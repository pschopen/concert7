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
        ducker.prepare (sampleRate);
        wet.setSize (2, block);
        dry.setSize (4, block); // 0/1 dry input, 2/3 convolver input
        mixSmooth.reset (sampleRate, 0.03);
        gainSmooth.reset (sampleRate, 0.03);
        mixSmooth.setCurrentAndTargetValue (param (ParamID::mix) * 0.01f);
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
    if (auto ir = engine.getActiveIR())
        return ir->length() / std::max (1.0, ir->sampleRate);
    return 2.0;
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
    engine.process (sendL, sendR, wetL, wetR, n, crossfeed != Crossfeed::NoCrossfeed);

    modulator.process (wetL, wetR, n, param (ParamID::modDepth), param (ParamID::modRate));

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

    // mix + output gain
    mixSmooth.setTargetValue (param (ParamID::mix) * 0.01f);
    gainSmooth.setTargetValue (Decibels::decibelsToGain (param (ParamID::gain)));
    float* outL = mainOut.getWritePointer (0);
    float* outR = mainOut.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float m = mixSmooth.getNextValue();
        const float g = gainSmooth.getNextValue();
        outL[i] = (dryL[i] * (1.0f - m) + wetL[i] * m) * g;
        outR[i] = (dryR[i] * (1.0f - m) + wetR[i] * m) * g;
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

DesignParams Concert7Processor::readDesignParams() const
{
    DesignParams d;
    d.decay = param (ParamID::decay);
    d.predelayMs = getEffectivePredelayMs();
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
    return d;
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
        setStatusMessage ("Loading...", true);
        String error;
        auto loaded = loadSourceIR (preset, sr, error);
        if (loaded == nullptr)
        {
            setStatusMessage (error, false);
            return;
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
    if (userPresetChange.exchange (false) && ! shared->getHoldDecay())
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

    auto built = designIR (*source, patternSource.get(), dp);
    if (! engine.loadIR (built, [this] { return abortBuild.load() || builder->threadShouldExit(); }))
        return;

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
    sendChangeMessage();
}

void Concert7Processor::parameterChanged (const String& id, float)
{
    if (isIRParameter (id))
        requestRebuild();
}

void Concert7Processor::changeListenerCallback (ChangeBroadcaster*)
{
    // the library was rescanned: reload the current preset (files may have moved).
    // Never lock buildMutex here: the builder may be waiting for the audio thread.
    sourcesInvalid = true;
    if (shared->indexOfPreset (getPresetId()) < 0)
        selectPreset (shared->getDefaultPresetId(), true);
    else
        requestRebuild();
    sendChangeMessage();
}

//==============================================================================
void Concert7Processor::selectPreset (const String& id, bool userAction)
{
    {
        const ScopedLock sl (statusLock);
        presetId = id;
    }
    if (userAction) userPresetChange = true;
    requestRebuild();
    sendChangeMessage();
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
    return s;
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

void Concert7Processor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetId", getPresetId(), nullptr);
    state.setProperty ("version", CONCERT7_VERSION, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Concert7Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName (apvts.state.getType())) return;
        auto tree = ValueTree::fromXml (*xml);
        const auto id = tree.getProperty ("presetId").toString();
        apvts.replaceState (tree);
        {
            const ScopedLock sl (statusLock);
            presetId = id.isNotEmpty() ? id : shared->getDefaultPresetId();
            pendingNativeDecay = -1.0f; // a build that ran before the restore must not override it
        }
        userPresetChange = false; // keep the stored decay
        requestRebuild();
        sendChangeMessage();
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
