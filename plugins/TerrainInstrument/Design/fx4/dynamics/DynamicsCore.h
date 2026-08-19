#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DynamicsCore.h — fb420+. The SHARED math behind BOTH fx4 dynamics devices:
//     TerrainCompressFx.h  (chain kind 11, SYN_CMP_*)   — single band
//     TerrainOttFx.h       (chain kind 12, SYN_OTT_*)   — three bands, up + down
//
// Contract: Design/fx4/CONTRACT.md §1 ("dynamics/ owns two engines over one shared core —
// the envelope follower, both gain computers, the −26 dBFS threshold calibration and the
// ballistics are the same math. Two agents building that separately is how the two halves
// quietly diverge.")
//
// ── WHAT LIVES HERE, AND WHY IT IS ONE FILE ─────────────────────────────────
//   1. The dBp system.  Every threshold in every compressor manual on earth is stated against
//      a 0 dBFS program. Terrain's FX bus is not that bus: a single note arrives at ≈ −26 dBFS
//      (PluginProcessor.cpp:46 measures −20 dBFS at the master, and kVoiceToFxPad = 0.5f at
//      :6300 pads the send 6 dB below it). Copy an LA-2A's −20 dB threshold and the program
//      NEVER CROSSES IT: the device ships dead. Both devices therefore work in **dBp**, where
//      0 dBp = −26.02 dBFS = single-note nominal, and the reference CHORD sits at ≈ +6 dBp.
//      One constant, one place, both devices.
//   2. log2/exp2 polynomials. `powf` per sample per band is the difference between 0.15 % of a
//      core and 2 %. Accuracy is GATED in the harness against libm, not asserted.
//   3. The branching follower — peak and mean-square, one struct, both devices.
//   4. Both gain computers — downward (Giannoulis soft knee, signed slope so OverEasy's
//      "Infinity+" negative zone is the SAME formula) and upward (+ the floor gate that makes
//      upward compression die with the note instead of resurrecting the noise floor forever).
//   5. LR4Split / AP2 — Linkwitz–Riley 4th order built on the SHIPPED Simper TPT SVF
//      coefficient math (TerrainFilters.h:346-358). Not a new filter: the same six lines.
//      LP4 + HP4 = AP2 exactly, which is what makes the OTT Mix law provable rather than hoped.
//
// ── ALLOCATION LAW (fb415) ──────────────────────────────────────────────────
// Nothing in this file allocates. Ever. Every struct is fixed-size and POD-ish; `prepare` on
// the owning engines only writes coefficients. fb415 caught a malloc on the audio thread from
// copying the Filter's prepare-in-processBlock shape — safe there (it allocates nothing), fatal
// for anything with a crossover.
//
// No allocation · no locks · no std::function · no virtual · denormal-flushed everywhere.
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace tw {
namespace dyn {

// ═════ 0. bus law + tiny helpers ═════════════════════════════════════════════

/** −26.02 dBFS, linear. A single note's nominal level on the Terrain FX bus. */
constexpr float kBusNomLin = 0.05f;
/** ... the same thing in dBFS. `dBFS = dBp + kBusNomDb`. */
constexpr float kBusNomDb  = -26.0206f;
/** Detector lift: multiply the detector input by this and dB(x) reads directly in dBp. */
constexpr float kDetLift   = 20.0f;
/** The reference CHORD, in dBp. fb249's law: calibrate makeup to the CHORD, never a sine —
 *  a summed chord sits ~6 dB over a single note and over-lifting a chord is broadband IMD. */
constexpr float kChordDbp  = 6.0f;

inline float flushd (float x) noexcept { return (std::fabs (x) < 1.0e-20f) ? 0.0f : x; }
inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline int   clampi (int   v, int   lo, int   hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf  (float a, float b, float t) noexcept { return a + (b - a) * t; }

/** Exponential map, knob 0..1 → [lo, hi]. Every time window in both devices uses this. */
inline float expMap (float t, float lo, float hi) noexcept
{
    return lo * std::pow (hi / lo, clampf (t, 0.0f, 1.0f));
}

// ═════ 1. fast log2 / exp2 ═══════════════════════════════════════════════════
// Both devices live or die on log-domain gain math: 6 pow() calls per sample per band is not
// affordable. Accuracy of BOTH is gated in the harness against std::log10 / std::pow over the
// whole working range; the gate fails if either drifts past 0.01 dB.

/** log2 via exponent extraction + the atanh series on the mantissa.
 *  t = (m−1)/(m+1) ∈ [0, 1/3]; ln m = 2(t + t³/3 + t⁵/5 + …). Truncating after t⁵ leaves
 *  |err| ≲ 1.4e-4 in ln ⇒ < 0.002 dB. */
inline float fastLog2 (float x) noexcept
{
    if (!(x > 0.0f)) return -100.0f;                    // also catches NaN
    union { float f; uint32_t i; } v; v.f = x;
    int   e = (int) ((v.i >> 23) & 0xFFu) - 127;
    v.i = (v.i & 0x007FFFFFu) | 0x3F800000u;            // mantissa → [1,2)
    float m = v.f;
    if (m > 1.4142136f) { m *= 0.5f; ++e; }             // → [0.707, 1.414): halves |t|
    const float t  = (m - 1.0f) / (m + 1.0f);
    const float t2 = t * t;
    const float ln = 2.0f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * 0.14285714f)));
    return (float) e + ln * 1.44269504f;                // 1/ln2
}

