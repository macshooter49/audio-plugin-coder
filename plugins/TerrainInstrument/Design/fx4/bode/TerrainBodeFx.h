#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  TerrainBodeFx — THE BODE FREQUENCY SHIFTER (rack device kind 13, fb444)
//
//  Harald Bode's 1964 single-sideband shifter, wrapped in the Echobode loop.
//  A frequency shifter adds a FIXED number of Hz to every partial. It is not a
//  pitch shifter: harmonic ratios are destroyed, which is the whole point — a
//  shifted 100/200/300 Hz becomes 105/205/305, which the ear hears as metallic,
//  bell-like, inharmonic. Put it inside a feedback delay and every repeat shifts
//  again: the barberpole that walks up (or down) forever.
//
//  WHAT IS LIFTED VERBATIM from TerrainFilters.h's BodeShifter (filter engine 22):
//    · the Niemitalo 90-degree quadrature allpass pair (8 mults, +-0.7 deg)
//    · the 1-sample I-branch alignment delay  (mandatory — without it the pair
//      is not in quadrature and the image sideband comes back)
//    · the recursive quadrature oscillator + its every-512-samples renormalise
//
//  WHAT IS DELIBERATELY NOT LIFTED — and why (fb444 recon):
//    · THE SHIFT MAPPING. The filter-engine version derives the shift from a LOG
//      CUTOFF with zero at 632.5 Hz and caps at FMAX = 1000 Hz. Its next line,
//      `jlimit (-2000, +2000)`, reads like a +-2 kHz range and is DEAD CODE — the
//      formula can never reach it. Lifting that verbatim would have shipped a
//      fifth of the asked-for range with every knob position still "doing
//      something", which is the failure mode that hides. Here the control is a
//      true bipolar +-5000 Hz with an exponential taper and a centre detent.
//    · ONE SIDEBAND ONLY. `y = i*cos + q*sin` is one sideband; `i*cos - q*sin` is
//      the other; and `i*cos` alone is BOTH, which is ring modulation. So the
//      whole down <-> ring <-> up axis is one continuous coefficient:
//
//           y = iOut * cos  +  m * qOut * sin        m in [-1, +1]
//
//      m = +1 is UP and m = -1 is DOWN -- established by probe, not by argument
//      (see bod_cert gate 0, and the sign comment at setParams). m = +-1 costs
//      nothing over the old fixed form, and m = 0 gives ring mod for
//      free. The old engine spent a whole second filter slot (BODE_DOWN, engine
//      76) on what is a sign here.
//    · MONO. The old one is a mono effect run twice with identical parameters —
//      the filter path deliberately bypasses its own L/R cutoff spread for Bode.
//      Stereo here is a SHIFT MIRROR: R shifts by L * (1 - 2*spread), so the two
//      channels walk apart in opposite directions. That is the only thing that
//      makes this device stereo, so it defaults to full mirror.
//    · NO LOOP AT ALL. There is no delay line anywhere in the old engine, so no
//      Echobode, no barberpole, no repeats. That is most of this file.
//
//  🔥 THE LOOP-GAIN LAW (fb306-310, and the Flanger is the proven sibling):
//      loop gain = fb x (everything else inside the recirculating path).
//    A shifter in a feedback loop is the WORST case for this, because the loop's
//    content walks out of band on every pass and can pile up on a resonance the
//    shifter happens to walk into. Four placements, all copied from the shipped
//    flanger because it already survived this:
//      1. The soft clip is INSIDE the loop and is NOT optional — it is what makes
//         the thing BIBO-bounded. It is C1-continuous; the DelayEngine softClip
//         is NOT (it jumps 1.4 -> 0.885 at the knee) and importing that into a
//         loop designed to reach the knee imports a click generator.
//      2. The makeup divides INSIDE the loop, so sat'(0) == 1 exactly. Without
//         that, Drive is a second secret feedback control and the stability gate
//         is measuring a lie.
//      3. The input-presence gate multiplies the COEFFICIENT, never the output
//         (gating the output clicks), and is applied SQUARED — a linear release
//         latches audibly, the square dies.
//      4. In-loop filters are CUT-ONLY. Damping is a low-pass. Any Character that
//         wants presence adds it OUTSIDE the loop, or loop gain exceeds 1 at
//         highs and the thing diverges (the reverb hit +130 dB doing exactly it).
//    Ceiling reference: a loop peaks at 1/(1-g). 0.995 = 200x = +46 dB.
//
//  CPU: ~20 mults/sample/channel for the shifter + the loop taps. NEVER
//  oversampled — the shifter is a linear time-varying operation with no
//  harmonic generation of its own, so there is nothing to alias. (The filter
//  path runs it at 2x only because it shares needsOversampling() with the
//  ladder family; that is an artefact, not a requirement.)
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace tw {

// ═════════════════════════════════════════════════════════════ local helpers
// Self-contained by law: this header pulls in no TerrainFilters.h, so the cert
// harness compiles it against the shim with nothing else in the include path.
namespace bode_detail {

inline float fastTanh (float x) noexcept
{
    if (x >  5.0f) return  1.0f;
    if (x < -5.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct DCBlock
{
    float xPrev = 0.0f, yPrev = 0.0f;
    void  reset() noexcept { xPrev = yPrev = 0.0f; }
    inline float process (float x) noexcept
    {
        const float y = x - xPrev + 0.995f * yPrev;
        xPrev = x; yPrev = y;
        return y;
    }
};

// 2nd-order allpass H(z) = (A - z^-2)/(1 - A z^-2), A = a^2  (Niemitalo).
struct AP2
{
    float A = 0.0f, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void  reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
    inline float process (float x) noexcept
    {
        const float y = A * (x + y2) - x2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// One-pole low-pass. CUT-ONLY by construction: g in [0,1), y converges to x.
struct LP1
{
    float g = 0.0f, z = 0.0f;
    void  reset() noexcept { z = 0.0f; }
    void  setHz (float hz, float fs) noexcept
    {
        const float f = std::max (10.0f, std::min (hz, 0.45f * fs));
        g = 1.0f - std::exp (-6.2831853f * f / fs);
        g = std::max (0.0f, std::min (1.0f, g));
    }
    inline float process (float x) noexcept { z += g * (x - z); return z; }
};

// One-pole high-pass built from the same state (for the Low Keep complement).
struct HP1
{
    LP1 lp;
    void  reset() noexcept { lp.reset(); }
    void  setHz (float hz, float fs) noexcept { lp.setHz (hz, fs); }
    inline float process (float x) noexcept { return x - lp.process (x); }
};

// A Schroeder allpass, for in-loop diffusion (Blur).
struct APDelay
{
    std::vector<float> buf;
    int   n = 0, w = 0;
    float g = 0.5f;
    void  prepare (int len) { n = std::max (1, len); buf.assign ((size_t) n, 0.0f); w = 0; }
    void  reset()  noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline float process (float x) noexcept
    {
        if (buf.empty()) return x;
        const float d = buf[(size_t) w];
        const float v = x + g * d;
        buf[(size_t) w] = v;
        if (++w >= n) w = 0;
        return d - g * v;
    }
};

// C1-continuous soft clip. sat'(0) == 1, so the in-loop makeup below cannot
// raise the loop gain past the calibrated feedback coefficient.
inline float softClipC1 (float x, float k) noexcept
{
    return fastTanh (x * k) / std::max (1e-6f, k);
}

// A cheap, smooth random walk (Drift). Deterministic per instance: seeded from
// the instance index, never from a global, so two cards never move in lockstep.
struct SmoothRandom
{
    uint32_t s = 22222u;
    float    cur = 0.0f, tgt = 0.0f, inc = 0.0f;
    int      countdown = 1;
    void  seed (uint32_t v) noexcept { s = v | 1u; cur = tgt = inc = 0.0f; countdown = 1; }
    inline float next (int stepSamples) noexcept
    {
        if (--countdown <= 0)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            tgt = ((float) (s & 0xFFFFu) / 32768.0f) - 1.0f;      // -1..+1
            countdown = std::max (1, stepSamples);
            inc = (tgt - cur) / (float) countdown;
        }
        cur += inc;
        return cur;
    }
};

} // namespace bode_detail

// ═════════════════════════════════════════════════════ the quadrature shifter
// The Hilbert pair + oscillator, lifted, plus the sideband-blend coefficient.
struct BodeQuad
{
    bode_detail::AP2 iA[4], qA[4];
    float  iDelay = 0.0f;
    double osC = 1.0, osS = 0.0, oscCos = 1.0, oscSin = 0.0;
    int    renorm = 0;

    void reset() noexcept
    {
        // Niemitalo coefficients — a 90-degree pair to +-0.7 deg over the band.
        static const float kI[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static const float kQ[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        for (int i = 0; i < 4; ++i)
        {
            iA[i].reset(); iA[i].A = kI[i] * kI[i];
            qA[i].reset(); qA[i].A = kQ[i] * kQ[i];
        }
        iDelay = 0.0f; osC = 1.0; osS = 0.0; renorm = 0;
    }

    // Set the shift in Hz. The oscillator PHASE is retained across calls, so a
    // shift change is phase-continuous (no click) — but note it does not glide
    // by itself, which is why the caller smooths the Hz value, not this.
    void setShiftHz (float hz, double fs) noexcept
    {
        const double w = 6.283185307179586 * (double) hz / std::max (1.0, fs);
        oscCos = std::cos (w);
        oscSin = std::sin (w);
    }

    // m = +1 -> one sideband · m = -1 -> the other · m = 0 -> BOTH = ring mod.
    inline float process (float x, float m) noexcept
    {
        float iSig = x;  for (int k = 0; k < 4; ++k) iSig = iA[k].process (iSig);
        const float iOut = iDelay; iDelay = iSig;     // the mandatory 1-sample align
        float qOut = x;  for (int k = 0; k < 4; ++k) qOut = qA[k].process (qOut);

        const double nC = oscCos * osC - oscSin * osS;
        osS = oscSin * osC + oscCos * osS;
        osC = nC;
        if (++renorm >= 512)                          // Gram-Schmidt-ish renormalise
        {
            renorm = 0;
            const double mm = 1.5 - 0.5 * (osC * osC + osS * osS);
            osC *= mm; osS *= mm;
        }
        return iOut * (float) osC + m * (qOut * (float) osS);
    }
};

// ═════════════════════════════════════════════════════════════════ the device
struct TerrainBodeFx
{
    // Rack Law C — cardinality is FROZEN AT BIRTH (fb373: a choice param
    // normalised on the wrong count silently hands you a different machine).
    // Declare the full width on day one and grow into it.
    static constexpr int   kNumTypes  = 8;
    static constexpr int   kNumChars  = 8;
    static constexpr int   kNumRoutes = 8;
    static constexpr float kShiftMax  = 5000.0f;    // Hz, each way. The ASK.
    static constexpr float kFineMax   = 2.0f;       // Hz vernier, each way
    static constexpr float kTouchMax  = 1200.0f;    // Hz of envelope throw
    // fb444 — 0.95 WAS THE TIMIDITY, and mutation testing is what exposed it.
    //   Deleting the in-loop soft clip turned ZERO gates red: at 0.95, with the
    //   shift decorrelating the loop's content on every pass, this thing is
    //   bounded with no guard at all. A guard that cannot be missed is a guard
    //   that is not doing anything, and a maximum that cannot misbehave is not
    //   a maximum -- the fx3 arc took the phaser to 0.998 and named the old
    //   ceilings exactly this. At 0.995 the loop peaks at 200x, the clip becomes
    //   the thing that holds it, and the top of the knob genuinely screams.
    static constexpr float kFbMax     = 0.995f;     // 1/(1-g) = 200x = +46 dB

    struct Params
    {
        int   type = 0, chr = 0, route = 0;
        float shift = 0.5f;      // 0..1, 0.5 = no shift (centre detent)
        float dir   = 1.0f;      // 0 = down · 0.5 = ring · 1 = up
        float fdbk  = 0.0f;      // 0..1 -> 0..kFbMax loop gain
        float mix   = 1.0f;      // 0..1 equal-power
        float fine  = 0.5f;      // 0..1 bipolar, +-kFineMax Hz
        float spread = 1.0f;     // 0..1 stereo shift mirror
        float time  = 0.45f;     // 0..1 -> 0.02 ms .. 1 s (or a sync division)
        float blur  = 0.0f;      // 0..1 continuous in-loop diffusion
        float lowKeep = 0.0f;    // 0..1 -> off, 20 .. 2000 Hz
        float damping = 1.0f;    // 0..1 -> 400 Hz .. 20 kHz in-loop LP
        float touch = 0.5f;      // 0..1 bipolar env -> shift
        float drift = 0.0f;      // 0..1 instability
        bool  sync = false, guard = true;
        double bpm = 120.0;
    };

    // ── lifecycle ───────────────────────────────────────────────────────────
    void prepare (double sampleRate, int instanceIndex = 0)
    {
        fs_ = (float) std::max (8000.0, sampleRate);

        // fb444 — SIZE FOR THE SYNC CEILING, NOT THE FREE RANGE. The free Time
        // knob stops at 1 s, but "4 bar" at 60 BPM is 16 s. Size for 16.5 s or
        // the top four sync divisions truncate silently, with no error anywhere.
        int need = (int) std::ceil (16.5f * fs_) + 8;
        int sz = 1; while (sz < need) sz <<= 1;
        dl_[0].assign ((size_t) sz, 0.0f);
        dl_[1].assign ((size_t) sz, 0.0f);
        mask_ = sz - 1;

        // Coprime-ish diffusion lengths, 5..35 ms, so the allpasses never align.
        static const float kBlurMs[4] = { 5.31f, 11.73f, 21.17f, 34.61f };
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
                blur_[c][k].prepare ((int) (kBlurMs[k] * 0.001f * fs_)
                                     + (c == 1 ? 7 : 0));       // L/R decorrelated

        drift_[0].seed (0x9E3779B9u + 2654435761u * (uint32_t) (instanceIndex + 1));
        drift_[1].seed (0x85EBCA6Bu + 2246822519u * (uint32_t) (instanceIndex + 1));
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            quad_[c].reset();
            std::fill (dl_[c].begin(), dl_[c].end(), 0.0f);
            for (int k = 0; k < 4; ++k) blur_[c][k].reset();
            damp_[c].reset(); lowLp_[c].reset(); dcOut_[c].reset();
            w_[c] = 0;
        }
        env_ = 0.0f; gate_ = 0.0f;
        shiftSm_ = 0.0f; fbSm_ = 0.0f; timeSm_ = 0.0f; mixSm_ = -1.0f;
        mImage_ = 0.0f; mLoop_ = 0.0f;
    }

    // ── per-block parameter intake (NEVER per sample) ───────────────────────
    void setParams (const Params& p) noexcept
    {
        p_ = p;

        // ── the shift, in real Hz. Bipolar with an exponential taper, so the
        //    sub-Hz beating region near zero is reachable by hand and the ends
        //    still get to +-5 kHz. sgn(v) * (5001^|v| - 1).
        const float v   = 2.0f * clamp01 (p.shift) - 1.0f;
        const float mag = std::pow (kShiftMax + 1.0f, std::fabs (v)) - 1.0f;
        shiftTarget_ = (v < 0.0f ? -mag : mag)
                     + (2.0f * clamp01 (p.fine) - 1.0f) * kFineMax;

        // ── the sideband blend. THE SIGN IS SETTLED BY THE CERT, NOT BY TASTE:
        //    gate 0 of bod_cert drives 1 kHz in with Direction = up and asserts
        //    the energy lands at 1100 Hz, not 900. A flipped sign builds clean,
        //    measures "different", and is a different instrument.
        //    y = i*cos + m*q*sin, so m = +1 shifts DOWN; up therefore needs -1.
        //    MEASURED, not reasoned: the first draft of this line wrote
        //    -(2b-1) on a pen-and-paper argument about which term carries which
        //    sideband, and gate 0 came back with 899 Hz where 1100 was wanted --
        //    a working shifter running backwards. The bible's own first draft
        //    made the identical mistake. The probe decides, every time.
        const float b = clamp01 (p.dir);
        mBlend_ = 2.0f * b - 1.0f;          // +1 = UP (proven by bod_cert gate 0)
        // Ring mod (m = 0) splits the energy into two half-amplitude sidebands:
        // half the power of SSB, so +3 dB back at the centre, tapering to unity
        // at either end. Without this the middle of the knob is a level dip.
        ringComp_ = 1.0f + 0.41421356f * (1.0f - std::fabs (mBlend_));

        // ── the loop
        fbTarget_   = kFbMax * clamp01 (p.fdbk);
        timeTarget_ = syncedTimeSamples (p);
        // fb444 — BLUR IS CONTINUOUS, NOT STEPPED. It was `floor(blur * 4)`
        //   allpasses, which made the first sixth of the knob dead travel: knob
        //   0 and knob 1/6 both resolved to ZERO sections and the cert measured
        //   them 0.000 dB apart. Switching sections in and out also jumps the
        //   loop length, because an allpass at g=0 is not transparent -- it is
        //   still a delay. So all four sections run all the time (their state
        //   stays warm) and the knob crossfades between the clean tap and the
        //   diffused one while opening g. No steps, no length jump, no plateau.
        blurMix_    = clamp01 (p.blur);
        blurG_      = 0.30f + 0.40f * clamp01 (p.blur);
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 4; ++k) blur_[c][k].g = blurG_;
            // CUT-ONLY, and the only filter inside the loop. fb444 — the top of
            // this range used to be 40 kHz, which is ABOVE the LP1 Nyquist clamp,
            // so the last third of the knob was one repeated value. It now ends
            // at 20 kHz: every position on the travel is a different filter.
            damp_[c].setHz (400.0f * std::pow (50.0f, clamp01 (p.damping)), fs_);
            lowLp_[c].setHz (lowKeepHz (p.lowKeep), fs_);
        }
        lowKeepOn_ = clamp01 (p.lowKeep) > 0.001f;

        // Drive comes from Character; it is applied INSIDE the loop and divided
        // straight back out, so sat'(0) == 1 and Character can never become a
        // second feedback control.
        clipK_ = characterDrive (p.chr);

        touchAmt_  = (2.0f * clamp01 (p.touch) - 1.0f) * kTouchMax;
        driftAmt_  = clamp01 (p.drift) * 6.0f;          // up to +-6 Hz of wander
        driftStep_ = std::max (1, (int) (fs_ / 0.3f / 64.0f));
        spread_    = 1.0f - 2.0f * clamp01 (p.spread);  // 1 = same, -1 = mirrored
        mixT_      = clamp01 (p.mix);
        if (mixSm_ < 0.0f) mixSm_ = mixT_;              // first block: no ramp
        routePre_  = (p.route == 4);
        routePost_ = (p.route == 5);
        routeCross_= (p.route == 1);
        routeMono_ = (p.route == 2);
    }

    // ── the audio ───────────────────────────────────────────────────────────
    inline void processStereo (float inL, float inR, float& outL, float& outR) noexcept
    {
        // Per-sample smoothing. The shift is the one that MUST glide: the
        // oscillator holds phase across a change, so a jump is click-free but
        // still a jump, and a jumped shift on a sustained note is audible as a
        // step. ~15 ms.
        const float aP = 1.0f - std::exp (-1.0f / (0.015f * fs_));
        const float aM = 1.0f - std::exp (-1.0f / (0.030f * fs_));

        // ── the input-presence gate. NOTHING FREE-RUNS: with feedback maxed and
        //    no input the tail must die, so the gate multiplies the COEFFICIENT
        //    (gating the output clicks) and is applied SQUARED (a linear release
        //    latches audibly; the square dies).
        const float rect = 0.5f * (std::fabs (inL) + std::fabs (inR));
        env_ += (rect > env_ ? 0.35f : 0.0006f) * (rect - env_);
        const float gTarget = env_ > 2.2e-4f ? 1.0f : 0.0f;      // ~ -73 dBFS
        gate_ += (gTarget > gate_ ? 0.02f : 0.0009f) * (gTarget - gate_);
        const float gate2 = gate_ * gate_;

        shiftSm_ += aP * (shiftTarget_ - shiftSm_);
        fbSm_    += aP * (fbTarget_    - fbSm_);
        timeSm_  += aM * (timeTarget_  - timeSm_);
        mixSm_   += aM * (mixT_        - mixSm_);

        const float touch = touchAmt_ * env_ * env_ * 24.0f;
        const float gFb   = fbSm_ * gate2;                        // ← THE COEFFICIENT
        mLoop_ = gFb;

        float dry[2] = { inL, inR };
        float x[2]   = { inL, inR };

        if (routeMono_) { const float m = 0.5f * (x[0] + x[1]); x[0] = x[1] = m; }
        if (routeCross_) std::swap (x[0], x[1]);

        // ── Low Keep: the band below the crossover skips the whole device and
        //    is re-added un-shifted, so the bass stays anchored and never enters
        //    the loop. Minimum-phase by construction (one pole), never linear.
        float low[2] = { 0.0f, 0.0f };
        if (lowKeepOn_)
            for (int c = 0; c < 2; ++c) { low[c] = lowLp_[c].process (x[c]); x[c] -= low[c]; }

        float wet[2];
        for (int c = 0; c < 2; ++c)
        {
            const float dr  = driftAmt_ > 0.0f ? driftAmt_ * drift_[c].next (driftStep_) : 0.0f;
            const float mir = (c == 0 ? 1.0f : spread_);          // the stereo shift MIRROR
            quad_[c].setShiftHz ((shiftSm_ + touch + dr) * mir, fs_);

            const int   len  = std::max (1, std::min ((int) timeSm_, mask_ - 2));
            const float frac = timeSm_ - std::floor (timeSm_);
            const int   r0   = (w_[c] - len)     & mask_;
            const int   r1   = (w_[c] - len - 1) & mask_;
            float tap = dl_[c][(size_t) r0] + frac * (dl_[c][(size_t) r1] - dl_[c][(size_t) r0]);

            tap = damp_[c].process (tap);                         // cut-only, in-loop
            {                                                     // continuous diffusion
                float dif = tap;
                for (int k = 0; k < 4; ++k) dif = blur_[c][k].process (dif);
                tap += blurMix_ * (dif - tap);
            }

            float u = x[c] + gFb * tap;
            u = bode_detail::softClipC1 (u, clipK_);              // BIBO bound, in-loop

            float y;
            if (routePost_)                                       // delay, then shift
            {
                dl_[c][(size_t) w_[c]] = u;
                y = quad_[c].process (u, mBlend_) * ringComp_;
            }
            else                                                  // shift inside the loop
            {                                                     // (the barberpole)
                y = quad_[c].process (u, mBlend_) * ringComp_;
                dl_[c][(size_t) w_[c]] = routePre_ ? u : y;
            }
            w_[c] = (w_[c] + 1) & mask_;

            wet[c] = dcOut_[c].process (y) + low[c];
        }

        // Equal-power mix, and 100 % is FULLY wet (dry residual below -60 dB).
        const float wg = std::sin (1.5707963f * mixSm_);
        const float dg = std::cos (1.5707963f * mixSm_);
        outL = dg * dry[0] + wg * wet[0];
        outR = dg * dry[1] + wg * wet[1];

        mImage_ = 0.5f * (std::fabs (wet[0]) + std::fabs (wet[1]));
    }

    // ── meters. fb432: read the engine's OWN numbers, and the SIGN of them —
    //    a device whose mechanism never ran still measures "different".
    float meterShiftHz()  const noexcept { return shiftSm_; }   // signed, in Hz
    float meterLoopGain() const noexcept { return mLoop_;   }   // the real coefficient
    float meterImage()    const noexcept { return mImage_;  }
    float meterBlend()    const noexcept { return mBlend_;  }   // +1 up · 0 ring · -1 down

private:
    static float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    static float lowKeepHz (float k) noexcept
    {
        return clamp01 (k) <= 0.001f ? 20.0f : 20.0f * std::pow (100.0f, clamp01 (k));
    }

    // Character sets the in-loop drive. Unity at the clean end; the makeup is
    // divided back inside softClipC1, so this cannot move the loop gain.
    static float characterDrive (int chr) noexcept
    {
        static const float k[kNumChars] = { 1.0f, 1.6f, 2.6f, 1.2f, 4.0f, 6.5f, 2.0f, 3.2f };
        return k[chr < 0 ? 0 : (chr >= kNumChars ? kNumChars - 1 : chr)];
    }

    float syncedTimeSamples (const Params& p) const noexcept
    {
        if (! p.sync)
        {
            // 0.02 ms .. 1 s, exponential. The short end is a comb, the middle a
            // flange, then slap, then discrete echoes: the knob keeps changing
            // character across its whole travel.
            const float ms = 0.02f * std::pow (50000.0f, clamp01 (p.time));
            return std::max (1.0f, ms * 0.001f * fs_);
        }
        // The house 20-entry division list, folded into this same knob so two
        // selectors can never disagree (there is no SYNCDIV on any device).
        static const float kDiv[20] = { 16.0f, 12.0f, 8.0f, 6.0f, 4.0f, 3.0f, 2.0f, 1.5f,
                                        1.0f, 0.75f, 0.5f, 0.375f, 0.25f, 0.1875f, 0.125f,
                                        0.09375f, 0.0625f, 0.03125f, 0.015625f, 0.0078125f };
        const int  i    = (int) (clamp01 (p.time) * 19.0f + 0.5f);
        const float beats = kDiv[i < 0 ? 0 : (i > 19 ? 19 : i)];
        const float secs  = beats * 60.0f / (float) std::max (20.0, p.bpm);
        return std::max (1.0f, std::min (secs, 16.4f) * fs_);
    }

    Params p_ {};
    float  fs_ = 48000.0f;

    BodeQuad            quad_[2];
    std::vector<float>  dl_[2];
    int                 w_[2] { 0, 0 }, mask_ = 0;
    bode_detail::APDelay blur_[2][4];
    bode_detail::LP1     damp_[2], lowLp_[2];
    bode_detail::DCBlock dcOut_[2];
    bode_detail::SmoothRandom drift_[2];

    float shiftTarget_ = 0.0f, shiftSm_ = 0.0f;
    float fbTarget_ = 0.0f, fbSm_ = 0.0f;
    float timeTarget_ = 480.0f, timeSm_ = 480.0f;
    float mixT_ = 1.0f, mixSm_ = -1.0f;
    float mBlend_ = 1.0f, ringComp_ = 1.0f;
    float clipK_ = 1.0f, blurG_ = 0.30f;
    float touchAmt_ = 0.0f, driftAmt_ = 0.0f, spread_ = -1.0f;
    float blurMix_ = 0.0f;
    int   driftStep_ = 4000;
    bool  lowKeepOn_ = false, routePre_ = false, routePost_ = false;
    bool  routeCross_ = false, routeMono_ = false;
    float env_ = 0.0f, gate_ = 0.0f, mImage_ = 0.0f, mLoop_ = 0.0f;
};

} // namespace tw
