#pragma once
// ══ tp103 — CONCERT PITCH (Settings → MIDI & Controllers → Tuning reference, 415–466 Hz) ══════════════════════════
//  ONE value per process: the page says "every instance", and a second instance must not play 432 against the
//  first's 440. The processor's setTuningA4 writes it (and the settings file); every reader of a note's frequency
//  reads it — the voice's coarse lane (so every oscillator engine and the sub), the resonator, the fold key-track,
//  the scope, the sample key detector and the chop sampler's auto-key.
//  440 is exactly 0 semitones, so every reader adds a literal 0 (or skips) at the default: bit-identical.
//  JUCE-free on purpose: the shim-built unit tests include the headers that read it.
#include <atomic>
#include <cmath>

namespace wc
{
    inline std::atomic<float>& tuningA4Hz() noexcept { static std::atomic<float> hz { 440.0f }; return hz; }
    inline float tuningA4Semis() noexcept
    {
        const float hz = tuningA4Hz().load (std::memory_order_relaxed);
        return hz == 440.0f ? 0.0f : 12.0f * std::log2 (hz / 440.0f);
    }
    inline double tuningA4HzD() noexcept { return (double) tuningA4Hz().load (std::memory_order_relaxed); }
}
