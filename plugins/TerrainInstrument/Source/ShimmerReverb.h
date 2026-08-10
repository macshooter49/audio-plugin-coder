#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ShimmerReverb — the SHIMMER reverb type (fb290): the ethereal ascending octave WASH
// (Eno/Lanois / Valhalla-Shimmer family). Clean-room from the reverb build bible: the
// validated Hall 8-line Jot/Hadamard FDN core with a GRANULAR PITCH-SHIFTER in an OUTER
// FEEDBACK loop — the reverb tail is pitch-shifted (usually +12) and fed back into the
// reverb input, so every recirculation shifts again and the shimmer stacks into cascading
// octaves: an angelic, ever-rising cloud.
//   in → (+ Regen · softclip( Shimmer·pitchShift(reverbTap) + (1−Shimmer)·reverbTap )) → FDN reverb
//      → output = reverb wet (the shimmer is built in via the feedback).
//
// The two stability governors the bible flags:
//   • The granular shifter is a DUAL-TAP crossfade with AMPLITUDE-COMPLEMENTARY Hann windows
//     (two grains offset by half the grain, w1+w2≡1) → the shifter gain is ≤ 1 (a convex
//     blend of two buffer reads), so it can never expand the loop.
//   • DOWNWARD shifts pile energy into the lows → an in-loop HIGH-PASS on the feedback
//     (cutoff rises with the downshift amount) drains it, and Regen is capped lower for
//     downshifts. A tanh soft-clip in the feedback is the final BIBO safety net (bounded
//     output regardless of Regen), and auto-damping tames the building highs.
// Freeze = an infinite evolving shimmer pad (reverb → unity, Regen eased so it holds, not runs).
// Click-free (delays glide, coeffs ramp, shift ratio glides). PURE C++ (no JUCE).
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>

// fb295 — PHASE-VOCODER pitch shifter (Bernsee streaming STFT + identity peak phase-lock). Replaces the granular
// dual-tap shifter for a CLEAN, IN-TUNE octave/fifth (the granular gave "detuned/wishy slop": a grain-crossfade
// frequency wobble that no time-domain tweak removes — harness proved the PV is ~+54 dB cleaner on the octave,
// ~+60 dB on the fifth). N=2048, 75% overlap ⇒ ~43 ms latency INSIDE the feedback (fine for an ethereal shimmer);
// peak phase-locking restores vertical phase coherence so the diffuse tail doesn't go watery. Everything pre-allocated.
struct ShimmerPV
{
    static constexpr int PN = 2048, PH = PN / 4, PNB = PN / 2 + 1, PLAT = PN - PH;
    static constexpr float TAUF = 6.28318530718f;
    int   brev[PN] = {0}, flog = 0;
    float cw[PN / 2] = {0}, sw[PN / 2] = {0};
    float win[PN] = {0}, fin[PN] = {0}, fout[PN] = {0}, acc[PN + PH] = {0};
    float lastPh[PNB] = {0}, sumPh[PNB] = {0}, fr[PN] = {0}, fi[PN] = {0};
    float aMag[PNB] = {0}, aFreq[PNB] = {0}, sMag[PNB] = {0}, sFreq[PNB] = {0};
    int   rover = PLAT; float ratio = 2.0f, expct = 0.0f;

