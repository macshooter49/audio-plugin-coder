#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ConvolutionReverb — the CONVOLUTION reverb type (fb291), the 9th and LAST. The ONE
// non-synthetic reverb: it convolves the input with an IMPULSE RESPONSE (a real/measured
// space's fingerprint) instead of synthesising the tail with delays/feedback — so it can be
// authentically a specific room, AND it exposes controls the algorithmic reverbs CAN'T:
// REVERSE (true backwards reverb), ATTACK (gated/swelling onset), DISTANCE (near ER-heavy ↔
// far tail-heavy), DENSITY (smear the IR into a wash). Built-in synthetic Character spaces
// ship now; user IR drag-drop lands next (the engine already loads any float IR via setUserIR).
//
// Pure C++ (no JUCE): a hand-rolled radix-2 FFT + UNIFORM-PARTITIONED OVERLAP-SAVE convolution
// (block B=512 ⇒ ONE latency number: 512 samples / 10.7 ms @48k — fine on a reverb send, reads
// as a touch of pre-delay). Everything PRE-ALLOCATED; the IR is baked + re-partitioned on the
// audio thread (rate-limited) into a DOUBLE-BUFFERED spectra set and cross-faded on a SHARED
// input FDL (both convolutions see the same history) ⇒ click-free IR changes, no threads, no
// audio-thread allocation. Convolution is FIR ⇒ unconditionally stable. Offline-validatable.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>

