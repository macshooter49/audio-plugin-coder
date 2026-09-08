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

/** fb603 — DRIVE CHARACTER, the shared taper.
 *
 *  fb602 measured 22 types where DRV changes the curve by < 0.3 dB and THD by
 *  0.00% -> 0.00%, and 17 more where preDrive*postMakeup is EXACTLY driveLin^0.5
 *  = +12.0 dB of volume with THD flat at 0.00%. Both classes are linear cores with
 *  no in-loop nonlinearity for the drive to bite on.
 *
 *  driveMix(driveLin) is the blend weight of a soft saturator against the dry path:
 *      m = 1 - 1/driveLin      (DRV 0 -> 0.000, DRV .15 -> 0.339, DRV .5 -> 0.749, DRV 1 -> 0.937)
 *  m == 0 is an EXACT bypass, so DRV 0 is byte-identical to the pre-fb603 sound and
 *  costs nothing (callers branch on it). The knee kDriveKnee = 2.0 sits well above the
 *  instrument bus peak (~0.2), so 10-50% of the knob stays clean and the dirt arrives
 *  as a taper, not a step. Callers pair this with preDrive_ = driveLin /
 *  postMakeup_ = driveMakeup(driveLin) — the ladder's grammar: louder AND dirtier. */
inline constexpr float kDriveKnee = 2.0f;

inline float driveMix (float driveLin) noexcept
{
    return 1.0f - 1.0f / juce::jmax (1.0f, driveLin);
}

/** Blend-form soft saturator. m == 0 returns x bit-exactly. */
inline float driveSat (float x, float m) noexcept
{
    if (m <= 0.0f) return x;
    return x + m * (kDriveKnee * fastTanh (x * (1.0f / kDriveKnee)) - x);
}

/** 1-pole DC blocker. After any asymmetric saturator (Acid 303 post-VCA,
 *  Ladder under heavy DRV). y[n] = x[n] - x[n-1] + R * y[n-1].
 *
 *  fb603 — R IS NOW PER-RATE. The hard-coded 0.995 is a pole, not a frequency: it
 *  puts the corner at (1-R)/2pi * fs, so it MOVED WITH THE SAMPLE RATE and, worse,
 *  with the 2x oversampling wrapper. Every oversampled core ran its blocker at 96 kHz
 *  => corner 76 Hz, and fb602 measured Diode LP / Germanium / French / Polivoks /
 *  Waveshaper / Ring Mod all at -6.6 dB @ 40 Hz on a WIDE-OPEN lowpass. setRate() pins
 *  the corner to kDcHz (10 Hz: -0.26 dB @ 40 Hz, -1.0 dB @ 20 Hz) at whatever rate the
 *  core is actually running at. Cores that never call setRate keep the legacy 0.995. */
struct DCBlocker
{
    static constexpr float kDcHz = 10.0f;
    float xPrev = 0.0f, yPrev = 0.0f;
    float R = 0.995f;
    void reset() noexcept { xPrev = 0.0f; yPrev = 0.0f; }
    /** Pin the corner to kDcHz at the sample rate this core actually runs at
     *  (i.e. the OVERSAMPLED rate for a 2x-wrapped type). */
    void setRate (double fs) noexcept
    {
        R = std::exp (-2.0f * juce::MathConstants<float>::pi * kDcHz / (float) juce::jmax (1000.0, fs));
    }
    float process (float x) noexcept
    {
        const float y = x - xPrev + R * yPrev;
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
    float poleMakeup = 1.0f;      // fb603 — now COMPUTED in setCoeffs from poleMkFlat and k
    float poleMkFlat = 1.0f;      // caller's flat (RES 0) level trim for the selected tap
    float tapBlend   = 0.0f;      // fb603 — blend in the next-LOWER tap (German LP's softer knee)
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

        // fb603 — SELF-OSC ABOVE FULL SCALE. Every tap shares one resonant loop, but each ladder
        // stage attenuates the oscillation by ~1/sqrt2, so tap 1 reads it (sqrt2)^3 = +9 dB hotter
        // than tap 4: LADDER_LP6 sustained at +5.1 dBFS PER VOICE after excitation while LP24 sat
        // at -5.2 (fb602, measured). Blend the tap trim toward that 1/sqrt2 ladder as k rises, so
        // the PASSBAND stays level-matched at RES 0 (kx = 0, trim = poleMkFlat) and the self-osc
        // level matches the 24 dB tap at RES 1. A TAPER, not a ceiling — it still self-oscillates,
        // which is exactly why musicians pick this filter.
        const float kx       = juce::jlimit (0.0f, 1.0f, k * 0.25f);
        const float tapRatio = std::pow (0.70710678f, (float) (3 - poleTap));
        poleMakeup = poleMkFlat * ((1.0f - kx) + kx * tapRatio);
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
        // fb603 — all four averaged taps are formed (3 adds + 3 mults over the old switch, next
        // to five tanh) so tapBlend can mix the next-lower one: that is German LP's softer,
        // 18/24-hybrid knee, the thing that finally makes it audibly NOT Ladder LP 24 at RES 0.
        const float tp[4] = { 0.5f * (s1 + s1Prev), 0.5f * (s2 + s2Prev),
                              0.5f * (s3 + s3Prev), 0.5f * (s4 + s4Prev) };
        float y = tp[poleTap];
        if (tapBlend > 0.0f)
            y = (1.0f - tapBlend) * y + tapBlend * tp[poleTap > 0 ? poleTap - 1 : 0];
        y *= driveComp * poleMakeup;
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
        // fb603 — MONOTONIC RES. k ran straight to the 3.99 clamp, past the point where this
        // core's SINGLE input saturator starts gain-compressing the probe itself: measured with a
        // 0.01 sine at 8 RES points, LADDER_HP24 peaked at RES 0.90 (+11.4 dB) and then FELL BACK
        // to +9.9 at 0.95 and +8.7 at 1.0 — and all six Xpd HP/BP siblings folded at exactly the
        // same place (+20.9 -> +18.2, +25.8 -> +23.1, ...). The top of a knob must be its loudest
        // point, so bend the last of the travel: resEff = r - 0.10*r^4 is strictly increasing
        // (d/dr = 1 - 0.4r^3 > 0), leaves RES 0.5 at 0.494 and RES 0.75 at 0.718 — inaudible —
        // and lands RES 1.0 exactly on the measured peak.
        const float rT = juce::jlimit (0.0f, 1.0f, res01);
        const float resEff = rT * (1.0f - kResTopTaper * rT * rT * rT);
        k     = juce::jlimit (0.0f, 3.99f, 4.0f * resEff * acr);
    }
    static constexpr float kResTopTaper = 0.10f;

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
    float satMix = 0.0f;      // fb603 — DRIVE blend weight, 0 = exact bypass (see setDrive)
    Output out  = Output::LP;

