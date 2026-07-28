// TerrainFilters.h — Terrain Instrument synth filter bank.
//
// Batch 1: NONE, LADDER LP·24, SVF LP, ACID 303 (original recipe).
// Batch 2: LADDER LP·12 (2-pole tap of the 4-pole loop), LADDER HP·24
//          (pole-mixed single-saturator path — see §1b), DIODE LP (Acid 303
//          core re-voiced clean — §3b), SVF HP, SVF BP (normalized k*v1,
//          Q-independent peak), SVF NOTCH, OB-X SVF (SEM morph + bounded Q).
//
// Built from "Math-locked filter recipes for Terrain Instrument" (research
// report). Every constant in this file is pinned to a numbered section of
// that report — do NOT improvise the math. The musical character lives in
// three details:
//   (a) per-stage nonlinearity inside the resonance loop (Huovilainen),
//   (b) Huovilainen tuning/amplitude polynomials,
//   (c) per-voice slow random drift in semitone space (EROSION, applied by
//       the caller — this header only consumes a per-sample driftSemis).
//
// Header-only. Per-voice instantiation. NOT thread-safe — each voice owns
// its own FilterSlot, mutated only from the audio thread.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tw
{

namespace filters
{

// ─── helpers ─────────────────────────────────────────────────────────────

/** Fast tanh approximation, ~1e-4 accurate over [-5, +5]. Padé form from
 *  the report §1.3 (last paragraph): tanh(x) ≈ x*(27+x²)/(27+9x²). Clamp
 *  input to ±5 so big self-osc spikes can't blow up the denominator. */
inline float fastTanh (float x) noexcept
{
    if (x >  5.0f) return  1.0f;
    if (x < -5.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Map cutoff knob 0..1 to Hz, exponential 20..20kHz. Caller is responsible
 *  for the final clamp against 0.45*fs. §4 of the prompt. */
inline float cutKnobToHz (float cut01) noexcept
{
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, cut01));
}

/** §4 drive helpers. */
inline float driveLinear   (float drv01) noexcept
{
    return std::pow (10.0f, (juce::jlimit (0.0f, 1.0f, drv01) * 24.0f) / 20.0f);
}
inline float driveMakeup   (float driveLin) noexcept
{
    return std::pow (driveLin, -0.5f);
}

/** 1-pole DC blocker. After any asymmetric saturator (Acid 303 post-VCA,
 *  Ladder under heavy DRV). y[n] = x[n] - x[n-1] + 0.995 * y[n-1]. */
struct DCBlocker
{
    float xPrev = 0.0f, yPrev = 0.0f;
    void reset() noexcept { xPrev = 0.0f; yPrev = 0.0f; }
    float process (float x) noexcept
    {
        const float y = x - xPrev + 0.995f * yPrev;
        xPrev = x; yPrev = y;
        return y;
    }
};

/** TPT (trapezoidal-integrator) one-pole helper used by the diode ladder.
 *  Single state `s`. g = tan(π·fc/fs), α = g/(1+g). Per Zavalishin §3.10. */
struct TPTOnePole
{
    float s = 0.0f;
    void reset() noexcept { s = 0.0f; }
    /** Process one sample at integrator gain `alpha` (already prewarped).
     *  Returns lowpass output. */
    float lp (float x, float alpha) noexcept
    {
        const float v = (x - s) * alpha;
        const float y = v + s;
        s = y + v;                // = s + 2v
        return y;
    }
};

// ─── filter type enum (matches Batch 1 prompt §3) ───────────────────────

enum class Type : int
{
    LADDER_LP24 = 0,    LADDER_LP12 = 1,     LADDER_HP24 = 2,
    DIODE_LP    = 3,    ACID_303    = 4,
    SVF_LP      = 5,    SVF_HP      = 6,     SVF_BP      = 7,
    SVF_NOTCH   = 8,    OBX_SVF     = 9,
    COMB_PLUS   = 10,   COMB_MINUS  = 11,    COMB_SHIMMER= 12,  KARPLUS = 13,
    FORMANT_A   = 14,   FORMANT_E   = 15,    FORMANT_I   = 16,  FORMANT_MORPH = 17,
    REVERB_FILT = 18,   PHASER_4P   = 19,    PHASER_8P   = 20,
    RING_MOD    = 21,   BODE_SHIFT  = 22,    BIT_CRUSH   = 23,
    WAVESHAPER  = 24,   GRAIN_MASK  = 25,
    REVERB_FILT_2 = 26,
    NONE        = 27,   // ── FROZEN: everything below is fb165 APPEND-ONLY (saved states hold raw indices) ──
    // fb165 — THE FILTER EXPANSION (Serum-2-scale taxonomy, Max 2026-07-28)
    LADDER_LP6  = 28,   LADDER_LP18 = 29,    GERMAN_LP   = 30,
    GERMANIUM_LP= 31,   FRENCH_LP   = 32,    ACID_SCREAM = 33,
    XPD_HP6     = 34,   XPD_HP12    = 35,    XPD_HP18    = 36,
    XPD_BP12    = 37,   XPD_BP24    = 38,    XPD_BP6     = 39,
    XPD_NOTCH   = 40,   XPD_PHASE   = 41,    XPD_LP1     = 42,
    SVF_LP24    = 43,   SVF_HP24    = 44,    SVF_BP24    = 45,
    SVF_N24     = 46,   SVF_PEAK    = 47,
    SEM_LP      = 48,   SEM_NOTCH   = 49,    SEM_HP      = 50,  SEM_BP = 51,
    MULTI_LH    = 52,   MULTI_LB    = 53,    MULTI_LN    = 54,
    MULTI_HB    = 55,   MULTI_HN    = 56,    MULTI_BB    = 57,
    MULTI_BN    = 58,   MULTI_PP    = 59,    MULTI_NN    = 60,  MULTI_PH = 61,
    COMB_WIDE   = 62,   COMB_OCTAVE = 63,    COMB_FIFTH  = 64,
    COMB_DAMP   = 65,   KARPLUS_BRIGHT = 66, KARPLUS_MUTE = 67,
    FORMANT_O   = 68,   FORMANT_U   = 69,    FORMANT_WIDE = 70, FORMANT_GROWL = 71,
    PHASER_6P   = 72,   PHASER_12P  = 73,    PHASER_16P  = 74,
    DIFFUSOR    = 75,   BODE_DOWN   = 76,
    TILT        = 77,   LOW_EQ      = 78,    HIGH_EQ     = 79,
    BAND_EQ     = 80,   AIR         = 81,    ADD_BASS    = 82,
    SAMPHOLD    = 83,   SAMPHOLD_MINUS = 84,
    SCREAM_LP   = 85,   SCREAM_BP   = 86,
    WASP        = 87,   MS20_LP     = 88,    POLIVOKS    = 89,
    RING_X2     = 90,   RADIO       = 91,
    REVERB_DARK = 92,   REVERB_METAL = 93
};
constexpr int kNumTypes = 94;

// ─── 1. Moog Ladder LP·24 (Huovilainen, corrected ZDF) — report §1 ─────
//
// Four cascaded TPT one-poles in a feedback loop with per-stage Huovilainen
// tanh. Uses the corrected (1-G) form (NOT the Pirkle g-propagation bug —
// see KVR thread 571909 / Della Cioppa correction). Constants from §5B of
// the prompt:
//   fcr = 1.8730·f³ + 0.4955·f² − 0.6490·f + 0.9988    (tuning correction)
//   acr = −3.9364·f² + 1.8409·f + 0.9968              (amp/res correction)
//
// Cache 5 tanh values per sample (tV1..tV4 + tVfb).
// Half-sample feedback delay: y = 0.5*(s4 + s4Prev) * (1 + 0.5k).
// Caller is responsible for oversampling (2x default / 4x HQ).

struct LadderLP24
{
    // State
    float s1 = 0, s2 = 0, s3 = 0, s4 = 0;
    float tV1 = 0, tV2 = 0, tV3 = 0, tV4 = 0, tVfb = 0;
    float s4Prev = 0;

    // Coefficients (recomputed when cutoff/res/fs changes)
    float G = 0.0f, oneMG = 1.0f, Gtot = 0.0f;
    float k = 0.0f;
    float driveComp = 1.0f;       // = 1 + 0.5k (bass restore, §1.2)

    // POLES — switchable slope. The 4-pole ladder computes all four stages every
    // sample, so tapping stage 1/2/3/4 reads out 6/12/18/24 dB/oct for free (the
    // 4-pole resonant feedback loop stays intact regardless of the tap point).
    // poleTap: 0=6dB(s1) 1=12dB(s2) 2=18dB(s3) 3=24dB(s4). poleMakeup level-matches
    // the brighter lower-order taps to the 24 dB reference.
    int   poleTap = 3;
    float poleMakeup = 1.0f;
    float s1Prev = 0.0f, s2Prev = 0.0f, s3Prev = 0.0f;

    void reset() noexcept
    {
        s1 = s2 = s3 = s4 = 0;
        tV1 = tV2 = tV3 = tV4 = tVfb = 0;
        s1Prev = s2Prev = s3Prev = s4Prev = 0;
    }

    /** Set cutoff (Hz) and resonance (0..1) at the current sample rate.
     *  Cheap (one tan, no allocations). Call per-sample if you need to
     *  track envelope/drift smoothly. */
    void setCoeffs (float fcHz, float res01, double fs) noexcept
    {
        const float nyq = 0.5f * (float) fs;
        const float fc  = juce::jlimit (5.0f, 0.49f * nyq, fcHz);
        const float f   = fc / nyq;
        const float fcr = 1.8730f * f * f * f
                        + 0.4955f * f * f
                        - 0.6490f * f
                        + 0.9988f;
        const float acr = -3.9364f * f * f + 1.8409f * f + 0.9968f;
        const float g   = std::tan (juce::MathConstants<float>::pi * fc * fcr / (float) fs);
        G       = g / (1.0f + g);
        oneMG   = 1.0f - G;
        Gtot    = G * G * G * G;
        k       = juce::jlimit (0.0f, 3.99f, 4.0f * res01 * acr);
        driveComp = 1.0f + 0.5f * k;
    }

    /** Process one sample. Returns the filter output. Pre-drive happens
     *  outside (multiply `x` by driveLinear before calling). */
    inline float process (float x) noexcept
    {
        const float S    = oneMG * (G * G * G * s1 + G * G * s2 + G * s3 + s4);
        const float uLin = (driveComp * x - k * S) / (1.0f + k * Gtot);
        const float in1  = fastTanh (uLin - tVfb);

        float v;
        v = (in1 - tV1) * G;  s1 += 2.0f * v;  tV1 = fastTanh (s1);
        v = (tV1 - tV2) * G;  s2 += 2.0f * v;  tV2 = fastTanh (s2);
        v = (tV2 - tV3) * G;  s3 += 2.0f * v;  tV3 = fastTanh (s3);
        v = (tV3 - tV4) * G;  s4 += 2.0f * v;  tV4 = fastTanh (s4);
        tVfb = fastTanh (k * s4);

        // Tap the selected stage (half-sample averaged, like the 24 dB path). Every
        // tap shares the 4-pole resonant feedback; only the readout point changes.
        float y;
        switch (poleTap)
        {
            case 0:  y = 0.5f * (s1 + s1Prev) * driveComp * poleMakeup; break;   // 6 dB/oct
            case 1:  y = 0.5f * (s2 + s2Prev) * driveComp * poleMakeup; break;   // 12 dB/oct
            case 2:  y = 0.5f * (s3 + s3Prev) * driveComp * poleMakeup; break;   // 18 dB/oct
            default: y = 0.5f * (s4 + s4Prev) * driveComp;             break;   // 24 dB/oct (unity)
        }
        s1Prev = s1; s2Prev = s2; s3Prev = s3; s4Prev = s4;
        return y;
    }
};

// ─── 1b. Pole-mixed ladder (HP·24) — Batch 2, research §2 ──────────────
//
// A Moog ladder is intrinsically lowpass; highpass/bandpass come from
// POLE-MIXING — a weighted sum of the four cascaded lowpass taps with the
// Oberheim Xpander binomial weights. The catch (research §2): the binomial
// highpass null is a *linear* cancellation identity, and the per-stage tanh
// of LadderLP24 destroys it (a 1% tap error lifts DC rejection from −∞ to
// −24 dB; per-stage saturation is far worse). So this is a SEPARATE path:
// ONE saturator at the input node, four LINEAR stages, then pole-mix the
// clean taps (the ddiakopoulos/Pirkle "Oberheim Variation" model). Same
// poles as the LP ladder (mixing moves only zeros), so it resonates and
// self-oscillates at the same k≈4 and tracks the same cutoff.
//
// y0 = input node (post-saturator, includes resonance feedback)
// y1..y4 = successive linear lowpass taps
//   HP24 = y0 − 4·y1 + 6·y2 − 4·y3 + y4     ( = (1−z)^4 )
// (weights settable so later batches can do HP12 / BP / Notch off this core.)
// Caller is responsible for oversampling (treat like LADDER_LP24).

struct LadderPoleMix
{
    float s1 = 0, s2 = 0, s3 = 0, s4 = 0;
    float G = 0.0f, oneMG = 1.0f, Gtot = 0.0f, k = 0.0f;

    // Pole-mix weights — default = 24 dB/oct highpass binomial {1,−4,6,−4,1}.
    float w0 = 1.0f, w1 = -4.0f, w2 = 6.0f, w3 = -4.0f, w4 = 1.0f;
    float outMakeup = 1.0f;       // measured level-match to LP24

    void reset() noexcept { s1 = s2 = s3 = s4 = 0; }

    /** Same tuning polynomials as LadderLP24 so HP tracks the LP cutoff. */
    void setCoeffs (float fcHz, float res01, double fs) noexcept
    {
        const float nyq = 0.5f * (float) fs;
        const float fc  = juce::jlimit (5.0f, 0.49f * nyq, fcHz);
        const float f   = fc / nyq;
        const float fcr = 1.8730f * f * f * f
                        + 0.4955f * f * f
                        - 0.6490f * f
                        + 0.9988f;
        const float acr = -3.9364f * f * f + 1.8409f * f + 0.9968f;
        const float g   = std::tan (juce::MathConstants<float>::pi * fc * fcr / (float) fs);
        G     = g / (1.0f + g);
        oneMG = 1.0f - G;
        Gtot  = G * G * G * G;
        k     = juce::jlimit (0.0f, 3.99f, 4.0f * res01 * acr);
    }

    /** Pre-drive happens outside (multiply x by driveLinear). */
    inline float process (float x) noexcept
    {
        // Resolved zero-delay feedback (same S-sum trick as LP24), then ONE
        // saturator at the input node — keeps the linear cancellation intact.
        const float S    = oneMG * (G * G * G * s1 + G * G * s2 + G * s3 + s4);
        const float uLin = (x - k * S) / (1.0f + k * Gtot);
        const float y0   = fastTanh (uLin);

        // Four LINEAR TPT one-poles (non-inverting lowpass taps).
        float v, y1, y2, y3, y4;
        v = (y0 - s1) * G;  y1 = v + s1;  s1 = y1 + v;
        v = (y1 - s2) * G;  y2 = v + s2;  s2 = y2 + v;
        v = (y2 - s3) * G;  y3 = v + s3;  s3 = y3 + v;
        v = (y3 - s4) * G;  y4 = v + s4;  s4 = y4 + v;

        const float out = w0 * y0 + w1 * y1 + w2 * y2 + w3 * y3 + w4 * y4;
        return out * outMakeup;
    }
};


// ─── 2. Cytomic SVF, trapezoidal, ALL outputs — report §2 ──────────────
//
// Per Andrew Simper SvfLinearTrapOptimised2.pdf:
//   g  = tan(π fc / fs)
//   k  = 1/Q                        (damping = 2R in Zavalishin notation)
//   a1 = 1 / (1 + g·(g + k))
//   a2 = g·a1
//   a3 = g·a2
// State: ic1eq, ic2eq.
// All outputs are computed from one pass — LP/BP/HP/Notch/Peak/SEM — Batch 2
// adds the SEM morph + a per-instance qMax ceiling. Normalized BP (returns
// k*v1, not v1) gives Q-independent unity peak so RES sweeps don't volume-jump.
// Stable up to Nyquist for arbitrarily time-varying coefficients
// (Bencina/Wishnick DAFx-14) — no oversampling needed unless DRV is hot.

struct SvfMultimode
{
    enum class Output : int { LP, HP, BP, Notch, Peak, SEM };

    float ic1eq = 0.0f, ic2eq = 0.0f;
    float g = 0.0f, k = 1.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float drive = 1.0f;       // pre-clip of BP node, 1.0 = off
    Output out  = Output::LP;

    // Batch 2 additions:
    //  morph : OB-X / SEM continuous LP→Notch→HP blend (0=LP, .5=Notch, 1=HP).
    //  qMax  : resonance ceiling. Default 2000 keeps the Batch-1 SVF feel
    //          (Q up to ~1000, razor self-osc). OB-X sets it ~60 for the
    //          gentle, musical, non-self-oscillating SEM voicing.
    float morph = 0.0f;
    float qMax  = 2000.0f;

    void reset() noexcept { ic1eq = 0.0f; ic2eq = 0.0f; }

    /** Map RES knob → Q exponentially, per §5C of the prompt:
     *  Q = 0.5 · pow(2000, res01)  → 0.5..1000, self-osc as k→0.
     *  Kept for compatibility; setCoeffs uses the per-instance qMax. */
    static float resToK (float res01) noexcept
    {
        const float Q = 0.5f * std::pow (2000.0f, juce::jlimit (0.0f, 1.0f, res01));
        return 1.0f / juce::jmax (0.0001f, Q);
    }

    void setCoeffs (float fcHz, float res01, double fs) noexcept
    {
        const float nyq = 0.5f * (float) fs;
        const float fc  = juce::jlimit (5.0f, 0.49f * nyq, fcHz);
        g  = std::tan (juce::MathConstants<float>::pi * fc / (float) fs);
        // k from the per-instance ceiling (qMax). Default 2000 == Batch-1 feel.
        const float Q = 0.5f * std::pow (qMax, juce::jlimit (0.0f, 1.0f, res01));
        k  = 1.0f / juce::jmax (0.0001f, Q);
        const float denom = 1.0f + g * (g + k);
        a1 = 1.0f / denom;
        a2 = g * a1;
        a3 = g * a2;
    }

    /** DRV saturates the BP node (Simper's KVR Feb 2020 recommendation —
     *  shallowest nonlinearity, cheapest, ~Moog/SEM warmth). Pass the
     *  linear drive amount (1.0 = off, >1 = saturated). */
    void setDrive (float driveLin) noexcept { drive = juce::jmax (1.0f, driveLin); }

    inline float process (float v0) noexcept
    {
        float v3 = v0 - ic2eq;
        float v1 = a1 * ic1eq + a2 * v3;
        if (drive > 1.0f)
            v1 = drive * fastTanh (v1 / drive);              // BP-node saturation
        float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        // All taps computed; return the active one.
        const float lp    = v2;
        const float hp    = v0 - k * v1 - v2;
        const float notch = v0 - k * v1;          // = lp + hp
        switch (out)
        {
            case Output::LP:    return lp;
            case Output::HP:    return hp;
            case Output::BP:    return k * v1;     // normalized BP: peak ≈ unity, Q-independent level
            case Output::Notch: return notch;
            case Output::Peak:  return 2.0f * v2 - v0 + k * v1;
            case Output::SEM:
            {
                // Oberheim SEM two-segment morph: LP → Notch → HP.
                if (morph < 0.5f)
                {
                    const float a = morph * 2.0f;
                    return (1.0f - a) * lp + a * notch;
                }
                const float a = (morph - 0.5f) * 2.0f;
                return (1.0f - a) * notch + a * hp;
            }
        }
        return lp;
    }
};

// ─── 3. Acid 303 — diode ladder + full 303 path — report §3 ────────────
//
// Pirkle ZDF diode ladder with asymmetric 0.5/0.5/0.5/1.0 per-stage weights
// (TB-303 C1=C/2). Open303 gain compensation:
//   r     = (1 − exp(−3·res)) / (1 − exp(−3))
//   gcomp = (k/17 − 1)·r + 1
//   gcomp = gcomp · (1 + r)
//   y     = 14 · gcomp · y4    (measured level-match to LADDER LP·24 ref)
// Full chain: pre-VCF HP ~80 Hz → diode VCF (with HP ~150 Hz in FB) →
// post-VCA asymmetric soft clip (+0.05 bias) → DC blocker.
// Caller is responsible for 4× oversampling. Self-osc near k=17.

struct Acid303
{
    // Pre-VCF 1-pole HP at ~80 Hz (kills sub mud before the filter)
    float preHpZ = 0.0f, preHpG = 0.0f;

    // Diode-ladder per-stage state (4 TPT one-poles)
    float z1 = 0, z2 = 0, z3 = 0, z4 = 0;

    // Internal HP in feedback loop at ~150 Hz (kills sub-audio resonance,
    // gives the 303 its "thin-at-low-cutoff" character)
    float fbHpZ = 0.0f, fbHpG = 0.0f;

    // DC blocker after the post-saturator
    DCBlocker dcOut;

    // Coefficients
    float alpha = 0.0f;                     // TPT one-pole gain g/(1+g)
    float k = 0.0f;                         // resonance, 0..17 (self-osc at 17)
    float gComp = 1.0f;                     // Open303 gain comp

    void reset() noexcept
    {
        preHpZ = fbHpZ = 0.0f;
        z1 = z2 = z3 = z4 = 0.0f;
        dcOut.reset();
        // ONSET DECLICK — the post-VCA saturator emits tanh(0.05) on sample 0 even
        // from zero state (the +0.05 bias is the 303 squelch character). Pre-charge the
        // DC blocker's xPrev to that value so the first-sample delta is ~0 instead of a
        // step (the click). Steady-state DC removal is unchanged (bias is constant DC).
        dcOut.xPrev = fastTanh (0.05f);
    }

    void setCoeffs (float fcHz, float res01, double fs) noexcept
    {
        const float nyq = 0.5f * (float) fs;
        const float fc  = juce::jlimit (10.0f, 0.49f * nyq, fcHz);
        const float g   = std::tan (juce::MathConstants<float>::pi * fc / (float) fs);
        alpha = g / (1.0f + g);

        // 1-pole HP integrator gains
        const float ghp1 = std::tan (juce::MathConstants<float>::pi *  80.0f / (float) fs);
        preHpG = ghp1 / (1.0f + ghp1);
        const float ghp2 = std::tan (juce::MathConstants<float>::pi * 150.0f / (float) fs);
        fbHpG = ghp2 / (1.0f + ghp2);

        // Resonance: Open303 skew + scale up to ~17 (self-osc). §5D.
        const float resR = (1.0f - std::exp (-3.0f * juce::jlimit (0.0f, 1.0f, res01)))
                         / (1.0f - std::exp (-3.0f));
        k = 17.0f * resR;
        // Open303 gain compensation (the "secret sauce" — without this the
        // filter just goes quiet at high resonance instead of squelching).
        float gc = k / 17.0f;
        gc = (gc - 1.0f) * resR + 1.0f;
        gc = gc * (1.0f + resR);
        gComp = gc;
    }

    /** One sample. Pre-drive happens outside (multiply x by driveLin first).
     *  Uses Schmidt's Open303 sequential pattern (1-sample FB delay, no full
     *  ZDF resolution) — proven to sound right; the Open303 gain compensation
     *  + post-VCA asymmetric clip are where the 303 character lives, not in
     *  the ladder's analytic precision. */
    inline float process (float x) noexcept
    {
        // Pre-VCF 1-pole HP ~80 Hz (kills sub mud before the filter)
        const float hp1Lp = preHpG * (x - preHpZ) + preHpZ;
        preHpZ = 2.0f * hp1Lp - preHpZ;
        const float xHp = x - hp1Lp;

        // HP at ~150 Hz on the resonance feedback signal (the "thin-at-low-
        // cutoff" character; HP-in-feedback per Schmidt rosic_TeeBeeFilter).
        const float fbSig = k * z4;
        const float fbLp  = fbHpG * (fbSig - fbHpZ) + fbHpZ;
        fbHpZ = 2.0f * fbLp - fbHpZ;
        const float fbHp = fbSig - fbLp;

        // 4 cascaded TPT one-poles, asymmetric 1.0 / 0.5 / 0.5 / 0.5 input
        // scaling (TB-303 C1 = C/2 — only the first capacitor is half value).
        float v, lp;
        const float u = xHp - fbHp;
        v = (1.0f * u  - z1) * alpha;  lp = v + z1;  z1 = lp + v;
        v = (0.5f * lp - z2) * alpha;  lp = v + z2;  z2 = lp + v;
        v = (0.5f * lp - z3) * alpha;  lp = v + z3;  z3 = lp + v;
        v = (0.5f * lp - z4) * alpha;  lp = v + z4;  z4 = lp + v;

        // Open303 gain compensation + measured output makeup. The 14.0f is
        // the level-match constant against LADDER LP·24 (was 2.0f originally
        // → −16.9 dB quieter than the ladder). The higher pre-tanh gain also
        // adds the post-clip squelch saturation that makes the 303 growl.
        float y = 14.0f * gComp * lp;   // output makeup: measured level-match (-0.5 dB vs the old -16.9 dB), and the higher pre-tanh gain adds squelch saturation

        // Post-VCA asymmetric soft clip (the growl). +0.05 DC bias asymmetry
        // gets removed by the DC blocker that follows.
        y = fastTanh (y + 0.05f);
        return dcOut.process (y);
    }
};

// ─── 3b. Diode LP — clean diode ladder — Batch 2, research §3 ──────────
//
// The Acid 303's diode core, re-voiced as a neutral, musical lowpass that is
// audibly DISTINCT from the Moog transistor ladder but WITHOUT the 303 path.
// The diode ladder's character (vs Moog): its poles interact electrically —
// they are NOT buffered/isolated like the Moog's, so they sit at spread,
// non-coincident positions (Stinchcombe), giving a thinner, looser corner and
// an earlier, softer distortion onset. We keep that core (the 1.0/0.5/0.5/0.5
// inter-stage scaling is load-bearing for stability — verified) and the
// Open303 resonance gain comp, but STRIP: the pre-VCF 80 Hz HP, the 150 Hz
// feedback HP, and the asymmetric post-VCA squelch clip. A gentle SYMMETRIC
// soft-clip stays for diode grit. Caller oversamples (treat like ACID_303).

struct DiodeLP
{
    float z1 = 0, z2 = 0, z3 = 0, z4 = 0;
    float alpha = 0.0f, k = 0.0f, gComp = 1.0f;
    float outMakeup = 1.0f;       // measured level-match to LP24
    DCBlocker dcOut;

    void reset() noexcept
    {
        z1 = z2 = z3 = z4 = 0.0f;
        dcOut.reset();
    }

    void setCoeffs (float fcHz, float res01, double fs) noexcept
    {
        const float nyq = 0.5f * (float) fs;
        const float fc  = juce::jlimit (10.0f, 0.49f * nyq, fcHz);
        const float g   = std::tan (juce::MathConstants<float>::pi * fc / (float) fs);
        alpha = g / (1.0f + g);

        // Open303 resonance skew, scaled to ~17 (diode self-osc point).
        const float resR = (1.0f - std::exp (-3.0f * juce::jlimit (0.0f, 1.0f, res01)))
                         / (1.0f - std::exp (-3.0f));
        k = 17.0f * resR;
        float gc = k / 17.0f;
        gc = (gc - 1.0f) * resR + 1.0f;
        gc = gc * (1.0f + resR);
        gComp = gc;
    }

    inline float process (float x) noexcept
    {
        // Resonance feedback straight off stage 4 — no HP in the loop (that
        // 150 Hz HP is the 303's "thin" trick; a clean diode keeps its lows).
        const float u = x - k * z4;

        // Diode cascade: 1.0 / 0.5 / 0.5 / 0.5 inter-stage scaling (stability).
        float v, lp;
        v = (1.0f * u  - z1) * alpha;  lp = v + z1;  z1 = lp + v;
        v = (0.5f * lp - z2) * alpha;  lp = v + z2;  z2 = lp + v;
        v = (0.5f * lp - z3) * alpha;  lp = v + z3;  z3 = lp + v;
        v = (0.5f * lp - z4) * alpha;  lp = v + z4;  z4 = lp + v;

        // Safety: the diode resonant loop is only fully tame when oversampled
        // (it's flagged needsOversampling). Clamp states so a pathological
        // extreme-cutoff/high-res case can't diverge into NaN. ±50 is far
        // outside the musical range, so normal output is untouched.
        z1 = juce::jlimit (-50.0f, 50.0f, z1);
        z2 = juce::jlimit (-50.0f, 50.0f, z2);
        z3 = juce::jlimit (-50.0f, 50.0f, z3);
        z4 = juce::jlimit (-50.0f, 50.0f, z4);

        // Resonance gain comp + level-match makeup, then a GENTLE symmetric
        // soft-clip (diode grit, no DC bias / squelch).
        float y = gComp * lp * outMakeup;
        y = fastTanh (0.7f * y) * 1.4286f;        // soft knee, ~unity small-signal
        return dcOut.process (y);
    }
};

// ─── 3c. Comb / delay-line core — Batch 3, research "Core 3" ───────────
//
// ONE interpolated-delay-line + damped-feedback core covering four types:
//   COMB+    positive feedback  → resonates at f0 and ALL harmonics
//   COMB−    inverted feedback  → ODD harmonics only (hollow / square / reedy)
//   SHIMMER  pitch-shifter (+12 st) in the feedback path → ascending octaves
//   KARPLUS  comb + in-loop damping LP = plucked-string; excite the input
//            stream, or call excite() on note-on for a true noise-burst pluck.
//
// CUT → pitch (delay length). RES → feedback. DRV is applied OUTSIDE as an
// input boost (so it makes the comb LOUDER, never quieter). Cubic Catmull-Rom
// fractional delay = modulation-safe tuning across 44.1–192 kHz. In-loop
// one-pole damping + DC blocker + soft limiter (4·tanh(s/4)) keep it stable at
// near-unity feedback and let DRIVE saturate without runaway. Runs at host
// rate — the in-loop LP band-limits, so no oversampling needed.
//
// COMB− halves the delay (D = fs/2f0) so its odd-harmonic fundamental still
// lands on the pitch the user dialed (research §2). The damping LP's phase
// delay is measured at f0 and folded into the delay so the comb stays in tune.

enum class CombMode : int { Plus, Minus, Shimmer, Karplus };

struct CombCore
{
    std::vector<float> buf;
    int   mask = 0, w = 0, size = 0;
    CombMode mode = CombMode::Plus;

    float fbk = 0.0f;          // feedback magnitude g (sign applied per mode)
    float dLine = 8.0f;        // TARGET fractional delay, phase-compensated
    float dCur = -1.0f;        // fb126 — SMOOTHED delay: glides to dLine per sample. setParams
                               // used to SNAP dLine, teleporting the read point through a loop
                               // ringing at up to 0.995 feedback — every filter-env sweep (i.e.
                               // every note-on) clicked, worst at high cutoff where delays are
                               // tiny and jumps are violently large relative. A comb sweep now
                               // BENDS like a flanger (that's the physics). <0 = snap on first use.
    float dSlewA = 0.001f;     // per-sample glide coef (~2.5 ms), set in prepare
    float dampA = 0.0f, dampZ = 0.0f;     // in-loop one-pole damping
    float dcX = 0.0f, dcY = 0.0f;         // in-loop DC blocker

    // shimmer pitch-shifter (dual-window crossfaded sliding tap)
    float shimPhase = 0.0f, shimInc = 0.0f;
    int   shimW = 1024;

    // karplus noise-burst excitation
    int      exciteCount = 0;
    float    exciteGain  = 0.0f;
    uint32_t rng = 0x1234567u;

    inline float noise() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) ((int32_t) rng) * (1.0f / 2147483648.0f);   // ~-1..1
    }

    void prepare (double fs) noexcept
    {
        shimW = (int) (0.030 * fs);                          // 30 ms shimmer window
        int need = (int) std::ceil (fs / 16.0) + shimW + 16; // lowest ~16 Hz + window
        size = 1; while (size < need) size <<= 1;            // power of two
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        dSlewA = 1.0f - std::exp (-1.0f / (0.0025f * (float) fs));   // fb126 — 2.5 ms delay glide
        reset();
    }

    void reset() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0; dampZ = 0.0f; dcX = dcY = 0.0f; shimPhase = 0.0f; exciteCount = 0;
        dCur = -1.0f;                                        // fb126 — snap to target on first use
    }

    // Catmull-Rom cubic read, D samples back from the write head.
    inline float readCubic (float D) const noexcept
    {
        const float rp = (float) w - D + (float) size;
        const int   i  = (int) rp;
        const float fr = rp - (float) i;
        const float y0 = buf[(i - 1) & mask], y1 = buf[i & mask];
        const float y2 = buf[(i + 1) & mask], y3 = buf[(i + 2) & mask];
        const float a0 = y3 - y2 - y0 + y1;
        const float a1 = y0 - y1 - a0;
        const float a2 = y2 - y0;
        return ((a0 * fr + a1) * fr + a2) * fr + y1;
    }

    // Two windowed taps half a cycle apart, constant-power crossfade (sin):
    // the read delay slides so the loop signal climbs +12 st each pass.
    inline float shimmerRead (float baseD) noexcept
    {
        float p1 = shimPhase, p2 = shimPhase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        const float s1 = readCubic (baseD + p1 * (float) shimW);
        const float s2 = readCubic (baseD + p2 * (float) shimW);
        const float e1 = std::sin (juce::MathConstants<float>::pi * p1);
        const float e2 = std::sin (juce::MathConstants<float>::pi * p2);
        shimPhase -= shimInc;                       // decrease → upward shift
        if (shimPhase < 0.0f) shimPhase += 1.0f;
        return e1 * s1 + e2 * s2;
    }

    /** f0 in Hz (from CUT), resonance 0..1. Drive is applied outside. */
    void setParams (float f0, float res01, double fs) noexcept
    {
        const float nyq = 0.5f * (float) fs;
        f0 = juce::jlimit (16.0f, 0.45f * nyq, f0);

        const float dTotal = (mode == CombMode::Minus) ? ((float) fs / (2.0f * f0))
                                                       : ((float) fs / f0);
        float fcDamp;
        switch (mode)
        {
            case CombMode::Karplus: fcDamp = juce::jlimit (800.0f,  nyq, 0.6f * f0 + 2200.0f); break;
            case CombMode::Shimmer: fcDamp = juce::jlimit (1200.0f, nyq, 5000.0f);             break;
            default:                fcDamp = juce::jlimit (2000.0f, nyq, 2.0f * f0 + 6000.0f); break;
        }
        dampA = std::exp (-2.0f * juce::MathConstants<float>::pi * fcDamp / (float) fs);
        const float wf   = 2.0f * juce::MathConstants<float>::pi * f0 / (float) fs;
        const float ph   = -std::atan2 (dampA * std::sin (wf), 1.0f - dampA * std::cos (wf));
        const float pdLp = (wf > 1e-6f) ? (-ph / wf) : 0.0f;   // damping phase delay at f0
        dLine = juce::jlimit (4.0f, (float) size - (float) shimW - 4.0f, dTotal - pdLp);

        switch (mode)
        {
            case CombMode::Plus:
            case CombMode::Minus:   fbk = 0.995f * res01;            break;
            case CombMode::Shimmer: fbk = 0.85f  * res01;            break;   // tamer ceiling
            case CombMode::Karplus: fbk = 0.90f + 0.0995f * res01;   break;   // long ring
        }
        shimInc = 1.0f / (float) shimW;   // ratio 2 (+12 semitones)
    }

    /** Inject a noise burst (Karplus note-on pluck). Optional — KS also
     *  resonates the input stream without this. */
    void excite (float level) noexcept
    {
        exciteCount = juce::jlimit (1, size, (int) dLine);
        exciteGain  = level;
    }

    inline float process (float x) noexcept
    {
        float in = x;
        if (exciteCount > 0) { in += exciteGain * noise(); --exciteCount; }

        if (dCur < 0.0f) dCur = dLine;                       // fb126 — fresh state: snap (buffer is silent)
        else             dCur += dSlewA * (dLine - dCur);    //         live: GLIDE (sweeps bend, never click)
        float d = (mode == CombMode::Shimmer) ? shimmerRead (dCur) : readCubic (dCur);
        dampZ = (1.0f - dampA) * d + dampA * dampZ;          // in-loop damping LP
        d = dampZ;
        const float fb = (mode == CombMode::Minus) ? (-fbk * d) : (fbk * d);

        float s = in + fb;
        s = 4.0f * fastTanh (0.25f * s);                     // soft limiter (~unity small, caps ±4)
        const float y = s - dcX + 0.999f * dcY;              // DC blocker
        dcX = s; dcY = y;

        buf[w] = y; w = (w + 1) & mask;
        return y;
    }
};

