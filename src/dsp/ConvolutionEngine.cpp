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
    for (auto* group : { &earlyTakes, &lateTakes })
        for (auto& set : *group)
            for (auto& c : set)
                if (c != nullptr)
                {
                    c->waitIdle();
                    c->reset();
                }
    for (auto& set : sets)
        for (auto& c : set)
            if (c != nullptr)
            {
                c->waitIdle();
                c->reset();
            }
    used = {};
    numEarlyTakes = numLateTakes = 0;
    ir.reset();
    loaded = false;
}

void TrueStereoConvolver::loadSet (Set& set, const std::array<std::vector<float>, 4>& data, size_t headBlock, size_t tailBlock)
{
    for (int c = 0; c < 4; ++c)
    {
        const auto& d = data[(size_t) c];
        if (d.empty()) continue;
        auto& conv = set[(size_t) c];
        if (conv == nullptr)
            conv = std::make_unique<Convolver>();
        conv->init (headBlock, tailBlock, d.data(), d.size());
    }
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
        if (used[s])
            loadSet (sets[s], *data[s], headBlock, tailBlock);
    }

    // the other takes, mixed into the early part and the late reverb (they need those streams)
    auto loadTakes = [&] (std::vector<Set>& group, const std::vector<std::array<std::vector<float>, 4>>& takes,
                          const std::vector<float>& rho, bool streamUsed) {
        const bool ok = streamUsed && ! takes.empty() && rho.size() == takes.size() + 1;
        const int num = ok ? (int) takes.size() : 0;
        if ((int) group.size() < num)
            group.resize ((size_t) num);
        for (int t = 0; t < num; ++t)
            loadSet (group[(size_t) t], takes[(size_t) t], headBlock, tailBlock);
        return num;
    };
    numEarlyTakes = loadTakes (earlyTakes, newIR->earlyTakes, newIR->earlyTakeRho, used[0]);
    numLateTakes = loadTakes (lateTakes, newIR->lateTakes, newIR->lateTakeRho, used[1]);
    earlyRho = newIR->earlyTakeRho;
    lateRho = newIR->lateTakeRho;

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
                                   bool crossfeed, float* scratch, const TakeClock* clock)
{
    for (int s = 0; s < WetStreams::kNumStreams; ++s)
    {
        if (used[(size_t) s])
        {
            processSet (sets[(size_t) s], inL, inR, out.l (s), out.r (s), n, crossfeed, scratch);
        }
        else
        {
            FloatVectorOperations::clear (out.l (s), n);
            FloatVectorOperations::clear (out.r (s), n);
        }
    }
    if (numEarlyTakes > 0)
        processTakes (earlyTakes, numEarlyTakes, earlyRho, inL, inR, out.l (0), out.r (0), n, crossfeed, scratch, clock);
    if (numLateTakes > 0)
        processTakes (lateTakes, numLateTakes, lateRho, inL, inR, out.l (1), out.r (1), n, crossfeed, scratch, clock);
}

