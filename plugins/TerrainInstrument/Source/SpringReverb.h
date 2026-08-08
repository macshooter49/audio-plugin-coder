#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SpringReverb — the SPRING reverb type (fb284). Clean-room Välimäki–Parker–Abel
// parametric spring (REVERB-BUILD-BIBLE Spring section). NOT an FDN/tank: the sound
// IS mechanical dispersion — in a helical spring, low frequencies travel slower than
// highs, so a transient smears into the iconic descending "boinggg" chirp.
//
//   per spring (mono, panned): drive-saturated input + feedback →
//     [DISPERSION CHAIN: N first-order allpasses, NEGATIVE coeff → lows delayed most
//      → the descending chirp; more stages / stronger coeff = bigger boing] →
//     transition shelf (how high the zing reaches) → in-loop damping LP →
//     noise-modulated feedback delay (Td = Tension; the ±leaky-noise jitter is
//     NON-OPTIONAL — it blurs the comb into a tail instead of a metallic ring) →
//     ×feedback gain (negative; Decay/Dwell) back into the loop.
//   Multiple DETUNED parallel springs (Springs dropdown) → interleaved boings, denser.
//   Germanium input drive (the tube/reverb-amp grit), a low tank-body resonance,
//   Shake (turbulence + rattle), Freeze (infinite ring).
//
// Dispersion is a cascade of PLAIN first-order allpasses (K=1) on purpose: the group
// delay is MONOTONIC (max at DC → min at Nyquist) = a clean descending chirp with no
// stretched-allpass periodicity artifacts. Cost is stages, but each stage is 2 mults.
// All click-free (per-sample ramps; Td glides; sparkle/tilt gains ramp) + stable
// (allpasses are unity-gain → loop gain = |feedback|·|damping| < 1) + denormal-flushed.
// PURE C++ (no JUCE): offline-validates standalone AND drops into the voice path.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>