// ─── 3d. Formant / vowel core — Batch 4, research "Core 4" ─────────────
//
// FOUR types from one shared bank of FOUR two-pole resonators (one per formant
// F1..F4), summed in PARALLEL with ALTERNATING SIGNS (+F1 −F2 +F3 −F4 — Klatt's
// trick to fill the inter-formant notches). Each resonator is the JOS
// constant-peak-gain form (unity peak at F for ANY bandwidth), so RES changes
// the vowel's SHARPNESS without changing its level, and the per-formant dB
// weights set the vowel's spectral shape:
//
//   y[n] = (1-r²)/2 · (x[n] − x[n−2]) + 2r·cosθ · y[n−1] − r² · y[n−2]
//   r = exp(−π·BW/fs)     θ = 2π·F/fs     (poles at radius r < 1 ⇒ stable ∀ BW>0)
//
//   FORMANT A / E / I : fixed Bass-voice vowel (a / e / i). CUT shifts all
//                       formants (vocal-tract length / "size"), RES = Q.
//   FORMANT MORPH     : CUT sweeps a→e→i→o→u (log-freq / dB / linear-BW
//                       interpolation between adjacent vowels), RES = Q.
//
// DRV = tanh input pre-saturation (talkbox grit) that makes output LOUDER.
// Constant-peak zeros at DC kill offset; a DC blocker on the sum is insurance.
// Coefficients are pure functions of F/BW/fs, so vowels are identical 44.1–192k.
// Runs at host rate (resonators are linear/alias-free).

