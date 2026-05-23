// WarpRenderCache.h
//
// Per-(slice, stretch, mode) cache of pre-rendered warp output. Used by
// Scan mode (Mark 1.5) to allow output-level back-and-forth scrubbing
// through stretched audio without re-running the warp engine in reverse
// (Signalsmith chokes on reversed input — see spec 2026-05-23
// "Warp x Scan combinations").
//
// Threading:
//   - Audio thread reads via get() / isReady() — lock-free on the data
//     pointer via acquire-load on the per-entry atomic<bool> ready flag.
//   - The map structure itself is mutex-protected (std::mutex). Audio
//     thread takes the mutex briefly for the lookup, but the buffer
//     pointer it returns is stable for the lifetime of that entry.
//   - Population happens on a juce::Thread worker launched by prewarm().
//
// Memory cap: 16 entries x ~3 MB worst case = ~50 MB ceiling.

#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "../Slice.h"
#include "WarpProcessor.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace tw
{

class WarpRenderCache
{
public:
    struct Key
    {
        int      sliceIndex      = 0;
        int      sourceVersionId = 0;   // monotonic; bumps on SampleLoader::loadNewSample or source pointer swap
        float    stretchRatio    = 1.0f;
        WarpMode warpMode        = WarpMode::None;

        bool operator== (const Key& o) const noexcept
        {
            return sliceIndex      == o.sliceIndex
                && sourceVersionId == o.sourceVersionId
                // Intentional exact-float equality: cache keys must be bit-identical to hit.
                // Do NOT replace with epsilon compare — that would silently merge distinct
                // renders for stretchRatios that look "close enough".
                && stretchRatio    == o.stretchRatio   // NOLINT(clang-diagnostic-float-equal)
                && warpMode        == o.warpMode;
        }
    };

    struct KeyHash
    {
        std::size_t operator() (const Key& k) const noexcept
        {
            // Mix the four components into a single hash.
            // No need for cryptographic quality — collisions just mean
            // a longer linear probe; correctness comes from operator==.
            auto h1 = std::hash<int>{} (k.sliceIndex);
            auto h2 = std::hash<int>{} (k.sourceVersionId);
            auto h3 = std::hash<float>{} (k.stretchRatio);
            auto h4 = std::hash<int>{} (static_cast<int> (k.warpMode));
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
        }
    };

    // ── Source / config setters (called from TerrainSynth / processor) ────────

    /** Set the source sample pointers. NOT owned — caller keeps the source
     *  buffer alive for the lifetime of the cache. Also clears all cached
     *  entries (source changed → all renders stale).
     */
    void setSource (const float* L, const float* R, int len)
    {
        std::lock_guard<std::mutex> lock (map_);
        sourceL_   = L;
        sourceR_   = R;
        sourceLen_ = len;
        entries_.clear();
    }

    /** Set the sample rate used for warp engine config in prewarm renders. */
    void setSampleRate (double sr) noexcept { sampleRate_ = sr; }

    /** Push slice bounds for a sliceIndex (called when slice layout changes).
     *  Required by prewarm() to know what region of the source to render.
     */
    void setSliceBounds (int sliceIndex, int startSample, int endSample)
    {
        std::lock_guard<std::mutex> lock (map_);
        sliceBounds_[sliceIndex] = { startSample, endSample };
    }

    // ── Read API (audio-thread safe) ──────────────────────────────────────────

    /** Lookup. Returns the rendered buffer if ready, else nullptr.
     *  Safe to call from the audio thread.
     *
     *  LIFETIME CONTRACT: the returned pointer is valid only while no
     *  invalidateSlice() / invalidateSource() / setSource() call runs concurrently.
     *  v1 satisfies this because invalidation only fires at non-realtime
     *  boundaries (sample load, slice mutation from UI).
     */
    const juce::AudioBuffer<float>* get (const Key& k) const
    {
        std::lock_guard<std::mutex> lock (map_);
        auto it = entries_.find (k);
        if (it == entries_.end()) return nullptr;
        if (! it->second->ready.load (std::memory_order_acquire)) return nullptr;
        return it->second->buffer.get();
    }

    /** True if the buffer for this key is fully rendered and ready. */
    bool isReady (const Key& k) const
    {
        return get (k) != nullptr;
    }

    // ── Background scheduler ──────────────────────────────────────────────────

    /** Schedule a background render. Idempotent — if the key already exists
     *  in the map (either ready or in-flight), this is a no-op.
     *
     *  Called from: UI thread (setSliceScanEnabled native fn) or message thread.
     *  Thread safety: the entry slot is claimed under map_ before spawning the
     *  worker, so concurrent prewarm() calls for the same key are harmless.
     */
    void prewarm (const Key& k)
    {
        // Claim the slot before launching the worker. The Entry exists with
        // ready=false until the worker publishes.
        {
            std::lock_guard<std::mutex> lock (map_);
            if (entries_.find (k) != entries_.end()) return;  // already in flight or ready
            entries_[k] = std::make_unique<Entry>();
        }

        // Snapshot the bits the worker needs (avoids stale capture).
        const float* L = sourceL_;
        const float* R = sourceR_;
        SliceBounds  bounds;
        {
            std::lock_guard<std::mutex> lock (map_);
            auto it = sliceBounds_.find (k.sliceIndex);
            if (it != sliceBounds_.end()) bounds = it->second;
        }
        const double sr = sampleRate_;

        if (L == nullptr || R == nullptr || bounds.end <= bounds.start)
        {
            // No source / no bounds — leave the entry with ready=false forever.
            // Subsequent prewarm() calls will see the entry and no-op, so this
            // is a one-time abort. Caller gets a cache miss → falls through to
            // whatever fallback SamplerVoice uses.
            return;
        }

        juce::Thread::launch ([this, k, L, R, bounds, sr]()
        {
            const int sliceLen = bounds.end - bounds.start;
            auto buffer = WarpProcessor::renderFullSlice (
                L + bounds.start, R + bounds.start,
                sliceLen, k.stretchRatio, k.warpMode, sr);

            std::lock_guard<std::mutex> lock (map_);
            auto it = entries_.find (k);
            if (it != entries_.end())
            {
                it->second->buffer = std::make_unique<juce::AudioBuffer<float>> (std::move (buffer));
                it->second->ready.store (true, std::memory_order_release);
            }
            // If the entry was erased mid-render (e.g. setSource() cleared the map),
            // the rendered buffer is discarded here — no harm done.
        });
    }

    // ── Invalidation ──────────────────────────────────────────────────────────

    /** Drop all cache entries for the given slice (called on slice mutation). */
    void invalidateSlice (int sliceIndex)
    {
        std::lock_guard<std::mutex> lock (map_);
        for (auto it = entries_.begin(); it != entries_.end(); )
        {
            if (it->first.sliceIndex == sliceIndex) it = entries_.erase (it);
            else                                    ++it;
        }
    }

    /** Drop all entries for the given source version (called on sample change). */
    void invalidateSource (int sourceVersionId)
    {
        std::lock_guard<std::mutex> lock (map_);
        for (auto it = entries_.begin(); it != entries_.end(); )
        {
            if (it->first.sourceVersionId == sourceVersionId) it = entries_.erase (it);
            else                                              ++it;
        }
    }

private:
    struct Entry
    {
        std::unique_ptr<juce::AudioBuffer<float>> buffer;
        std::atomic<bool> ready { false };
    };

    struct SliceBounds { int start = 0; int end = 0; };

    mutable std::mutex map_;
    std::unordered_map<Key, std::unique_ptr<Entry>, KeyHash> entries_;
    std::unordered_map<int, SliceBounds>                     sliceBounds_;

    // Source pointers — NOT owned; set by setSource().
    const float* sourceL_   = nullptr;
    const float* sourceR_   = nullptr;
    int          sourceLen_ = 0;
    double       sampleRate_ = 48000.0;
};

}  // namespace tw
