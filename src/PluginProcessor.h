// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>
#include <atomic>
#include <memory>
#include "Parameters.h"
#include "SharedState.h"
#include "dsp/ConvolutionEngine.h"
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

    /** Selects a library preset. userAction = true resets decay to the preset's native time. */
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

private:
    class Builder;
    friend class Builder;

    void parameterChanged (const juce::String& id, float value) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;
    void requestRebuild();
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
    juce::AudioBuffer<float> wet, dry;
    juce::SmoothedValue<float> mixSmooth, gainSmooth;

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