struct FormantBank
{
    // Bass-voice singer table (Csound Book), nodes a,e,i,o,u × formants F1..F4.
    static constexpr float VF[5][4] = {
        { 600.f, 1040.f, 2250.f, 2450.f },   // a
        { 400.f, 1620.f, 2400.f, 2800.f },   // e
        { 250.f, 1750.f, 2600.f, 3050.f },   // i
        { 400.f,  750.f, 2400.f, 2600.f },   // o
        { 350.f,  600.f, 2400.f, 2675.f } }; // u
    static constexpr float VB[5][4] = {
        {  60.f,  70.f, 110.f, 120.f },      // a
        {  40.f,  80.f, 100.f, 120.f },      // e
        {  60.f,  90.f, 100.f, 120.f },      // i
        {  40.f,  80.f, 100.f, 120.f },      // o
        {  40.f,  80.f, 100.f, 120.f } };    // u
    static constexpr float VDB[5][4] = {
        { 0.f,  -7.f,  -9.f,  -9.f },        // a
        { 0.f, -12.f,  -9.f, -12.f },        // e
        { 0.f, -30.f, -16.f, -22.f },        // i
        { 0.f, -11.f, -21.f, -20.f },        // o
        { 0.f, -20.f, -32.f, -28.f } };      // u

    // Per-vowel output normalization (measured offline vs LP24 on white noise,
    // then scaled for headroom; seats the five vowels at even loudness so MORPH
    // doesn't pump and A/E/I switch evenly).
    static constexpr float kVowelNorm[5] = { 5.01f, 5.67f, 5.58f, 5.00f, 5.07f };

    struct Reson {
        float a1 = 0.f, a2 = 0.f, gain = 0.f;
        float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
        inline void set (float F, float BW, double fs) noexcept {
            const float r  = std::exp (-juce::MathConstants<float>::pi * BW / (float) fs);
            const float th = 2.0f * juce::MathConstants<float>::pi * F / (float) fs;
            a1   = 2.0f * r * std::cos (th);
            a2   = -(r * r);
            gain = 0.5f * (1.0f - r * r);     // unity peak at F for any BW
        }
        inline float process (float x) noexcept {
            const float y = gain * (x - x2) + a1 * y1 + a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        inline void reset() noexcept { x1 = x2 = y1 = y2 = 0.f; }
    };

    Reson  f[4];
    float  g[4]   = { 1.f, 1.f, 1.f, 1.f };   // linear per-formant amp weights
    float  drive  = 1.0f;                      // tanh pre-sat gain (DRV)
    float  driveMk = 1.0f;                     // drive makeup (guarantees louder)
    float  outTrim = 1.0f;                     // per-vowel level normalization
    float  dcX = 0.f, dcY = 0.f;               // DC blocker

    void reset() noexcept { for (auto& r : f) r.reset(); dcX = dcY = 0.f; }

    // shift = formant-frequency multiplier (CUT), qScale = bandwidth multiplier
    // (RES; <1 = sharper). Loads one fixed vowel node (0=a..4=u).
    void setVowel (int v, float shift, float qScale, double fs) noexcept
    {
        const float nyq = 0.45f * (float) fs;
        for (int k = 0; k < 4; ++k) {
            const float F  = juce::jlimit (20.0f, nyq, VF[v][k] * shift);
            const float BW = juce::jmax (25.0f, VB[v][k] * qScale);
            f[k].set (F, BW, fs);
            g[k] = std::pow (10.0f, VDB[v][k] / 20.0f);
        }
        outTrim = kVowelNorm[v];
    }

