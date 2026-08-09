#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// RoomReverb — the ROOM reverb type (fb281). A room is NOT a small hall: its
// signature is DISCRETE EARLY REFLECTIONS (you hear the walls/floor/ceiling as
// distinct echoes) followed by a SHORT, DENSE, DAMPED tail. So this engine =
//   [pre-delay → input tone LP + low-cut]
//     ├─ EARLY-REFLECTION network : a 16-tap delay, Size-scaled, Shape-patterned
//     │    (timing + stereo pan) → the room's spatial fingerprint.
//     └─ LATE TAIL : the proven Hall 8-line Jot/Hadamard FDN (tank diffusion,
//          HF damping, low-shelf bass, freeze) but tuned ROOM-short/dense.
//   wet = Reflections·ER + tail   (Reflections balances discrete ↔ diffuse).
// Clean-room from the reverb build bible; borrows Hall's validated, click-free
// FDN verbatim so it inherits the no-clicks/stable guarantees.
//   • Character (8) = real room voicings: Booth/Bedroom/Studio/Live/Chamber/Tile/
//     Garage/Warehouse (size/decay/tone/damp/diffusion/bass/reflections biases).
//   • Shape (6) = ER pattern: Tight/Wide/Long/Diffuse/Scatter/Sparse.
//   • Mod (gentle de-metallizing) + Freeze (infinite room). Every continuous
//     control per-sample smoothed; Shape switches dip the ER through zero.
// PURE C++ (no JUCE): offline-validates standalone AND drops into the voice path.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>

