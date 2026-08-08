#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DigitalReverb — the DIGITAL reverb type (fb285): the LEXICON 224 (Max's favourite).
// Clean-room from the reverb build bible. The 224 is an allpass-loop / cross-coupled
// figure-8 TANK (Griesinger; canonical via Dattorro), NOT an FDN or convolver — the
// SAME tank family as our Plate, but voiced completely differently:
//
//   pre-delay → input bandwidth LP → low-cut → 4 series input diffusers (142/107/379/277)
//   → TANK (two mirror halves, cross-coupled figure-8):
//       each half = [modulated allpass 672/908] → long delay 4453/4217 → Treble-Decay LP
//                   → dual-band (Bass/Mid) decay → decay-diffusion allpass 1800/2656
//                   → long delay 3720/3163 → ×decay → cross-feed to the OTHER half
//   → 7 alternating-sign output taps per channel (wide decorrelated stereo)
//
// ITS SOUL = TIME-VARYING delays: the 2 tank allpasses AND 2 output taps are modulated by
// RANDOM band-limited-noise motion (Chorus Voicing) so the eigentones never settle — the
// metallic modes dissolve into the lush seasick "chorused" shimmer that IS the 224.
// We modulate delay LENGTH / tap position, NEVER the allpass coefficient (coeff-mod itself
// sounds metallic). Dual-band Bass/Mid decay + crossover = the booming-lows-under-faster-mids
// bloom. A ±1e-4 noise/DC inject doubles as authentic 224 hiss (Random-Vintage grain).
//
// NO metallic dispersion (unlike Plate) — Digital is smooth & lush. Bold Tone tilt makes
// brightness night-and-day. All click-free (per-sample glide, fixed-write-pointer tank) +
// stable (loop gain = decay·allpass·damping-DC < 1; freeze 0.9995). PURE C++ (no JUCE).
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>

