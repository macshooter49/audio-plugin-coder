// BeatsEngine.h — v3 (no AM, crossfade only at within-beat wraps)
//
// Diagnosis of why v2 sounded crackly/robotic/inharmonic:
//   - v2 applied a Hann window throughout EVERY grain cycle, not just at
//     wraps. That windowing modulates amplitude at the grain rate (33 Hz
//     for a 30 ms grain) — i.e., 33 Hz tremolo across all audio, all the
//     time, even at stretchRatio = 1. Amplitude modulation of a tonal
//     signal at 33 Hz creates ±33 Hz sidebands around every partial:
//     those sidebands are exactly the "inharmonic frequencies" the user
//     heard.
//   - The window also dipped to zero at every cycle boundary even when
//     no wrap was happening, creating the periodic "robotic" pulsing.
//
// v3 fixes:
//   - NO constant windowing. Reads are clean linear-interp lookups.
//   - At within-beat wraps (when the loop is about to repeat WITHIN the
//     same beat — only when stretchRatio > pitchRatio so the loop has
//     to repeat), a short equal-power cos/sin crossfade with the loop's
//     opening samples smooths the cycle boundary. Between beats (where
//     the source is naturally continuous) no crossfade — the audio
//     advances cleanly.
//   - stretchRatio + pitchRatio combined into a single outputsPerLoop
//     formula. At stretchRatio = 1 and pitchRatio = 1 this is exactly
//     one cycle per beat with sequential reads — bit-identical
//     pass-through. No artifacts at unity.
//   - Grain size bumped from 30 ms → 60 ms. Slower wrap rate, more
//     musical-feeling stutter at higher stretches.
//   - Crossfade length bumped from ~2 ms → ~10 ms. More gradual.
//
// RT-safety: process() is allocation-free.
//
#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <cstring>

namespace tw
{
    class BeatsEngine
    {
    public:
        BeatsEngine() = default;

        void prepare (double sampleRateHz, int numChannels, int /*blockSize*/)
        {
            sampleRate = sampleRateHz;
            channels   = juce::jlimit (1, 2, numChannels);

            int target = 1;
            while (target < (int) (sampleRate * 4.0)) target *= 2;
            if (target < 32768) target = 32768;
            historyL.realloc ((size_t) target);
            historyR.realloc ((size_t) target);
            std::memset (historyL.getData(), 0, (size_t) target * sizeof (float));
            std::memset (historyR.getData(), 0, (size_t) target * sizeof (float));
            historyMask = target - 1;

            // 60 ms grain — wraps at ~16 Hz, comfortably sub-audio. 10 ms
            // crossfade region for soft loop boundaries.
            targetGrainSize = juce::jlimit (1024, 8192, (int) (sampleRate * 0.060));
            crossfadeLen    = juce::jlimit (64, 1024, (int) (sampleRate * 0.010));

            ready = true;
            reset();
        }

        void reset() noexcept
        {
            historyWriteIdx    = 0;
            loopAnchor         = 0;
            outputsThisLoop    = 0;
            firstBlockPending  = true;
        }

        bool isReady()      const noexcept { return ready; }
        int  inputLatency() const noexcept { return targetGrainSize; }

        void setStretchRatio (float r) noexcept
        {
            stretchRatio = juce::jlimit (0.1f, 15.0f, r);
        }

        void setPitchSemitones (float s) noexcept
        {
            pitchSemitones = juce::jlimit (-24.0f, 24.0f, s);
        }

        void seek (const float* primeL, const float* primeR, int n)
        {
            if (! ready || n <= 0) return;
            for (int i = 0; i < n; i++)
            {
                const int writeAt = historyWriteIdx & historyMask;
                historyL[writeAt] = primeL ? primeL[i] : 0.0f;
                historyR[writeAt] = primeR ? primeR[i] : (primeL ? primeL[i] : 0.0f);
                historyWriteIdx++;
            }
        }

        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;
            jassert (inL != outL && inR != outR);

            const int inputLen = juce::jmax (1, (int) std::round (
                (double) numSamples / (double) stretchRatio));
            const double pitchRatio = std::pow (2.0, (double) pitchSemitones / 12.0);

            // Append this block's input to history.
            for (int i = 0; i < inputLen; i++)
            {
                const int writeAt = historyWriteIdx & historyMask;
                historyL[writeAt] = inL[i];
                historyR[writeAt] = inR[i];
                historyWriteIdx++;
            }

            // Bootstrap — wait for a full grain of history (seek priming
            // should have provided it).
            if (firstBlockPending)
            {
                if (historyWriteIdx >= targetGrainSize)
                {
                    loopAnchor = 0;
                    outputsThisLoop = 0;
                    firstBlockPending = false;
                }
                else
                {
                    std::memset (outL, 0, sizeof (float) * (size_t) numSamples);
                    std::memset (outR, 0, sizeof (float) * (size_t) numSamples);
                    return;
                }
            }

