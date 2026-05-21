// BeatsEngine.h — v5 (broadened crossfade gate — kills per-wrap clicks)
//
// Diagnosis of remaining clicks in v4:
//   v4 reset cyclePos to crossfadeLen after wrap (fixing v3's structural
//   backward jump), but the crossfade itself never actually ran across
//   its 25 ms window. The gate read:
//       if (wrapImminent && cyclePos >= crossfadeBegin)
//   where `wrapImminent = (nextCyclePos < cyclePos)`. At unity pitchRatio
//   (cyclePos += 1 per sample) the wrap condition `cyclePos + 1 >= grainSize`
//   is true only on the single last sample before wrap. So the gate
//   intersected to ~1 sample even though the inner math used a `t` that
//   ran 0..1 across the entire crossfadeLen-wide region. The "crossfade"
//   was effectively a 1-sample hard switch with t≈1 (output = start head
//   alone), producing an audible jump from source[grainSize-2] to
//   source[crossfadeLen-1] every wrap. At higher stretch the wrap rate
//   was 16–77 Hz → buzzy clicking.
//
// v5 fix (this file):
//   - Crossfade gate now `inCrossfadeRegion && wrapWithinBeat`:
//       inCrossfadeRegion = cyclePos >= crossfadeBegin           (~25 ms tail)
//       wrapWithinBeat    = samplesToWrap   < samplesToBeatEnd   (not preempted by beat boundary)
//     The crossfade now actually runs across its full window, producing
//     a smooth equal-power blend instead of a 1-sample snap.
//   - pos1 wrap (the within-grain interp pair wrap) still fires only when
//     pos0+1 would step off the grain — but is gated by the same
//     doCrossfade so beat-boundary samples keep the historic "read into
//     next beat" interp behavior (partial smoothing of beat boundary).
//
// Companion fix in SamplerVoice::renderWarp (v5):
//   - The input feed length had been computed `numSamples / stretchRatio`
//     which is correct for Signalsmith (handles pitch internally) but
//     starved BeatsEngine at pitchRatio > 1 — Beats advances cyclePos
//     through source at `pitchRatio` per output sample, so it consumes
//     grainSize source samples per beat regardless of pitch. With the
//     old formula, historyWriteIdx advanced by grainSize/pitchRatio per
//     beat while loopAnchor advanced by grainSize, so loopAnchor outran
//     the write head after a few beats. The engine then read stale
//     circular-buffer data → buzz / artifacts on any chromatic note
//     above the root.
//   - WarpProcessor now exposes `sourceSamplesPerBlock(numSamples)` which
//     returns the right input length per mode. For Beats that's
//     `numSamples * pitchRatio / stretchRatio`.
//
// Still-known artifact: beat-boundary discontinuity once per beat (much
// slower than per-grain wraps, typically 1–8 Hz). To fully smooth it we'd
// need to pre-buffer the next grain to crossfade across the boundary —
// deferred to v6 (raises inputLatency by crossfadeLen ≈ 25 ms).
//
// At stretchRatio=1.0 with pitchRatio=1.0 the engine is still bit-identical
// passthrough — cyclePos sweeps 0..grainSize-1 sequentially, no within-beat
// wrap, no crossfade, beat boundary reset is exactly one sample after the
// last grain sample.
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

            // 60 ms grain — wraps at ~16 Hz, sub-audio. Crossfade tail is
            // 25 ms ("extreme X fade" the user asked for); in v5's gate it
            // actually runs across the full 25 ms instead of collapsing to
            // a single sample.
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

            const double pitchRatio = std::pow (2.0, (double) pitchSemitones / 12.0);

            // SamplerVoice now feeds the right number of source samples per
            // block via WarpProcessor::sourceSamplesPerBlock — for Beats
            // that's numSamples * pitchRatio / stretchRatio. We can't easily
            // recover that here because we don't know the caller's numSamples
            // contract, so we just write what we were given (matches the
            // contract: pullSourceIntoScratch wrote `inputLen` samples to
            // inL/inR up to its return value).
            const int inputLen = juce::jmax (1, (int) std::round (
                (double) numSamples * pitchRatio / (double) stretchRatio));

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
            const double effLoopLen     = (double) (targetGrainSize - crossfadeLen);
            const double crossfadeBegin = (double) (targetGrainSize - crossfadeLen);

            for (int i = 0; i < numSamples; i++)
            {
                if (outputsThisLoop >= outputsPerLoop)
                {
                    loopAnchor += targetGrainSize;
                    outputsThisLoop = 0;
                    cyclePos = 0.0;  // Fresh cycle at each new beat
                }

                // Crossfade detector — fires across the FULL crossfadeLen
                // tail of every cycle whose wrap lands within the current
                // beat. v4 gated this on `wrapImminent` (the single sample
                // before wrap) which collapsed the 25 ms taper to 1 sample
                // and produced a hard switch — see file header.
                //
                // samplesToWrap: how many OUTPUT samples until cyclePos
                //                hits grainSize, computed at the current
                //                pitchRatio (cyclePos advances per sample
                //                at pitchRatio).
                // samplesToBeatEnd: how many output samples remain in this
                //                   beat. If the beat ends first, the
                //                   beat-boundary handler fires and the
                //                   in-grain wrap is preempted — skip the
                //                   crossfade so we don't smear into a
                //                   stale fade-in head.
                const double remGrain = (double) targetGrainSize - cyclePos;
                const int samplesToWrap    = (int) std::ceil (remGrain / pitchRatio);
                const int samplesToBeatEnd = outputsPerLoop - outputsThisLoop;
                const bool wrapWithinBeat  = samplesToWrap < samplesToBeatEnd;
                const bool inCrossfadeRegion = cyclePos >= crossfadeBegin;
                const bool doCrossfade     = wrapWithinBeat && inCrossfadeRegion;

                // Main read at loopAnchor + cyclePos with within-grain wrap
                // for idx1 ONLY at the very last sample of a within-beat
                // wrap. Outside the crossfade region, the natural advance
                // into the next sample (including across beat boundaries)
                // is preserved.
                const int pos0 = (int) cyclePos;
                int pos1;
                if (doCrossfade && pos0 + 1 >= targetGrainSize)
                    pos1 = 0;
                else
                    pos1 = pos0 + 1;
                const float frac = (float) (cyclePos - pos0);
                const int idx0 = (loopAnchor + pos0) & historyMask;
                const int idx1 = (loopAnchor + pos1) & historyMask;
                float sL = historyL[idx0] + frac * (historyL[idx1] - historyL[idx0]);
                float sR = historyR[idx0] + frac * (historyR[idx1] - historyR[idx0]);

                if (doCrossfade)
                {
                    // Start head reads source[0..crossfadeLen-1] of the
                    // current grain. Advances in lockstep with the main
                    // head (startCyclePos = cyclePos - crossfadeBegin, same
                    // per-sample increment). At wrap, the main head will
                    // resume at cyclePos = crossfadeLen exactly where the
                    // start head's last position (source[crossfadeLen - 1])
                    // ended — continuous.
                    const double startCyclePos = cyclePos - crossfadeBegin;  // 0..crossfadeLen
                    const int    sPos0 = (int) startCyclePos;
                    const int    sPos1 = sPos0 + 1;  // < crossfadeLen < grainSize/2 — always in grain
                    const float  sFrac = (float) (startCyclePos - sPos0);
                    const int    sIdx0 = (loopAnchor + sPos0) & historyMask;
                    const int    sIdx1 = (loopAnchor + sPos1) & historyMask;
                    const float  startL = historyL[sIdx0] + sFrac * (historyL[sIdx1] - historyL[sIdx0]);
                    const float  startR = historyR[sIdx0] + sFrac * (historyR[sIdx1] - historyR[sIdx0]);

                    const float t = juce::jlimit (0.0f, 1.0f,
                        (float) ((cyclePos - crossfadeBegin) / (double) crossfadeLen));
                    const float a = std::cos (t * juce::MathConstants<float>::halfPi);
                    const float b = std::sin (t * juce::MathConstants<float>::halfPi);
                    sL = sL * a + startL * b;
                    sR = sR * a + startR * b;
                }

                outL[i] = sL;
                outR[i] = sR;

                // Advance cycle position. Wrap from grainSize back to
                // crossfadeLen so the main read continues smoothly from
                // where the start head was at end of crossfade.
                cyclePos += pitchRatio;
                if (cyclePos >= targetGrainSize)
                    cyclePos -= effLoopLen;

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
