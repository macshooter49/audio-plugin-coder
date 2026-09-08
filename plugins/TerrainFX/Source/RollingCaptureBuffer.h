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
//==============================================================================
class RollingCaptureBuffer
{
public:
    static constexpr double MAX_CAPTURE_SECONDS = 600.0; // 10 minutes

    RollingCaptureBuffer() = default;

    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        // Preserve buffer if sample rate unchanged (DAW may call prepareToPlay
        // multiple times during transport changes — don't wipe captured audio)
        if (maxSamples > 0 && std::abs(sr - sampleRate) < 1.0)
            return;

        sr = sampleRate;
        maxSamples = static_cast<int>(sr * MAX_CAPTURE_SECONDS);
        bufL.assign(static_cast<size_t>(maxSamples), 0.0f);
        bufR.assign(static_cast<size_t>(maxSamples), 0.0f);
        writePos.store(0, std::memory_order_relaxed);
        samplesWritten.store(0, std::memory_order_relaxed);
    }

    // Called once per processBlock from the audio thread, after final output.
    void writeBlock(const float* leftCh, const float* rightCh, int numSamples)
    {
        if (maxSamples == 0) return;

        // Auto-reset when buffer is full — start fresh from 0
        auto prev = samplesWritten.load(std::memory_order_relaxed);
        if (prev >= maxSamples)
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
            wp = (wp + 1) % maxSamples;
        }

        writePos.store(wp, std::memory_order_release);

        auto next = prev + numSamples;
        if (next > maxSamples) next = maxSamples;
        samplesWritten.store(next, std::memory_order_release);
    }

    // Called from message thread. Copies the last `durationSeconds` of audio
    // into caller-provided buffers. Returns number of samples copied.
    int copyForExport(float* outL, float* outR, double durationSeconds) const
    {
        if (maxSamples == 0 || sr <= 0.0) return 0;

        int available = samplesWritten.load(std::memory_order_acquire);
        int requested = static_cast<int>(durationSeconds * sr);
        int toCopy = std::min(requested, available);
        if (toCopy <= 0) return 0;

        int wp = writePos.load(std::memory_order_acquire);

        // Start reading from (wp - toCopy), wrapping around
        int readStart = (wp - toCopy + maxSamples) % maxSamples;

        for (int i = 0; i < toCopy; ++i)
        {
            int idx = (readStart + i) % maxSamples;
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
    int maxSamples = 0;
    std::vector<float> bufL;
    std::vector<float> bufR;
    std::atomic<int> writePos { 0 };
    std::atomic<int> samplesWritten { 0 };
};
