// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>
#include <array>
#include <atomic>
#include <memory>
#include "Parameters.h"
#include "SharedState.h"
#include "dsp/ConvolutionEngine.h"
#include "dsp/Distance.h"
#include "dsp/IRDesigner.h"
#include "dsp/PostProcessing.h"

namespace concert7
{
class Concert7Processor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::ChangeListener,
                          private juce::AsyncUpdater
{
public:
    Concert7Processor();
    ~Concert7Processor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return kPluginName; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    SharedState& getShared() { return *shared; }

    /** Selects a library preset. userAction = true (also when the current preset is chosen again)
        resets all parameters to their defaults and decay to the preset's native time. */
    void selectPreset (const juce::String& presetId, bool userAction);
    void selectPresetByOffset (int delta);
    juce::String getPresetId() const;

    /** Status for the user interface. */
    struct Status
    {
        juce::String presetName, category, number;
        juce::String message;         // errors / loading info
        bool loading = false;
        bool hasIR = false;
        float nativeRT = 0.0f;
        int fileRate = 0;
        bool trueStereo = false;
    };
    Status getStatus() const;
    std::shared_ptr<const BuiltIR> getCurrentIR() const { return engine.getActiveIR(); }

    PeakMeter inMeterL, inMeterR, outMeterL, outMeterR, wetMeter;
    float getDuckReductionDb() const { return ducker.getGainReductionDb(); }
    double getHostBpm() const { return bpm.load(); }
    double getSampleRateForUI() const { return currentSampleRate.load(); }

    /** Waits until all pending IR work is done (tests / offline rendering). */
    bool waitUntilIdle (int timeoutMs);

    /** Effective pre-delay and delay times in ms (tempo sync resolved). */
    float getEffectivePredelayMs() const;
    float getEffectiveDelayMs() const;

    /** Hall model: volume and absorption drive the decay (Eyring). Switching it on keeps the decay.
        While it is off, the volume is estimated from the decay and the absorption follows from both. */
    bool isHallOn() const { return param (ParamID::hallOn) > 0.5f; }
    float getEffectiveDecay() const;
    float getHallVolume() const;
    float getHallAbsorption() const;

    /** Distance mode. Switching it on keeps the sound: the current mix becomes the reference distance.
        While it is on, MIX and the DIRECT settings follow the distance (relative to their values);
        switching it off hands the values in effect back to them. While it is off, the model sits at
        the reference (the distance the current mix corresponds to). */
    bool isDistanceOn() const { return param (ParamID::distOn) > 0.5f; }
    DistanceModel getDistanceModel() const { return makeDistanceModel (isDistanceOn()); }

    struct DistanceState
    {
        DistanceModel model;
        DistanceResult result;
        DirectSettings direct;                 // in effect: MIX and DIRECT, moved by the distance when on
        float drrDb = 0.0f;                    // resulting direct to reverberant ratio (mid band)
        float criticalDistance = 0.0f;         // where that ratio crosses 0 dB
        float firstReflectionMs = 0.0f;        // gap between direct sound and first reflection
    };
    /** withReadouts = false skips the values only the user interface needs. */
    DistanceState getDistanceState (bool withReadouts = true) const { return makeDistanceState (isDistanceOn(), withReadouts); }

private:
    class Builder;
    friend class Builder;

    void parameterChanged (const juce::String& id, float value) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;
    void requestRebuild();
    void resetParameters();
    void buildNow();
    DesignParams readDesignParams() const;
    EQSettings readEQSettings() const;
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::SharedResourcePointer<SharedState> shared;
    juce::AudioProcessorValueTreeState apvts;

    ConvolutionEngine engine;
    PostEQ eq;
    Modulator modulator;
    Ducker ducker;
    DistanceRenderer distanceRenderer;
    juce::AudioBuffer<float> wet, dry;
    juce::SmoothedValue<float> dryGainSmooth, wetGainSmooth, gainSmooth;
    std::atomic<float> irMidEnergy { 0.0f }, irMidEnergyDiagonal { 0.0f }, irOnsetMs { 0.0f };  // of the last built IR
    std::atomic<float> distanceReference { 8.0f };   // m, stored with the project
    std::atomic<float> pendingDistance { -1.0f };    // reference to be written to the distance parameter
    std::atomic<float> pendingHallVolume { -1.0f }, pendingHallAbsorption { -1.0f };  // written when the hall is switched on
    std::atomic<float> pendingDecay { -1.0f };       // the hall's decay, written to DECAY when the hall is switched off
    std::atomic<bool> restoringState { false };
    float computeReferenceDistance (float rt60, float volume) const;
    DistanceModel makeDistanceModel (bool distanceOn) const;
    DistanceState makeDistanceState (bool distanceOn, bool withReadouts) const;
    DirectSettings readDirectSettings() const;   // MIX and DIRECT (or the values being handed back)
    std::array<std::atomic<float>, 6> pendingDirect {};  // values handed back when distance is switched off
    std::atomic<bool> handbackPending { false };

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> prepared { false };

    // builder state
    std::unique_ptr<Builder> builder;
    std::mutex buildMutex;             // held while building; prepareToPlay takes it too
    std::atomic<bool> abortBuild { false };
    std::atomic<bool> rebuildRequested { false };
    std::atomic<bool> building { false };
    std::atomic<bool> userPresetChange { false };
    std::atomic<bool> sourcesInvalid { false };   // library rescanned: reload files

    mutable juce::CriticalSection statusLock;
    juce::String presetId;
    Status status;
    std::shared_ptr<SourceIR> source, patternSource;
    DesignParams lastBuiltParams;
    juce::String lastBuiltPreset, lastBuiltPattern;
    double lastBuiltRate = 0.0;
    float pendingNativeDecay = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Concert7Processor)
};
} // namespace concert7
