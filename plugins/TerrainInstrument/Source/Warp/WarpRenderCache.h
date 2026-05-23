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
//   - Population (in Task 10) happens on a worker thread.
//
// Memory cap: 16 entries x ~3 MB worst case = ~50 MB ceiling.
//
// This is the SKELETON: prewarm() is a stub until Task 10 adds the
// background-render scheduler.

#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Slice.h"
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
                && stretchRatio    == o.stretchRatio
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

    /** Lookup. Returns the rendered buffer if ready, else nullptr.
     *  Safe to call from the audio thread.
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

    /** Schedule a background render. NO-OP in Task 8 — Task 10 wires the
     *  background scheduler. Stub here so the API surface is in place.
     */
    void prewarm (const Key& /*k*/)
    {
        // TODO(Task 10): launch worker thread to call WarpProcessor::renderFullSlice
        //                then publish via ready.store(true).
    }

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

    mutable std::mutex map_;
    std::unordered_map<Key, std::unique_ptr<Entry>, KeyHash> entries_;
};

}  // namespace tw
