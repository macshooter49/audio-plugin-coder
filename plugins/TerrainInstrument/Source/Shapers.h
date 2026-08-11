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
                const float driven = x * pre;
                const float q      = (driven + 1.0f) * 0.25f;
                return 4.0f * std::fabs (q - std::round (q)) - 1.0f;
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
                const float driven = x * pre;
                auto linfold = [] (float v) -> float
                {
                    const float q = (v + 1.0f) * 0.25f;
                    return 4.0f * std::fabs (q - std::round (q)) - 1.0f;
                };
                const float s1 = linfold (driven * 1.0f)        * 0.50f;
                const float s2 = linfold (driven * 1.41421356f) * 0.35f;
                const float s3 = linfold (driven * 2.0f)        * 0.15f;
                return s1 + s2 + s3;
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

} // namespace shapers
} // namespace tw
