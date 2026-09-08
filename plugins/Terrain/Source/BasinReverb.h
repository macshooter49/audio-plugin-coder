#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BasinReverb — the BASIN reverb type (fb289): a HUGE, DARK, deeply-modulated ambient
// WASH (Serum-2 "Basin" family). Clean-room from the reverb build bible: the validated
// Hall 8-line Jot/Hadamard FDN core, RETUNED into a vast dark cavern —
//   • HUGE — long lines + a big Size range + a long decay range (cathedral → cavern → 40 s).
//   • DARK — heavier default HF damping + a darker input band-limit + a bold output tone
//     TILT + a SIZE-linked darkening (a bigger basin swallows more highs, like a real cave).
//   • BASS-SAFE CROSSOVER < 1 (the signature) — the low band decays FASTER than the mids by
//     default (Bass Decay knob sweeps very-safe → neutral → bloom), so a giant dark reverb
//     stays clean and never turns to low-end mud. Inherently stable (low per-pass gain < 1).
//   • DEEP MOTION — a deeper, slower evolving modulation (sine ↔ per-line random walk) for
//     the lush, seasick, endlessly-drifting wash. Freeze = an infinite dark pad.
// Every continuous control glides per-sample (delays GLIDE, coeffs ramp, freeze grabs) so
// sweeps are click-free; the recirculating loop gain stays < 1 so it's unconditionally stable.
// PURE C++ (no JUCE): offline-validates standalone AND drops into the voice path.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>

class BasinReverb
{
public:
    static constexpr int N = 8;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const float sr = fs / 48000.0f;
        for (int i = 0; i < N; ++i)
        {
            const int need = (int) std::ceil (baseLen48[i] * sr * 4.0f) + 96;   // headroom for the big Size + deep mod
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
        { int need = (int) std::ceil (0.25f * fs) + 8; int sz = 1; while (sz < need) sz <<= 1;
          pre.assign ((size_t) sz, 0.0f); preMask = sz - 1; preWr = 0; }
        for (int i = 0; i < N; ++i) { oldRand[i] = rand11(); newRand[i] = rand11(); }
        smth = 1.0f - std::exp (-1.0f / (0.015f * fs));
        bassCoef = std::exp (-2.0f * PI * 300.0f / fs);         // bass crossover ~300 Hz
        dcR = 1.0f - (126.0f / fs);
        tiltCoef = 1.0f - std::exp (-2.0f * PI * 1100.0f / fs); // output brightness-tilt split ~1.1 kHz
        reset();
        primed = false;
        updateCoefficients();
    }

