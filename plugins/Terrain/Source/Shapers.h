#pragma once

#include <cmath>

/*  Shapers.h — shared nonlinear waveshapers (fb313)
    ------------------------------------------------------------------------------------------------
    EXTRACTED VERBATIM from TerrainSynthVoice so that ONE implementation serves both the per-voice
    oscillator fold and the FX-rack Distortion device's FOLD family. SynthVoice keeps thin forwarding
    wrappers, so every existing call site is untouched and the audio output is byte-identical.

    🔑🔑 WHY THIS FILE EXISTS — THE ADAA LOCKSTEP TRAP.
    ADAA is only valid while F really is the antiderivative of f:

        y[n] = ( F(x[n]) - F(x[n-1]) ) / ( x[n] - x[n-1] )

    Before this extraction the fold pre-gain constants were written TWICE — once in applyFold and
    again in foldAntideriv (9.0f at two sites, 5.28318530f at two sites, 5.0f at two sites). Raising a
    fold depth in one place and not the other silently breaks the F-is-the-antiderivative-of-f
    relationship, and the fold then gets LOUDER *and* ALIASES WORSE at the same time — which reads as
    "the deeper range sounds bad, back it off" and causes exactly the timid retreat we are trying to
    stop. There is now ONE table (kFoldPre) and one accessor (foldPre), so a depth change is a single
    edit and the trap is structurally impossible.

    Shape mapping (unchanged): 0 = Linear/Serge · 1 = Sine/Vital · 2 = Triangle/Buchla-259.
    Out-of-range behaviour is preserved EXACTLY as it was: applyFold returns x for an unknown shape,
    while foldAntideriv falls through to the Linear branch. Do not "tidy" that asymmetry — the ADAA
    call path only ever passes 0..2, and matching the old switches keeps this bit-exact.
*/

namespace tw {
namespace shapers {

    // ── THE ONE SOURCE OF TRUTH for fold pre-gain ────────────────────────────
    // Indexed by shape. Raising a fold's depth = change ONE number here and both
    // the shaper and its antiderivative follow automatically.
    //   [0] Linear (Serge)      pre 1..10
    //   [1] Sine   (Vital)      pre 1..2π
    //   [2] Triangle (Buchla)   pre 1..6
    static constexpr float kFoldPre[3] = { 9.0f, 5.28318530f, 5.0f };

    inline float foldPre (int shape, float amount) noexcept
    {
        const int s = (shape >= 0 && shape < 3) ? shape : 0;
        return 1.0f + amount * amount * kFoldPre[s];
    }

    // ══════════════════════════════════════════════════════════════════════════════════════════
    //  WARP-SHAPER PRIMITIVES (fb524)
    //  ------------------------------------------------------------------------------------------
    //  Every function below is PURE, STATELESS and ALLOCATION-FREE. That is not a style choice,
    //  it is the entry ticket: SynthVoice::applyPhaseWarp / applyAmpWarp are `static` precisely so
    //  the wavetable DISPLAY can run the shipped chain instead of a JS copy (the fb458 law,
    //  PluginProcessor.cpp getOscWavetableJson). A warp mode that needs per-voice state cannot be
    //  drawn, so it does not go here — it goes in the "documented gap" list instead.
    //
    //  ⚠️ ADAA IS DELIBERATELY ABSENT FROM THIS SECTION, AND THAT IS SAFE.
    //  The LOCKSTEP TRAP at the top of this file is about applyFold/foldAntideriv, where F really
    //  must be ∫f. Nothing below has an antiderivative partner, so there is no pair to desync.
    //  If anyone ever ADAAs one of these, they must add its F next to it and route it through a
    //  kFoldPre-style single table. Foldback IS the product at the top of these knobs
    //  (the Lifeguard law, clause 2) — do not "fix" the aliasing by backing a ceiling off.
    // ══════════════════════════════════════════════════════════════════════════════════════════

    /** Linear (Serge) fold kernel — the closed-form triangle wave, output bounded ±1.
     *  EXACTLY the expression that was inline in applyFold case 0; factored so the warp
     *  fold modes reuse the shipped math instead of a second copy of it. */
    inline float wsLinFold (float v) noexcept
    {
        const float q = (v + 1.0f) * 0.25f;
        return 4.0f * std::fabs (q - std::round (q)) - 1.0f;
    }

