#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// HallReverb — shared 8-line Feedback Delay Network (Jot/Stautner-Puckette) that
// voices the HALL reverb (and becomes the reused core for Room/Basin/Shimmer).
// Clean-room from the reverb build bible (fb275). fb277 — CLICK-FREE rewrite:
//   • CORRECT fractional delay interpolation (integer delay + forward interp) —
//     the fb276 read used the wrong neighbour, so modulation crossed integer
//     boundaries and jumped ~2 samples = continuous crackle. Fixed.
//   • EVERY continuous control is per-sample smoothed toward a target: the delay
//     lengths GLIDE (Size — comb-click law), and gains/coeffs ramp (no zipper).
//   • Low-shelf bass gain precomputed per block (no per-sample pow).
//   • Denormal-flushed recirculating state (belt-and-suspenders under FTZ).
// fb278 — DRAMATIC DIFFUSION: one Schroeder allpass per FDN line INSIDE the
//   feedback loop (Griesinger/Dattorro "tank" diffusion), on top of the 4 input
//   diffusers. Diffusion now spreads echo density across the WHOLE tail, not just
//   the ~30 ms attack: 0% = sparse/fluttery/metallic, 100% = glassy wash. The tank
//   coeff RAMPS per-sample (click-free) and the tank delay folds into the Jot loss
//   so RT60 stays exact (allpasses are unity-gain → decay unchanged).
// fb279 — CHARACTER · MOD MODE · MOD · FREEZE (all wired + dramatic + click-free):
//   • Character (8): Smooth/Random/Vintage/Cathedral/Chamber/Dark/Bright/Ethereal —
//     each a strong bias set on decay/size/tone/damping/diffusion-floor/low-decay/
//     width/mod → a night-and-day tonal personality. Rides the existing per-sample
//     coeff ramps, so switching is click-free.
//   • Mod Mode (6): Off/Subtle/Lush/Chorale scale a sine LFO (depth+rate); Random/
//     Chaos CROSSFADE to a smoothed per-line random walk (shapeMix ramps → no click).
//   • Mod (front toggle): master gate for the modulation (static ↔ moving).
//   • Freeze (front toggle): feedback → ~unity + tank input cut → infinite held pad.
//     Ramped grab (freezeCur), guaranteed stable (loop gain < 1, unity-gain allpasses).
// PURE C++ (no JUCE): offline-validates standalone AND drops into the voice path.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>

class HallReverb
{
public:
    static constexpr int N = 8;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const float sr = fs / 48000.0f;
        for (int i = 0; i < N; ++i)
        {
            const int need = (int) std::ceil (baseLen48[i] * sr * 1.8f) + 64;
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
        for (int i = 0; i < N; ++i)   // fb278 — per-line in-loop tank diffuser (short, mutually prime)
        {
            const int len = std::max (8, (int) std::lround (tankLen48[i] * sr));
            int sz = 1; while (sz < len + 4) sz <<= 1;
            tank[i].assign ((size_t) sz, 0.0f); tankMask[i] = sz - 1; tankWr[i] = 0; tankDelay[i] = len;
        }
        { int need = (int) std::ceil (0.25f * fs) + 8; int sz = 1; while (sz < need) sz <<= 1;
          pre.assign ((size_t) sz, 0.0f); preMask = sz - 1; preWr = 0; }
        for (int i = 0; i < N; ++i) { oldRand[i] = rand11(); newRand[i] = rand11(); }   // fb279 — seed random walk
        smth = 1.0f - std::exp (-1.0f / (0.015f * fs));   // ~15 ms per-sample smoothing
        bassCoef = std::exp (-2.0f * PI * 400.0f / fs);
        dcR = 1.0f - (126.0f / fs);
        reset();
        primed = false;
        updateCoefficients();   // computes targets; primes current == target
    }