    void prepare()
    {
        flog = 0; while ((1 << flog) < PN) flog++;
        for (int i = 0; i < PN; ++i) { int r = 0, x = i; for (int b = 0; b < flog; ++b) { r = (r << 1) | (x & 1); x >>= 1; } brev[i] = r; }
        for (int i = 0; i < PN / 2; ++i) { cw[i] = std::cos (-TAUF * i / PN); sw[i] = std::sin (-TAUF * i / PN); }
        for (int k = 0; k < PN; ++k) win[k] = 0.5f - 0.5f * std::cos (TAUF * k / PN);
        expct = TAUF * (float) PH / (float) PN;
        reset();
    }
    void reset()
    {
        for (int i = 0; i < PN; ++i) { fin[i] = fout[i] = fr[i] = fi[i] = 0.0f; }
        for (int i = 0; i < PN + PH; ++i) acc[i] = 0.0f;
        for (int k = 0; k < PNB; ++k) { lastPh[k] = sumPh[k] = 0.0f; }
        rover = PLAT;
    }
    void fftp (float* re, float* im, bool inv)
    {
        for (int i = 0; i < PN; ++i) { int j = brev[i]; if (j > i) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
        for (int len = 2; len <= PN; len <<= 1) { int half = len >> 1, step = PN / len;
            for (int i = 0; i < PN; i += len) for (int k = 0; k < half; ++k) { float wr = cw[k * step], wi = inv ? -sw[k * step] : sw[k * step];
                int a = i + k, b = i + k + half; float tr = wr * re[b] - wi * im[b], ti = wr * im[b] + wi * re[b];
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti; } }
        if (inv) { float s = 1.0f / PN; for (int i = 0; i < PN; ++i) { re[i] *= s; im[i] *= s; } }
    }
    static inline float wrapp (float x) { int q = (int) (x / (float) M_PI); if (q >= 0) q += q & 1; else q -= q & 1; return x - (float) M_PI * (float) q; }
    inline float process (float x)
    {
        fin[rover] = x; float out = fout[rover - PLAT]; ++rover;
        if (rover >= PN)
        {
            rover = PLAT; const float r = ratio;
            for (int k = 0; k < PN; ++k) { fr[k] = fin[k] * win[k]; fi[k] = 0.0f; }
            fftp (fr, fi, false);
            for (int k = 0; k < PNB; ++k) { float re = fr[k], im = fi[k]; float mg = std::sqrt (re * re + im * im); float ph = std::atan2 (im, re);
                float dp = ph - lastPh[k]; lastPh[k] = ph; dp -= (float) k * expct; dp = wrapp (dp);
                aMag[k] = mg; aFreq[k] = (float) k + dp * (float) PN / ((float) PH * TAUF); }
            for (int k = 0; k < PNB; ++k) { sMag[k] = 0.0f; sFreq[k] = 0.0f; }
            for (int k = 0; k < PNB; ++k) { int idx = (int) std::floor (k * r + 0.5f); if (idx >= 0 && idx < PNB) { sMag[idx] += aMag[k]; sFreq[idx] = aFreq[k] * r; } }
            for (int k = 0; k < PNB; ++k) { float tmp = (sFreq[k] - (float) k) * ((float) PH * TAUF) / (float) PN + (float) k * expct;
                sumPh[k] += tmp; fr[k] = sMag[k] * std::cos (sumPh[k]); fi[k] = sMag[k] * std::sin (sumPh[k]); }
            // identity peak phase-lock (reuse aFreq/sFreq as pr/pi scratch — done being read above)
            for (int k = 0; k < PNB; ++k) { aFreq[k] = fr[k]; sFreq[k] = fi[k]; }
            for (int k = 1; k < PNB - 1; ++k) { int pk = k; if (sMag[k - 1] > sMag[pk]) pk = k - 1; if (sMag[k + 1] > sMag[pk]) pk = k + 1;
                if (pk != k && sMag[pk] > 1.0e-9f) { float m = std::sqrt (aFreq[k] * aFreq[k] + sFreq[k] * sFreq[k]);
                    float nr = 1.0f / (std::sqrt (aFreq[pk] * aFreq[pk] + sFreq[pk] * sFreq[pk]) + 1.0e-12f);
                    fr[k] = m * aFreq[pk] * nr; fi[k] = m * sFreq[pk] * nr; } }
            for (int k = PNB; k < PN; ++k) { fr[k] = fr[PN - k]; fi[k] = -fi[PN - k]; }
            fftp (fr, fi, true);
            const float ola = 2.0f / 3.0f;   // Hann², 75% overlap ⇒ OLA gain ≈ 1.5 → ×2/3
            for (int k = 0; k < PN; ++k) acc[k] += fr[k] * win[k] * ola;
            for (int k = 0; k < PH; ++k) fout[k] = acc[k];
            for (int k = 0; k < PN; ++k) acc[k] = acc[k + PH]; for (int k = PN; k < PN + PH; ++k) acc[k] = 0.0f;
            for (int k = 0; k < PLAT; ++k) fin[k] = fin[k + PH];
        }
        return out;
    }
};

class ShimmerReverb
{
public:
    static constexpr int N = 8;
    static constexpr int WINLUT = 256;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        const float sr = fs / 48000.0f;
        for (int i = 0; i < N; ++i)
        {
            const int need = (int) std::ceil (baseLen48[i] * sr * 2.2f) + 64;
            int sz = 1; while (sz < need) sz <<= 1;
            line[i].assign ((size_t) sz, 0.0f); lineMask[i] = sz - 1; lineWr[i] = 0;
            dampZ[i] = 0.0f; lfoPh[i] = (float) i / (float) N;
        }
        for (int i = 0; i < 4; ++i)
        { const int len = std::max (8, (int) std::lround (apLen29k[i] * (fs / 29761.0f)));
          int sz = 1; while (sz < len + 4) sz <<= 1; ap[i].assign ((size_t) sz, 0.0f); apMask[i] = sz - 1; apWr[i] = 0; apDelay[i] = len; }
        for (int i = 0; i < N; ++i)
        { const int len = std::max (8, (int) std::lround (tankLen48[i] * sr));
          int sz = 1; while (sz < len + 4) sz <<= 1; tank[i].assign ((size_t) sz, 0.0f); tankMask[i] = sz - 1; tankWr[i] = 0; tankDelay[i] = len; }
        { int need = (int) std::ceil (0.20f * fs) + 8; int sz = 1; while (sz < need) sz <<= 1; pre.assign ((size_t) sz, 0.0f); preMask = sz - 1; preWr = 0; }
        // pitch-shifter grain buffer (~50 ms) + Hann window LUT
        G = 0.085f * fs;                                   // fb294 — grain size ↑ 48→85 ms: a LONGER, smoother crossfade drops the granular grain-transition artifact rate (the "crystal/metallic" the FDN mod was masking) so the octave/fifth shift reads CLEAN with Mod OFF — no wobble added (Max)
        { int need = (int) std::ceil (G * 2.0f) + 64; int sz = 1; while (sz < need) sz <<= 1; sh.assign ((size_t) sz, 0.0f); shMask = sz - 1; shWr = 0; }
        for (int k = 0; k < WINLUT; ++k) winLut[k] = 0.5f - 0.5f * std::cos (2.0f * PI * (float) k / (float) WINLUT);
        for (int i = 0; i < N; ++i) { oldRand[i] = rand11(); newRand[i] = rand11(); }
        smth = 1.0f - std::exp (-1.0f / (0.015f * fs));
        shR   = 1.0f - std::exp (-1.0f / (0.030f * fs));   // shift-ratio glide (glissando on interval change)
        pitchLpCoef = 1.0f - std::exp (-2.0f * PI * 14000.0f / fs);   // (retired granular LP)
        pv_.prepare();   // fb295 — phase-vocoder pitch shifter
        dcR = 1.0f - (126.0f / fs);
        tiltCoef = 1.0f - std::exp (-2.0f * PI * 1200.0f / fs);   // output brightness-tilt split
        reset();
        primed = false;
        updateCoefficients();
    }