    /** Triangle (Buchla 259) 3-stage cascade — EXACTLY applyFold case 2's inner expression. */
    inline float wsTriFold (float v) noexcept
    {
        return wsLinFold (v)              * 0.50f
             + wsLinFold (v * 1.41421356f) * 0.35f
             + wsLinFold (v * 2.0f)        * 0.15f;
    }

    /** Bounded Padé tanh. |err| < 3.4e-3 vs std::tanh, and it is EXACTLY ±1 at ±3 with a slope
     *  of EXACTLY 0 there, so the clamp is C1 — no corner to alias on. ~5x cheaper than
     *  std::tanh, which matters because this runs per sample, per unison sine, per osc. */
    inline float wsTanh (float u) noexcept
    {
        const float c  = (u < -3.0f) ? -3.0f : (u > 3.0f ? 3.0f : u);
        const float c2 = c * c;
        return c * (27.0f + c2) / (27.0f + 9.0f * c2);
    }

    /** Smooth hard clip, rails at ±1. `w2` is the SQUARE of the corner width, so w2 = 0 is
     *  0.5(|u+1| − |u−1|) = jlimit(−1,1,u) exactly — a razor with no special case. Odd. */
    inline float wsKnee (float u, float w2) noexcept
    {
        const float a = u + 1.0f, b = u - 1.0f;
        return 0.5f * (std::sqrt (a * a + w2) - std::sqrt (b * b + w2));
    }

    /** Cubic soft clip — the triode curve. Odd, 3rd-harmonic-first (no 2nd at all until VAR
     *  adds bias), rails at |u| = 1. This is the answer to the measured Terrain-vs-reference
     *  odd/even defect: the reference's Tube reads +53.9 dB odd/even, our rack Tube reads
     *  −5.5 dB because its bias is baked in. Here the bias is a knob that starts at zero. */
    inline float wsCubic (float u) noexcept
    {
        if (u >=  1.0f) return  1.0f;
        if (u <= -1.0f) return -1.0f;
        return u * (1.5f - 0.5f * u * u);
    }

    /** Algebraic soft clip. Same rails as tanh but a MUCH slower approach to them, so the
     *  harmonic tail is longer and the top of the knob keeps producing new partials where
     *  tanh has already gone to a square. */
    inline float wsAlg (float u) noexcept { return u / (1.0f + std::fabs (u)); }

    /** Transformer core — the softest bounded curve in the set (low order only, no hard corner
     *  anywhere). Measured as the cleanest mode in the whole Terrain distortion set. */
    inline float wsRoot (float u) noexcept { return u / std::sqrt (1.0f + u * u); }

    /** Tape — two stages with different knees: a gentle one that shapes the body and a firm one
     *  that catches the peaks. Bounded ±1, C'(0) = 1.35. */
    inline float wsTape (float u) noexcept
    {
        return 0.75f * wsTanh (0.9f * u) + 0.25f * wsTanh (2.7f * u);
    }

    /** Stomp box — asymmetric BY DESIGN (one polarity clips 2.8 dB earlier and harder). */
    inline float wsStomp (float u) noexcept
    {
        // ⚠️ BOTH HALVES CARRY THE SAME SMALL-SIGNAL SLOPE (0.894427). wsKnee's slope at 0 is
        // 1/sqrt(1+w2), so two different knees are a GAIN STEP at the zero crossing unless they
        // are matched: the unmatched pair measured 7.25 % THD at 15 % of the knob, which breaks
        // the shallow-end-stays-clean clause. The asymmetry now lives where it belongs — in the
        // RAILS (1.000 up / 1.090 down) and in how early each side bends.
        return (u >= 0.0f) ? wsKnee (u, 0.25f)
                           : 1.08980f * wsKnee (u * 1.28205f, 1.44f);
    }

    /** Diode 1 — shunt pair with unequal forward drops: rails at +1.00 / −0.55. */
    inline float wsDiode1 (float u) noexcept
    {
        return (u >= 0.0f) ? wsKnee (u, 0.36f)
                           : 0.55f * wsKnee (u * 1.81818f, 0.36f);
    }