    // morph 0..1 across a→e→i→o→u (log-freq / dB-linear / BW-linear).
    void setMorph (float morph01, float qScale, double fs) noexcept
    {
        const float nyq = 0.45f * (float) fs;
        const float x = juce::jlimit (0.0f, 1.0f, morph01) * 4.0f;   // 0..4
        const int   i = juce::jlimit (0, 3, (int) x);
        const float p = x - (float) i;
        for (int k = 0; k < 4; ++k) {
            const float F  = juce::jlimit (20.0f, nyq,
                                std::exp ((1.0f - p) * std::log (VF[i][k]) + p * std::log (VF[i+1][k])));
            const float BW = juce::jmax (25.0f, ((1.0f - p) * VB[i][k] + p * VB[i+1][k]) * qScale);
            const float dB = (1.0f - p) * VDB[i][k] + p * VDB[i+1][k];
            f[k].set (F, BW, fs);
            g[k] = std::pow (10.0f, dB / 20.0f);
        }
        outTrim = (1.0f - p) * kVowelNorm[i] + p * kVowelNorm[i+1];
    }

    void setDrive (float driveLin) noexcept { drive = juce::jmax (1.0f, driveLin); driveMk = std::pow (drive, 0.30f); }

    inline float process (float x) noexcept
    {
        const float xs = fastTanh (x * drive);                  // pre-saturation (louder + grit)
        // parallel bank, alternating signs (+ − + −) to fill inter-formant notches
        const float s = g[0]*f[0].process(xs) - g[1]*f[1].process(xs)
                      + g[2]*f[2].process(xs) - g[3]*f[3].process(xs);
        const float yb = s - dcX + 0.999f * dcY;                // DC blocker
        dcX = s; dcY = yb;
        float out = yb * outTrim * driveMk;
        return 4.0f * fastTanh (0.25f * out);                   // soft limiter (peaks safe, normal clean)
    }
};

// ─── 3e. SPECIAL effects — Batch 5 ──────────────────────────────────────
//
// PHASER 4P / 8P (shared cascaded first-order allpass), RING MOD, BIT-CRUSH,
// WAVESHAPER. Each maps the universal CUT/RES/DRV grammar to what's musical
// for a per-voice, envelope-swept synth filter, is level-matched to the
// ladder reference, and DRV always makes output LOUDER.

// First-order allpass  H(z) = (g + z^-1)/(1 + g*z^-1). Unity magnitude, phase
// 0deg->-180deg (-90deg at the break freq). One multiply. (JOS, PASP.)
struct AllpassStage
{
    float g = 0.f, x1 = 0.f, y1 = 0.f;
    inline float process (float x) noexcept {
        const float y = g * (x - y1) + x1;   // = g*x + x1 - g*y1
        x1 = x; y1 = y; return y;
    }
    inline void reset() noexcept { x1 = y1 = 0.f; }
};

// PHASER: N allpass stages spread around CUT (ratio 1.5), summed 0.5*(dry+wet)
// for deepest notches, with feedback = RES (resonance) and a tanh input drive
// (DRV, louder). 4 stages -> 2 notches; 8 -> 4. CUT/ENV sweep the notches.
struct PhaserCore
{
    static constexpr int MAXST = 16;   // fb165 — 12P/16P types
    AllpassStage ap[MAXST];
    int   nStages = 4;
    float fb = 0.f, fbState = 0.f, preDrv = 1.f, drvMk = 1.f;

    void reset() noexcept { for (auto& a : ap) a.reset(); fbState = 0.f; }

    void setParams (int stages, float cutHz, float res01, float driveLin, double fs) noexcept
    {
        nStages = juce::jlimit (2, MAXST, stages);
        const float nyq = 0.45f * (float) fs;
        const float fratio = 1.5f;
        for (int k = 0; k < nStages; ++k) {
            float fk = cutHz * std::pow (fratio, (float) k - (float)(nStages - 1) * 0.5f);
            fk = juce::jlimit (20.0f, nyq, fk);
            const float t = std::tan (juce::MathConstants<float>::pi * fk / (float) fs);
            ap[k].g = (t - 1.0f) / (t + 1.0f);
        }
        fb     = res01 * 0.9f;            // RES -> feedback (cap 0.9 for stability)
        preDrv = driveLin;
        drvMk  = std::pow (driveLin, 0.30f);
    }

    inline float process (float x) noexcept
    {
        const float in = fastTanh (x * preDrv);
        float v = in + fb * fbState;
        for (int k = 0; k < nStages; ++k) v = ap[k].process (v);
        fbState = v;
        const float out = 0.5f * (in + v) * drvMk * 1.45f;   // 1.45 = level-match trim
        return 4.0f * fastTanh (0.25f * out);                 // soft limiter (transparent at normal level)
    }
};

// RING MOD: x * carrier. CUT = carrier freq, RES = sine->diode-bridge blend,
// DRV = input drive. x2 makeup (a unit-sine multiply halves RMS).
struct RingMod
{
    double phase = 0.0, inc = 0.0;
    float  blend = 0.f, preDrv = 1.f, drvMk = 1.f;
    DCBlocker dc;
    void reset() noexcept { phase = 0.0; dc.reset(); }
    void setParams (float carrierHz, float res01, float driveLin, double fs) noexcept
    {
        inc    = (2.0*juce::MathConstants<double>::pi) * (double) carrierHz / fs;
        blend  = res01;
        preDrv = driveLin;
        drvMk  = std::pow (driveLin, 0.30f);
    }
    static inline float diode (float v) noexcept { return v > 0.f ? 0.2f * v * v : 0.f; }
    inline float process (float x) noexcept
    {
        const float in = fastTanh (x * preDrv);
        const float c  = (float) std::sin (phase);
        phase += inc;
        if (phase >= (2.0*juce::MathConstants<double>::pi)) phase -= (2.0*juce::MathConstants<double>::pi);
        const float sineRM = in * c;
        // diode bridge (Parker DAFx-11 topology, simple quadratic diode)
        const float vi = in * 0.5f;
        const float diodeRM = 2.2f * (diode (c + vi) - diode (-(c + vi))
                                    - diode (c - vi) + diode (-(c - vi)));
        float y = sineRM * (1.0f - blend) + diodeRM * blend;
        y = dc.process (y);
        return y * 1.42f * drvMk;   // 1.42 = makeup/level-match vs ladder reference
    }
};

// BIT-CRUSH: DRV (drive in) -> quantize (RES = bit depth, low = crushed) ->
// sample-hold decimate (CUT = sample rate, low = crushed). Aliasing is the
// effect, so this type is deliberately NOT oversampled.
struct BitCrush
{
    float phase = 0.f, hold = 0.f;
    float halfL = 1.f, invHalfL = 1.f, rateRatio = 1.f, preDrv = 1.f, drvMk = 1.f;
    void reset() noexcept { phase = 0.f; hold = 0.f; }
    void setParams (float cutHz, float res01, float driveLin, double fs) noexcept
    {
        const float bits = 1.0f + res01 * 15.0f;                 // 1..16 bits
        halfL    = 0.5f * (std::pow (2.0f, bits) - 1.0f);
        invHalfL = 1.0f / halfL;
        const float cut01 = juce::jlimit (0.0f, 1.0f,
            std::log (juce::jmax (20.0f, cutHz) / 20.0f) / std::log (1000.0f));
        const float targetRate = 20.0f * std::pow ((float) fs / 20.0f, cut01);  // 20Hz..fs
        rateRatio = juce::jlimit (1.0e-4f, 1.0f, targetRate / (float) fs);
        preDrv = driveLin; drvMk = std::pow (driveLin, 0.30f);
    }
    inline float process (float x) noexcept
    {
        const float in = fastTanh (x * preDrv);
        const float q  = std::round (in * halfL) * invHalfL;     // quantize
        phase += rateRatio;
        if (phase >= 1.0f) { phase -= 1.0f; hold = q; }           // sample-hold
        return hold * drvMk;
    }
};

// WAVESHAPER: DRV = drive k, RES = morph tanh->sine-fold, CUT = post-LP tone.
// 1st-order antiderivative anti-aliasing (ADAA) + host 2x oversampling.
struct WaveShaper
{
    float k = 1.f, res = 0.f, makeup = 1.f, lpAlpha = 1.f;
    float x1 = 0.f;
    TPTOnePole post;
    DCBlocker  dc;
    void reset() noexcept { x1 = 0.f; post.reset(); dc.reset(); }

    static inline float logcosh (float z) noexcept {            // stable ln(cosh z)
        const float a = std::fabs (z);
        return a - 0.6931472f + std::log1p (std::exp (-2.0f * a));
    }
    inline float f  (float x) const noexcept {                  // shaping curve
        return (1.0f - res) * fastTanh (k * x) + res * std::sin (k * x);
    }
    inline float F1 (float x) const noexcept {                  // its antiderivative
        return (1.0f - res) * (logcosh (k * x) / k) + res * (-std::cos (k * x) / k);
    }
    void setParams (float cutHz, float res01, float driveLin, double fs) noexcept
    {
        k       = driveLin;                                     // DRV -> drive
        res     = res01;                                        // RES -> morph
        makeup  = std::pow (driveLin, 0.30f);
        const float fc = juce::jlimit (20.0f, 0.45f * (float) fs, cutHz);
        const float t  = std::tan (juce::MathConstants<float>::pi * fc / (float) fs);
        lpAlpha = t / (1.0f + t);                               // resolved TPT gain ∈ (0,1)
    }
    inline float process (float x) noexcept
    {
        // 1st-order ADAA. F1(x1) is recomputed with CURRENT coeffs every sample
        // so a note reset (x1=0) seeds F1(0)=-res/k correctly — without this the
        // first sample is (F1(x)-0)/x, a per-note onset spike that grows with RES.
        const float dx = x - x1;
        float out;
        if      (std::fabs (dx) < 1.0e-5f) out = f (0.5f * (x + x1));   // ill-conditioned
        else if (std::fabs (dx) > 0.9f)    out = f (x);                 // big jump: no ADAA dropout
        else                               out = (F1 (x) - F1 (x1)) / dx;
        x1 = x;
        out = post.lp (out * makeup, lpAlpha);                  // CUT tone
        return dc.process (out);
    }
};

// ─── 3f. ADVANCED specials — Batch 6 (final, closes the 26-type system) ──
//
// REVERB_FILT (18): resonant SVF -> tiny allpass diffuser bloom ("filter into space").
// BODE_SHIFT  (22): true single-sideband frequency shifter (Hilbert/Bode); RES=feedback.
// GRAIN_MASK  (25): Terrain-original — grain-rate Hann gate x resonant spectral mask.

// Fixed-length Schroeder allpass diffuser. v=x+g*v[-M]; y=-g*v+v[-M]. Unity gain.
template <int N>
struct APDiffuser
{
    float buf[N] = { 0.0f };
    int   idx = 0;
    float g = 0.55f;
    void reset() noexcept { for (int i = 0; i < N; ++i) buf[i] = 0.0f; idx = 0; }
    inline float process (float x) noexcept
    {
        const float d = buf[idx];        // v[n-M]
        const float v = x + g * d;        // v[n]
        const float y = -g * v + d;       // y[n]
        buf[idx] = v;
        if (++idx >= N) idx = 0;
        return y;
    }
};

// REVERB FILTER: resonant lowpass feeding 4 coprime allpass diffusers with a
// short feedback bloom + HF damping. CUT=cutoff, RES=resonance+bloom, DRV=drive.
// Tiny per-voice (1486 samples/channel @48k) — a TIMBRE reverb, not a hall.
struct ReverbFilter
{
    SvfMultimode    svf;
    APDiffuser<173> ap0;  APDiffuser<281> ap1;
    APDiffuser<419> ap2;  APDiffuser<613> ap3;
    TPTOnePole damp;
    DCBlocker  dc;
    float loopState = 0.0f, mix = 0.5f, gLoop = 0.5f, preDrv = 1.0f, drvMk = 1.0f, dampA = 0.4f;
    int   dnsign = 1;