    void reset()
    {
        for (int i = 0; i < N; ++i) { std::fill (line[i].begin(), line[i].end(), 0.0f); dampZ[i] = 0.0f; }
        for (int i = 0; i < 4; ++i)  std::fill (ap[i].begin(), ap[i].end(), 0.0f);
        for (int i = 0; i < N; ++i)  std::fill (tank[i].begin(), tank[i].end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f); std::fill (sh.begin(), sh.end(), 0.0f);
        inLpZ = lcZ = dcxL = dcyL = dcxR = dcyR = tiltLpL = tiltLpR = 0.0f;
        d1 = 0.0f; rvbMonoLast = 0.0f; hpZ = 0.0f; pitchLpZ = 0.0f; pv_.reset();
    }

    // ── params (0..1 unless noted); call updateCoefficients() after a batch ──
    void setSize        (float v) { size    = clamp01 (v); }
    void setDecay       (float v) { decay   = clamp01 (v); }
    void setTone        (float v) { tone    = clamp01 (v); }
    void setPreDelayMs  (float ms){ preMs   = clampf (ms, 0.0f, 200.0f); }
    void setDiffusion   (float v) { diffuse = clamp01 (v); }
    void setModDepth    (float v) { modDepth= clamp01 (v); }
    void setModRate     (float hz){ modRate = clampf (hz, 0.01f, 8.0f); }
    void setShimmer     (float v) { blend   = clamp01 (v); }        // HIDAMP slot → pitch BLEND (shimmer amount)
    void setRegen       (float v) { regen   = clamp01 (v); }        // LOWDECAY slot → shimmer FEEDBACK (buildup)
    void setLowCutHz    (float hz){ lowCut  = clampf (hz, 20.0f, 1000.0f); }
    void setWidth       (float v) { width   = clamp01 (v); }
    void setCharacter   (int c)   { character = c < 0 ? 0 : (c > 7 ? 7 : c); }   // 8 shimmer voicings
    void setShift       (int s)   { shift     = s < 0 ? 0 : (s > 5 ? 5 : s); }   // 6 pitch intervals
    void setModEnabled  (bool on) { modOn   = on; }
    void setFreeze      (bool f)  { freezeOn = f; }
    void setMix         (float v) { mixExt  = clamp01 (v); }