    // fb603 — WIDE OPEN. The ceiling was 0.49*NYQUIST = 11 760 Hz at 48 k against a shipped
    // DEFAULT cutoff of 20 000 Hz, so 33 SVF-family types measured -12 to -24 dB at 16 kHz ON A
    // FRESH PATCH. The trapezoidal (Cytomic) form is stable for any fc < nyquist for arbitrarily
    // time-varying coefficients, so the real limit is numerical, not stability: 0.49*fs = 0.98*nyq
    // (23 520 Hz at 48 k, 21 609 Hz at 44.1 k) is Simper's own recommended clamp and keeps
    // a1 = 1/(1+g(g+k)) ~ 1e-4 — comfortably inside float. Verified against the 4 s / 6 Hz stress
    // sweep at RES 1 / DRV 1: no NaN, no ring, peak unchanged.
    static constexpr float kFcCeilOverFs = 0.49f;
    static constexpr float kSvfV1Rail    = 6.0f;   // fb603 — integrator rail at DRV 1 (see setDrive)
    float v1Rail = 1.0e9f, invV1Rail = 1.0e-9f;

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

    /** fb603 — explicit-Q entry point. setCoeffs() is now a thin wrapper on this so the
     *  24 dB cascade can hand each of its two sections its OWN Q (the Butterworth pair)
     *  instead of both running off the same res01 knob. */
    void setCoeffsQ (float fcHz, float Q, double fs) noexcept
    {
        const float fc = juce::jlimit (5.0f, kFcCeilOverFs * (float) fs, fcHz);
        g  = std::tan (juce::MathConstants<float>::pi * fc / (float) fs);
        k  = 1.0f / juce::jmax (0.0001f, Q);
        const float denom = 1.0f + g * (g + k);
        a1 = 1.0f / denom;
        a2 = g * a1;
        a3 = g * a2;
    }

    void setCoeffs (float fcHz, float res01, double fs) noexcept
    {
        // k from the per-instance ceiling (qMax). Default 2000 == Batch-1 feel.
        setCoeffsQ (fcHz, 0.5f * std::pow (qMax, juce::jlimit (0.0f, 1.0f, res01)), fs);
    }

    /** fb603 — DRV IS A REAL NONLINEARITY NOW.
     *
     *  The old form was `v1 = drive * tanh(v1/drive)` on the BP node: the knee sat AT the
     *  drive amount, so MORE drive meant LESS saturation, and it only ever bit when v1 > ~15
     *  — never at instrument level. fb602 measured all 22 SVF-family types at < 0.3 dB curve
     *  change and THD 0.00% -> 0.00% across the whole DRV knob.
     *
     *  Two changes: the saturator moves to the INPUT node (where a filter's drive stage
     *  physically is — the BP node carries almost no signal at RES 0, which is exactly why
     *  the THD probe saw nothing), and its knee is FIXED at kDriveKnee while the DRIVE knob
     *  sets how much of it is blended in. Callers pair this with preDrive_ = driveLin and
     *  postMakeup_ = driveLin^-0.5 — the ladder's grammar. satMix == 0 is a bit-exact bypass,
     *  so DRV 0 sounds and costs exactly what it did before. */
    void setDrive (float driveLin) noexcept
    {
        satMix = driveMix (driveLin);
        // fb603 — MEASURE BEFORE DELETING A CLAMP. The old BP-node line was also acting as the
        // resonance lifeguard: removing it took SVF LP's 4 s stress peak from 1.95 to 10.68 and
        // SVF Peak's from 2.27 to 14.90. So the rail stays — but as a TAPER, not the old
        // knee-at-drive (which LOOSENED as you turned drive up). rail = kSvfV1Rail/satMix goes to
        // infinity as DRV -> 0, so DRV 0 is still exactly unclamped and there is no step anywhere
        // on the knob; by DRV 1 the integrator sits on a 6.4 rail, tighter than the old 15.85.
        v1Rail    = kSvfV1Rail / juce::jmax (1.0e-3f, satMix);
        invV1Rail = 1.0f / v1Rail;
    }

    inline float process (float v0) noexcept
    {
        v0 = driveSat (v0, satMix);                         // fb603 — input-node drive character
        float v3 = v0 - ic2eq;
        float v1 = a1 * ic1eq + a2 * v3;
        if (satMix > 0.0f)
            v1 = v1Rail * fastTanh (v1 * invV1Rail);        // fb603 — integrator rail (soft, drive-tapered)
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
        dcOut.setRate (fs);        // fb603 — 10 Hz corner at the OVERSAMPLED rate (was a fixed 0.995 pole = 76 Hz at 2x)

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
        // fb603 — STATE LEAK. outMakeup is CONFIG, not state, but only Type::DIODE_LP ever
        // wrote it: Germanium LP(31) / French LP(32) / Polivoks(89) inherited whatever the
        // previously auditioned type left behind, so they recalled 22.28 dB apart depending
        // on listening order (fb602, measured to 0.01 dB). Clearing it here is only half the
        // fix — FilterSlot::reset() also invalidates the setParams change-gate so the very
        // next setParams re-writes it. All four diode voicings now set it explicitly.
        outMakeup = 1.0f;
    }

