// BeatsEngine.h — v12 (CHOP-STRETCH: beat boundary = measured-skip CROSSFADE; activation fade)
//
// v12 (2026-09-23): the beat boundary no longer dips to zero — the old grain plays on through the
// boundary while the new beat fades in (equal-gain for a small/correlated skip, equal-power for a
// real jump), and the skip that decides it is measured off the read heads instead of predicted
// (v11's prediction called a real 2-sample jump "continuous"). A short slice whose first grain
// starts after silent pending blocks now fades in. Measured in Source/ChopStretch_test.cpp. The
// v11 notes below describe the retired dip.
//
// v11 (skip-aware boundary fade; replaced v7 rate-gate)
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
            startFadeLen    = juce::jmax (16, (int) (sampleRate * 0.003));   // CHOP-STRETCH house onset

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
            bxRemain           = 0;
            mainXf             = false;
            xfLatched          = false;
            silentBlocksOut    = false;
            startFadePos       = 1 << 30;
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
                    // CHOP-STRETCH — if silent blocks went out while the history filled (a slice
                    // shorter than one grain primes less than a grain), the first grain would START
                    // at full level mid-waveform: fade it in (equal-power, ~3 ms house onset).
                    if (silentBlocksOut) startFadePos = 0;
                }
                else
                {
                    std::memset (outL, 0, sizeof (float) * (size_t) numSamples);
                    std::memset (outR, 0, sizeof (float) * (size_t) numSamples);
                    silentBlocksOut = true;
                    return;
                }
            }

            const int outputsPerLoop = juce::jmax (1, (int) std::round (
                (double) targetGrainSize * (double) stretchRatio / pitchRatio));
            const double effLoopLen     = (double) (targetGrainSize - crossfadeLen);
            const double crossfadeBegin = (double) (targetGrainSize - crossfadeLen);

            // v11 boundary-fade decision (analytic skip + rate-gate) — RETIRED in v12, see below.
            //
            // CHOP-STRETCH (v12) — the boundary is now a CROSSFADE, not a dip, and the skip is MEASURED.
            //  (1) v6–v11 faded the old beat OUT to zero and the new one IN from zero (≤ 2 ms each side):
            //      no step, but a gated notch at the beat rate — on sustained material a tick every beat
            //      (Source/ChopStretch_test.cpp counts it as a dropout: one per beat on a stretched pad).
            //      Now the OLD grain keeps playing past the boundary (the history holds it — it is simply
            //      the source continuing) while the new beat fades in over it.
            //  (2) v11 predicted the skip analytically and called anything under 2·pitch+1 samples
            //      "continuous" (no fade at all). But crossfadeLen is clamped to grain/2 − 1, so at 2×
            //      stretch every beat really jumped 2 samples — a bare step at the beat rate (−30 dB HF
            //      click every 200 ms on a pad). The skip is now read off the heads themselves at the
            //      boundary: skip = (new read) − (where the old read goes next). Only |skip| < ¼ sample
            //      (truly continuous) goes without a fade.
            //  GAIN LAW (house, SampleEngine): a SMALL skip means the two reads are the same audio a hair
            //  apart — CORRELATED — so equal-GAIN smoothstep (equal-power would swell +3 dB); a large skip
            //  means different audio — equal-POWER sin/cos. Split at 0.5 ms. Length = the old fade-out +
            //  fade-in (≤ 4 ms, ≤ 20 % of a fast beat) so a new beat's transient is no softer than before.
            int xfLen = 0;
            {
                double fadeTarget = (double) boundaryFadeLen;  // ≤ 2 ms baseline cap
                const double beatPeriod = (double) outputsPerLoop;
                if (beatPeriod < sampleRate / 5.0)            // > 5 Hz beat rate
                    fadeTarget = juce::jmin (fadeTarget, beatPeriod * 0.10);
                xfLen = 2 * juce::jmax (1, (int) std::round (juce::jmin (fadeTarget, (double) outputsPerLoop / 4.0)));
            }
            const double correlatedSkip = 0.0005 * sampleRate;

            // One READ HEAD = (anchor, cyclePos, in-grain crossfade latched?). The main head is the one
            // being played; the boundary crossfade runs a COPY of the old one — its main read AND its
            // in-grain crossfade, exactly as it would have continued — under the new beat's fade-in.
            auto readHead = [&] (int anchor, double pos, bool xf, float& oL, float& oR)
            {
                // Never read at/after the write head (belt-and-braces for a starved feed).
                const double pMax = (double) (historyWriteIdx - 3 - anchor);
                const double pc   = pos > pMax ? pMax : pos;
                const int    p0   = (int) std::floor (pc);
                const float  fr   = (float) (pc - (double) p0);
                readHermite (anchor + p0, fr, oL, oR);
                if (xf && pos >= crossfadeBegin)
                {
                    // Start head (reading the grain's [0..crossfadeLen) so the wrap is continuous).
                    const double startCyclePos = pos - crossfadeBegin;
                    const int    sPos0 = (int) startCyclePos;
                    const float  sFrac = (float) (startCyclePos - sPos0);
                    float startL, startR;
                    readHermite (anchor + sPos0, sFrac, startL, startR);
                    // v6 start-head transient softener (1 ms fade-in on the start head only).
                    if (startCyclePos < (double) innerFadeLen)
                    {
                        const float gateGain = std::sin ((float) (startCyclePos / (double) innerFadeLen) * juce::MathConstants<float>::halfPi);
                        startL *= gateGain;
                        startR *= gateGain;
                    }
                    const float t  = juce::jlimit (0.0f, 1.0f, (float) ((pos - crossfadeBegin) / (double) crossfadeLen));
                    const float ga = std::cos (t * juce::MathConstants<float>::halfPi);
                    const float gb = std::sin (t * juce::MathConstants<float>::halfPi);
                    oL = oL * ga + startL * gb;
                    oR = oR * ga + startR * gb;
                }
            };

            for (int i = 0; i < numSamples; i++)
            {
                if (outputsThisLoop >= outputsPerLoop)
                {
                    // Hand the OLD grain to the boundary crossfade, exactly where it would go next.
                    // Contiguous only if it is a plain read landing on the new beat's first sample.
                    const double skip = mainXf ? 1.0e9 : ((double) targetGrainSize - cyclePos);
                    if (xfLen > 1 && bxRemain == 0 && std::abs (skip) > 0.25)
                    {
                        bxAnchor     = loopAnchor;
                        bxPos        = cyclePos;
                        bxXf         = mainXf;
                        bxRemain     = xfLen;
                        bxLen        = xfLen;
                        bxCorrelated = ! mainXf && std::abs (skip) < correlatedSkip;
                    }
                    loopAnchor += targetGrainSize;
                    outputsThisLoop = 0;
                    cyclePos = 0.0;
                    mainXf = false;
                    xfLatched = false;
                    beatCount++;
                }

                // In-grain crossfade gate (v5 — full crossfadeLen tail), now LATCHED once per grain
                // cycle at the moment the read enters the crossfade region. With a steady pitch the
                // prediction is invariant across the region, so this matches v5's per-sample test;
                // with VIBRATO (a per-block pitch step) the per-sample test FLIPPED mid-region — the
                // crossfade switched on/off half-way through: a gain step (the harness measured hard
                // discontinuities on every vibrato'd Beats chop).
                if (! xfLatched && cyclePos >= crossfadeBegin)
                {
                    const double remGrain = (double) targetGrainSize - cyclePos;
                    const int samplesToWrap    = (int) std::ceil (remGrain / pitchRatio);
                    const int samplesToBeatEnd = outputsPerLoop - outputsThisLoop;
                    mainXf    = samplesToWrap < samplesToBeatEnd;
                    xfLatched = true;
                }

                float sL, sR;
                readHead (loopAnchor, cyclePos, mainXf, sL, sR);

                // v12 beat-boundary CROSSFADE (see above): old grain continues, new beat fades in.
                if (bxRemain > 0)
                {
                    float oL, oR;
                    readHead (bxAnchor, bxPos, bxXf, oL, oR);
                    const float  t  = 1.0f - (float) bxRemain / (float) bxLen;     // 0 → 1
                    float gN, gO;
                    if (bxCorrelated) { gN = t * t * (3.0f - 2.0f * t); gO = 1.0f - gN; }   // equal-gain smoothstep
                    else { gN = std::sin (t * juce::MathConstants<float>::halfPi);          // equal-power
                           gO = std::cos (t * juce::MathConstants<float>::halfPi); }
                    sL = sL * gN + oL * gO;
                    sR = sR * gN + oR * gO;
                    bxPos += pitchRatio;
                    if (bxXf && bxPos >= targetGrainSize) { bxPos -= effLoopLen; bxXf = false; }
                    --bxRemain;
                }

                // Activation fade-in (only after silent pending blocks — see above).
                if (startFadePos < startFadeLen)
                {
                    const float g = std::sin ((float) startFadePos / (float) startFadeLen * juce::MathConstants<float>::halfPi);
                    sL *= g; sR *= g; ++startFadePos;
                }

                outL[i] = sL;
                outR[i] = sR;

                cyclePos += pitchRatio;
                if (cyclePos >= targetGrainSize)
                {
                    if (mainXf)
                    {
                        // Crossfaded wrap: the start head has fully taken over — seamless.
                        cyclePos -= effLoopLen;
                        mainXf = false;
                        xfLatched = false;
                    }
                    else if (outputsThisLoop + 1 + xfLen < outputsPerLoop)
                    {
                        // An UNPLANNED wrap mid-beat (vibrato moved the beat end after the gate was
                        // latched): a bare grain restart would jump — crossfade it like a boundary.
                        // Only when the beat has more than one crossfade left to run: closer to the
                        // end the read just carries on past the grain (the history holds the source
                        // continuing) and the boundary's crossfade takes it — two crossfades must
                        // never overlap (the second would have to cut the first).
                        if (xfLen > 1 && bxRemain == 0)
                        {
                            bxAnchor = loopAnchor; bxPos = cyclePos; bxXf = false;
                            bxRemain = xfLen; bxLen = xfLen; bxCorrelated = false;
                        }
                        cyclePos -= effLoopLen;
                        xfLatched = false;
                    }
                    // else: the beat ends on the next sample — the boundary takes it from here.
                }

                outputsThisLoop++;
            }
        }

    private:
        /** CHOP-STRETCH — 4-point cubic Hermite read of the history (SampleEngine's reader, reused).
         *  The 2-point linear read imaged every transient whenever the chop was pitched off its root
         *  (−45 dB HF bursts per hit at −7 st in Source/ChopStretch_test.cpp). frac == 0 (unpitched)
         *  returns the sample itself — bit-identical to the linear read there. */
        void readHermite (int absIdx, float frac, float& l, float& r) const noexcept
        {
            const int i0 = absIdx & historyMask;
            if (frac == 0.0f) { l = historyL[i0]; r = historyR[i0]; return; }
            const int im1 = (absIdx - 1) & historyMask, i1 = (absIdx + 1) & historyMask, i2 = (absIdx + 2) & historyMask;
            l = hermite4 (historyL[im1], historyL[i0], historyL[i1], historyL[i2], frac);
            r = hermite4 (historyR[im1], historyR[i0], historyR[i1], historyR[i2], frac);
        }
        static inline float hermite4 (float xm1, float x0, float x1, float x2, float t) noexcept
        {
            const float c  = (x1 - xm1) * 0.5f;
            const float v  = x0 - x1;
            const float w  = c + v;
            const float a  = w + v + (x2 - x0) * 0.5f;
            const float bn = w + a;
            return ((((a * t) - bn) * t + c) * t + x0);
        }

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
        // v12 boundary crossfade — the old grain's continuing read head
        int    bxAnchor = 0, bxRemain = 0, bxLen = 1;
        double bxPos    = 0.0;
        bool   bxCorrelated = false;
        bool   bxXf         = false;     // the old head's in-grain crossfade state
        bool   mainXf       = false;     // main head: in-grain crossfade latched ON for this cycle
        bool   xfLatched    = false;     // main head: gate decided for this cycle
        // activation fade after silent pending blocks (short slices)
        bool   silentBlocksOut = false;
        int    startFadeLen    = 144;    // ~3 ms @ 48k — set in prepare
        int    startFadePos    = 1 << 30;

        double sampleRate     = 48000.0;
        int    channels       = 2;
        float  stretchRatio   = 1.0f;
        float  pitchSemitones = 0.0f;
        bool   ready          = false;
    };
}
