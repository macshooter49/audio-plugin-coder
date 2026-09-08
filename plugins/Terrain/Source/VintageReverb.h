#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// VintageReverb — the VINTAGE reverb type (fb288): the sound of an 80s DIGITAL RACK
// unit (EMT 250 / Lexicon 224-era / AMS RMX16 / Ursa Major Space Station / early Alesis).
// Clean-room from the reverb build bible. It's the Digital tank family (cross-coupled
// figure-8 — the proven-stable skeleton) but voiced completely differently: LO-FI and
// COLORED, the grainy 12–16-bit converter sound, not the smooth hi-fi 224.
//
//   in → DRIVE (input tanh saturation, the converter/preamp warmth)
//      → AGE: ZOH sample-rate DECIMATION (staircase → aliasing + band-limit, the reduced
//              internal-SR tell) — on the way IN and the way OUT
//      → GRIT: bit-CRUSH (quantization grit, dithered)
//      → pre-delay → input diffusers → TANK (figure-8, 2 modulated allpasses = chorus,
//              in-loop AGE low-pass = the dull reduced-Nyquist tail; single-band decay so
//              there's no freeze-bass divergence class of bug)
//      → 7 decorrelated output taps → Tone tilt → output AGE-decimate + GRIT-crush
//      → SHAPE envelope (keyed off the input level): Normal / Gate / Gate-Long / Reverse /
//              Nonlin / Ambience — the 80s GATED-SNARE + REVERSE-SWELL that define the era
//      → M/S width → DC block.
//
// ALL the color stages sit OUTSIDE the recirculating loop (the only in-loop filter is the
// cut-only Age LP) so it's unconditionally stable; every continuous param glides per-sample
// (incl. the ZOH rate + crush depth) so sweeps are click-free. PURE C++ (no JUCE): offline-
// validates standalone AND drops into the voice path.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>