class SpringReverb
{
public:
    static constexpr int MAXSPRINGS = 6;
    static constexpr int NAP        = 180;   // first-order allpasses per dispersion chain (the chirp)

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        // feedback delay per spring: max Td ~130 ms + mod + margin
        { int need = (int) std::ceil (0.14f * fs) + 128; int sz = 1; while (sz < need) sz <<= 1; fbMask = sz - 1;
          for (int s = 0; s < MAXSPRINGS; ++s) { fb[s].assign ((size_t) sz, 0.0f); fbWr[s] = 0; } }
        { int need = (int) std::ceil (0.16f * fs) + 8; int sz = 1; while (sz < need) sz <<= 1; pre.assign ((size_t) sz, 0.0f); preMask = sz - 1; preWr = 0; }
        smth = 1.0f - std::exp (-1.0f / (0.02f * fs));         // ~20 ms per-sample smoothing
        dcR  = 1.0f - (126.0f / fs);
        tiltCoef  = 1.0f - std::exp (-2.0f * PI * 1400.0f / fs);   // Tone-tilt crossover ~1.4 kHz
        transCoef = 1.0f - std::exp (-2.0f * PI * 4500.0f / fs);   // transition high-shelf ~4.5 kHz
        // tank-body resonance: RBJ peaking @ ~95 Hz, gentle, adds the physical "boom"
        { float f0 = 95.0f, Q = 0.9f, A = 1.7f, w0 = 2.0f*PI*f0/fs, cw = std::cos(w0), sw = std::sin(w0), al = sw/(2.0f*Q);
          float a0 = 1.0f + al/A;
          tb0 = (1.0f + al*A)/a0; tb1 = (-2.0f*cw)/a0; tb2 = (1.0f - al*A)/a0; ta1 = (-2.0f*cw)/a0; ta2 = (1.0f - al/A)/a0; }
        // detune + pan spread the springs
        for (int s = 0; s < MAXSPRINGS; ++s) { detune[s] = 1.0f + DET[s]; rng[s] = 0x1234567u + 2654435761u * (uint32_t) (s + 1); }
        reset();
        primed = false;
        updateCoefficients();
    }

    void reset()
    {
        for (int s = 0; s < MAXSPRINGS; ++s)
        {
            std::fill (fb[s].begin(), fb[s].end(), 0.0f); fbWr[s] = 0; fbState[s] = 0.0f; dampZ[s] = 0.0f;
            modLp[s] = 0.0f;
        }
        transShL = transShR = 0.0f;
        for (int i = 0; i < MAXSPRINGS * NAP; ++i) { apX[i] = apY[i] = 0.0f; }
        std::fill (pre.begin(), pre.end(), 0.0f);
        inLpZ = lcZ = 0.0f; dcxL = dcyL = dcxR = dcyR = 0.0f; tbx1 = tbx2 = tby1 = tby2 = 0.0f; tiltLpL = tiltLpR = 0.0f;
    }

    // ── params (0..1 unless noted) — mapped from the shared reverb slots ──
    void setTension     (float v) { tension = clamp01 (v); }   // SIZE  → echo spacing + boing pitch
    void setDecay       (float v) { decay   = clamp01 (v); }   // DECAY → dwell / T60
    void setTone        (float v) { tone    = clamp01 (v); }   // TONE  → bright↔dark
    void setPreDelayMs  (float ms){ preMs   = clampf (ms, 0.0f, 150.0f); }
    void setTransition  (float v) { trans   = clamp01 (v); }   // DIFFUSE  → chirp HF extent (the zing)
    void setShake       (float v) { shake   = clamp01 (v); }   // MODDEPTH → turbulence / rattle
    void setDispersion  (float v) { disp    = clamp01 (v); }   // MODRATE  → the BOING amount
    void setDamping     (float v) { damp    = clamp01 (v); }   // HIDAMP   → HF loss in the loop
    void setDrive       (float v) { drive   = clamp01 (v); }   // LOWDECAY → germanium input saturation
    void setLowCutHz    (float hz){ lowCut  = clampf (hz, 20.0f, 1000.0f); }
    void setWidth       (float v) { width   = clamp01 (v); }
    void setCharacter   (int c)   { character = c < 0 ? 0 : (c > 7 ? 7 : c); }
    void setSprings     (int s)   { springsSel = s < 0 ? 0 : (s > 5 ? 5 : s); }   // 0..5 → 1..6 springs
    void setModEnabled  (bool on) { modOn   = on; }            // Shake gate
    void setFreeze      (bool f)  { freezeOn = f; }
    void setMix         (float v) { mixExt  = clamp01 (v); }

    void updateCoefficients()
    {
        const CharBias cb = CHAR[character];
        static constexpr int SPR[6] = { 1, 2, 3, 4, 5, 6 };
        nSprings   = SPR[springsSel];
        springNorm = 1.0f / std::sqrt ((float) nSprings);

        // Dispersion → allpass coeff (NEGATIVE = lows delayed = descending boing). Char scales it.
        float dispEff = clamp01 (disp * cb.dispMul);
        apCoefT = -(0.03f + 0.70f * dispEff);                         // ~0 (clean) … −0.73 (huge boing) — night-and-day
        // Tension → feedback delay Td (echo spacing / boing pitch), 12…120 ms. Char scales.
        float tenMs = (12.0f + 108.0f * tension) * cb.tensionMul;
        TdT = clampf (tenMs * 0.001f * fs, 4.0f, (float) fbMask - 64.0f);
        // Decay/Dwell → |feedback| (toward self-oscillation). Char scales tail length.
        float g = 0.30f + 0.66f * std::pow (decay, 0.85f);
        g = clampf (g * cb.decayMul, 0.10f, 0.96f);
        fbGainT = -g;                                                 // negative feedback (spring phase)
        // in-loop damping LP: Damping + Char (Tone also darkens via input LP below)
        float dampEff = clamp01 (damp * cb.dampMul + cb.dampAdd);
        dampT = 0.04f + 0.92f * dampEff;
        // Tone → input band-limit (bright↔dark) + a BOLD output tilt (one-pole alone barely moves the
        // centroid — same lesson as fb283 Plate). Identity at toneEff 0.5.
        float toneEff = clamp01 (tone * cb.toneMul + cb.toneAdd);
        inLpT = std::exp (-2.0f * PI * (1200.0f * std::pow (15.0f, toneEff)) / fs);
        float tilt = (toneEff - 0.5f) * 2.0f;                         // −1 dark .. +1 bright
        hiGainT = std::pow (4.5f, tilt);                              // ±~13 dB high tilt
        loGainT = std::pow (2.0f, -tilt);
        lcT   = std::exp (-2.0f * PI * lowCut / fs);
        // Transition → chirp HF-extent shelf gain (how high the metallic zing reaches)
        transGainT = 0.20f + 3.6f * clamp01 (trans);                  // <1 = dark boing, >1 = brittle zing
        // Drive → germanium saturation amount
        driveT = clamp01 (drive + cb.driveAdd) * 7.0f;               // pre-gain into tanh
        // Shake → mod excursion + SPEED (a slow deep wobble barely smears; turbulence must be faster)
        float shk = modOn ? clamp01 (shake + cb.shakeAdd) : 0.0f;
        modAmpT   = 4.0f + 64.0f * shk;                             // samples of leaky-noise jitter
        modRateT  = 0.0012f + 0.055f * shk;                        // random-walk speed (still → violent turbulence)
        rattleT   = shk * shk * cb.rattleMul;                      // injected rattle noise level
        widthT    = clamp01 (width + cb.widthAdd);
        preSampT  = preMs * 0.001f * fs;
        freezeTgt = freezeOn ? 1.0f : 0.0f;
        if (! primed)
        {
            apCoefC = apCoefT; TdC = TdT; fbGainC = fbGainT; dampC = dampT; inLpC = inLpT; lcC = lcT;
            transGainC = transGainT; driveC = driveT; modAmpC = modAmpT; modRateC = modRateT; rattleC = rattleT;
            widthC = widthT; preSampC = preSampT; freezeCur = freezeTgt; hiGainC = hiGainT; loGainC = loGainT;
            primed = true;
        }
    }

    void process (float* L, float* R, int n)
    {
        const float dgm = std::cos (0.5f * PI * mixExt), wgm = std::sin (0.5f * PI * mixExt);
        for (int s = 0; s < n; ++s) { float wl, wr; processSample (L[s], R[s], wl, wr); L[s] = dgm * L[s] + wgm * wl; R[s] = dgm * R[s] + wgm * wr; }
    }

    inline void processSample (float inL, float inR, float& wetL, float& wetR)
    {
        // ── per-sample smoothing (click-free) ──
        apCoefC    += (apCoefT    - apCoefC)    * smth;
        TdC        += (TdT        - TdC)        * smth;
        fbGainC    += (fbGainT    - fbGainC)    * smth;
        dampC      += (dampT      - dampC)      * smth;
        inLpC      += (inLpT      - inLpC)      * smth;
        lcC        += (lcT        - lcC)        * smth;
        transGainC += (transGainT - transGainC) * smth;
        driveC     += (driveT     - driveC)     * smth;
        modAmpC    += (modAmpT    - modAmpC)    * smth;
        modRateC   += (modRateT   - modRateC)   * smth;
        rattleC    += (rattleT    - rattleC)    * smth;
        widthC     += (widthT     - widthC)     * smth;
        preSampC   += (preSampT   - preSampC)   * smth;
        hiGainC    += (hiGainT    - hiGainC)     * smth;
        loGainC    += (loGainT    - loGainC)     * smth;
        freezeCur  += (freezeTgt  - freezeCur)  * smth;
        const float fbG = fbGainC + (FREEZE_G * (fbGainC < 0 ? -1.0f : 1.0f) - fbGainC) * freezeCur;  // freeze → |g|→~1
        const float inG = 1.0f - freezeCur;                            // freeze cuts input → infinite ring

        // ── shared input: pre-delay → germanium drive → low-cut → tone band-limit ──
        float x = 0.5f * (inL + inR);
        pre[(size_t) preWr] = x; x = readFrac (pre, preMask, preWr, preSampC); preWr = (preWr + 1) & preMask;
        { float dpre = 1.0f + driveC; float xd = std::tanh (x * dpre); x = xd * (0.5f + 0.5f / dpre); }  // driver saturation, level-comp
        lcZ   = (1.0f - lcC)   * x + lcC   * lcZ;   x = x - lcZ;
        inLpZ = (1.0f - inLpC) * x + inLpC * inLpZ; x = inLpZ;
        const float drv = x * inG;

        // ── parallel detuned springs ──
        float sumL = 0.0f, sumR = 0.0f;
        for (int s = 0; s < nSprings; ++s)
        {
            float u = drv + fbState[s];
            // rattle excitation (Shake) — a touch of filtered noise into the loop
            if (rattleC > 1.0e-5f) u += rattleC * whiteNoise (rng[s]);
            // DISPERSION CHAIN — N first-order allpasses, coeff apCoefC (negative → descending boing)
            float* AX = &apX[s * NAP]; float* AY = &apY[s * NAP]; const float a = apCoefC;
            for (int i = 0; i < NAP; ++i)
            {
                float y = a * u + AX[i] - a * AY[i];
                AX[i] = flush (u); AY[i] = flush (y); u = y;
            }
            // in-loop damping LP (highs die first, like a real spring)
            dampZ[s] = (1.0f - dampC) * u + dampC * dampZ[s]; u = dampZ[s];
            // feedback delay Td·detune + leaky-noise jitter (blurs the comb into a tail)
            fb[s][(size_t) fbWr[s]] = flush (u);
            modLp[s] += (whiteNoise (rng[s]) * modAmpC - modLp[s]) * modRateC;   // leaky random walk (Shake speed)
            float rd = TdC * detune[s] + modLp[s];
            float out = readFrac (fb[s], fbMask, fbWr[s], rd);
            fbWr[s] = (fbWr[s] + 1) & fbMask;
            fbState[s] = fbG * out;
            // pan across the field (Width decorrelates the springs L/R)
            float pan = (nSprings > 1) ? PAN[s] * widthC : 0.0f;
            sumL += out * (0.5f - 0.5f * pan);
            sumR += out * (0.5f + 0.5f * pan);
        }
        sumL *= springNorm; sumR *= springNorm;

        // tank-body resonance (low hump) on the mono sum, added back for the physical "boom"
        float m = 0.5f * (sumL + sumR);
        float tb = tb0 * m + tb1 * tbx1 + tb2 * tbx2 - ta1 * tby1 - ta2 * tby2;
        tbx2 = tbx1; tbx1 = m; tby2 = tby1; tby1 = flush (tb);
        float body = 0.5f * (tb - m);   // the resonant lift only
        sumL += body; sumR += body;

        // bold Tone brightness tilt (one-pole split ~1.4 kHz, identity at tone 0.5) — click-free, stable
        tiltLpL += (sumL - tiltLpL) * tiltCoef; { float hi = sumL - tiltLpL; sumL = tiltLpL * loGainC + hi * hiGainC; }
        tiltLpR += (sumR - tiltLpR) * tiltCoef; { float hi = sumR - tiltLpR; sumR = tiltLpR * loGainC + hi * hiGainC; }
        // Transition high-shelf (~4.5 kHz, OUTPUT so it may boost freely = stable): the metallic "zing"
        // extent — how high the boing reaches. Distinct band from Tone's broad tilt.
        transShL += (sumL - transShL) * transCoef; { float hi = sumL - transShL; sumL = transShL + hi * transGainC; }
        transShR += (sumR - transShR) * transCoef; { float hi = sumR - transShR; sumR = transShR + hi * transGainC; }

        // DC block
        float ol = sumL - dcxL + dcR * dcyL; dcxL = sumL; dcyL = flush (ol);
        float orr= sumR - dcxR + dcR * dcyR; dcxR = sumR; dcyR = flush (orr);
        wetL = dcyL; wetR = dcyR;
    }