class RoomReverb
{
public:
    static constexpr int N  = 8;    // FDN tail lines
    static constexpr int ET = 16;   // early-reflection taps

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const float sr = fs / 48000.0f;
        for (int i = 0; i < N; ++i)   // ROOM-short lines (~11–32 ms @48k) — dense/boxy; headroom for big Characters
        {
            const int need = (int) std::ceil (baseLen48[i] * sr * 4.0f) + 64;
            int sz = 1; while (sz < need) sz <<= 1;
            line[i].assign ((size_t) sz, 0.0f); lineMask[i] = sz - 1; lineWr[i] = 0;
            dampZ[i] = bassZ[i] = 0.0f; lfoPh[i] = (float) i / (float) N;
        }
        for (int i = 0; i < 4; ++i)
        {
            const int len = std::max (8, (int) std::lround (apLen29k[i] * (fs / 29761.0f)));
            int sz = 1; while (sz < len + 4) sz <<= 1;
            ap[i].assign ((size_t) sz, 0.0f); apMask[i] = sz - 1; apWr[i] = 0; apDelay[i] = len;
        }
        for (int i = 0; i < N; ++i)
        {
            const int len = std::max (8, (int) std::lround (tankLen48[i] * sr));
            int sz = 1; while (sz < len + 4) sz <<= 1;
            tank[i].assign ((size_t) sz, 0.0f); tankMask[i] = sz - 1; tankWr[i] = 0; tankDelay[i] = len;
        }
        { int need = (int) std::ceil (0.12f * fs) + 8; int sz = 1; while (sz < need) sz <<= 1;   // pre-delay ≤120 ms
          pre.assign ((size_t) sz, 0.0f); preMask = sz - 1; preWr = 0; }
        { int need = (int) std::ceil (0.60f * fs) + 8; int sz = 1; while (sz < need) sz <<= 1;   // ER delay (Size×Shape×Char headroom)
          er.assign ((size_t) sz, 0.0f); erMask = sz - 1; erWr = 0; }
        for (int k = 0; k < ET; ++k) { jitT[k] = rand11(); jitP[k] = rand11(); }   // deterministic Scatter jitter
        tailPreSamp = 0.012f * fs;   // late field trails the early reflections ~12 ms (physical + keeps ERs discrete)
        smth = 1.0f - std::exp (-1.0f / (0.015f * fs));   // ~15 ms smoothing
        bassCoef = std::exp (-2.0f * PI * 400.0f / fs);
        dcR = 1.0f - (126.0f / fs);
        reset();
        primed = false; shapeActive = -1;   // force first table build
        updateCoefficients();
    }

    void reset()
    {
        for (int i = 0; i < N; ++i) { std::fill (line[i].begin(), line[i].end(), 0.0f); dampZ[i] = bassZ[i] = 0.0f; }
        for (int i = 0; i < 4; ++i)  std::fill (ap[i].begin(), ap[i].end(), 0.0f);
        for (int i = 0; i < N; ++i)  std::fill (tank[i].begin(), tank[i].end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        std::fill (er.begin(), er.end(), 0.0f);
        inLpZ = lcZ = erLpL = erLpR = 0.0f; dcxL = dcyL = dcxR = dcyR = 0.0f;
    }

    // ── params (0..1 unless noted); call updateCoefficients() after a batch ──
    void setSize        (float v) { size    = clamp01 (v); }
    void setDecay       (float v) { decay   = clamp01 (v); }
    void setTone        (float v) { tone    = clamp01 (v); }
    void setPreDelayMs  (float ms){ preMs   = clampf (ms, 0.0f, 120.0f); }
    void setDiffusion   (float v) { diffuse = clamp01 (v); }
    void setModDepth    (float v) { modDepth= clamp01 (v); }
    void setSpread      (float v) { spread  = clamp01 (v); }   // fb287 — early-reflection stereo width (Room has no mod → this owns the MODDEPTH slot)
    void setReflections (float v) { refl    = clamp01 (v); }   // ER ↔ tail balance
    void setDamping     (float v) { damp    = clamp01 (v); }   // HF absorption
    void setBassDecay   (float m) { bassMul = clampf (m, 0.25f, 2.5f); }
    void setLowCutHz    (float hz){ lowCut  = clampf (hz, 20.0f, 1000.0f); }
    void setWidth       (float v) { width   = clamp01 (v); }
    void setCharacter   (int c)   { character = c < 0 ? 0 : (c > 7 ? 7 : c); }
    void setShape       (int s)   { shape     = s < 0 ? 0 : (s > 5 ? 5 : s); }
    void setModEnabled  (bool on) { modOn   = on; }
    void setFreeze      (bool f)  { freezeOn = f; }
    void setMix         (float v) { mixExt  = clamp01 (v); }

    void updateCoefficients()
    {
        const CharBias cb = CHAR[character];
        // ROOM decay: 0.15 … 4.5 s (much shorter than a hall) — rooms are tight.
        rt60 = 0.15f * std::pow (30.0f, decay) * cb.decayMul;
        const float sizeScale   = (0.4f + 1.8f * size) * cb.sizeMul;   // room dimensions
        const float diffEff     = std::max (diffuse, cb.diffFloor);
        const float bassEff     = clampf (bassMul * cb.lowMul, 0.25f, 3.0f);
        const float sr = fs / 48000.0f;
        const float lowPow = 1.0f / bassEff - 1.0f;
        for (int i = 0; i < N; ++i)
        {
            float d = baseLen48[i] * sr * sizeScale;
            if (d > (float) lineMask[i] - 30.0f) d = (float) lineMask[i] - 30.0f;
            baseDelayT[i] = d;
            float dLoop = d + (float) tankDelay[i];
            float g = std::pow (10.0f, -3.0f * dLoop / (fs * rt60));
            if (g > 0.9995f) g = 0.9995f;
            gLineT[i]   = g;
            lowGainT[i] = std::pow (g, lowPow);
        }
        dampT = 0.9f * clamp01 (damp * cb.dampMul + cb.dampAdd);
        for (int i = 0; i < 4; ++i) apGT[i] = diffEff * apBaseG[i];
        tankGT = 0.7f * diffEff;
        float toneEff = clamp01 (tone * cb.toneMul + cb.toneAdd);
        inLpT   = std::exp (-2.0f * PI * (1200.0f * std::pow (14.0f, toneEff)) / fs);   // room tone ~1.2k..17k
        erLpT   = std::exp (-2.0f * PI * (1400.0f * std::pow (11.0f, toneEff) * (1.0f - 0.55f * dampT)) / fs);  // ER also softens w/ damping
        lcT     = std::exp (-2.0f * PI * lowCut / fs);
        preSampT= preMs * 0.001f * fs;
        sizeScaleT = sizeScale;
        // gentle de-metallizing mod (sine only; rooms are near-static) — Mod gates it, Character scales.
        float depth01 = modOn ? clampf (modDepth * cb.modMul, 0.0f, 1.6f) : 0.0f;
        modSampT   = depth01 * 14.0f;
        modInc     = (0.7f * (0.6f + 0.8f * size)) / fs;   // ~0.4–1.0 Hz, a touch faster in bigger rooms
        widthT     = clamp01 (width + cb.widthAdd);
        spreadT    = spread;                               // fb287 — ER stereo-width target (M/S on the reflections)
        freezeTgt  = freezeOn ? 1.0f : 0.0f;
        // Reflections (+ Character reflMul) = ER prominence vs tail balance. Low floor so
        // Reflections=0 is a smooth diffuse room and =1 is all discrete early slaps (night & day).
        const float reflEff = clamp01 (refl * cb.reflMul);
        erGainBaseT = 0.05f + 1.60f * reflEff;    // ER level (near-silent → strong)
        tailGainT   = 1.00f - 0.50f * reflEff;    // tail recedes as reflections take over

        // ── ER TAP TABLES: rebuilt only on Shape change; switch dips the ER through zero (click-free) ──
        if (shape != shapePending) { buildTaps (shape); shapePending = shape; if (primed) { erDipping = true; erGateTgt = 0.0f; } }

        if (! primed)
        {
            for (int i = 0; i < N; ++i) { baseDelayC[i] = baseDelayT[i]; gLineC[i] = gLineT[i]; lowGainC[i] = lowGainT[i]; }
            dampC = dampT; for (int i = 0; i < 4; ++i) apGC[i] = apGT[i];
            inLpC = inLpT; erLpC = erLpT; lcC = lcT; preSampC = preSampT; modSampC = modSampT; widthC = widthT;
            tankGC = tankGT; sizeScaleC = sizeScaleT; freezeCur = freezeTgt;
            erGainBaseC = erGainBaseT; tailGainC = tailGainT; spreadC = spreadT;
            commitTaps(); shapeActive = shape; erGateC = 1.0f; erGateTgt = 1.0f; erDipping = false;
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
        // ── per-sample smoothing (glide everything → no zipper / comb click) ──
        for (int i = 0; i < N; ++i)
        {
            baseDelayC[i] += (baseDelayT[i] - baseDelayC[i]) * smth;
            gLineC[i]     += (gLineT[i]     - gLineC[i])     * smth;
            lowGainC[i]   += (lowGainT[i]   - lowGainC[i])   * smth;
        }
        dampC += (dampT - dampC) * smth;
        for (int i = 0; i < 4; ++i) apGC[i] += (apGT[i] - apGC[i]) * smth;
        tankGC     += (tankGT     - tankGC)     * smth;
        inLpC      += (inLpT      - inLpC)      * smth;
        erLpC      += (erLpT      - erLpC)      * smth;
        lcC        += (lcT        - lcC)        * smth;
        preSampC   += (preSampT   - preSampC)   * smth;
        modSampC   += (modSampT   - modSampC)   * smth;
        widthC     += (widthT     - widthC)     * smth;
        sizeScaleC += (sizeScaleT - sizeScaleC) * smth;
        erGainBaseC+= (erGainBaseT- erGainBaseC)* smth;
        tailGainC  += (tailGainT  - tailGainC)  * smth;
        spreadC    += (spreadT    - spreadC)    * smth;
        freezeCur  += (freezeTgt  - freezeCur)  * smth;
        erGateC    += (erGateTgt  - erGateC)    * smth;
        if (erDipping && erGateC < 0.02f) { commitTaps(); shapeActive = shapePending; erDipping = false; erGateTgt = 1.0f; }

        float x = 0.5f * (inL + inR);

        // pre-delay → input tone LP + low-cut HP
        pre[(size_t) preWr] = x;
        x = readFrac (pre, preMask, preWr, preSampC);
        preWr = (preWr + 1) & preMask;
        inLpZ = (1.0f - inLpC) * x + inLpC * inLpZ;   x = inLpZ;
        lcZ   = (1.0f - lcC)   * x + lcC   * lcZ;      x = x - lcZ;   // xF = filtered input (feeds BOTH ER + tail)

        // ── EARLY REFLECTIONS: Size-scaled, Shape-patterned discrete taps ──
        er[(size_t) erWr] = x;
        float erL = 0.0f, erR = 0.0f;
        const int na = nActive;
        for (int k = 0; k < na; ++k)
        {
            float t = tapBaseSamp[k] * sizeScaleC;
            if (t < 1.0f) t = 1.0f;
            const float mx = (float) erMask - 4.0f; if (t > mx) t = mx;
            const float s = readFrac (er, erMask, erWr, t);
            erL += s * tapGL[k];
            erR += s * tapGR[k];
        }
        float xt = readFrac (er, erMask, erWr, tailPreSamp);   // late-field input = ~12 ms-delayed dry (ERs come first)
        erWr = (erWr + 1) & erMask;
        erLpL = (1.0f - erLpC) * erL + erLpC * erLpL;   erL = erLpL;   // ER HF absorption
        erLpR = (1.0f - erLpC) * erR + erLpC * erLpR;   erR = erLpR;
        const float erScale = erGainBaseC * erGateC * (1.0f - freezeCur);  // freeze holds only the diffuse tail
        erL *= erScale; erR *= erScale;
        // fb287 — SPREAD: widen the early reflections' stereo image (M/S on the ER pair only; the tail keeps
        // its own Width). 0 = collapsed/narrow discrete slaps, 1 = very wide. Ramped → click-free; mono-safe.
        { const float erMid = 0.5f * (erL + erR), erSide = 0.5f * (erL - erR) * (0.10f + 2.30f * spreadC);
          erL = erMid + erSide; erR = erMid - erSide; }

        // ── LATE TAIL: input diffusion → room FDN (borrowed from the validated Hall core) ──
        // xt = the ~12 ms-delayed input read above, so the diffuse field trails the discrete ERs.
        for (int i = 0; i < 4; ++i)
        {
            float d = ap[i][(size_t) ((apWr[i] - apDelay[i]) & apMask[i])];
            float in = xt - apGC[i] * d;
            ap[i][(size_t) apWr[i]] = in;
            apWr[i] = (apWr[i] + 1) & apMask[i];
            xt = d + apGC[i] * in;
        }
        float sIn[N];
        for (int i = 0; i < N; ++i)
        {
            float exc = modSampC * fastSin (lfoPh[i]);
            float dd  = baseDelayC[i] + exc;
            if (dd < 1.0f) dd = 1.0f;
            float maxd = (float) lineMask[i] - 4.0f; if (dd > maxd) dd = maxd;
            float v = readFrac (line[i], lineMask[i], lineWr[i], dd);
            { float td  = tank[i][(size_t) ((tankWr[i] - tankDelay[i]) & tankMask[i])];
              float tin = v - tankGC * td;
              tank[i][(size_t) tankWr[i]] = flush (tin);
              tankWr[i] = (tankWr[i] + 1) & tankMask[i];
              v = td + tankGC * tin; }
            dampZ[i] = (1.0f - dampC) * v + dampC * dampZ[i];   v = dampZ[i];
            bassZ[i] = (1.0f - bassCoef) * v + bassCoef * bassZ[i];
            float loopG = gLineC[i]   + (FREEZE_G - gLineC[i])   * freezeCur;
            float lowG  = lowGainC[i] + (FREEZE_G - lowGainC[i]) * freezeCur;
            v = (v - bassZ[i]) + bassZ[i] * lowG;
            sIn[i] = loopG * v;
            lfoPh[i] += modInc; if (lfoPh[i] >= 1.0f) lfoPh[i] -= 1.0f;
        }
        float m[N]; for (int i = 0; i < N; ++i) m[i] = sIn[i];
        fwht8 (m);
        const float hs = 0.35355339f;
        const float inG = 0.5f * (1.0f - freezeCur);
        for (int i = 0; i < N; ++i)
        {
            float w = inG * xt + hs * m[i];
            line[i][(size_t) lineWr[i]] = flush (w);
            lineWr[i] = (lineWr[i] + 1) & lineMask[i];
        }
        float tL = (sIn[0] + sIn[2] + sIn[4] + sIn[6]) * 0.5f;
        float tR = (sIn[1] + sIn[3] + sIn[5] + sIn[7]) * 0.5f;

        // ── mix ER + tail, then M/S width + DC block ──
        float wl = erL + tailGainC * tL;
        float wr = erR + tailGainC * tR;
        float mid = 0.5f * (wl + wr), sid = 0.5f * (wl - wr) * (0.15f + 1.85f * widthC);
        wl = mid + sid; wr = mid - sid;
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
        int di = (int) d; float fr = d - (float) di;
        float a = buf[(size_t) ((wr - di)     & mask)];
        float b = buf[(size_t) ((wr - di - 1) & mask)];
        return a + (b - a) * fr;
    }
    static inline void fwht8 (float* a)
    {
        for (int len = 1; len < 8; len <<= 1)
            for (int i = 0; i < 8; i += (len << 1))
                for (int j = i; j < i + len; ++j)
                { float u = a[j], v = a[j + len]; a[j] = u + v; a[j + len] = u - v; }
    }
    static inline float fastSin (float ph01) { return std::sin (2.0f * PI * ph01); }
    inline float rand11()
    {
        rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
        return (float) ((rngState >> 8) & 0xFFFFFFu) * (1.0f / 8388607.5f) - 1.0f;
    }

    // Build the PENDING ER tap tables for a Shape (times in ms→base samples, gains incl. pan).
    void buildTaps (int sh)
    {
        const ShapeSpec ss = SHAPE[sh];
        const float sr1000 = fs / 1000.0f;
        nActiveP = ss.nActive;
        for (int k = 0; k < ET; ++k)
        {
            float tms = baseTapMs[k] * ss.timeScale;
            float pan = basePan[k] * ss.panSpread;
            if (sh == 4) { tms *= (1.0f + 0.28f * jitT[k]); pan += 0.32f * jitP[k]; }   // Scatter = irregular room
            pan = clampf (pan, -1.0f, 1.0f);
            tapBaseSampP[k] = tms * sr1000;
            const float sign = (k & 1) ? -1.0f : 1.0f;
            const float g = 0.72f * std::pow (ss.decayExp, (float) k) * sign;
            tapGLP[k] = g * std::sqrt (0.5f * (1.0f - pan));   // constant-power pan
            tapGRP[k] = g * std::sqrt (0.5f * (1.0f + pan));
        }
    }
    void commitTaps()
    {
        nActive = nActiveP;
        for (int k = 0; k < ET; ++k) { tapBaseSamp[k] = tapBaseSampP[k]; tapGL[k] = tapGLP[k]; tapGR[k] = tapGRP[k]; }
    }

    static constexpr float PI = 3.14159265358979f;
    static constexpr float FREEZE_G = 0.9999f;
    // ROOM FDN lines: short (~11–32 ms @48k), mutually prime → dense/boxy tail.
    static constexpr float baseLen48[N] = { 557.f, 653.f, 761.f, 887.f, 1013.f, 1187.f, 1367.f, 1531.f };
    static constexpr float apLen29k[4]  = { 142.f, 107.f, 379.f, 277.f };
    static constexpr float apBaseG[4]   = { 0.75f, 0.75f, 0.625f, 0.625f };
    static constexpr float tankLen48[N] = { 113.f, 157.f, 197.f, 239.f, 131.f, 179.f, 223.f, 271.f };
    // Early-reflection base pattern (ms @ Size 0.5 / Shape ref): irregular, coprime-ish.
    static constexpr float baseTapMs[ET] = { 6.9f, 10.7f, 14.3f, 18.1f, 21.7f, 25.9f, 30.1f, 34.7f,
                                             39.3f, 44.9f, 50.3f, 56.1f, 62.9f, 69.7f, 77.3f, 85.9f };
    static constexpr float basePan[ET]   = { -0.12f, 0.17f, -0.22f, 0.27f, -0.32f, 0.37f, -0.42f, 0.47f,
                                             -0.52f, 0.57f, -0.62f, 0.67f, -0.72f, 0.77f, -0.82f, 0.87f };
    // Shape = ER pattern: timeScale, panSpread, decayExp, nActive.
    struct ShapeSpec { float timeScale, panSpread, decayExp; int nActive; };
    static constexpr ShapeSpec SHAPE[6] = {
        /* Tight   */ { 0.55f, 0.45f, 0.78f, 12 },
        /* Wide    */ { 1.00f, 1.35f, 0.86f, 14 },
        /* Long    */ { 1.75f, 0.95f, 0.90f, 16 },
        /* Diffuse */ { 1.05f, 0.75f, 0.94f, 16 },
        /* Scatter */ { 1.20f, 1.15f, 0.84f, 14 },
        /* Sparse  */ { 1.40f, 1.00f, 0.60f,  6 },
    };
    // Room Character voicings (8): decayMul, sizeMul, toneMul, toneAdd, dampMul, dampAdd,
    //   diffFloor, lowMul, widthAdd, modMul, reflMul.
    struct CharBias { float decayMul, sizeMul, toneMul, toneAdd, dampMul, dampAdd, diffFloor, lowMul, widthAdd, modMul, reflMul; };
    static constexpr CharBias CHAR[8] = {
        /* Studio    */ { 1.00f, 1.00f, 1.00f, 0.00f, 1.00f, 0.00f, 0.00f, 1.00f,  0.00f, 1.0f, 1.00f },   // neutral reference (index 0)
        /* Booth     */ { 0.40f, 0.40f, 0.70f, 0.00f, 1.00f, 0.40f, 0.30f, 0.80f, -0.20f, 0.8f, 0.55f },
        /* Bedroom   */ { 0.60f, 0.60f, 0.85f, 0.00f, 1.00f, 0.25f, 0.20f, 1.00f, -0.05f, 0.9f, 0.80f },
        /* Live      */ { 1.15f, 1.10f, 1.40f, 0.05f, 0.50f, 0.00f, 0.40f, 0.90f,  0.10f, 1.1f, 1.40f },
        /* Chamber   */ { 1.50f, 1.40f, 1.00f, 0.00f, 1.00f, 0.00f, 0.70f, 1.20f,  0.10f, 1.0f, 0.70f },
        /* Tile      */ { 1.00f, 0.80f, 1.60f, 0.10f, 0.30f, 0.00f, 0.15f, 0.70f,  0.05f, 1.0f, 1.60f },
        /* Garage    */ { 1.10f, 1.10f, 0.70f, 0.00f, 1.00f, 0.10f, 0.35f, 1.60f,  0.00f, 1.0f, 1.10f },
        /* Warehouse */ { 1.60f, 1.60f, 0.90f, 0.00f, 1.00f, 0.05f, 0.60f, 1.50f,  0.20f, 1.1f, 1.20f },
    };

    float fs = 48000.0f, smth = 0.001f, bassCoef = 0.f, dcR = 0.999f;
    bool  primed = false;
    std::vector<float> line[N]; int lineMask[N] = {0}; int lineWr[N] = {0};
    std::vector<float> ap[4];   int apMask[4] = {0};   int apWr[4] = {0}; int apDelay[4] = {0};
    std::vector<float> tank[N]; int tankMask[N] = {0}; int tankWr[N] = {0}; int tankDelay[N] = {0};
    std::vector<float> pre;     int preMask = 0;       int preWr = 0;
    std::vector<float> er;      int erMask = 0;        int erWr = 0;
    float dampZ[N] = {0}, bassZ[N] = {0}, lfoPh[N] = {0};
    float inLpZ = 0, lcZ = 0, erLpL = 0, erLpR = 0, dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0;
    // targets (block rate) + current (per-sample ramped)
    float baseDelayT[N] = {0}, baseDelayC[N] = {0}, gLineT[N] = {0}, gLineC[N] = {0}, lowGainT[N] = {1}, lowGainC[N] = {1};
    float dampT = 0, dampC = 0, apGT[4] = {0}, apGC[4] = {0}, inLpT = 0, inLpC = 0, erLpT = 0, erLpC = 0, lcT = 0, lcC = 0;
    float tankGT = 0, tankGC = 0, preSampT = 0, preSampC = 0, modSampT = 0, modSampC = 0;
    float widthT = 0.7f, widthC = 0.7f, modInc = 0, rt60 = 1.0f, mixExt = 0.3f;
    float sizeScaleT = 1, sizeScaleC = 1, erGainBaseT = 1, erGainBaseC = 1, tailGainT = 1, tailGainC = 1;
    float spreadT = 0.5f, spreadC = 0.5f;   // fb287 — ER stereo-width (target + ramped)
    float tailPreSamp = 576.f;   // late-field pre-delay (~12 ms), constant → click-free
    float freezeCur = 0, freezeTgt = 0, erGateC = 1, erGateTgt = 1;
    bool  erDipping = false;
    // ER tap tables (active = read in the loop, pending = built on Shape change, committed at the dip)
    float tapBaseSamp[ET] = {0}, tapGL[ET] = {0}, tapGR[ET] = {0};       int nActive = 12;
    float tapBaseSampP[ET] = {0}, tapGLP[ET] = {0}, tapGRP[ET] = {0};    int nActiveP = 12;
    int   shape = 0, shapePending = -1, shapeActive = -1;
    float jitT[ET] = {0}, jitP[ET] = {0};
    std::uint32_t rngState = 0x2545F491u;
    // param state
    int   character = 0;
    bool  modOn = true, freezeOn = false;
    float size = 0.5f, decay = 0.45f, tone = 0.5f, preMs = 8.f, diffuse = 0.6f, modDepth = 0.25f,
          refl = 0.6f, damp = 0.4f, bassMul = 1.0f, lowCut = 20.f, width = 0.7f, spread = 0.5f;   // fb287 — ER Spread
};
