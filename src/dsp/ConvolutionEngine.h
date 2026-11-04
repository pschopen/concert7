// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <functional>
#include <vector>
#include <memory>
#include "Convolver.h"
#include "IRDesigner.h"

namespace concert7
{
/** Four zero latency partitioned convolvers forming a true stereo reverb. */
class TrueStereoConvolver
{
public:
    TrueStereoConvolver();

    /** Not real-time safe: allocates. Must not run while the audio thread uses this object. */
    void load (std::shared_ptr<const BuiltIR> ir, size_t headBlock, size_t tailBlock);
    void unload();

    /** Real-time safe. scratch must hold 4 * numSamples floats. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  bool crossfeed, float* scratch);

    bool isLoaded() const { return loaded; }
    bool isTrueStereo() const { return trueStereo; }
    std::shared_ptr<const BuiltIR> getIR() const { return ir; }

private:
    std::array<std::unique_ptr<Convolver>, 4> conv;
    std::shared_ptr<const BuiltIR> ir;
    bool loaded = false;
    bool trueStereo = false;
};

/**
    Owns an active and a pending convolver. A background thread loads a new IR into
    the pending convolver and warms it up with the most recent input (kept in a
    ring buffer) so that the new reverb tail is already "ringing". The audio thread
    then crossfades from the active to the pending convolver.

    Threading: prepare()/process() are called from the audio side (prepare never
    concurrently with process). loadIR() is called from a single worker thread.
*/
class ConvolutionEngine
{
public:
    ConvolutionEngine();
    ~ConvolutionEngine();

    void prepare (double sampleRate, int maxBlockSize);

    /** Worker thread: blocks until the engine can take a new IR (or timeout).
        Returns false if it gave up (e.g. shouldAbort returned true). */
    bool loadIR (std::shared_ptr<const BuiltIR> ir, const std::function<bool()>& shouldAbort);

    /** Audio thread. Inputs may alias each other. Outputs are overwritten. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples, bool crossfeed);

    std::shared_ptr<const BuiltIR> getActiveIR() const;
    bool isCrossfading() const { return state.load() == Fading; }
    bool isIdle() const { return state.load() == Idle; }
    /** True when a new IR is ready for, or being crossfaded by, the audio thread. */
    bool hasHandover() const { const int s = state.load(); return s == Ready || s == Fading; }
    int getCrossfadeSamples() const { return crossfadeLength; }

    static constexpr double kCrossfadeSeconds = 0.08;
    static constexpr double kHistorySeconds   = 8.0;
    static constexpr double kWarmupSeconds    = 3.0;

private:
    enum State { Idle, Preparing, Ready, Fading };

    struct FeedBuffers
    {
        void resize (int n) { l.assign ((size_t) n, 0.0f); r = ol = orr = l; sc.assign ((size_t) n * 4, 0.0f); }
        std::vector<float> l, r, ol, orr, sc;
    };
    void feedHistory (TrueStereoConvolver& c, juce::int64 from, juce::int64 to, bool crossfeed, FeedBuffers& buf);

    std::unique_ptr<TrueStereoConvolver> active, pending;
    std::atomic<int> state { Idle };
    std::atomic<bool> lastCrossfeed { true };

    // input history for warming up the pending convolver
    juce::AudioBuffer<float> history;
    std::atomic<juce::int64> written { 0 };
    juce::int64 pendingFedUpTo = 0;

    std::vector<float> scratch, xfL, xfR;
    FeedBuffers audioFeed; // used by the audio thread only
    double sampleRate = 44100.0;
    int maxBlock = 512;
    size_t headBlock = 256, tailBlock = 8192;
    int crossfadeLength = 1, crossfadePos = 0;

    mutable juce::SpinLock irLock;
    std::shared_ptr<const BuiltIR> activeIR;
    std::shared_ptr<const BuiltIR> pendingIR;
};
} // namespace concert7
