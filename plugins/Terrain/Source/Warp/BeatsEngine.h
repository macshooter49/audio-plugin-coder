// BeatsEngine.h — v11 (skip-aware boundary fade; replaces v7 rate-gate)
//
// v11 (2026-05-22): the v7 binary rate-gate ("disable boundary fade above
// 20 Hz beat rate") was correct for the C4–C7 ring-mod scenario at
// stretchRatio=1 (where the boundary IS continuous and a fade just adds
// AM with nothing to mask) but wrong at stretchRatio < 1 — there the
// boundary has a REAL source-position skip (3360 source samples at
// stretchRatio=0.30) and a bare boundary becomes a hard step click at
// the beat rate. User: "at 0.30 it's damn near unusable, it's still
// clicking."
//
// v11 replaces the rate gate with a skip-aware gate. cyclePos at the
// final sample of each beat is computed analytically from the engine
// parameters; the boundary skip is then `grainSize - cyclePos_last -
// pitchRatio`. Skip ≈ 0 → no fade (preserves v7's "no AM at clean
// boundary" intent). Skip > 0 → apply a v10-style constant-depth scaled
// fade so AM sidebands stay at ~−16 dB regardless of beat rate. At
// extreme rates the fade gets sub-millisecond but stays alive, trading
// hard 0-dB step harmonics for soft −16 dB AM sidebands.
//
// At stretchRatio=0.30 the boundary skip click is replaced with a
// smooth amplitude dip at the beat rate — still a perceptible rhythmic
// modulation (the "BEATS at low stretch is fundamentally a skip-based
// algorithm" character is intrinsic and not removable without rewriting
// the engine), but FAR less offensive than the broadband step click.
//
// Earlier versions kept for reference:
//
// BeatsEngine.h — v7 (high-octave de-roboticizer — gate the boundary fade)
//
// v6 introduced a 2 ms Hann fade-in + fade-out at every beat boundary to
// mask the source-position discontinuity that occurs when loopAnchor
// advances and cyclePos resets to 0. At low pitchRatio that fade runs at
// the beat rate (sampleRate * pitchRatio / (grainSize * stretchRatio))
// which is 5–10 Hz — sub-audible, perceived as benign tremolo, and it
// actually masks the slow boundary click.
//
// At HIGH pitchRatio the beat rate climbs INTO audible range. At pitch
// shift +12 semis (pitchRatio=2) the beat rate is 20 Hz; at +24 semis
// (pitchRatio=4, the clamp) it's 40 Hz. The boundary fade then becomes
// AM at the beat rate, producing inharmonic sidebands (±beatRate around
// every spectral component) — classic ring-mod "robotic" character that
// the user reported at C4–C7 on a 48-key keyboard. The trapezoid envelope
// also has harmonics at 80, 120, 160 Hz which add more sidebands.
//
// v7 fix: gate the boundary fade on beat rate. Apply only when the beat
// rate is sub-audible (outputsPerLoop > sampleRate / 20 = beat duration
// > 50 ms). Above that, beatFadeLen = 0 → no AM, no fade — we accept the
// slight boundary click as the lesser evil. The v6 "fade-in only after
// first beat" rule is preserved (chop attack stays sharp at note-on).
//
// v6 changes preserved:
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

            // v11 boundary-fade decision — skip-aware + v10 constant-depth scaling.
            //
            // The v7 rate gate ("disable above 20 Hz beat rate") fixed the C4–C7
            // ring-mod (fade AM at audible beat rate when the boundary itself
            // was CONTINUOUS at stretchRatio=1 with NO skip to mask). But it
            // left bare-step clicks at stretchRatio < 1, where each beat
            // boundary jumps forward `grainSize - cyclePos_last - pitchRatio`
            // source samples (3360 at stretchRatio=0.30). User: "at 0.30 it's
            // damn near unusable... it's still clicking."
            //
            // Compute the analytical cyclePos at the final sample of each beat:
            //   - If `(outputsPerLoop-1) × pitchRatio < grainSize` → no wraps,
            //     cyclePos_last = that linear advance.
            //   - Else → cyclePos_last = (finalAdvance - k × effLoopLen) for
            //     the wrap count k that places cyclePos in [crossfadeLen, grainSize).
            //
            // Then boundarySkip = grainSize - cyclePos_last - pitchRatio. Skip ≈ 0
            // means the boundary is naturally continuous (stretchRatio=1, integer
            // multiples) — no fade needed, the v7 "no AM at clean boundary"
            // intent is preserved. Skip > pitchRatio×2 means there's a real
            // discontinuity to mask — apply a v10-style constant-depth fade
            // (period × 0.10 capped at boundaryFadeLen) so AM sidebands stay
            // around −16 dB regardless of how fast the beat rate climbs. At
            // high beat rates the fade gets short (sub-millisecond) but stays
            // alive; at sub-audio rates it tops out at the configured 2 ms.
            const double finalAdvance =
                (double) (outputsPerLoop - 1) * pitchRatio;
            double cyclePosLast;
            if (finalAdvance < (double) targetGrainSize)
            {
                cyclePosLast = finalAdvance;
            }
            else
            {
                const double afterFirstWrap = finalAdvance - (double) targetGrainSize;
                const double wrapsAfterFirst = std::floor (afterFirstWrap / effLoopLen) + 1.0;
                cyclePosLast = finalAdvance - wrapsAfterFirst * effLoopLen;
            }
            const double boundarySkipSamples =
                (double) targetGrainSize - cyclePosLast - pitchRatio;
            const bool boundaryContinuous = boundarySkipSamples < pitchRatio * 2.0 + 1.0;

            int beatFadeLen = 0;
            if (! boundaryContinuous)
            {
                double fadeTarget = (double) boundaryFadeLen;  // ≤ 2 ms baseline cap
                const double beatPeriod = (double) outputsPerLoop;
                if (beatPeriod < sampleRate / 5.0)            // > 5 Hz beat rate
                    fadeTarget = juce::jmin (fadeTarget, beatPeriod * 0.10);
                beatFadeLen = juce::jmax (1, (int) std::round (
                    juce::jmin (fadeTarget, (double) outputsPerLoop / 4.0)));
            }

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
