#pragma once

#include <vector>
#include <atomic>
#include <cstring>
#include <algorithm>

//==============================================================================
// RollingCaptureBuffer: Always-on circular capture of final stereo output
//
// Lock-free design:
//   - Audio thread is sole writer (writeBlock)
//   - Message thread reads via copyForExport (one-shot, no contention)
//   - Atomic writePos + samplesWritten with release/acquire semantics
//
// Capacity: up to 10 minutes at any sample rate.
//
// fb496 — LAZY ALLOCATION. Ten minutes of stereo float at 44.1k is 202 MB, and
// prepareToPlay used to pay it unconditionally, for every instance, whether or
// not the Export feature was ever reachable. It is now armed on demand (see
// TerrainInstrumentAudioProcessor::ensureCaptureBufferAllocated) and, until it
// is, every entry point is a cheap early-out.
//
// 🔑 THE PUBLICATION ORDER IS THE WHOLE SAFETY ARGUMENT. maxSamples is the ONLY
// thing the audio thread tests, so it is written LAST with a release store and
// read FIRST with an acquire load. The vectors are sized and zeroed before that
// store, so an audio thread that sees a non-zero maxSamples is guaranteed to see
// fully-constructed buffers behind it. It is never un-armed while audio runs.
//==============================================================================
class RollingCaptureBuffer
{
public:
    static constexpr double MAX_CAPTURE_SECONDS = 600.0; // 10 minutes

    RollingCaptureBuffer() = default;

    /** True once the rings exist. Cheap — one relaxed load. */
    bool isAllocated() const noexcept
    {
        return maxSamples.load (std::memory_order_relaxed) > 0;
    }

    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        // Preserve buffer if sample rate unchanged (DAW may call prepareToPlay
        // multiple times during transport changes — don't wipe captured audio)
        const int have = maxSamples.load (std::memory_order_relaxed);
        if (have > 0 && std::abs(sr - sampleRate) < 1.0)
            return;

        // Un-publish first: nothing may index the vectors while they resize.
        // (The host contract says processBlock is not concurrent with
        // prepareToPlay, but arming can also happen from the message thread
        // while audio runs — so the store is not decoration.)
        maxSamples.store (0, std::memory_order_release);

        sr = sampleRate;
        const int n = static_cast<int>(sr * MAX_CAPTURE_SECONDS);
        bufL.assign(static_cast<size_t>(n), 0.0f);
        bufR.assign(static_cast<size_t>(n), 0.0f);
        writePos.store(0, std::memory_order_relaxed);
        samplesWritten.store(0, std::memory_order_relaxed);
        maxSamples.store (n, std::memory_order_release);   // publish LAST
    }

    // Called once per processBlock from the audio thread, after final output.
    void writeBlock(const float* leftCh, const float* rightCh, int numSamples)
    {
        const int cap = maxSamples.load (std::memory_order_acquire);
        if (cap == 0) return;

        // Auto-reset when buffer is full — start fresh from 0
        auto prev = samplesWritten.load(std::memory_order_relaxed);
        if (prev >= cap)
        {
            writePos.store(0, std::memory_order_relaxed);
            samplesWritten.store(0, std::memory_order_release);
            prev = 0;
        }

        int wp = writePos.load(std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            bufL[static_cast<size_t>(wp)] = leftCh[i];
            bufR[static_cast<size_t>(wp)] = rightCh != nullptr ? rightCh[i] : leftCh[i];
            if (++wp >= cap) wp = 0;   // fb494 — was a runtime idiv per sample
        }

        writePos.store(wp, std::memory_order_release);

        auto next = prev + numSamples;
        if (next > cap) next = cap;
        samplesWritten.store(next, std::memory_order_release);
    }

    // Called from message thread. Copies the last `durationSeconds` of audio
    // into caller-provided buffers. Returns number of samples copied.
    int copyForExport(float* outL, float* outR, double durationSeconds) const
    {
        const int cap = maxSamples.load (std::memory_order_acquire);
        if (cap == 0 || sr <= 0.0) return 0;

        int available = samplesWritten.load(std::memory_order_acquire);
        int requested = static_cast<int>(durationSeconds * sr);
        int toCopy = std::min(requested, available);
        if (toCopy <= 0) return 0;

        int wp = writePos.load(std::memory_order_acquire);

        // Start reading from (wp - toCopy), wrapping around
        int readStart = (wp - toCopy + cap) % cap;

        for (int i = 0; i < toCopy; ++i)
        {
            int idx = (readStart + i) % cap;
            outL[i] = bufL[static_cast<size_t>(idx)];
            outR[i] = bufR[static_cast<size_t>(idx)];
        }

        return toCopy;
    }

    // Returns how many seconds of audio are currently available
    float getAvailableSeconds() const
    {
        if (sr <= 0.0) return 0.0f;
        return static_cast<float>(samplesWritten.load(std::memory_order_acquire)) / static_cast<float>(sr);
    }

    double getSampleRate() const { return sr; }

private:
    double sr = 0.0;
    std::atomic<int> maxSamples { 0 };   // 0 = not armed; published LAST (release)
    std::vector<float> bufL;
    std::vector<float> bufR;
    std::atomic<int> writePos { 0 };
    std::atomic<int> samplesWritten { 0 };
};
