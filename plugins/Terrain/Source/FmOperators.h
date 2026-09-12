#pragma once
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  FmOperators.h — fb587. THE FM OPERATOR STAGE, IN ONE PLACE, SO THE PICTURE CAN SHOW IT.
// ══════════════════════════════════════════════════════════════════════════════════════════════
//
//  Max: "with FM, I want to make sure that the waveform visual and the waterfall visual actually
//  move with FM. Whatever the ratio does, whatever FM does, I need to actually see it move the
//  table... what does it look like when the FM scorches up?"
//
//  It did not move, and the reason was structural: getOscWavetableJson is ENGINE-UNAWARE. It draws
//  table → warp → fold → feedback and nothing else, so on FM it was faithfully drawing the carrier
//  and none of the modulation sitting on top of it.
//
//  🚨 AND THE DISPLAY MAY NOT KEEP ITS OWN COPY OF THE MATH. That rule is written into
//     PluginProcessor.cpp itself — "A JS reimplementation of twenty-odd warp modes is exactly the
//     second-copy trap this codebase has paid for before" — which is why applyPhaseWarp and
//     applyAmpWarp are `static` and why the drawing calls the very functions the voice calls.
//     The FM operator stage had no such function: it lived inline in the render loop, FOUR TIMES,
//     once per oscillator. So it moves here, and now the voice and the picture read one source.
//
//  WHAT IS AND IS NOT IN HERE. This is the operator stage ONLY — the two sine modulators, their
//  algorithm, feedback, STORM, SCORCH and QUAKE, ending at the carrier's read phase. Everything
//  downstream (warp 1, warp 2, the table read, the window, the amp warps) already had shared
//  static functions and is untouched.
//
//  ⚠️ THE SUMMATION ORDER IS LOAD-BEARING. The voice built the carrier phase as
//         (((uPhase + qSub) + blendOff) + d1*m1) ...
//     so `run` takes the carrier phase and the blend offset AS ARGUMENTS and assembles them in that
//     exact order. Returning a "phase offset" for the caller to add would have re-associated the
//     sum and changed the rounding — which is not a null refactor, and Tests/fm_ops_cert.cpp
//     compares against a verbatim transcription of the original to prove it bit-for-bit.
//
//  No JUCE here on purpose (juce::jlimit is transcribed literally below) so the cert can run the
//  operator stage offline, with no plugin and no audio device.
// ══════════════════════════════════════════════════════════════════════════════════════════════

#include <cmath>

namespace tw
{
    struct FmOps
    {
        /** Everything the operator stage remembers between samples, per unison voice. */
        struct State
        {
            double m1Phase    = 0.0;   // uModPhase*_  — modulator 1's accumulator
            double m2Phase    = 0.0;   // uMod2Phase*_ — modulator 2's accumulator
            double quakePhase = 0.0;   // fmQuakePhase*_
            float  fbMem      = 0.0f;  // fmFb*_       — M1's averaged self-feedback memory
            float  prevM1     = 0.0f;  // fmPrevM1*_   — STORM's one-sample cross memory
        };

        /** Everything the stage reads that is constant across a sample. Every one of these is
            already block-conditioned and de-zippered by the caller (the fm*Now_ glide). */
        struct Params
        {
            int    alg            = 0;      // 0 STACK (M2→M1→carrier) · 1 SPLIT (both→carrier) · 2 RING
            float  d1             = 0.0f;   // index 1, SCORCH's index push already folded in
            float  d2             = 0.0f;   // index 2, likewise
            float  fbk            = 0.0f;   // M1 self-feedback depth
            float  storm12        = 0.0f;   // M1 → M2 cross-couple
            float  storm21        = 0.0f;   // M2 → M1 cross-couple
            float  scorchPre      = 1.0f;   // in-loop shaper input gain (1 = off)
            float  scorchBias     = 0.0f;
            float  scorchTanhBias = 0.0f;
            float  scorchMakeup   = 1.0f;
            float  quakeIdx       = 0.0f;   // subharmonic operator depth
            float  quakeFry       = 0.0f;
            float  quakeSubRatio  = 0.5f;
            double ratio1         = 1.0;    // fmR1Eff_ — turns per sample multiplier for M1
            double ratio2         = 2.0;    // fmR2Eff_
            double rustTps        = 0.0;    // RUST — absolute-Hz detune of M1, in turns/sample
            float  ringDepth      = 0.0f;   // fmD1Sm_ — RING's dry→wet, which is NOT the glided d1
        };

