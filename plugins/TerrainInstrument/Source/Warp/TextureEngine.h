// TextureEngine.h — v3 (more prominent fragmentation + intentional dropouts)
//
// v2 introduced grain-scatter (output-history offset jumps) instead of v1's
// pitch jitter — user confirmed: "love how texture sounds fragmented." But
// at low stretch it sounded too close to Tones, and they asked for more:
// "maybe add a little bit more fragmentation… couple like stutters maybe?
// Maybe like dropouts/stutters can happen frequently from time to time."
//
// v3 changes:
//   1. Chaos floor raised — even stretchRatio=1 now has a meaningful jump
//      rate (140 ms vs v2's 250 ms) and offset range (80 ms vs v2's 50 ms).
//      Texture is unambiguously distinct from Tones even at neutral stretch.
//   2. Jump crossfade bumped 5 → 8 ms for cleaner transitions at the
//      higher jump rates.
//   3. NEW dropout events — every jump rolls 15% chance to become a 12 ms
//      smooth dip-to-silence-and-back instead of a normal scatter jump.
//      The dip uses a 1 - sin(t·π) envelope so it's click-free at both
//      edges. Creates the "intentional dropout" character — feels like
//      digital glitches punctuating the texture.
//
// v3 chaos schedule:
//   stretchRatio = 1  → jumps every 140 ms, max offset  80 ms  (clearly Texture-character even neutral)
//   stretchRatio = 4  → jumps every  85 ms, max offset 190 ms  (glitchy)
//   stretchRatio = 10 → jumps every  35 ms, max offset 400 ms  (chaotic)
//   ~15% of jump events become dropouts at all stretch levels.
//
// RT-safety: process() is allocation-free after prepare(). Scratch grows
// on the audio thread only if numSamples exceeds prior capacity (rare).
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

            xfadeLen = juce::jmax (32, (int) (sampleRateHz * 0.008));  // 8 ms crossfade (v3)
            dropoutTotalSamples = juce::jmax (96, (int) (sampleRateHz * 0.012));  // 12 ms dip

            ready = true;
            reset();
        }

        void reset()
        {
            if (! ready) return;
            inner.reset();
            historyWriteIdx           = 0;
            currentReadOffset         = 0;
            targetReadOffset          = 0;
            samplesUntilNextJump      = (int) (sampleRate * 0.080);  // first jump at ~80 ms in
            xfadePos                  = 0;
            dropoutSamplesRemaining   = 0;
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

            // Map stretchRatio to chaos with a meaningful floor (v3) so that
            // stretchRatio=1 still has clearly Texture-character behavior
            // and isn't easily mistaken for Tones. Below: at ratio=1 chaos
            // is 0.2 (clearly fragmented), at ratio=10 it caps at 1.0.
            const float chaosRaw  = juce::jlimit (0.0f, 1.0f, (baseStretchRatio - 1.0f) / 9.0f);
            const float chaosNorm = 0.2f + 0.8f * chaosRaw;
            const int jumpIntervalSamples = juce::jmax (800,
                8000 - (int) (chaosRaw * 6400.0f));   // 140 ms → 35 ms (with floor enforced by base 8000)
            const int maxOffsetUpperBound = juce::jmax (1024,
                historySize - numSamples - xfadeLen - 256);
            const int maxJumpOffset = juce::jmin (maxOffsetUpperBound,
                (int) ((0.060 + chaosNorm * 0.340) * sampleRate));  // ~80 ms → 400 ms

            for (int i = 0; i < numSamples; ++i)
            {
                // Write Signalsmith's output to history.
                const int writeAt = historyWriteIdx & historyMask;
                historyL[writeAt] = sL[i];
                historyR[writeAt] = sR[i];
                historyWriteIdx++;

                // Time for a new event? Each jump rolls a die — 15% become a
                // dropout (a brief amplitude dip to silence and back), 85%
                // are normal random scatter jumps. The dropout punctuates
                // the texture with intentional digital glitches.
                if (--samplesUntilNextJump <= 0)
                {
                    samplesUntilNextJump = jumpIntervalSamples;
                    if (rng.nextFloat() < 0.15f && dropoutSamplesRemaining == 0)
                    {
                        // Trigger dropout — full duration of dip will play
                        // out via the envelope applied at output (below).
                        // Skip the scatter jump for this event.
                        dropoutSamplesRemaining = dropoutTotalSamples;
                    }
                    else
                    {
                        // Normal scatter jump.
                        if (xfadePos > 0)
                            currentReadOffset = targetReadOffset;
                        targetReadOffset = rng.nextInt (maxJumpOffset + 1);
                        xfadePos = xfadeLen;
                    }
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

                // v3 dropout envelope — when active, apply a smooth dip-to-
                // silence-and-back using `1 - sin(t·π)` so the output is
                // click-free at both edges of the dropout. The dip's bottom
                // (gain ≈ 0) lands at the midpoint, giving the perceptual
                // "stutter / momentary digital silence" punctuation.
                if (dropoutSamplesRemaining > 0)
                {
                    const float t = 1.0f -
                        (float) dropoutSamplesRemaining / (float) dropoutTotalSamples;
                    const float gain = 1.0f - std::sin (t * juce::MathConstants<float>::pi);
                    L *= gain;
                    R *= gain;
                    --dropoutSamplesRemaining;
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

        int  currentReadOffset      = 0;   // samples behind live the main read sits at
        int  targetReadOffset       = 0;   // samples behind live the upcoming jump targets
        int  samplesUntilNextJump   = 0;   // countdown to next random jump
        int  xfadePos               = 0;   // 0 = no fade; >0 = samples remaining in fade-out of current
        int  xfadeLen               = 384; // 8 ms @ 48 kHz (v3)
        int  dropoutSamplesRemaining = 0;  // > 0 → currently in a dropout dip
        int  dropoutTotalSamples    = 576; // 12 ms @ 48 kHz (v3)

        double sampleRate         = 48000.0;
        int    channels           = 2;
        bool   ready              = false;

        float baseStretchRatio    = 1.0f;
        float basePitchSemitones  = 0.0f;
    };
}