    void reset()
    {
        for (int i = 0; i < N; ++i) { std::fill (line[i].begin(), line[i].end(), 0.0f); dampZ[i] = bassZ[i] = 0.0f; }
        for (int i = 0; i < 4; ++i)  std::fill (ap[i].begin(), ap[i].end(), 0.0f);
        for (int i = 0; i < N; ++i)  std::fill (tank[i].begin(), tank[i].end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        inLpZ = lcZ = 0.0f; dcxL = dcyL = dcxR = dcyR = 0.0f;
    }

    // ── params (0..1 unless noted); call updateCoefficients() after a batch ──
    void setSize        (float v) { size    = clamp01 (v); }
    void setDecay       (float v) { decay   = clamp01 (v); }
    void setTone        (float v) { tone    = clamp01 (v); }
    void setPreDelayMs  (float ms){ preMs   = clampf (ms, 0.0f, 250.0f); }
    void setDiffusion   (float v) { diffuse = clamp01 (v); }
    void setModDepth    (float v) { modDepth= clamp01 (v); }
    void setModRate     (float hz){ modRate = clampf (hz, 0.01f, 8.0f); }
    void setHighDamping (float v) { hiDamp  = clamp01 (v); }
    void setLowDecay    (float m) { lowDecay= clampf (m, 0.25f, 2.0f); }
    void setLowCutHz    (float hz){ lowCut  = clampf (hz, 20.0f, 1000.0f); }
    void setWidth       (float v) { width   = clamp01 (v); }
    void setCharacter   (int c)   { character = c < 0 ? 0 : (c > 7 ? 7 : c); }   // fb279
    void setModMode     (int m)   { modMode   = m < 0 ? 0 : (m > 5 ? 5 : m); }   // Off..Chaos
    void setModEnabled  (bool on) { modOn   = on; }                              // front Mod toggle
    void setFreeze      (bool f)  { freezeOn = f; }                              // front Freeze toggle

    void updateCoefficients()
    {
        const CharBias cb = CHAR[character];   // fb279 — Character voicing biases
        rt60 = 0.3f * std::pow (20.0f / 0.3f, decay) * cb.decayMul;
        const float sizeScale   = (0.5f + 1.3f * size) * cb.sizeMul;
        const float diffEff     = std::max (diffuse, cb.diffFloor);                       // Character raises density floor
        const float lowDecayEff = clampf (lowDecay * cb.lowMul, 0.25f, 3.0f);
        const float sr = fs / 48000.0f;
        const float lowPow = 1.0f / lowDecayEff - 1.0f;
        for (int i = 0; i < N; ++i)
        {
            float d = baseLen48[i] * sr * sizeScale;
            if (d > (float) lineMask[i] - 30.0f) d = (float) lineMask[i] - 30.0f;
            baseDelayT[i] = d;
            // the in-loop tank diffuser adds tankDelay to the round-trip; fold it into the
            // Jot loss so RT60 stays exact regardless of diffusion (allpass = unity gain).
            float dLoop = d + (float) tankDelay[i];
            float g = std::pow (10.0f, -3.0f * dLoop / (fs * rt60));
            if (g > 0.9995f) g = 0.9995f;
            gLineT[i]   = g;
            lowGainT[i] = std::pow (g, lowPow);   // low-shelf extra gain -> low RT60 = RT60*lowDecay
        }
        dampT = 0.9f * clamp01 (hiDamp * cb.dampMul + cb.dampAdd);                        // Character tilts HF damping
        for (int i = 0; i < 4; ++i) apGT[i] = diffEff * apBaseG[i];
        tankGT = 0.7f * diffEff;   // fb278 — in-loop tank diffusion (0 = pure delay/sparse, 0.7 = glassy)
        float toneEff = clamp01 (tone * cb.toneMul + cb.toneAdd);
        inLpT   = std::exp (-2.0f * PI * (1500.0f * std::pow (12.0f, toneEff)) / fs);     // tone: ~1.5k..18k
        lcT     = std::exp (-2.0f * PI * lowCut / fs);
        preSampT= preMs * 0.001f * fs;
        // ── modulation: Mod Mode scales depth+rate + selects sine vs random shape;
        //    front Mod gates it; Character scales depth (modMul). ──
        // fb283 — DEEPER + more distinct modes (movement was barely above the static floor). Each mode is
        // its own character: Off=static · Subtle=slow shimmer · Lush=classic wide chorus · Chorale=deep+slow
        // ensemble detune · Random=evolving random shimmer · Chaos=fast wild warble.
        static constexpr float mDepth[6] = { 0.0f, 0.50f, 1.15f, 2.00f, 1.60f, 3.00f };   // Off,Subtle,Lush,Chorale,Random,Chaos
        static constexpr float mRate [6] = { 1.0f, 0.55f, 1.10f, 0.38f, 0.95f, 2.40f };
        float depth01 = modOn ? clampf (modDepth * mDepth[modMode] * cb.modMul, 0.0f, 3.0f) : 0.0f;
        modSampT    = depth01 * 50.0f;                       // peak excursion (samples); deep enough to clearly hear
        modInc      = (modRate * mRate[modMode]) / fs;       // phase-continuous (no click)
        shapeMixTgt = (modMode >= 4) ? 1.0f : 0.0f;          // Random/Chaos = smoothed random walk
        widthT      = clamp01 (width + cb.widthAdd);
        freezeTgt   = freezeOn ? 1.0f : 0.0f;                // fb279 — infinite hold target
        if (! primed)   // first call after prepare: snap current == target (no start-up swell)
        {
            for (int i = 0; i < N; ++i) { baseDelayC[i] = baseDelayT[i]; gLineC[i] = gLineT[i]; lowGainC[i] = lowGainT[i]; }
            dampC = dampT; for (int i = 0; i < 4; ++i) apGC[i] = apGT[i];
            inLpC = inLpT; lcC = lcT; preSampC = preSampT; modSampC = modSampT; widthC = widthT; tankGC = tankGT;
            shapeMix = shapeMixTgt; freezeCur = freezeTgt;
            primed = true;
        }
    }

    // stereo in-place with internal equal-power mix (dry+wet). (Terrain drives mix
    // externally via processSample; process() kept for standalone use.)
    void process (float* L, float* R, int n)
    {
        const float dg = std::cos (0.5f * PI * mixExt), wg = std::sin (0.5f * PI * mixExt);
        for (int s = 0; s < n; ++s) { float wl, wr; processSample (L[s], R[s], wl, wr); L[s] = dg * L[s] + wg * wl; R[s] = dg * R[s] + wg * wr; }
    }
    void setMix (float v) { mixExt = clamp01 (v); }

    // wet-only per-sample (Terrain mixes dry/wet in the processor)
    inline void processSample (float inL, float inR, float& wetL, float& wetR)
    {
        // ── per-sample smoothing: glide delays, ramp coeffs (no zipper / comb click) ──
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
        shapeMix += (shapeMixTgt - shapeMix) * smth;   // fb279 — sine<->random crossfade (click-free)
        freezeCur+= (freezeTgt  - freezeCur)* smth;    // fb279 — freeze grab/release (click-free)

        float x = 0.5f * (inL + inR);

        // pre-delay (write current, then fractional read — di may be 0)
        pre[(size_t) preWr] = x;
        x = readFrac (pre, preMask, preWr, preSampC);
        preWr = (preWr + 1) & preMask;

        // input tone LP + low-cut HP
        inLpZ = (1.0f - inLpC) * x + inLpC * inLpZ;   x = inLpZ;
        lcZ   = (1.0f - lcC)   * x + lcC   * lcZ;      x = x - lcZ;

        // 4 series Schroeder input-diffusion allpasses (integer delay)
        for (int i = 0; i < 4; ++i)
        {
            float d = ap[i][(size_t) ((apWr[i] - apDelay[i]) & apMask[i])];
            float in = x - apGC[i] * d;
            ap[i][(size_t) apWr[i]] = in;
            apWr[i] = (apWr[i] + 1) & apMask[i];
            x = d + apGC[i] * in;
        }

        // ── FDN: read (correct frac interp) -> HF damping -> bass shelf -> Jot loss ──
        float sIn[N];
        for (int i = 0; i < N; ++i)
        {
            // modulation excursion: crossfade sine <-> smoothed per-line random walk (Mod Mode).
            float ph = lfoPh[i];
            float sineV  = fastSin (ph);
            float ss     = ph * ph * (3.0f - 2.0f * ph);                  // smoothstep across the cycle
            float noiseV = oldRand[i] + (newRand[i] - oldRand[i]) * ss;
            float exc = modSampC * ((1.0f - shapeMix) * sineV + shapeMix * noiseV);
            float dd  = baseDelayC[i] + exc;
            if (dd < 1.0f) dd = 1.0f;
            float maxd = (float) lineMask[i] - 4.0f; if (dd > maxd) dd = maxd;   // OOB guard (wild Chaos)
            float v = readFrac (line[i], lineMask[i], lineWr[i], dd);
            // fb278 — in-loop tank diffusion allpass (Griesinger): smears every recirculation,
            // so density builds across the whole tail. Unity-gain → RT60 untouched. Ramped coeff.
            {
                float td  = tank[i][(size_t) ((tankWr[i] - tankDelay[i]) & tankMask[i])];
                float tin = v - tankGC * td;
                tank[i][(size_t) tankWr[i]] = flush (tin);
                tankWr[i] = (tankWr[i] + 1) & tankMask[i];
                v = td + tankGC * tin;
            }
            dampZ[i] = (1.0f - dampC) * v + dampC * dampZ[i];   v = dampZ[i];
            bassZ[i] = (1.0f - bassCoef) * v + bassCoef * bassZ[i];   // low band
            // fb279 — Freeze pushes loop loss + bass gain toward unity so the tail holds forever.
            float loopG = gLineC[i]   + (FREEZE_G - gLineC[i])   * freezeCur;
            float lowG  = lowGainC[i] + (FREEZE_G - lowGainC[i]) * freezeCur;
            v = (v - bassZ[i]) + bassZ[i] * lowG;                     // high + shelved low
            sIn[i] = loopG * v;
            // advance phase; on wrap pick a fresh random target (decorrelated per line)
            lfoPh[i] += modInc;
            if (lfoPh[i] >= 1.0f) { lfoPh[i] -= 1.0f; oldRand[i] = newRand[i]; newRand[i] = rand11(); }
        }
        // lossless mix (fast Walsh-Hadamard) * 1/sqrt(8)
        float m[N]; for (int i = 0; i < N; ++i) m[i] = sIn[i];
        fwht8 (m);
        const float hs = 0.35355339f;
        const float inG = 0.5f * (1.0f - freezeCur);   // fb279 — Freeze cuts tank input → clean infinite hold
        for (int i = 0; i < N; ++i)
        {
            float w = inG * x + hs * m[i];
            line[i][(size_t) lineWr[i]] = flush (w);
            lineWr[i] = (lineWr[i] + 1) & lineMask[i];
        }
        // decorrelated even/odd taps -> L/R, then M/S width
        float wl = (sIn[0] + sIn[2] + sIn[4] + sIn[6]) * 0.5f;
        float wr = (sIn[1] + sIn[3] + sIn[5] + sIn[7]) * 0.5f;
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
    // correct fractional delay read: integer delay + forward linear interp
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
    inline float rand11()   // fb279 — cheap deterministic xorshift, returns ~[-1,1]
    {
        rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
        return (float) ((rngState >> 8) & 0xFFFFFFu) * (1.0f / 8388607.5f) - 1.0f;
    }

    static constexpr float PI = 3.14159265358979f;
    static constexpr float FREEZE_G = 0.9999f;   // fb279 — freeze loop gain (RT60 ~ tens of min = "infinite", stable)
    static constexpr float baseLen48[N] = { 1699.f, 2003.f, 2399.f, 2699.f, 3011.f, 3347.f, 3701.f, 4099.f };
    static constexpr float apLen29k[4]  = { 142.f, 107.f, 379.f, 277.f };
    static constexpr float apBaseG[4]   = { 0.75f, 0.75f, 0.625f, 0.625f };
    // fb278 — in-loop tank diffuser lengths (samples @48k): short, mutually prime, < smallest main line
    static constexpr float tankLen48[N] = { 113.f, 157.f, 197.f, 239.f, 131.f, 179.f, 223.f, 271.f };
    // fb279 — Character voicing biases (8): decayMul, sizeMul, toneMul, toneAdd, dampMul, dampAdd,
    //   diffFloor, lowMul, widthAdd, modMul. Smooth = neutral reference (index 0).
    struct CharBias { float decayMul, sizeMul, toneMul, toneAdd, dampMul, dampAdd, diffFloor, lowMul, widthAdd, modMul; };
    static constexpr CharBias CHAR[8] = {
        /* Smooth    */ { 1.00f, 1.00f, 1.00f, 0.00f, 1.00f, 0.00f, 0.00f, 1.00f,  0.00f, 1.0f },
        /* Random    */ { 1.00f, 1.05f, 1.00f, 0.00f, 1.00f, 0.05f, 0.00f, 1.00f,  0.10f, 1.5f },
        /* Vintage   */ { 0.85f, 0.95f, 0.50f, 0.00f, 1.00f, 0.28f, 0.15f, 1.30f, -0.10f, 1.3f },
        /* Cathedral */ { 1.60f, 1.30f, 1.10f, 0.05f, 1.00f,-0.05f, 0.72f, 1.50f,  0.15f, 1.0f },
        /* Chamber   */ { 0.70f, 0.75f, 1.00f, 0.00f, 1.00f, 0.05f, 0.00f, 0.80f, -0.05f, 0.9f },
        /* Dark      */ { 1.00f, 1.00f, 0.35f, 0.00f, 1.00f, 0.42f, 0.00f, 1.20f,  0.00f, 1.0f },
        /* Bright    */ { 1.10f, 1.00f, 1.70f, 0.15f, 0.30f, 0.00f, 0.00f, 0.70f,  0.10f, 1.0f },
        /* Ethereal  */ { 1.40f, 1.10f, 1.30f, 0.05f, 0.70f, 0.00f, 0.72f, 0.90f,  0.20f, 2.2f },
    };

    float fs = 48000.0f, smth = 0.001f, bassCoef = 0.f, dcR = 0.999f;
    bool  primed = false;
    std::vector<float> line[N]; int lineMask[N] = {0}; int lineWr[N] = {0};
    std::vector<float> ap[4];   int apMask[4] = {0};   int apWr[4] = {0}; int apDelay[4] = {0};
    std::vector<float> tank[N]; int tankMask[N] = {0}; int tankWr[N] = {0}; int tankDelay[N] = {0};  // fb278
    std::vector<float> pre;     int preMask = 0;       int preWr = 0;
    float dampZ[N] = {0}, bassZ[N] = {0}, lfoPh[N] = {0};
    float inLpZ = 0, lcZ = 0, dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0;
    // targets (block rate) + current (per-sample ramped)
    float baseDelayT[N] = {0}, baseDelayC[N] = {0}, gLineT[N] = {0}, gLineC[N] = {0}, lowGainT[N] = {1}, lowGainC[N] = {1};
    float dampT = 0, dampC = 0, apGT[4] = {0}, apGC[4] = {0}, inLpT = 0, inLpC = 0, lcT = 0, lcC = 0;
    float tankGT = 0, tankGC = 0;   // fb278 — in-loop tank diffusion coeff (target / per-sample ramped)
    float preSampT = 0, preSampC = 0, modSampT = 0, modSampC = 0, widthT = 0.8f, widthC = 0.8f, modInc = 0, rt60 = 3.0f, mixExt = 0.3f;
    // fb279 — Character / Mod Mode / Mod / Freeze runtime state
    int   character = 0, modMode = 2;                    // Smooth / Lush (defaults)
    bool  modOn = true, freezeOn = false;
    float shapeMix = 0, shapeMixTgt = 0;                 // 0 = sine, 1 = random walk (ramped crossfade)
    float freezeCur = 0, freezeTgt = 0;                  // infinite-hold amount (ramped grab/release)
    float oldRand[N] = {0}, newRand[N] = {0};            // per-line random-walk endpoints
    std::uint32_t rngState = 0x9E3779B9u;                // xorshift seed (deterministic)
    // param state
    float size = 0.3f, decay = 0.55f, tone = 0.5f, preMs = 20.f, diffuse = 0.7f, modDepth = 0.25f,
          modRate = 0.4f, hiDamp = 0.35f, lowDecay = 1.0f, lowCut = 20.f, width = 0.8f;
};
