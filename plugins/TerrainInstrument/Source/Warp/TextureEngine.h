// TextureEngine.h — v2 (grain-scatter; Ableton "Flux" character)
//
// v1 perturbed Signalsmith's transpose — that's just wow/flutter, NOT the
// "weird little breakups" Ableton Texture produces. User pushed back:
// "the texture sounds like some really really bad wow and flutter… find
// a new texture mode… Ableton has those weird little breakups in the
// artifact… the higher you go, the more it breaks up."
//
// v2 architecture (matches Ableton Texture's Flux behavior):
//   1. Run input through SignalsmithEngine for time-stretch as usual.
//   2. Write Signalsmith's OUTPUT into a circular history buffer (~500 ms).
//   3. Read from history at a variable offset (0 = "live"). Every
//      jumpInterval samples, pick a NEW random offset in [0, maxJumpOffset].
//   4. Crossfade between the old and new offset over ~5 ms so each jump
//      is a tight glitch instead of a click.
//
// "Higher you go = more breakup" is wired by scaling chaos with stretch:
//   stretchRatio = 1  → jumps every 250 ms, max offset 50 ms  (subtle wobble)
//   stretchRatio = 4  → jumps every 130 ms, max offset 170 ms (clearly glitchy)
//   stretchRatio = 10 → jumps every 50 ms,  max offset 400 ms (chaotic)
//
// Net character vs Tones: smooth spectral stretch as the underlying engine,
// but the output skips around in recent history at the Flux rate — creates
// the "stutter / skip / repeat" glitch breakup that Ableton's Texture
// signatures. Especially audible on pads, vocals, ambient material.
//
// RT-safety: process() is allocation-free after prepare(). Scratch buffer
// grows on the audio thread only when numSamples exceeds prior capacity
// (rare; typically constant per session).
//
#pragma once

#include <juce_core/juce_core.h>
#include "SignalsmithEngine.h"
#include <cmath>
#include <cstring>

namespace tw
{
    class TextureEngine
    {
    public:
        TextureEngine() = default;

        void prepare (double sampleRateHz, int numChannels, int blockSize)
        {
            sampleRate = sampleRateHz;
            channels   = juce::jlimit (1, 2, numChannels);
            inner.prepare (sampleRateHz, numChannels, blockSize);

            // History buffer: 500 ms minimum, power-of-two for fast mask wrap.
            int target = 1;
            while (target < (int) (sampleRateHz * 0.5)) target *= 2;
            if (target < 8192) target = 8192;
            historyL.realloc ((size_t) target);
            historyR.realloc ((size_t) target);
            std::memset (historyL.getData(), 0, (size_t) target * sizeof (float));
            std::memset (historyR.getData(), 0, (size_t) target * sizeof (float));
            historyMask = target - 1;
            historySize = target;

            // Scratch for Signalsmith output before scattering.
            scratchCapacity = juce::jmax (1, blockSize);
            scratchL.realloc ((size_t) scratchCapacity);
            scratchR.realloc ((size_t) scratchCapacity);

            xfadeLen = juce::jmax (32, (int) (sampleRateHz * 0.005));  // 5 ms crossfade

            ready = true;
            reset();
        }

        void reset()
        {
            if (! ready) return;
            inner.reset();
            historyWriteIdx       = 0;
            currentReadOffset     = 0;
            targetReadOffset      = 0;
            samplesUntilNextJump  = (int) (sampleRate * 0.080);  // first jump at ~80 ms in
            xfadePos              = 0;
            std::memset (historyL.getData(), 0, (size_t) historySize * sizeof (float));
            std::memset (historyR.getData(), 0, (size_t) historySize * sizeof (float));
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
            inner.setPitchSemitones (basePitchSemitones);
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

            if (numSamples > scratchCapacity)
            {
                scratchCapacity = numSamples;
                scratchL.realloc ((size_t) scratchCapacity);
                scratchR.realloc ((size_t) scratchCapacity);
            }
            auto* sL = scratchL.getData();
            auto* sR = scratchR.getData();

            // Signalsmith handles the time-stretch + pitch.
            inner.process (inL, inR, sL, sR, numSamples);

            // Map stretchRatio to chaos: ratio=1 → 0 (mild), ratio=10 → 1 (heavy).
            const float chaosNorm = juce::jlimit (0.0f, 1.0f,
                                                  (baseStretchRatio - 1.0f) / 9.0f);
            const int jumpIntervalSamples = juce::jmax (1200,
                12000 - (int) (chaosNorm * 9600.0f));  // 250 ms → 50 ms
            const int maxOffsetUpperBound = juce::jmax (1024,
                historySize - numSamples - xfadeLen - 256);
            const int maxJumpOffset = juce::jmin (maxOffsetUpperBound,
                (int) ((0.050 + chaosNorm * 0.350) * sampleRate));  // 50 ms → 400 ms

            for (int i = 0; i < numSamples; ++i)
            {
                // Write Signalsmith's output to history.
                const int writeAt = historyWriteIdx & historyMask;
                historyL[writeAt] = sL[i];
                historyR[writeAt] = sR[i];
                historyWriteIdx++;

                // Time for a new jump?
                if (--samplesUntilNextJump <= 0)
                {
                    samplesUntilNextJump = jumpIntervalSamples;
                    // If a previous crossfade is still in progress, commit
                    // it (move current to its target) before starting the
                    // new one. Prevents stuck-mid-fade glitches if jumps
                    // pile up faster than the crossfade can complete.
                    if (xfadePos > 0)
                        currentReadOffset = targetReadOffset;
                    targetReadOffset = rng.nextInt (maxJumpOffset + 1);
                    xfadePos = xfadeLen;
                }

                // Read from history at the current offset (offset 0 = "just-
                // written sample = live"). historyWriteIdx is now one past
                // the last write, so the live sample sits at -1.
                const int curIdx = (historyWriteIdx - 1 - currentReadOffset) & historyMask;
                float L = historyL[curIdx];
                float R = historyR[curIdx];

                if (xfadePos > 0)
                {
                    const int newIdx = (historyWriteIdx - 1 - targetReadOffset) & historyMask;
                    const float newL = historyL[newIdx];
                    const float newR = historyR[newIdx];

                    const float t = 1.0f - (float) xfadePos / (float) xfadeLen;
                    const float a = std::cos (t * juce::MathConstants<float>::halfPi);
                    const float b = std::sin (t * juce::MathConstants<float>::halfPi);
                    L = L * a + newL * b;
                    R = R * a + newR * b;

                    if (--xfadePos == 0)
                        currentReadOffset = targetReadOffset;
                }

                outL[i] = L;
                outR[i] = R;
            }
        }

    private:
        SignalsmithEngine    inner;
        juce::HeapBlock<float> historyL, historyR;
        juce::HeapBlock<float> scratchL, scratchR;
        juce::Random         rng;

        int  historySize          = 0;
        int  historyMask          = 0;
        int  historyWriteIdx      = 0;
        int  scratchCapacity      = 0;

        int  currentReadOffset    = 0;     // samples behind live the main read sits at
        int  targetReadOffset     = 0;     // samples behind live the upcoming jump targets
        int  samplesUntilNextJump = 0;     // countdown to next random jump
        int  xfadePos             = 0;     // 0 = no fade; >0 = samples remaining in fade-out of current
        int  xfadeLen             = 240;   // 5 ms @ 48 kHz

        double sampleRate         = 48000.0;
        int    channels           = 2;
        bool   ready              = false;

        float baseStretchRatio    = 1.0f;
        float basePitchSemitones  = 0.0f;
    };
}
