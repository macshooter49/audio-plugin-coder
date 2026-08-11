#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DelayEngine — ONE shared fractional stereo delay line that voices FOUR delay
// CHARACTERS (Digital / Tape / BBD / Diffuse), with Ping-Pong routing, tempo
// sync (resolved to ms by the host processor), an HQ interpolation toggle, an
// in-loop tone/low-cut/hi-cut filter, wow+flutter+chorus modulation, BBD
// companding, allpass diffusion, input-ducking and mid/side width.
//
// Clean-room from Design/DELAY-RESEARCH.md (the multi-agent delay research). Same
// contract as the reverb engines: PURE C++ (no JUCE) so it offline-validates
// standalone AND drops straight into the voice/FX path; processSample() returns
// the WET signal only — the processor owns the dry/wet Mix (Mix 100% = fully wet)
// exactly like the reverb send.
//
// THE HARD RULES, honored here:
//  • NO clicks/crackle — delay length GLIDES (comb-click law), every gain/coeff
//    ramps per-sample, denormals flushed on the recirculating state.
//  • DRAMATICISM — each of the 4 types owns a DIFFERENT degradation mechanism
//    (Digital=clean · Tape=pitch wobble+saturation · BBD=dark band-limit+companding
//    pump · Diffuse=allpass smear), not just EQ. Feedback → near-runaway wash.
//  • CPU-friendly — one engine active at a time, one shared delay line, the heavy
//    optional blocks (diffusion) only run for the Diffuse type.
//  • THE SERUM "HQ" ANSWER — HQ = a brighter fractional read (cubic Hermite) vs a
//    duller linear read, but BOTH are (near) linear-phase, so — unlike Serum's
//    allpass HQ — ours is bright WITHOUT the phasey top-end dispersion. The phasey
//    vintage smear lives deliberately in the Diffuse type, not in the HQ toggle.
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

class DelayEngine
{
public:
    // ── lifecycle ────────────────────────────────────────────────────────────
    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        int need = (int) std::ceil (16.5f * fs) + 8;         // fb304 — up to ~16 s (4 bars @ 60 BPM) for the new 4-bar sync ceiling + guard
        int sz = 1; while (sz < need) sz <<= 1;
        bufL.assign ((size_t) sz, 0.0f); bufR.assign ((size_t) sz, 0.0f);
        mask = sz - 1; wr = 0;