class ConvolutionReverb
{
public:
    static constexpr int B    = 512;         // partition / block size = the latency
    static constexpr int FN   = 1024;        // FFT size (2B)
    static constexpr int FLOG = 10;          // log2(FN)
    static constexpr int HB   = FN / 2 + 1;  // half-spectrum bins (real FFT)
    static constexpr int MAXP = 288;         // max partitions → ~3.07 s @48k

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        // FFT tables (bit-reversal + twiddles)
        for (int i = 0; i < FN; ++i) { int r = 0, x = i; for (int b = 0; b < FLOG; ++b) { r = (r << 1) | (x & 1); x >>= 1; } bitrev[i] = r; }
        for (int i = 0; i < FN / 2; ++i) { twRe[i] = std::cos (-2.0f * PI * i / FN); twIm[i] = std::sin (-2.0f * PI * i / FN); }
        // pre-size everything (no audio-thread allocation ever after this)
        for (int s = 0; s < 2; ++s) { HreBuf[s].assign ((size_t) MAXP * HB, 0.0f); HimBuf[s].assign ((size_t) MAXP * HB, 0.0f);
                                      HreBufR[s].assign ((size_t) MAXP * HB, 0.0f); HimBufR[s].assign ((size_t) MAXP * HB, 0.0f); Pcnt[s] = 0; }
        fdlRe.assign ((size_t) MAXP * HB, 0.0f); fdlIm.assign ((size_t) MAXP * HB, 0.0f);
        baseL.assign ((size_t) MAXP * B, 0.0f); baseR.assign ((size_t) MAXP * B, 0.0f);
        bakeL.assign ((size_t) MAXP * B, 0.0f); bakeR.assign ((size_t) MAXP * B, 0.0f);
        tmpL.assign ((size_t) MAXP * B, 0.0f);  tmpR.assign ((size_t) MAXP * B, 0.0f);
        preMax = (int) (0.25f * fs) + 16; { int sz = 1; while (sz < preMax + 4) sz <<= 1; preBuf.assign ((size_t) sz, 0.0f); preMask = sz - 1; }
        modMax = (int) (0.03f * fs) + 8;  { int sz = 1; while (sz < modMax + 8) sz <<= 1; modBufL.assign ((size_t) sz, 0.0f); modBufR.assign ((size_t) sz, 0.0f); modMask = sz - 1; }
        smth = 1.0f - std::exp (-1.0f / (0.015f * fs));
        dcR  = 1.0f - (126.0f / fs);
        tiltCoef = 1.0f - std::exp (-2.0f * PI * 1100.0f / fs);
        reset();
        baseChar = baseShape = -1;   // force first synth
        primed = false;
        updateCoefficients();
        forceRebake();               // build the initial IR
    }

    void reset()
    {
        std::fill (fdlRe.begin(), fdlRe.end(), 0.0f); std::fill (fdlIm.begin(), fdlIm.end(), 0.0f);
        std::fill (frame.begin(), frame.end(), 0.0f); std::fill (inA.begin(), inA.end(), 0.0f); std::fill (inB.begin(), inB.end(), 0.0f);
        std::fill (outBlk.begin(), outBlk.end(), 0.0f); std::fill (outBlkR.begin(), outBlkR.end(), 0.0f);
        std::fill (outOld.begin(), outOld.end(), 0.0f); std::fill (outOldR.begin(), outOldR.end(), 0.0f);
        std::fill (preBuf.begin(), preBuf.end(), 0.0f); std::fill (modBufL.begin(), modBufL.end(), 0.0f); std::fill (modBufR.begin(), modBufR.end(), 0.0f);
        phase = 0; fdlHead = 0; preWr = modWr = 0; modPh = 0.0f;
        lcZ = tiltLpL = tiltLpR = 0.0f; xfActive = false; xfGain = 1.0f;
    }

    // ── params (0..1 unless noted) ──
    void setSize        (float v) { size    = clamp01 (v); }        // IR length / space scale (bake)
    void setDecay       (float v) { decayP  = clamp01 (v); }        // IR decay envelope (bake)
    void setTone        (float v) { tone    = clamp01 (v); }        // real-time output tilt
    void setPreDelayMs  (float ms){ preMs   = clampf (ms, 0.0f, 240.0f); }
    void setDensity     (float v) { density = clamp01 (v); }        // DIFFUSE slot → smear the IR (bake)
    void setMotionDepth (float v) { motDepth= clamp01 (v); }        // MODDEPTH → real-time chorus depth
    void setMotionRate  (float hz){ motRate = clampf (hz, 0.01f, 8.0f); }  // MODRATE
    void setAttack      (float v) { attack  = clamp01 (v); }        // HIDAMP slot → IR onset fade-in (bake)
    void setDistance    (float v) { distance= clamp01 (v); }        // LOWDECAY slot → ER↔tail balance (bake)
    void setLowCutHz    (float hz){ lowCut  = clampf (hz, 20.0f, 1000.0f); }
    void setWidth       (float v) { width   = clamp01 (v); }
    void setCharacter   (int c)   { character = c < 0 ? 0 : (c > 7 ? 7 : c); }   // 8 IR spaces (bake)
    void setShape       (int s)   { shape     = s < 0 ? 0 : (s > 5 ? 5 : s); }   // 6 ER patterns (bake)
    void setReverse     (bool r)  { reverse = r; }                  // FREEZE slot → true backwards reverb (bake)
    void setModEnabled  (bool on) { modOn   = on; }
    void setMix         (float v) { mixExt  = clamp01 (v); }

    // load an arbitrary user IR (stereo; if mono pass same for L/R). len in samples. Triggers a rebake.
    void setUserIR (const float* irL, const float* irR, int len)
    {
        userLen = len < 4 ? 0 : (len > MAXP * B ? MAXP * B : len);
        if (userLen > 0) { userL.assign ((size_t) userLen, 0.0f); userR.assign ((size_t) userLen, 0.0f);
                           for (int i = 0; i < userLen; ++i) { userL[i] = irL[i]; userR[i] = irR ? irR[i] : irL[i]; } }
        baseChar = -1; irDirty = true;   // force re-synth from the user IR
    }
    void clearUserIR () { userLen = 0; baseChar = -1; irDirty = true; }

    void updateCoefficients()
    {
        // Bake-affecting params: if any changed enough, mark dirty (rate-limited rebake happens in process()).
        if (character != baseChar || shape != baseShape) irDirty = true;
        if (paramMoved (size, lSize) | paramMoved (decayP, lDecay) | paramMoved (attack, lAttack)
          | paramMoved (distance, lDist) | paramMoved (density, lDens) || reverse != lRev) irDirty = true;
        lRev = reverse;
        // Real-time: output tone tilt (identity at 0.5), low cut, motion, pre-delay.
        float tilt = (tone - 0.5f) * 2.0f;
        hiGainT = std::pow (3.4f, tilt); loGainT = std::pow (1.9f, -tilt);
        lcT = std::exp (-2.0f * PI * lowCut / fs);
        preSampT = clampf (preMs * 0.001f * fs, 0.0f, (float) preMax - 2.0f);
        modDepthT = (modOn ? motDepth : 0.0f) * 0.012f * fs;   // ± up to ~12 ms chorus
        modIncT   = (motRate * 0.9f) / fs;
        modIncC   = modIncT;   // rate: snap per block (a phase increment doesn't click)
        widthT = width;
        if (! primed) { hiGainC = hiGainT; loGainC = loGainT; lcC = lcT; preSampC = preSampT; modDepthC = modDepthT; widthC = widthT; primed = true; }
    }

    // block-process a mono send buffer → stereo wet (mixExt applied by the caller like the other engines).
    // (Terrain calls processSample per-sample; this engine buffers internally so both work.)
    inline void processSample (float inL, float inR, float& wetL, float& wetR)
    {
        // rate-limited rebake (audio-thread, pre-allocated, click-free via crossfade) — at block boundaries only
        hiGainC += (hiGainT - hiGainC) * smth; loGainC += (loGainT - loGainC) * smth; lcC += (lcT - lcC) * smth;
        preSampC += (preSampT - preSampC) * smth; modDepthC += (modDepthT - modDepthC) * smth; widthC += (widthT - widthC) * smth;

        float x = 0.5f * (inL + inR);
        (*inCur)[(size_t) phase] = x;
        float yL = outBlk[(size_t) phase], yR = outBlkR[(size_t) phase];
        if (xfActive) { yL = xfGain * yL + (1.0f - xfGain) * outOld[(size_t) phase];
                        yR = xfGain * yR + (1.0f - xfGain) * outOldR[(size_t) phase];
                        xfGain += xfStep; if (xfGain >= 1.0f) { xfGain = 1.0f; xfActive = false; } }
        ++phase;
        if (phase >= B) { phase = 0; runBlock(); }

        // ── real-time WET chain: pre-delay → motion (chorus) → low-cut → tone tilt → width ──
        // pre-delay (mono-ish: delay both taps by the same amount)
        preBuf[(size_t) preWr] = 0.5f * (yL + yR); int pr = preWr; preWr = (preWr + 1) & preMask;
        // (a light pre-delay reads from the shared line; keeps CPU low and the wet coherent)
        float pd = readFrac (preBuf, preMask, pr + 1, preSampC);
        // motion — modulated delay on each channel (chorus/wobble)
        modBufL[(size_t) modWr] = yL; modBufR[(size_t) modWr] = yR; int mr = modWr; modWr = (modWr + 1) & modMask;
        float lfo = fastSin (modPh); modPh += modIncC; if (modPh >= 1.0f) modPh -= 1.0f;
        float md = 4.0f + modDepthC * (0.5f + 0.5f * lfo);
        yL = readFrac (modBufL, modMask, mr + 1, md);
        yR = readFrac (modBufR, modMask, mr + 1, 4.0f + modDepthC * (0.5f + 0.5f * fastSin (modPh + 0.25f)));
        if (preSampC > 1.0f) { yL = 0.5f * yL + 0.5f * pd; yR = 0.5f * yR + 0.5f * pd; }   // blend the pre-delayed copy in
        // low cut
        lcZ = (1.0f - lcC) * (0.5f * (yL + yR)) + lcC * lcZ;
        yL -= lcZ; yR -= lcZ;
        // tone tilt
        tiltLpL += (yL - tiltLpL) * tiltCoef; { float hi = yL - tiltLpL; yL = tiltLpL * loGainC + hi * hiGainC; }
        tiltLpR += (yR - tiltLpR) * tiltCoef; { float hi = yR - tiltLpR; yR = tiltLpR * loGainC + hi * hiGainC; }
        // width
        float mid = 0.5f * (yL + yR), sid = 0.5f * (yL - yR) * (0.15f + 1.85f * widthC);
        wetL = mid + sid; wetR = mid - sid;
    }
    // pre-sample-loop hook: do the (possibly heavy) rebake here so it can't spike inside the tight loop
    void prepareBlock () { if (irDirty && ! xfActive && ! xfPending && (sinceBake += B) >= (int) (0.05f * fs)) rebake(); }
    void process (float* L, float* R, int n)
    { const float dg = std::cos (0.5f * PI * mixExt), wg = std::sin (0.5f * PI * mixExt);
      prepareBlock(); for (int s = 0; s < n; ++s) { float wl, wr; processSample (L[s], R[s], wl, wr); L[s] = dg * L[s] + wg * wl; R[s] = dg * R[s] + wg * wr; } }
    void setMixExternal (float v) { mixExt = clamp01 (v); }
    int  getLatency () const { return B; }

    // expose the current baked IR (for offline validation / a future viz)
    const std::vector<float>& bakedL () const { return bakeL; }  const std::vector<float>& bakedR () const { return bakeR; }  int bakedLen () const { return curBakeLen; }
    // offline self-test of the FFT (0 = OK; 1/2/3 = which check failed)
    int selfTestFFT ()
    {
        for (int i = 0; i < FN; ++i) { fr[i] = (i == 0) ? 1.0f : 0.0f; fi[i] = 0.0f; }
        fft (fr.data(), fi.data(), false);
        for (int k = 0; k < FN; ++k) if (std::fabs (fr[k] - 1.0f) > 1e-3f || std::fabs (fi[k]) > 1e-3f) return 1;
        std::vector<float> sv (FN); for (int i = 0; i < FN; ++i) { fr[i] = std::sin (2.0f * PI * 5 * i / FN); fi[i] = 0.0f; sv[i] = fr[i]; }
        fft (fr.data(), fi.data(), false); fft (fr.data(), fi.data(), true);
        for (int i = 0; i < FN; ++i) if (std::fabs (fr[i] - sv[i]) > 1e-3f) return 2;
        for (int i = 0; i < FN; ++i) { fr[i] = (i == 3) ? 1.0f : 0.0f; fi[i] = 0.0f; }
        fft (fr.data(), fi.data(), false);
        for (int k = 0; k < FN; ++k) { float er = std::cos (-2.0f * PI * k * 3.0f / FN), ei = std::sin (-2.0f * PI * k * 3.0f / FN);
            if (std::fabs (fr[k] - er) > 1e-2f || std::fabs (fi[k] - ei) > 1e-2f) return 3; }
        return 0;
    }
    void forceRebake () { irDirty = true; sinceBake = 1 << 30; rebake(); xfActive = false; xfPending = false; xfGain = 1.0f; }   // HARD swap (no crossfade)

