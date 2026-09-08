#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <cstring>   // fb488 — memcpy in the snapshot

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
        writeCount.store (0, std::memory_order_relaxed);
        lastFftCount = 0;
        std::fill (ring.begin(), ring.end(), 0.0f);
        for (auto& b : magBuffers) b.fill (0.0f);
        readyIndex.store (-1, std::memory_order_release);
    }

    // AUDIO THREAD — one store and one counter bump. THAT IS ALL.
    // fb488: this used to run the whole analysis INSIDE the real-time callback — every 1024
    // samples it zero-filled a 32 KB stack array, copied 4096 samples one at a time through a
    // modulo, windowed them and ran a 4096-point FFT, x2 analysers, ~94 times a second. On Apple
    // that FFT is vDSP; on Windows juce::dsp::FFT falls back to its own implementation, so the
    // cost landed on the audio thread of every Windows host — which is exactly why Max's FL
    // meter read 36% with one sine and no notes WHENEVER THE EDITOR WAS OPEN, and ran like water
    // when it was closed. The transform now happens in update(), on the message thread, on
    // demand. Nothing about the published data changes.
    void pushSample (float monoSample) noexcept
    {
        const uint64_t w = writeCount.load (std::memory_order_relaxed);
        ring[(size_t) (w & (uint64_t) (FFT_SIZE - 1))] = monoSample;
        writeCount.store (w + 1, std::memory_order_release);
    }

    // MESSAGE THREAD — compute a frame if at least HOP_SIZE new samples have arrived since the
    // last one. The editor calls this only while a consumer is actually on screen, so a hidden
    // spectrum now costs literally nothing anywhere.
    //
    // The snapshot races the writer by design: the audio thread's next write lands exactly at the
    // OLDEST sample of our window, and a Hann window multiplies that edge by ~0 — so the raced
    // samples cannot influence the spectrum. No lock, no copy on the audio thread.
    void update() noexcept
    {
        const uint64_t w = writeCount.load (std::memory_order_acquire);
        if (w < (uint64_t) FFT_SIZE) return;                    // not enough history yet
        if (w - lastFftCount < (uint64_t) HOP_SIZE) return;     // nothing new enough to redraw
        lastFftCount = w;

        const int published = readyIndex.load (std::memory_order_acquire);
        int writeIdx = nextBuf;
        if (writeIdx == published) writeIdx = (writeIdx + 1) % NUM_BUFS;
        nextBuf = (writeIdx + 1) % NUM_BUFS;

        // newest FFT_SIZE samples, oldest-first, as two contiguous runs (no per-sample modulo)
        const size_t start    = (size_t) (w & (uint64_t) (FFT_SIZE - 1));
        const size_t firstRun = (size_t) FFT_SIZE - start;
        std::memcpy (work.data(), ring.data() + start, firstRun * sizeof (float));
        if (start > 0) std::memcpy (work.data() + firstRun, ring.data(), start * sizeof (float));
        std::fill (work.begin() + FFT_SIZE, work.end(), 0.0f);   // the transform's scratch half

        window.multiplyWithWindowingTable (work.data(), FFT_SIZE);
        fft.performFrequencyOnlyForwardTransform (work.data());

        const float scale = 1.0f / (float) FFT_SIZE;
        auto& dst = magBuffers[(size_t) writeIdx];
        for (int i = 0; i < NUM_BINS; ++i) dst[(size_t) i] = work[(size_t) i] * scale;

        readyIndex.store (writeIdx, std::memory_order_release);
        frameCounter.fetch_add (1, std::memory_order_release);   // fb342 — the editor's fresh-frame gate
    }

    // UI thread. Returns pointer to NUM_BINS floats, or nullptr if no frame yet.
    const float* readLatest() const noexcept
    {
        const int idx = readyIndex.load (std::memory_order_acquire);
        if (idx < 0) return nullptr;
        return magBuffers[(size_t) idx].data();
    }

    // fb342 — monotone frame counter: readLatest() latches non-null FOREVER after the first
    // note ever played, which made the editor push the ~40-80KB EQ string at 60Hz for the
    // rest of the session even with every consumer hidden. The editor compares this count
    // to skip pushes that would resend an identical frame (FFT publishes ~47/s, tick is 60/s).
    uint32_t frameSeq() const noexcept { return frameCounter.load (std::memory_order_acquire); }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    std::array<float, FFT_SIZE>     ring {};        // fb488 — audio thread writes, message thread snapshots
    std::array<float, FFT_SIZE * 2> work {};        // fb488 — a MEMBER: no 32 KB stack zero-fill per frame
    std::atomic<uint64_t> writeCount { 0 };
    uint64_t lastFftCount = 0;                      // message thread only
    int nextBuf  = 0;
    std::atomic<uint32_t> frameCounter { 0 };   // fb342 — see frameSeq()

    std::array<std::array<float, NUM_BINS>, NUM_BUFS> magBuffers;
    std::atomic<int> readyIndex { -1 };
};