    void setCoeffs (float fcHz, float res01, double fs) noexcept
    {
        const float nyq = 0.5f * (float) fs;
        const float fc  = juce::jlimit (10.0f, 0.49f * nyq, fcHz);
        const float g   = std::tan (juce::MathConstants<float>::pi * fc / (float) fs);
        alpha = g / (1.0f + g);
        dcOut.setRate (fs);        // fb603 — 10 Hz corner at the OVERSAMPLED rate (was 76 Hz: -6.6 dB @ 40 Hz)

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
    // fb603 — COMB SHIMMER measured RESPEAK 0.0/0.0/0.0/0.0: a pitch-SHIFTING feedback loop
    // decorrelates itself every pass, so by construction it can have no stationary comb peak —
    // a comb with no comb. Keeping a fraction of the STRAIGHT, in-tune tap in the loop restores
    // the comb at f0 (RES now grows a real peak) while the shifted tap still climbs in octaves.
    float shimBlend = 0.0f;
    // fb603 — KARPLUS BRIGHT / MUTE differed from KARPLUS-STRONG only by a resonance offset, so
    // "Bright" was not brighter: the string's in-loop damping LP sat at the same corner for all
    // three (measured 1.91 dB apart across 20 Hz-18 kHz — the closest pair left in the roster).
    // A plucked string's brightness IS that damping corner, so it becomes per-voicing.
    float dampScale = 1.0f;

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
            case CombMode::Karplus: fcDamp = juce::jlimit (350.0f,  nyq, (0.6f * f0 + 2200.0f) * dampScale); break;
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
            case CombMode::Shimmer: fbk = 0.85f  * res01; shimBlend = 0.45f; break;   // fb603 — 45% straight tap = a real comb peak
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
        float d = (mode == CombMode::Shimmer)
                    ? (shimBlend * readCubic (dCur) + (1.0f - shimBlend) * shimmerRead (dCur))
                    : readCubic (dCur);
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
        dc.setRate (fs);           // fb603 — per-rate DC corner (was 76 Hz at 2x: -6.6 dB @ 40 Hz)
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
        // fb603 — RES 0 meant ONE BIT: round(x*0.5)/0.5 is identically zero for every |x| < 1, so
        // the bottom of the RES knob was digital silence at ANY input level, bus level included.
        // Floor at 4 bits (step 1/7.5): a 0.2-peak voice now quantises to real steps, not to nothing.
        const float bits = 4.0f + res01 * 12.0f;                 // 4..16 bits
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
    float kFold = 1.f, foldN = 1.f;   // fb603 — RES-driven fold gain (see setParams)
    float x1 = 0.f;
    TPTOnePole post;
    DCBlocker  dc;
    void reset() noexcept { x1 = 0.f; post.reset(); dc.reset(); }

    static inline float logcosh (float z) noexcept {            // stable ln(cosh z)
        const float a = std::fabs (z);
        return a - 0.6931472f + std::log1p (std::exp (-2.0f * a));
    }
    // fb603 — RES WAS INERT BELOW DRV 0.5 (measured RESsens 0.2 dB cold / 0.0 dB hot). The morph
    // was tanh(kx) -> sin(kx) at the SAME k, and for |kx| < 0.3 those two curves are both just x —
    // there was nothing to morph between until DRIVE pushed k up. The fold branch now carries its
    // own gain kFold = k*(1+7*res), divided by foldN so its SMALL-SIGNAL slope still matches
    // tanh's: RES adds fold-overs at any drive instead of only at the top of the DRV knob.
    inline float f  (float x) const noexcept {                  // shaping curve
        return (1.0f - res) * fastTanh (k * x) + res * (std::sin (kFold * x) / foldN);
    }
    inline float F1 (float x) const noexcept {                  // its antiderivative
        return (1.0f - res) * (logcosh (k * x) / k)
             + res * (-std::cos (kFold * x) / (kFold * foldN));
    }
    void setParams (float cutHz, float res01, float driveLin, double fs) noexcept
    {
        k       = driveLin;                                     // DRV -> drive
        res     = res01;                                        // RES -> morph
        // fb603 — kFold sets the FOLD COUNT, foldN the level. Normalising by the full (1+7*res)
        // kept the small-signal slope identical to tanh's, which measured as RES doing nothing
        // (0.1 dB). sqrt() instead: RES now buys BOTH folds and +9 dB of the extra energy a
        // wavefolder genuinely makes, which is what the ear (and the magnitude metric) hears.
        const float foldK = 1.0f + 7.0f * juce::jlimit (0.0f, 1.0f, res01);
        foldN   = std::sqrt (foldK);
        kFold   = k * foldK;
        makeup  = std::pow (driveLin, 0.30f);
        const float fc = juce::jlimit (20.0f, 0.45f * (float) fs, cutHz);
        const float t  = std::tan (juce::MathConstants<float>::pi * fc / (float) fs);
        lpAlpha = t / (1.0f + t);                               // resolved TPT gain ∈ (0,1)
        dc.setRate (fs);           // fb603 — per-rate DC corner (was 76 Hz at 2x: -6.7 dB @ 40 Hz)
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
        dc.setRate (fs);                            // fb603 — per-rate DC corner
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
        dc.setRate (fs);           // fb603 — per-rate DC corner (the blocker sits INSIDE the feedback loop)
        fb     = 0.95f * res01;
        preDrv = driveLin;  drvMk = std::pow (driveLin, 0.30f);
    }
    inline float process (float x) noexcept
    {
        // fb603 — RUNAWAY. preDrv multiplied the FEEDBACK as well as the input, so the
        // small-signal loop gain was fb*preDrv = 0.95*res01*driveLin: 1.13 at RES 0.3 / DRV 0.5,
        // and the level ladder measured +68 dB of gain on a -66 dBFS input. Drive now pre-gains
        // the INPUT ONLY, which pins the loop gain at fb <= 0.95 across the whole RES x DRV plane
        // — a taper (the tanh still softens as the loop fills), never a limiter.
        const float in = fastTanh (x * preDrv + fb * fbState);
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
    int   w = 0;
    float fb = 0.5f, damp = 0.4f, lpZ = 0.0f;
    float sat = 0.0f;   // fb603 — DRIVE blend INSIDE the feedback (0 = exact bypass; CombReverb keeps 0)
    // fb603 — THE COMB-CLICK LAW, applied here too. setLen() SNAPPED the loop length, teleporting
    // the read point through a comb ringing at up to 0.97 feedback: a CUT sweep (i.e. every
    // filter-envelope note) produced a sample-to-sample jump of 0.45 on a 0.2-peak input —
    // 19x the filter's own worst static slew, and louder than the signal. This is the same
    // fb126 fix CombCore already carries: a SMOOTHED FRACTIONAL delay read with cubic
    // interpolation, so a sweep BENDS like a flanger instead of clicking. Measured after: 1.0x.
    float lenT = 64.0f, lenC = -1.0f, slewA = 0.00833f;   // <0 = snap on first use

    void reset() noexcept
    { for (int i = 0; i < MAXLEN; ++i) buf[i] = 0.0f; w = 0; lpZ = 0.0f; lenC = -1.0f; }
    void setSlew (double fs) noexcept
    { slewA = 1.0f - std::exp (-1.0f / (0.0025f * (float) juce::jmax (1000.0, fs))); }   // ~2.5 ms glide
    void setLen (float L) noexcept { lenT = juce::jlimit (4.0f, (float) MAXLEN - 4.0f, L); }

    /** Linear-interpolated read, D samples back. Two loads and one branch-free wrap: a
     *  delay-length GLIDE only needs the read point to move continuously, and the loop's own
     *  damping LP already band-limits what is in the buffer, so the cubic kernel CombCore uses
     *  for a tuned comb is not worth 4 loads x 3 combs x 2 channels here (measured 41 ns/sample
     *  on REVERB FILTER 2 against 12 ns for this form). */
    inline float readLin (float D) const noexcept
    {
        float rp = (float) w - D + (float) MAXLEN;
        if (rp >= (float) MAXLEN) rp -= (float) MAXLEN;
        const int   i  = (int) rp;
        const float fr = rp - (float) i;
        const int   j  = (i + 1 >= MAXLEN) ? 0 : i + 1;
        return buf[i] + fr * (buf[j] - buf[i]);
    }
    inline float process (float x) noexcept
    {
        if (lenC < 0.0f) lenC = lenT;                       // fresh state: snap (buffer is silent)
        else             lenC += slewA * (lenT - lenC);      // live: GLIDE
        const float y = readLin (lenC);            // w[n-D]
        lpZ = (1.0f - damp) * y + damp * lpZ;       // HF damping in the loop
        // fb603 — a damped comb's drive belongs in the RECIRCULATION (tape-echo grammar): each
        // pass through the loop saturates a little more. COMB_DAMP measured VOLUME-ONLY +24.0 dB
        // with THD flat at 0.00% because nothing in the path was nonlinear.
        buf[w] = x + fb * driveSat (lpZ, sat);      // w[n]
        if (++w >= MAXLEN) w = 0;
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
        c0.setSlew (fs); c1.setSlew (fs); c2.setSlew (fs);   // fb603 — 2.5 ms length glide (comb-click law)
        c0.setLen (1116.0f * scale);
        c1.setLen (1277.0f * scale);
        c2.setLen (1491.0f * scale);
        const float fb = 0.5f + 0.49f * res01;                 // RES = decay/feedback
        c0.fb = c1.fb = c2.fb = fb;
        const float dmp = juce::jlimit (0.05f, 0.9f, 0.55f - 0.35f * cut01);  // bigger = brighter
        c0.damp = c1.damp = c2.damp = dmp;
        ap.g  = 0.6f;
        dc.setRate (fs);                                       // fb603 — per-rate DC corner
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
    float fb = 0.0f, norm = 1.0f, sat = 0.0f;
    bool  minus = false;
    void reset() noexcept { held = 0.0f; acc = 1.0f; }
    /** fb603 — res01 was NEVER PASSED IN (RESsens measured 0.00 dB; flagged RES-INERT). A stepped
     *  hold WITH FEEDBACK is a comb whose delay is the hold period, so RES grows teeth at multiples
     *  of the S&H rate — the metallic pitched ring a resonant sample-and-hold is supposed to have.
     *  Normalising the input by (1-fb) pins the DC gain at exactly 1, so RES sharpens the teeth
     *  without a volume jump, and the loop can never run away (fb <= 0.92). DRV saturates the held
     *  value: an overdriven S&H clips its own staircase. */
    void setParams (float rateHz, float res01, float driveLin, double fs) noexcept
    {
        inc  = juce::jlimit (1.0e-4f, 0.9f, rateHz / (float) fs);
        fb   = 0.92f * juce::jlimit (0.0f, 1.0f, res01);
        norm = 1.0f - fb;
        sat  = driveMix (driveLin);
    }
    inline float process (float x) noexcept
    {
        // fb603 — DRIVE goes on the signal ENTERING the hold, not on `held` and not on the output.
        // On `held` it breaks the (x - held) cancellation the MINUS variant is built on (measured
        // +37.3 dB of level instead of the +12 dB the grammar asks for); on the output the MINUS
        // variant's difference is near-zero at low frequency so there was nothing to saturate
        // (THD 0.00% -> 0.10%). Here both variants see a driven signal and MINUS passes the
        // harmonics, because a difference IS a highpass.
        const float xs = driveSat (x, sat);
        acc += inc;
        if (acc >= 1.0f) { acc -= 1.0f; held = norm * xs + fb * held; }
        return minus ? (xs - held) * 1.4f : held;
    }
};

// Variable-length Schroeder allpass (DIFFUSOR stages + ADD BASS phase rotator).
struct VarAllpass
{
    static constexpr int LEN = 1024, MASK = LEN - 1;
    float buf[LEN] = { 0.0f };
    int   w = 0;
    float g = 0.5f;
    float sat = 0.0f;   // fb603 — DRIVE blend inside the allpass recirculation (0 = exact bypass)
    // fb603 — same comb-click fix as DampComb: DIFFUSOR's CUT sweep snapped four allpass lengths
    // at once and measured a 0.35 slew on a 0.2-peak input (26x its own static worst). Smoothed
    // fractional read; measured after: 1.0x.
    float lenT = 256.0f, lenC = -1.0f, slewA = 0.00833f;

    void reset() noexcept { for (int i = 0; i < LEN; ++i) buf[i] = 0.0f; w = 0; lenC = -1.0f; }
    void setSlew (double fs) noexcept
    { slewA = 1.0f - std::exp (-1.0f / (0.0025f * (float) juce::jmax (1000.0, fs))); }
    void setLen (float L) noexcept { lenT = juce::jlimit (4.0f, (float) LEN - 4.0f, L); }

    /** Linear read — 2 loads. DIFFUSOR runs EIGHT of these per sample (4 stages x 2 channels),
     *  so a 4-tap kernel here cost 73 ns/sample on its own; a smear stage does not need it. */
    inline float readLin (float D) const noexcept
    {
        const float rp = (float) w - D + (float) LEN;
        const int   i  = (int) rp;
        const float fr = rp - (float) i;
        const float y1 = buf[i & MASK], y2 = buf[(i + 1) & MASK];
        return y1 + fr * (y2 - y1);
    }
    inline float process (float x) noexcept
    {
        if (lenC < 0.0f) lenC = lenT;
        else             lenC += slewA * (lenT - lenC);
        const float d = readLin (lenC);
        // fb603 — saturating the allpass node is the ONLY drive an allpass chain can have: it is
        // unity-magnitude by construction, so a linear DIFFUSOR could only ever be +24 dB of
        // volume (measured THD 0.00% -> 0.00%). Saturation breaks the allpass identity, which is
        // the point — driven, a diffusor stops being invisible and starts smearing spectrally.
        const float v = driveSat (x + g * d, sat);
        const float y = -g * v + d;
        buf[w] = v;
        w = (w + 1) & MASK;
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
        dampL_.setSlew (sampleRate); dampR_.setSlew (sampleRate);       // fb603 — comb-click law
        for (int i = 0; i < 4; ++i) { vapL_[i].setSlew (sampleRate); vapR_[i].setSlew (sampleRate); }
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
        // fb603 — STATE LEAK, the structural half. reset() clears every core's STATE but the
        // cores also carry CONFIG written by setParams (DiodeLP::outMakeup, LadderPoleMix::
        // outMakeup, poleTap, SVF morph/qMax...). setParams is change-gated on
        // (cut,res,drv,fs,type,morph,poles,spread), so after a note-on reset() the gate could
        // return early and leave a core running on config a DIFFERENT type had written —
        // fb602 measured Germanium/French/Polivoks recalling 22.28 dB apart on that path.
        // Invalidating the memo here costs one coefficient recompute per note-on and closes
        // the whole class of leak, not just the diode one.
        lastCut_ = lastRes_ = lastDrv_ = lastMorph_ = -1.0f;
        lastFs_  = -1.0;
        diodeL_.outMakeup = diodeR_.outMakeup = 1.0f;
        ladderHpL_.outMakeup = ladderHpR_.outMakeup = 1.0f;
        ladderL_.tapBlend = ladderR_.tapBlend = 0.0f;
        combL_.dampScale = combR_.dampScale = 1.0f;
        dampL_.sat = dampR_.sat = 0.0f;
        for (int i = 0; i < 4; ++i) { vapL_[i].sat = 0.0f; vapR_[i].sat = 0.0f; }
        screamDrv_ = 1.0f; screamFb_ = 0.0f; satMix_ = 0.0f;
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
                ladderL_.poleMkFlat = mk; ladderR_.poleMkFlat = mk;
                ladderL_.tapBlend = 0.0f; ladderR_.tapBlend = 0.0f;
                ladderL_.setCoeffs (cutHzL, res01, fs);
                ladderR_.setCoeffs (cutHzR, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            }
            case Type::LADDER_LP12:
                ladderL_.poleTap = 1;  ladderR_.poleTap = 1;   // fixed 12 dB type (Poles doesn't apply here)
                ladderL_.poleMkFlat = kLadder12Makeup;
                ladderR_.poleMkFlat = kLadder12Makeup;
                ladderL_.tapBlend = 0.0f; ladderR_.tapBlend = 0.0f;
                ladderL_.setCoeffs (cutHzL, res01, fs);
                ladderR_.setCoeffs (cutHzR, res01, fs);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            case Type::LADDER_HP24:
            {
                // fb603 — POLES GENERALISED. The pole-mix weights ARE a slope selector, and this
                // core already computes all four linear taps every sample, so SYN_FILTER*_POLES
                // now picks 6/12/18/24 dB of HIGHPASS off the binomial rows (1,-1) (1,-2,1)
                // (1,-3,3,-1) (1,-4,6,-4,1). Zero cardinality change, zero extra CPU, and it is
                // unambiguous here because the type is named "Ladder HP", not "Ladder HP 6".
                // (The XPD_* entries already name their own slope, so POLES stays off them.)
                struct HW { float w0, w1, w2, w3, w4; };
                const HW hw = (poleTapSel_ == 0) ? HW{ 1, -1,  0,  0, 0 }
                            : (poleTapSel_ == 1) ? HW{ 1, -2,  1,  0, 0 }
                            : (poleTapSel_ == 2) ? HW{ 1, -3,  3, -1, 0 }
                            :                      HW{ 1, -4,  6, -4, 1 };
                auto cfgHp = [&] (LadderPoleMix& m, float c)
                { m.w0 = hw.w0; m.w1 = hw.w1; m.w2 = hw.w2; m.w3 = hw.w3; m.w4 = hw.w4;
                  m.outMakeup = kLadderHp24Makeup; m.setCoeffs (c, res01, fs); };
                cfgHp (ladderHpL_, cutHzL); cfgHp (ladderHpR_, cutHzR);
                preDrive_  = driveLin;
                postMakeup_= driveMakeup (driveLin);
                break;
            }
            case Type::DIODE_LP:
                diodeL_.outMakeup = kDiodeMakeup;
                diodeR_.outMakeup = kDiodeMakeup;
                diodeL_.setCoeffs (cutHzL, res01, fs);
                diodeR_.setCoeffs (cutHzR, res01, fs);
                preDrive_  = driveLin;
                // fb603 — RE-TRIM. The per-rate DC blocker gave the diode family back the 5.6 dB
                // of 20-125 Hz its 76 Hz corner was eating, so the passband measured +3.9 dB
                // against the LP24 reference's -0.1. kDiodeMakeup stays 13.0 — it is the DRIVE
                // into the soft clip, i.e. the grit — and the level comes off AFTER it.
                postMakeup_= driveMakeup (driveLin) * kDiodeBassTrim;
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
                // fb603 — OB-X is the MORPHING SEM (kObxMorph, and morph_ ADDS to it once a Var
                // knob exists), with its own harder resonance ceiling (90 vs the SEM detents' 60).
                // Before this it read morph_ == 0 and was byte-identical to SEM LP.
                svfL_.morph = juce::jlimit (0.0f, 1.0f, kObxMorph + morph_);
                svfR_.morph = svfL_.morph;
                setSvf (SvfMultimode::Output::SEM, 90.0f, cutHz, res01, driveLin, fs);
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
                combL_.dampScale = 1.0f; combR_.dampScale = 1.0f;
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
                ladderL_.poleMkFlat = mk; ladderR_.poleMkFlat = mk;
                ladderL_.tapBlend = 0.0f; ladderR_.tapBlend = 0.0f;
                ladderL_.setCoeffs (cutHzL, res01, fs);
                ladderR_.setCoeffs (cutHzR, res01, fs);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
                break;
            }
            case Type::GERMAN_LP:      // clean ZDF voicing (Serum "German LP" homage): tamed res, soft drive
                // fb603 — DE-DUPLICATE + POLES. At RES 0 this was byte-identical to LADDER_LP24
                // (its only difference was the 0.85 res scale, which does nothing at res 0), so
                // auditioning the two side by side with the resonance down heard ONE filter.
                // tapBlend 0.45 gives it a genuinely softer 18/24-hybrid knee — the "German"
                // voicing — which separates the two everywhere, resonance or not. And because it
                // runs the same 4-tap ladder core, SYN_FILTER*_POLES now works on it too (it was
                // hard-wired to tap 3); that is 1 more type on an axis that already exists.
                ladderL_.poleTap = poleTapSel_; ladderR_.poleTap = poleTapSel_;
                {
                    const float mkG = std::pow (kLadder12Makeup, (3 - poleTapSel_) * 0.5f);
                    ladderL_.poleMkFlat = mkG; ladderR_.poleMkFlat = mkG;
                }
                ladderL_.tapBlend = kGermanTapBlend; ladderR_.tapBlend = kGermanTapBlend;
                ladderL_.setCoeffs (cutHzL, res01 * 0.85f, fs);
                ladderR_.setCoeffs (cutHzR, res01 * 0.85f, fs);
                preDrive_ = 1.0f + (driveLin - 1.0f) * 0.4f;
                postMakeup_ = driveMakeup (preDrive_);
                break;
            case Type::GERMANIUM_LP:   // fuzzy-forward diode voicing (transistor grit)
                // fb603 — outMakeup was NEVER written here: this voicing inherited 13.0 if you had
                // just auditioned Diode LP and 1.0 (-22.28 dB) if you had not. Set it explicitly.
                // fb603 — DE-DUPLICATE. Once the 22.28 dB level leak was gone these three diode
                // voicings measured IDENTICAL to 0.24 dB: at the linear probe level they were the
                // same circuit differing only by how hard they hit the soft clip, which a low-level
                // noise curve cannot see. Each now has its own effective corner and resonance skew
                // — Germanium darker and more resonant, French brighter with a softer res onset.
                diodeL_.outMakeup = kDiodeMakeup; diodeR_.outMakeup = kDiodeMakeup;
                diodeL_.setCoeffs (cutHzL * 0.80f, juce::jmin (1.0f, res01 * 1.15f), fs);
                diodeR_.setCoeffs (cutHzR * 0.80f, juce::jmin (1.0f, res01 * 1.15f), fs);
                preDrive_ = driveLin * 1.8f; postMakeup_ = driveMakeup (driveLin) * 0.358f;   // fb603 — was 0.8 (+7.0 dB after the DC-blocker fix)
                break;
            case Type::FRENCH_LP:      // nonlinear-responding LP (Serum "French LP" homage)
                diodeL_.outMakeup = kDiodeMakeup; diodeR_.outMakeup = kDiodeMakeup;   // fb603 — was inherited (22.28 dB recall leak)
                diodeL_.setCoeffs (cutHzL * 1.30f, std::pow (res01, 0.7f) * 0.85f, fs);   // fb603 — own corner + res skew (was == Diode LP to 0.20 dB)
                diodeR_.setCoeffs (cutHzR * 1.30f, std::pow (res01, 0.7f) * 0.85f, fs);
                preDrive_ = driveLin * 1.3f; postMakeup_ = driveMakeup (driveLin) * 0.494f;   // fb603 — was 0.9 (+5.2 dB)
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
                          // fb603 — XPD_BP12 was {0,2,-2,0,0} = XPD_BP6's {0,1,-1,0,0} doubled:
                          // fb602 measured them the SAME CURVE +4.08 dB apart with 0.01 dB spread.
                          // {0,0,1,-1,0} = L^2(1-L) is the Xpander's real 3-POLE bandpass — 12 dB/oct
                          // below the centre, 6 dB/oct above — genuinely between BP6 and BP24.
                          : (type_ == Type::XPD_BP12)  ? W{ 0,  0,  1, -1, 0, 3.4f }
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
                setSvf24 (o, cutHz_, res01, driveLin, fs);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);   // fb603 — was 1/1 (DRV-INERT)
                break;
            }
            case Type::SVF_PEAK:
                setSvf (SvfMultimode::Output::Peak, 2000.0f, cutHz_, res01, driveLin, fs);
                preDrive_ = driveLin; postMakeup_ = 0.7f * driveMakeup (driveLin);   // fb603 — was 1/0.7 (DRV-INERT)
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
                preDrive_ = driveLin; postMakeup_ = kObxMakeup * driveMakeup (driveLin);   // fb603 — was 1/1 (DRV-INERT)
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
                preDrive_ = driveLin; postMakeup_ = 0.7f * driveMakeup (driveLin);   // fb603 — was 1/0.7 (all 10 DRV-INERT)
                break;
            }
            case Type::COMB_WIDE: case Type::COMB_OCTAVE: case Type::COMB_FIFTH:
            {
                combL_.dampScale = 1.0f; combR_.dampScale = 1.0f;
                // fb603 — these three retuned only the RIGHT channel, so in the LEFT channel (and
                // therefore in mono) they were BYTE-IDENTICAL to COMB+ and to each other: fb602
                // measured 0.00 dB max deviation on all three pairs. Split the interval
                // SYMMETRICALLY around the dialled pitch (L below, R above) so each type owns a
                // different comb spacing in BOTH channels and the interval is still what its name says.
                const float half = (type_ == Type::COMB_WIDE)   ? 1.012f      // ~20 cents apart
                                 : (type_ == Type::COMB_OCTAVE) ? 1.41421f    // an octave apart
                                 :                                1.22474f;   // a fifth apart
                combL_.mode = CombMode::Plus; combR_.mode = CombMode::Plus;
                combL_.setParams (juce::jmax (16.0f, cutHzL / half), res01, fs);
                combR_.setParams (juce::jmin (cutHzR * half, 18000.0f), res01, fs);
                preDrive_ = driveLin; postMakeup_ = kCombPlusMakeup;
                break;
            }
            case Type::KARPLUS_BRIGHT: case Type::KARPLUS_MUTE:
            {
                const float rr = (type_ == Type::KARPLUS_BRIGHT)
                               ? juce::jmin (1.0f, res01 * 1.15f + 0.08f) : res01 * 0.45f;
                combL_.mode = CombMode::Karplus; combR_.mode = CombMode::Karplus;
                // fb603 — the damping corner is what "Bright" and "Mute" actually mean.
                const float ds = (type_ == Type::KARPLUS_BRIGHT) ? 3.2f : 0.30f;
                combL_.dampScale = ds; combR_.dampScale = ds;
                combL_.setParams (cutHzL, rr, fs);
                combR_.setParams (cutHzR * 1.0015f, rr, fs);
                preDrive_ = driveLin;
                postMakeup_ = kCombKarplusMakeup * ((type_ == Type::KARPLUS_MUTE) ? 1.4f : 1.0f);
                break;
            }
            case Type::COMB_DAMP:
            {
                const float len = juce::jlimit (8.0f, 4790.0f, (float) fs / juce::jmax (20.0f, cutHz_));
                dampL_.setLen (len); dampR_.setLen (len * 1.007f + 1.0f);
                dampL_.fb = 0.5f + res01 * 0.47f;  dampR_.fb = dampL_.fb;
                dampL_.damp = 0.5f;                dampR_.damp = 0.5f;
                dampL_.sat = dampR_.sat = driveMix (driveLin);       // fb603 — drive in the recirculation
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
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
                    vapL_[i].setLen (base * kR[i]);
                    vapR_[i].setLen (base * kR[i] * 1.011f + 1.0f);
                    vapL_[i].g = 0.4f + res01 * 0.5f; vapR_[i].g = vapL_[i].g;
                    // fb603 — drive in the allpass node, but on the LAST stage only. One
                    // saturator already breaks the unity-magnitude allpass identity (which is the
                    // whole point); putting one in all four cost 8 serial tanh per sample = 56 ns.
                    vapL_[i].sat = vapR_[i].sat = (i == 3) ? driveMix (driveLin) : 0.0f;
                }
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
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
                // fb603 — an EQ's drive is its output stage (console/tape grammar): the band shaping
                // is linear by definition, so without a saturator DRV could only ever be the
                // measured "+12.0 dB, THD 0.00% -> 0.00%".
                satMix_ = driveMix (driveLin);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
                break;
            }
            case Type::LOW_EQ: case Type::HIGH_EQ: case Type::AIR:
            {
                const bool  hi  = (type_ != Type::LOW_EQ);
                const float fc2 = (type_ == Type::AIR) ? juce::jmax (cutHz_, 4000.0f) : cutHz_;
                const float g   = (type_ == Type::AIR) ? res01 * 15.0f : res01 * 24.0f - 12.0f;
                eqAL_.setShelf (fc2, g, hi, fs); eqAR_.setShelf (fc2, g, hi, fs);
                satMix_ = driveMix (driveLin);          // fb603 — post-EQ saturator (was VOLUME-ONLY +12.0 dB)
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
                break;
            }
            case Type::BAND_EQ:
                // NOTE (fb603, REPORT-ONLY — no behaviour change here): on BAND EQ the DRIVE knob is
                // secretly the bell's Q (0.7 -> 4.7), not a drive. That is a useful control but a
                // MISLABELLED one, and it is why this type measures "VOLUME-ONLY +0.1 dB". The Q
                // belongs on the Var knob when that lands; leaving the behaviour intact until then.
                eqAL_.setBell (cutHz_, res01 * 24.0f - 12.0f, 0.7f + drv01 * 4.0f, fs);
                eqAR_.setBell (cutHz_, res01 * 24.0f - 12.0f, 0.7f + drv01 * 4.0f, fs);
                satMix_ = 0.0f;
                preDrive_ = 1.0f; postMakeup_ = 1.0f;
                break;
            case Type::ADD_BASS:
            {
                // Serum's joke-but-useful: phase-rotated LP folded onto the dry with a touch of drive.
                vapL_[0].setLen ((float) (fs * 0.0008)); vapR_[0].setLen ((float) (fs * 0.0008) + 3.0f);
                vapL_[1].setLen ((float) (fs * 0.0019)); vapR_[1].setLen ((float) (fs * 0.0019) + 5.0f);
                vapL_[0].g = vapL_[1].g = vapR_[0].g = vapR_[1].g = 0.5f;
                vapL_[0].sat = vapL_[1].sat = vapR_[0].sat = vapR_[1].sat = 0.0f;   // fb603 — ADD BASS has its own drive stage
                setSvf (SvfMultimode::Output::LP, 60.0f, juce::jmax (60.0f, cutHz_),
                        0.15f + res01 * 0.3f, 1.0f, fs);
                preDrive_ = driveLin; postMakeup_ = 1.0f;
                break;
            }
            case Type::SAMPHOLD: case Type::SAMPHOLD_MINUS:
                shfxL_.minus = shfxR_.minus = (type_ == Type::SAMPHOLD_MINUS);
                shfxL_.setParams (juce::jmax (30.0f, cutHz_), res01, driveLin, fs);          // fb603 — res01/drive were never passed
                shfxR_.setParams (juce::jmax (30.0f, cutHz_) * 1.003f, res01, driveLin, fs);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
                break;
            case Type::SCREAM_LP: case Type::SCREAM_BP:
            {
                // fb603 — RUNAWAY. The loop was fastTanh(fb * (1 + 6*drv01)) * (0.30 + 0.65*res01)
                // around an SVF whose own peak gain reaches Q = 148: small-signal loop gain crossed
                // 1.0 at RES 0 / DRV 0.5 — more than half of BOTH knobs was past threshold — and the
                // level ladder measured +59.8 dB of gain on a -66 dBFS input (spread 49.5 dB).
                // Normalise the feedback by the resonant peak the SVF actually has, so the loop gain
                // is a TAPER that rises with RES to 0.95 and never reaches 1. DRIVE keeps its whole
                // job: it sets how hard the feedback path clips (the knee moves as 1/dGain), which
                // is where the scream lives — it just no longer multiplies the linear loop gain.
                // The two resonances MULTIPLY: total peak = Qsvf / (1 - loopGain). Budget them
                // together — the SVF keeps a modest Q (0.67..3.7) and the feedback loop carries the
                // rest, which is what a Steiner/scream feedback filter actually is. Engaging the
                // SVF's own drive taper as well puts the integrator rail in the loop at high DRV.
                const bool  isLp = (type_ == Type::SCREAM_LP);
                const float resU = 0.05f + 0.55f * juce::jlimit (0.0f, 1.0f, res01);
                setSvf (isLp ? SvfMultimode::Output::LP : SvfMultimode::Output::BP,
                        400.0f, cutHz_, resU, driveLin, fs);
                preDrive_ = driveLin; postMakeup_ = 0.8f * driveMakeup (driveLin);
                screamDrv_ = 1.0f + drv01 * 6.0f;
                // peak gain of the tap actually in the loop: ~Q for the LP, exactly 1 for the
                // NORMALISED BP (which returns k*v1).
                const float qPk = isLp ? juce::jmax (1.0f, 0.5f * std::pow (400.0f, resU)) : 1.0f;
                const float loopTarget = kScreamLoopMin
                                       + (kScreamLoopMax - kScreamLoopMin) * res01;
                screamFb_ = loopTarget / (screamDrv_ * qPk * postMakeup_);
                break;
            }
            case Type::WASP:
                // fb603 — the Wasp is a GRITTY filter, so its saturator is partly engaged at DRV 0
                // (satMix 0.55) and fully at DRV 1; the input gain rides the knob so the drive has
                // something to bite on. Before, preDrive_ was a fixed 1.2 and DRV moved nothing.
                setSvf (SvfMultimode::Output::LP, 200.0f, cutHz_, res01, driveLin * 2.2f, fs);
                preDrive_ = 1.2f * driveLin; postMakeup_ = 0.9f * driveMakeup (driveLin);
                break;
            case Type::MS20_LP:
                // fb603 — same fix, gentler voicing (the MS-20 is dirty at the top of the knob,
                // not at the bottom): satMix 0.29 at DRV 0 -> 0.96 at DRV 1.
                setSvf (SvfMultimode::Output::LP, 500.0f, cutHz_, std::pow (res01, 0.8f),
                        driveLin * 1.4f, fs);
                preDrive_ = driveLin; postMakeup_ = driveMakeup (driveLin);
                break;
            case Type::POLIVOKS:
                diodeL_.outMakeup = kDiodeMakeup; diodeR_.outMakeup = kDiodeMakeup;   // fb603 — was inherited (22.28 dB recall leak)
                // fb603 — own corner too. Level-matching the diode family left Polivoks only
                // 1.72 dB from Diode LP anywhere in 20 Hz-18 kHz (its 1.25 res skew is all it had,
                // and that clamps to the same value by RES 0.8). The real Polivoks is the bright,
                // aggressive one of the family, so it gets the brightest effective corner.
                diodeL_.setCoeffs (cutHzL * 1.60f, juce::jmin (1.0f, res01 * 1.25f), fs);
                diodeR_.setCoeffs (cutHzR * 1.60f, juce::jmin (1.0f, res01 * 1.25f), fs);
                preDrive_ = driveLin * 2.5f; postMakeup_ = driveMakeup (driveLin) * 0.257f;   // fb603 — was 0.7 (+8.7 dB)
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
                // fb603 — RADIO WAS SILENT AT RES 1.0 AT EVERY LEVEL AND DRIVE (-256..-316 dB across
                // the whole grid). Two BPs in series at Q 1000 and 218 leave a band so thin that the
                // signal reaching the bit-crusher never crossed half an LSB, so the quantiser emitted
                // a constant zero. Two fixes, both physical: an AM-radio IF filter is Q ~ 20-50, not
                // 1000 (cap qMax at 40 and offset the second stage so the pair is a BAND, not a
                // whistle); and a radio has an AGC in front of its converter, so the crusher gets a
                // fixed pre-gain that keeps the signal above one step at any input level.
                // The band also has to STAY a radio band: with the raw 20 Hz-20 kHz CUT knob, the
                // shipped default of 20 kHz put both bandpasses above the programme material and a
                // fresh patch was silent again. Remap CUT to 150 Hz-8 kHz (an AM channel) — CUT
                // still owns the decimation rate above that, so no part of the knob is dead.
                const float cut01 = juce::jlimit (0.0f, 1.0f,
                    std::log (juce::jmax (20.0f, cutHz_) / 20.0f) / std::log (1000.0f));
                const float rc   = 150.0f * std::pow (8000.0f / 150.0f, cut01);
                const float rq   = 0.35f + 0.50f * res01;
                const float sMul = std::exp2 (spread_ * kSpreadSemis / 12.0f);
                setSvf (SvfMultimode::Output::BP, 40.0f, rc, rq, 1.0f, fs);
                svf2L_.qMax = 40.0f; svf2R_.qMax = 40.0f;
                svf2L_.out = SvfMultimode::Output::BP; svf2R_.out = SvfMultimode::Output::BP;
                svf2L_.setCoeffs (rc * 1.35f / sMul, rq * 0.8f, fs);
                svf2R_.setCoeffs (rc * 1.35f * sMul, rq * 0.8f, fs);
                svf2L_.setDrive (1.0f); svf2R_.setDrive (1.0f);
                crushL_.setParams (juce::jmax (2000.0f, cutHz_), kRadioBits, driveLin * kRadioAgc, fs);
                crushR_.setParams (juce::jmax (2000.0f, cutHz_), kRadioBits, driveLin * kRadioAgc, fs);
                preDrive_ = 1.0f; postMakeup_ = 0.35f;
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
                // fb603 — preDrive_ was missing here, so DRV only ever applied postMakeup_
                // (measured -12.0 dB of pure volume). Both halves of the grammar now apply.
                l = svf2L_.process (svfL_.process (l * preDrive_)) * postMakeup_;
                r = svf2R_.process (svfR_.process (r * preDrive_)) * postMakeup_;
                break;
            case Type::MULTI_LH: case Type::MULTI_LB: case Type::MULTI_LN: case Type::MULTI_HB:
            case Type::MULTI_HN: case Type::MULTI_BB: case Type::MULTI_BN: case Type::MULTI_PP:
            case Type::MULTI_NN: case Type::MULTI_PH:
            {
                const float li = l * preDrive_, ri = r * preDrive_;   // fb603 — preDrive_ was missing (DRV was -12 dB of volume)
                l = (svfL_.process (li) + svf2L_.process (li)) * postMakeup_;
                r = (svfR_.process (ri) + svf2R_.process (ri)) * postMakeup_;
                break;
            }
            case Type::COMB_DAMP:
            {
                const float li = l * preDrive_, ri = r * preDrive_;
                l = 0.5f * (li + dampL_.process (li)) * postMakeup_;   // fb603 — postMakeup_ was dropped (DRV was +24 dB of volume)
                r = 0.5f * (ri + dampR_.process (ri)) * postMakeup_;
                break;
            }
            case Type::DIFFUSOR:
            {
                float v = l * preDrive_; for (int i = 0; i < 4; ++i) v = vapL_[i].process (v); l = v * postMakeup_;   // fb603 — postMakeup_ was dropped
                v = r * preDrive_;       for (int i = 0; i < 4; ++i) v = vapR_[i].process (v); r = v * postMakeup_;
                break;
            }
            case Type::TILT:
                l = eqBL_.process (eqAL_.process (driveSat (l * preDrive_, satMix_))) * postMakeup_;
                r = eqBR_.process (eqAR_.process (driveSat (r * preDrive_, satMix_))) * postMakeup_;
                break;
            case Type::LOW_EQ: case Type::HIGH_EQ: case Type::AIR: case Type::BAND_EQ:
                l = eqAL_.process (driveSat (l * preDrive_, satMix_)) * postMakeup_;   // fb603 — satMix_ is 0 for BAND_EQ (bit-exact bypass)
                r = eqAR_.process (driveSat (r * preDrive_, satMix_)) * postMakeup_;
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
                // fb603 — the loop coefficients are now computed ONCE in setParams (they need the
                // SVF's peak gain, which is a pow()) instead of per sample from res01_/drv01_.
                float in = l * preDrive_ + fastTanh (fbScrL_ * screamDrv_) * screamFb_;
                l = svfL_.process (in) * postMakeup_; fbScrL_ = l;
                in = r * preDrive_ + fastTanh (fbScrR_ * screamDrv_) * screamFb_;
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
        // fb390 — ALL THREE Karplus voices pluck, not just the first. The guard read
        // `type_ == Type::KARPLUS` alone, so KARPLUS_BRIGHT and KARPLUS_MUTE were silent no-ops:
        // a character literally named "Pluck" that could not pluck. All three run the same
        // CombCore behind the same façade, so the exciter was always correct for them — only the
        // guard was too narrow. (Flagged twice before it was fixed; it touches the synth filter
        // too, which is why it waited for a deliberate call rather than being slipped in.)
        if (type_ == Type::KARPLUS || type_ == Type::KARPLUS_BRIGHT || type_ == Type::KARPLUS_MUTE)
        { combL_.excite (level); combR_.excite (level); }
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

    /** fb603 — THE 24 dB CASCADE, re-derived.
     *
     *  It used to be svf (Q up to 1000, straight off the RES knob) -> svf2 (res01*0.5, Q up
     *  to 22): the composite peak measured +50.7 / +51.5 dB and the 4 s stress sweep hit a
     *  PEAK OF 35.21 on a 0.087 input = +31 dBFS per voice, with no limiter anywhere. And at
     *  RES 0 both sections sat at Q = 0.5 — critically damped, so the pair measured -24.1 dB
     *  at 16 kHz on a WIDE-OPEN (20 kHz) patch.
     *
     *  Now each section gets its OWN Q: the 4th-order Butterworth pair (0.5412 / 1.3066)
     *  scaled by a common factor kSvf24QScale^res01. RES 0 is therefore maximally flat, and
     *  the composite peak is 0.5412*1.3066*qq^2 -> kSvf24QScale^2 * 0.707 at RES 1, i.e. a
     *  resonance the user actually asked for rather than the product of two independent Qs.
     *  ONE saturator in the chain (the first section) — two would double the grit for free. */
    void setSvf24 (SvfMultimode::Output o, float cutHz, float res01,
                   float driveLin, double fs) noexcept
    {
        const float sMul = std::exp2 (spread_ * kSpreadSemis / 12.0f);
        const float qq   = std::pow (kSvf24QScale, juce::jlimit (0.0f, 1.0f, res01));
        svfL_.out  = o; svfR_.out  = o;
        svf2L_.out = o; svf2R_.out = o;
        svfL_ .setCoeffsQ (cutHz / sMul, kSvfButterA * qq, fs);
        svfR_ .setCoeffsQ (cutHz * sMul, kSvfButterA * qq, fs);
        svf2L_.setCoeffsQ (cutHz / sMul, kSvfButterB * qq, fs);
        svf2R_.setCoeffsQ (cutHz * sMul, kSvfButterB * qq, fs);
        svfL_ .setDrive (driveLin); svfR_ .setDrive (driveLin);
        svf2L_.setDrive (1.0f);     svf2R_.setDrive (1.0f);
    }

    // Per-mode level-match constants (measured offline against LP24 ref).
    static constexpr float kLadder12Makeup  = 0.99f;  // measured -1.94 dB vs ref -2.06
    static constexpr float kLadderHp24Makeup= 1.05f;  // measured -2.50 dB -> match
    static constexpr float kDiodeMakeup     = 13.0f;  // 0.5-scaling deficit, like the 303
    static constexpr float kDiodeBassTrim   = 0.638f; // fb603 — -3.9 dB: level-match after the DC-blocker fix restored the lows
    static constexpr float kObxMakeup       = 1.0f;   // SEM already ~unity passband
    // fb603 — 4th-order Butterworth Q pair + the resonance scale that puts the cascade peak
    // at ~+29 dB (SVF Peak measures +30 dB at stress peak 2.27) instead of the old +50.7 dB.
    static constexpr float kSvfButterA      = 0.54120f;
    static constexpr float kSvfButterB      = 1.30656f;
    static constexpr float kSvf24QScale     = 6.32f;
    // fb603 — OB-X is a MORPHING SEM, not a second copy of SEM LP. fb602 measured them
    // identical to 0.00 dB because FilterSlot::setMorph() had zero callers, so morph_ was
    // permanently 0.0 and OB-X was a plain lowpass. A real OB-X 2-pole leaks a little
    // highpass past its corner; morph 0.07 puts the stopband floor at -17.1 dB — still
    // unambiguously a lowpass, and 14.9 dB clear of SEM LP everywhere above the corner.
    static constexpr float kObxMorph        = 0.07f;
    // fb603 — German LP's softer 18/24-hybrid knee (the de-dup against LADDER_LP24 at RES 0).
    static constexpr float kGermanTapBlend  = 0.45f;
    // fb603 — SCREAM loop-gain taper: small-signal loop gain rises with RES to 0.95, never to 1.0.
    static constexpr float kScreamLoopMin   = 0.30f;
    static constexpr float kScreamLoopMax   = 0.92f;
    // fb603 — RADIO: 8-bit converter behind a fixed AGC (see Type::RADIO).
    static constexpr float kRadioBits       = 0.3333f;   // -> 4 + 0.3333*12 = 8 bits
    static constexpr float kRadioAgc        = 10.0f;

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
    float          screamDrv_ = 1.0f, screamFb_ = 0.0f;   // fb603 — SCREAM loop taper (computed in setParams)
    float          satMix_ = 0.0f;                        // fb603 — post-EQ drive saturator blend
};

} // namespace filters
} // namespace tw