        struct Out
        {
            double carrierPhase = 0.0;   // wrapped, ready for warp 1
            float  m1           = 0.0f;
            float  ringGain     = 1.0f;  // multiply the carrier's OUTPUT by this (1.0 unless RING)
        };

        /** Fast odd-symmetric tanh (rational 135135… approx) — SCORCH's in-loop waveshaper; no
            libm call in the hot FM path. Input clamped to ±5. Transcribed from
            SynthVoice::fmFastTanh, juce::jlimit expanded to its literal definition
            (`v < lo ? lo : (hi < v ? hi : v)`) so this header needs no JUCE. */
        static inline float fastTanh (float x) noexcept
        {
            x = (x < -5.0f) ? -5.0f : ((5.0f < x) ? 5.0f : x);
            const float x2 = x * x;
            const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
            const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
            return a / b;
        }

        /** One sample of the operator stage. Mutates `st` (feedback memory, STORM memory and the
            QUAKE phase all advance here, exactly where they did in the render loop). The two
            MODULATOR phases do not — they advance at the END of the sample, in `advance`, because
            that is where the voice advanced them. */
        static inline Out run (State& st, const Params& p, double inc,
                               double carrierPhase, double blendOff) noexcept
        {
            const double pi2 = 6.2831853071795865;

            // fb636 — M2 reaches the sound only as d2·m2 (into M1, or into the carrier on SPLIT) and storm21·m2.
            //  With both exactly 0 (6 of Max's 7 FM slots) its double sine was computed for nobody; the terms
            //  it fed become +0·m2 = ±0 added to a phase that is never -0 (phases live in [+0,1)), i.e. the
            //  same value. m2Phase still advances in `advance`, untouched.
            float m2 = 0.0f;
            if (p.d2 != 0.0f || p.storm21 != 0.0f)
            {
                m2 = static_cast<float> (std::sin (pi2 * (st.m2Phase
                                         + static_cast<double> (p.storm12 * st.prevM1))));
                // SCORCH — asymmetric drive on M2 (adds harmonics → richer sidebands)
                if (p.scorchPre > 1.0f)
                    m2 = (fastTanh (p.scorchPre * m2 + p.scorchBias) - p.scorchTanhBias) * p.scorchMakeup;
            }

            double m1Arg = st.m1Phase + static_cast<double> (p.fbk * st.fbMem)
                                      + static_cast<double> (p.storm21 * m2);
            if (p.alg != 1) m1Arg += static_cast<double> (p.d2 * m2);       // STACK + RING: M2 → M1
            float m1 = static_cast<float> (std::sin (pi2 * m1Arg));
            // SCORCH — same drive on M1 (the operator that hits the carrier)
            if (p.scorchPre > 1.0f)
                m1 = (fastTanh (p.scorchPre * m1 + p.scorchBias) - p.scorchTanhBias) * p.scorchMakeup;
            st.fbMem  = 0.5f * (st.fbMem + m1);
            st.prevM1 = m1;

            // QUAKE — phase-locked subharmonic operator folded into the carrier phase
            double qSub = 0.0;
            if (p.quakeIdx > 1.0e-5f)
            {
                st.quakePhase += inc * static_cast<double> (p.quakeSubRatio);
                st.quakePhase -= std::floor (st.quakePhase);
                float sub = static_cast<float> (std::sin (pi2 * st.quakePhase));
                if (p.quakeFry > 0.0f) sub += p.quakeFry * (sub - sub * sub * sub * (1.0f / 6.0f));
                qSub = static_cast<double> (p.quakeIdx * sub);
            }

            Out o;
            double cPh = carrierPhase + qSub + blendOff;        // ⚠️ this order is the original's
            if (p.alg != 2) cPh += static_cast<double> (p.d1 * m1);
            if (p.alg == 1) cPh += static_cast<double> (p.d2 * m2);
            cPh -= std::floor (cPh);

            o.carrierPhase = cPh;
            o.m1           = m1;
            o.ringGain     = (p.alg == 2) ? ((1.0f - p.ringDepth) + p.ringDepth * m1) : 1.0f;
            return o;
        }

        /** The two modulator accumulators, advanced at the end of the sample. */
        static inline void advance (State& st, const Params& p, double inc) noexcept
        {
            st.m1Phase += inc * p.ratio1 + p.rustTps;
            st.m1Phase -= std::floor (st.m1Phase);
            st.m2Phase += inc * p.ratio2;
            st.m2Phase -= std::floor (st.m2Phase);
        }
    };
}