    void reset()
    {
        for (int i = 0; i < N; ++i) { std::fill (line[i].begin(), line[i].end(), 0.0f); dampZ[i] = bassZ[i] = 0.0f; }
        for (int i = 0; i < 4; ++i)  std::fill (ap[i].begin(), ap[i].end(), 0.0f);
        for (int i = 0; i < N; ++i)  std::fill (tank[i].begin(), tank[i].end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        inLpZ = lcZ = dcxL = dcyL = dcxR = dcyR = tiltLpL = tiltLpR = 0.0f;
    }

    // ── params (0..1 unless noted); call updateCoefficients() after a batch ──
    void setSize        (float v) { size    = clamp01 (v); }
    void setDecay       (float v) { decay   = clamp01 (v); }
    void setTone        (float v) { tone    = clamp01 (v); }
    void setPreDelayMs  (float ms){ preMs   = clampf (ms, 0.0f, 250.0f); }
    void setDiffusion   (float v) { diffuse = clamp01 (v); }
    void setModDepth    (float v) { modDepth= clamp01 (v); }
    void setModRate     (float hz){ modRate = clampf (hz, 0.01f, 8.0f); }
    void setDamping     (float v) { damp    = clamp01 (v); }        // HIDAMP slot → HF damping (how dark)
    void setBassDecay   (float m) { bassMul = clampf (m, 0.15f, 3.0f); }   // LOWDECAY slot → bass-safe (<1) ↔ bloom (>1)
    void setLowCutHz    (float hz){ lowCut  = clampf (hz, 20.0f, 1000.0f); }
    void setWidth       (float v) { width   = clamp01 (v); }
    void setCharacter   (int c)   { character = c < 0 ? 0 : (c > 7 ? 7 : c); }   // 8 basin voicings
    void setMotion      (int m)   { motion    = m < 0 ? 0 : (m > 5 ? 5 : m); }   // 6 modulation modes
    void setModEnabled  (bool on) { modOn   = on; }
    void setFreeze      (bool f)  { freezeOn = f; }
    void setMix         (float v) { mixExt  = clamp01 (v); }

    void updateCoefficients()
    {
        const CharBias cb = CHAR[character];
        // HUGE: long RT60 range (0.5 s … ~40 s) + Size ALSO extends the tail (a bigger space rings longer)
        // + Character extends it further.
        rt60 = 0.5f * std::pow (80.0f, decay) * cb.decayMul * (0.5f + 1.0f * size);
        const float sizeScale = (0.8f + 2.6f * size) * cb.sizeMul;   // cavernous
        const float diffEff   = std::max (diffuse, cb.diffFloor);
        // BASS-SAFE crossover: low band per-pass = g^(1/lowDecayEff). lowDecayEff<1 ⇒ lows decay FASTER
        // than mid (bass-safe, no mud) — always <1 ⇒ stable. Default lands well below 1 (the Basin identity).
        const float lowDecayEff = clampf (bassMul * cb.lowMul, 0.15f, 4.0f);
        const float sr = fs / 48000.0f;
        const float lowPow = 1.0f / lowDecayEff - 1.0f;
        for (int i = 0; i < N; ++i)
        {
            float d = baseLen48[i] * sr * sizeScale;
            if (d > (float) lineMask[i] - 30.0f) d = (float) lineMask[i] - 30.0f;
            baseDelayT[i] = d;
            float dLoop = d + (float) tankDelay[i];
            float g = std::pow (10.0f, -3.0f * dLoop / (fs * rt60));
            if (g > 0.9997f) g = 0.9997f;
            gLineT[i]   = g;
            lowGainT[i] = std::pow (g, lowPow);
        }
        // DARK (Basin's identity — clearly darker than Hall): heavy base damping + a SIZE-linked darkening
        // (a bigger basin swallows more highs, like a real cave).
        float dampEff = clamp01 (0.33f + damp * 0.66f * cb.dampMul + cb.dampAdd + 0.16f * size);
        dampT = 0.95f * dampEff;
        for (int i = 0; i < 4; ++i) apGT[i] = diffEff * apBaseG[i];
        tankGT = 0.72f * diffEff;
        // Tone: a DARKER input band-limit than Hall + a bold OUTPUT tilt that LEANS DARK even at Tone 0.5,
        // so Basin reads dark by default and Tone still sweeps bright↔dark night-and-day.
        float toneEff = clamp01 (tone * cb.toneMul + cb.toneAdd);
        inLpT = std::exp (-2.0f * PI * (600.0f * std::pow (15.0f, toneEff)) / fs);   // ~600 Hz … 9 kHz (dark-leaning)
        float tilt = (toneEff - 0.5f) * 2.0f - 0.30f;   // −0.3 dark bias
        hiGainT = std::pow (3.4f, tilt); loGainT = std::pow (1.9f, -tilt);
        lcT = std::exp (-2.0f * PI * lowCut / fs);
        preSampT = preMs * 0.001f * fs;
        // DEEP MOTION — deeper + slower than Hall for the evolving wash. Motion selects depth/rate + sine↔random.
        static constexpr float mDepth[6] = { 0.0f, 0.60f, 1.30f, 2.20f, 1.70f, 3.20f };   // Still,Drift,Swell,Deep,Tidal,Chaos
        static constexpr float mRate [6] = { 1.0f, 0.35f, 0.70f, 0.28f, 0.50f, 1.80f };
        float depth01 = modOn ? clampf (modDepth * mDepth[motion] * cb.modMul, 0.0f, 3.2f) : 0.0f;
        modSampT    = depth01 * 70.0f;                        // deep excursion (samples) — lush, seasick
        modInc      = (modRate * mRate[motion]) / fs;
        shapeMixTgt = (motion >= 4) ? 1.0f : 0.0f;            // Tidal/Chaos = random walk
        widthT      = clamp01 (width + cb.widthAdd);
        freezeTgt   = freezeOn ? 1.0f : 0.0f;
        if (! primed)
        {
            for (int i = 0; i < N; ++i) { baseDelayC[i] = baseDelayT[i]; gLineC[i] = gLineT[i]; lowGainC[i] = lowGainT[i]; }
            dampC = dampT; for (int i = 0; i < 4; ++i) apGC[i] = apGT[i];
            inLpC = inLpT; lcC = lcT; preSampC = preSampT; modSampC = modSampT; widthC = widthT; tankGC = tankGT;
            hiGainC = hiGainT; loGainC = loGainT; shapeMix = shapeMixTgt; freezeCur = freezeTgt;
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
        for (int i = 0; i < N; ++i)
        {
            baseDelayC[i] += (baseDelayT[i] - baseDelayC[i]) * smth;
            gLineC[i]     += (gLineT[i]     - gLineC[i])     * smth;
            lowGainC[i]   += (lowGainT[i]   - lowGainC[i])   * smth;
        }
        dampC += (dampT - dampC) * smth;
        for (int i = 0; i < 4; ++i) apGC[i] += (apGT[i] - apGC[i]) * smth;
        tankGC  += (tankGT  - tankGC)  * smth;
        inLpC   += (inLpT   - inLpC)   * smth;
        lcC     += (lcT     - lcC)     * smth;
        preSampC+= (preSampT- preSampC)* smth;
        modSampC+= (modSampT- modSampC)* smth;
        widthC  += (widthT  - widthC)  * smth;
        hiGainC += (hiGainT - hiGainC) * smth;
        loGainC += (loGainT - loGainC) * smth;
        shapeMix += (shapeMixTgt - shapeMix) * smth;
        freezeCur+= (freezeTgt  - freezeCur)* smth;

        float x = 0.5f * (inL + inR);
        pre[(size_t) preWr] = x; x = readFrac (pre, preMask, preWr, preSampC); preWr = (preWr + 1) & preMask;
        inLpZ = (1.0f - inLpC) * x + inLpC * inLpZ;   x = inLpZ;
        lcZ   = (1.0f - lcC)   * x + lcC   * lcZ;      x = x - lcZ;
        for (int i = 0; i < 4; ++i)
        {
            float d = ap[i][(size_t) ((apWr[i] - apDelay[i]) & apMask[i])];
            float in = x - apGC[i] * d;
            ap[i][(size_t) apWr[i]] = in;
            apWr[i] = (apWr[i] + 1) & apMask[i];
            x = d + apGC[i] * in;
        }

        float sIn[N];
        for (int i = 0; i < N; ++i)
        {
            float ph = lfoPh[i];
            float sineV  = fastSin (ph);
            float ss     = ph * ph * (3.0f - 2.0f * ph);
            float noiseV = oldRand[i] + (newRand[i] - oldRand[i]) * ss;
            float exc = modSampC * ((1.0f - shapeMix) * sineV + shapeMix * noiseV);
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
            float lowG  = lowGainC[i] + (FREEZE_G - lowGainC[i]) * freezeCur;   // freeze → lows hold too (bass-safe gain <1 so no divergence)
            v = (v - bassZ[i]) + bassZ[i] * lowG;
            sIn[i] = loopG * v;
            lfoPh[i] += modInc;
            if (lfoPh[i] >= 1.0f) { lfoPh[i] -= 1.0f; oldRand[i] = newRand[i]; newRand[i] = rand11(); }
        }
        float m[N]; for (int i = 0; i < N; ++i) m[i] = sIn[i];
        fwht8 (m);
        const float hs = 0.35355339f;
        const float inG = 0.5f * (1.0f - freezeCur);
        for (int i = 0; i < N; ++i)
        {
            float w = inG * x + hs * m[i];
            line[i][(size_t) lineWr[i]] = flush (w);
            lineWr[i] = (lineWr[i] + 1) & lineMask[i];
        }
        float wl = (sIn[0] + sIn[2] + sIn[4] + sIn[6]) * 0.5f;
        float wr = (sIn[1] + sIn[3] + sIn[5] + sIn[7]) * 0.5f;
        // bold output tone tilt (identity at tone 0.5) → bright↔dark night-and-day
        tiltLpL += (wl - tiltLpL) * tiltCoef; { float hi = wl - tiltLpL; wl = tiltLpL * loGainC + hi * hiGainC; }
        tiltLpR += (wr - tiltLpR) * tiltCoef; { float hi = wr - tiltLpR; wr = tiltLpR * loGainC + hi * hiGainC; }
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
    inline float rand11() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (float) ((rngState >> 8) & 0xFFFFFFu) * (1.0f / 8388607.5f) - 1.0f; }

    static constexpr float PI = 3.14159265358979f;
    static constexpr float FREEZE_G = 0.9999f;
    // BASIN FDN lines — longer than Hall (cavernous), mutually coprime-ish primes (samples @48k).
    static constexpr float baseLen48[N] = { 2549.f, 3001.f, 3593.f, 4051.f, 4517.f, 5023.f, 5557.f, 6143.f };
    static constexpr float apLen29k[4]  = { 142.f, 107.f, 379.f, 277.f };
    static constexpr float apBaseG[4]   = { 0.75f, 0.75f, 0.625f, 0.625f };
    static constexpr float tankLen48[N] = { 149.f, 211.f, 263.f, 317.f, 173.f, 229.f, 281.f, 349.f };
    // 8 BASIN voicings: decayMul, sizeMul, toneMul, toneAdd, dampMul, dampAdd, diffFloor, lowMul, widthAdd, modMul.
    struct CharBias { float decayMul, sizeMul, toneMul, toneAdd, dampMul, dampAdd, diffFloor, lowMul, widthAdd, modMul; };
    static constexpr CharBias CHAR[8] = {
        /* Basin    */ { 1.00f, 1.00f, 1.00f, 0.00f, 1.00f, 0.00f, 0.30f, 1.00f,  0.05f, 1.0f },   // huge dark wash (default)
        /* Cavern   */ { 1.20f, 1.15f, 0.80f, 0.00f, 1.10f, 0.05f, 0.45f, 0.75f,  0.10f, 1.1f },   // rocky, diffuse, darker
        /* Abyss    */ { 1.75f, 1.40f, 0.62f, 0.00f, 1.15f, 0.12f, 0.55f, 0.55f,  0.15f, 1.3f },   // deepest, longest, darkest
        /* Glacier  */ { 1.35f, 1.20f, 1.55f, 0.10f, 0.65f, 0.00f, 0.50f, 0.65f,  0.15f, 1.4f },   // cold, icy, a touch brighter
        /* Chasm    */ { 1.10f, 1.05f, 0.75f, 0.00f, 1.05f, 0.06f, 0.20f, 0.45f,  0.00f, 0.8f },   // narrow, resonant, very bass-safe
        /* Fog      */ { 1.30f, 1.10f, 0.72f, 0.00f, 1.10f, 0.08f, 0.72f, 0.85f,  0.10f, 1.2f },   // soft, misty, dense
        /* Void     */ { 1.60f, 1.35f, 0.90f, 0.00f, 0.90f, 0.00f, 0.10f, 0.60f,  0.20f, 1.6f },   // sparse, huge, evolving
        /* Bloom    */ { 1.25f, 1.10f, 0.85f, 0.00f, 1.00f, 0.00f, 0.40f, 2.20f,  0.05f, 1.0f },   // the warm variant — lets the bass BLOOM
    };

    float fs = 48000.0f, smth = 0.001f, bassCoef = 0.f, dcR = 0.999f, tiltCoef = 0.3f;
    bool  primed = false;
    std::vector<float> line[N]; int lineMask[N] = {0}; int lineWr[N] = {0};
    std::vector<float> ap[4];   int apMask[4] = {0};   int apWr[4] = {0}; int apDelay[4] = {0};
    std::vector<float> tank[N]; int tankMask[N] = {0}; int tankWr[N] = {0}; int tankDelay[N] = {0};
    std::vector<float> pre;     int preMask = 0;       int preWr = 0;
    float dampZ[N] = {0}, bassZ[N] = {0}, lfoPh[N] = {0};
    float inLpZ = 0, lcZ = 0, dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0, tiltLpL = 0, tiltLpR = 0;
    float baseDelayT[N] = {0}, baseDelayC[N] = {0}, gLineT[N] = {0}, gLineC[N] = {0}, lowGainT[N] = {1}, lowGainC[N] = {1};
    float dampT = 0, dampC = 0, apGT[4] = {0}, apGC[4] = {0}, inLpT = 0, inLpC = 0, lcT = 0, lcC = 0;
    float tankGT = 0, tankGC = 0, hiGainT = 1, hiGainC = 1, loGainT = 1, loGainC = 1;
    float preSampT = 0, preSampC = 0, modSampT = 0, modSampC = 0, widthT = 0.85f, widthC = 0.85f, modInc = 0, rt60 = 6.0f, mixExt = 0.3f;
    int   character = 0, motion = 2;                     // Basin / Swell (defaults)
    bool  modOn = true, freezeOn = false;
    float shapeMix = 0, shapeMixTgt = 0, freezeCur = 0, freezeTgt = 0;
    float oldRand[N] = {0}, newRand[N] = {0};
    std::uint32_t rngState = 0x9E3779B9u;
    float size = 0.55f, decay = 0.6f, tone = 0.45f, preMs = 25.f, diffuse = 0.75f, modDepth = 0.4f,
          modRate = 0.4f, damp = 0.4f, bassMul = 0.66f, lowCut = 20.f, width = 0.9f;
};
