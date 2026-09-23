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

        // ── AUTO KEY DETECTION (SampleKeyDetect) ─────────────────────────────────
        // Set once by the loader / restore path (off the audio thread) BEFORE the
        // buffer is stored; read lock-free per-note by the sample oscillator
        // (SynthVoice) and the chop sampler (SamplerVoice). keyOffset() is the
        // base-tuning offset in semitones that snaps the sample's root key to the
        // same C as the oscillators (0 when un-pitched → a guaranteed no-op).
        struct KeyInfo
        {
            bool  detected  = false;
            int   midiNote  = -1;
            float frequency = 0.0f;
            float confidence = 0.0f;
            float offsetSemis = 0.0f;
        };

        void setKeyInfo (bool detected, int midiNote, float frequency,
                         float confidence, float offsetSemis) noexcept
        {
            keyDetected_.store (detected, std::memory_order_relaxed);
            keyMidiNote_.store (midiNote, std::memory_order_relaxed);
            keyFrequency_.store (frequency, std::memory_order_relaxed);
            keyConfidence_.store (confidence, std::memory_order_relaxed);
            // offset written LAST (release) so a reader that sees a fresh offset
            // also sees the fields above; the audio thread only reads keyOffset().
            keyOffsetSemis_.store (detected ? offsetSemis : 0.0f, std::memory_order_release);
        }

        void clearKeyInfo() noexcept { setKeyInfo (false, -1, 0.0f, 0.0f, 0.0f); }

        /** Audio-thread safe: semitone base-tuning offset (0 if un-pitched/none). */
        float keyOffset() const noexcept { return keyOffsetSemis_.load (std::memory_order_acquire); }

        KeyInfo getKeyInfo() const noexcept
        {
            KeyInfo k;
            k.detected    = keyDetected_.load (std::memory_order_relaxed);
            k.midiNote    = keyMidiNote_.load (std::memory_order_relaxed);
            k.frequency   = keyFrequency_.load (std::memory_order_relaxed);
            k.confidence  = keyConfidence_.load (std::memory_order_relaxed);
            k.offsetSemis = keyOffsetSemis_.load (std::memory_order_acquire);
            return k;
        }

    private:
        BufferPtr current;
        std::atomic<double> nativeRate { 0.0 };

        std::atomic<float> keyOffsetSemis_ { 0.0f };
        std::atomic<bool>  keyDetected_    { false };
        std::atomic<int>   keyMidiNote_    { -1 };
        std::atomic<float> keyFrequency_   { 0.0f };
        std::atomic<float> keyConfidence_  { 0.0f };
    };
}