    /** Asym — same rails, two different KNEES (soft up, razor down). The reference's `Asym`
     *  measures +14.9 dB odd/even, i.e. mildly asymmetric; this is aimed at that window. */
    inline float wsAsym (float u) noexcept
    {
        // Slope-matched at 0 (both exactly 1.0 — see wsStomp): the asymmetry is the RAIL
        // (1.887 up / 1.044 down) and the knee, not a gain step. Unmatched measured 12.53 % THD
        // at 15 % of the knob and a 1.81x level jump.
        return (u >= 0.0f) ? 1.88680f * wsKnee (u, 2.56f) : 1.04403f * wsKnee (u, 0.09f);
    }

    /** Class-B gap — the set's only EXPANDER: everything under `dz` is silent, and the gap's
     *  re-entry is the buzz. Note C'(0) = 0, which is why mode 20 rides a dry/wet taper and not
     *  a drive taper (see the WHY TWO TAPERS note on warpShaper). */
    inline float wsGap (float u, float dz) noexcept
    {
        const float a = std::fabs (u) - dz;
        const float v = (a > 0.0f) ? ((u >= 0.0f) ? a : -a) : 0.0f;
        return 1.35f * wsKnee (v, 0.0025f);
    }

    /** Hard wrap — the digital overflow. wrap(v) = v − 2·round(v/2) lands in [−1,1) and is the
     *  EXACT identity for |v| < 1, which is what makes a pre-gain of 1 bit-transparent. */
    inline float wsWrap (float v) noexcept { return v - 2.0f * std::round (v * 0.5f); }

    /** EXPANDER — the one curve here that is not a compressor.
     *  y = u·(1 − q + q·e),  e = u² (VAR 0) morphing to |u| (VAR 1).
     *  The rails are FIXED at ±1 for every q (e = 1 there), and the middle of the range is
     *  pushed DOWN, so the waveform gets spikier instead of squarer — the opposite harmonic
     *  phase relationship to every clipper in this file. Past q = 1 the middle inverts and the
     *  curve grows a second lobe; q = 4 is where |y| first touches 1 at the interior extremum
     *  (derived: |y|max = √((q−1)/3q)·(2/3)(q−1), which is 0.628 at q = 3 and exactly 1.000 at
     *  q = 4), so 4 is the algorithm's own ceiling, not a chosen one.
     *
     *  ⚠️ THE OBVIOUS FORM u(1+k)/(1+k|u|) IS NOT THIS. It looks like an expander and measures
     *  BIT-IDENTICAL to Overdrive once wsDriveForm normalises it (both reduce to
     *  x(1+g)/(1+g|x|)) — caught by the metric harness, which printed two identical rows. */
    inline float wsExpand (float u, float q, float v) noexcept
    {
        const float e = u * u + v * (std::fabs (u) - u * u);
        return u * (1.0f - q + q * e);
    }

    /** y = ( C(g·(x+b)) − C(g·b) ) / ( C(g·(1+b)) − C(g·b) ) — the DRIVE form.
     *
     *  🔑 WHY THE DIVISION IS NOT THE "SELF-NORMALISING CURVE" DistortionEngine.h law 2 forbids.
     *  That law is about a BUS effect, where the input level is unknown: there, dividing by C(g)
     *  makes every drive setting a rescaled copy of one shape and the knob goes dead. Here the
     *  input is a peak-normalised wavetable sample, i.e. a KNOWN reference of ±1, so C(g) is a
     *  CALIBRATION TO THAT REFERENCE, not a self-normalisation: the knee still shrinks relative
     *  to the (fixed) signal as g rises, so the curve genuinely morphs gentle → razor. What the
     *  division buys is that the knob changes TIMBRE and not LOUDNESS, which is the whole point
     *  of a shaper that lives INSIDE an oscillator rather than on a bus.
     *
     *  As g → 0 this tends to x exactly (C(gx)/C(g) → x for any C with C'(0) ≠ 0), which is how
     *  the modes get an exactly transparent floor without a dry/wet blend.
     *
     *  `b` is the VAR bias in the INPUT domain. Subtracting C(g·b) — f(bias), never bias — is
     *  DistortionEngine.h law 3: it makes C_eff(0) = 0 exactly, so there is no note-off click. */
    template <typename Curve>
    inline float wsDriveForm (Curve c, float g, float x, float b) noexcept
    {
        // ⚠️ THE REFERENCE IS THE HALF PEAK-TO-PEAK OVER x in [-1,1], NOT C(g(1+b)) - C(g·b).
        // The difference form looked right and was a live divide-by-almost-zero: once the drive
        // saturates the curve at BOTH x = b and x = 1+b, the two values differ by ~1e-9 and the
        // output explodes (measured 3.4e7 on a +-1 input at VAR = 1). Half-pp is exact for the
        // b = 0 case of every odd curve here — 0.5*(C(g) - (-C(g))) is C(g) bit-for-bit — and it
        // stays finite for every bias and every drive.
        const float dc = (b > 0.0f) ? c (g * b) : 0.0f;   // every curve in this file has C(0) = 0
        const float pk = 0.5f * (c (g * (1.0f + b)) - c (g * (b - 1.0f)));
        return (pk > 1.0e-12f) ? (c (g * (x + b)) - dc) / pk : x;
    }

