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
    NONE        = 26
};
constexpr int kNumTypes = 27;

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

    // Batch 2: 12 dB/oct (2-pole) tap. Output stage 2 instead of stage 4 with
    // the 4-pole feedback loop intact (Diva/Repro switchable-slope approach).
    // twoPoleMakeup level-matches the steeper-passband 2-pole tap to LP24.
    bool  twoPole = false;
    float s2Prev  = 0.0f;
    float twoPoleMakeup = 1.0f;   // measured constant

    void reset() noexcept
    {
        s1 = s2 = s3 = s4 = 0;
        tV1 = tV2 = tV3 = tV4 = tVfb = 0;
        s4Prev = 0;
        s2Prev = 0;
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

        float y;
        if (twoPole)
            y = 0.5f * (s2 + s2Prev) * driveComp * twoPoleMakeup;   // 12 dB/oct tap
        else
            y = 0.5f * (s4 + s4Prev) * driveComp;                   // 24 dB/oct
        s2Prev = s2;
        s4Prev = s4;
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
    float dLine = 8.0f;        // fractional delay, phase-compensated
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
        reset();
    }

    void reset() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0; dampZ = 0.0f; dcX = dcY = 0.0f; shimPhase = 0.0f; exciteCount = 0;
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

        float d = (mode == CombMode::Shimmer) ? shimmerRead (dLine) : readCubic (dLine);
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

    /** Per-sample setter. Cutoff in Hz (post-env, post-drift, post-clamp),
     *  resonance 0..1, drive 0..1. */
    void setParams (float cutHz, float res01, float drv01, double fs) noexcept
    {
        cutHz_ = cutHz; res01_ = res01; drv01_ = drv01;
        const float driveLin = driveLinear (drv01);
        switch (type_)
        {
            case Type::LADDER_LP24:
                ladderL_.twoPole = false; ladderR_.twoPole = false;
                ladderL_.setCoeffs (cutHz, res01, fs);
                ladderR_.setCoeffs (cutHz, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::LADDER_LP12:
                ladderL_.twoPole = true;  ladderR_.twoPole = true;
                ladderL_.twoPoleMakeup = kLadder12Makeup;
                ladderR_.twoPoleMakeup = kLadder12Makeup;
                ladderL_.setCoeffs (cutHz, res01, fs);
                ladderR_.setCoeffs (cutHz, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::LADDER_HP24:
                ladderHpL_.outMakeup = kLadderHp24Makeup;
                ladderHpR_.outMakeup = kLadderHp24Makeup;
                ladderHpL_.setCoeffs (cutHz, res01, fs);
                ladderHpR_.setCoeffs (cutHz, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::DIODE_LP:
                diodeL_.outMakeup = kDiodeMakeup;
                diodeR_.outMakeup = kDiodeMakeup;
                diodeL_.setCoeffs (cutHz, res01, fs);
                diodeR_.setCoeffs (cutHz, res01, fs);
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
                acidL_.setCoeffs (cutHz, res01, fs);
                acidR_.setCoeffs (cutHz, res01, fs);
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
                combL_.setParams (cutHz,           res01, fs);
                combR_.setParams (cutHz * 1.0015f, res01, fs);   // ~+2.6 cents → stereo width
                preDrive_  = driveLin;       // DRV boosts the comb input (LOUDER, never quieter)
                postMakeup_= combMakeup (m);
                break;
            }
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
                l = ladderL_.process (l * preDrive_) * postMakeup_;
                r = ladderR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::LADDER_HP24:
                l = ladderHpL_.process (l * preDrive_) * postMakeup_;
                r = ladderHpR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::DIODE_LP:
                l = diodeL_.process (l * preDrive_) * postMakeup_;
                r = diodeR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::SVF_LP:
            case Type::SVF_HP:
            case Type::SVF_BP:
            case Type::SVF_NOTCH:
            case Type::OBX_SVF:
                l = svfL_.process (l * preDrive_) * postMakeup_;
                r = svfR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::ACID_303:
                l = acidL_.process (l * preDrive_) * postMakeup_;
                r = acidR_.process (r * preDrive_) * postMakeup_;
                break;
            case Type::COMB_PLUS:
            case Type::COMB_MINUS:
            case Type::COMB_SHIMMER:
            case Type::KARPLUS:
                l = combL_.process (l * preDrive_) * postMakeup_;
                r = combR_.process (r * preDrive_) * postMakeup_;
                break;
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
            || type_ == Type::ACID_303;
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
        svfL_.qMax = qMax; svfR_.qMax = qMax;
        svfL_.out  = o;    svfR_.out  = o;
        svfL_.setCoeffs (cutHz, res01, fs);
        svfR_.setCoeffs (cutHz, res01, fs);
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
};

} // namespace filters
} // namespace tw
