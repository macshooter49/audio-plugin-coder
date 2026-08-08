#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// HallReverb — the shared 8-line Feedback Delay Network (Jot/Stautner-Puckette)
// that voices the HALL reverb type (and becomes the reused core for Room/Basin/
// Shimmer). Clean-room from the reverb-research build bible (fb275):
//   pre-delay -> input LP + low-cut -> 4 Schroeder input-diffusion allpasses
//   -> 8 mutually-prime delay lines, each with a one-pole HF-damping LP + a
//      low-shelf bass-decay multiplier -> lossless 8x8 Hadamard (fast WHT, then
//      1/sqrt(8) folded in) -> per-line Jot loss g_i sets RT60.
//   Multi-phase LFO detunes the long lines (anti-metallic). Decorrelated output
//   taps + M/S width. Equal-power dry/wet. DC-blocked. Denormal-flushed.
//
// PURE C++ (no JUCE) so it offline-validates standalone AND drops into the voice
// path. Every setter is a 0..1 (or Hz / seconds) musical value; call
// updateCoefficients() after a batch of setters (block rate).
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <array>
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

        // delay-line buffers (pow2 + mask), sized for max Size (1.8x) + mod headroom
        for (int i = 0; i < N; ++i)
        {
            const int need = (int) std::ceil (baseLen48[i] * sr * 1.8f) + 48;
            int sz = 1; while (sz < need) sz <<= 1;
            line[i].assign ((size_t) sz, 0.0f);
            lineMask[i] = sz - 1;
            lineWr[i]   = 0;
            dampZ[i]    = 0.0f;
            bassZ[i]    = 0.0f;
        }
        // 4 input-diffusion allpasses (Dattorro lengths @29761, scaled to fs)
        for (int i = 0; i < 4; ++i)
        {
            const int len = std::max (8, (int) std::round (apLen29k[i] * (fs / 29761.0f)));
            int sz = 1; while (sz < len + 4) sz <<= 1;
            ap[i].assign ((size_t) sz, 0.0f);
            apMask[i] = sz - 1; apWr[i] = 0; apDelay[i] = len;
        }
        // pre-delay ring (up to 250 ms)
        {
            int need = (int) std::ceil (0.25f * fs) + 8;
            int sz = 1; while (sz < need) sz <<= 1;
            pre.assign ((size_t) sz, 0.0f); preMask = sz - 1; preWr = 0;
        }
        reset();
        updateCoefficients();
    }

    void reset()
    {
        for (int i = 0; i < N; ++i) { std::fill (line[i].begin(), line[i].end(), 0.0f); dampZ[i] = bassZ[i] = 0.0f; lfoPh[i] = (float) i / (float) N; }
        for (int i = 0; i < 4; ++i)  std::fill (ap[i].begin(), ap[i].end(), 0.0f);
        std::fill (pre.begin(), pre.end(), 0.0f);
        inLpZ = lcZ = 0.0f; dcxL = dcyL = dcxR = dcyR = 0.0f;
    }

    // ── params (call updateCoefficients() after) ──────────────────────────────
    void setSize        (float v01) { size    = clamp01 (v01); }   // room scale
    void setDecay       (float v01) { decay   = clamp01 (v01); }   // RT60 0.3..20 s (exp)
    void setTone        (float v01) { tone    = clamp01 (v01); }   // dark<->bright input tilt
    void setMix         (float v01) { mix     = clamp01 (v01); }   // equal-power dry/wet
    void setPreDelayMs  (float ms ) { preMs   = (ms < 0 ? 0 : (ms > 250 ? 250 : ms)); }
    void setDiffusion   (float v01) { diffuse = clamp01 (v01); }   // grain -> glass
    void setModDepth    (float v01) { modDepth= clamp01 (v01); }
    void setModRate     (float hz ) { modRate = (hz < 0.01f ? 0.01f : (hz > 8.0f ? 8.0f : hz)); }
    void setHighDamping (float v01) { hiDamp  = clamp01 (v01); }   // crystal -> felt
    void setLowDecay    (float mult){ lowDecay= (mult < 0.25f ? 0.25f : (mult > 2.0f ? 2.0f : mult)); }
    void setLowCutHz    (float hz ) { lowCut  = (hz < 20 ? 20 : (hz > 1000 ? 1000 : hz)); }
    void setWidth       (float v01) { width   = clamp01 (v01); }

    void updateCoefficients()
    {
        // RT60 0.3..20 s (exp map) ; Size scales all delay lengths 0.5..1.8x
        rt60      = 0.3f * std::pow (20.0f / 0.3f, decay);
        sizeScale = 0.5f + 1.3f * size;
        const float sr = fs / 48000.0f;

        for (int i = 0; i < N; ++i)
        {
            float d = baseLen48[i] * sr * sizeScale;            // delay in samples
            if (d > (float) lineMask[i] - 26.0f) d = (float) lineMask[i] - 26.0f;
            baseDelay[i] = d;
            // Jot per-line broadband loss for the target RT60 (folds Hadamard 1/sqrt(8) so the
            // matrix stays orthonormal without a separate scale). g = 10^(-3 d /(fs RT60)).
            float g = std::pow (10.0f, -3.0f * d / (fs * rt60));
            if (g > 0.9995f) g = 0.9995f;                       // freeze safety cap
            gLine[i] = g;
        }
        // per-line HF damping one-pole LP coefficient (0 = none/bright, ->0.9 = dark felt)
        dampCoef = 0.9f * hiDamp;
        // low-shelf extra gain so low band reaches RT60*lowDecay (loop gain stays < g < 1)
        //   low effective g = g^(1/lowDecay)  ->  extra = g^(1/lowDecay - 1)
        lowExtraPow = 1.0f / lowDecay - 1.0f;
        // crossover ~400 Hz one-pole split
        bassCoef = std::exp (-2.0f * (float) M_PI * 400.0f / fs);
        // input bandwidth LP from Tone (dark ~1.5 kHz -> bright ~18 kHz)
        float toneHz = 1500.0f * std::pow (18000.0f / 1500.0f, tone);
        inLpCoef = std::exp (-2.0f * (float) M_PI * toneHz / fs);
        // low cut one-pole HP
        lcCoef = std::exp (-2.0f * (float) M_PI * lowCut / fs);
        // input diffusion allpass gain (0 = pass, 1 = full Dattorro density)
        for (int i = 0; i < 4; ++i) apG[i] = diffuse * apBaseG[i];
        // pre-delay in samples
        preSamp = preMs * 0.001f * fs;
        // modulation
        modInc   = modRate / fs;
        modSampP = modDepth * 22.0f;      // peak excursion in samples
        // DC blocker
        dcR = 1.0f - (126.0f / fs);       // ~20 Hz
        // equal-power mix
        dryG = std::cos (mix * 0.5f * (float) M_PI);
        wetG = std::sin (mix * 0.5f * (float) M_PI);
    }

    // stereo, in-place block
    void process (float* L, float* R, int n)
    {
        for (int s = 0; s < n; ++s)
        {
            float dryL = L[s], dryR = R[s];
            float outL, outR;
            processSample (dryL, dryR, outL, outR);
            L[s] = dryG * dryL + wetG * outL;
            R[s] = dryG * dryR + wetG * outR;
        }
    }

    // wet-only per-sample (dry mixing done by caller or process())
    inline void processSample (float inL, float inR, float& wetL, float& wetR)
    {
        float x = 0.5f * (inL + inR);                       // mono send

        // pre-delay
        pre[(size_t) preWr] = x;
        {
            float rp = (float) preWr - preSamp; int i0; float fr;
            fracIndex (rp, preMask, i0, fr);
            x = pre[(size_t) i0] * (1.0f - fr) + pre[(size_t) ((i0 - 1) & preMask)] * fr;
            preWr = (preWr + 1) & preMask;
        }
        // input bandwidth LP (tone) + low cut (HP)
        inLpZ = (1.0f - inLpCoef) * x + inLpCoef * inLpZ;   x = inLpZ;
        lcZ   = (1.0f - lcCoef) * x + lcCoef * lcZ;          x = x - lcZ;   // one-pole HP
        // 4 series Schroeder input-diffusion allpasses
        for (int i = 0; i < 4; ++i)
            x = allpass (i, x, apG[i]);

        // ── FDN read: delayed -> damping LP -> bass low-shelf ──
        float sIn[N];
        for (int i = 0; i < N; ++i)
        {
            float mod = modSampP * fastSin (lfoPh[i]);
            float dd  = baseDelay[i] + mod;
            if (dd < 1.0f) dd = 1.0f;
            float rp = (float) lineWr[i] - dd; int i0; float fr;
            fracIndex (rp, lineMask[i], i0, fr);
            float v = line[i][(size_t) i0] * (1.0f - fr) + line[i][(size_t) ((i0 - 1) & lineMask[i])] * fr;
            // HF damping one-pole LP (DC gain = 1 -> RT60 preserved at DC, HF decays faster)
            dampZ[i] = (1.0f - dampCoef) * v + dampCoef * dampZ[i];
            v = dampZ[i];
            // low-shelf bass decay: split low band, apply extra gain g^(1/lowDecay - 1)
            bassZ[i] = (1.0f - bassCoef) * v + bassCoef * bassZ[i];  // low band
            float lowGain = fastPow (gLine[i], lowExtraPow);
            v = (v - bassZ[i]) + bassZ[i] * lowGain;                 // high + shelved low
            sIn[i] = gLine[i] * v;                                   // Jot loss
            lfoPh[i] += modInc; if (lfoPh[i] >= 1.0f) lfoPh[i] -= 1.0f;
        }
        // ── lossless mix (fast Walsh-Hadamard) + 1/sqrt(8) ──
        float m[N]; for (int i = 0; i < N; ++i) m[i] = sIn[i];
        fwht8 (m);
        const float hs = 0.35355339f;                        // 1/sqrt(8)
        // ── write back: diffused input + matrix feedback ──
        const float inG = 0.5f;                              // input into the tank
        for (int i = 0; i < N; ++i)
        {
            float w = inG * x + hs * m[i];
            w = flush (w);
            line[i][(size_t) lineWr[i]] = w;
            lineWr[i] = (lineWr[i] + 1) & lineMask[i];
        }
        // ── output taps: decorrelated even/odd -> L/R, then M/S width ──
        float wl = (sIn[0] + sIn[2] + sIn[4] + sIn[6]) * 0.5f;
        float wr = (sIn[1] + sIn[3] + sIn[5] + sIn[7]) * 0.5f;
        float mid = 0.5f * (wl + wr);
        float sid = 0.5f * (wl - wr) * (0.15f + 1.85f * width);   // width: near-mono -> ultra-wide
        wl = mid + sid; wr = mid - sid;
        // DC blockers
        float ol = wl - dcxL + dcR * dcyL; dcxL = wl; dcyL = ol;
        float orr= wr - dcxR + dcR * dcyR; dcxR = wr; dcyR = orr;
        wetL = ol; wetR = orr;
    }