private:
    static inline float clamp01 (float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    static inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static inline float flush (float v) { return (v > -1e-20f && v < 1e-20f) ? 0.0f : v; }
    static inline float readFrac (const std::vector<float>& buf, int mask, int wr, float d)
    {
        if (d < 1.0f) d = 1.0f; float mx = (float) mask - 2.0f; if (d > mx) d = mx;
        int di = (int) d; float fr = d - (float) di;
        float a = buf[(size_t) ((wr - di) & mask)];
        float b = buf[(size_t) ((wr - di - 1) & mask)];
        return a + (b - a) * fr;
    }
    static inline float whiteNoise (uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) ((s >> 8) & 0xFFFFFF) * (1.0f / 8388607.5f) - 1.0f; }

    static constexpr float PI = 3.14159265358979f;
    static constexpr float FREEZE_G = 0.9997f;         // freeze loop gain (near-infinite, stable)
    static constexpr float TR_COEF  = 0.72f;           // transition-shelf one-pole (~2 kHz split)
    static constexpr float DET[MAXSPRINGS] = { 0.000f, 0.092f, -0.071f, 0.138f, -0.115f, 0.052f };  // per-spring detune
    static constexpr float PAN[MAXSPRINGS] = { -0.9f, 0.9f, -0.45f, 0.45f, -0.7f, 0.7f };            // stereo spread
    // Spring Character voicings (8): dispMul, tensionMul, dampMul, dampAdd, driveAdd, decayMul, toneMul, toneAdd, shakeAdd, rattleMul, widthAdd.
    struct CharBias { float dispMul, tensionMul, dampMul, dampAdd, driveAdd, decayMul, toneMul, toneAdd, shakeAdd, rattleMul, widthAdd; };
    static constexpr CharBias CHAR[8] = {
        /* Surf      */ { 1.15f, 1.20f, 1.00f, 0.00f, 0.10f, 1.10f, 1.00f, 0.05f, 0.05f, 0.010f,  0.05f },  // bold lows, big drippy boings
        /* Amp       */ { 0.95f, 1.00f, 1.05f, 0.05f, 0.15f, 1.05f, 1.00f, 0.00f, 0.00f, 0.008f,  0.00f },  // denser/smoother
        /* Studio    */ { 0.75f, 0.95f, 1.15f, 0.10f, 0.00f, 1.00f, 1.05f, 0.00f, 0.00f, 0.004f, -0.05f },  // lush/refined, minimal ring
        /* Cheap     */ { 1.20f, 0.55f, 0.80f, 0.00f, 0.20f, 0.80f, 1.30f, 0.10f, 0.20f, 0.028f,  0.05f },  // fast bright metallic drip, rattly
        /* Outboard  */ { 1.00f, 1.05f, 1.10f, 0.05f, 0.55f, 1.05f, 0.70f, 0.00f, 0.05f, 0.012f,  0.00f },  // germanium-warmed, compressed
        /* Long Pipe */ { 1.05f, 1.55f, 1.05f, 0.05f, 0.10f, 1.35f, 0.90f, 0.00f, 0.15f, 0.014f,  0.15f },  // cavernous modulated wash
        /* Dub       */ { 0.90f, 1.25f, 1.30f, 0.22f, 0.25f, 1.25f, 0.45f, 0.00f, 0.05f, 0.010f,  0.05f },  // dark, heavy, dubby
        /* Lo-Fi     */ { 1.45f, 1.30f, 0.85f, 0.00f, 0.35f, 1.10f, 1.10f, 0.05f, 0.35f, 0.040f,  0.10f },  // extreme dispersion, cartoon boing
    };

    float fs = 48000.0f, smth = 0.001f, dcR = 0.999f;
    bool  primed = false;
    std::vector<float> fb[MAXSPRINGS]; int fbMask = 0, fbWr[MAXSPRINGS] = {0};
    std::vector<float> pre; int preMask = 0, preWr = 0;
    float apX[MAXSPRINGS * NAP] = {0}, apY[MAXSPRINGS * NAP] = {0};   // dispersion allpass state
    float fbState[MAXSPRINGS] = {0}, dampZ[MAXSPRINGS] = {0}, modLp[MAXSPRINGS] = {0};
    float transShL = 0, transShR = 0, transCoef = 0.4f;   // fb284 — output transition high-shelf state
    float detune[MAXSPRINGS] = {1,1,1,1,1,1}; uint32_t rng[MAXSPRINGS] = {0};
    float inLpZ = 0, lcZ = 0, dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0;
    float tb0=0, tb1=0, tb2=0, ta1=0, ta2=0, tbx1=0, tbx2=0, tby1=0, tby2=0;   // tank-body biquad
    // targets + ramped current
    float apCoefT=-0.4f, apCoefC=-0.4f, TdT=1000, TdC=1000, fbGainT=-0.7f, fbGainC=-0.7f, dampT=0.5f, dampC=0.5f;
    float inLpT=0, inLpC=0, lcT=0, lcC=0, transGainT=1, transGainC=1, driveT=0, driveC=0, modAmpT=6, modAmpC=6;
    float modRateT=0.0012f, modRateC=0.0012f;
    float rattleT=0, rattleC=0, widthT=0.8f, widthC=0.8f, preSampT=0, preSampC=0, freezeCur=0, freezeTgt=0;
    float hiGainT=1, hiGainC=1, loGainT=1, loGainC=1, tiltCoef=0.3f, tiltLpL=0, tiltLpR=0;   // fb284 — bold Tone tilt
    float springNorm = 1.0f, mixExt = 0.3f;
    int   character=0, springsSel=1, nSprings=2;
    bool  modOn=true, freezeOn=false;
    float tension=0.5f, decay=0.5f, tone=0.5f, preMs=6.f, trans=0.5f, shake=0.2f, disp=0.5f, damp=0.4f, drive=0.2f, lowCut=20.f, width=0.85f;
};