class DigitalReverb
{
public:
    static constexpr int NID = 4;   // input diffusers

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        scl = fs / 29761.0f;   // Dattorro's original SR
        alloc (dif1L, dif1Lmask, 672);  alloc (dif1R, dif1Rmask, 908);
        alloc (del1L, del1Lmask, 4453); alloc (del1R, del1Rmask, 4217);
        alloc (dif2L, dif2Lmask, 2656); alloc (dif2R, dif2Rmask, 2656);
        alloc (del2L, del2Lmask, 3720); alloc (del2R, del2Rmask, 3720);
        for (int i = 0; i < NID; ++i) { alloc (id[i], idMask[i], (int) idLen[i]); idWr[i] = 0; idDelay[i] = std::max (4, (int) std::lround (idLen[i] * scl)); }
        { int need = (int) std::ceil (0.22f * fs) + 8; int sz = 1; while (sz < need) sz <<= 1; pre.assign ((size_t) sz, 0.0f); preMask = sz - 1; preWr = 0; }
        smth = 1.0f - std::exp (-1.0f / (0.015f * fs));
        bassCoef = std::exp (-2.0f * PI * 350.0f / fs);
        dcR = 1.0f - (126.0f / fs);
        tiltCoef = 1.0f - std::exp (-2.0f * PI * 1500.0f / fs);
        for (int i = 0; i < 4; ++i) { modPh[i] = 0.13f * i; oldR[i] = rand11(); newR[i] = rand11(); }
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
        dif1Lwr = dif1Rwr = del1Lwr = del1Rwr = dif2Lwr = dif2Rwr = del2Lwr = del2Rwr = preWr = 0;
        inLpZ = lcZ = bassLz = bassRz = dampLz = dampRz = 0.0f; dcxL = dcyL = dcxR = dcyR = 0.0f;
        tiltLpL = tiltLpR = 0.0f;
    }

    // ── params (0..1 unless noted) — mapped from the shared reverb slots ──
    void setSize        (float v) { size    = clamp01 (v); }   // SIZE → scale all tank/diffuser delays
    void setDecay       (float v) { decay   = clamp01 (v); }   // DECAY → Time (tail length)
    void setTone        (float v) { tone    = clamp01 (v); }   // TONE → bright↔dark
    void setPreDelayMs  (float ms){ preMs   = clampf (ms, 0.0f, 200.0f); }
    void setDiffusion   (float v) { diffuse = clamp01 (v); }   // DIFFUSE → density (grainy↔glassy)
    void setModDepth    (float v) { modDepth= clamp01 (v); }   // MODDEPTH → THE 224 chorus excursion
    void setModRate     (float hz){ modRate = clampf (hz, 0.02f, 6.0f); }  // MODRATE → chorus speed
    void setTrebleDecay (float v) { trebDec = clamp01 (v); }   // HIDAMP → in-loop HF decay (dark tail)
    void setBassDecay   (float m) { bassMul = clampf (m, 0.25f, 4.0f); }   // LOWDECAY → LF bloom
    void setLowCutHz    (float hz){ lowCut  = clampf (hz, 20.0f, 1000.0f); }
    void setWidth       (float v) { width   = clamp01 (v); }
    void setCharacter   (int c)   { character = c < 0 ? 0 : (c > 7 ? 7 : c); }   // 8 programs
    void setVoicing     (int m)   { voicing   = m < 0 ? 0 : (m > 5 ? 5 : m); }   // 6 chorus voicings
    void setModEnabled  (bool on) { modOn   = on; }
    void setFreeze      (bool f)  { freezeOn = f; }
    void setMix         (float v) { mixExt  = clamp01 (v); }

    void updateCoefficients()
    {
        const CharBias cb = CHAR[character];
        const VoiceBias vb = VOICE[voicing];
        rt60 = 0.3f * std::pow (20.0f, decay) * cb.decayMul;
        sizeScaleT = (0.4f + 1.4f * size) * cb.sizeMul;                      // Size 0.25×…3.5×-ish
        float dTot = (672.f + 4453.f + 1800.f + 3720.f + 908.f + 4217.f + 2656.f + 3163.f) * scl * sizeScaleT;
        float dc = std::pow (10.0f, -1.5f * dTot / (fs * rt60));
        if (dc > 0.9995f) dc = 0.9995f;
        decayT = dc;
        // dual-band: LF decays at dc^(1/bassMul) (bass bloom = rings longer than mid) — inherently <1 stable
        float bassEff = clampf (bassMul * cb.bassMul, 0.25f, 4.0f);
        lowGainT = std::pow (dc, 1.0f / bassEff - 1.0f);
        // Treble Decay: in-loop LP coeff (more = darker). Program tilts it.
        float dampEff = clamp01 (trebDec * cb.dampMul + cb.dampAdd);
        dampT = 0.02f + 0.93f * dampEff;
        // Tone: input band-limit + bold OUTPUT tilt (identity at toneEff 0.5)
        float toneEff = clamp01 (tone * cb.toneMul + cb.toneAdd);
        inLpT = std::exp (-2.0f * PI * (1400.0f * std::pow (13.0f, toneEff)) / fs);
        float tilt = (toneEff - 0.5f) * 2.0f;
        hiGainT = std::pow (4.0f, tilt); loGainT = std::pow (1.9f, -tilt);
        lcT = std::exp (-2.0f * PI * lowCut / fs);
        // Diffusion: input + decay allpass coeffs (density). Program sets a floor (Halls build; plates dense).
        float diffEff = clamp01 (std::max (diffuse, cb.diffFloor) * cb.diffMul);
        idGT = 0.55f + 0.30f * diffEff;                                     // input diffuser scale
        g1T  = 0.70f * diffEff; g2T = 0.50f * diffEff;                       // decay-diffusion
        // Chorus: Mod Depth × voicing × program; ±~24..60 samples; Voicing sets shape(rnd)/rate/grain.
        float depth = modOn ? clampf (modDepth * vb.depthMul * cb.modMul, 0.0f, 2.5f) : 0.0f;
        modApT = depth * 24.0f;                                             // allpass excursion (samples)
        modTapT= depth * 17.0f;                                            // tap excursion (samples)
        modRndT= vb.rnd; grainT = modOn ? vb.grain : 0.0f;
        for (int i = 0; i < 4; ++i) modIncT[i] = (modRate * vb.rateMul * MODR[i]) / fs;
        gritT  = modOn ? vb.grain * 6.0e-5f : 0.0f;                        // faint 224 hiss (output-only, ~-84 dB)
        widthT = clamp01 (width + cb.widthAdd + vb.widthAdd);
        preSampT = preMs * 0.001f * fs;
        freezeTgt = freezeOn ? 1.0f : 0.0f;
        if (! primed)
        {
            sizeScaleC = sizeScaleT; decayC = decayT; lowGainC = lowGainT; dampC = dampT; inLpC = inLpT; lcC = lcT;
            idGC = idGT; g1C = g1T; g2C = g2T; hiGainC = hiGainT; loGainC = loGainT;
            modApC = modApT; modTapC = modTapT; modRndC = modRndT; grainC = grainT; gritC = gritT;
            for (int i = 0; i < 4; ++i) modIncC[i] = modIncT[i];
            widthC = widthT; preSampC = preSampT; freezeCur = freezeTgt;
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
        // per-sample glide (click-free)
        sizeScaleC += (sizeScaleT - sizeScaleC) * smth; decayC += (decayT - decayC) * smth;
        lowGainC += (lowGainT - lowGainC) * smth; dampC += (dampT - dampC) * smth;
        inLpC += (inLpT - inLpC) * smth; lcC += (lcT - lcC) * smth; idGC += (idGT - idGC) * smth;
        g1C += (g1T - g1C) * smth; g2C += (g2T - g2C) * smth;
        hiGainC += (hiGainT - hiGainC) * smth; loGainC += (loGainT - loGainC) * smth;
        modApC += (modApT - modApC) * smth; modTapC += (modTapT - modTapC) * smth;
        modRndC += (modRndT - modRndC) * smth; grainC += (grainT - grainC) * smth; gritC += (gritT - gritC) * smth;
        for (int i = 0; i < 4; ++i) modIncC[i] += (modIncT[i] - modIncC[i]) * smth;
        widthC += (widthT - widthC) * smth; preSampC += (preSampT - preSampC) * smth;
        freezeCur += (freezeTgt - freezeCur) * smth;
        const float S = sizeScaleC * scl;

        // ── 4 random/sine modulators (voicing shape) → excursions [-1,1] ──
        float exc[4];
        for (int i = 0; i < 4; ++i)
        {
            float ph = modPh[i];
            float sine = fastSin (ph);
            float ss = ph * ph * (3.0f - 2.0f * ph);
            float walk = oldR[i] + (newR[i] - oldR[i]) * ss;
            float e = (1.0f - modRndC) * sine + modRndC * walk;
            if (grainC > 0.01f) { float q = std::round (e * 6.0f) * (1.0f / 6.0f); e += grainC * (q - e); }   // vintage grain
            exc[i] = e;
            modPh[i] += modIncC[i];
            if (modPh[i] >= 1.0f) { modPh[i] -= 1.0f; oldR[i] = newR[i]; newR[i] = rand11(); }
        }

        float x = 0.5f * (inL + inR);
        pre[(size_t) preWr] = x; x = readFrac (pre, preMask, preWr, preSampC); preWr = (preWr + 1) & preMask;
        inLpZ = (1.0f - inLpC) * x + inLpC * inLpZ; x = inLpZ;
        lcZ   = (1.0f - lcC)   * x + lcC   * lcZ;   x = x - lcZ;
        for (int i = 0; i < NID; ++i)
        {
            float g = idG[i] * idGC;
            float d = id[i][(size_t) ((idWr[i] - idDelay[i]) & idMask[i])];
            float in = x - g * d; id[i][(size_t) idWr[i]] = in; idWr[i] = (idWr[i] + 1) & idMask[i];
            x = d + g * in;
        }
        const float xd = x;   // tank input (hiss is added at the OUTPUT so it can't sustain the loop)

        // ── TANK (figure-8). Read half tails, dual-band decay, cross-couple. ──
        float outL = readFrac (del2L, del2Lmask, del2Lwr, 3720.f * S);
        float outR = readFrac (del2R, del2Rmask, del2Rwr, 3163.f * S);
        // dual-band bass shelf. At FREEZE the loop gain (dec) → FREEZE_G, so the low-band boost must ramp to
        // UNITY or dec·lowGain exceeds 1 and the low band DIVERGES (a real freeze-hold blowup). Ramp it to 1.
        const float lowGeff = lowGainC + (1.0f - lowGainC) * freezeCur;
        bassLz = (1.0f - bassCoef) * outL + bassCoef * bassLz; outL = (outL - bassLz) + bassLz * lowGeff;
        bassRz = (1.0f - bassCoef) * outR + bassCoef * bassRz; outR = (outR - bassRz) + bassRz * lowGeff;
        const float dec = decayC + (FREEZE_G - decayC) * freezeCur;
        const float inG = 0.5f * (1.0f - freezeCur);
        float lIn = inG * xd + dec * outR;
        float rIn = inG * xd + dec * outL;

        // LEFT half — modulated allpass (RANDOM delay-length mod) → del1 → treble-decay LP → dif2 → del2
        { float D = 672.f * S + modApC * exc[0]; float d = readFrac (dif1L, dif1Lmask, dif1Lwr, D);
          float in = lIn - g1C * d; dif1L[(size_t) dif1Lwr] = flush (in); lIn = d + g1C * in; }
        dampLz = (1.0f - dampC) * lIn + dampC * dampLz; lIn = dampLz;   // Treble Decay BEFORE del1 → every tap darkens
        { float d = readFrac (del1L, del1Lmask, del1Lwr, 4453.f * S); del1L[(size_t) del1Lwr] = flush (lIn); lIn = d; }
        { float d = readFrac (dif2L, dif2Lmask, dif2Lwr, 1800.f * S); float in = lIn - g2C * d; dif2L[(size_t) dif2Lwr] = flush (in); lIn = d + g2C * in; }
        del2L[(size_t) del2Lwr] = flush (lIn);
        // RIGHT half
        { float D = 908.f * S + modApC * exc[1]; float d = readFrac (dif1R, dif1Rmask, dif1Rwr, D);
          float in = rIn - g1C * d; dif1R[(size_t) dif1Rwr] = flush (in); rIn = d + g1C * in; }
        dampRz = (1.0f - dampC) * rIn + dampC * dampRz; rIn = dampRz;   // Treble Decay BEFORE del1
        { float d = readFrac (del1R, del1Rmask, del1Rwr, 4217.f * S); del1R[(size_t) del1Rwr] = flush (rIn); rIn = d; }
        { float d = readFrac (dif2R, dif2Rmask, dif2Rwr, 2656.f * S); float in = rIn - g2C * d; dif2R[(size_t) dif2Rwr] = flush (in); rIn = d + g2C * in; }
        del2R[(size_t) del2Rwr] = flush (rIn);

        // ── 7 output taps/ch (2 taps MODULATED = the moving 224 shimmer) ──
        float yl = 0.6f * ( readFrac (del1R, del1Rmask, del1Rwr, 266.f*S + modTapC*exc[2]) + readFrac (del1R, del1Rmask, del1Rwr, 2974.f*S)
                          - readFrac (dif2R, dif2Rmask, dif2Rwr, 1913.f*S) + readFrac (del2R, del2Rmask, del2Rwr, 1996.f*S)
                          - readFrac (del1L, del1Lmask, del1Lwr, 1990.f*S) - readFrac (dif2L, dif2Lmask, dif2Lwr, 187.f*S)
                          - readFrac (del2L, del2Lmask, del2Lwr, 1066.f*S) );
        float yr = 0.6f * ( readFrac (del1L, del1Lmask, del1Lwr, 353.f*S + modTapC*exc[3]) + readFrac (del1L, del1Lmask, del1Lwr, 3627.f*S)
                          - readFrac (dif2L, dif2Lmask, dif2Lwr, 1228.f*S) + readFrac (del2L, del2Lmask, del2Lwr, 2673.f*S)
                          - readFrac (del1R, del1Rmask, del1Rwr, 2111.f*S) - readFrac (dif2R, dif2Rmask, dif2Rwr, 335.f*S)
                          - readFrac (del2R, del2Rmask, del2Rwr, 121.f*S) );

        // advance write pointers
        dif1Lwr = (dif1Lwr + 1) & dif1Lmask; dif1Rwr = (dif1Rwr + 1) & dif1Rmask;
        del1Lwr = (del1Lwr + 1) & del1Lmask; del1Rwr = (del1Rwr + 1) & del1Rmask;
        dif2Lwr = (dif2Lwr + 1) & dif2Lmask; dif2Rwr = (dif2Rwr + 1) & dif2Rmask;
        del2Lwr = (del2Lwr + 1) & del2Lmask; del2Rwr = (del2Rwr + 1) & del2Rmask;

        // bold Tone tilt (identity at tone 0.5) + M/S width + DC block
        tiltLpL += (yl - tiltLpL) * tiltCoef; { float hi = yl - tiltLpL; yl = tiltLpL * loGainC + hi * hiGainC; }
        tiltLpR += (yr - tiltLpR) * tiltCoef; { float hi = yr - tiltLpR; yr = tiltLpR * loGainC + hi * hiGainC; }
        float mid = 0.5f * (yl + yr), sid = 0.5f * (yl - yr) * (0.12f + 1.88f * widthC);
        float wl = mid + sid, wr = mid - sid;
        float ol = wl - dcxL + dcR * dcyL; dcxL = wl; dcyL = flush (ol);
        float orr= wr - dcxR + dcR * dcyR; dcxR = wr; dcyR = flush (orr);
        wetL = dcyL + gritC * rand11(); wetR = dcyR + gritC * rand11();   // faint authentic 224 hiss (Random-Vintage)
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
    inline float rand11() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (float) ((rngState >> 8) & 0xFFFFFFu) * (1.0f / 8388607.5f) - 1.0f; }
    void alloc (std::vector<float>& buf, int& mask, int baseLen29k)
    {
        int need = (int) std::ceil (baseLen29k * scl * 3.9f) + 96;   // headroom for Size×3.5 + mod
        int sz = 1; while (sz < need) sz <<= 1; buf.assign ((size_t) sz, 0.0f); mask = sz - 1;
    }

    static constexpr float PI = 3.14159265358979f;
    static constexpr float FREEZE_G = 0.9995f;
    static constexpr float idLen[NID] = { 142.f, 107.f, 379.f, 277.f };
    static constexpr float idG[NID]   = { 0.75f, 0.75f, 0.625f, 0.625f };
    static constexpr float MODR[4]    = { 1.00f, 1.13f, 0.87f, 1.29f };   // decorrelated per-modulator rates
    // 8 Lexicon 224 PROGRAMS: sizeMul, decayMul, toneMul, toneAdd, dampMul, dampAdd, diffMul, diffFloor, bassMul, modMul, widthAdd.
    struct CharBias { float sizeMul, decayMul, toneMul, toneAdd, dampMul, dampAdd, diffMul, diffFloor, bassMul, modMul, widthAdd; };
    static constexpr CharBias CHAR[8] = {
        /* Concert Hall B */ { 1.35f, 1.45f, 1.02f, 0.00f, 1.00f, 0.00f, 1.00f, 0.20f, 1.30f, 1.30f,  0.05f },  // flagship — builds into lush wash
        /* Small Hall A   */ { 0.85f, 0.95f, 1.30f, 0.08f, 0.80f, 0.00f, 1.05f, 0.35f, 1.00f, 1.00f,  0.00f },  // brighter, tighter
        /* Large Hall B   */ { 1.60f, 1.80f, 0.82f, 0.00f, 1.00f, 0.05f, 1.05f, 0.40f, 1.35f, 1.10f,  0.10f },  // longest, low/dark coloration
        /* Vocal Plate    */ { 0.80f, 0.90f, 1.45f, 0.12f, 0.70f, 0.00f, 1.10f, 0.60f, 0.85f, 0.90f,  0.00f },  // bright, forward, dense
        /* Percussion Plt */ { 0.70f, 0.80f, 1.15f, 0.05f, 0.85f, 0.00f, 1.10f, 0.65f, 0.90f, 0.70f,  0.05f },  // dense fast attack
        /* Constant Dens  */ { 0.72f, 0.72f, 1.10f, 0.02f, 0.95f, 0.00f, 1.05f, 0.75f, 0.90f, 0.40f,  0.00f },  // even metallic, gated-80s
        /* Chamber        */ { 0.60f, 0.75f, 0.78f, 0.00f, 1.05f, 0.10f, 0.90f, 0.15f, 0.95f, 0.90f, -0.18f },  // near-mono, dark, less density
        /* Room A         */ { 0.72f, 0.80f, 1.12f, 0.04f, 0.90f, 0.00f, 1.05f, 0.55f, 0.90f, 1.45f,  0.05f },  // high density, lively fast mod
    };
    // 6 Chorus VOICINGS: depthMul, rateMul, rnd (0=sine 1=random), grain, widthAdd.
    struct VoiceBias { float depthMul, rateMul, rnd, grain, widthAdd; };
    static constexpr VoiceBias VOICE[6] = {
        /* Random-Vintage */ { 1.00f, 1.00f, 1.00f, 1.00f, 0.00f },   // authentic grainy 224 shimmer (DEFAULT)
        /* Sine           */ { 0.90f, 1.00f, 0.00f, 0.00f, 0.00f },   // smooth modern swirl
        /* Ensemble       */ { 1.65f, 0.70f, 0.35f, 0.00f, 0.15f },   // deep detune, widest/thickest
        /* Wow/Drift      */ { 1.40f, 0.22f, 1.00f, 0.00f, 0.00f },   // slow evolving ambient drift
        /* Chaos          */ { 2.30f, 2.70f, 1.00f, 0.35f, 0.05f },   // fast wild warble
        /* Off            */ { 0.00f, 1.00f, 0.00f, 0.00f, 0.00f },   // static tank — colder, metallic
    };

    float fs = 48000.0f, scl = 1.613f, smth = 0.001f, bassCoef = 0.f, dcR = 0.999f, tiltCoef = 0.3f;
    bool  primed = false;
    std::vector<float> dif1L, dif1R, del1L, del1R, dif2L, dif2R, del2L, del2R, pre;
    int dif1Lmask=0, dif1Rmask=0, del1Lmask=0, del1Rmask=0, dif2Lmask=0, dif2Rmask=0, del2Lmask=0, del2Rmask=0, preMask=0;
    int dif1Lwr=0, dif1Rwr=0, del1Lwr=0, del1Rwr=0, dif2Lwr=0, dif2Rwr=0, del2Lwr=0, del2Rwr=0, preWr=0;
    std::vector<float> id[NID]; int idMask[NID] = {0}; int idWr[NID] = {0}; int idDelay[NID] = {0};
    float inLpZ=0, lcZ=0, bassLz=0, bassRz=0, dampLz=0, dampRz=0, dcxL=0, dcyL=0, dcxR=0, dcyR=0, tiltLpL=0, tiltLpR=0;
    float modPh[4] = {0}, oldR[4] = {0}, newR[4] = {0}, modIncT[4] = {0}, modIncC[4] = {0};
    std::uint32_t rngState = 0x1234567u;
    // targets + ramped current
    float sizeScaleT=1, sizeScaleC=1, decayT=0.3f, decayC=0.3f, lowGainT=1, lowGainC=1, dampT=0.5f, dampC=0.5f;
    float inLpT=0, inLpC=0, lcT=0, lcC=0, idGT=0.8f, idGC=0.8f, g1T=0.7f, g1C=0.7f, g2T=0.5f, g2C=0.5f;
    float hiGainT=1, hiGainC=1, loGainT=1, loGainC=1, modApT=0, modApC=0, modTapT=0, modTapC=0;
    float modRndT=1, modRndC=1, grainT=0, grainC=0, gritT=0, gritC=0, widthT=0.8f, widthC=0.8f;
    float preSampT=0, preSampC=0, freezeCur=0, freezeTgt=0, rt60=2.0f, mixExt=0.3f;
    // param state
    int   character=0, voicing=0;
    bool  modOn=true, freezeOn=false;
    float size=0.5f, decay=0.5f, tone=0.5f, preMs=12.f, diffuse=0.7f, modDepth=0.4f, modRate=1.2f,
          trebDec=0.4f, bassMul=1.5f, lowCut=20.f, width=0.85f;
};
