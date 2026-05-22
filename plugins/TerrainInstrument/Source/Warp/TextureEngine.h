// TextureEngine.h — Signalsmith + periodic pitch jitter for the "Flux"
// character described in the warp-modes design (Ableton Texture analog).
//
// Wraps SignalsmithEngine and perturbs the engine's transpose every
// ~jitterIntervalMs with a random offset in ±pitchJitterSemis. The base
// transpose set by the caller is preserved; the jitter is added on top.
// Net character vs Tones: same spectral-stretch quality but with an
// evolving warble / wow that gives sustained material a glitchy, textural
// feel. Sounds especially distinct on pads, vocals, and tonal content;
// percussive material still works but the difference is more subtle.
//
// First-version constants:
//   - jitterIntervalMs    = 80 ms — slow enough to hear each jump as a
//                                     distinct micro-modulation, fast
//                                     enough to feel continuously alive.
//   - pitchJitterSemis    = 1.0 — ±1 semitone around the caller's base
//                                  pitch. Audible but musical (not
//                                  monstrous detune).
//
// Future hooks (deferred until user requests controls):
//   - expose jitter amount + rate as per-chop params
//   - add random position seeks (more dramatic glitches)
//   - add formant jitter (vowel-morph character)
//
// RT-safety: process() is allocation-free; juce::Random is allocation-
// free; SignalsmithEngine's setTransposeSemitones is allocation-free
// between blocks (smoothly interpolated inside the stretcher).
//
#pragma once

#include <juce_core/juce_core.h>
#include "SignalsmithEngine.h"

namespace tw
{
    class TextureEngine
    {
    public:
        TextureEngine() = default;

        void prepare (double sampleRateHz, int numChannels, int blockSize)
        {
            sampleRate = sampleRateHz;
            inner.prepare (sampleRateHz, numChannels, blockSize);
            jitterIntervalSamples =
                juce::jmax (32, (int) (sampleRateHz * (jitterIntervalMs / 1000.0)));
            ready = true;
            reset();
        }

        void reset()
        {
            if (! ready) return;
            inner.reset();
            samplesSinceLastJitter = 0;
            currentJitterSemis     = 0.0f;
            // Re-apply base + zeroed jitter so the first block starts clean.
            inner.setPitchSemitones (basePitchSemitones);
        }

        bool isReady() const noexcept { return ready && inner.isReady(); }

        void setStretchRatio (float r) noexcept
        {
            baseStretchRatio = juce::jlimit (0.1f, 15.0f, r);
            inner.setStretchRatio (baseStretchRatio);
        }

        void setPitchSemitones (float s) noexcept
        {
            basePitchSemitones = juce::jlimit (-24.0f, 24.0f, s);
            // Update inner with base + current jitter so an explicit pitch
            // change from the caller takes effect immediately (between
            // jitter events the engine still reflects the latest base).
            inner.setPitchSemitones (basePitchSemitones + currentJitterSemis);
        }

        int inputLatency() const noexcept { return inner.inputLatency(); }

        void seek (const float* primeL, const float* primeR, int numSamples)
        {
            inner.seek (primeL, primeR, numSamples);
        }

        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;

            // Walk the jitter clock and roll a new offset whenever we cross
            // the interval boundary. Done before process() so the engine
            // pulls in the new transpose for this block. SignalsmithStretch
            // smooths transpose changes between successive process() calls
            // internally, so a small jump per block reads as a smooth wow
            // rather than a hard step.
            samplesSinceLastJitter += numSamples;
            if (samplesSinceLastJitter >= jitterIntervalSamples)
            {
                samplesSinceLastJitter -= jitterIntervalSamples;
                // Random in [-1, +1] × pitchJitterSemis.
                currentJitterSemis = (rng.nextFloat() * 2.0f - 1.0f) * pitchJitterSemis;
                inner.setPitchSemitones (basePitchSemitones + currentJitterSemis);
            }

            inner.process (inL, inR, outL, outR, numSamples);
        }

    private:
        SignalsmithEngine inner;
        juce::Random      rng;

        double sampleRate            = 48000.0;
        bool   ready                 = false;

        float  baseStretchRatio      = 1.0f;
        float  basePitchSemitones    = 0.0f;
        float  currentJitterSemis    = 0.0f;

        int    samplesSinceLastJitter = 0;
        int    jitterIntervalSamples  = 3840;     // 80 ms @ 48 kHz, recomputed in prepare

        // Tunable first-version constants. Move to per-chop params once
        // the UI surfaces controls.
        static constexpr double jitterIntervalMs  = 80.0;
        static constexpr float  pitchJitterSemis  = 1.0f;
    };
}