    // ── The wavefolder. 3 shapes, each output-bounded to ±1. ─────────────────
    // amount in [0,1]; pre-gain is quadratic so the lower half of the knob ramps
    // gently and the upper half drives hard.
    inline float applyFold (float x, int shape, float amount) noexcept
    {
        if (amount <= 1.0e-6f) return x;   // identity fast-path

        switch (shape)
        {
            case 0:
            {
                // Linear (Serge) — closed-form triangle wave fold.
                const float pre    = foldPre (0, amount);
                return wsLinFold (x * pre);      // SAME expression, factored (see wsLinFold)
            }
            case 1:
            {
                // Sine (Vital) — sin(drive·x), bounded ±1.
                const float pre = foldPre (1, amount);
                return std::sin (x * pre);
            }
            case 2:
            {
                // Triangle (Buchla 259) — 3-stage cascade.
                const float pre    = foldPre (2, amount);
                return wsTriFold (x * pre);      // SAME expression, factored (see wsTriFold)
            }
            default: return x;
        }
    }

    // ── Antiderivatives (for ADAA) ───────────────────────────────────────────
    // foldAntideriv(x) = ∫ applyFold dx, so applyFold = d/dx foldAntideriv.
    //   triangle-wave antiderivative: G(q) = 2 r|r| − r,  r = q − round(q)
    //   ∫ linfold(x·a) dx = (4/a)·G((x·a+1)/4)
    inline float foldGtri (float q) noexcept
    {
        const float r = q - std::round (q);
        return 2.0f * r * std::fabs (r) - r;
    }

    inline float foldFlin (float x, float a) noexcept
    {
        return (4.0f / a) * foldGtri ((x * a + 1.0f) * 0.25f);
    }

    inline float foldAntideriv (float x, int shape, float amount) noexcept
    {
        switch (shape)
        {
            case 1: { const float pre = foldPre (1, amount); return -std::cos (pre * x) / pre; }
            case 2: { const float pre = foldPre (2, amount);
                      return 0.5f  * foldFlin (x, pre)
                           + 0.35f * foldFlin (x, pre * 1.41421356f)
                           + 0.15f * foldFlin (x, pre * 2.0f); }
            default:{ const float pre = foldPre (0, amount); return foldFlin (x, pre); }
        }
    }

    struct FoldState
    {
        float x1  = 0.0f;    // previous input sample
        float Fx1 = 0.0f;    // cached antiderivative F(x1) for the (sh1,am1) curve below
        int   sh1 = -1;      // shape Fx1 was computed under (-1 = cache invalid)
        float am1 = -1.0f;   // amount Fx1 was computed under (-1 = cache invalid)
    };