/** exp2 via integer split + degree-5 Taylor on the fraction. |rel err| < 1.5e-5. */
inline float fastExp2 (float x) noexcept
{
    x = clampf (x, -126.0f, 126.0f);
    const float xi = std::floor (x);
    const float f  = x - xi;
    const float p  = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f
                            + f * (0.00961813f + f * 0.00133336f))));
    union { float f; uint32_t i; } v;
    v.i = (uint32_t) (((int) xi + 127) << 23);
    return p * v.f;
}

inline float lin2db (float x) noexcept { return 6.0205999f  * fastLog2 (x < 1.0e-20f ? 1.0e-20f : x); }
inline float db2lin (float d) noexcept { return fastExp2 (d * 0.16609640f); }
/** mean-square → dB. 10·log10(e) = 3.0103·log2(e). */
inline float ms2db  (float e) noexcept { return 3.01029996f * fastLog2 (e < 1.0e-20f ? 1.0e-20f : e); }

/** DelayEngine.h:315-321, verbatim behaviour — the NaN / overshoot net. */
inline float softClip (float x) noexcept
{
    if (x >  1.4f) return  std::tanh (x);
    if (x < -1.4f) return  std::tanh (x);
    return x;
}

/** TerrainFilters.h:42 fastTanh, Padé form — reused for every saturating stage here. */
inline float fastTanh (float x) noexcept
{
    if (x >  5.0f) return  1.0f;
    if (x < -5.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ═════ 2. coefficients + smoothers ═══════════════════════════════════════════

/** One-pole coefficient for a time-to-63 % of `tauSec`. DelayEngine.h:324 idiom. */
inline float coefTau (float tauSec, float fs) noexcept
{
    if (tauSec <= 1.0e-7f) return 1.0f;
    return clampf (1.0f - std::exp (-1.0f / (fs * tauSec)), 0.0f, 1.0f);
}
inline float coefHz (float hz, float fs) noexcept
{
    if (hz <= 0.0f)          return 0.0f;
    if (hz >= fs * 0.49f)    return 1.0f;
    return 1.0f - std::exp (-6.2831853f * hz / fs);
}

/** Plain glide. Every continuous param in both devices rides one of these (law 4: no clicks). */
struct Glide
{
    float z = 0.0f, a = 1.0f;
    void  setTau (float tauSec, float fs) noexcept { a = coefTau (tauSec, fs); }
    void  snap  (float v) noexcept { z = v; }
    inline float proc (float target) noexcept { z += (target - z) * a; return (z = flushd (z)); }
    inline float value() const noexcept { return z; }
};

/** The branching one-pole every follower in both devices is built from. */
struct Branch
{
    float z = 0.0f;
    void reset (float v = 0.0f) noexcept { z = v; }
    inline float proc (float x, float aA, float aR) noexcept
    {
        z += (x - z) * (x > z ? aA : aR);
        return (z = flushd (z));
    }
};

/** Mean-square running average (Vital's `(x² + env·N)/(N+1)` written as a one-pole — identical
 *  algebra, one fewer divide). Used for every RMS-class detector in both devices. */
struct MeanSquare
{
    float z = 0.0f;
    void reset() noexcept { z = 0.0f; }
    inline float proc (float x2, float a) noexcept { z += (x2 - z) * a; return (z = flushd (z)); }
};

/** One-pole highpass, detector side only (Compress "Hear Cut"; OTT's low-band mono sum). */
struct HP1
{
    float z = 0.0f;
    void reset() noexcept { z = 0.0f; }
    inline float proc (float x, float a) noexcept { z += (x - z) * a; z = flushd (z); return x - z; }
};

/** TerrainFilters.h:69 DCBlocker, verbatim. Engaged only when an asymmetric stage is active. */
struct DCBlock
{
    float xz = 0.0f, yz = 0.0f;
    void reset() noexcept { xz = yz = 0.0f; }
    inline float proc (float x) noexcept
    { const float y = x - xz + 0.995f * yz; xz = x; yz = flushd (y); return yz; }
};

// ═════ 3. the two gain computers ═════════════════════════════════════════════

/** DOWNWARD, Giannoulis/Massberg/Reiss (JAES 60(6) 2012) eq. 4, in SLOPE form.
 *
 *  `s` is the GR-per-dB-over slope, s = 1 − 1/R:
 *      s = 0    →   1:1        (nothing)
 *      s = 0.5  →   2:1
 *      s = 0.9  →  10:1
 *      s = 1.0  →   ∞:1        (brick wall — output is flat above T)
 *      s = 2.0  →  −1:1        (dbx "Infinity+": output FALLS 1 dB per input dB)
 *  Writing the computer in s instead of R is what lets the SAME function carry OverEasy's
 *  negative zone with no branch and no second formula — the region is continuous through ∞.
 *
 *  Returns gain reduction in dB (≥ 0 for s > 0). W = knee width in dB (0 = hard corner). */
inline float grDown (float xdb, float T, float s, float W) noexcept
{
    const float d = xdb - T;
    if (W > 1.0e-4f)
    {
        const float w2 = 2.0f * d;
        if (w2 < -W) return 0.0f;
        if (w2 <=  W) { const float u = d + 0.5f * W; return s * (u * u) / (2.0f * W); }
    }
    else if (d <= 0.0f) return 0.0f;
    return s * d;
}

/** UPWARD. Below `Tup`, lift toward it with slope `sUp` (sUp = 1 − 1/R_up; 0.8 ⇒ 5:1 upward),
 *  hard-capped at `capDb`. Returns a POSITIVE dB boost.
 *
 *  ⚠️ This is the computer that threatens the whole device: with no input, xdb → −∞ and it pins
 *  at the cap, amplifying the noise floor forever. It is ONLY safe in combination with
 *  `floorGate` below, which is a stability requirement of the design, not polish. */
inline float liftUp (float xdb, float Tup, float sUp, float capDb) noexcept
{
    const float under = Tup - xdb;
    if (under <= 0.0f) return 0.0f;
    const float g = sUp * under;
    return g < capDb ? g : capDb;
}

/** THE FLOOR GATE (house law 6 — nothing free-runs, sound dies with the note).
 *  1 above F + ramp, 0 at or below F, smoothstep between. NOT a comparator: a decaying tail
 *  crossing a hard threshold gate-flutters. The clamp matters — smoothstep is undefined
 *  outside [0,1] and the polynomial turns around. */
inline float floorGate (float xdb, float F, float ramp = 12.0f) noexcept
{
    float t = (xdb - F) / ramp;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// ═════ 4. crossovers — LR4 on the shipped Simper TPT SVF ═════════════════════
//
// Coefficient math is TerrainFilters.h:346-358 verbatim (g = tan(π fc/fs), k = 1/Q,
// a1 = 1/(1+g(g+k)), a2 = g·a1, a3 = g·a2). The only thing this adds is returning lp, hp AND
// the raw bp tap from ONE tick, because LR4 needs both halves and the alignment allpass needs
// the bp node. SvfMultimode returns one tap per call, so calling it twice would run the state
// twice — hence a local struct with the same six lines rather than a wrapper.
//
// LR4 = two cascaded Butterworth sections (Q = 1/√2, k = √2). The property that makes the OTT
// Mix law PROVABLE rather than hoped:
//     LP4 + HP4  =  (s⁴+1)/(s²+√2s+1)²  =  (s²−√2s+1)/(s²+√2s+1)  =  AP2(fc)   exactly.
// So a 3-band tree recombines to AP2(f_lo)·AP2(f_hi)·x, and passing the DRY through the same
// two allpasses makes wet and dry differ by GAIN ALONE. Mix then cannot comb at any setting.

struct Svf1
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float g = 0.0f, k = 1.4142136f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** Butterworth section (k = √2) at fc. */
    void setLR (float fcHz, float fs) noexcept
    {
        const float f = clampf (fcHz, 5.0f, 0.245f * fs);   // 0.49 · Nyquist
        g  = std::tan (3.14159265f * f / fs);
        k  = 1.4142136f;
        const float den = 1.0f + g * (g + k);
        a1 = 1.0f / den; a2 = g * a1; a3 = g * a2;
    }

    /** One tick, every tap. lp/hp are the Butterworth halves; `bp` is the RAW v1 node (the
     *  alignment allpass is v0 − 2k·v1, and the caller needs k, which it has). */
    inline void tick (float v0, float& lp, float& hp, float& bp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushd (2.0f * v1 - ic1);
        ic2 = flushd (2.0f * v2 - ic2);
        lp  = v2;
        hp  = v0 - k * v1 - v2;
        bp  = v1;
    }

    /** 2nd-order Butterworth ALLPASS — the band/dry alignment stage. One tick. */
    inline float ap (float v0) noexcept
    {
        float lp, hp, bp;
        tick (v0, lp, hp, bp);
        return v0 - 2.0f * k * bp;
    }
};

/** LR4 split: 3 ticks, LP4 + HP4 out, and LP4 + HP4 = AP2(fc) exactly. */
struct LR4
{
    Svf1 s1, s2lo, s2hi;
    void reset() noexcept { s1.reset(); s2lo.reset(); s2hi.reset(); }
    void set (float fcHz, float fs) noexcept { s1.setLR (fcHz, fs); s2lo.setLR (fcHz, fs); s2hi.setLR (fcHz, fs); }
    inline void split (float x, float& lo, float& hi) noexcept
    {
        float lp1, hp1, bp1, a, b, c;
        s1.tick (x, lp1, hp1, bp1);
        s2lo.tick (lp1, lo, a, b);          // take lp
        s2hi.tick (hp1, c, hi, b);          // take hp
    }
};

// ═════ 5. the shared static-curve evaluator (used by BOTH devices' Viz) ══════
/** Output level (same units as `xdb`) for one input level through a down+up pair.
 *  Compress fills its 32-point knee[] with this; OTT's per-band curve is the same call. */
inline float staticCurve (float xdb, float T, float s, float W,
                          float Tup = -1000.0f, float sUp = 0.0f, float capDb = 0.0f) noexcept
{
    const float gr = grDown (xdb, T, s, W);
    const float up = (sUp > 0.0f) ? liftUp (xdb, Tup, sUp, capDb) * floorGate (xdb, -1000.0f) : 0.0f;
    return xdb - gr + up;
}

} // namespace dyn
} // namespace tw