void TrueStereoConvolver::processTakes (std::vector<Set>& takes, int numTakes, const std::vector<float>& rho,
                                        const float* inL, const float* inR, float* outL, float* outR, int n,
                                        bool crossfeed, float* scratch, const TakeClock* clock)
{
    // scratch: 4 n for processSet, then one take's output, and per sample the two takes in the mix with their weights
    float* takeL = scratch + 4 * n;
    float* takeR = scratch + 5 * n;
    float* firstWeight = scratch + 6 * n;
    float* secondWeight = scratch + 7 * n;
    float* firstTake = scratch + 8 * n;
    float* secondTake = scratch + 9 * n;

    // take k fades into take k + 1 (the last into the first); the weights keep the level of the mix, whatever the
    // correlation of the two (alike reflections: about equal gain; independent late reverbs: equal power)
    const int count = numTakes + 1;
    for (int i = 0; i < n && clock != nullptr; ++i)
    {
        const double pos = std::fmod (clock->position + clock->increment * i, (double) count);
        const int k = std::min (count - 1, (int) pos);
        const float f = (float) (pos - k);
        const float s = f * f * (3.0f - 2.0f * f);
        const float a = 1.0f - s, b = s;
        const float r = rho[(size_t) k];
        const float norm = 1.0f / std::sqrt (std::max (1.0e-6f, a * a + b * b + 2.0f * a * b * r));
        firstTake[i] = (float) k;
        secondTake[i] = (float) ((k + 1) % count);
        firstWeight[i] = a * norm;
        secondWeight[i] = b * norm;
    }
    auto weightOf = [&] (int take, int i) {
        return (firstTake[i] == (float) take ? firstWeight[i] : 0.0f) + (secondTake[i] == (float) take ? secondWeight[i] : 0.0f);
    };

    if (clock != nullptr)
        for (int i = 0; i < n; ++i)
        {
            const float w = weightOf (0, i);
            outL[i] *= w;
            outR[i] *= w;
        }
    // every take is convolved all the time (it must be ringing when its turn comes)
    for (int t = 0; t < numTakes; ++t)
    {
        processSet (takes[(size_t) t], inL, inR, takeL, takeR, n, crossfeed, scratch);
        if (clock == nullptr) continue;
        for (int i = 0; i < n; ++i)
        {
            const float w = weightOf (t + 1, i);
            outL[i] += w * takeL[i];
            outR[i] += w * takeR[i];
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

void ConvolutionEngine::prepare (double sr, int maxBlockSize, int headBlockSize)
{
    sampleRate = sr;
    maxBlock = std::max (1, maxBlockSize);

    size_t hb = 128;
    while (hb < (size_t) maxBlock && hb < 2048)
        hb *= 2;
    headBlock = headBlockSize > 0 ? (size_t) headBlockSize : hb;
    tailBlock = std::max<size_t> (8192, 4 * headBlock);

    history.setSize (2, (int) (kHistorySeconds * sr) + 4 * maxBlock);
    history.clear();
    written.store (0);
    scratch.assign ((size_t) maxBlock * TrueStereoConvolver::kScratchPerSample, 0.0f);
    takePosition = 0.0;
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

bool ConvolutionEngine::skipSilence (int n)
{
    if (state.load (std::memory_order_acquire) != Idle) return false;
    // the history goes on (silent), so a response loaded later is warmed up with what really came in
    const int size = history.getNumSamples();
    const int64 w = written.load (std::memory_order_relaxed);
    for (int done = 0; done < n;)
    {
        const int idx = (int) ((w + done) % size);
        const int len = std::min (n - done, size - idx);
        history.clear (idx, len);
        done += len;
    }
    written.store (w + n, std::memory_order_release);
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

void ConvolutionEngine::process (const float* inL, const float* inR, const WetStreams& out, int n, bool crossfeed,
                                 double takesPerSecond)
{
    lastCrossfeed.store (crossfeed);

    // process in chunks that fit the scratch buffers
    if (n > maxBlock)
    {
        for (int off = 0; off < n; off += maxBlock)
        {
            const int len = std::min (maxBlock, n - off);
            process (inL + off, inR + off, out + off, len, crossfeed, takesPerSecond);
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
        // the first response with takes starts at the first take, where the one before is
        if (pending->hasTakes() && ! active->hasTakes())
            takePosition = 0.0;

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

    // 3. convolve (both responses at the same place in the sequence of their takes)
    const TakeClock clock { takePosition, std::max (0.0, takesPerSecond) / sampleRate };
    takePosition = std::fmod (takePosition + clock.increment * n, kTakeCycle);
    active->process (inL, inR, out, n, crossfeed, scratch.data(), &clock);

    if (state.load() == Fading)
    {
        pending->process (inL, inR, crossfade.streams, n, crossfeed, scratch.data(), &clock);
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
