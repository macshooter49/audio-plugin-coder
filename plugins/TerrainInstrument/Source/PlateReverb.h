#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PlateReverb — the PLATE reverb type (fb282). The FIRST non-FDN engine: a faithful
// Dattorro (1997) plate — a single figure-8 recirculating "tank", NOT parallel lines.
//   input → pre-delay → bandwidth LP + low-cut → 4 series input-diffusion allpasses
//   → TANK (figure-8, cross-coupled):
//       each half = modulated allpass (decay-diffusion 1) → delay → damping LP
//                   → allpass (decay-diffusion 2) → delay → (×decay) → other half
//   → 7 decorrelated OUTPUT TAPS per channel (Dattorro's tap set) = instant dense,
//     bright, smooth plate sheen (no early reflections — the opposite of a Room).
// Enhancements over textbook Dattorro (all click-free — fractional reads + per-sample
// glide, like the Hall/Room engines):
//   • Dispersion (fb283 — now AUDIBLE) = allpass shimmer/boing (phase) + a METALLIC SPARKLE:
//     a ramped RBJ band-pass resonance whose gain rides the knob → a bright metallic "zing"
//     on all material (the fb282 pure-allpass version was magnitude-flat = inaudible). Out of loop.
//   • Character (8) plate voicings + Material (6) metals — wide, archetypal signatures
//     (brightness+decay+sparkle each distinct), read out via a bold ±13 dB brightness TILT.
//   • Size glides all tank lengths + tap offsets; Bass Decay low-shelf; Mod; Freeze.
// PURE C++ (no JUCE): offline-validates standalone AND drops into the voice path.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>

