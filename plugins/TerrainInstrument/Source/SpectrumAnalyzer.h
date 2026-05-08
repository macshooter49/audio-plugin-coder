#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

//==============================================================================
// SpectrumAnalyzer
//
// Lock-free FFT collector for pushing magnitudes from the audio thread to a
// UI consumer at ~30 Hz. Triple-buffered atomic exchange — readers always get
// a complete frame, writers never block.
//
// Usage (per direction — typically two instances, "pre" and "post"):
//   prepare(sampleRate);
//   pushSample(monoFrame);   // every audio sample, audio thread
//   const float* bins = readLatest();  // UI thread, returns nullptr if no frame yet
//==============================================================================
class SpectrumAnalyzer
{
public:
    static constexpr int FFT_ORDER = 12;       // 4096
    static constexpr int FFT_SIZE  = 1 << FFT_ORDER;
    static constexpr int HOP_SIZE  = FFT_SIZE / 4;  // 1024 → 75% overlap, ~47 frames/s @ 48 kHz
    static constexpr int NUM_BINS  = FFT_SIZE / 2;
    static constexpr int NUM_BUFS  = 3;

    SpectrumAnalyzer()
        : fft (FFT_ORDER),
          window (FFT_SIZE, juce::dsp::WindowingFunction<float>::hann)
    {
        for (auto& b : magBuffers) b.fill (0.0f);
    }

    void prepare (double /*sampleRate*/) { reset(); }

    void reset()
    {
        writePos = 0;
        hopCounter = 0;
        std::fill (timeData.begin(), timeData.end(), 0.0f);
        for (auto& b : magBuffers) b.fill (0.0f);
        readyIndex.store (-1, std::memory_order_release);
    }

    // Push one mono sample (L+R sum already done by caller). Audio thread.
    // Uses a circular buffer + hop-based FFT for 75% overlap so the visual
    // spectrum updates ~47x/sec instead of ~12x/sec at sr=48 kHz.
    void pushSample (float monoSample) noexcept
    {
        timeData[(size_t) writePos] = monoSample;
        writePos = (writePos + 1) % FFT_SIZE;

        if (++hopCounter >= HOP_SIZE)
        {
            hopCounter = 0;

            // Choose a write buffer that isn't the currently-published one.
            const int published = readyIndex.load (std::memory_order_acquire);
            int writeIdx = nextBuf;
            if (writeIdx == published) writeIdx = (writeIdx + 1) % NUM_BUFS;
            nextBuf = (writeIdx + 1) % NUM_BUFS;

            // Copy from circular buffer into FFT work area, oldest-first.
            // After a write, writePos points at the *oldest* sample in the ring.
            std::array<float, FFT_SIZE * 2> work {};
            for (int i = 0; i < FFT_SIZE; ++i)
                work[(size_t) i] = timeData[(size_t) ((writePos + i) % FFT_SIZE)];
            window.multiplyWithWindowingTable (work.data(), FFT_SIZE);
            fft.performFrequencyOnlyForwardTransform (work.data());

            // Copy magnitudes into the chosen buffer, normalize.
            const float scale = 1.0f / (float) FFT_SIZE;
            auto& dst = magBuffers[(size_t) writeIdx];
            for (int i = 0; i < NUM_BINS; ++i) dst[(size_t) i] = work[(size_t) i] * scale;

            readyIndex.store (writeIdx, std::memory_order_release);
        }
    }

    // UI thread. Returns pointer to NUM_BINS floats, or nullptr if no frame yet.
    const float* readLatest() const noexcept
    {
        const int idx = readyIndex.load (std::memory_order_acquire);
        if (idx < 0) return nullptr;
        return magBuffers[(size_t) idx].data();
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    std::array<float, FFT_SIZE> timeData {};
    int writePos = 0;
    int hopCounter = 0;
    int nextBuf  = 0;

    std::array<std::array<float, NUM_BINS>, NUM_BUFS> magBuffers;
    std::atomic<int> readyIndex { -1 };
};