private:
    static inline float clamp01 (float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    static inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static inline float fastSin (float ph01) { return std::sin (2.0f * PI * ph01); }
    static inline float readFrac (const std::vector<float>& b, int mask, int wr, float d)
    { if (d < 1.0f) d = 1.0f; int di = (int) d; float fr = d - (float) di; float a = b[(size_t)((wr - di) & mask)]; float c = b[(size_t)((wr - di - 1) & mask)]; return a + (c - a) * fr; }
    inline float rand11 () { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)((rng >> 8) & 0xFFFFFFu) * (1.0f / 8388607.5f) - 1.0f; }
    bool paramMoved (float v, float& last) { if (std::fabs (v - last) > 0.008f) { last = v; return true; } return false; }

    // ── radix-2 iterative FFT (in-place, re/im). inv=false forward, true inverse (÷N). ──
    void fft (float* re, float* im, bool inv)
    {
        for (int i = 0; i < FN; ++i) { int j = bitrev[i]; if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); } }
        for (int len = 2; len <= FN; len <<= 1)
        {
            int half = len >> 1, step = FN / len;
            for (int i = 0; i < FN; i += len)
                for (int k = 0; k < half; ++k)
                {
                    float wr = twRe[k * step], wi = inv ? -twIm[k * step] : twIm[k * step];
                    int a = i + k, b2 = i + k + half;
                    float tr = wr * re[b2] - wi * im[b2], ti = wr * im[b2] + wi * re[b2];
                    re[b2] = re[a] - tr; im[b2] = im[a] - ti; re[a] += tr; im[a] += ti;
                }
        }
        if (inv) { const float s = 1.0f / FN; for (int i = 0; i < FN; ++i) { re[i] *= s; im[i] *= s; } }
    }

    // FFT a length-≤B real partition (zero-padded to FN) → half spectrum (HB bins) at dst offset.
    void fftPartition (const float* src, int len, float* dstRe, float* dstIm)
    {
        for (int i = 0; i < FN; ++i) { fr[i] = (i < len) ? src[i] : 0.0f; fi[i] = 0.0f; }
        fft (fr.data(), fi.data(), false);
        for (int k = 0; k < HB; ++k) { dstRe[k] = fr[k]; dstIm[k] = fi[k]; }
    }

    // one convolution block (called at each B boundary): FFT the 2B frame, MAC over partitions, IFFT → outBlk.
    void runBlock ()
    {
        // frame = [prev B | cur B]
        for (int i = 0; i < B; ++i) { frame[(size_t) i] = (*inPrev)[(size_t) i]; frame[(size_t)(B + i)] = (*inCur)[(size_t) i]; }
        std::swap (inPrev, inCur);
        for (int i = 0; i < FN; ++i) { fr[i] = frame[(size_t) i]; fi[i] = 0.0f; }
        fft (fr.data(), fi.data(), false);
        float* Fre = &fdlRe[(size_t) fdlHead * HB]; float* Fim = &fdlIm[(size_t) fdlHead * HB];
        for (int k = 0; k < HB; ++k) { Fre[k] = fr[k]; Fim[k] = fi[k]; }

        convOne (aSlot, outBlk, outBlkR);
        if (xfPending) { convOne (oSlot, outOld, outOldR); xfActive = true; xfGain = 0.0f; xfPending = false; }   // both slots valid → start the crossfade
        else if (xfActive) convOne (oSlot, outOld, outOldR);

        fdlHead = (fdlHead + 1) % MAXP;
    }
    // convolve the shared FDL with slot's IR spectra → dstL/dstR output block (last B of the IFFT).
    void convOne (int slot, std::vector<float>& dstL, std::vector<float>& dstR)
    {
        const float* Hre = HreBuf[slot].data(); const float* Him = HimBuf[slot].data();
        const float* HreR= HreBufR[slot].data();const float* HimR= HimBufR[slot].data();
        int P = Pcnt[slot];
        for (int k = 0; k < HB; ++k) { ar[k] = ai[k] = arR[k] = aiR[k] = 0.0f; }
        for (int p = 0; p < P; ++p)
        {
            int fi2 = (fdlHead - p + MAXP) % MAXP; const float* Fre = &fdlRe[(size_t) fi2 * HB]; const float* Fim = &fdlIm[(size_t) fi2 * HB];
            const float* hr = Hre + (size_t) p * HB; const float* hi2 = Him + (size_t) p * HB;
            const float* hrR= HreR+ (size_t) p * HB; const float* hiR= HimR+ (size_t) p * HB;
            for (int k = 0; k < HB; ++k)
            { float xr = Fre[k], xi = Fim[k];
              ar[k]  += xr * hr[k]  - xi * hi2[k];  ai[k]  += xr * hi2[k]  + xi * hr[k];
              arR[k] += xr * hrR[k] - xi * hiR[k];  aiR[k] += xr * hiR[k]  + xi * hrR[k]; }
        }
        ifftHalf (ar.data(), ai.data(), dstL);
        ifftHalf (arR.data(), aiR.data(), dstR);
    }
    // reconstruct full spectrum from half (conj-symmetric), IFFT, take the LAST B samples (overlap-save).
    void ifftHalf (const float* hre, const float* him, std::vector<float>& dst)
    {
        for (int k = 0; k < HB; ++k) { fr[k] = hre[k]; fi[k] = him[k]; }
        for (int k = HB; k < FN; ++k) { fr[k] = hre[FN - k]; fi[k] = -him[FN - k]; }
        fft (fr.data(), fi.data(), true);
        for (int i = 0; i < B; ++i) dst[(size_t) i] = fr[B + i];
    }

    // ── IR SYNTHESIS (8 Character spaces × 6 Shape ER patterns) → baseL/baseR (time domain) ──
    int synth ()
    {
        rng = 0x2545F491u ^ (std::uint32_t) (character * 2654435761u) ^ (std::uint32_t) (shape * 40503u);
        if (userLen > 0)   // a loaded user IR is the base
        { int n = userLen; for (int i = 0; i < n; ++i) { baseL[(size_t) i] = userL[(size_t) i]; baseR[(size_t) i] = userR[(size_t) i]; } return n; }
        const CharBias cb = CHAR[character];
        const ShapeBias sb = SHAPE[shape];
        int len = (int) (cb.lenSec * fs); if (len > MAXP * B) len = MAXP * B; if (len < B) len = B;
        for (int i = 0; i < len; ++i) baseL[(size_t) i] = baseR[(size_t) i] = 0.0f;
        // early reflections — discrete taps (Shape sets count/spread/pattern)
        int nER = (int) (cb.erCount * sb.erMul);
        float preSamp = cb.preMs * 0.001f * fs;
        for (int e = 0; e < nER; ++e)
        {
            float t = preSamp + std::pow ((float) (e + 1) / nER, sb.erCurve) * cb.erSpread * fs;
            int ti = (int) t; if (ti >= len - 2) continue;
            float g = cb.erGain * std::pow (0.86f, (float) e) * (0.6f + 0.4f * rand11());
            float pan = clampf (rand11() * sb.erPan, -1.0f, 1.0f);
            baseL[(size_t) ti] += g * std::sqrt (0.5f * (1.0f - pan));
            baseR[(size_t) ti] += g * std::sqrt (0.5f * (1.0f + pan));
        }
        // diffuse tail — exponentially-decaying band-limited noise, L/R decorrelated
        float tau = cb.lenSec * fs * cb.tailDecay;
        float lpc = std::exp (-2.0f * PI * (cb.bright * 1000.0f) / fs);   // tail brightness (one-pole LP)
        float zL = 0.0f, zR = 0.0f;
        int tailStart = (int) (preSamp + 0.004f * fs);
        float fadeLen = 0.035f * fs;   // the diffuse tail BUILDS over ~35 ms so the discrete ERs read up front
        float sw = sb.erPan;           // Shape also sets the TAIL decorrelation: Tight=narrow (correlated), Wide=wide
        for (int i = tailStart; i < len; ++i)
        {
            float t = (float) (i - tailStart);
            float fadeIn = t < fadeLen ? (t / fadeLen) * (t / fadeLen) : 1.0f;
            float env = std::exp (-t / tau) * cb.tailGain * fadeIn;
            zL = (1.0f - lpc) * rand11() + lpc * zL;
            zR = (1.0f - lpc) * rand11() + lpc * zR;
            baseL[(size_t) i] += zL * env;
            baseR[(size_t) i] += (sw * zR + (1.0f - sw) * zL) * env;   // Shape-dependent L/R correlation
        }
        return len;
    }

    // ── IR BAKE: Distance → Density → Decay → Attack → Size(resample) → Reverse → Normalize ──
    void rebake ()
    {
        irDirty = false; sinceBake = 0;
        if (character != baseChar || shape != baseShape || baseChar < 0) { baseLen = synth(); baseChar = character; baseShape = shape; }
        int n = baseLen;
        // work in tmp (copy base)
        for (int i = 0; i < n; ++i) { tmpL[(size_t) i] = baseL[(size_t) i]; tmpR[(size_t) i] = baseR[(size_t) i]; }
        int erEnd = (int) (0.05f * fs);   // ~50 ms = the ER region
        // DISTANCE: near (0) boosts ER + cuts tail; far (1) cuts ER + boosts tail (+ recede)
        { float erG = 1.0f - 0.85f * distance, tG = 0.45f + 1.1f * distance;
          for (int i = 0; i < n; ++i) { float g = (i < erEnd) ? erG : tG; tmpL[(size_t) i] *= g; tmpR[(size_t) i] *= g; } }
        // DENSITY: smear the discrete ERs into a wash. A normalized moving-average (box) spreads each spike
        // over a density-scaled window and FILLS the gaps between discrete reflections → the crest drops and
        // the IR reads dense/continuous instead of crisp/discrete. (0 = crisp, 1 = washy.)
        if (density > 0.01f)
        {
            int W = 1 + (int) (density * density * 0.010f * fs); if (W > 900) W = 900; if (W < 2) W = 2;
            const float inv = 1.0f / (float) W;
            for (int ch = 0; ch < 2; ++ch)
            { std::vector<float>& v = ch ? tmpR : tmpL;
              for (int j = 0; j < W; ++j) apScratch[j] = 0.0f; int wr = 0; float run = 0.0f;
              for (int i = 0; i < n; ++i)
              { run += v[(size_t) i] - apScratch[wr]; apScratch[wr] = v[(size_t) i]; if (++wr >= W) wr = 0; v[(size_t) i] = run * inv; } }
        }
        // DECAY: extra decay window — decayP 1 = full IR, 0 = heavily shortened
        { float k = (1.0f - decayP) * 6.0f / (float) n;
          if (k > 1e-7f) for (int i = 0; i < n; ++i) { float g = std::exp (-k * i); tmpL[(size_t) i] *= g; tmpR[(size_t) i] *= g; } }
        // ATTACK: fade-in the onset — attack·0.3 s raised-cosine window (removes direct → gated/swell)
        { int aLen = (int) (attack * 0.30f * fs); if (aLen > n) aLen = n;
          for (int i = 0; i < aLen; ++i) { float w = 0.5f - 0.5f * std::cos (PI * (float) i / aLen); tmpL[(size_t) i] *= w; tmpR[(size_t) i] *= w; } }
        // SIZE: resample the IR length (0.5 = 1×; 0.4×..2.0× space scale)
        int m = n;
        { float ratio = 0.4f + size * 1.6f; m = (int) (n * ratio); if (m > MAXP * B) m = MAXP * B; if (m < B) m = B;
          for (int i = 0; i < m; ++i) { float sp = (float) i / ratio; int i0 = (int) sp; float f = sp - i0; int i1 = i0 + 1;
              float aL = (i0 < n) ? tmpL[(size_t) i0] : 0.0f, bL = (i1 < n) ? tmpL[(size_t) i1] : 0.0f;
              float aR = (i0 < n) ? tmpR[(size_t) i0] : 0.0f, bR = (i1 < n) ? tmpR[(size_t) i1] : 0.0f;
              bakeL[(size_t) i] = aL + (bL - aL) * f; bakeR[(size_t) i] = aR + (bR - aR) * f; } }
        // REVERSE: true backwards reverb (flip the fully-shaped IR)
        if (reverse) for (int i = 0; i < m / 2; ++i) { std::swap (bakeL[(size_t) i], bakeL[(size_t)(m - 1 - i)]); std::swap (bakeR[(size_t) i], bakeR[(size_t)(m - 1 - i)]); }
        // NORMALIZE total ENERGY (not RMS) → the convolution output loudness stays consistent regardless of IR
        // LENGTH (longer IR = more taps summed, so energy-normalize keeps the wet even + the hot-burst peak bounded).
        { double e = 0; for (int i = 0; i < m; ++i) e += (double) bakeL[(size_t) i] * bakeL[(size_t) i] + (double) bakeR[(size_t) i] * bakeR[(size_t) i];
          float g = (e > 1e-12) ? (float) std::sqrt (2.2 / e) : 1.0f; if (g > 16.0f) g = 16.0f;
          for (int i = 0; i < m; ++i) { bakeL[(size_t) i] *= g; bakeR[(size_t) i] *= g; } }
        curBakeLen = m;
        // partition + FFT into the INACTIVE slot, then start the crossfade
        int slot = 1 - aSlot;
        int P = (m + B - 1) / B; if (P > MAXP) P = MAXP;
        for (int p = 0; p < P; ++p)
        { int off = p * B, plen = (m - off < B) ? (m - off) : B;
          fftPartition (&bakeL[(size_t) off], plen, &HreBuf[slot][(size_t) p * HB], &HimBuf[slot][(size_t) p * HB]);
          fftPartition (&bakeR[(size_t) off], plen, &HreBufR[slot][(size_t) p * HB], &HimBufR[slot][(size_t) p * HB]); }
        Pcnt[slot] = P;
        oSlot = aSlot; aSlot = slot;
        // DEFER the crossfade to the next runBlock (so BOTH slots have a valid output block first → no click on
        // the first block after a rebake). ~40 ms equal-ramp blend on the SHARED input FDL.
        if (Pcnt[oSlot] > 0) { xfPending = true; xfStep = 1.0f / (0.040f * fs); }
        else { xfActive = false; xfPending = false; xfGain = 1.0f; }
    }

    static constexpr float PI = 3.14159265358979f;
    // 8 CHARACTER spaces: lenSec, tailDecay, tailGain, bright(kHz), erCount, erSpread(s), erGain, preMs.
    struct CharBias { float lenSec, tailDecay, tailGain, bright, erCount, erSpread, erGain, preMs; };
    static constexpr CharBias CHAR[8] = {
        /* Hall     */ { 2.20f, 0.34f, 0.80f, 5.0f, 14.f, 0.070f, 0.55f, 12.f },   // big smooth hall
        /* Room     */ { 0.85f, 0.28f, 0.70f, 6.0f, 20.f, 0.028f, 0.70f,  5.f },   // small, dense ER
        /* Chamber  */ { 1.40f, 0.32f, 0.78f, 4.5f, 16.f, 0.045f, 0.60f,  8.f },   // medium warm
        /* Cathedral*/ { 3.00f, 0.42f, 0.85f, 3.2f, 10.f, 0.110f, 0.45f, 22.f },   // huge, long, dark
        /* Plate    */ { 1.60f, 0.24f, 0.90f, 8.0f,  4.f, 0.010f, 0.30f,  2.f },   // dense metallic, few ER
        /* Ambience */ { 0.55f, 0.22f, 0.60f, 7.0f, 24.f, 0.020f, 0.85f,  3.f },   // short ER-forward
        /* Cave     */ { 2.60f, 0.40f, 0.75f, 2.6f,  9.f, 0.090f, 0.60f, 18.f },   // dark, boomy, sparse
        /* Tunnel   */ { 1.90f, 0.36f, 0.72f, 3.8f,  8.f, 0.055f, 0.75f, 10.f },   // resonant, slap-y
    };
    // 6 SHAPE ER patterns: erMul (count), erCurve (spacing), erPan (stereo spread). Slap = sparse/big spikes
    // (high crest); Diffuse = dense/small (low crest) — a wide spread so the pattern reads night-and-day.
    struct ShapeBias { float erMul, erCurve, erPan; };
    static constexpr ShapeBias SHAPE[6] = {
        /* Natural */ { 1.00f, 1.00f, 0.65f }, /* Tight   */ { 1.60f, 0.70f, 0.40f },
        /* Wide    */ { 0.80f, 1.15f, 1.00f }, /* Diffuse */ { 2.60f, 1.35f, 0.75f },
        /* Slap    */ { 0.28f, 0.55f, 0.90f }, /* Scatter */ { 1.30f, 1.00f, 1.00f },
    };

    float fs = 48000.0f, smth = 0.001f, dcR = 0.999f, tiltCoef = 0.3f;
    bool  primed = false;
    // FFT tables + scratch
    int   bitrev[FN] = {0}; float twRe[FN/2] = {0}, twIm[FN/2] = {0};
    std::vector<float> fr = std::vector<float> (FN, 0.0f), fi = std::vector<float> (FN, 0.0f);
    std::vector<float> ar = std::vector<float> (HB, 0.0f), ai = std::vector<float> (HB, 0.0f), arR = std::vector<float> (HB, 0.0f), aiR = std::vector<float> (HB, 0.0f);
    std::vector<float> frame = std::vector<float> (FN, 0.0f);
    // IR spectra (double-buffered) + FDL
    std::vector<float> HreBuf[2], HimBuf[2], HreBufR[2], HimBufR[2]; int Pcnt[2] = {0,0}; int aSlot = 0, oSlot = 0;
    std::vector<float> fdlRe, fdlIm; int fdlHead = 0;
    // IR work buffers
    std::vector<float> baseL, baseR, bakeL, bakeR, tmpL, tmpR, userL, userR;
    int baseLen = 0, curBakeLen = 0, userLen = 0, baseChar = -1, baseShape = -1;
    // per-sample I/O buffering
    std::vector<float> inA = std::vector<float> (B, 0.0f), inB = std::vector<float> (B, 0.0f);
    std::vector<float>* inPrev = &inA; std::vector<float>* inCur = &inB;
    std::vector<float> outBlk = std::vector<float> (B, 0.0f), outBlkR = std::vector<float> (B, 0.0f), outOld = std::vector<float> (B, 0.0f), outOldR = std::vector<float> (B, 0.0f);
    int phase = 0;
    // crossfade
    bool xfActive = false, xfPending = false; float xfGain = 1.0f, xfStep = 0.001f;
    // real-time wet chain
    std::vector<float> preBuf, modBufL, modBufR; int preMask = 0, modMask = 0, preWr = 0, modWr = 0, preMax = 0, modMax = 0;
    float modPh = 0.0f, lcZ = 0.0f, tiltLpL = 0.0f, tiltLpR = 0.0f;
    float hiGainT = 1, hiGainC = 1, loGainT = 1, loGainC = 1, lcT = 0, lcC = 0, preSampT = 0, preSampC = 0, modDepthT = 0, modDepthC = 0, modIncT = 0, modIncC = 0, widthT = 0.85f, widthC = 0.85f;
    // param state
    int   character = 0, shape = 0;
    bool  reverse = false, modOn = true, lRev = false;
    bool  irDirty = true; int sinceBake = 1 << 30;
    float size = 0.5f, decayP = 0.8f, tone = 0.55f, preMs = 10.f, density = 0.3f, motDepth = 0.25f, motRate = 0.4f,
          attack = 0.0f, distance = 0.5f, lowCut = 20.f, width = 0.9f, mixExt = 0.3f;
    float lSize = -1, lDecay = -1, lAttack = -1, lDist = -1, lDens = -1;
    float apScratch[1024] = {0};   // Density box-smear scratch (window ≤ 900)
    std::uint32_t rng = 0x2545F491u;
};
