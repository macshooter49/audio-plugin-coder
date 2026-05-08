// SampleBuffer.h
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>

namespace tw
{
    /**
     * SampleBuffer — lock-free shared storage for the loaded sample.
     *
     * UI thread (loader) writes the new buffer via store().
     * Audio thread reads the current buffer per-voice via load().
     * Old buffers are released automatically when last voice releases its shared_ptr.
     *
     * Per the v0 design spec, the buffer holds the entire decoded sample in RAM
     * (cap: 10 minutes ≈ 230MB stereo float32 at 48kHz).
     */
    class SampleBuffer
    {
    public:
        using BufferPtr = std::shared_ptr<juce::AudioBuffer<float>>;

        SampleBuffer() = default;

        /** Audio-thread safe. Returns the current buffer (may be empty/null). */
        BufferPtr load() const noexcept
        {
            return std::atomic_load (&current);
        }

        /** UI-thread call. Replaces the current buffer atomically. */
        void store (BufferPtr newBuffer) noexcept
        {
            std::atomic_store (&current, std::move (newBuffer));
        }

        /** Convenience: total samples in the current buffer (0 if empty). */
        int getNumSamples() const noexcept
        {
            auto b = load();
            return b ? b->getNumSamples() : 0;
        }

        /** Convenience: native sample rate of the loaded sample. */
        double getSampleRate() const noexcept { return nativeRate.load(); }
        void   setSampleRate (double sr) noexcept { nativeRate.store (sr); }

    private:
        BufferPtr current;
        std::atomic<double> nativeRate { 0.0 };
    };
}
