// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "ConvolutionEngine.h"
#include <thread>

namespace concert7
{
using namespace juce;

TrueStereoConvolver::TrueStereoConvolver()
{
    for (auto& c : sets[0])
        c = std::make_unique<Convolver>();
}

void TrueStereoConvolver::unload()
{
    for (auto& set : sets)
        for (auto& c : set)
            if (c != nullptr)
            {
                c->waitIdle();
                c->reset();
            }
    used = {};
    ir.reset();
    loaded = false;
}

void TrueStereoConvolver::load (std::shared_ptr<const BuiltIR> newIR, size_t headBlock, size_t tailBlock)
{
    unload();
    trueStereo = newIR->trueStereo;
    const std::array<const std::array<std::vector<float>, 4>*, WetStreams::kNumStreams> data {
        &newIR->ch, &newIR->late, &newIR->variant };
    for (size_t s = 0; s < sets.size(); ++s)
    {
        used[s] = ! (*data[s])[0].empty();
        if (! used[s]) continue;
        for (int c = 0; c < 4; ++c)
        {
            const auto& d = (*data[s])[(size_t) c];
            if (d.empty()) continue;
            auto& conv = sets[s][(size_t) c];
            if (conv == nullptr)
                conv = std::make_unique<Convolver>();
            conv->init (headBlock, tailBlock, d.data(), d.size());
        }
    }
    // keeping a reference here guarantees the audio thread never releases the last one
    ir = std::move (newIR);
    loaded = true;
}

void TrueStereoConvolver::processSet (Set& set, const float* inL, const float* inR, float* outL, float* outR, int n,
                                      bool crossfeed, float* scratch)
{
    set[0]->process (inL, outL, (size_t) n);   // L -> L
    set[3]->process (inR, outR, (size_t) n);   // R -> R

    if (trueStereo)
    {
        // the cross paths always run so their state stays valid when crossfeed is toggled
        float* lr = scratch;
        float* rl = scratch + n;
        set[1]->process (inL, lr, (size_t) n); // L -> R
        set[2]->process (inR, rl, (size_t) n); // R -> L
        if (crossfeed)
        {
            FloatVectorOperations::add (outL, rl, n);
            FloatVectorOperations::add (outR, lr, n);
        }
    }
}

void TrueStereoConvolver::process (const float* inL, const float* inR, const WetStreams& out, int n,
                                   bool crossfeed, float* scratch)
{
    for (int s = 0; s < WetStreams::kNumStreams; ++s)
    {
        if (loaded && used[(size_t) s])
        {
            processSet (sets[(size_t) s], inL, inR, out.l (s), out.r (s), n, crossfeed, scratch);
        }
        else
        {
            FloatVectorOperations::clear (out.l (s), n);
            FloatVectorOperations::clear (out.r (s), n);
        }
    }
}

//==============================================================================
ConvolutionEngine::ConvolutionEngine()
    : active (std::make_unique<TrueStereoConvolver>()),
      pending (std::make_unique<TrueStereoConvolver>())
{
}

ConvolutionEngine::~ConvolutionEngine()
{
    active->unload();
    pending->unload();
}

void ConvolutionEngine::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    maxBlock = std::max (1, maxBlockSize);

    size_t hb = 128;
    while (hb < (size_t) maxBlock && hb < 2048)
        hb *= 2;
    headBlock = hb;
    tailBlock = std::max<size_t> (8192, 4 * headBlock);

    history.setSize (2, (int) (kHistorySeconds * sr) + 4 * maxBlock);
    history.clear();
    written.store (0);
    scratch.assign ((size_t) maxBlock * 4, 0.0f);
    crossfade.resize (maxBlock);
    summed.resize (maxBlock);
    audioFeed.resize (maxBlock);
    crossfadeLength = std::max (1, (int) (kCrossfadeSeconds * sr));

    // drop everything: the worker re-delivers an IR for the new sample rate
    active->unload();
    pending->unload();
    {
        SpinLock::ScopedLockType sl (irLock);
        activeIR.reset();
        pendingIR.reset();
    }
    state.store (Idle);
}

std::shared_ptr<const BuiltIR> ConvolutionEngine::getActiveIR() const
{
    SpinLock::ScopedLockType sl (irLock);
    return activeIR;
}

void ConvolutionEngine::feedHistory (TrueStereoConvolver& c, int64 from, int64 to, bool crossfeed, FeedBuffers& buf)
{
    const int size = history.getNumSamples();
    const int chunk = (int) buf.l.size();
    auto& l = buf.l; auto& r = buf.r; auto& sc = buf.sc;
    for (int64 pos = from; pos < to;)
    {
        const int n = (int) std::min<int64> (chunk, to - pos);
        for (int i = 0; i < n; ++i)
        {
            const int idx = (int) ((pos + i) % size);
            l[(size_t) i] = history.getSample (0, idx);
            r[(size_t) i] = history.getSample (1, idx);
        }
        c.process (l.data(), r.data(), buf.streams, n, crossfeed, sc.data());
        pos += n;
    }
}