        // Diffuse allpass diffusers — 4 per channel, mutually non-multiple lengths
        // (ms → samples), so the smear has no metallic ring.
        const float apMs[NAP] = { 4.77f, 3.59f, 12.7f, 9.31f };
        for (int i = 0; i < NAP; ++i)
        {
            int alen = std::max (4, (int) std::lround (apMs[i] * 0.001f * fs));
            int asz = 1; while (asz < alen + 4) asz <<= 1;
            apL[i].assign ((size_t) asz, 0.0f); apR[i].assign ((size_t) asz, 0.0f);
            apMask[i] = asz - 1; apWr[i] = 0; apDelay[i] = alen;
        }
        smCoef  = 1.0f - std::exp (-1.0f / (0.015f * fs));   // ~15 ms generic ramp
        rng = 0x2545F491u;
        reset();
        updateCoefficients();
        // adopt targets immediately on the first prepare (no start-up glide sweep)
        delCurL = delTgtL; delCurR = delTgtR;
        fbCur = fbTgt; toneCur = toneTgt; widthCur = widthTgt; pingCur = pingTgt;
        modDepthCur = modDepthTgt; wowCur = wowTgt; spreadCur = spreadTgt;
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        for (int i = 0; i < NAP; ++i)
        {
            std::fill (apL[i].begin(), apL[i].end(), 0.0f);
            std::fill (apR[i].begin(), apR[i].end(), 0.0f);
        }
        hpZL = hpZR = lpZL = lpZR = toneZL = toneZR = 0.0f;
        tapeZL = tapeZR = bbdZL = bbdZR = 0.0f;
        compEnvL = compEnvR = expEnvL = expEnvR = 0.0f;
        duckEnv = 0.0f;
        modPh = wowPh = flutPh = 0.0f;
    }

    // ── setters (called once per block from the processor) ────────────────────
    void setType      (int t)     { type_ = t < 0 ? 0 : (t > 3 ? 3 : t); }
    void setCharacter (int c)     { character_ = c < 0 ? 0 : c; }
    void setTimeMs    (float ms)  { timeMs_ = ms < 1.0f ? 1.0f : (ms > 16000.0f ? 16000.0f : ms); }   // fb304 — 4-bar ceiling (was 2.5 s); this is the LEFT time
    void setTimeMsR   (float ms)  { timeMsR_ = ms < 1.0f ? 1.0f : (ms > 16000.0f ? 16000.0f : ms); }  // fb305 — independent RIGHT time (used when unlinked)
    void setLink      (bool  l)   { linked_ = l; }                                                     // fb305 — Link on ⇒ R follows L (+Spread); off ⇒ independent
    void setFeedback  (float f)   { fbTgt = f < 0.0f ? 0.0f : (f > 1.15f ? 1.15f : f); }
    void setTone      (float t)   { toneTgt = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }   // 0.5 neutral
    void setLowCutHz  (float hz)  { lowCutHz_ = hz; }
    void setHiCutHz   (float hz)  { hiCutHz_  = hz; }
    void setSpread    (float s)   { spreadTgt = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s); }
    void setWidth     (float w)   { widthTgt = w < 0.0f ? 0.0f : (w > 1.6f ? 1.6f : w); }   // 1 = normal
    void setModRate   (float hz)  { modRateHz_ = hz < 0.02f ? 0.02f : hz; }
    void setModDepth  (float d)   { modDepthTgt = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }
    void setWow       (float w)   { wowTgt = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w); }
    void setDucking   (float d)   { duckAmt_ = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }
    void setPing      (bool p)    { pingTgt = p ? 1.0f : 0.0f; }
    void setHQ        (bool h)    { hq_ = h; }

    // Per-block: resolve filter coefficients + the delay-length target. Everything
    // that could zipper is ramped per-sample in processSample().
    void updateCoefficients()
    {
        const float baseSamp = timeMs_ * 0.001f * fs;
        // Spread pushes the RIGHT tap later (0..+35% of the time, capped) → stereo
        // bounce + ping-pong swing; keeps L as the anchor.
        const float rMul = 1.0f + spreadCur * 0.35f;
        delTgtL = baseSamp;
        // fb305 — LINK: R follows L (+ Spread offset, the classic stereo bounce). UNLINKED: R is a fully
        // independent time (its own knob / sync division), like Serum's L·R.
        delTgtR = linked_ ? (baseSamp * rMul) : (timeMsR_ * 0.001f * fs);
        const float lim = (float) mask - 4.0f;
        if (delTgtL > lim) delTgtL = lim;
        if (delTgtR > lim) delTgtR = lim;

        // In-loop band edges (one-pole). LowCut = HP, HiCut = LP.
        hpCoef = onePole (lowCutHz_);
        lpCoef = onePole (hiCutHz_);
        // Type-specific fixed rolloff: Tape softens ~7 kHz; BBD band-limits hard and
        // DARKER the longer the time (authentic bucket-brigade clock droop).
        tapeLpCoef = onePole (7200.0f);
        {
            const float tnorm = std::min (1.0f, timeMs_ / 600.0f);       // 0..1 over 0..600 ms
            const float bbdHz = 5200.0f - 2900.0f * tnorm;               // 5.2 kHz → 2.3 kHz
            bbdLpCoef = onePole (bbdHz);
        }
        toneCoef = onePole (760.0f);                                     // tilt split point

        // Mod / duck coefficients
        modInc  = modRateHz_ / fs;
        wowInc  = 0.55f / fs;                                            // slow wow ~0.55 Hz
        flutInc = 8.3f  / fs;                                            // flutter ~8.3 Hz
        duckAtk = 1.0f - std::exp (-1.0f / (0.004f * fs));               // 4 ms
        duckRel = 1.0f - std::exp (-1.0f / (0.180f * fs));               // 180 ms
        compCoef = 1.0f - std::exp (-1.0f / (0.020f * fs));             // 20 ms companding follower
    }

    // ── per-sample render. Returns WET only (dry/Mix owned by the processor). ──
    inline void processSample (float inL, float inR, float& outL, float& outR)
    {
        // ── per-sample smoothing (click-free) ────────────────────────────────
        delCurL += smCoef * (delTgtL - delCurL);
        delCurR += smCoef * (delTgtR - delCurR);
        fbCur   += smCoef * (fbTgt   - fbCur);
        toneCur += smCoef * (toneTgt - toneCur);
        widthCur+= smCoef * (widthTgt- widthCur);
        pingCur += smCoef * (pingTgt - pingCur);
        modDepthCur += smCoef * (modDepthTgt - modDepthCur);
        wowCur  += smCoef * (wowTgt  - wowCur);
        spreadCur += smCoef * (spreadTgt - spreadCur);

        // ── modulation offset (samples) ──────────────────────────────────────
        modPh  += modInc;  if (modPh  >= 1.0f) modPh  -= 1.0f;
        wowPh  += wowInc;  if (wowPh  >= 1.0f) wowPh  -= 1.0f;
        flutPh += flutInc; if (flutPh >= 1.0f) flutPh -= 1.0f;
        const float twoPi = 6.2831853071795864f;
        const float chorus = std::sin (twoPi * modPh);
        const float chorusR= std::sin (twoPi * modPh + 1.5707963f);      // 90° for stereo motion
        const float depthSamp = modDepthCur * 0.006f * fs;               // up to ~6 ms chorus
        // Tape owns the deepest wow+flutter; BBD a touch less; Digital/Diffuse a subtle wobble so the
        // Wow knob is never dead on any type (every control does something for every character).
        const float wowSamp = (type_ == 1 ? wowCur : (type_ == 2 ? wowCur * 0.5f : wowCur * 0.25f)) * 0.004f * fs;
        const float wowFlut = wowSamp * (0.75f * std::sin (twoPi * wowPh) + 0.25f * std::sin (twoPi * flutPh));
        const float modL = depthSamp * chorus  + wowFlut;
        const float modR = depthSamp * chorusR + wowFlut;

        // ── read the delayed taps (fractional, HQ = cubic Hermite / else linear)─
        float rL = readAt (bufL.data(), delCurL + modL);
        float rR = readAt (bufR.data(), delCurR + modR);

        // ── in-loop band edges (low cut / hi cut) — tone tilt is now output-only (fb310) ──
        rL = loopFilter (rL, hpZL, lpZL);
        rR = loopFilter (rR, hpZR, lpZR);

        // ── per-type CHARACTER block ─────────────────────────────────────────
        float fL = rL, fR = rR;
        switch (type_)
        {
            case 1:  fL = tape (fL, tapeZL); fR = tape (fR, tapeZR); break;      // Tape
            case 2:  fL = bbd  (fL, bbdZL);                                      // BBD (Analog) — fb308: dark grit, no noise
                     fR = bbd  (fR, bbdZR); break;
            case 3:  fL = diffuse (fL, apL);  fR = diffuse (fR, apR); break;     // Diffuse / Ghost
            default: break;                                                     // Digital = clean
        }

        // WET output = the character tap, THEN the Tone tilt (fb310 — output-only, so Tone colours what you HEAR
        // without changing the feedback loop gain). toneZL/R (freed from the loop) now hold the output-tilt state.
        float wetL = toneTilt (fL, toneZL), wetR = toneTilt (fR, toneZR);

        // ── feedback write (Normal ↔ Ping-Pong crossfade via pingCur) ────────
        const float p   = pingCur;
        const float inM = 0.5f * (inL + inR);
        const float fb  = fbCur > 0.98f ? 0.98f : fbCur;                 // hard cap; loop soft-clips
        // fb308 — per-type FEEDBACK MAKEUP (Max: "feedback only works on Tape; amplify it for everything"). Only
        // Tape's in-loop tanh gave loop-gain > 1 (so only Tape built up); Digital/Diffuse are ~unity and BBD's dark
        // LP loses energy, so their echoes died. Lift each type's FEEDBACK (not the wet) to Tape-like loop gain so
        // all build / near-self-oscillate; softClip still bounds the runaway → BIBO stable. Tape unchanged (1.0).
        const float fbMk = (type_ == 1) ? 1.0f : (type_ == 2 ? 1.00f : (type_ == 3 ? 2.15f : 1.28f));   // fb310 — trimmed ~12% (the loop no longer sees the tone-tilt attenuation) so the consistent level ≈ Max's "perfect 40%"; Diffuse sits near threshold so it keeps its value. Tape · BBD · Diffuse · Digital
        const float fbE  = fb * fbMk;
        float newL = (inL * (1.0f - p) + inM * p) + fbE * (fL * (1.0f - p) + fR * p);
        float newR = (inR * (1.0f - p))           + fbE * (fR * (1.0f - p) + fL * p);
        newL = softClip (newL);
        newR = softClip (newR);
        bufL[(size_t) wr] = flush (newL);
        bufR[(size_t) wr] = flush (newR);
        wr = (wr + 1) & mask;

        // ── mid/side width on the WET only (never in the loop) ───────────────
        const float M = 0.5f * (wetL + wetR);
        const float S = 0.5f * (wetL - wetR) * widthCur;
        wetL = M + S; wetR = M - S;

        // ── input-ducking: env-follow the dry input, pull the wet down under it ─
        if (duckAmt_ > 1.0e-4f)
        {
            const float lvl = 0.5f * (std::abs (inL) + std::abs (inR));
            duckEnv += (lvl > duckEnv ? duckAtk : duckRel) * (lvl - duckEnv);
            const float g = 1.0f / (1.0f + (14.0f * duckAmt_) * duckEnv);
            wetL *= g; wetR *= g;
        }

        outL = wetL; outR = wetR;
    }

    // Viz helper — 0..1 feedback "fullness" the UI can poll for the tap envelope.
    float getFeedbackViz() const { return fbCur > 1.0f ? 1.0f : fbCur; }

