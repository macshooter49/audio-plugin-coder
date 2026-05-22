// BeatsEngine.h — v6 (heavier de-clicking — larger grain + longer xfade + boundary fade)
//
// v5 fixed the two structural bugs that were producing audible clicks
// (broadened the crossfade gate so it actually ran across its full window,
// and corrected SamplerVoice's source-feed length so pitchRatio>1 didn't
// starve the history). v6 turns the dial further:
//
// 1. Wrap rate halved: grain 60 ms → 100 ms (wrap rate 16 Hz → ~10 Hz).
//    Fewer wraps per second = fewer perceptual click events per second.
//    Effective loop after the first cycle's plain opening is 50 ms.
//
// 2. Crossfade doubled: 25 ms → 50 ms. The longer fade-in delay means the
//    start head's transient at source[0] enters at much lower amplitude
//    per sample (b'(0) is still π/2 but t=1/crossfadeLen halves, so b at
//    sample 1 halves). Transient re-fire is much less prominent.
//
// 3. Beat-boundary fade-in/out: 2 ms Hann fade-out at the last samples of
//    every beat and 2 ms fade-in at the start of every beat *after the
//    first*. The total ~4 ms output-amplitude dip at the boundary masks
//    the loopAnchor-advance source-position discontinuity that v5 could
//    not address. We deliberately skip the fade-in on the FIRST beat so
//    the chop's note-on attack stays sharp.
//
// 4. Start-head transient softener: when the start head is at
//    startCyclePos < innerFadeLen (≈1 ms), multiply its read by a fade-in
//    factor. This further suppresses a sharp source[0] transient (kick
//    hit, snare attack) from re-firing on every grain wrap. Doesn't
//    affect cycle 1's plain opening read because that's done via the
//    main head, not the start head.
//
// v6 keeps the v5 architecture intact:
//   - Post-wrap cyclePos resets to crossfadeLen (the start head's last
//     position is source[crossfadeLen-1]; main resumes at
//     source[crossfadeLen] — continuous).
//   - Crossfade gate: inCrossfadeRegion && wrapWithinBeat. The gate spans
//     the full crossfadeLen tail.
//   - Per-mode source-feed math lives in WarpProcessor::sourceSamplesPerBlock.
//
// Unity stretchRatio + unity pitchRatio is still bit-identical passthrough
// EXCEPT for the boundary fade (2 ms output dip every grain at the beat
// boundary). For passthrough use the user should set warpMode = None.
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

            // v6: 100 ms grain → wrap rate ≈ 10 Hz (was 16 Hz at 60 ms).
            // crossfadeLen 50 ms → effective loop = grainSize - crossfadeLen = 50 ms.
            targetGrainSize = juce::jlimit (1024, 8192, (int) (sampleRate * 0.100));
            crossfadeLen    = juce::jlimit (64,   targetGrainSize / 2 - 1,
                                            (int) (sampleRate * 0.050));

            // v6 boundary smoothing windows (caller-block-fixed, not pitch-scaled):
            //   boundaryFadeLen = 2 ms → ~4 ms amplitude dip at each beat boundary
            //   innerFadeLen    = 1 ms → softens start-head transient at source[0]
            boundaryFadeLen = juce::jmax (16, (int) (sampleRate * 0.002));
            innerFadeLen    = juce::jmax (8,  (int) (sampleRate * 0.001));

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
            beatCount          = 0;
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
                    beatCount = 0;
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

            // Cap the boundary fade in cases where the beat itself is shorter
            // than 4 × boundaryFadeLen (otherwise the fade-out and fade-in
            // overlap and we lose audible content). Stays at the configured
            // 2 ms in normal use.
            const int beatFadeLen = juce::jmin (boundaryFadeLen,
                                                juce::jmax (1, outputsPerLoop / 4));

            for (int i = 0; i < numSamples; i++)
            {
                if (outputsThisLoop >= outputsPerLoop)
                {
                    loopAnchor += targetGrainSize;
                    outputsThisLoop = 0;
                    cyclePos = 0.0;
                    beatCount++;
                }

                // Crossfade detector (v5 — broadened gate, full crossfadeLen tail).
                const double remGrain = (double) targetGrainSize - cyclePos;
                const int samplesToWrap    = (int) std::ceil (remGrain / pitchRatio);
                const int samplesToBeatEnd = outputsPerLoop - outputsThisLoop;
                const bool wrapWithinBeat  = samplesToWrap < samplesToBeatEnd;
                const bool inCrossfadeRegion = cyclePos >= crossfadeBegin;
                const bool doCrossfade     = wrapWithinBeat && inCrossfadeRegion;

                // Main read.
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
                    // Start head (reading current grain's [0..crossfadeLen) so the
                    // wrap is continuous at the algorithm boundary).
                    const double startCyclePos = cyclePos - crossfadeBegin;
                    const int    sPos0 = (int) startCyclePos;
                    const int    sPos1 = sPos0 + 1;  // < crossfadeLen < grainSize/2 — always in grain
                    const float  sFrac = (float) (startCyclePos - sPos0);
                    const int    sIdx0 = (loopAnchor + sPos0) & historyMask;
                    const int    sIdx1 = (loopAnchor + sPos1) & historyMask;
                    float startL = historyL[sIdx0] + sFrac * (historyL[sIdx1] - historyL[sIdx0]);
                    float startR = historyR[sIdx0] + sFrac * (historyR[sIdx1] - historyR[sIdx0]);

                    // v6 start-head transient softener: apply a 1 ms fade-in to
                    // the start head's read at source[0..innerFadeLen]. Suppresses
                    // sharp source[0] transients (kick/snare attack) from re-firing
                    // at every grain wrap. Doesn't affect the main head, so cycle
                    // 1's plain opening is untouched.
                    if (startCyclePos < (double) innerFadeLen)
                    {
                        const float ts = (float) (startCyclePos / (double) innerFadeLen);
                        const float gateGain = std::sin (ts * juce::MathConstants<float>::halfPi);
                        startL *= gateGain;
                        startR *= gateGain;
                    }

                    const float t = juce::jlimit (0.0f, 1.0f,
                        (float) ((cyclePos - crossfadeBegin) / (double) crossfadeLen));
                    const float a = std::cos (t * juce::MathConstants<float>::halfPi);
                    const float b = std::sin (t * juce::MathConstants<float>::halfPi);
                    sL = sL * a + startL * b;
                    sR = sR * a + startR * b;
                }

                // v6 beat-boundary fade. Fade-in only applies for beats AFTER the
                // first (beatCount > 0) so the chop's note-on attack stays sharp.
                float beatFade = 1.0f;
                if (beatCount > 0 && outputsThisLoop < beatFadeLen)
                {
                    const float tin = (float) outputsThisLoop / (float) beatFadeLen;
                    beatFade *= std::sin (tin * juce::MathConstants<float>::halfPi);
                }
                const int samplesUntilBeatEnd = outputsPerLoop - outputsThisLoop - 1;
                if (samplesUntilBeatEnd < beatFadeLen)
                {
                    const float tout = (float) samplesUntilBeatEnd / (float) beatFadeLen;
                    beatFade *= std::sin (tout * juce::MathConstants<float>::halfPi);
                }

                outL[i] = sL * beatFade;
                outR[i] = sR * beatFade;

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
        int targetGrainSize    = 4800;   // v6: 100 ms @ 48k
        int crossfadeLen       = 2400;   // v6: 50 ms @ 48k
        int boundaryFadeLen    = 96;     // v6: ~2 ms @ 48k
        int innerFadeLen       = 48;     // v6: ~1 ms @ 48k
        double cyclePos        = 0.0;
        bool firstBlockPending = true;
        int  beatCount         = 0;      // 0 during first beat; >0 after first boundary

        double sampleRate     = 48000.0;
        int    channels       = 2;
        float  stretchRatio   = 1.0f;
        float  pitchSemitones = 0.0f;
        bool   ready          = false;
    };
}