    void updateCoefficients()
    {
        const CharBias cb = CHAR[character];
        rt60 = 0.4f * std::pow (28.0f, decay) * cb.decayMul;        // medium-long (shimmer LENGTH comes mostly from Regen)
        const float sizeScale = (0.6f + 1.5f * size) * cb.sizeMul;
        const float diffEff   = std::max (diffuse, cb.diffFloor);
        const float sr = fs / 48000.0f;
        for (int i = 0; i < N; ++i)
        {
            float d = baseLen48[i] * sr * sizeScale;
            if (d > (float) lineMask[i] - 30.0f) d = (float) lineMask[i] - 30.0f;
            baseDelayT[i] = d;
            float dLoop = d + (float) tankDelay[i];
            float g = std::pow (10.0f, -3.0f * dLoop / (fs * rt60));
            if (g > 0.9995f) g = 0.9995f;
            gLineT[i] = g;
        }
        // Pitch interval → read-rate ratio; glissando-glides on change. Down-shift flagged for the stability governors.
        shiftRatioT = SHIFTR[shift];
        pv_.ratio = shiftRatioT;   // fb295 — phase-vocoder shift ratio (discrete per interval; the PV re-maps bins on change)
        downAmt = shiftRatioT < 0.999f ? clamp01 (1.0f - shiftRatioT) : 0.0f;   // 0 for up/detune, >0 for downshift
        // Shimmer feedback: Regen capped (lower for downshift) + auto-damp tames the build. Character scales.
        float regCap = 0.90f - 0.20f * downAmt;
        regenT = clampf (regen * cb.regenMul, 0.0f, 1.0f) * regCap;
        blendT = clamp01 (blend * cb.blendMul);
        float toneEff = clamp01 (tone * cb.toneMul + cb.toneAdd);
        // HF damping: base + AUTO-damp that rises with Regen (keeps the ascending highs from getting harsh) +
        // a DARK Tone adds damping (tames the shimmer stack) + Character.
        float dampEff = clamp01 (0.10f + 0.30f * regenT + (1.0f - toneEff) * 0.45f + cb.dampAdd);
        dampT = 0.9f * clamp01 (dampEff + 0.20f);
        for (int i = 0; i < 4; ++i) apGT[i] = diffEff * apBaseG[i];
        tankGT = 0.7f * diffEff;
        inLpT = std::exp (-2.0f * PI * (1200.0f * std::pow (13.0f, toneEff)) / fs);
        // bold OUTPUT tilt (identity at toneEff 0.5) so Tone is night-and-day even under the shimmer stack.
        float tilt = (toneEff - 0.5f) * 2.0f;
        hiGainT = std::pow (3.4f, tilt); loGainT = std::pow (1.9f, -tilt);
        lcT   = std::exp (-2.0f * PI * lowCut / fs);
        // in-loop feedback HIGH-PASS — drains the lows that a DOWNSHIFT piles up (cutoff rises with downAmt).
        hpCutT = std::exp (-2.0f * PI * (20.0f + 620.0f * downAmt) / fs);
        preSampT = preMs * 0.001f * fs;
        static constexpr float mDepth[1] = { 0.0f };   // (unused placeholder to keep tables aligned)
        (void) mDepth;
        float depth01 = modOn ? clampf (modDepth * cb.modMul, 0.0f, 2.0f) : 0.0f;
        modSampT = depth01 * 40.0f;
        modInc   = (modRate * 0.9f) / fs;
        widthT   = clamp01 (width + cb.widthAdd);
        freezeTgt= freezeOn ? 1.0f : 0.0f;
        if (! primed)
        {
            for (int i = 0; i < N; ++i) { baseDelayC[i] = baseDelayT[i]; gLineC[i] = gLineT[i]; }
            dampC = dampT; for (int i = 0; i < 4; ++i) apGC[i] = apGT[i];
            inLpC = inLpT; lcC = lcT; hpCutC = hpCutT; preSampC = preSampT; modSampC = modSampT; widthC = widthT; tankGC = tankGT;
            hiGainC = hiGainT; loGainC = loGainT;
            regenC = regenT; blendC = blendT; shiftRatioC = shiftRatioT; freezeCur = freezeTgt;
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
        for (int i = 0; i < N; ++i) { baseDelayC[i] += (baseDelayT[i]-baseDelayC[i])*smth; gLineC[i] += (gLineT[i]-gLineC[i])*smth; }
        dampC += (dampT-dampC)*smth; for (int i=0;i<4;++i) apGC[i] += (apGT[i]-apGC[i])*smth;
        tankGC += (tankGT-tankGC)*smth; inLpC += (inLpT-inLpC)*smth; lcC += (lcT-lcC)*smth; hpCutC += (hpCutT-hpCutC)*smth;
        preSampC += (preSampT-preSampC)*smth; modSampC += (modSampT-modSampC)*smth; widthC += (widthT-widthC)*smth;
        hiGainC += (hiGainT-hiGainC)*smth; loGainC += (loGainT-loGainC)*smth;
        regenC += (regenT-regenC)*smth; blendC += (blendT-blendC)*smth; freezeCur += (freezeTgt-freezeCur)*smth;
        shiftRatioC += (shiftRatioT - shiftRatioC) * shR;

        // ── PHASE-VOCODER pitch shift (fb295) — CLEAN, in-tune octave/fifth (replaced the granular grain reader that
        //    gave the "detuned/wishy slop"). Push the previous reverb output each sample; the PV returns the shifted
        //    stream (delayed by its ~43 ms latency, which becomes the shimmer's per-octave stack interval).
        float pitchOut = pv_.process (rvbMonoLast);

        // ── shimmer feedback: blend shifted vs direct, in-loop HP (downshift drain), tanh safety ──
        float fbSig = blendC * pitchOut + (1.0f - blendC) * rvbMonoLast;
        hpZ = (1.0f - hpCutC) * fbSig + hpCutC * hpZ; fbSig = fbSig - hpZ;      // one-pole HP
        float regEff = regenC * (1.0f - 0.55f * freezeCur);                    // freeze eases regen so it HOLDS, not runs
        // fb294 — the SHIFTED feedback self-terminates (energy ascends out of band → the shimmer stack Max likes) so it
        // tolerates a hot 1.5x drive; the DIRECT (unshifted) feedback is a plain reverb loop with NO escape, so a hot
        // drive pushes its small-signal loop gain past 1 at moderate Regen → the "crazy feedback loop when the Shimmer is
        // DOWN" Max heard. Fade the drive with Shimmer: 0.75 (all direct — loop gain < 1 at any Regen) → 1.5 (all shifted,
        // unchanged shimmer). Output level unchanged (this is the FEEDBACK loop gain, not the wet gain).
        float fbDrive = 0.75f + 0.75f * blendC;
        float shimFB  = regEff * std::tanh (fbDrive * fbSig);                  // bounded → BIBO safe at any Regen

        float x = 0.5f * (inL + inR) * (1.0f - freezeCur) + shimFB;            // freeze cuts the dry-in; shimmer holds
        pre[(size_t) preWr] = x; x = readFrac (pre, preMask, preWr, preSampC); preWr = (preWr + 1) & preMask;
        inLpZ = (1.0f - inLpC) * x + inLpC * inLpZ; x = inLpZ;
        lcZ   = (1.0f - lcC)   * x + lcC   * lcZ;    x = x - lcZ;
        for (int i = 0; i < 4; ++i)
        { float d = ap[i][(size_t) ((apWr[i] - apDelay[i]) & apMask[i])]; float in = x - apGC[i]*d; ap[i][(size_t) apWr[i]] = in; apWr[i] = (apWr[i]+1)&apMask[i]; x = d + apGC[i]*in; }

        float sIn[N];
        for (int i = 0; i < N; ++i)
        {
            float ph = lfoPh[i]; float sineV = fastSin (ph);
            float dd = baseDelayC[i] + modSampC * sineV;
            if (dd < 1.0f) dd = 1.0f; float maxd = (float) lineMask[i] - 4.0f; if (dd > maxd) dd = maxd;
            float v = readFrac (line[i], lineMask[i], lineWr[i], dd);
            { float td = tank[i][(size_t) ((tankWr[i]-tankDelay[i]) & tankMask[i])]; float tin = v - tankGC*td; tank[i][(size_t) tankWr[i]] = flush (tin); tankWr[i] = (tankWr[i]+1)&tankMask[i]; v = td + tankGC*tin; }
            dampZ[i] = (1.0f - dampC) * v + dampC * dampZ[i]; v = dampZ[i];
            float loopG = gLineC[i] + (FREEZE_G - gLineC[i]) * freezeCur;
            sIn[i] = loopG * v;
            lfoPh[i] += modInc; if (lfoPh[i] >= 1.0f) lfoPh[i] -= 1.0f;
        }
        float m[N]; for (int i = 0; i < N; ++i) m[i] = sIn[i];
        fwht8 (m);
        const float hs = 0.35355339f;
        const float inG = 0.5f;
        for (int i = 0; i < N; ++i) { float w = inG * x + hs * m[i]; line[i][(size_t) lineWr[i]] = flush (w); lineWr[i] = (lineWr[i]+1)&lineMask[i]; }

        float wl = (sIn[0]+sIn[2]+sIn[4]+sIn[6]) * 0.5f;
        float wr = (sIn[1]+sIn[3]+sIn[5]+sIn[7]) * 0.5f;
        float rvbMono = 0.5f * (wl + wr);
        // feed the shifter for the next samples' reads, and remember the direct tap
        rvbMonoLast = flush (rvbMono);   // fb295 — feeds the phase-vocoder shifter next sample (the granular grain buffer is retired)

        // bold output tone tilt (identity at Tone 0.5) — output-only (the shifter above saw the flat tail)
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
    { if (d < 1.0f) d = 1.0f; float mx = (float) mask - 2.0f; if (d > mx) d = mx; int di = (int) d; float fr = d - (float) di;
      float a = buf[(size_t) ((wr - di) & mask)]; float b = buf[(size_t) ((wr - di - 1) & mask)]; return a + (b - a) * fr; }
    // fb295 — 4-point cubic Hermite (Catmull-Rom) for the PITCH-SHIFT grain read: linear interp aliases badly on an
    // upshifted (octave/fifth) read → the "detuned / wishy-washy / granular slop" Max heard. Cubic ≈ mild built-in
    // band-limit + far lower imaging = a clean, in-tune shift. (4 taps vs 2; only the shifter uses it — the FDN stays linear.)
    static inline float readFracCubic (const std::vector<float>& buf, int mask, int wr, float d)
    { if (d < 2.0f) d = 2.0f; float mx = (float) mask - 3.0f; if (d > mx) d = mx; int di = (int) d; float fr = d - (float) di;
      float ym1 = buf[(size_t) ((wr - di + 1) & mask)]; float y0 = buf[(size_t) ((wr - di) & mask)];
      float y1  = buf[(size_t) ((wr - di - 1) & mask)]; float y2 = buf[(size_t) ((wr - di - 2) & mask)];
      float c1 = 0.5f * (y1 - ym1); float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
      float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
      return ((c3 * fr + c2) * fr + c1) * fr + y0; }
    static inline void fwht8 (float* a)
    { for (int len = 1; len < 8; len <<= 1) for (int i = 0; i < 8; i += (len << 1)) for (int j = i; j < i + len; ++j) { float u = a[j], v = a[j+len]; a[j] = u+v; a[j+len] = u-v; } }
    static inline float fastSin (float ph01) { return std::sin (2.0f * PI * ph01); }
    inline float rand11() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (float) ((rngState >> 8) & 0xFFFFFFu) * (1.0f / 8388607.5f) - 1.0f; }

    static constexpr float PI = 3.14159265358979f;
    static constexpr float FREEZE_G = 0.9999f;
    static constexpr float baseLen48[N] = { 1481.f, 1759.f, 2113.f, 2503.f, 2861.f, 3251.f, 3607.f, 4001.f };
    static constexpr float apLen29k[4]  = { 142.f, 107.f, 379.f, 277.f };
    static constexpr float apBaseG[4]   = { 0.75f, 0.75f, 0.625f, 0.625f };
    static constexpr float tankLen48[N] = { 113.f, 157.f, 197.f, 239.f, 131.f, 179.f, 223.f, 271.f };
    // 6 SHIFT intervals (read-rate ratios): index 2 = Octave Up (+12, the classic) is the shared MODMODE default.
    static constexpr float SHIFTR[6] = {
        1.49831f,   // +7  Fifth
        2.99661f,   // +19 Oct+Fifth
        2.00000f,   // +12 Octave (DEFAULT)
        4.00000f,   // +24 Two Octaves
        0.50000f,   // −12 Octave Down
        1.00595f,   // +0.1 semitone — Detune (a slow chorusy shimmer, not an octave)
    };
    // 8 SHIMMER voicings: decayMul, sizeMul, toneMul, toneAdd, dampAdd, diffFloor, widthAdd, modMul, regenMul, blendMul.
    struct CharBias { float decayMul, sizeMul, toneMul, toneAdd, dampAdd, diffFloor, widthAdd, modMul, regenMul, blendMul; };
    static constexpr CharBias CHAR[8] = {
        /* Shimmer  */ { 1.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.35f, 0.05f, 1.0f, 1.00f, 1.00f },   // classic (default)
        /* Angel    */ { 1.15f, 1.05f, 1.35f, 0.10f,-0.05f, 0.45f, 0.15f, 1.1f, 1.10f, 1.15f },   // bright, ethereal
        /* Ice      */ { 1.10f, 0.95f, 1.60f, 0.15f,-0.08f, 0.55f, 0.10f, 0.8f, 1.05f, 1.20f },   // crystalline, high
        /* Choir    */ { 1.30f, 1.15f, 0.95f, 0.00f, 0.05f, 0.65f, 0.20f, 1.5f, 1.05f, 1.05f },   // lush, vocal
        /* Sparkle  */ { 0.85f, 0.90f, 1.45f, 0.10f,-0.05f, 0.40f, 0.10f, 1.0f, 1.15f, 1.25f },   // bright, fast bloom
        /* Deep     */ { 1.45f, 1.25f, 0.70f, 0.00f, 0.12f, 0.55f, 0.15f, 1.2f, 1.00f, 0.90f },   // darker, slower
        /* Ghost    */ { 1.20f, 1.10f, 0.85f, 0.00f, 0.06f, 0.15f, 0.10f, 1.4f, 0.90f, 0.80f },   // sparse, haunting
        /* Cascade  */ { 1.10f, 1.05f, 1.10f, 0.03f,-0.02f, 0.72f, 0.10f, 1.0f, 1.20f, 1.15f },   // dense, fast-building
    };

    float fs = 48000.0f, smth = 0.001f, shR = 0.03f, dcR = 0.999f, G = 2304.f, tiltCoef = 0.3f;
    bool  primed = false;
    std::vector<float> line[N]; int lineMask[N] = {0}; int lineWr[N] = {0};
    std::vector<float> ap[4];   int apMask[4] = {0};   int apWr[4] = {0}; int apDelay[4] = {0};
    std::vector<float> tank[N]; int tankMask[N] = {0}; int tankWr[N] = {0}; int tankDelay[N] = {0};
    std::vector<float> pre;     int preMask = 0;       int preWr = 0;
    std::vector<float> sh;      int shMask = 0;        int shWr = 0;       // (retired granular grain buffer — kept for ABI/no-op)
    ShimmerPV pv_;              // fb295 — phase-vocoder pitch shifter (clean octave/fifth)
    float winLut[WINLUT] = {0};
    float dampZ[N] = {0}, lfoPh[N] = {0};
    float inLpZ = 0, lcZ = 0, dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0, hpZ = 0;
    float d1 = 0, rvbMonoLast = 0, pitchLpZ = 0, pitchLpCoef = 0.85f;   // fb295 — gentle band-limit on the cubic pitch-shift output (de-fizz)
    float baseDelayT[N] = {0}, baseDelayC[N] = {0}, gLineT[N] = {0}, gLineC[N] = {0};
    float dampT = 0, dampC = 0, apGT[4] = {0}, apGC[4] = {0}, inLpT = 0, inLpC = 0, lcT = 0, lcC = 0, hpCutT = 0, hpCutC = 0;
    float hiGainT = 1, hiGainC = 1, loGainT = 1, loGainC = 1, tiltLpL = 0, tiltLpR = 0;
    float tankGT = 0, tankGC = 0, preSampT = 0, preSampC = 0, modSampT = 0, modSampC = 0, widthT = 0.85f, widthC = 0.85f, modInc = 0, rt60 = 3.0f, mixExt = 0.3f;
    float regenT = 0, regenC = 0, blendT = 0, blendC = 0, shiftRatioT = 2.0f, shiftRatioC = 2.0f, downAmt = 0;
    int   character = 0, shift = 2;                      // Shimmer / Octave (defaults)
    bool  modOn = true, freezeOn = false;
    float freezeCur = 0, freezeTgt = 0, oldRand[N] = {0}, newRand[N] = {0};
    std::uint32_t rngState = 0x9E3779B9u;
    float size = 0.5f, decay = 0.5f, tone = 0.55f, preMs = 15.f, diffuse = 0.75f, modDepth = 0.3f,
          modRate = 0.5f, blend = 0.5f, regen = 0.5f, lowCut = 20.f, width = 0.9f;
};