private:
    // ── fractional read ──────────────────────────────────────────────────────
    inline float readAt (const float* b, float d) const
    {
        if (d < 1.0f) d = 1.0f;
        const int di = (int) d; const float fr = d - (float) di;
        const int i0 = (wr - di) & mask;
        if (! hq_)                                              // linear — duller top, phase-benign
        {
            const int i1 = (wr - di - 1) & mask;
            return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
        }
        // cubic Hermite (4-pt) — bright AND (near) linear-phase → HQ without Serum's phasey dispersion
        const int im1 = (wr - di + 1) & mask;
        const int i1  = (wr - di - 1) & mask;
        const int i2  = (wr - di - 2) & mask;
        const float ym1 = b[(size_t) im1], y0 = b[(size_t) i0], y1 = b[(size_t) i1], y2 = b[(size_t) i2];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * fr + c2) * fr + c1) * fr + y0;
    }

    // ── in-loop band edges ONLY: HP (low cut) → LP (hi cut). ──────────────────
    //    fb310 — the bipolar Tone TILT is NO LONGER here; it moved OUT of the feedback loop (toneTilt below) so
    //    the feedback is INDEPENDENT of the Tone knob (Max: high Tone was cutting the loop's lows → starving the
    //    buildup; "the feedback should keep playing no matter where Tone is"). Dub-darkening still comes from the
    //    per-type character LPs (Tape/BBD) + the Hi-Cut, which stay in the loop.
    inline float loopFilter (float x, float& hpZ, float& lpZ) const
    {
        hpZ += hpCoef * (x - hpZ);        x -= hpZ;             // one-pole HP (low cut)
        lpZ += lpCoef * (x - lpZ);        x  = lpZ;             // one-pole LP (hi cut)
        return x;
    }
    // ── OUTPUT-only bipolar Tone tilt (split @ ~760 Hz). NOT in the loop ⇒ colours the wet WITHOUT changing loop gain.
    inline float toneTilt (float x, float& toneZ) const
    {
        toneZ += toneCoef * (x - toneZ);
        const float low = toneZ, high = x - toneZ;
        const float t = (toneCur - 0.5f) * 2.0f;                // -1..+1
        const float gLow  = t < 0.0f ? 1.0f : 1.0f - 0.85f * t; // right → thin (cut lows)
        const float gHigh = t > 0.0f ? 1.0f : 1.0f + 0.85f * t; // left → dark (cut highs)
        return low * gLow + high * gHigh;
    }

    // ── Tape: soft asymmetric saturation + fixed HF roll-off (head gap) ──────
    inline float tape (float x, float& z) const
    {
        const float drive = 1.4f + 1.2f * (float) (character_ & 1);     // Vintage variant hotter
        float y = std::tanh (drive * (x + 0.06f * x * x));              // asymmetric → even harmonics
        z += tapeLpCoef * (y - z);                                      // ~7 kHz head-gap loss per repeat
        return z * 0.86f;                                              // gentle level trim (tanh makeup)
    }

    // ── BBD (Analog): dark, gritty bucket-brigade — a hard, time-tracking band-limit + soft grit.
    //    fb308 (Max): NO noise, and the feedback must BUILD like Tape. The old compander both SQUASHED the
    //    feedback (so echoes died) and pumped an audible hiss (character-tied). Both are gone; what remains is
    //    the dark time-tracking LP (the BBD signature) + a touch of Character-varied grit — and the shared
    //    per-type feedback makeup below lets it build/self-oscillate cleanly.
    inline float bbd (float x, float& z) const
    {
        const float drive = 1.15f + 0.45f * (float) (character_ % 3);   // grit varies with Character (NOT noise)
        float y = std::tanh (drive * x);                               // clock/quantise grit
        z += bbdLpCoef * (y - z);                                      // steep, time-tracking LP (dark) = the BBD signature
        return z;
    }

    // ── Diffuse: 4-stage allpass smear (delay → reverb wash) ─────────────────
    inline float diffuse (float x, std::vector<float>* ap)
    {
        const float g = 0.62f;
        float v = x;
        for (int i = 0; i < NAP; ++i)
        {
            const int rd = (apWr[i] - apDelay[i]) & apMask[i];
            const float d = ap[i][(size_t) rd];
            const float w = v + g * d;
            const float y = d - g * w;
            ap[i][(size_t) apWr[i]] = flush (w);
            apWr[i] = (apWr[i] + 1) & apMask[i];
            v = y;
        }
        // Diffuse amount rides Mod Depth (relabeled in the UI) — crossfade dry↔smeared.
        const float amt = 0.35f + 0.65f * modDepthTgt;
        return x * (1.0f - amt) + v * amt;
    }

    inline float softClip (float x) const
    {
        // ~linear in the normal region (clean digital), tanh-saturates past ±1.4 so
        // runaway feedback / self-oscillation stays bounded and musical, not exploding.
        if (x >  1.4f) return std::tanh (x);
        if (x < -1.4f) return std::tanh (x);
        return x;
    }

    inline float onePole (float hz) const
    {
        if (hz <= 0.0f)      return 0.0f;
        if (hz >= fs * 0.49f) return 1.0f;
        return 1.0f - std::exp (-6.2831853071795864f * hz / fs);
    }
    static inline float flush (float x) { return (std::abs (x) < 1.0e-20f) ? 0.0f : x; }
    inline float rand11 () const { rng = rng * 1664525u + 1013904223u; return (float) ((int32_t) rng) * (1.0f / 2147483648.0f); }

    // ── state ────────────────────────────────────────────────────────────────
    static constexpr int NAP = 4;
    float fs = 48000.0f;
    std::vector<float> bufL, bufR;
    int mask = 0, wr = 0;

    std::vector<float> apL[NAP], apR[NAP];
    int apMask[NAP] = {0,0,0,0}, apWr[NAP] = {0,0,0,0}, apDelay[NAP] = {1,1,1,1};

    // params (targets set by setters; *Cur are the per-sample smoothed values)
    int   type_ = 0, character_ = 0;
    float timeMs_ = 350.0f, lowCutHz_ = 20.0f, hiCutHz_ = 16000.0f, modRateHz_ = 0.5f, duckAmt_ = 0.0f;
    float timeMsR_ = 350.0f;   // fb305 — independent RIGHT delay time (used when unlinked)
    bool  linked_  = true;     // fb305 — Link: R = L + Spread (default, = classic behavior); false ⇒ independent L/R
    bool  hq_ = true;
    float fbTgt = 0.45f, toneTgt = 0.5f, spreadTgt = 0.0f, widthTgt = 1.0f, pingTgt = 0.0f, modDepthTgt = 0.0f, wowTgt = 0.0f;
    float fbCur = 0.45f, toneCur = 0.5f, spreadCur = 0.0f, widthCur = 1.0f, pingCur = 0.0f, modDepthCur = 0.0f, wowCur = 0.0f;
    float delTgtL = 0.0f, delTgtR = 0.0f, delCurL = 0.0f, delCurR = 0.0f;

    // coefficients
    float smCoef = 0.01f, hpCoef = 0.0f, lpCoef = 1.0f, tapeLpCoef = 0.5f, bbdLpCoef = 0.5f, toneCoef = 0.1f;
    float modInc = 0.0f, wowInc = 0.0f, flutInc = 0.0f, duckAtk = 0.1f, duckRel = 0.001f, compCoef = 0.01f;

    // filter / follower state
    float hpZL=0, hpZR=0, lpZL=0, lpZR=0, toneZL=0, toneZR=0, tapeZL=0, tapeZR=0, bbdZL=0, bbdZR=0;
    float compEnvL=0, compEnvR=0, expEnvL=0, expEnvR=0, duckEnv=0;
    float modPh=0, wowPh=0, flutPh=0;
    mutable uint32_t rng = 0x2545F491u;
};