    // 1st-order ADAA: y[n] = (F(x[n]) − F(x[n−1])) / (x[n] − x[n−1]).
    // WITHIN-BLOCK CACHE (CPU): shape/amount are pushed ONCE PER BLOCK (setFold), so across a
    // block F(x[n−1]) this sample is bit-identical to last sample's F(x[n]) — cache it (st.Fx1)
    // and reuse it ONLY while (shape,amount) are unchanged. ANY curve change (a block boundary
    // that moved the knob, or per-block automation) forces a live recompute of F(x1) on the
    // CURRENT curve, so the output is identical to the recompute-both version — this just skips
    // recomputing a value that is provably unchanged. After a note-on reset (x1=0, sh1=-1) the
    // first sample recomputes F(0) live (≠ 0 for any fold). The low-slew and fold-off branches
    // produce no F, so they invalidate the cache (sh1=-1) → the next real sample recomputes.
    // A midpoint-naive fallback handles the low-slew 0/0 case. [ADAA audit vs Waveshaper 75cb6a9]
    //
    // ⚠️ CACHE VALIDITY IS CONDITIONAL. The (sh1,am1) guard is what makes the cache correct, and it
    // only pays off while shape/amount are pushed per BLOCK. If a caller smooths amount PER SAMPLE
    // (as the FX-rack Distortion device will), the guard misses every sample and this costs TWO
    // antiderivative evaluations per sample, not one. That is correct but not free — budget for it.
    inline float applyFoldADAA (float x, int shape, float amount, FoldState& st) noexcept
    {
        float y, Fx = 0.0f;
        bool  haveFx = false;
        if (amount <= 1.0e-6f)
        {
            y = x;                                                   // fold off → identity
        }
        else if (std::fabs (x - st.x1) < 1.0e-5f)
        {
            y = applyFold (0.5f * (x + st.x1), shape, amount);       // low-slew fallback
        }
        else
        {
            Fx = foldAntideriv (x, shape, amount);
            // F(x1): reuse the cache iff it was computed on the SAME curve, else recompute live.
            const float Fx1 = (st.sh1 == shape && st.am1 == amount)
                                ? st.Fx1
                                : foldAntideriv (st.x1, shape, amount);
            y = (Fx - Fx1) / (x - st.x1);
            haveFx = true;
        }
        st.x1 = x;
        if (haveFx) { st.Fx1 = Fx; st.sh1 = shape; st.am1 = amount; }
        else          st.sh1 = -1;                                    // invalidate cache
        return y;
    }