class VintageReverb
{
public:
    static constexpr int NID = 4;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        scl = fs / 29761.0f;
        alloc (dif1L, dif1Lmask, 672);  alloc (dif1R, dif1Rmask, 908);
        alloc (del1L, del1Lmask, 4453); alloc (del1R, del1Rmask, 4217);
        alloc (dif2L, dif2Lmask, 2656); alloc (dif2R, dif2Rmask, 2656);
        alloc (del2L, del2Lmask, 3720); alloc (del2R, del2Rmask, 3720);
        for (int i = 0; i < NID; ++i) { alloc (id[i], idMask[i], (int) idLen[i]); idWr[i] = 0; idDelay[i] = std::max (4, (int) std::lround (idLen[i] * scl)); }
        { int need = (int) std::ceil (0.22f * fs) + 8; int sz = 1; while (sz < need) sz <<= 1; pre.assign ((size_t) sz, 0.0f); preMask = sz - 1; preWr = 0; }
        smth = 1.0f - std::exp (-1.0f / (0.015f * fs));
        dcR = 1.0f - (126.0f / fs);
        tiltCoef = 1.0f - std::exp (-2.0f * PI * 1500.0f / fs);
        // Shape envelope coefficients (fast attack / fast-ish gate close — ramped so no click)
        envAtkC  = 1.0f - std::exp (-1.0f / (0.001f * fs));   // input follower attack ~1 ms
        envRelC  = 1.0f - std::exp (-1.0f / (0.030f * fs));   // input follower release ~30 ms
        gAtkC    = 1.0f - std::exp (-1.0f / (0.003f * fs));   // gate open ~3 ms
        gRelC    = 1.0f - std::exp (-1.0f / (0.012f * fs));   // gate close ~12 ms (soft, click-free)
        nRelC    = 1.0f - std::exp (-1.0f / (0.004f * fs));   // Nonlin close ~4 ms (harder)
        aRelC    = 1.0f - std::exp (-1.0f / (0.070f * fs));   // Ambience soft release ~70 ms (natural taper)
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
        inLpZ = lcZ = dampLz = dampRz = ageLzL = ageLzR = dcxL = dcyL = dcxR = dcyR = tiltLpL = tiltLpR = 0.0f;
        srPhI = srPhO = 0.0f; heldInL = heldInR = heldOutL = heldOutR = 0.0f;
        recLpI = recLpOL = recLpOR = 0.0f;
        inEnv = 0.0f; gateGain = 0.0f; gateTimer = 0.0f; revGain = 0.0f; revStep = 0.0f; armed = true; sinceTrig = 1.0e9f;
        ditherState = 0x9E3779B9u;
    }

    // ── params (0..1 unless noted) — mapped from the shared reverb slots ──
    void setSize        (float v) { size    = clamp01 (v); }   // SIZE  → tank scale
    void setDecay       (float v) { decay   = clamp01 (v); }   // DECAY → tail length + gate/reverse time
    void setTone        (float v) { tone    = clamp01 (v); }   // TONE  → bright↔dark
    void setPreDelayMs  (float ms){ preMs   = clampf (ms, 0.0f, 200.0f); }
    void setDiffusion   (float v) { diffuse = clamp01 (v); }   // DIFFUSE → density
    void setModDepth    (float v) { modDepth= clamp01 (v); }   // MODDEPTH → chorus depth
    void setModRate     (float hz){ modRate = clampf (hz, 0.02f, 6.0f); }  // MODRATE → chorus speed
    void setAge         (float v) { age     = clamp01 (v); }   // HIDAMP slot → SR reduction (alias+band-limit+dull tail)
    void setGrit        (float v) { grit    = clamp01 (v); }   // LOWDECAY slot → bit-crush
    void setDrive       (float v) { drive   = clamp01 (v); }   // LOWCUT slot → input saturation
    void setWidth       (float v) { width   = clamp01 (v); }
    void setCharacter   (int c)   { character = c < 0 ? 0 : (c > 7 ? 7 : c); }   // 8 vintage voicings
    void setShape       (int s)   { shape     = s < 0 ? 0 : (s > 5 ? 5 : s); }   // 6 envelope shapes
    void setModEnabled  (bool on) { modOn   = on; }
    void setFreeze      (bool f)  { freezeOn = f; }
    void setMix         (float v) { mixExt  = clamp01 (v); }

    void updateCoefficients()
    {
        const CharBias cb = CHAR[character];
        rt60 = 0.28f * std::pow (16.0f, decay) * cb.decayMul;
        sizeScaleT = (0.4f + 1.4f * size) * cb.sizeMul;
        float dTot = (672.f + 4453.f + 1800.f + 3720.f + 908.f + 4217.f + 2656.f + 3163.f) * scl * sizeScaleT;
        float dc = std::pow (10.0f, -1.5f * dTot / (fs * rt60));
        if (dc > 0.9993f) dc = 0.9993f;
        decayT = dc;
        // Tone: input band-limit + bold output tilt (identity at 0.5).
        float toneEff = clamp01 (tone * cb.toneMul + cb.toneAdd);
        inLpT = std::exp (-2.0f * PI * (1200.0f * std::pow (14.0f, toneEff)) / fs);
        float tilt = (toneEff - 0.5f) * 2.0f;
        hiGainT = std::pow (3.6f, tilt); loGainT = std::pow (2.0f, -tilt);
        lcT = std::exp (-2.0f * PI * 22.0f / fs);   // fixed gentle sub-cut (no user Low Cut on Vintage — Drive owns that slot)
        // Diffusion: input + decay allpass density.
        float diffEff = clamp01 (0.15f * cb.diffFloor + diffuse * cb.diffMul);
        idGT = 0.20f + 0.66f * diffEff;
        g1T  = 0.80f * diffEff; g2T = 0.64f * diffEff;
        // Chorus (matched Depth+Rate pair).
        float depth = modOn ? clampf (modDepth * cb.modMul, 0.0f, 2.2f) : 0.0f;
        modApT = depth * 30.0f; modTapT = depth * 20.0f;
        for (int i = 0; i < 4; ++i) modIncT[i] = (modRate * cb.rateMul * MODR[i]) / fs;
        // ── AGE = the reduced-internal-SR tell. ZOH rate (1 = full 48k / clean, low = crunchy),
        //    an in-loop cut-only LP that dulls the tail as it ages, and (via the same knob) the amount
        //    of staircase aliasing. Character can push an era offset.
        float ageEff = clamp01 (age + cb.ageAdd);
        srRateT = 1.0f - 0.90f * ageEff;                       // 1.0 → ~4.8 kHz ZOH
        ageLpT  = std::exp (-2.0f * PI * (18000.0f * std::pow (0.16f, ageEff)) / fs);   // 18k → ~2.9k in-loop LP
        // DAC reconstruction LP @ the reduced Nyquist — removes the harsh staircase images ABOVE the effective
        // SR (so Age BAND-LIMITS/dulls like a real converter) while the foldback aliasing BELOW it stays (the grit).
        recCoefT = std::exp (-2.827f * srRateT);               // fc ≈ srRate·0.45·fs
        // ── GRIT = bit-crush depth. 0 → 16-bit (clean), 1 → ~4-bit (crushed). Dithered so a decaying
        //    tail grains out smoothly instead of sticking on a quantization step (no idle DC tone).
        float gritEff = clamp01 (grit + cb.gritAdd);
        bitsT = 16.0f - 12.0f * gritEff;
        // ── DRIVE = input saturation (tanh pre-gain). 0 → transparent, 1 → hot converter crunch.
        float driveEff = clamp01 (drive + cb.driveAdd);
        driveGainT = 1.0f + driveEff * 7.0f;
        driveMkT   = 1.0f / std::tanh (0.7f * driveGainT) * 0.7f;   // ~unity makeup so Drive colors, doesn't just quiet
        widthT = clamp01 (width + cb.widthAdd);
        preSampT = preMs * 0.001f * fs;
        // Shape timing — gate/reverse length scales with Decay (+ Size); "Gate Long" = ×2.
        float baseLen = 0.045f + decay * 0.55f + 0.15f * size;   // 45 ms … ~0.75 s
        gateLenT = (shape == 1) ? baseLen * 2.0f : baseLen;       // Gate Long = index 1
        revLenT  = baseLen * 1.3f;
        freezeTgt = freezeOn ? 1.0f : 0.0f;
        if (! primed)
        {
            sizeScaleC = sizeScaleT; decayC = decayT; inLpC = inLpT; lcC = lcT; idGC = idGT; g1C = g1T; g2C = g2T;
            hiGainC = hiGainT; loGainC = loGainT; modApC = modApT; modTapC = modTapT;
            for (int i = 0; i < 4; ++i) modIncC[i] = modIncT[i];
            srRateC = srRateT; ageLpC = ageLpT; recCoefC = recCoefT; bitsC = bitsT; driveGainC = driveGainT; driveMkC = driveMkT;
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
        // per-sample glide (click-free — includes the ZOH rate + crush depth + drive gain)
        sizeScaleC += (sizeScaleT - sizeScaleC) * smth; decayC += (decayT - decayC) * smth;
        inLpC += (inLpT - inLpC) * smth; lcC += (lcT - lcC) * smth; idGC += (idGT - idGC) * smth;
        g1C += (g1T - g1C) * smth; g2C += (g2T - g2C) * smth;
        hiGainC += (hiGainT - hiGainC) * smth; loGainC += (loGainT - loGainC) * smth;
        modApC += (modApT - modApC) * smth; modTapC += (modTapT - modTapC) * smth;
        for (int i = 0; i < 4; ++i) modIncC[i] += (modIncT[i] - modIncC[i]) * smth;
        srRateC += (srRateT - srRateC) * smth; ageLpC += (ageLpT - ageLpC) * smth; recCoefC += (recCoefT - recCoefC) * smth; bitsC += (bitsT - bitsC) * smth;
        driveGainC += (driveGainT - driveGainC) * smth; driveMkC += (driveMkT - driveMkC) * smth;
        widthC += (widthT - widthC) * smth; preSampC += (preSampT - preSampC) * smth;
        freezeCur += (freezeTgt - freezeCur) * smth;
        const float S = sizeScaleC * scl;

        // ── SHAPE — input-keyed envelope (drives the wet window at the very end) ──
        const float ain = 0.5f * (std::fabs (inL) + std::fabs (inR));
        inEnv += (ain - inEnv) * (ain > inEnv ? envAtkC : envRelC);
        sinceTrig += 1.0f / fs;
        bool onset = false;
        if (armed && inEnv > kGateThr && sinceTrig > 0.05f) { onset = true; armed = false; sinceTrig = 0.0f; }
        if (! armed && inEnv < kGateThr * 0.4f) armed = true;
        // Shapes: 0 Gate · 1 Gate-Long · 2 Normal (default — index 2 is the shared MODMODE default) ·
        //         3 Reverse · 4 Nonlin · 5 Ambience.
        float shapeGain = 1.0f;
        switch (shape)
        {
            case 0: case 1:   // Gate / Gate Long — fixed-length window, fast close
                if (onset) gateTimer = gateLenT;
                if (gateTimer > 0.0f) { gateGain += (1.0f - gateGain) * gAtkC; gateTimer -= 1.0f / fs; }
                else                    gateGain += (0.0f - gateGain) * gRelC;
                shapeGain = gateGain; break;
            case 3:           // Reverse — swell 0→1 over revLen, reset on the next onset
                if (onset) { revGain = 0.0f; revStep = 1.0f / (revLenT * fs); }
                revGain = revGain + revStep; if (revGain > 1.0f) revGain = 1.0f;
                shapeGain = revGain * revGain; break;   // squared = a more convincing backwards swell
            case 4:           // Nonlin — instant plateau then a harder cut (AMS-style)
                if (onset) gateTimer = gateLenT * 0.8f;
                if (gateTimer > 0.0f) { gateGain += (1.0f - gateGain) * gAtkC; gateTimer -= 1.0f / fs; }
                else                    gateGain += (0.0f - gateGain) * nRelC;
                shapeGain = gateGain; break;
            case 5:           // Ambience — long soft-gate: a tight, gently-tapered natural room (distinct from Normal)
                if (onset) gateTimer = gateLenT * 2.2f;
                if (gateTimer > 0.0f) { gateGain += (1.0f - gateGain) * gAtkC; gateTimer -= 1.0f / fs; }
                else                    gateGain += (0.0f - gateGain) * aRelC;
                shapeGain = gateGain; break;
            default: shapeGain = 1.0f; break;   // 2 Normal — natural full decay
        }

        // ── INPUT COLOR: mono → DRIVE (tanh) → GRIT crush → AGE ZOH decimate → reconstruction LP ──
        // (models saturate → quantize → sample → reconstruct: the vintage AD/DA chain.)
        float x = 0.5f * (inL + inR);
        x = std::tanh (x * driveGainC) * driveMkC;                       // DRIVE
        x = crush (x, bitsC);                                            // GRIT (in) — quantize before sampling
        srPhI += srRateC; if (srPhI >= 1.0f) { srPhI -= 1.0f; heldInL = x; } x = heldInL;   // AGE ZOH (in)
        recLpI = (1.0f - recCoefC) * x + recCoefC * recLpI; x = recLpI;   // AGE reconstruction (reduced Nyquist)

        // pre-delay → input band-limit → sub-cut → input diffusers
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
        const float xd = x;

        // ── modulators (sine chorus) ──
        float exc[4];
        for (int i = 0; i < 4; ++i)
        {
            exc[i] = fastSin (modPh[i]);
            modPh[i] += modIncC[i]; if (modPh[i] >= 1.0f) modPh[i] -= 1.0f;
        }

        // ── TANK (figure-8, single-band decay). In-loop AGE LP = the dull reduced-Nyquist tail. ──
        float outL = readFrac (del2L, del2Lmask, del2Lwr, 3720.f * S);
        float outR = readFrac (del2R, del2Rmask, del2Rwr, 3163.f * S);
        const float dec = decayC + (FREEZE_G - decayC) * freezeCur;
        const float inG = 0.5f * (1.0f - freezeCur);
        float lIn = inG * xd + dec * outR;
        float rIn = inG * xd + dec * outL;
        // LEFT half — modulated allpass → in-loop AGE LP → del1 → dif2 → del2
        { float D = 672.f * S + modApC * exc[0]; float d = readFrac (dif1L, dif1Lmask, dif1Lwr, D);
          float in = lIn - g1C * d; dif1L[(size_t) dif1Lwr] = flush (in); lIn = d + g1C * in; }
        ageLzL = (1.0f - ageLpC) * lIn + ageLpC * ageLzL; lIn = ageLzL;
        { float d = readFrac (del1L, del1Lmask, del1Lwr, 4453.f * S); del1L[(size_t) del1Lwr] = flush (lIn); lIn = d; }
        { float d = readFrac (dif2L, dif2Lmask, dif2Lwr, 1800.f * S); float in = lIn - g2C * d; dif2L[(size_t) dif2Lwr] = flush (in); lIn = d + g2C * in; }
        del2L[(size_t) del2Lwr] = flush (lIn);
        // RIGHT half
        { float D = 908.f * S + modApC * exc[1]; float d = readFrac (dif1R, dif1Rmask, dif1Rwr, D);
          float in = rIn - g1C * d; dif1R[(size_t) dif1Rwr] = flush (in); rIn = d + g1C * in; }
        ageLzR = (1.0f - ageLpC) * rIn + ageLpC * ageLzR; rIn = ageLzR;
        { float d = readFrac (del1R, del1Rmask, del1Rwr, 4217.f * S); del1R[(size_t) del1Rwr] = flush (rIn); rIn = d; }
        { float d = readFrac (dif2R, dif2Rmask, dif2Rwr, 2656.f * S); float in = rIn - g2C * d; dif2R[(size_t) dif2Rwr] = flush (in); rIn = d + g2C * in; }
        del2R[(size_t) del2Rwr] = flush (rIn);

        // ── 7 output taps/ch (2 modulated = chorus shimmer) ──
        float yl = 0.6f * ( readFrac (del1R, del1Rmask, del1Rwr, 266.f*S + modTapC*exc[2]) + readFrac (del1R, del1Rmask, del1Rwr, 2974.f*S)
                          - readFrac (dif2R, dif2Rmask, dif2Rwr, 1913.f*S) + readFrac (del2R, del2Rmask, del2Rwr, 1996.f*S)
                          - readFrac (del1L, del1Lmask, del1Lwr, 1990.f*S) - readFrac (dif2L, dif2Lmask, dif2Lwr, 187.f*S)
                          - readFrac (del2L, del2Lmask, del2Lwr, 1066.f*S) );
        float yr = 0.6f * ( readFrac (del1L, del1Lmask, del1Lwr, 353.f*S + modTapC*exc[3]) + readFrac (del1L, del1Lmask, del1Lwr, 3627.f*S)
                          - readFrac (dif2L, dif2Lmask, dif2Lwr, 1228.f*S) + readFrac (del2L, del2Lmask, del2Lwr, 2673.f*S)
                          - readFrac (del1R, del1Rmask, del1Rwr, 2111.f*S) - readFrac (dif2R, dif2Rmask, dif2Rwr, 335.f*S)
                          - readFrac (del2R, del2Rmask, del2Rwr, 121.f*S) );

        dif1Lwr = (dif1Lwr + 1) & dif1Lmask; dif1Rwr = (dif1Rwr + 1) & dif1Rmask;
        del1Lwr = (del1Lwr + 1) & del1Lmask; del1Rwr = (del1Rwr + 1) & del1Rmask;
        dif2Lwr = (dif2Lwr + 1) & dif2Lmask; dif2Rwr = (dif2Rwr + 1) & dif2Rmask;
        del2Lwr = (del2Lwr + 1) & del2Lmask; del2Rwr = (del2Rwr + 1) & del2Rmask;

        // Tone tilt (identity at 0.5)
        tiltLpL += (yl - tiltLpL) * tiltCoef; { float hi = yl - tiltLpL; yl = tiltLpL * loGainC + hi * hiGainC; }
        tiltLpR += (yr - tiltLpR) * tiltCoef; { float hi = yr - tiltLpR; yr = tiltLpR * loGainC + hi * hiGainC; }

        // ── OUTPUT COLOR: GRIT crush (dithered) → AGE ZOH decimate → reconstruction LP (the DAC chain) ──
        yl = crushDither (yl, bitsC); yr = crushDither (yr, bitsC);
        srPhO += srRateC; if (srPhO >= 1.0f) { srPhO -= 1.0f; heldOutL = yl; heldOutR = yr; } yl = heldOutL; yr = heldOutR;
        recLpOL = (1.0f - recCoefC) * yl + recCoefC * recLpOL; yl = recLpOL;
        recLpOR = (1.0f - recCoefC) * yr + recCoefC * recLpOR; yr = recLpOR;

        // M/S width + DC block + SHAPE window
        float mid = 0.5f * (yl + yr), sid = 0.5f * (yl - yr) * (0.12f + 1.88f * widthC);
        float wl = mid + sid, wr = mid - sid;
        float ol = wl - dcxL + dcR * dcyL; dcxL = wl; dcyL = flush (ol);
        float orr= wr - dcxR + dcR * dcyR; dcxR = wr; dcyR = flush (orr);
        wetL = dcyL * shapeGain; wetR = dcyR * shapeGain;
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
    // Undithered bit-crush (input side): round to 2^bits levels.
    static inline float crush (float x, float bits)
    {
        if (bits > 15.8f) return x;                 // ~clean
        float levels = std::pow (2.0f, bits) * 0.5f; if (levels < 1.0f) levels = 1.0f;
        float q = std::round (x * levels) / levels;
        return q > 1.5f ? 1.5f : (q < -1.5f ? -1.5f : q);
    }
    // Dithered bit-crush (output side): tiny triangular dither breaks idle tones so a decaying
    // tail grains out smoothly (no stuck quantization DC).
    inline float crushDither (float x, float bits)
    {
        if (bits > 15.8f) return x;
        float levels = std::pow (2.0f, bits) * 0.5f; if (levels < 1.0f) levels = 1.0f;
        float d = (rand11() + rand11()) * (0.5f / levels);   // TPDF, ±1 LSB
        float q = std::round ((x + d) * levels) / levels;
        return q > 1.5f ? 1.5f : (q < -1.5f ? -1.5f : q);
    }
    inline float rand11() { ditherState ^= ditherState << 13; ditherState ^= ditherState >> 17; ditherState ^= ditherState << 5; return (float) ((ditherState >> 8) & 0xFFFFFFu) * (1.0f / 8388607.5f) - 1.0f; }
    void alloc (std::vector<float>& buf, int& mask, int baseLen29k)
    {
        int need = (int) std::ceil (baseLen29k * scl * 3.9f) + 96;
        int sz = 1; while (sz < need) sz <<= 1; buf.assign ((size_t) sz, 0.0f); mask = sz - 1;
    }

    static constexpr float PI = 3.14159265358979f;
    static constexpr float FREEZE_G = 0.9993f;
    static constexpr float kGateThr = 0.012f;        // Shape onset threshold (send level ~0.18 typical)
    static constexpr float idLen[NID] = { 142.f, 107.f, 379.f, 277.f };
    static constexpr float idG[NID]   = { 0.75f, 0.75f, 0.625f, 0.625f };
    static constexpr float MODR[4]    = { 1.00f, 1.13f, 0.87f, 1.29f };
    // 8 VINTAGE VOICINGS: sizeMul, decayMul, toneMul, toneAdd, ageAdd, gritAdd, driveAdd, diffMul, diffFloor, modMul, rateMul, widthAdd.
    struct CharBias { float sizeMul, decayMul, toneMul, toneAdd, ageAdd, gritAdd, driveAdd, diffMul, diffFloor, modMul, rateMul, widthAdd; };
    static constexpr CharBias CHAR[8] = {
        /* Classic  */ { 1.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 1.00f, 0.30f, 1.00f, 1.00f,  0.00f },  // balanced 80s digital (default)
        /* Bright   */ { 0.85f, 0.90f, 1.35f, 0.10f,-0.15f,-0.05f, 0.05f, 1.05f, 0.45f, 1.00f, 1.20f,  0.05f },  // clean, forward rack (REV/SPX-ish)
        /* Metal    */ { 0.80f, 1.05f, 1.15f, 0.02f, 0.05f, 0.10f, 0.05f, 0.80f, 0.20f, 0.70f, 0.80f,  0.05f },  // metallic sparse plate
        /* Space    */ { 1.55f, 1.60f, 0.78f, 0.00f, 0.20f, 0.00f, 0.00f, 1.05f, 0.40f, 1.35f, 0.55f,  0.15f },  // huge dark ambience
        /* Cheap    */ { 0.75f, 0.85f, 0.85f, 0.00f, 0.45f, 0.30f, 0.10f, 0.90f, 0.35f, 0.90f, 1.30f,  0.00f },  // budget lo-fi — very aged + gritty
        /* Warm     */ { 1.05f, 1.10f, 0.70f, 0.00f, 0.12f, 0.00f, 0.20f, 1.05f, 0.35f, 1.10f, 0.70f,  0.05f },  // gold, saturated, smooth
        /* Gated    */ { 0.72f, 0.72f, 1.10f, 0.03f, 0.05f, 0.05f, 0.00f, 1.10f, 0.70f, 0.50f, 1.00f,  0.00f },  // dense/even — made for Gate/Nonlin
        /* Cathedral*/ { 1.60f, 1.75f, 0.88f, 0.00f, 0.10f, 0.00f, 0.00f, 1.05f, 0.45f, 1.10f, 0.60f,  0.10f },  // long, grand, dark
    };

    float fs = 48000.0f, scl = 1.613f, smth = 0.001f, dcR = 0.999f, tiltCoef = 0.3f;
    float envAtkC = 0.1f, envRelC = 0.01f, gAtkC = 0.1f, gRelC = 0.05f, nRelC = 0.1f, aRelC = 0.02f;
    bool  primed = false;
    std::vector<float> dif1L, dif1R, del1L, del1R, dif2L, dif2R, del2L, del2R, pre;
    int dif1Lmask=0, dif1Rmask=0, del1Lmask=0, del1Rmask=0, dif2Lmask=0, dif2Rmask=0, del2Lmask=0, del2Rmask=0, preMask=0;
    int dif1Lwr=0, dif1Rwr=0, del1Lwr=0, del1Rwr=0, dif2Lwr=0, dif2Rwr=0, del2Lwr=0, del2Rwr=0, preWr=0;
    std::vector<float> id[NID]; int idMask[NID] = {0}; int idWr[NID] = {0}; int idDelay[NID] = {0};
    float inLpZ=0, lcZ=0, dampLz=0, dampRz=0, ageLzL=0, ageLzR=0, dcxL=0, dcyL=0, dcxR=0, dcyR=0, tiltLpL=0, tiltLpR=0;
    float modPh[4] = {0}, oldR[4] = {0}, newR[4] = {0}, modIncT[4] = {0}, modIncC[4] = {0};
    // vintage-color state
    float srPhI=0, srPhO=0, heldInL=0, heldInR=0, heldOutL=0, heldOutR=0;
    float recCoefT=0.06f, recCoefC=0.06f, recLpI=0, recLpOL=0, recLpOR=0;   // DAC reconstruction LP (glided)
    std::uint32_t ditherState = 0x9E3779B9u;
    // shape state
    float inEnv=0, gateGain=0, gateTimer=0, revGain=0, revStep=0, sinceTrig=1e9f;
    bool  armed=true;
    // targets + ramped current
    float sizeScaleT=1, sizeScaleC=1, decayT=0.3f, decayC=0.3f, inLpT=0, inLpC=0, lcT=0, lcC=0;
    float idGT=0.8f, idGC=0.8f, g1T=0.7f, g1C=0.7f, g2T=0.5f, g2C=0.5f, hiGainT=1, hiGainC=1, loGainT=1, loGainC=1;
    float modApT=0, modApC=0, modTapT=0, modTapC=0;
    float srRateT=1, srRateC=1, ageLpT=0, ageLpC=0, bitsT=16, bitsC=16, driveGainT=1, driveGainC=1, driveMkT=1, driveMkC=1;
    float gateLenT=0.2f, revLenT=0.2f;
    float widthT=0.85f, widthC=0.85f, preSampT=0, preSampC=0, freezeCur=0, freezeTgt=0, rt60=2.0f, mixExt=0.3f;
    // param state
    int   character=0, shape=0;
    bool  modOn=true, freezeOn=false;
    float size=0.5f, decay=0.5f, tone=0.5f, preMs=12.f, diffuse=0.65f, modDepth=0.35f, modRate=1.0f,
          age=0.4f, grit=0.4f, drive=0.2f, width=0.85f;
};
