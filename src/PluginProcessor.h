// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>
#include <array>
#include <atomic>
#include <memory>
#include "EditHistory.h"
#include "Parameters.h"
#include "SharedState.h"
#include "dsp/ConvolutionEngine.h"
#include "dsp/Distance.h"
#include "dsp/Halfband.h"
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

    /** The whole sound: all parameters, the impulse response (presetId) and the distance reference.
        The project, user presets, A/B and undo are made of it. */
    juce::ValueTree createStateTree();
    /** Sets the sound from a tree of createStateTree() (message thread). Parameters it lacks go to their
        defaults, parameters in the kept groups stay as they are. DECAY is taken from the tree, not from the
        impulse response. */
    void applyStateTree (const juce::ValueTree& tree, GroupMask keep = 0);

    //==========================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    SharedState& getShared() { return *shared; }

    /** Selects a library preset. userAction = true (also when the current preset is chosen again)
        resets all parameters to their defaults and decay to the preset's native time. */
    void selectPreset (const juce::String& presetId, bool userAction);
    /** Steps through the presets of the library in use. */
    void selectPresetByOffset (int delta);
    juce::String getPresetId() const;

    /** The library in use (SAM / LS / OTHER): the one of the current preset. */
    IROrigin getLibrary() const;
    /** Switches to the same preset in the other M7 library, else to the first of its category there, else to the
        library's first. Like choosing a preset: one undo step, its own settings and decay except for the kept
        groups. False when that library is not installed. Message thread. */
    bool switchLibrary (IROrigin target);

    /** Undo / redo of the user's edits (kept while the editor is closed, not stored with the project). */
    EditHistory& getHistory() { return *history; }
    /** True while the processor still has values to hand over after an edit (decay of a new preset, the
        values the hall model or the distance mode write when switched). */
    bool hasPendingHandover() const;
    /** Undo / redo: sets parameters (0..1) and the impulse response as they were. Nothing is handed over,
        DECAY is not taken from the preset. Message thread. */
    void restoreValues (const std::vector<std::pair<juce::RangedAudioParameter*, float>>& values,
                        const juce::String& presetId, float distanceReference, const juce::String& userPresetKey);
    float getDistanceReference() const { return distanceReference.load(); }

    /** User presets: the whole sound in a file (see UserPresetLibrary). Loading keeps the kept groups and is
        one undo step. Message thread. */
    bool loadUserPreset (const juce::String& key);
    /** Saves the current sound as a user preset (replacing one of that name); returns its key or empty. */
    juce::String saveUserPreset (const juce::String& name, const juce::StringArray& tags, const juce::String& notes,
                                 juce::String& error);
    /** The user preset in use, empty when a library preset is (or was changed to). */
    juce::String getUserPresetKey() const;
    /** After the user preset was renamed or deleted in the browser. */
    void userPresetRenamed (const juce::String& oldKey, const juce::String& newKey);

    /** A/B comparison: two complete settings (impulse response included), both stored with the project.
        Choosing the other slot keeps the current one in its slot. */
    int getABSlot() const { return abSlot; }
    void selectABSlot (int slot);
    /** Copies the current settings into the other slot. */
    void copyToOtherABSlot();

    /** Status for the user interface. */
    struct Status
    {
        juce::String presetName, category, number;   // number with the bank: "2.05" is Halls 2, preset 5
        IROrigin origin = IROrigin::Combined;
        juce::String message;         // errors / loading info
        bool loading = false;
        bool hasIR = false;
        float nativeRT = 0.0f;
        int fileRate = 0;
        bool trueStereo = false;
        juce::String userPresetName;   // a user preset is loaded
        bool userPresetModified = false;
        bool lowCpu = false, halfRate = false;   // as prepared
        int decayMode = 0;                       // DecayMode
    };
    Status getStatus() const;
    std::shared_ptr<const BuiltIR> getCurrentIR() const { return engine.getActiveIR(); }

    PeakMeter inMeterL, inMeterR, outMeterL, outMeterR, wetMeter;
    float getDuckReductionDb() const { return ducker.getGainReductionDb(); }
    double getHostBpm() const { return bpm.load(); }
    double getSampleRateForUI() const { return currentSampleRate.load(); }

    /** Low CPU: everything runs in blocks of kLowCpuBlock samples, so the convolution can use large blocks.
        Adds that much latency (reported to the host). Stored with the project. */
    static constexpr int kLowCpuBlock = 4096;
    bool isLowCpu() const { return lowCpu.load(); }
    void setLowCpu (bool on);
    /** Under-sampling: at 88.2 kHz and above the reverb is convolved at half the sample rate (the impulse
        responses hold nothing above 22 / 24 kHz). Stored with the project. */
    bool isUnderSampling() const { return underSampling.load(); }
    void setUnderSampling (bool on);
    bool isUnderSamplingActive() const { return underSamplingActive.load(); }
    /** Silent input longer than the reverb pauses the convolution (CPU). */
    bool isConvolutionPaused() const { return convolutionPaused.load(); }

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
    /** The convolution of n host rate samples: under-sampled when active, paused after a long silence. */
    void convolve (const float* inL, const float* inR, const WetStreams& out, int n, bool crossfeed);
    void requestRebuild();
    void resetParameters();
    void applyParameters (const juce::ValueTree& tree, GroupMask keep, bool skipDecay);
    void buildNow();
    DesignParams readDesignParams() const;
    int lateLayout() const;            // which streams the modulation needs: 0 none, 1 late, 2 late + variant
    std::atomic<int> requestedLateLayout { -1 };
    EQSettings readEQSettings() const;
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::SharedResourcePointer<SharedState> shared;
    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<EditHistory> history;
    std::array<juce::ValueTree, 2> abSlots;   // the slot not in use (the one in use is the current state)
    int abSlot = 0;

    ConvolutionEngine engine;
    PostEQ eq;
    Modulator modulator;
    TailModulator tailModulator;
    Ducker ducker;
    DistanceRenderer distanceRenderer;
    juce::AudioBuffer<float> wet, dry, wetLate;   // wetLate: late reverb and its variant (modulation)
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

    std::atomic<bool> lowCpu { false }, underSampling { false }, modesChanged { false };
    // as prepared (audio side)
    std::atomic<bool> lowCpuActive { false }, underSamplingActive { false }, convolutionPaused { false };
    std::atomic<double> convolutionRate { 0.0 };   // the rate of the impulse responses (half the rate: under-sampling)
    int hostBlockSize = 512;
    // low CPU: convolver input and output (one chunk late), dry signal and sidechain delayed by a chunk
    juce::AudioBuffer<float> chunkIn, chunkOut, delayLine;
    int chunkPos = 0;
    juce::AudioBuffer<float> halfSend, halfWet;     // under-sampling: convolver input and streams at half the rate
    std::array<HalfbandDecimator, 2> decimators;
    std::array<HalfbandInterpolator, 2 * WetStreams::kNumStreams> interpolators;
    juce::int64 silentSamples = 0;                  // input below kSilence for this long (host samples)
    std::atomic<juce::int64> silenceLimit { std::numeric_limits<juce::int64>::max() };  // ... pauses the convolution
    static constexpr float kSilence = 1.0e-5f;      // -100 dB

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
    IROrigin library = IROrigin::Combined;
    /** Sets presetId and with it the library (statusLock held). */
    void setPresetIdLocked (const juce::String& id);
    // the user preset in use and its values (0..1, NaN = not compared: a kept group) to show changes
    juce::String userPresetKey, userPresetName, userPresetIR;
    std::vector<float> userPresetValues;
    /** Without presetState nothing is compared (the preset file is not at hand). */
    void setUserPreset (const juce::String& key, const juce::String& name, const juce::ValueTree& presetState,
                        GroupMask notCompared);
    juce::String notice;                 // shown for a while instead of the readout
    juce::uint32 noticeUntil = 0;
    Status status;
    std::shared_ptr<SourceIR> source, patternSource;
    // the impulse responses used last: switching back (A/B, undo, the previous preset) skips loading and design
    struct RecentBuild
    {
        juce::String presetId, patternId;
        double rate = 0.0;
        DesignParams params;
        std::shared_ptr<const BuiltIR> ir;
    };
    std::vector<std::shared_ptr<SourceIR>> recentSources;
    std::vector<RecentBuild> recentBuilds;
    static constexpr size_t kRecentSources = 3, kRecentBuilds = 2;
    DesignParams lastBuiltParams;
    juce::String lastBuiltPreset, lastBuiltPattern;
    double lastBuiltRate = 0.0;
    float pendingNativeDecay = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Concert7Processor)
};
} // namespace concert7