    // ══════════════════════════════════════════════════════════════════════════════════════════
    //  THE WARP SHAPER ROSTER — modes 11..34 of SYN_OSC_x_WARP_MODE / _WARP2_MODE (fb524)
    //  ------------------------------------------------------------------------------------------
    //  WHY WARP AND NOT THE FX RACK, AND WHAT IT COSTS. Warp runs INSIDE the unison loop, so a
    //  shaper here reshapes each of up to 16 unison sines INDEPENDENTLY and then sums. That is the
    //  same physics as the measured 62.7-83.6 dB chord-IMD gap against the reference: distorting a
    //  SUM makes its components intermodulate, distorting each component separately does not.
    //  It is also 10-40x more expensive than a post-sum shaper, which is why applyAmpWarp's FIRST
    //  line is `if (mode < 9) return s;` — one compare, and every patch that selects no shaper
    //  pays exactly that and nothing else.
    //
    //  🔑 WHY TWO TAPERS, AND WHICH MODE GETS WHICH.
    //   * DRIVE taper (wsDriveForm): for the SATURATING curves. y = C(g·x)/C(g) with g = gMax·a³.
    //     Transparent at a = 0 by the limit, level-held at every setting, and the character
    //     genuinely morphs because the knee shrinks against a signal of fixed amplitude. A dry/wet
    //     blend on a saturator sounds like a parallel mix, which is not what "drive" means.
    //   * DRY/WET taper (y = s + (wet − s)·a): for the DISCONTINUOUS and EXPANDING curves —
    //     the class-B gap, the comparator, the rectifiers, the quantiser, the Chebyshev
    //     injectors, the sine/triangle folders. These have C'(0) = 0 or C(0) ≠ 0, so a drive
    //     taper cannot reach them transparently: at low drive they would GATE or OFFSET the
    //     signal instead of leaving it alone, and the shallow end must stay clean (Lifeguard
    //     clause 3). Mode 9 (Rectify) already ships this way; these follow its convention.
    //   * PRE-GAIN taper (no normaliser at all): for the three folders whose kernel is the EXACT
    //     identity below its first fold — Linear Fold, Overflow, Bias Fold. pre = 1 is
    //     bit-transparent by construction, so nothing else is needed.
    //
    //  🔑 VAR IS A REAL SECOND DIMENSION, PER FAMILY (SYN_OSC_x_WVAR / _W2VAR, default 0):
    //      11-21, 34   BIAS / asymmetry   (0 = symmetric; the even harmonics are opt-in)
    //      22          pedestal height
    //      23, 26, 31  fold offset (bias, DC-subtracted)
    //      24, 25      extra fold depth   (x1 .. x3)
    //      27, 28      pre-gain           (the mode-9 Rectify convention)
    //      29          quantiser ladder offset
    //      30, 32      which harmonic dominates
    //      33          ripple order k = 2..12 (integer, so the ±1 rails are ripple NODES)
    //
    //  🚨 fb470 — THE DEFAULT BELOW IS `return s`, AND THAT IS A TRAP FOR THE NEXT PERSON.
    //  Modes 35-47 are the RESERVED tail of the choice param and are SUPPOSED to be inert. A new
    //  LIVE mode added without a `case` compiles clean, shows its name in the menu, draws in the
    //  waterfall and makes no sound. Every new mode needs a cert row that a mutation to its case
    //  label makes FAIL, and an entry in warpAmpNeedsDc (SynthVoice.h) if it is not odd-symmetric.
    // ══════════════════════════════════════════════════════════════════════════════════════════
    inline float warpShaper (int mode, float amount, float s, float var) noexcept
    {
        // EXACT transparency at zero, and the 0/0 guard for every /C(g) below in one line.
        // 1e-6 is applyFold's own convention; WARP_AMOUNT's step is 1e-3, so the dead travel is
        // 0.1 % of one step.
        if (amount <= 1.0e-6f) return s;

        const float a = amount;
        const float t = a * a * a;                                    // the drive taper
        const float v = (var < 0.0f) ? 0.0f : (var > 1.0f ? 1.0f : var);
        const float b = 0.6f * v;                                     // VAR-as-bias (input domain)

        switch (mode)
        {
            // ── SATURATION (drive taper, VAR = bias) ─────────────────────────────────────────
            case 11: return wsDriveForm ([] (float u) { return wsCubic (u); },          32.0f * t, s, b);   // Tube
            case 12: return wsDriveForm ([] (float u) { return wsTanh  (u); },          48.0f * t, s, b);   // Soft Sat.
            case 13: return wsDriveForm ([] (float u) { return wsKnee (u, 0.81f); },    48.0f * t, s, b);   // Soft Clip
            case 14: return wsDriveForm ([] (float u) { return wsKnee (u, 0.0004f); },  40.0f * t, s, b);   // Hard Clip
            case 15: return wsDriveForm ([] (float u) { return wsAlg   (u); },          96.0f * t, s, b);   // Overdrive
            case 16: return wsDriveForm ([] (float u) { return wsTape  (u); },          44.0f * t, s, b);   // Tape Sat.
            case 17: return wsDriveForm ([] (float u) { return wsStomp (u); },          56.0f * t, s, b);   // Stomp Box
            case 18: return wsDriveForm ([] (float u) { return wsRoot  (u); },          64.0f * t, s, b);   // Transformer
            case 19: return wsDriveForm ([] (float u) { return wsDiode1(u); },          48.0f * t, s, b);   // Diode 1
            case 21: return wsDriveForm ([] (float u) { return wsAsym  (u); },          56.0f * t, s, b);   // Asym

            case 20:   // Diode 2 — class-B gap. EXPANDER: C'(0) = 0, so dry/wet, not drive.
            {
                // The GAP opens with the taper too. A fixed 0.35 gap ate 35 % of the waveform at
                // the very bottom of the knob (2.95 % THD at a = 0.15) — an expander whose floor
                // is not clean. dz = 0.03..0.35 keeps the class-B character and gives it a floor.
                const float g  = 1.0f + 63.0f * t;
                const float dz = 0.03f + 0.32f * t;
                const float dc = wsGap (g * b, dz);
                const float n  = 0.5f * (wsGap (g * (1.0f + b), dz) - wsGap (g * (b - 1.0f), dz));
                const float y  = (n > 1.0e-6f) ? (wsGap (g * (s + b), dz) - dc) / n : 0.0f;
                return s + (y - s) * a;
            }

            case 22:   // Zero-Square — comparator + scaled clean path. VAR = pedestal.
            {
                // ⚠️ THE 2.5e-4 GATE IS A STABILITY REQUIREMENT, NOT TASTE (the same one
                // DistortionEngine's ZeroSquare carries): with a pedestal near 1 and no gate,
                // ANY nonzero input — a denormal, a released voice's tail — becomes full scale.
                const float g    = 1.0f + 7.0f * t;
                const float p    = (0.60f + 0.40f * v) * t;   // VAR 1 at full travel = p 1.0 = a PURE comparator
                const float u    = g * s;
                const float ped  = (std::fabs (u) > 2.5e-4f) ? ((u > 0.0f) ? p : -p) : 0.0f;
                const float core = (u < -1.0f) ? -1.0f : (u > 1.0f ? 1.0f : u);
                return s + ((ped + (1.0f - p) * core) - s) * a;
            }

            // ── FOLD (pre-gain taper — the kernel is the exact identity below its first fold) ──
            case 23:   // Linear Fold (Serge), VAR = fold offset
            {
                const float pre = 1.0f + 63.0f * t;
                const float o   = 0.5f * v;
                return wsLinFold ((s + o) * pre) - wsLinFold (o * pre);
            }
            case 26:   // Overflow — a FOLD that morphs into a hard digital WRAP. VAR = offset.
            {
                // ⚠️ A BARE WRAP HAS NO CLEAN FLOOR AND MEASURING IT PROVED IT: at a = 0.15 the
                // pre-gain is only 1.10, but a wrap turns that 10 % overshoot into a FULL-SCALE
                // jump — 2331 % THD and a centroid 7.7x the carrier's, at 15 % of the knob. The
                // fix is not a smaller ceiling (that is playing safe), it is a MORPH: the fold
                // kernel is continuous and identical to the wrap below the first fold, so
                // crossfading fold -> wrap by the same taper gives a clean shallow end AND an
                // unchanged, fully brutal top.
                const float pre = 1.0f + 31.0f * t;
                auto f = [t] (float u) { return wsLinFold (u) + t * (wsWrap (u) - wsLinFold (u)); };
                const float o = 0.5f * v;
                return f ((s + o) * pre) - f (o * pre);
            }
            case 31:   // Bias Fold — positive half FOLDS, negative half CLIPS. VAR = offset.
            {
                const float pre = 1.0f + 47.0f * t;
                const float o   = 0.5f * v;
                auto f = [] (float u) { return (u >= 0.0f) ? wsLinFold (u) : wsKnee (u, 0.0004f); };
                return f ((s + o) * pre) - f (o * pre);
            }

            // ── FOLD (dry/wet — these two are not the identity at pre = 1) ───────────────────
            case 24:   // Sine Fold — up to 6pi of fold at VAR 0, 18pi at VAR 1
            {
                const float pre = (1.0f + 74.40f * t) * (1.0f + 2.0f * v);
                return s + (std::sin (pre * s) - s) * a;
            }
            case 25:   // West Coast — the Buchla 259 3-stage cascade
            {
                const float pre = (1.0f + 39.0f * t) * (1.0f + 2.0f * v);
                // 1/1.295 is the small-signal slope; x1.15 on top pulls the cascade's PEAK back
                // up (three staggered folds partially cancel — the unscaled wet measured 0.617
                // peak at full travel). A dry/wet mode may scale its wet freely: transparency at
                // a = 0 is guaranteed by the blend, not by the wet's gain.
                return s + (wsTriFold (pre * s) * 0.888031f - s) * a;
            }

            // ── RECTIFY / OCTAVE (dry/wet, VAR = pre-gain — the mode-9 convention) ───────────
            case 27:   // Half Rect — the fundamental survives at -6 dB under the octave
            {
                const float sd = s * (1.0f + 3.0f * v);
                const float r  = ((sd > 0.0f) ? sd : 0.0f) * 2.0f - 1.0f;
                return s * (1.0f - a) + r * a;
            }
            case 28:   // Doubler — a squarer: ONE harmonic, a pure octave, no fundamental left.
            {          // VAR morphs squarer -> bridge rectifier (two different octave textures).
                // ⚠️ VAR IS NOT A PRE-GAIN HERE, unlike modes 9 and 27. A squarer squares its
                // pre-gain too: 1+3v put the output at 31x full scale (+30 dB) at VAR 1. The
                // morph keeps both endpoints bounded to +-1 and is the more useful axis anyway.
                const float sq = 2.0f * s * s - 1.0f;
                const float br = std::fabs (s) * 2.0f - 1.0f;
                const float r  = sq + (br - sq) * v;
                return s * (1.0f - a) + r * a;
            }

            // ── DIGITAL / HARMONIC (dry/wet) ─────────────────────────────────────────────────
            case 29:   // Bitcrush — 64 levels down to 2. VAR = ladder offset.
            {
                // Cubic in `a`, not in `t`: with the t taper the ladder was still 56 levels at
                // half travel (0.35 % THD — a dead first half). This lands 5.2 bits at a = 0.15,
                // 3.3 bits at a = 0.5 and exactly 2 levels at a = 1, and it cannot go below 2.
                const float ia = (a < 1.0f) ? (1.0f - a) : 0.0f;
                const float L  = 2.0f + 62.0f * ia * ia * ia;
                const float o = 0.5f * v;
                return s + (((std::floor (s * L - o + 0.5f) + o) / L) - s) * a;
            }
            case 30:   // Harmonics — a Chebyshev injector. VAR sweeps 2nd -> 3rd -> 4th.
            {
                const float x  = (s < -1.0f) ? -1.0f : (s > 1.0f ? 1.0f : s);
                const float x2 = x * x;
                const float T2 = 2.0f * x2 - 1.0f;
                const float T3 = x * (4.0f * x2 - 3.0f);
                const float T4 = 8.0f * x2 * x2 - 8.0f * x2 + 1.0f;
                const float p  = 2.0f * v;
                const float w2 = 1.0f - ((p < 1.0f) ? p : 1.0f);
                const float w3 = 1.0f - std::fabs (p - 1.0f);
                const float w4 = (p > 1.0f) ? (p - 1.0f) : 0.0f;
                return s + ((w2 * T2 + w3 * T3 + w4 * T4) - s) * a;
            }
            case 32:   // Cheby Odd — the odd-only ladder. VAR sweeps 3rd -> 5th -> 7th.
            {
                const float x  = (s < -1.0f) ? -1.0f : (s > 1.0f ? 1.0f : s);
                const float x2 = x * x;
                const float T3 = x * (4.0f * x2 - 3.0f);
                const float T5 = x * (16.0f * x2 * x2 - 20.0f * x2 + 5.0f);
                const float T7 = x * (x2 * (x2 * (64.0f * x2 - 112.0f) + 56.0f) - 7.0f);
                const float p  = 2.0f * v;
                const float w3 = 1.0f - ((p < 1.0f) ? p : 1.0f);
                const float w5 = 1.0f - std::fabs (p - 1.0f);
                const float w7 = (p > 1.0f) ? (p - 1.0f) : 0.0f;
                return s + ((w3 * T3 + w5 * T5 + w7 * T7) - s) * a;
            }

            // ── THE TWO NOBODY ELSE HAS ─────────────────────────────────────────────────────
            case 33:   // Ripple — a sinusoidal ripple ON the transfer curve. k is an INTEGER, so
            {          // sin(k*pi*s) is ZERO at s = +-1: the rails never move, the middle does.
                const int   k = 2 + (int) std::floor (10.0f * v + 0.5f);
                const float d = 1.6f * t;   // > 1/(k*pi) makes the curve NON-monotone = foldback
                return s + d * std::sin ((float) k * 3.14159265358979f * s);
            }
            case 34:   // Exciter — the only EXPANDER here. VAR morphs the expansion law u² -> |u|.
            {
                return wsExpand (s, 4.0f * t, v);
            }

            default: return s;   // 🚨 reserved tail 35-47 — see the fb470 note above
        }
    }

} // namespace shapers
} // namespace tw