bool ConvolutionEngine::loadIR (std::shared_ptr<const BuiltIR> ir, const std::function<bool()>& shouldAbort)
{
    // Wait until the previous handover has finished. An IR that is prepared but not yet
    // taken by the audio thread (e.g. transport stopped) is simply replaced.
    for (;;)
    {
        int expected = Idle;
        if (state.compare_exchange_strong (expected, Preparing)) break;
        expected = Ready;
        if (state.compare_exchange_strong (expected, Preparing)) break;
        if (shouldAbort && shouldAbort()) return false;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    pending->load (ir, headBlock, tailBlock);

    // warm up with recent input so the tail of the new response is already present
    const int size = history.getNumSamples();
    const int64 w0 = written.load (std::memory_order_acquire);
    const int64 warm = std::min<int64> ({ (int64) (kWarmupSeconds * sampleRate), (int64) ir->length(), w0,
                                          (int64) size - 8 * maxBlock });
    int64 fed = w0 - std::max<int64> (0, warm);
    const bool crossfeed = lastCrossfeed.load();
    FeedBuffers buf;
    buf.resize (std::max (1, maxBlock));
    feedHistory (*pending, fed, w0, crossfeed, buf);
    fed = w0;

    // chase the audio thread until only a small gap remains
    for (int iterations = 0; iterations < 100; ++iterations)
    {
        const int64 w = written.load (std::memory_order_acquire);
        if (w - fed <= 2048) break;
        feedHistory (*pending, fed, w, crossfeed, buf);
        fed = w;
    }

    pendingFedUpTo = fed;
    {
        SpinLock::ScopedLockType sl (irLock);
        pendingIR = ir;
    }
    state.store (Ready, std::memory_order_release);
    return true;
}

void ConvolutionEngine::process (const float* inL, const float* inR, float* outL, float* outR, int n, bool crossfeed)
{
    for (int off = 0; off < n; off += maxBlock)
    {
        const int len = std::min (maxBlock, n - off);
        auto streams = summed.streams;
        streams.ch[0] = outL + off;
        streams.ch[1] = outR + off;
        process (inL + off, inR + off, streams, len, crossfeed);
        FloatVectorOperations::add (outL + off, streams.l (1), len);
        FloatVectorOperations::add (outR + off, streams.r (1), len);
    }
}

void ConvolutionEngine::process (const float* inL, const float* inR, const WetStreams& out, int n, bool crossfeed)
{
    lastCrossfeed.store (crossfeed);

    // process in chunks that fit the scratch buffers
    if (n > maxBlock)
    {
        for (int off = 0; off < n; off += maxBlock)
        {
            const int len = std::min (maxBlock, n - off);
            process (inL + off, inR + off, out + off, len, crossfeed);
        }
        return;
    }

    // 1. history
    {
        const int size = history.getNumSamples();
        const int64 w = written.load (std::memory_order_relaxed);
        int idx = (int) (w % size);
        const int first = std::min (n, size - idx);
        history.copyFrom (0, idx, inL, first);
        history.copyFrom (1, idx, inR, first);
        if (first < n)
        {
            history.copyFrom (0, 0, inL + first, n - first);
            history.copyFrom (1, 0, inR + first, n - first);
        }
        written.store (w + n, std::memory_order_release);
    }

    // 2. take over a prepared convolver
    int expected = Ready;
    if (state.compare_exchange_strong (expected, Fading, std::memory_order_acq_rel))
    {
        const int64 upTo = written.load (std::memory_order_relaxed) - n;
        if (upTo > pendingFedUpTo)
            feedHistory (*pending, pendingFedUpTo, upTo, crossfeed, audioFeed);
        pendingFedUpTo = upTo;
        crossfadePos = 0;

        if (! active->isLoaded())
        {
            // nothing to fade from (first IR after prepare): switch immediately
            std::swap (active, pending);
            {
                SpinLock::ScopedLockType sl (irLock);
                activeIR = pendingIR;
                pendingIR.reset();
            }
            state.store (Idle, std::memory_order_release);
        }
    }

    // 3. convolve
    active->process (inL, inR, out, n, crossfeed, scratch.data());

    if (state.load() == Fading)
    {
        pending->process (inL, inR, crossfade.streams, n, crossfeed, scratch.data());
        for (size_t s = 0; s < out.ch.size(); ++s)
        {
            float* o = out.ch[s];
            const float* x = crossfade.streams.ch[s];
            for (int i = 0; i < n; ++i)
            {
                // linear (equal gain) fade: old and new responses are usually highly correlated
                const float a = std::min (1.0f, (float) (crossfadePos + i) / (float) crossfadeLength);
                o[i] += a * (x[i] - o[i]);
            }
        }
        crossfadePos += n;
        if (crossfadePos >= crossfadeLength)
        {
            std::swap (active, pending);
            {
                SpinLock::ScopedLockType sl (irLock);
                activeIR = pendingIR;
                pendingIR.reset();
            }
            state.store (Idle, std::memory_order_release);
        }
    }
}
} // namespace concert7