class PlateReverb
{
public:
    static constexpr int NID = 4;    // input diffusers
    static constexpr int NDISP = 6;  // in-loop dispersion allpasses per half (metallic tail)
    static constexpr int NDO = 8;    // output dispersion allpasses per channel (direct metallic chirp)

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        scl = fs / 29761.0f;   // Dattorro's original SR
        alloc (dif1L, dif1Lmask, 672);  alloc (dif1R, dif1Rmask, 908);
        alloc (del1L, del1Lmask, 4453); alloc (del1R, del1Rmask, 4217);
        alloc (dif2L, dif2Lmask, 2656); alloc (dif2R, dif2Rmask, 2656);   // sized to the larger (R) + taps
        alloc (del2L, del2Lmask, 3720); alloc (del2R, del2Rmask, 3720);
        for (int i = 0; i < NID; ++i) { alloc (id[i], idMask[i], (int) idLen[i]); idWr[i] = 0; idDelay[i] = std::max (4, (int) std::lround (idLen[i] * scl)); }
        { int need = (int) std::ceil (0.12f * fs) + 8; int sz = 1; while (sz < need) sz <<= 1; pre.assign ((size_t) sz, 0.0f); preMask = sz - 1; preWr = 0; }
        dif1Lwr = dif1Rwr = del1Lwr = del1Rwr = dif2Lwr = dif2Rwr = del2Lwr = del2Rwr = 0;
        smth = 1.0f - std::exp (-1.0f / (0.015f * fs));
        bassCoef = std::exp (-2.0f * PI * 350.0f / fs);
        dcR = 1.0f - (126.0f / fs);
        // fb283 — metallic-sparkle resonator: RBJ band-pass @ ~6.2 kHz, moderate Q (rings, not whistles).
        { float f0 = 6200.0f, Q = 1.7f, w0 = 2.0f * PI * f0 / fs, cw = std::cos (w0), sw = std::sin (w0);
          float al = sw / (2.0f * Q), a0 = 1.0f + al;
          rb0 = al / a0; rb1 = 0.0f; rb2 = -al / a0; ra1 = (-2.0f * cw) / a0; ra2 = (1.0f - al) / a0; }
        tiltCoef = 1.0f - std::exp (-2.0f * PI * 1250.0f / fs);   // fb283 — brightness-tilt crossover ~1.8 kHz
        reset();
        primed = false;
        updateCoefficients();
    }

    void reset()
    {
        std::fill (dif1L.begin(), dif1L.end(), 0.0f); std::fill (dif1R.begin(), dif1R.end(), 0.0f);
        std::fill (del1L.begin(), del1L.end(), 0.0f); std::fill (del1R.begin(), del1R.end(), 0.0f);
        std::fill (dif2L.begin(), dif2L.end(), 0.0f); std::fill (dif2R.begin(), dif2R.end(), 0.0f);
        std::fill (del2L.begin(), del2L.end(), 0.0f); std::fill (del2R.begin(), del2R.end(), 0.0f);
        for (int i = 0; i < NID; ++i) std::fill (id[i].begin(), id[i].end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        inLpZ = lcZ = bassLz = bassRz = dampLz = dampRz = 0.0f; dcxL = dcyL = dcxR = dcyR = 0.0f;
        for (int i = 0; i < NDISP; ++i) { dzL[i] = dzR[i] = 0.0f; }
        for (int i = 0; i < NDO; ++i)   { doL[i] = doR[i] = 0.0f; }
        rxL1 = rxL2 = ryL1 = ryL2 = rxR1 = rxR2 = ryR1 = ryR2 = 0.0f;   // fb283 — sparkle resonator state
        tiltLpL = tiltLpR = 0.0f;                                        // fb283 — brightness-tilt state
        lfoPhL = 0.0f; lfoPhR = 0.37f;
    }

    // ── params (0..1 unless noted) ──
    void setSize        (float v) { size    = clamp01 (v); }
    void setDecay       (float v) { decay   = clamp01 (v); }
    void setTone        (float v) { tone    = clamp01 (v); }
    void setPreDelayMs  (float ms){ preMs   = clampf (ms, 0.0f, 120.0f); }
    void setDiffusion   (float v) { diffuse = clamp01 (v); }
    void setModDepth    (float v) { modDepth= clamp01 (v); }
    void setDispersion  (float v) { disp    = clamp01 (v); }   // metallic chirp
    void setDamping     (float v) { damp    = clamp01 (v); }
    void setBassDecay   (float m) { bassMul = clampf (m, 0.25f, 2.5f); }
    void setLowCutHz    (float hz){ lowCut  = clampf (hz, 20.0f, 1000.0f); }
    void setWidth       (float v) { width   = clamp01 (v); }
    void setCharacter   (int c)   { character = c < 0 ? 0 : (c > 7 ? 7 : c); }
    void setMaterial    (int m)   { material  = m < 0 ? 0 : (m > 5 ? 5 : m); }
    void setModEnabled  (bool on) { modOn   = on; }
    void setFreeze      (bool f)  { freezeOn = f; }
    void setMix         (float v) { mixExt  = clamp01 (v); }

    void updateCoefficients()
    {
        const CharBias cb = CHAR[character];
        const MatBias  mb = MAT[material];
        rt60 = 0.3f * std::pow (20.0f, decay) * cb.decayMul * mb.decay;   // plate range; Material rings differ
        sizeScaleT = (0.5f + 1.3f * size) * cb.sizeMul;
        // round-trip loop delay (both halves) → decay so 2 cross-couple mults hit the target RT60
        float dTot = (672.f + 4453.f + 1800.f + 3720.f + 908.f + 4217.f + 2656.f + 3163.f) * scl * sizeScaleT;
        float dc = std::pow (10.0f, -1.5f * dTot / (fs * rt60));
        if (dc > 0.9995f) dc = 0.9995f;
        decayT = dc;
        float dampEff = clamp01 (damp * cb.dampMul * mb.damp + cb.dampAdd);
        dampT = 0.05f + 0.9f * dampEff;                              // one-pole LP coeff in the tank
        float toneEff = clamp01 (tone * cb.toneMul * mb.tone + cb.toneAdd);
        inLpT = std::exp (-2.0f * PI * (1400.0f * std::pow (13.0f, toneEff)) / fs);   // bandwidth ~1.4k..18k
        // fb283 — BOLD brightness TILT on the output: the tank's gentle one-pole damping alone barely
        // moves the tail centroid (metals/Tone read weak). A ±~10 dB high/low tilt driven by the same
        // brightness makes Material (aluminum-bright ↔ copper-dark), Tone, and Character night-and-day.
        // Identity at toneEff=0.5 → the default sound is untouched. Out of loop → stable; ramped → click-free.
        float tilt = (toneEff - 0.5f) * 2.0f;                 // -1 dark .. +1 bright
        hiGainT = std::pow (4.8f, tilt);                      // highs: ~-13 dB .. +13 dB (night-and-day)
        loGainT = std::pow (2.1f, -tilt);                     // lows: opposite tilt (warmth on dark)
        lcT   = std::exp (-2.0f * PI * lowCut / fs);
        float diffEff = clamp01 (std::max (diffuse, cb.diffFloor) * mb.diff);
        g1T = 0.70f * diffEff;                                       // decay-diffusion 1 (mod allpass)
        g2T = 0.50f * diffEff;                                       // decay-diffusion 2
        idGT = diffEff;                                             // input diffuser scale
        // fb283 — Dispersion is now AUDIBLE on all material. It splits into two layers:
        //  (a) allpass shimmer/boing (phase — inharmonic detune + transient chirp), and
        //  (b) a metallic resonant SPARKLE: a ringing HF peak whose gain rides Dispersion, so the
        //      knob is night-and-day (0 = clean plate, 100% = bright shimmery metal). Distinct from
        //      Tone/Material (broadband) — this is a narrow resonant zing. Out of the loop → stable.
        float dispAmt = clampf (disp * cb.dispMul * mb.disp, 0.0f, 1.0f);
        dispCoefT   = dispAmt * 0.58f;                              // in/out allpass shimmer (phase character)
        dispBrightT = dispAmt * 3.2f;                              // resonant sparkle add-gain (measurable + audible)
        preSampT = preMs * 0.001f * fs;
        // bass-decay low-shelf: low band reaches RT60×bassMul
        float bassEff = clampf (bassMul * cb.lowMul, 0.25f, 3.0f);
        lowGainT = std::pow (dc, 1.0f / bassEff - 1.0f);
        float depth01 = modOn ? clampf (modDepth * cb.modMul, 0.0f, 2.0f) : 0.0f;
        modSampT = depth01 * 12.0f;                                 // ± excursion on the dif1 allpasses
        modInc   = (0.9f * (0.6f + 0.8f * size)) / fs;
        widthT   = clamp01 (width + cb.widthAdd);
        freezeTgt= freezeOn ? 1.0f : 0.0f;
        if (! primed)
        {
            sizeScaleC = sizeScaleT; decayC = decayT; dampC = dampT; inLpC = inLpT; lcC = lcT;
            g1C = g1T; g2C = g2T; idGC = idGT; dispCoefC = dispCoefT; dispBrightC = dispBrightT; preSampC = preSampT;
            hiGainC = hiGainT; loGainC = loGainT;
            lowGainC = lowGainT; modSampC = modSampT; widthC = widthT; freezeCur = freezeTgt;
            primed = true;
        }
    }

    void process (float* L, float* R, int n)
    {
        const float dg = std::cos (0.5f * PI * mixExt), wg = std::sin (0.5f * PI * mixExt);
        for (int s = 0; s < n; ++s) { float wl, wr; processSample (L[s], R[s], wl, wr); L[s] = dg * L[s] + wg * wl; R[s] = dg * R[s] + wg * wr; }
    }

    inline void processSample (float inL, float inR, float& wetL, float& wetR)
    {
        // ── per-sample smoothing (glide everything → click-free) ──
        sizeScaleC += (sizeScaleT - sizeScaleC) * smth;
        decayC     += (decayT     - decayC)     * smth;
        dampC      += (dampT      - dampC)      * smth;
        inLpC      += (inLpT      - inLpC)      * smth;
        lcC        += (lcT        - lcC)        * smth;
        g1C        += (g1T        - g1C)        * smth;
        g2C        += (g2T        - g2C)        * smth;
        idGC       += (idGT       - idGC)       * smth;
        dispCoefC  += (dispCoefT  - dispCoefC)  * smth;
        dispBrightC+= (dispBrightT- dispBrightC) * smth;
        hiGainC    += (hiGainT    - hiGainC)     * smth;
        loGainC    += (loGainT    - loGainC)     * smth;
        preSampC   += (preSampT   - preSampC)   * smth;
        lowGainC   += (lowGainT   - lowGainC)   * smth;
        modSampC   += (modSampT   - modSampC)   * smth;
        widthC     += (widthT     - widthC)     * smth;
        freezeCur  += (freezeTgt  - freezeCur)  * smth;
        const float S = sizeScaleC * scl;   // samples-per-Dattorro-unit at this size

        float x = 0.5f * (inL + inR);
        // pre-delay → bandwidth LP → low-cut
        pre[(size_t) preWr] = x; x = readFrac (pre, preMask, preWr, preSampC); preWr = (preWr + 1) & preMask;
        inLpZ = (1.0f - inLpC) * x + inLpC * inLpZ; x = inLpZ;
        lcZ   = (1.0f - lcC)   * x + lcC   * lcZ;    x = x - lcZ;
        // 4 series input-diffusion allpasses (integer delay, scaled by diffusion)
        for (int i = 0; i < NID; ++i)
        {
            float g = idG[i] * idGC;
            float d = id[i][(size_t) ((idWr[i] - idDelay[i]) & idMask[i])];
            float in = x - g * d; id[i][(size_t) idWr[i]] = in; idWr[i] = (idWr[i] + 1) & idMask[i];
            x = d + g * in;
        }
        const float xd = x;   // tank input (fed into BOTH halves)

        // ── TANK (figure-8). Fixed write pointers this sample; advance all at the end. ──
        // read both half outputs (tails of del2), then disperse + bass-shelf + decay the feedback.
        float outL = readFrac (del2L, del2Lmask, del2Lwr, 3720.f * S);
        float outR = readFrac (del2R, del2Rmask, del2Rwr, 3163.f * S);
        // bass-decay low-shelf on the feedback. fb285 — at FREEZE the loop gain → FREEZE_G, so the
        // low-band boost must ramp to UNITY or dec·lowGain > 1 and the low band DIVERGES on freeze-hold.
        const float lowGeff = lowGainC + (1.0f - lowGainC) * freezeCur;
        bassLz = (1.0f - bassCoef) * outL + bassCoef * bassLz; outL = (outL - bassLz) + bassLz * lowGeff;
        bassRz = (1.0f - bassCoef) * outR + bassCoef * bassRz; outR = (outR - bassRz) + bassRz * lowGeff;
        // dispersion cascade (metallic chirp) — CPU-gated
        if (dispCoefC > 0.001f)
            for (int i = 0; i < NDISP; ++i)
            {
                float c = dispCoefC;
                float yL = c * outL + dzL[i]; dzL[i] = outL - c * yL; outL = yL;
                float yR = c * outR + dzR[i]; dzR[i] = outR - c * yR; outR = yR;
            }
        const float dec = decayC + (FREEZE_G - decayC) * freezeCur;   // freeze → near-unity loop
        const float inG = 0.5f * (1.0f - freezeCur);                  // freeze cuts input → infinite hold
        float lIn = inG * xd + dec * outR;   // figure-8 cross-couple
        float rIn = inG * xd + dec * outL;

        // LEFT half: dif1L (mod allpass) → del1L → damp → dif2L → del2L(write)
        { float D = 672.f * S + modSampC * fastSin (lfoPhL);
          float d = readFrac (dif1L, dif1Lmask, dif1Lwr, D); float in = lIn - g1C * d; dif1L[(size_t) dif1Lwr] = flush (in); lIn = d + g1C * in; }
        { float d = readFrac (del1L, del1Lmask, del1Lwr, 4453.f * S); del1L[(size_t) del1Lwr] = flush (lIn); lIn = d; }
        dampLz = (1.0f - dampC) * lIn + dampC * dampLz; lIn = dampLz;
        { float d = readFrac (dif2L, dif2Lmask, dif2Lwr, 1800.f * S); float in = lIn - g2C * d; dif2L[(size_t) dif2Lwr] = flush (in); lIn = d + g2C * in; }
        del2L[(size_t) del2Lwr] = flush (lIn);
        // RIGHT half
        { float D = 908.f * S + modSampC * fastSin (lfoPhR);
          float d = readFrac (dif1R, dif1Rmask, dif1Rwr, D); float in = rIn - g1C * d; dif1R[(size_t) dif1Rwr] = flush (in); rIn = d + g1C * in; }
        { float d = readFrac (del1R, del1Rmask, del1Rwr, 4217.f * S); del1R[(size_t) del1Rwr] = flush (rIn); rIn = d; }
        dampRz = (1.0f - dampC) * rIn + dampC * dampRz; rIn = dampRz;
        { float d = readFrac (dif2R, dif2Rmask, dif2Rwr, 2656.f * S); float in = rIn - g2C * d; dif2R[(size_t) dif2Rwr] = flush (in); rIn = d + g2C * in; }
        del2R[(size_t) del2Rwr] = flush (rIn);

        // ── 7 decorrelated output taps per channel (Dattorro's tap set), read at fixed wr ──
        float yl = 0.6f * ( readFrac (del1R, del1Rmask, del1Rwr, 266.f*S) + readFrac (del1R, del1Rmask, del1Rwr, 2974.f*S)
                          - readFrac (dif2R, dif2Rmask, dif2Rwr, 1913.f*S) + readFrac (del2R, del2Rmask, del2Rwr, 1996.f*S)
                          - readFrac (del1L, del1Lmask, del1Lwr, 1990.f*S) - readFrac (dif2L, dif2Lmask, dif2Lwr, 187.f*S)
                          - readFrac (del2L, del2Lmask, del2Lwr, 1066.f*S) );
        float yr = 0.6f * ( readFrac (del1L, del1Lmask, del1Lwr, 353.f*S) + readFrac (del1L, del1Lmask, del1Lwr, 3627.f*S)
                          - readFrac (dif2L, dif2Lmask, dif2Lwr, 1228.f*S) + readFrac (del2L, del2Lmask, del2Lwr, 2673.f*S)
                          - readFrac (del1R, del1Rmask, del1Rwr, 2111.f*S) - readFrac (dif2R, dif2Rmask, dif2Rwr, 335.f*S)
                          - readFrac (del2R, del2Rmask, del2Rwr, 121.f*S) );

        // output dispersion allpasses — phase-only (magnitude-flat): the inharmonic shimmer +
        // transient "boing" character. Audible on transients but NOT on a diffuse tail (that was
        // fb282's mistake — RMS moved, the ear didn't). The audible/measurable sparkle is below.
        if (dispCoefC > 0.001f)
            for (int i = 0; i < NDO; ++i)
            {
                float c = dispCoefC;
                float a = c * yl + doL[i]; doL[i] = yl - c * a; yl = a;
                float b = c * yr + doR[i]; doR[i] = yr - c * b; yr = b;
            }
        // fb283 — METALLIC SPARKLE: a ringing HF resonance (RBJ band-pass, fixed coeffs) added with
        // a gain that rides Dispersion. THIS is what you hear — a bright metallic "zing" that grows
        // with the knob on ANY material (sustained + transient) and MOVES the spectrum (measurable).
        // Out of the feedback loop → unconditionally stable; gain ramps → click-free; 0 = identity.
        if (dispBrightC > 1.0e-4f)
        {
            float bpL = rb0*yl + rb1*rxL1 + rb2*rxL2 - ra1*ryL1 - ra2*ryL2;
            rxL2 = rxL1; rxL1 = yl; ryL2 = ryL1; ryL1 = flush (bpL); yl += dispBrightC * bpL;
            float bpR = rb0*yr + rb1*rxR1 + rb2*rxR2 - ra1*ryR1 - ra2*ryR2;
            rxR2 = rxR1; rxR1 = yr; ryR2 = ryR1; ryR1 = flush (bpR); yr += dispBrightC * bpR;
        }
        // fb283 — brightness TILT (Material/Tone/Character): one-pole split @ ~1.8 kHz, recombine with
        // high/low gains. Identity when hiGain=loGain=1 (toneEff 0.5). Bold, stable, click-free.
        tiltLpL += (yl - tiltLpL) * tiltCoef; { float hi = yl - tiltLpL; yl = tiltLpL * loGainC + hi * hiGainC; }
        tiltLpR += (yr - tiltLpR) * tiltCoef; { float hi = yr - tiltLpR; yr = tiltLpR * loGainC + hi * hiGainC; }

        // advance all tank write pointers + LFOs
        dif1Lwr = (dif1Lwr + 1) & dif1Lmask; dif1Rwr = (dif1Rwr + 1) & dif1Rmask;
        del1Lwr = (del1Lwr + 1) & del1Lmask; del1Rwr = (del1Rwr + 1) & del1Rmask;
        dif2Lwr = (dif2Lwr + 1) & dif2Lmask; dif2Rwr = (dif2Rwr + 1) & dif2Rmask;
        del2Lwr = (del2Lwr + 1) & del2Lmask; del2Rwr = (del2Rwr + 1) & del2Rmask;
        lfoPhL += modInc; if (lfoPhL >= 1.0f) lfoPhL -= 1.0f;
        lfoPhR += modInc; if (lfoPhR >= 1.0f) lfoPhR -= 1.0f;

        // M/S width + DC block
        float mid = 0.5f * (yl + yr), sid = 0.5f * (yl - yr) * (0.15f + 1.85f * widthC);
        float wl = mid + sid, wr = mid - sid;
        float ol = wl - dcxL + dcR * dcyL; dcxL = wl; dcyL = flush (ol);
        float orr= wr - dcxR + dcR * dcyR; dcxR = wr; dcyR = flush (orr);
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
    static inline float fastSin (float ph01) { return std::sin (2.0f * PI * ph01); }
    void alloc (std::vector<float>& buf, int& mask, int baseLen29k)
    {
        int need = (int) std::ceil (baseLen29k * scl * 1.9f) + 64;   // headroom for Size×1.8 + mod + taps
        int sz = 1; while (sz < need) sz <<= 1; buf.assign ((size_t) sz, 0.0f); mask = sz - 1;
    }

    static constexpr float PI = 3.14159265358979f;
    static constexpr float FREEZE_G = 0.9999f;
    static constexpr float idLen[NID]  = { 142.f, 107.f, 379.f, 277.f };
    static constexpr float idG[NID]    = { 0.75f, 0.75f, 0.625f, 0.625f };
    // Plate Character voicings (8): decayMul, sizeMul, toneMul, toneAdd, dampMul, dampAdd, diffFloor, lowMul, widthAdd, modMul, dispMul.
    struct CharBias { float decayMul, sizeMul, toneMul, toneAdd, dampMul, dampAdd, diffFloor, lowMul, widthAdd, modMul, dispMul; };
    static constexpr CharBias CHAR[8] = {
        /* Classic */ { 1.00f, 1.00f, 1.00f, 0.00f, 1.00f, 0.00f, 0.30f, 1.00f,  0.00f, 1.0f, 1.0f },
        /* Vintage */ { 0.90f, 0.95f, 0.60f, 0.00f, 1.00f, 0.25f, 0.35f, 1.25f,  0.05f, 1.4f, 1.2f },
        /* Modern  */ { 1.05f, 1.00f, 1.45f, 0.05f, 0.55f, 0.00f, 0.45f, 0.85f,  0.10f, 0.9f, 0.8f },
        /* Vocal   */ { 1.00f, 1.00f, 0.90f, 0.00f, 1.00f, 0.15f, 0.55f, 1.05f,  0.00f, 1.1f, 0.9f },
        /* Drum    */ { 0.50f, 0.80f, 1.10f, 0.00f, 0.90f, 0.05f, 0.35f, 0.95f,  0.05f, 0.8f, 0.7f },
        /* Bright  */ { 1.10f, 1.00f, 2.10f, 0.18f, 0.28f, 0.00f, 0.40f, 0.75f,  0.10f, 1.0f, 1.1f },
        /* Dark    */ { 1.00f, 1.00f, 0.28f, 0.00f, 1.00f, 0.48f, 0.40f, 1.25f,  0.00f, 1.0f, 0.7f },
        /* Shimmer */ { 1.45f, 1.15f, 1.25f, 0.05f, 0.55f, 0.00f, 0.55f, 0.90f,  0.20f, 2.0f, 1.4f },
    };
    // Plate Material (6 metals): tone, damp, disp, diff, decay multipliers (dense metals ring longer).
    struct MatBias { float tone, damp, disp, diff, decay; };
    // fb283 — WIDE, archetypal metals: tone (input bandwidth) + damp (tank brightness) move TOGETHER so
    // brightness reads on the tail, decay sets ring length, disp sets each metal's metallic sparkle.
    static constexpr MatBias MAT[6] = {
        /* Steel    */ { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },   // balanced reference
        /* Gold     */ { 0.72f, 1.30f, 0.60f, 1.18f, 1.30f },   // warm · smooth · long ring
        /* Aluminum */ { 1.95f, 0.30f, 1.55f, 0.90f, 0.58f },   // bright · zingy · short (light metal)
        /* Brass    */ { 0.60f, 1.20f, 1.25f, 1.20f, 1.75f },   // dark · resonant · longest ring
        /* Copper   */ { 0.46f, 1.80f, 0.50f, 1.05f, 1.16f },   // mellow · darkest · medium
        /* Chrome   */ { 2.10f, 0.25f, 1.80f, 0.95f, 0.64f },   // brilliant · glassy · tight
    };

    float fs = 48000.0f, scl = 1.613f, smth = 0.001f, bassCoef = 0.f, dcR = 0.999f;
    bool  primed = false;
    std::vector<float> dif1L, dif1R, del1L, del1R, dif2L, dif2R, del2L, del2R, pre;
    int dif1Lmask=0, dif1Rmask=0, del1Lmask=0, del1Rmask=0, dif2Lmask=0, dif2Rmask=0, del2Lmask=0, del2Rmask=0, preMask=0;
    int dif1Lwr=0, dif1Rwr=0, del1Lwr=0, del1Rwr=0, dif2Lwr=0, dif2Rwr=0, del2Lwr=0, del2Rwr=0, preWr=0;
    std::vector<float> id[NID]; int idMask[NID] = {0}; int idWr[NID] = {0}; int idDelay[NID] = {0};
    float inLpZ=0, lcZ=0, bassLz=0, bassRz=0, dampLz=0, dampRz=0, dcxL=0, dcyL=0, dcxR=0, dcyR=0;
    float dzL[NDISP] = {0}, dzR[NDISP] = {0}, doL[NDO] = {0}, doR[NDO] = {0}, lfoPhL=0, lfoPhR=0.37f;
    // targets + ramped
    float sizeScaleT=1, sizeScaleC=1, decayT=0.3f, decayC=0.3f, dampT=0.5f, dampC=0.5f, inLpT=0, inLpC=0, lcT=0, lcC=0;
    float g1T=0.7f, g1C=0.7f, g2T=0.5f, g2C=0.5f, idGT=1, idGC=1, dispCoefT=0, dispCoefC=0, preSampT=0, preSampC=0;
    float dispBrightT=0, dispBrightC=0;                 // fb283 — metallic sparkle add-gain (ramped)
    float hiGainT=1, hiGainC=1, loGainT=1, loGainC=1, tiltCoef=0.3f;   // fb283 — brightness tilt (ramped)
    float tiltLpL=0, tiltLpR=0;                         // fb283 — tilt one-pole split state
    // fb283 — metallic resonance: fixed RBJ band-pass coeffs (computed in prepare) + per-channel DF-I state
    float rb0=0, rb1=0, rb2=0, ra1=0, ra2=0;
    float rxL1=0, rxL2=0, ryL1=0, ryL2=0, rxR1=0, rxR2=0, ryR1=0, ryR2=0;
    float lowGainT=1, lowGainC=1, modSampT=0, modSampC=0, widthT=0.8f, widthC=0.8f, modInc=0, rt60=2.0f, mixExt=0.3f;
    float freezeCur=0, freezeTgt=0;
    // param state
    int   character=0, material=0;
    bool  modOn=true, freezeOn=false;
    float size=0.5f, decay=0.5f, tone=0.55f, preMs=6.f, diffuse=0.7f, modDepth=0.3f, disp=0.3f,
          damp=0.4f, bassMul=1.0f, lowCut=20.f, width=0.85f;
};
