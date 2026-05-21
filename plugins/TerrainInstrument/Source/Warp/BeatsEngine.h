// BeatsEngine.h — v4 (wrap-to-crossfadeLen — eliminates post-wrap click)
//
// Diagnosis of remaining clicks in v3:
//   v3's crossfade at the end of a cycle transitioned the audio from
//   source[grainSize - crossfadeLen..grainSize-1] (main head, fading out)
//   to source[0..crossfadeLen-1] (fade-in head, fading in). At the end
//   of the crossfade region, the fade-in head was at source[crossfadeLen-1].
//   After the wrap, the main head reset to cyclePos = 0 — i.e. it started
//   reading source[0]. That's a JUMP BACKWARD by (crossfadeLen - 1)
//   samples in source. For tonal material this is approximately
//   continuous, but for drum loops with a transient at sample 0 it's an
//   audible click on every wrap, regardless of how long the crossfade is.
//   Making the crossfade longer actually made the click WORSE because
//   the backward jump grew with it.
//
// v4 fix:
//   - After a within-beat wrap, cyclePos resets to crossfadeLen (not 0)
//     so the main head continues from EXACTLY where the fade-in head
//     was when the crossfade completed. Transition becomes
//     source[crossfadeLen-1] → source[crossfadeLen] = a single sample
//     forward. Continuous.
//   - Effective loop length after first cycle = grainSize - crossfadeLen.
//     source[0..crossfadeLen-1] is heard ONLY during fade-in regions
//     (and in cycle 1's plain opening).
//   - Crossfade lengthened from 10 ms → 25 ms ("extreme X fade" per the
//     user). With the wrap-to-crossfadeLen architecture this no longer
//     amplifies clicks — it just makes the loop seam more gradual.
//   - Unity stretchRatio with unity pitchRatio still passes through
//     bit-identically (outputsPerLoop = grainSize → one cycle per beat,
//     phase < grainSize the entire time, no wrap, no crossfade).
//
// Known remaining artifact: at beat boundaries (when loopAnchor advances
// by grainSize), if the cycle was mid-loop when the beat ended, there's
// a forward jump in source position. Audible once per beat (16 Hz at
// 60 ms grain) — much slower than the within-beat wrap rate that was
// the v3 click source. Can be smoothed in a future v5 with a beat-end
// crossfade if needed.
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

            // 60 ms grain — wraps at ~16 Hz, sub-audio. Crossfade region
            // extended to 25 ms for the user-requested "extreme X fade".
            targetGrainSize = juce::jlimit (1024, 8192, (int) (sampleRate * 0.060));
            crossfadeLen    = juce::jlimit (64,   targetGrainSize / 2 - 1,
                                            (int) (sampleRate * 0.025));

            ready = true;
            reset();
        }

        void reset() noexcept
        {
            historyWriteIdx    = 0;
            loopAnchor         = 0;
            outputsThisLoop    = 0;
            cyclePos           = 0.0;
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

            for (int i = 0; i < inputLen; i++)
            {
                const int writeAt = historyWriteIdx & historyMask;
                historyL[writeAt] = inL[i];
                historyR[writeAt] = inR[i];
                historyWriteIdx++;
            }

            if (firstBlockPending)
            {
                if (historyWriteIdx >= targetGrainSize)
                {
                    loopAnchor = 0;
                    outputsThisLoop = 0;
                    cyclePos = 0.0;
                    firstBlockPending = false;
                }
                else
                {
                    std::memset (outL, 0, sizeof (float) * (size_t) numSamples);
                    std::memset (outR, 0, sizeof (float) * (size_t) numSamples);
                    return;
                }
            }

            const int outputsPerLoop = juce::jmax (1, (int) std::round (
                (double) targetGrainSize * (double) stretchRatio / pitchRatio));
            const double effLoopLen = (double) (targetGrainSize - crossfadeLen);
            const double crossfadeBegin = (double) (targetGrainSize - crossfadeLen);

            for (int i = 0; i < numSamples; i++)
            {
                if (outputsThisLoop >= outputsPerLoop)
                {
                    loopAnchor += targetGrainSize;
                    outputsThisLoop = 0;
                    cyclePos = 0.0;  // Fresh cycle at each new beat
                }

                // Look-ahead phase test for the crossfade detector.
                const double nextCyclePos = advanceCyclePos (cyclePos, pitchRatio);
                const bool willWrap    = nextCyclePos < cyclePos;
                const bool stillInBeat = (outputsThisLoop + 1) < outputsPerLoop;
                const bool wrapImminent = willWrap && stillInBeat;

                // Main read at loopAnchor + cyclePos with within-grain wrap
                // for idx1 only when an in-beat wrap is imminent (otherwise
                // natural advance into next sample, including across beats).
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

                // Crossfade with start-of-loop position during the last
                // crossfadeLen samples of every within-beat wrap. After the
                // wrap, the main head continues at cyclePos = crossfadeLen,
                // so the fade-in head's last position (source[crossfadeLen
                // - 1]) is exactly one sample before the main head's first
                // post-wrap read (source[crossfadeLen]) — continuous.
                if (wrapImminent && cyclePos >= crossfadeBegin)
                {
                    const double startCyclePos = cyclePos - crossfadeBegin;  // 0..crossfadeLen
                    const int    sPos0 = (int) startCyclePos;
                    const int    sPos1 = (sPos0 + 1) % targetGrainSize;
                    const float  sFrac = (float) (startCyclePos - sPos0);
                    const int    sIdx0 = (loopAnchor + sPos0) & historyMask;
                    const int    sIdx1 = (loopAnchor + sPos1) & historyMask;
                    const float  startL = historyL[sIdx0] + sFrac * (historyL[sIdx1] - historyL[sIdx0]);
                    const float  startR = historyR[sIdx0] + sFrac * (historyR[sIdx1] - historyR[sIdx0]);

                    const float t = (float) ((cyclePos - crossfadeBegin) / (double) crossfadeLen);
                    const float a = std::cos (t * juce::MathConstants<float>::halfPi);
                    const float b = std::sin (t * juce::MathConstants<float>::halfPi);
                    sL = sL * a + startL * b;
                    sR = sR * a + startR * b;
                }

                outL[i] = sL;
                outR[i] = sR;

                // Advance cycle position. Wrap from grainSize back to
                // crossfadeLen so the main read continues smoothly from
                // where the fade-in head was.
                cyclePos += pitchRatio;
                if (cyclePos >= targetGrainSize)
                    cyclePos -= effLoopLen;

                outputsThisLoop++;
            }
        }

    private:
        double advanceCyclePos (double cur, double step) const noexcept
        {
            double n = cur + step;
            if (n >= (double) targetGrainSize)
                n -= (double) (targetGrainSize - crossfadeLen);
            return n;
        }

        juce::HeapBlock<float> historyL, historyR;
        int historyMask        = 0;
        int historyWriteIdx    = 0;
        int loopAnchor         = 0;
        int outputsThisLoop    = 0;
        int targetGrainSize    = 2880;
        int crossfadeLen       = 1200;
        double cyclePos        = 0.0;
        bool firstBlockPending = true;

        double sampleRate     = 48000.0;
        int    channels       = 2;
        float  stretchRatio   = 1.0f;
        float  pitchSemitones = 0.0f;
        bool   ready          = false;
    };
}