            // outputsPerLoop combines stretch and pitch. At unity settings
            // this equals grainSize → exactly one cycle per beat, reads are
            // sequential, output = input. Bit-identical pass-through.
            // At stretchRatio > pitchRatio, outputsPerLoop > grainSize → the
            // grain wraps inside the beat (stutter). At pitchRatio >
            // stretchRatio, outputsPerLoop < grainSize → beat advances
            // mid-grain (chipmunk).
            const int outputsPerLoop = juce::jmax (1, (int) std::round (
                (double) targetGrainSize * (double) stretchRatio / pitchRatio));

            for (int i = 0; i < numSamples; i++)
            {
                if (outputsThisLoop >= outputsPerLoop)
                {
                    loopAnchor += targetGrainSize;
                    outputsThisLoop = 0;
                }

                const double phase    = (double) outputsThisLoop * pitchRatio;
                double cyclePos       = std::fmod (phase, (double) targetGrainSize);
                if (cyclePos < 0) cyclePos += targetGrainSize;

                // Look ahead one sample to decide whether the next sample
                // will wrap within the same beat — only THEN does the
                // crossfade need to kick in.
                const double nextPhase    = (double) (outputsThisLoop + 1) * pitchRatio;
                const double nextCyclePos = std::fmod (nextPhase, (double) targetGrainSize);
                const bool   willWrap     = nextCyclePos < cyclePos;
                const bool   stillInBeat  = (outputsThisLoop + 1) < outputsPerLoop;
                const bool   wrapImminent = willWrap && stillInBeat;

                // Main read. idx1 wraps within grain ONLY if we're heading
                // into a within-beat wrap — otherwise it advances naturally
                // into the next source sample (which is the next beat at
                // the end of a beat, giving clean inter-beat continuity).
                const int pos0 = (int) cyclePos;
                int pos1;
                if (wrapImminent)
                    pos1 = (pos0 + 1) % targetGrainSize;
                else
                    pos1 = pos0 + 1;

                const float frac = (float) (cyclePos - pos0);
                const int idx0 = (loopAnchor + pos0) & historyMask;
                const int idx1 = (loopAnchor + pos1) & historyMask;
                float sL = historyL[idx0] + frac * (historyL[idx1] - historyL[idx0]);
                float sR = historyR[idx0] + frac * (historyR[idx1] - historyR[idx0]);

                // Crossfade with the loop's opening over the last
                // crossfadeLen samples of a cycle — but only when the wrap
                // is genuinely a within-beat wrap. Between beats, source
                // is naturally continuous so no crossfade is needed.
                if (wrapImminent && pos0 >= targetGrainSize - crossfadeLen)
                {
                    // Position the fade-in head reads from at the START of
                    // the loop, advancing in lockstep with the main head's
                    // distance into the crossfade region.
                    const double startCyclePos = cyclePos - (targetGrainSize - crossfadeLen);
                    const int    sPos0 = (int) startCyclePos;
                    const int    sPos1 = (sPos0 + 1) % targetGrainSize;
                    const float  sFrac = (float) (startCyclePos - sPos0);
                    const int    sIdx0 = (loopAnchor + sPos0) & historyMask;
                    const int    sIdx1 = (loopAnchor + sPos1) & historyMask;
                    const float  startL = historyL[sIdx0] + sFrac * (historyL[sIdx1] - historyL[sIdx0]);
                    const float  startR = historyR[sIdx0] + sFrac * (historyR[sIdx1] - historyR[sIdx0]);

                    const float t = (float) ((cyclePos - (targetGrainSize - crossfadeLen)) / crossfadeLen);
                    // Equal-power crossfade — cos²+sin²=1 preserves
                    // perceived loudness across the boundary.
                    const float a = std::cos (t * juce::MathConstants<float>::halfPi);
                    const float b = std::sin (t * juce::MathConstants<float>::halfPi);
                    sL = sL * a + startL * b;
                    sR = sR * a + startR * b;
                }

                outL[i] = sL;
                outR[i] = sR;
                outputsThisLoop++;
            }
        }

    private:
        juce::HeapBlock<float> historyL, historyR;
        int historyMask        = 0;
        int historyWriteIdx    = 0;
        int loopAnchor         = 0;
        int outputsThisLoop    = 0;
        int targetGrainSize    = 2880;
        int crossfadeLen       = 480;
        bool firstBlockPending = true;

        double sampleRate     = 48000.0;
        int    channels       = 2;
        float  stretchRatio   = 1.0f;
        float  pitchSemitones = 0.0f;
        bool   ready          = false;
    };
}