    void reset() noexcept
    {
        svf.reset(); ap0.reset(); ap1.reset(); ap2.reset(); ap3.reset();
        damp.reset(); dc.reset(); loopState = 0.0f; dnsign = 1;
    }
    void setParams (float cutHz, float res01, float driveLin, double fs) noexcept
    {
        svf.qMax = 400.0f;                         // musical resonance, not razor self-osc
        svf.out  = SvfMultimode::Output::LP;
        svf.setCoeffs (cutHz, res01, fs);
        const float gd = 0.5f + 0.12f * res01;     // diffusion 0.50..0.62
        ap0.g = ap1.g = ap2.g = ap3.g = gd;
        mix    = 0.30f + 0.50f * res01;            // RES -> wet/bloom
        gLoop  = juce::jlimit (0.0f, 0.88f, 0.45f + 0.45f * res01);   // RES -> decay
        const float dfc = juce::jlimit (200.0f, 0.45f * (float) fs, cutHz * 1.5f + 800.0f);
        const float t   = std::tan (juce::MathConstants<float>::pi * dfc / (float) fs);
        dampA  = t / (1.0f + t);                    // resolved TPT gain
        preDrv = driveLin;  drvMk = std::pow (driveLin, 0.30f);
    }
    inline float process (float x) noexcept
    {
        const float in = fastTanh (x * preDrv);
        const float lp = svf.process (in);                  // resonant filter
        float fbIn = lp + gLoop * loopState;
        float d = ap0.process (fbIn);
        d = ap1.process (d);
        d = ap2.process (d);
        d = ap3.process (d);
        d = damp.lp (d, dampA);                             // HF absorption in the loop
        loopState = d + (float) dnsign * 1.0e-18f;          // denormal guard
        dnsign = -dnsign;
        const float wet = dc.process (d);
        const float out = (1.0f - mix) * lp + mix * wet;
        return 4.0f * fastTanh (0.25f * out * drvMk * 1.8f);// level trim + soft limiter
    }
};

// 2nd-order allpass section H(z) = (A - z^-2)/(1 - A z^-2), A = a^2. (Niemitalo.)
struct BodeAP
{
    float A = 0.0f, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
    inline float process (float x) noexcept
    {
        const float y = A * (x + y2) - x2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// BODE SHIFTER: single-sideband frequency shifter via a 90-degree quadrature
// allpass pair (Niemitalo, 8 mults) + recursive quadrature oscillator.
// CUT = bipolar shift Hz (0.5 = none, <0.5 down, >0.5 up), RES = feedback
// (barber-pole/metallic cascade), DRV = drive (louder). Shifts every partial
// by a fixed +-Hz (linear, inharmonic) — NOT a pitch shifter.
struct BodeShifter
{
    BodeAP iA[4], qA[4];
    float  iDelay = 0.0f;
    double osC = 1.0, osS = 0.0, oscCos = 1.0, oscSin = 0.0;
    int    renorm = 0;
    float  fb = 0.0f, fbState = 0.0f, preDrv = 1.0f, drvMk = 1.0f;
    float  dirMul = 1.0f;   // fb165 — BODE DOWN mirrors the shift direction
    DCBlocker dc;

    void reset() noexcept
    {
        static const float kI[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static const float kQ[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        for (int i = 0; i < 4; ++i) { iA[i].reset(); iA[i].A = kI[i] * kI[i];
                                      qA[i].reset(); qA[i].A = kQ[i] * kQ[i]; }
        iDelay = 0.0f; osC = 1.0; osS = 0.0; fbState = 0.0f; renorm = 0; dc.reset();
    }
    void setParams (float cutHz, float res01, float driveLin, double fs) noexcept
    {
        const float cut01 = juce::jlimit (0.0f, 1.0f,
            std::log (juce::jmax (20.0f, cutHz) / 20.0f) / std::log (1000.0f));
        const float d01  = cut01 - 0.5f;
        const float FMAX = 1000.0f;
        float fshift = (d01 < 0.0f ? -1.0f : 1.0f) * (std::pow (FMAX + 1.0f, 2.0f * std::fabs (d01)) - 1.0f);
        fshift = dirMul * juce::jlimit (-2000.0f, 2000.0f, fshift);
        const double w = 2.0 * juce::MathConstants<double>::pi * (double) fshift / fs;
        oscCos = std::cos (w);  oscSin = std::sin (w);
        fb     = 0.95f * res01;
        preDrv = driveLin;  drvMk = std::pow (driveLin, 0.30f);
    }
    inline float process (float x) noexcept
    {
        const float in = fastTanh ((x + fb * fbState) * preDrv);
        float iSig = in;  for (int k = 0; k < 4; ++k) iSig = iA[k].process (iSig);
        const float iOut = iDelay; iDelay = iSig;           // 1-sample delay aligns I with Q
        float qOut = in;  for (int k = 0; k < 4; ++k) qOut = qA[k].process (qOut);
        const double nC = oscCos * osC - oscSin * osS;       // rotate quadrature osc
        osS = oscSin * osC + oscCos * osS;
        osC = nC;
        if (++renorm >= 512) { renorm = 0; const double m = 1.5 - 0.5 * (osC * osC + osS * osS); osC *= m; osS *= m; }
        const float y = iOut * (float) osC + qOut * (float) osS;   // SSB (signed shift = up/down)
        fbState = dc.process (y);
        return y * drvMk;
    }
};

// GRAIN MASK (Terrain-original): grain-rate Hann gate x resonant bandpass mask.
// CUT = grain rate 2..200 Hz (and mask center), RES = mask focus + dry/mask
// blend, DRV = intensity (louder). ENV->CUT sweeps grain rate into audio rate.
struct GrainMask
{
    SvfMultimode svf;
    float  phase = 0.0f, rate = 10.0f, duty = 0.5f, blend = 0.0f;
    float  preDrv = 1.0f, drvMk = 1.0f, makeup = 1.0f;
    double fs_ = 48000.0;
    void reset() noexcept { svf.reset(); phase = 0.0f; }
    void setParams (float cutHz, float res01, float driveLin, double fs) noexcept
    {
        fs_ = fs;
        const float cut01 = juce::jlimit (0.0f, 1.0f,
            std::log (juce::jmax (20.0f, cutHz) / 20.0f) / std::log (1000.0f));
        rate = 2.0f * std::pow (100.0f, cut01);                       // 2..200 Hz
        const float fcMask = juce::jlimit (40.0f, 0.45f * (float) fs,
                                           200.0f * std::pow (40.0f, cut01));   // 200..8000
        svf.qMax = 600.0f;
        svf.out  = SvfMultimode::Output::BP;
        svf.setCoeffs (fcMask, juce::jlimit (0.0f, 1.0f, 0.3f + 0.6f * res01), fs);
        blend  = res01;
        preDrv = driveLin;  drvMk = std::pow (driveLin, 0.30f);
        makeup = drvMk * 2.0f / std::sqrt (juce::jmax (0.05f, duty * 0.375f));// gating RMS makeup
    }
    inline float process (float x) noexcept
    {
        const float in = fastTanh (x * preDrv);
        phase += rate / (float) fs_;
        if (phase >= 1.0f) phase -= 1.0f;
        const float gEnv = (phase < duty)
            ? 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * phase / duty))
            : 0.0f;
        const float bp     = svf.process (in);
        const float masked = (1.0f - blend) * in + blend * bp;
        return gEnv * masked * makeup;
    }
};

// ─── 3g. REVERB FILTER 2 — Serum/Pigments-style comb reverb — 27th filter ──
//
// Distinct from REVERB_FILT (smooth allpass-diffuser bloom = "real-room" timbre).
// This is COMB-BASED: 3 parallel damped feedback combs (Schroeder/Freeverb-style
// lengths 1116 / 1277 / 1491 samples, scaled by CUT) → series allpass diffusion.
// The comb modal peaks give the signature RIPPLING transfer (jagged teeth that
// move with CUT and deepen with RES — exactly the Pigments "Reverb" filter shape).
//
// CUT = size (comb length → ripple density + decay + brightness).
// RES = decay (feedback) + wet amount.
// DRV = input drive (LOUDER) with soft limiter at the output.

// Damped feedback comb (Schroeder/Freeverb): y=w[-D]; w=x+fb*LP(y); LP one-pole.
template <int MAXLEN>
struct DampComb
{
    float buf[MAXLEN] = { 0.0f };
    int   idx = 0, len = MAXLEN;
    float fb = 0.5f, damp = 0.4f, lpZ = 0.0f;
    void reset() noexcept { for (int i = 0; i < MAXLEN; ++i) buf[i] = 0.0f; idx = 0; lpZ = 0.0f; }
    void setLen (int L) noexcept { len = juce::jlimit (4, MAXLEN, L); if (idx >= len) idx = 0; }
    inline float process (float x) noexcept
    {
        const float y = buf[idx];                  // w[n-D]
        lpZ = (1.0f - damp) * y + damp * lpZ;       // HF damping in the loop
        buf[idx] = x + fb * lpZ;                    // w[n]
        if (++idx >= len) idx = 0;
        return y;
    }
};

struct CombReverb
{
    static constexpr int ML = 1600;
    DampComb<ML>    c0, c1, c2;
    APDiffuser<225> ap;
    DCBlocker dc;
    float mix = 0.5f, preDrv = 1.0f, drvMk = 1.0f;

    void reset() noexcept { c0.reset(); c1.reset(); c2.reset(); ap.reset(); dc.reset(); }

    void setParams (float cutHz, float res01, float driveLin, double fs) noexcept
    {
        const float cut01 = juce::jlimit (0.0f, 1.0f,
            std::log (juce::jmax (20.0f, cutHz) / 20.0f) / std::log (1000.0f));
        const float scale = 0.12f + 0.88f * cut01;             // CUT = size (ripple density/decay)
        (void) fs;                                             // fixed sample-length combs
        c0.setLen ((int) (1116.0f * scale));
        c1.setLen ((int) (1277.0f * scale));
        c2.setLen ((int) (1491.0f * scale));
        const float fb = 0.5f + 0.49f * res01;                 // RES = decay/feedback
        c0.fb = c1.fb = c2.fb = fb;
        const float dmp = juce::jlimit (0.05f, 0.9f, 0.55f - 0.35f * cut01);  // bigger = brighter
        c0.damp = c1.damp = c2.damp = dmp;
        ap.g  = 0.6f;
        mix   = 0.35f + 0.5f * res01;                          // RES = wet amount
        preDrv = driveLin;  drvMk = std::pow (driveLin, 0.30f);
    }
    inline float process (float x) noexcept
    {
        const float in   = fastTanh (x * preDrv);
        const float comb = (c0.process (in) + c1.process (in) + c2.process (in)) * 0.3333f;
        const float diff = ap.process (comb);
        const float wet  = dc.process (diff);
        const float out  = (1.0f - mix) * in + mix * wet;
        return 4.0f * fastTanh (0.25f * out * drvMk * 1.8f);   // level trim + soft limiter
    }
};


// ─── 4. FilterSlot — switchable wrapper consumed by SynthVoice ─────────
//
// One per voice. Owns one of each active filter class (cheap; states are
// reset on type-switch). Drives cutoff per-sample from:
//   cutSemi = hzToSemi(baseCutHz) + env * envValue * 96 + driftSemis
// (Caller computes baseCutHz from cutKnob, envValue from filter ADSR,
//  driftSemis from per-voice EROSION drift.)
//
// Processes stereo (two independent filter instances per channel, ganged
// coefficients). All math runs at the BLOCK sample rate the caller passes
// in via setCoeffs — caller is responsible for oversampling around this
// call when type is LADDER_LP24/LP12/HP24, DIODE_LP, or ACID_303.

// ═══ fb165 — THE FILTER EXPANSION cores (Max: "add like 100 more filters") ═══

// RBJ bell / shelf biquad (EQ & TONE group). One instance = one band. TDF2.
struct BellEQ
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() noexcept { z1 = z2 = 0; }
    void setBell (float fc, float gainDb, float Q, double fs) noexcept
    {
        const float A  = std::pow (10.0f, gainDb / 40.0f);
        const float w  = 2.0f * juce::MathConstants<float>::pi
                       * juce::jlimit (20.0f, 0.45f * (float) fs, fc) / (float) fs;
        const float sn = std::sin (w), cs = std::cos (w);
        const float al = sn / (2.0f * juce::jmax (0.1f, Q));
        const float a0 = 1.0f + al / A;
        b0 = (1.0f + al * A) / a0;  b1 = (-2.0f * cs) / a0;  b2 = (1.0f - al * A) / a0;
        a1 = b1;                    a2 = (1.0f - al / A) / a0;
    }
    void setShelf (float fc, float gainDb, bool high, double fs) noexcept
    {
        const float A  = std::pow (10.0f, gainDb / 40.0f);
        const float w  = 2.0f * juce::MathConstants<float>::pi
                       * juce::jlimit (20.0f, 0.45f * (float) fs, fc) / (float) fs;
        const float sn = std::sin (w), cs = std::cos (w);
        const float al = 0.5f * sn * std::sqrt (2.0f);            // S = 1
        const float tA = 2.0f * std::sqrt (A) * al;
        float A0;
        if (high)
        {
            b0 =      A * ((A + 1) + (A - 1) * cs + tA);
            b1 = -2 * A * ((A - 1) + (A + 1) * cs);
            b2 =      A * ((A + 1) + (A - 1) * cs - tA);
            A0 =          (A + 1) - (A - 1) * cs + tA;
            a1 =      2 * ((A - 1) - (A + 1) * cs);
            a2 =          (A + 1) - (A - 1) * cs - tA;
        }
        else
        {
            b0 =      A * ((A + 1) - (A - 1) * cs + tA);
            b1 =  2 * A * ((A - 1) - (A + 1) * cs);
            b2 =      A * ((A + 1) - (A - 1) * cs - tA);
            A0 =          (A + 1) + (A - 1) * cs + tA;
            a1 =     -2 * ((A - 1) + (A + 1) * cs);
            a2 =          (A + 1) + (A - 1) * cs - tA;
        }
        b0 /= A0; b1 /= A0; b2 /= A0; a1 /= A0; a2 /= A0;
    }
    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// SAMP-HOLD "filter" (Serum Misc homage): hold the input at rate = CUT.
struct SampHoldFx
{
    float held = 0.0f, acc = 1.0f, inc = 0.001f;
    bool  minus = false;
    void reset() noexcept { held = 0.0f; acc = 1.0f; }
    void setParams (float rateHz, double fs) noexcept
    { inc = juce::jlimit (1.0e-4f, 0.9f, rateHz / (float) fs); }
    inline float process (float x) noexcept
    {
        acc += inc; if (acc >= 1.0f) { acc -= 1.0f; held = x; }
        return minus ? (x - held) * 1.4f : held;
    }
};

// Variable-length Schroeder allpass (DIFFUSOR stages + ADD BASS phase rotator).
struct VarAllpass
{
    float buf[1024] = { 0.0f };
    int   idx = 0, len = 256;
    float g = 0.5f;
    void reset() noexcept { for (int i = 0; i < 1024; ++i) buf[i] = 0.0f; idx = 0; }
    void setLen (int L) noexcept { len = juce::jlimit (4, 1023, L); if (idx >= len) idx = 0; }
    inline float process (float x) noexcept
    {
        const float d = buf[idx];
        const float v = x + g * d;
        const float y = -g * v + d;
        buf[idx] = v;
        if (++idx >= len) idx = 0;
        return y;
    }
};

class FilterSlot
{
public:
    void prepare (double sampleRate) noexcept
    {
        fsLocal_ = sampleRate;
        combL_.prepare (sampleRate);
        combR_.prepare (sampleRate);
        reset();
    }

    void reset() noexcept
    {
        ladderL_.reset();   ladderR_.reset();
        ladderHpL_.reset(); ladderHpR_.reset();
        svfL_.reset();      svfR_.reset();
        acidL_.reset();     acidR_.reset();
        diodeL_.reset();    diodeR_.reset();
        combL_.reset();     combR_.reset();
        formantL_.reset();  formantR_.reset();
        phaserL_.reset();   phaserR_.reset();
        ringL_.reset();     ringR_.reset();
        crushL_.reset();    crushR_.reset();
        shaperL_.reset();   shaperR_.reset();
        reverbL_.reset();   reverbR_.reset();
        bodeL_.reset();     bodeR_.reset();
        grainL_.reset();    grainR_.reset();
        combrevL_.reset();  combrevR_.reset();
        svf2L_.reset();     svf2R_.reset();
        ring2L_.reset();    ring2R_.reset();
        dampL_.reset();     dampR_.reset();
        eqAL_.reset(); eqAR_.reset(); eqBL_.reset(); eqBR_.reset();
        shfxL_.reset();     shfxR_.reset();
        for (int i = 0; i < 4; ++i) { vapL_[i].reset(); vapR_[i].reset(); }
        fbScrL_ = 0.0f; fbScrR_ = 0.0f;
    }

    /** Set the active type. State of inactive filters is left dirty —
     *  reset() everything on a swap so a stale tail doesn't kick in when
     *  the user comes back to that type. */
    void setType (Type t) noexcept
    {
        if (t == type_) return;
        type_ = t;
        reset();
    }

    /** POLES — ladder slope override: true = 4-pole/24 dB, false = 2-pole/12 dB tap. Applies to
     *  LADDER_LP24 (LADDER_LP12 stays fixed 12 dB). CPU-free — both taps are already computed. */
    void setPoles (int tapSel) noexcept { poleTapSel_ = juce::jlimit (0, 3, tapSel); }   // 0=6 1=12 2=18 3=24 dB
    int  poleTapSel_ = 3, lastPoleTap_ = 3;        // Poles slope: ladder tap 0..3 (6/12/18/24 dB), 24 default
    // STEREO SPREAD — L/R cutoff offset (0..1 -> +/-kSpreadSemis). Frequency-based cores only.
    void setSpread (float s01) noexcept { spread_ = juce::jlimit (0.0f, 1.0f, s01); }
    float spread_ = 0.0f, lastSpread_ = 0.0f;
    static constexpr float kSpreadSemis = 6.0f;    // +/-6 st at full -> an octave of L/R separation

    /** Per-sample setter. Cutoff in Hz (post-env, post-drift, post-clamp),
     *  resonance 0..1, drive 0..1. */
    void setParams (float cutHz, float res01, float drv01, double fs) noexcept
    {
        cutHz_ = cutHz; res01_ = res01; drv01_ = drv01;
        // CPU: coefficients are a pure function of (type, cut, res, drive, morph, fs) — the voice
        // calls this PER SAMPLE, but with nothing modulating, the inputs are block-constant. Gate
        // the tan()/pow()/exp() coefficient recompute on actual change; NONE has no coefficients.
        if (type_ == Type::NONE) { preDrive_ = 1.0f; postMakeup_ = 1.0f; return; }
        if (cutHz == lastCut_ && res01 == lastRes_ && drv01 == lastDrv_
            && fs == lastFs_ && type_ == lastType_ && morph_ == lastMorph_
            && poleTapSel_ == lastPoleTap_ && spread_ == lastSpread_)
            return;
        lastCut_ = cutHz; lastRes_ = res01; lastDrv_ = drv01;
        lastFs_ = fs; lastType_ = type_; lastMorph_ = morph_;
        lastPoleTap_ = poleTapSel_; lastSpread_ = spread_;
        const float driveLin = driveLinear (drv01);
        // STEREO SPREAD: split the cutoff +/-kSpreadSemis around the set value (L below / R above) so a
        // resonant filter blooms into a wide stereo image. spread_=0 -> sMul=1 -> cutHzL=cutHzR=cutHz
        // (byte-identical to the mono path). Each core clamps cutoff internally, so extremes are safe.
        const float sMul   = std::exp2 (spread_ * kSpreadSemis / 12.0f);
        const float cutHzL = cutHz / sMul;
        const float cutHzR = cutHz * sMul;
        switch (type_)
        {
            case Type::LADDER_LP24:
            {
                // Poles: tap 0..3 -> 6/12/18/24 dB (see LadderLP24). Makeup anchors on the two
                // measured points (24 dB=1.0, 12 dB=kLadder12Makeup) and interpolates the rest.
                ladderL_.poleTap = poleTapSel_; ladderR_.poleTap = poleTapSel_;
                const float mk = std::pow (kLadder12Makeup, (3 - poleTapSel_) * 0.5f);
                ladderL_.poleMakeup = mk; ladderR_.poleMakeup = mk;
                ladderL_.setCoeffs (cutHzL, res01, fs);
                ladderR_.setCoeffs (cutHzR, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            }
            case Type::LADDER_LP12:
                ladderL_.poleTap = 1;  ladderR_.poleTap = 1;   // fixed 12 dB type (Poles doesn't apply here)
                ladderL_.poleMakeup = kLadder12Makeup;
                ladderR_.poleMakeup = kLadder12Makeup;
                ladderL_.setCoeffs (cutHzL, res01, fs);
                ladderR_.setCoeffs (cutHzR, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::LADDER_HP24:
                ladderHpL_.outMakeup = kLadderHp24Makeup;
                ladderHpR_.outMakeup = kLadderHp24Makeup;
                ladderHpL_.setCoeffs (cutHzL, res01, fs);
                ladderHpR_.setCoeffs (cutHzR, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::DIODE_LP:
                diodeL_.outMakeup = kDiodeMakeup;
                diodeR_.outMakeup = kDiodeMakeup;
                diodeL_.setCoeffs (cutHzL, res01, fs);
                diodeR_.setCoeffs (cutHzR, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::SVF_LP:
                setSvf (SvfMultimode::Output::LP, 2000.0f, cutHz, res01, driveLin, fs);
                preDrive_  = driveLin;   // DRV boosts input into the filter (consistent w/ ladder)
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::SVF_HP:
                setSvf (SvfMultimode::Output::HP, 2000.0f, cutHz, res01, driveLin, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::SVF_BP:
                setSvf (SvfMultimode::Output::BP, 2000.0f, cutHz, res01, driveLin, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::SVF_NOTCH:
                setSvf (SvfMultimode::Output::Notch, 2000.0f, cutHz, res01, driveLin, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::OBX_SVF:
                // SEM voicing: gentle bounded Q (qMax 60, no razor self-osc),
                // morph default = LP. Bind morph_ to a UI knob when one exists.
                svfL_.morph = morph_; svfR_.morph = morph_;
                setSvf (SvfMultimode::Output::SEM, 60.0f, cutHz, res01, driveLin, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin) * kObxMakeup;
                break;
            case Type::ACID_303:
                acidL_.setCoeffs (cutHzL, res01, fs);
                acidR_.setCoeffs (cutHzR, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::COMB_PLUS:
            case Type::COMB_MINUS:
            case Type::COMB_SHIMMER:
            case Type::KARPLUS:
            {
                const CombMode m = (type_ == Type::COMB_PLUS)    ? CombMode::Plus
                                 : (type_ == Type::COMB_MINUS)   ? CombMode::Minus
                                 : (type_ == Type::COMB_SHIMMER) ? CombMode::Shimmer
                                 :                                 CombMode::Karplus;
                combL_.mode = m; combR_.mode = m;
                combL_.setParams (cutHzL,            res01, fs);
                combR_.setParams (cutHzR * 1.0015f, res01, fs);   // +2.6 cents base width, plus Spread
                preDrive_  = driveLin;       // DRV boosts the comb input (LOUDER, never quieter)
                postMakeup_= combMakeup (m);
                break;
            }
            case Type::FORMANT_A:
            case Type::FORMANT_E:
            case Type::FORMANT_I:
            case Type::FORMANT_MORPH:
            {
                // CUT: 20Hz–20kHz exp knob → 0..1. RES → resonator Q (sharpness).
                const float cut01  = juce::jlimit (0.0f, 1.0f,
                    std::log (juce::jmax (20.0f, cutHz) / 20.0f) / std::log (1000.0f));
                const float qScale = std::pow (0.1f, res01) * 2.0f;     // RES0 broad → RES1 sharp
                if (type_ == Type::FORMANT_MORPH) {
                    // CUT sweeps the vowel a→e→i→o→u (the "talking" knob).
                    formantL_.setMorph (cut01, qScale, fs);
                    formantR_.setMorph (cut01, qScale, fs);
                } else {
                    // Fixed vowel; CUT shifts all formants (vocal-tract size).
                    const int   v     = (type_ == Type::FORMANT_A) ? 0
                                      : (type_ == Type::FORMANT_E) ? 1 : 2;
                    const float shift = std::exp2 ((cut01 - 0.5f) * 2.0f);  // 0.5×..2×
                    formantL_.setVowel (v, shift, qScale, fs);
                    formantR_.setVowel (v, shift, qScale, fs);
                }
                formantL_.setDrive (driveLin);   // tanh pre-sat inside the bank (LOUDER)
                formantR_.setDrive (driveLin);
                preDrive_  = 1.0f;               // drive handled inside
                postMakeup_= 1.0f;
                break;
            }
            case Type::PHASER_4P:
            case Type::PHASER_8P:
            {
                const int stages = (type_ == Type::PHASER_8P) ? 8 : 4;
                phaserL_.setParams (stages, cutHz, res01, driveLin, fs);
                phaserR_.setParams (stages, cutHz, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;   // handled inside
                break;
            }
            case Type::RING_MOD:
                ringL_.setParams (cutHz, res01, driveLin, fs);
                ringR_.setParams (cutHz, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::BIT_CRUSH:
                crushL_.setParams (cutHz, res01, driveLin, fs);
                crushR_.setParams (cutHz, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::WAVESHAPER:
                shaperL_.setParams (cutHz, res01, driveLin, fs);
                shaperR_.setParams (cutHz, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::REVERB_FILT:
                reverbL_.setParams (cutHz, res01, driveLin, fs);
                reverbR_.setParams (cutHz, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::BODE_SHIFT:
                bodeL_.dirMul = 1.0f; bodeR_.dirMul = 1.0f;
                bodeL_.setParams (cutHz, res01, driveLin, fs);
                bodeR_.setParams (cutHz, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::GRAIN_MASK:
                grainL_.setParams (cutHz, res01, driveLin, fs);
                grainR_.setParams (cutHz, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::REVERB_FILT_2:
                combrevL_.setParams (cutHz, res01, driveLin, fs);
                combrevR_.setParams (cutHz, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            // ═══ fb165 — THE FILTER EXPANSION ═══
            case Type::LADDER_LP6:
            case Type::LADDER_LP18:
            {
                const int tap = (type_ == Type::LADDER_LP6) ? 0 : 2;
                ladderL_.poleTap = tap; ladderR_.poleTap = tap;
                const float mk = std::pow (kLadder12Makeup, (3 - tap) * 0.5f);
                ladderL_.poleMakeup = mk; ladderR_.poleMakeup = mk;
                ladderL_.setCoeffs (cutHzL, res01, fs);
                ladderR_.setCoeffs (cutHzR, res01, fs);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
                break;
            }
            case Type::GERMAN_LP:      // clean ZDF voicing (Serum "German LP" homage): tamed res, soft drive
                ladderL_.poleTap = 3; ladderR_.poleTap = 3;
                ladderL_.poleMakeup = 1.0f; ladderR_.poleMakeup = 1.0f;
                ladderL_.setCoeffs (cutHzL, res01 * 0.85f, fs);
                ladderR_.setCoeffs (cutHzR, res01 * 0.85f, fs);
                preDrive_ = 1.0f + (driveLin - 1.0f) * 0.4f;
                postMakeup_ = driveMakeup (preDrive_);
                break;
            case Type::GERMANIUM_LP:   // fuzzy-forward diode voicing (transistor grit)
                diodeL_.setCoeffs (cutHzL, res01, fs);
                diodeR_.setCoeffs (cutHzR, res01, fs);
                preDrive_ = driveLin * 1.8f; postMakeup_ = driveMakeup (driveLin) * 0.8f;
                break;
            case Type::FRENCH_LP:      // nonlinear-responding LP (Serum "French LP" homage)
                diodeL_.setCoeffs (cutHzL, std::pow (res01, 0.7f), fs);
                diodeR_.setCoeffs (cutHzR, std::pow (res01, 0.7f), fs);
                preDrive_ = driveLin * 1.3f; postMakeup_ = driveMakeup (driveLin) * 0.9f;
                break;
            case Type::ACID_SCREAM:
                acidL_.setCoeffs (cutHzL, juce::jmin (1.0f, res01 * 1.1f), fs);
                acidR_.setCoeffs (cutHzR, juce::jmin (1.0f, res01 * 1.1f), fs);
                preDrive_ = driveLin * 2.2f; postMakeup_ = driveMakeup (driveLin) * 0.75f;
                break;
            case Type::XPD_HP6:  case Type::XPD_HP12: case Type::XPD_HP18:
            case Type::XPD_BP12: case Type::XPD_BP24: case Type::XPD_BP6:
            case Type::XPD_NOTCH: case Type::XPD_PHASE: case Type::XPD_LP1:
            {
                // Oberheim-Xpander pole mixing: ONE ladder core, per-type tap weights
                // (Electric Druid tables). Resonance stays around the 4-pole loop = authentic.
                struct W { float w0, w1, w2, w3, w4, mk; };
                const W w = (type_ == Type::XPD_HP6)   ? W{ 1, -1,  0,  0, 0, 1.0f }
                          : (type_ == Type::XPD_HP12)  ? W{ 1, -2,  1,  0, 0, 1.0f }
                          : (type_ == Type::XPD_HP18)  ? W{ 1, -3,  3, -1, 0, 1.0f }
                          : (type_ == Type::XPD_BP12)  ? W{ 0,  2, -2,  0, 0, 1.6f }
                          : (type_ == Type::XPD_BP24)  ? W{ 0,  0,  4, -8, 4, 1.5f }
                          : (type_ == Type::XPD_BP6)   ? W{ 0,  1, -1,  0, 0, 2.0f }
                          : (type_ == Type::XPD_NOTCH) ? W{ 1, -2,  2,  0, 0, 1.0f }
                          : (type_ == Type::XPD_PHASE) ? W{ 1, -4,  4,  0, 0, 1.0f }
                          :                              W{ 0,  1,  0,  0, 0, 1.0f };
                auto cfg = [&] (LadderPoleMix& m, float c)
                { m.w0 = w.w0; m.w1 = w.w1; m.w2 = w.w2; m.w3 = w.w3; m.w4 = w.w4;
                  m.outMakeup = w.mk; m.setCoeffs (c, res01, fs); };
                cfg (ladderHpL_, cutHzL); cfg (ladderHpR_, cutHzR);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin) * kLadderHp24Makeup;
                break;
            }
            case Type::SVF_LP24: case Type::SVF_HP24: case Type::SVF_BP24: case Type::SVF_N24:
            {
                const SvfMultimode::Output o = (type_ == Type::SVF_LP24) ? SvfMultimode::Output::LP
                                             : (type_ == Type::SVF_HP24) ? SvfMultimode::Output::HP
                                             : (type_ == Type::SVF_BP24) ? SvfMultimode::Output::BP
                                             :                             SvfMultimode::Output::Notch;
                setSvf (o, 2000.0f, cutHz_, res01, driveLin, fs);
                const float sMul = std::exp2 (spread_ * kSpreadSemis / 12.0f);
                svf2L_.qMax = 2000.0f; svf2R_.qMax = 2000.0f; svf2L_.out = o; svf2R_.out = o;
                svf2L_.setCoeffs (cutHz_ / sMul, res01 * 0.5f, fs);
                svf2R_.setCoeffs (cutHz_ * sMul, res01 * 0.5f, fs);
                svf2L_.setDrive (1.0f); svf2R_.setDrive (1.0f);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            }
            case Type::SVF_PEAK:
                setSvf (SvfMultimode::Output::Peak, 2000.0f, cutHz_, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 0.7f;
                break;
            case Type::SEM_LP: case Type::SEM_NOTCH: case Type::SEM_HP: case Type::SEM_BP:
                if (type_ == Type::SEM_BP)
                    setSvf (SvfMultimode::Output::BP, 60.0f, cutHz_, res01, driveLin, fs);
                else
                {
                    svfL_.morph = (type_ == Type::SEM_LP) ? 0.0f : (type_ == Type::SEM_NOTCH) ? 0.5f : 1.0f;
                    svfR_.morph = svfL_.morph;
                    setSvf (SvfMultimode::Output::SEM, 60.0f, cutHz_, res01, driveLin, fs);
                }
                preDrive_ = 1.0f; postMakeup_ = kObxMakeup;
                break;
            case Type::MULTI_LH: case Type::MULTI_LB: case Type::MULTI_LN: case Type::MULTI_HB:
            case Type::MULTI_HN: case Type::MULTI_BB: case Type::MULTI_BN: case Type::MULTI_PP:
            case Type::MULTI_NN: case Type::MULTI_PH:
            {
                // Serum "Multi" homage: dual SVF in parallel, second band fixed +2 octaves.
                using O = SvfMultimode::Output;
                struct M { O a, b; };
                const M m = (type_ == Type::MULTI_LH) ? M{ O::LP, O::HP }
                          : (type_ == Type::MULTI_LB) ? M{ O::LP, O::BP }
                          : (type_ == Type::MULTI_LN) ? M{ O::LP, O::Notch }
                          : (type_ == Type::MULTI_HB) ? M{ O::HP, O::BP }
                          : (type_ == Type::MULTI_HN) ? M{ O::HP, O::Notch }
                          : (type_ == Type::MULTI_BB) ? M{ O::BP, O::BP }
                          : (type_ == Type::MULTI_BN) ? M{ O::BP, O::Notch }
                          : (type_ == Type::MULTI_PP) ? M{ O::Peak, O::Peak }
                          : (type_ == Type::MULTI_NN) ? M{ O::Notch, O::Notch }
                          :                             M{ O::Peak, O::HP };
                setSvf (m.a, 2000.0f, cutHz_, res01, driveLin, fs);
                const float f2   = juce::jmin (cutHz_ * 4.0f, 18000.0f);
                const float sMul = std::exp2 (spread_ * kSpreadSemis / 12.0f);
                svf2L_.qMax = 2000.0f; svf2R_.qMax = 2000.0f; svf2L_.out = m.b; svf2R_.out = m.b;
                svf2L_.setCoeffs (f2 / sMul, res01 * 0.7f, fs);
                svf2R_.setCoeffs (f2 * sMul, res01 * 0.7f, fs);
                svf2L_.setDrive (driveLin); svf2R_.setDrive (driveLin);
                preDrive_ = 1.0f; postMakeup_ = 0.7f;
                break;
            }
            case Type::COMB_WIDE: case Type::COMB_OCTAVE: case Type::COMB_FIFTH:
            {
                const float ratio = (type_ == Type::COMB_WIDE) ? 1.012f
                                  : (type_ == Type::COMB_OCTAVE) ? 2.0f : 1.5f;
                combL_.mode = CombMode::Plus; combR_.mode = CombMode::Plus;
                combL_.setParams (cutHzL, res01, fs);
                combR_.setParams (juce::jmin (cutHzR * ratio, 18000.0f), res01, fs);
                preDrive_ = driveLin; postMakeup_ = kCombPlusMakeup;
                break;
            }
            case Type::KARPLUS_BRIGHT: case Type::KARPLUS_MUTE:
            {
                const float rr = (type_ == Type::KARPLUS_BRIGHT)
                               ? juce::jmin (1.0f, res01 * 1.15f + 0.08f) : res01 * 0.45f;
                combL_.mode = CombMode::Karplus; combR_.mode = CombMode::Karplus;
                combL_.setParams (cutHzL, rr, fs);
                combR_.setParams (cutHzR * 1.0015f, rr, fs);
                preDrive_ = driveLin;
                postMakeup_ = kCombKarplusMakeup * ((type_ == Type::KARPLUS_MUTE) ? 1.4f : 1.0f);
                break;
            }
            case Type::COMB_DAMP:
            {
                const int len = (int) juce::jlimit (8.0f, 4790.0f, (float) fs / juce::jmax (20.0f, cutHz_));
                dampL_.setLen (len); dampR_.setLen ((int) (len * 1.007f) + 1);
                dampL_.fb = 0.5f + res01 * 0.47f;  dampR_.fb = dampL_.fb;
                dampL_.damp = 0.5f;                dampR_.damp = 0.5f;
                preDrive_ = driveLin; postMakeup_ = 1.0f;
                break;
            }
            case Type::FORMANT_O: case Type::FORMANT_U:
            {
                const float cut01  = juce::jlimit (0.0f, 1.0f,
                    std::log (juce::jmax (20.0f, cutHz_) / 20.0f) / std::log (1000.0f));
                const float qScale = std::pow (0.1f, res01) * 2.0f;
                const float shift  = std::exp2 ((cut01 - 0.5f) * 2.0f);
                const int   v      = (type_ == Type::FORMANT_O) ? 3 : 4;
                formantL_.setVowel (v, shift, qScale, fs);
                formantR_.setVowel (v, shift, qScale, fs);
                formantL_.setDrive (driveLin); formantR_.setDrive (driveLin);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            }
            case Type::FORMANT_WIDE: case Type::FORMANT_GROWL:
            {
                const float cut01  = juce::jlimit (0.0f, 1.0f,
                    std::log (juce::jmax (20.0f, cutHz_) / 20.0f) / std::log (1000.0f));
                const float qScale = (type_ == Type::FORMANT_WIDE)
                                   ? std::pow (0.1f, res01) * 4.5f
                                   : std::pow (0.1f, juce::jmin (1.0f, res01 * 1.2f + 0.15f)) * 1.6f;
                formantL_.setMorph (cut01, qScale, fs);
                formantR_.setMorph (cut01, qScale, fs);
                const float dd = (type_ == Type::FORMANT_GROWL) ? driveLin * 2.0f : driveLin;
                formantL_.setDrive (dd); formantR_.setDrive (dd);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            }
            case Type::PHASER_6P: case Type::PHASER_12P: case Type::PHASER_16P:
            {
                const int st = (type_ == Type::PHASER_6P) ? 6 : (type_ == Type::PHASER_12P) ? 12 : 16;
                phaserL_.setParams (st, cutHz_, res01, driveLin, fs);
                phaserR_.setParams (st, cutHz_, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            }
            case Type::DIFFUSOR:
            {
                const float base = juce::jlimit (16.0f, 1000.0f, (float) fs / juce::jmax (60.0f, cutHz_));
                static constexpr float kR[4] = { 1.0f, 1.37f, 1.93f, 2.71f };
                for (int i = 0; i < 4; ++i)
                {
                    vapL_[i].setLen ((int) (base * kR[i]));
                    vapR_[i].setLen ((int) (base * kR[i] * 1.011f) + 1);
                    vapL_[i].g = 0.4f + res01 * 0.5f; vapR_[i].g = vapL_[i].g;
                }
                preDrive_ = driveLin; postMakeup_ = 1.0f;
                break;
            }
            case Type::BODE_DOWN:
                bodeL_.dirMul = -1.0f; bodeR_.dirMul = -1.0f;
                bodeL_.setParams (cutHz_, res01, driveLin, fs);
                bodeR_.setParams (cutHz_, res01, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::TILT:
            {
                const float g = (res01 - 0.5f) * 18.0f;   // RES: dark <-> bright around CUT
                eqAL_.setShelf (cutHz_, -g, false, fs); eqAR_.setShelf (cutHz_, -g, false, fs);
                eqBL_.setShelf (cutHz_,  g, true,  fs); eqBR_.setShelf (cutHz_,  g, true,  fs);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
                break;
            }
            case Type::LOW_EQ: case Type::HIGH_EQ: case Type::AIR:
            {
                const bool  hi  = (type_ != Type::LOW_EQ);
                const float fc2 = (type_ == Type::AIR) ? juce::jmax (cutHz_, 4000.0f) : cutHz_;
                const float g   = (type_ == Type::AIR) ? res01 * 15.0f : res01 * 24.0f - 12.0f;
                eqAL_.setShelf (fc2, g, hi, fs); eqAR_.setShelf (fc2, g, hi, fs);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
                break;
            }
            case Type::BAND_EQ:
                eqAL_.setBell (cutHz_, res01 * 24.0f - 12.0f, 0.7f + drv01 * 4.0f, fs);
                eqAR_.setBell (cutHz_, res01 * 24.0f - 12.0f, 0.7f + drv01 * 4.0f, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::ADD_BASS:
            {
                // Serum's joke-but-useful: phase-rotated LP folded onto the dry with a touch of drive.
                vapL_[0].setLen ((int) (fs * 0.0008)); vapR_[0].setLen ((int) (fs * 0.0008) + 3);
                vapL_[1].setLen ((int) (fs * 0.0019)); vapR_[1].setLen ((int) (fs * 0.0019) + 5);
                vapL_[0].g = vapL_[1].g = vapR_[0].g = vapR_[1].g = 0.5f;
                setSvf (SvfMultimode::Output::LP, 60.0f, juce::jmax (60.0f, cutHz_),
                        0.15f + res01 * 0.3f, 1.0f, fs);
                preDrive_ = driveLin; postMakeup_ = 1.0f;
                break;
            }
            case Type::SAMPHOLD: case Type::SAMPHOLD_MINUS:
                shfxL_.minus = shfxR_.minus = (type_ == Type::SAMPHOLD_MINUS);
                shfxL_.setParams (juce::jmax (30.0f, cutHz_), fs);
                shfxR_.setParams (juce::jmax (30.0f, cutHz_) * 1.003f, fs);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
                break;
            case Type::SCREAM_LP: case Type::SCREAM_BP:
                setSvf ((type_ == Type::SCREAM_LP) ? SvfMultimode::Output::LP : SvfMultimode::Output::BP,
                        400.0f, cutHz_, juce::jmin (1.0f, res01 * 0.9f + 0.05f), 1.0f, fs);
                preDrive_ = driveLin; postMakeup_ = 0.8f;
                break;
            case Type::WASP:
                setSvf (SvfMultimode::Output::LP, 200.0f, cutHz_, res01, driveLin * 3.0f + 2.0f, fs);
                preDrive_ = 1.2f; postMakeup_ = 0.9f;
                break;
            case Type::MS20_LP:
                setSvf (SvfMultimode::Output::LP, 500.0f, cutHz_, std::pow (res01, 0.8f),
                        driveLin * 1.6f + 0.5f, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::POLIVOKS:
                diodeL_.setCoeffs (cutHzL, juce::jmin (1.0f, res01 * 1.25f), fs);
                diodeR_.setCoeffs (cutHzR, juce::jmin (1.0f, res01 * 1.25f), fs);
                preDrive_ = driveLin * 2.5f; postMakeup_ = driveMakeup (driveLin) * 0.7f;
                break;
            case Type::RING_X2:
                ringL_.setParams (cutHz_, res01, driveLin, fs);
                ringR_.setParams (cutHz_, res01, driveLin, fs);
                ring2L_.setParams (juce::jmin (cutHz_ * 1.5f, 18000.0f), res01, 1.0f, fs);
                ring2R_.setParams (juce::jmin (cutHz_ * 1.5f, 18000.0f), res01, 1.0f, fs);
                preDrive_ = 1.0f; postMakeup_ = 0.8f;
                break;
            case Type::RADIO:
            {
                setSvf (SvfMultimode::Output::BP, 2000.0f, cutHz_, juce::jmax (0.4f, res01), 1.0f, fs);
                svf2L_.qMax = 2000.0f; svf2R_.qMax = 2000.0f;
                svf2L_.out = SvfMultimode::Output::BP; svf2R_.out = SvfMultimode::Output::BP;
                svf2L_.setCoeffs (cutHz_, juce::jmax (0.3f, res01 * 0.8f), fs);
                svf2R_.setCoeffs (cutHz_, juce::jmax (0.3f, res01 * 0.8f), fs);
                svf2L_.setDrive (1.0f); svf2R_.setDrive (1.0f);
                crushL_.setParams (juce::jmax (2000.0f, cutHz_), 0.25f, driveLin, fs);
                crushR_.setParams (juce::jmax (2000.0f, cutHz_), 0.25f, driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.6f;
                break;
            }
            case Type::REVERB_DARK:
                reverbL_.setParams (cutHz_ * 0.6f, res01, driveLin * 0.85f, fs);
                reverbR_.setParams (cutHz_ * 0.6f, res01, driveLin * 0.85f, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::REVERB_METAL:
                combrevL_.setParams (cutHz_, juce::jmin (1.0f, res01 * 1.25f + 0.1f), driveLin, fs);
                combrevR_.setParams (cutHz_, juce::jmin (1.0f, res01 * 1.25f + 0.1f), driveLin, fs);
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::NONE:
            default:
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
        }
    }

    /** Process one stereo sample in-place. */
    inline void processStereo (float& l, float& r) noexcept
    {
        switch (type_)
        {
            case Type::LADDER_LP24:
            case Type::LADDER_LP12:
            case Type::LADDER_LP6:  case Type::LADDER_LP18: case Type::GERMAN_LP:
                l = ladderL_.process (l * preDrive_) * postMakeup_;
                r = ladderR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::LADDER_HP24:
            case Type::XPD_HP6:  case Type::XPD_HP12: case Type::XPD_HP18:
            case Type::XPD_BP12: case Type::XPD_BP24: case Type::XPD_BP6:
            case Type::XPD_NOTCH: case Type::XPD_PHASE: case Type::XPD_LP1:
                l = ladderHpL_.process (l * preDrive_) * postMakeup_;
                r = ladderHpR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::DIODE_LP:
            case Type::GERMANIUM_LP: case Type::FRENCH_LP: case Type::POLIVOKS:
                l = diodeL_.process (l * preDrive_) * postMakeup_;
                r = diodeR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::SVF_LP:
            case Type::SVF_HP:
            case Type::SVF_BP:
            case Type::SVF_NOTCH:
            case Type::OBX_SVF:
            case Type::SVF_PEAK:
            case Type::SEM_LP: case Type::SEM_NOTCH: case Type::SEM_HP: case Type::SEM_BP:
            case Type::WASP:   case Type::MS20_LP:
                l = svfL_.process (l * preDrive_) * postMakeup_;
                r = svfR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::ACID_303:
            case Type::ACID_SCREAM:
                l = acidL_.process (l * preDrive_) * postMakeup_;
                r = acidR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::COMB_PLUS:
            case Type::COMB_MINUS:
            case Type::COMB_SHIMMER:
            case Type::KARPLUS:
            case Type::COMB_WIDE: case Type::COMB_OCTAVE: case Type::COMB_FIFTH:
            case Type::KARPLUS_BRIGHT: case Type::KARPLUS_MUTE:
                l = combL_.process (l * preDrive_) * postMakeup_;
                r = combR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::FORMANT_A:
            case Type::FORMANT_E:
            case Type::FORMANT_I:
            case Type::FORMANT_MORPH:
            case Type::FORMANT_O: case Type::FORMANT_U:
            case Type::FORMANT_WIDE: case Type::FORMANT_GROWL:
                l = formantL_.process (l * preDrive_) * postMakeup_;
                r = formantR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::PHASER_4P:
            case Type::PHASER_8P:
            case Type::PHASER_6P: case Type::PHASER_12P: case Type::PHASER_16P:
                l = phaserL_.process (l); r = phaserR_.process (r); break;
            case Type::RING_MOD:
                l = ringL_.process (l);   r = ringR_.process (r);   break;
            case Type::BIT_CRUSH:
                l = crushL_.process (l);  r = crushR_.process (r);  break;
            case Type::WAVESHAPER:
                l = shaperL_.process (l); r = shaperR_.process (r); break;
            case Type::REVERB_FILT:
            case Type::REVERB_DARK:
                l = reverbL_.process (l); r = reverbR_.process (r); break;
            case Type::BODE_SHIFT:
            case Type::BODE_DOWN:
                l = bodeL_.process (l);   r = bodeR_.process (r);   break;
            case Type::GRAIN_MASK:
                l = grainL_.process (l);  r = grainR_.process (r);  break;
            case Type::REVERB_FILT_2:
            case Type::REVERB_METAL:
                l = combrevL_.process (l); r = combrevR_.process (r); break;
            // ═══ fb165 — genuinely new signal paths ═══
            case Type::SVF_LP24: case Type::SVF_HP24: case Type::SVF_BP24: case Type::SVF_N24:
                l = svf2L_.process (svfL_.process (l)) * postMakeup_;
                r = svf2R_.process (svfR_.process (r)) * postMakeup_;
                break;
            case Type::MULTI_LH: case Type::MULTI_LB: case Type::MULTI_LN: case Type::MULTI_HB:
            case Type::MULTI_HN: case Type::MULTI_BB: case Type::MULTI_BN: case Type::MULTI_PP:
            case Type::MULTI_NN: case Type::MULTI_PH:
            {
                const float li = l, ri = r;
                l = (svfL_.process (li) + svf2L_.process (li)) * postMakeup_;
                r = (svfR_.process (ri) + svf2R_.process (ri)) * postMakeup_;
                break;
            }
            case Type::COMB_DAMP:
            {
                const float li = l * preDrive_, ri = r * preDrive_;
                l = 0.5f * (li + dampL_.process (li));
                r = 0.5f * (ri + dampR_.process (ri));
                break;
            }
            case Type::DIFFUSOR:
            {
                float v = l * preDrive_; for (int i = 0; i < 4; ++i) v = vapL_[i].process (v); l = v;
                v = r * preDrive_;       for (int i = 0; i < 4; ++i) v = vapR_[i].process (v); r = v;
                break;
            }
            case Type::TILT:
                l = eqBL_.process (eqAL_.process (l * preDrive_)) * postMakeup_;
                r = eqBR_.process (eqAR_.process (r * preDrive_)) * postMakeup_;
                break;
            case Type::LOW_EQ: case Type::HIGH_EQ: case Type::AIR: case Type::BAND_EQ:
                l = eqAL_.process (l * preDrive_) * postMakeup_;
                r = eqAR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::ADD_BASS:
            {
                float v = vapL_[1].process (vapL_[0].process (l));
                l = fastTanh ((l + svfL_.process (v)) * preDrive_ * 0.8f) * 1.25f;
                v = vapR_[1].process (vapR_[0].process (r));
                r = fastTanh ((r + svfR_.process (v)) * preDrive_ * 0.8f) * 1.25f;
                break;
            }
            case Type::SAMPHOLD: case Type::SAMPHOLD_MINUS:
                l = shfxL_.process (l * preDrive_) * postMakeup_;
                r = shfxR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::SCREAM_LP: case Type::SCREAM_BP:
            {
                const float fbAmt = 0.3f + res01_ * 0.65f;
                const float dGain = 1.0f + drv01_ * 6.0f;
                float in = l * preDrive_ + fastTanh (fbScrL_ * dGain) * fbAmt;
                l = svfL_.process (in) * postMakeup_; fbScrL_ = l;
                in = r * preDrive_ + fastTanh (fbScrR_ * dGain) * fbAmt;
                r = svfR_.process (in) * postMakeup_; fbScrR_ = r;
                break;
            }
            case Type::RING_X2:
                l = ring2L_.process (ringL_.process (l)) * postMakeup_;
                r = ring2R_.process (ringR_.process (r)) * postMakeup_;
                break;
            case Type::RADIO:
            {
                float v = svf2L_.process (svfL_.process (l));
                l = crushL_.process (v) * postMakeup_;
                v = svf2R_.process (svfR_.process (r));
                r = crushR_.process (v) * postMakeup_;
                break;
            }
            case Type::NONE:
            default:
                // True bypass — Max finally hears the oscillators clean.
                break;
        }
    }

    Type getType() const noexcept { return type_; }

    /** Public for visualization / debug. Caller reads these after setParams. */
    float currentCutoffHz()   const noexcept { return cutHz_; }
    float currentResonance()  const noexcept { return res01_; }
    float currentDrive()      const noexcept { return drv01_; }

    /** Whether the active type uses a per-sample nonlinearity that benefits
     *  from oversampling (Ladder family + Diode LP + Acid 303). SVF + NONE don't. */
    bool needsOversampling() const noexcept
    {
        return type_ == Type::LADDER_LP24 || type_ == Type::LADDER_LP12
            || type_ == Type::LADDER_HP24 || type_ == Type::DIODE_LP
            || type_ == Type::ACID_303
            || type_ == Type::WAVESHAPER  || type_ == Type::RING_MOD
            || type_ == Type::BODE_SHIFT
            // fb165 — same nonlinear cores, new voicings/mixes
            || type_ == Type::LADDER_LP6  || type_ == Type::LADDER_LP18
            || type_ == Type::GERMAN_LP   || type_ == Type::GERMANIUM_LP
            || type_ == Type::FRENCH_LP   || type_ == Type::POLIVOKS
            || type_ == Type::ACID_SCREAM
            || (type_ >= Type::XPD_HP6 && type_ <= Type::XPD_LP1)
            || type_ == Type::RING_X2     || type_ == Type::BODE_DOWN;
    }

    /** Set the OB-X / SEM morph (0=LP, .5=Notch, 1=HP). Wired for when a
     *  morph knob exists; until then OB-X uses the default (LP-voiced SEM). */
    void setMorph (float m01) noexcept { morph_ = juce::jlimit (0.0f, 1.0f, m01); }

    /** Karplus-Strong note-on pluck (OPTIONAL). Call from SynthVoice on
     *  note-on for a true noise-burst pluck; without it, KARPLUS resonates
     *  whatever the oscillators feed in (exciter mode). No-op for other types. */
    void excite (float level = 1.0f) noexcept
    {
        if (type_ == Type::KARPLUS) { combL_.excite (level); combR_.excite (level); }
    }

private:
    /** Configure both SVF channels for a given output tap + Q ceiling. */
    void setSvf (SvfMultimode::Output o, float qMax,
                 float cutHz, float res01, float driveLin, double fs) noexcept
    {
        // STEREO SPREAD: offset L/R cutoff (same +/-kSpreadSemis split as the direct cores above).
        const float sMul = std::exp2 (spread_ * kSpreadSemis / 12.0f);
        svfL_.qMax = qMax; svfR_.qMax = qMax;
        svfL_.out  = o;    svfR_.out  = o;
        svfL_.setCoeffs (cutHz / sMul, res01, fs);
        svfR_.setCoeffs (cutHz * sMul, res01, fs);
        svfL_.setDrive  (driveLin);
        svfR_.setDrive  (driveLin);
    }

    // Per-mode level-match constants (measured offline against LP24 ref).
    static constexpr float kLadder12Makeup  = 0.99f;  // measured -1.94 dB vs ref -2.06
    static constexpr float kLadderHp24Makeup= 1.05f;  // measured -2.50 dB -> match
    static constexpr float kDiodeMakeup     = 13.0f;  // 0.5-scaling deficit, like the 303
    static constexpr float kObxMakeup       = 1.0f;   // SEM already ~unity passband

    // Comb output trims (measured; the in-loop limiter caps level, these just
    // seat the four comb types near the LP24 reference so switching is neutral).
    static constexpr float kCombPlusMakeup    = 1.0f;
    static constexpr float kCombMinusMakeup   = 1.0f;
    static constexpr float kCombShimmerMakeup = 1.0f;
    static constexpr float kCombKarplusMakeup = 1.0f;

    static float combMakeup (CombMode m) noexcept
    {
        switch (m)
        {
            case CombMode::Plus:    return kCombPlusMakeup;
            case CombMode::Minus:   return kCombMinusMakeup;
            case CombMode::Shimmer: return kCombShimmerMakeup;
            case CombMode::Karplus: return kCombKarplusMakeup;
        }
        return 1.0f;
    }

    float  morph_     = 0.0f;     // OB-X / SEM morph (UI-bindable)

    // setParams change-gate memo (lastFs_ = -1 → first call always recomputes)
    float  lastCut_   = -1.0f, lastRes_ = -1.0f, lastDrv_ = -1.0f, lastMorph_ = -1.0f;
    double lastFs_    = -1.0;
    Type   lastType_  = Type::NONE;

    Type   type_      = Type::NONE;
    double fsLocal_   = 48000.0;
    float  cutHz_     = 20000.0f;
    float  res01_     = 0.0f;
    float  drv01_     = 0.0f;
    float  preDrive_  = 1.0f;
    float  postMakeup_= 1.0f;

    LadderLP24    ladderL_,   ladderR_;
    LadderPoleMix ladderHpL_, ladderHpR_;   // HP24 (pole-mixed, single-sat path)
    SvfMultimode  svfL_,      svfR_;
    Acid303       acidL_,     acidR_;
    DiodeLP       diodeL_,    diodeR_;
    CombCore      combL_,     combR_;        // COMB ± / SHIMMER / KARPLUS
    FormantBank   formantL_,  formantR_;     // FORMANT A / E / I / MORPH
    PhaserCore    phaserL_,   phaserR_;       // PHASER 4P / 8P
    RingMod       ringL_,     ringR_;         // RING MOD
    BitCrush      crushL_,    crushR_;        // BIT-CRUSH
    WaveShaper    shaperL_,   shaperR_;       // WAVESHAPER
    ReverbFilter  reverbL_,   reverbR_;       // REVERB FILTER
    BodeShifter   bodeL_,     bodeR_;         // BODE SHIFTER
    GrainMask     grainL_,    grainR_;        // GRAIN MASK
    CombReverb    combrevL_,  combrevR_;      // REVERB FILTER 2 (Serum/Pigments comb)

    // ── fb165 expansion cores ──
    SvfMultimode   svf2L_,  svf2R_;      // 24 dB cascades / MULTI 2nd band / RADIO 2nd BP
    RingMod        ring2L_, ring2R_;     // RING X2 second carrier
    DampComb<4800> dampL_,  dampR_;      // COMB DAMP (fs/20Hz fits at 48k x2)
    BellEQ         eqAL_, eqAR_, eqBL_, eqBR_;   // EQ & TONE (B pair = TILT's high band)
    SampHoldFx     shfxL_,  shfxR_;
    VarAllpass     vapL_[4], vapR_[4];   // DIFFUSOR 4-stage + ADD BASS rotator (stages 0-1)
    float          fbScrL_ = 0.0f, fbScrR_ = 0.0f;   // SCREAM feedback state
};

} // namespace filters
} // namespace tw