private:
    // ── helpers ──
    static inline float clamp01 (float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    static inline float flush (float v) { return (v > -1e-20f && v < 1e-20f) ? 0.0f : v; }
    static inline void fracIndex (float rp, int mask, int& i0, float& fr)
    {
        // rp may be negative; wrap into [0, mask]
        float sz = (float) (mask + 1);
        while (rp < 0.0f) rp += sz;
        i0 = (int) rp; fr = rp - (float) i0; i0 &= mask;
    }
    inline float allpass (int i, float x, float g)
    {
        float rp = (float) apWr[i] - (float) apDelay[i]; int i0; float fr;
        fracIndex (rp, apMask[i], i0, fr);
        float d = ap[i][(size_t) i0] * (1.0f - fr) + ap[i][(size_t) ((i0 - 1) & apMask[i])] * fr;
        float in = x + (-g) * d;               // Schroeder allpass
        ap[i][(size_t) apWr[i]] = flush (in);
        apWr[i] = (apWr[i] + 1) & apMask[i];
        return d + g * in;
    }
    static inline void fwht8 (float* a)        // in-place fast Walsh-Hadamard, N=8
    {
        for (int len = 1; len < 8; len <<= 1)
            for (int i = 0; i < 8; i += (len << 1))
                for (int j = i; j < i + len; ++j)
                { float u = a[j], v = a[j + len]; a[j] = u + v; a[j + len] = u - v; }
    }
    static inline float fastSin (float ph01)   // ph in [0,1) -> sin(2pi ph), cheap parabola
    {
        float x = ph01 - 0.5f;                 // [-0.5,0.5)
        // 4th-order-ish sine approx over the phase; good enough for mod (not audio-band)
        return std::sin (2.0f * (float) M_PI * ph01) + 0.0f * x;
    }
    static inline float fastPow (float base, float e)
    {
        if (e == 0.0f) return 1.0f;
        return std::pow (base, e);
    }

    // ── delay-line base lengths (samples @48k): 8 mutually-prime, ~35–85 ms hall spread ──
    static constexpr float baseLen48[N] = { 1699.f, 2003.f, 2399.f, 2699.f, 3011.f, 3347.f, 3701.f, 4099.f };
    static constexpr float apLen29k[4]  = { 142.f, 107.f, 379.f, 277.f };
    static constexpr float apBaseG[4]   = { 0.75f, 0.75f, 0.625f, 0.625f };

    float fs = 48000.0f;
    // buffers
    std::vector<float> line[N]; int lineMask[N] = {0}; int lineWr[N] = {0};
    std::vector<float> ap[4];   int apMask[4] = {0};   int apWr[4] = {0}; int apDelay[4] = {0};
    std::vector<float> pre;     int preMask = 0;       int preWr = 0;
    // state
    float dampZ[N] = {0}, bassZ[N] = {0}, lfoPh[N] = {0};
    float inLpZ = 0, lcZ = 0, dcxL = 0, dcyL = 0, dcxR = 0, dcyR = 0;
    // coeffs
    float baseDelay[N] = {0}, gLine[N] = {0}, apG[4] = {0};
    float rt60 = 3.0f, sizeScale = 1.0f, dampCoef = 0, bassCoef = 0, lowExtraPow = 0;
    float inLpCoef = 0, lcCoef = 0, preSamp = 0, modInc = 0, modSampP = 0, dcR = 0.999f, dryG = 1, wetG = 0;
    // param state
    float size = 0.3f, decay = 0.55f, tone = 0.5f, mix = 0.3f, preMs = 20.f, diffuse = 0.7f,
          modDepth = 0.25f, modRate = 0.4f, hiDamp = 0.35f, lowDecay = 1.0f, lowCut = 20.f, width = 0.8f;
};
