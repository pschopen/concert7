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
/** Output of the convolution, one stereo pair per stream: the response (only its early part when the late
    reverb is split off, see BuiltIR::late), the late reverb and its variant. Streams the response does not
    have come out silent. */
struct WetStreams
{
    static constexpr int kNumStreams = 3;   // main, late, variant
    std::array<float*, 2 * kNumStreams> ch {};

    float* l (int stream) const { return ch[(size_t) (2 * stream)]; }
    float* r (int stream) const { return ch[(size_t) (2 * stream + 1)]; }
    WetStreams operator+ (int offset) const
    {
        WetStreams s;
        for (size_t i = 0; i < ch.size(); ++i) s.ch[i] = ch[i] + offset;
        return s;
    }
};

/** Four zero latency partitioned convolvers forming a true stereo reverb, per stream of the response. */
class TrueStereoConvolver
{
public:
    TrueStereoConvolver();

    /** Not real-time safe: allocates. Must not run while the audio thread uses this object. */
    void load (std::shared_ptr<const BuiltIR> ir, size_t headBlock, size_t tailBlock);
    void unload();

    /** Real-time safe. scratch must hold 4 * numSamples floats. */
    void process (const float* inL, const float* inR, const WetStreams& out, int numSamples,
                  bool crossfeed, float* scratch);

    bool isLoaded() const { return loaded; }
    bool isTrueStereo() const { return trueStereo; }
    std::shared_ptr<const BuiltIR> getIR() const { return ir; }

private:
    using Set = std::array<std::unique_ptr<Convolver>, 4>;
    void processSet (Set& set, const float* inL, const float* inR, float* outL, float* outR, int n,
                     bool crossfeed, float* scratch);

    std::array<Set, WetStreams::kNumStreams> sets;   // late and variant are created when first needed
    std::array<bool, WetStreams::kNumStreams> used {};
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
    void process (const float* inL, const float* inR, const WetStreams& out, int numSamples, bool crossfeed);

    /** Audio thread. The streams summed without modulation: the whole response (variants are left out). */
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
        void resize (int n)
        {
            l.assign ((size_t) n, 0.0f);
            r = l;
            sc.assign ((size_t) n * 4, 0.0f);
            out.assign ((size_t) n * 2 * WetStreams::kNumStreams, 0.0f);
            for (size_t i = 0; i < streams.ch.size(); ++i)
                streams.ch[i] = out.data() + i * (size_t) n;
        }
        std::vector<float> l, r, sc, out;
        WetStreams streams;
    };
    void feedHistory (TrueStereoConvolver& c, juce::int64 from, juce::int64 to, bool crossfeed, FeedBuffers& buf);

    std::unique_ptr<TrueStereoConvolver> active, pending;
    std::atomic<int> state { Idle };
    std::atomic<bool> lastCrossfeed { true };

    // input history for warming up the pending convolver
    juce::AudioBuffer<float> history;
    std::atomic<juce::int64> written { 0 };
    juce::int64 pendingFedUpTo = 0;

    std::vector<float> scratch;
    FeedBuffers crossfade;  // output of the pending convolver while fading (audio thread)
    FeedBuffers summed;     // streams for the summing process() (audio thread)
    FeedBuffers audioFeed;  // used by the audio thread only
    double sampleRate = 44100.0;
    int maxBlock = 512;
    size_t headBlock = 256, tailBlock = 8192;
    int crossfadeLength = 1, crossfadePos = 0;

    mutable juce::SpinLock irLock;
    std::shared_ptr<const BuiltIR> activeIR;
    std::shared_ptr<const BuiltIR> pendingIR;
};
} // namespace concert7
