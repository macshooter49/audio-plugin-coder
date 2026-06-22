#pragma once
// =============================================================================
//  FlowGlitch.h  —  Terrain Instrument · FLOW mode GLITCH
//  Waves Crate
//
//  Header-only, NO JUCE. RT-safe in process() (allocation only in prepare()).
//  Sibling of FlowArp.h / FlowChop.h — shares the rate ladder, ArpRng, clamps.
//
//  WHAT IT IS
//  GLITCH is a performance mode (FLOW mode 3). It mangles the synth's OWN output
//  with a randomized, beat-synced chain of buffer effects — stutter repeats,
//  reverses, tape-stops, gates, pitch jumps, bitcrush, spectral-ish freezes, and
//  grain scatter. Where CHOP musically *re-grooves* slices, GLITCH *destroys*
//  them for chaos/fills. Driven by the same five macros + a glass mode menu.
//
//  ── THE NON-NEGOTIABLE: ZERO CLICKS ──────────────────────────────────────────
//  Most stutter/glitch effects click on every repeat — when a grain loops, the
//  read pointer jumps and the waveform steps discontinuously. We kill that the
//  same way CHOP does (the "silent light-switch"), on three fronts:
//    1) MASTER WET GATE — a cosine ramp (zero-slope ends) crossfades dry<->wet
//       on glitch enter and on natural release.
//    2) SEAM-CROSSFADE TAIL READER — INSIDE a glitch, whenever the effect's read
//       pointer jumps (grain wrap, scatter hop) a short cosine crossfade blends
//       the old grain's continuation into the new read. Length is clamped below
//       the effect's seam interval so it always finishes before the next seam.
//    3) DELAYED-COMMIT DUCK — when a NEW glitch fires while one is already wet
//       (a retrigger), the old effect fades to silence over ~2 ms, THEN the new
//       effect is committed and fades up. This covers every effect-type change
//       (read<->direct, direct<->direct) with one uniform mechanism, so no hard
//       wet jump is possible. A fresh glitch from silence commits immediately.
//  Proven offline by asserting max |out[n]-out[n-1]| stays tiny under every
//  effect at punishing settings, including back-to-back retriggers.
//
//  KNOBS (effective = base + LFO mod, summed upstream):
//    RATE -> glitch grid (ladder)        GATE -> slice length (tight<->long stutters)
//    VARY -> fire probability + deja-vu lock    TRAJ -> chaos (effect/pitch/reverse jumps)
//    MORPH-> swing
//  Effect palette, weights, hold length, crush depth, etc. live on the extension
//  panel (most controls there - front = 5 macros + glass mode menu).
//
//  Build test: g++ -std=c++17 Source/FlowGlitch_test.cpp -o /tmp/fg && /tmp/fg
// =============================================================================

#include "FlowArp.h"
#include <vector>
#include <cmath>

namespace wc
{

enum class GlitchFx : int { Repeat = 0, Reverse, TapeStop, Gate, Pitch, Crush, Freeze, Scatter };
static constexpr int kGlitchFxN = 8;

class FlowGlitch
{
public:
    FlowGlitch() { rng_.seed (0x6117C40Du); }

    // -- lifecycle ----------------------------------------------------------------
    void prepare (double sampleRate, double captureSeconds = 4.0) noexcept
    {
        sr_  = (sampleRate > 0.0 ? sampleRate : 44100.0);
        cap_ = (int) std::ceil (sr_ * (captureSeconds > 0.5 ? captureSeconds : 0.5));
        if (cap_ < 1024) cap_ = 1024;
        bufL_.assign ((size_t) cap_, 0.0f);
        bufR_.assign ((size_t) cap_, 0.0f);
        fade_ = (int) std::lround (sr_ * 0.0020);            // 2 ms cosine seam/gate/duck edge
        if (fade_ < 2) fade_ = 2;
        reset();
    }

    void reset() noexcept
    {
        w_ = 0; written_ = 0;
        glitchActive_ = false; gCounter_ = 0; gDur_ = 0; gLen_ = 0; gStartW_ = 0;
        readPosF_ = 0.0; shCnt_ = 0; shHold_ = 0.f; shHoldR_ = 0.f; lastSubGrain_ = -1; scatterBase_ = 0; lastWrap_ = -1;
        wetLevel_ = 0.f; wetTarget_ = 0.f;
        xf_ = 0; xfLen_ = fade_; seamInterval_ = 1 << 30;
        lastIdx_ = 0.0; lastStep_ = 0.0; tailIdx_ = 0.0; tailStep_ = 0.0; haveLast_ = false;
        pendingFire_ = false;
        haveClock_ = false; nextStep_ = 0; nextBoundary_ = 0.0; freePpq_ = 0.0;
        for (int i = 0; i < 64; ++i) lockFireValid_[i] = false;
        rng_.seed (seed_ ? seed_ : 0x6117C40Du);
    }

    // -- configuration (card -> engine) -------------------------------------------
    void setEnabled (GlitchFx fx, bool on) noexcept { enabled_[(int) fx] = on; }
    void setWeight (GlitchFx fx, float wgt) noexcept { weight_[(int) fx] = wgt < 0.f ? 0.f : wgt; }
    void setMix (float wet) noexcept { mix_ = arpClamp01 (wet); }
    void setHoldSteps (float steps) noexcept { holdSteps_ = steps < 0.25f ? 0.25f : (steps > 32.f ? 32.f : steps); }
    void setSeed (uint32_t s) noexcept { seed_ = s; }
    void setDejavu (float d) noexcept { dejavu_ = arpClamp01 (d); }
    void setLoopLen (int n) noexcept { loopLen_ = n < 1 ? 1 : (n > 64 ? 64 : n); }
    void setPitchRatio (float r) noexcept { pitchRatio_ = r < 0.25f ? 0.25f : (r > 4.f ? 4.f : r); }
    void setCrush (int bits, int downsample) noexcept { crushBits_ = bits < 1 ? 1 : (bits > 16 ? 16 : bits); crushDown_ = downsample < 1 ? 1 : (downsample > 64 ? 64 : downsample); }
    void setGateDiv (int div) noexcept { gateDiv_ = div < 1 ? 1 : (div > 32 ? 32 : div); }
    void setTapeCurve (float c) noexcept { tapeCurve_ = c < 0.f ? 0.f : (c > 8.f ? 8.f : c); } // 0 = linear (brake), ~3.5 = tape coast
    void setFreezeGrainMs (float ms) noexcept { freezeMs_ = ms < 1.f ? 1.f : (ms > 200.f ? 200.f : ms); }
    void setScatterMs (float grainMs, float windowMs) noexcept { scatterGrainMs_ = grainMs < 5.f ? 5.f : grainMs; scatterWinMs_ = windowMs < 20.f ? 20.f : windowMs; }

    bool     isActive() const noexcept { return glitchActive_ || wetLevel_ > 0.001f; }
    GlitchFx currentFx() const noexcept { return gFx_; }

    // -- main: process the audio buffer in place ----------------------------------
    void process (float rate, float gate, float vary, float traj, float morph,
                  double hostPpq, double bpm, double sampleRate,
                  float* L, float* R, int numSamples, bool playing) noexcept
    {
        if (cap_ <= 0 || numSamples <= 0) return;
        const double SR  = (sampleRate > 0.0 ? sampleRate : sr_);
        const double BP  = (bpm > 0.0 ? bpm : 120.0);
        const double pps = (BP / 60.0) / SR;
        const float  beats = arpBeatsPerStepRich (rate);
        const double stepSamp = (double) beats / pps;                 // FIX: samples per step (was beats*SR)
        const double sw  = (double) arpClamp01 (morph > 0.9f ? 0.9f : morph) * ((double) beats * 0.5);

        double p = playing ? hostPpq : freePpq_;
        if (! haveClock_) { nextStep_ = (long long) std::floor (p / (double) beats); nextBoundary_ = boundaryTime (nextStep_, beats, sw); haveClock_ = true; }

        for (int i = 0; i < numSamples; ++i, p += pps)
        {
            const float dryL = L[i], dryR = R[i];
            bufL_[(size_t) w_] = dryL;
            bufR_[(size_t) w_] = dryR;

            int guard = 0;
            while (p >= nextBoundary_ && guard++ < 64)
            {
                onBoundary (nextStep_, gate, vary, traj, stepSamp);
                ++nextStep_;
                nextBoundary_ = boundaryTime (nextStep_, beats, sw);
            }

            // commit a pending glitch once the previous one has ducked to silence (or if idle)
            if (pendingFire_ && (! glitchActive_ || wetLevel_ <= 0.001f))
                commitPending();

            float outL = dryL, outR = dryR;
            if (glitchActive_)
            {
                // wet target: duck to 0 while a retrigger waits to commit OR during natural release; else open
                if (pendingFire_)                       wetTarget_ = 0.f;
                else if (gCounter_ >= gDur_ - fade_)     wetTarget_ = 0.f;
                else                                     wetTarget_ = 1.f;

                double idx = 0.0, step = 0.0; bool seam = false; float wl = dryL, wr = dryR;
                const bool isRead = fxRead (gCounter_, dryL, dryR, idx, step, seam, wl, wr);

                if (isRead)
                {
                    // seam is reported explicitly by the effect at a real grain wrap (never inferred)
                    if (seam && haveLast_) { tailIdx_ = lastIdx_ + lastStep_; tailStep_ = lastStep_; xf_ = xfLen_; }

                    const float pL = interp (bufL_, idx), pR = interp (bufR_, idx);
                    if (xf_ > 0)
                    {
                        const float q   = (float) (xfLen_ - xf_) / (float) xfLen_;   // 0->1
                        // EQUAL-POWER seam fade: grain joins are UNCORRELATED (the new read vs
                        // the old grain's tail), so sin/cos (att^2 + rel^2 = 1) holds energy
                        // constant and removes the -3 dB dip a linear (att+rel=1) fade leaves at
                        // every wrap — that dip, repeated at grain rate, is audible as tremolo.
                        const float att = std::sin (1.57079633f * q);
                        const float rel = std::cos (1.57079633f * q);
                        wl = pL * att + interp (bufL_, tailIdx_) * rel;
                        wr = pR * att + interp (bufR_, tailIdx_) * rel;
                        tailIdx_ += tailStep_; --xf_;
                    }
                    else { wl = pL; wr = pR; }

                    lastIdx_ = idx; lastStep_ = step; haveLast_ = true;   // step from the effect, never stale
                }

                // master wet gate (cosine, zero-slope)
                const float inc = 1.0f / (float) fade_;
                if (wetLevel_ < wetTarget_)      wetLevel_ = std::min (wetTarget_, wetLevel_ + inc);
                else if (wetLevel_ > wetTarget_) wetLevel_ = std::max (wetTarget_, wetLevel_ - inc);
                const float wenv = 0.5f - 0.5f * std::cos (3.14159265f * arpClamp01 (wetLevel_));
                const float a = mix_ * wenv;

                outL = dryL + (wl - dryL) * a;
                outR = dryR + (wr - dryR) * a;

                ++gCounter_;
                if (! pendingFire_ && gCounter_ >= gDur_) { glitchActive_ = false; haveLast_ = false; }
            }
            else
            {
                if (wetLevel_ > 0.f) wetLevel_ = std::max (0.f, wetLevel_ - 1.0f / (float) fade_);
            }

            L[i] = flush (outL);
            R[i] = flush (outR);

            if (++w_ >= cap_) w_ = 0;
            if (written_ < cap_) ++written_;
        }

        freePpq_ = (playing ? hostPpq : freePpq_) + pps * (double) numSamples;
    }

private:
    static double boundaryTime (long long step, float beats, double sw) noexcept
    { return (double) step * (double) beats + ((step & 1LL) ? sw : 0.0); }

    // decide fire + effect at a step boundary; latch into PENDING (committed after the duck)
    void onBoundary (long long step, float gate, float vary, float traj, double stepSamp) noexcept
    {
        bool fire;
        if (vary >= 1.0f)      fire = true;
        else if (vary <= 0.0f) fire = false;
        else                   fire = rollFire ((int) (((step % loopLen_) + loopLen_) % loopLen_), vary);
        if (! fire) return;

        pendFx_      = pickFx();
        pendStartW_  = w_;
        double sliceFrac = (double) arpClamp01 (gate);
        if (traj > 0.f) sliceFrac *= (1.0 - (double) traj * 0.5 * rng_.unit());
        pendGLen_ = (int) std::lround (stepSamp * (sliceFrac < 0.03 ? 0.03 : sliceFrac));
        if (pendGLen_ < 2) pendGLen_ = 2;
        if (pendGLen_ > cap_ / 2) pendGLen_ = cap_ / 2;
        pendGDur_ = (int) std::lround (stepSamp * (double) holdSteps_);
        if (pendGDur_ < pendGLen_) pendGDur_ = pendGLen_;
        pendPitch_   = pitchRatio_;
        if (traj > 0.f && rng_.unit() < traj) pendPitch_ = (rng_.unit() < 0.5f) ? 2.0f : 0.5f;
        pendReverse_ = (traj > 0.f && rng_.unit() < traj * 0.5f);

        pendingFire_ = true;                            // process() ducks (if wet) then commits
    }

    void commitPending() noexcept
    {
        gFx_ = pendFx_; gStartW_ = pendStartW_;
        gLen_ = pendGLen_; gDur_ = pendGDur_;
        latchedPitch_ = pendPitch_; latchedReverse_ = pendReverse_;
        gCounter_ = 0; readPosF_ = 0.0; shCnt_ = 0; shHold_ = 0.f; shHoldR_ = 0.f; lastSubGrain_ = -1; lastWrap_ = -1;
        seamInterval_ = computeSeamInterval();
        xfLen_ = fade_;
        if (xfLen_ > seamInterval_ / 2) xfLen_ = seamInterval_ / 2;
        if (xfLen_ < 1) xfLen_ = 1;
        xf_ = 0; haveLast_ = false;
        glitchActive_ = true;
        pendingFire_ = false;
    }

    int computeSeamInterval() const noexcept
    {
        switch (gFx_)
        {
            case GlitchFx::Repeat:
            case GlitchFx::Reverse:  return gLen_ > 1 ? gLen_ : 1;
            case GlitchFx::Pitch:    { const double pr = latchedPitch_ > 0.01f ? latchedPitch_ : 1.0; int s = (int) (gLen_ / pr); return s > 1 ? s : 1; }
            case GlitchFx::Freeze:   { int g = (int) std::lround (sr_ * (double) freezeMs_ * 0.001); if (g < 2) g = 2; if (g > gLen_) g = gLen_; return g; }
            case GlitchFx::Scatter:  { int g = (int) std::lround (sr_ * (double) scatterGrainMs_ * 0.001); if (g < 2) g = 2; return g; }
            case GlitchFx::TapeStop:
            case GlitchFx::Gate:
            case GlitchFx::Crush:
            default:                 return 1 << 30;        // no internal read seam (direct / monotonic)
        }
    }

    bool rollFire (int slot, float chance) noexcept
    {
        if (dejavu_ > 0.f && slot >= 0 && slot < 64)
        {
            if (lockFireValid_[slot] && rng_.unit() < dejavu_) return lockFire_[slot];
            const bool f = rng_.unit() < chance;
            lockFire_[slot] = f; lockFireValid_[slot] = true; return f;
        }
        return rng_.unit() < chance;
    }

    GlitchFx pickFx() noexcept
    {
        float total = 0.f; for (int i = 0; i < kGlitchFxN; ++i) if (enabled_[i]) total += (weight_[i] > 0.f ? weight_[i] : 1.f);
        if (total <= 0.f) return GlitchFx::Repeat;
        float r = rng_.unit() * total;
        for (int i = 0; i < kGlitchFxN; ++i) if (enabled_[i]) { r -= (weight_[i] > 0.f ? weight_[i] : 1.f); if (r <= 0.f) return (GlitchFx) i; }
        return GlitchFx::Repeat;
    }

    float interp (const std::vector<float>& b, double pos) const noexcept
    {
        double fp = std::floor (pos);
        const double frac = pos - fp;
        const int a = wrap ((long long) fp), c = wrap ((long long) fp + 1);
        return b[(size_t) a] + (float) frac * (b[(size_t) c] - b[(size_t) a]);
    }
    int wrap (long long i) const noexcept { long long m = i % cap_; if (m < 0) m += cap_; return (int) m; }

    // per-effect read. true => buffer-read effect (sets idx, step, seam);
    // false => direct (Gate/Crush set wet). step = per-sample idx increment in normal
    // motion; seam = true exactly at a grain wrap (so the seam-crossfade fires once).
    bool fxRead (int t, float dryL, float dryR, double& idx, double& step, bool& seam, float& wetL, float& wetR) noexcept
    {
        step = 0.0; seam = false;
        switch (gFx_)
        {
            case GlitchFx::Repeat:
            {
                const bool rev = latchedReverse_;
                const int  ph  = (gLen_ > 0) ? (t % gLen_) : 0;
                seam = (t > 0 && ph == 0);
                idx  = rev ? ((double) gStartW_ - 1.0 - (double) ph)
                           : ((double) gStartW_ - (double) gLen_ + (double) ph);
                step = rev ? -1.0 : 1.0;
                return true;
            }
            case GlitchFx::Reverse:
            {
                const int ph = (gLen_ > 0) ? (t % gLen_) : 0;
                seam = (t > 0 && ph == 0);
                idx  = (double) gStartW_ - 1.0 - (double) ph;
                step = -1.0;
                return true;
            }
            case GlitchFx::TapeStop:
            {
                // Physically-correct deceleration. A coasting (power-cut) machine slows
                // EXPONENTIALLY (fast initial pitch drop + long low tail), not linearly.
                // tapeCurve_ blends linear (braked stop) -> exponential coast (Kilohearts "Curve").
                const double T = (double) (gDur_ > 0 ? gDur_ : 1);
                const double x = (double) t / T;                       // 0..1 progress
                double rate;
                if (tapeCurve_ < 0.01)                                  // ~linear: braked stop
                    rate = 1.0 - x;
                else                                                   // normalized exp coast: 1 at x=0, 0 at x=1
                {
                    const double k = (double) tapeCurve_;
                    const double ek = std::exp (-k);
                    rate = (std::exp (-k * x) - ek) / (1.0 - ek);
                }
                if (rate < 0.0) rate = 0.0;
                idx  = (double) gStartW_ - (double) gLen_ + readPosF_;
                readPosF_ += rate;
                step = rate; seam = false;            // monotonic, no wrap
                return true;
            }
            case GlitchFx::Gate:
            {
                const int period = (gLen_ > 1) ? (gLen_ / (gateDiv_ < 1 ? 1 : gateDiv_)) : 1;
                const int per = period < 1 ? 1 : period;
                const int ph  = t % per;
                const int half = per / 2 > 0 ? per / 2 : 1;
                int eg = per / 8 + 1;
                if (eg > half) eg = half;
                float g;
                if (ph < half)                      // open window, cosine edges BOTH sides
                {
                    if (ph < eg)              g = 0.5f - 0.5f * std::cos (3.14159265f * (float) ph / (float) eg);
                    else if (ph > half - eg)  g = 0.5f + 0.5f * std::cos (3.14159265f * (float) (ph - (half - eg)) / (float) eg);
                    else                      g = 1.f;
                }
                else g = 0.f;
                wetL = dryL * g; wetR = dryR * g;
                return false;
            }
            case GlitchFx::Pitch:
            {
                // continuous read within the grain, advancing by pitch, wrapping at gLen
                const double pr  = latchedPitch_ > 0.01f ? (double) latchedPitch_ : 1.0;
                const double pos = (double) t * pr;
                const int    wrapIx = (gLen_ > 0) ? (int) std::floor (pos / (double) gLen_) : 0;
                seam = (t > 0 && wrapIx != lastWrap_);
                lastWrap_ = wrapIx;
                const double within = pos - (double) wrapIx * (double) gLen_;
                idx  = (double) gStartW_ - (double) gLen_ + within;
                step = pr;
                return true;
            }
            case GlitchFx::Crush:
            {
                if (shCnt_ <= 0) { shHold_ = dryL; shHoldR_ = dryR; shCnt_ = crushDown_; }
                --shCnt_;
                const float levels = (float) ((1 << crushBits_) - 1);
                wetL = std::round (shHold_  * 0.5f * levels) / (0.5f * levels);
                wetR = std::round (shHoldR_ * 0.5f * levels) / (0.5f * levels);
                return false;
            }
            case GlitchFx::Freeze:
            {
                int grain = (int) std::lround (sr_ * (double) freezeMs_ * 0.001);
                if (grain < 2) grain = 2;
                if (grain > gLen_) grain = gLen_;
                const int ph = t % grain;
                seam = (t > 0 && ph == 0);
                idx  = (double) gStartW_ - (double) grain + (double) ph;
                step = 1.0;
                return true;
            }
            case GlitchFx::Scatter:
            {
                int grain = (int) std::lround (sr_ * (double) scatterGrainMs_ * 0.001);
                if (grain < 2) grain = 2;
                int win = (int) std::lround (sr_ * (double) scatterWinMs_ * 0.001);
                if (win < grain + 1) win = grain + 1;
                if (win > cap_ - 2) win = cap_ - 2;
                const int sub = t / grain;
                seam = (t > 0 && sub != lastSubGrain_);
                if (sub != lastSubGrain_) { lastSubGrain_ = sub; scatterBase_ = (int) (rng_.below (win - grain)); }
                const int ph = t % grain;
                idx  = (double) gStartW_ - (double) win + (double) scatterBase_ + (double) ph;
                step = 1.0;
                return true;
            }
        }
        return false;
    }

    static float flush (float x) noexcept
    {
        if (! (x == x)) return 0.f;
        if (x >  8.f) return 8.f;
        if (x < -8.f) return -8.f;
        if (x < 1.0e-20f && x > -1.0e-20f) return 0.f;
        return x;
    }

    // -- state --------------------------------------------------------------------
    std::vector<float> bufL_, bufR_;
    int      cap_ = 0, w_ = 0, written_ = 0, fade_ = 2;
    double   sr_ = 44100.0;

    bool     enabled_ [kGlitchFxN] = { true, true, true, true, true, true, true, true };
    float    weight_  [kGlitchFxN] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    float    mix_ = 1.0f, holdSteps_ = 1.0f, dejavu_ = 0.0f;
    int      loopLen_ = 8;
    float    pitchRatio_ = 2.0f;
    float    tapeCurve_ = 3.5f;        // tape-stop deceleration curve: 0 linear -> ~3.5 exponential coast (default tape)
    int      crushBits_ = 8, crushDown_ = 4, gateDiv_ = 4;
    float    freezeMs_ = 50.f, scatterGrainMs_ = 40.f, scatterWinMs_ = 500.f;
    uint32_t seed_ = 0x6117C40Du;

    // active glitch instance
    GlitchFx gFx_ = GlitchFx::Repeat;
    bool     glitchActive_ = false;
    int      gCounter_ = 0, gDur_ = 0, gLen_ = 0, gStartW_ = 0;
    double   readPosF_ = 0.0;
    float    shHold_ = 0.f, shHoldR_ = 0.f; int shCnt_ = 0;
    int      lastSubGrain_ = -1, scatterBase_ = 0, lastWrap_ = -1;
    float    latchedPitch_ = 2.0f; bool latchedReverse_ = false;

    // pending (latched at boundary, committed after the duck)
    bool     pendingFire_ = false;
    GlitchFx pendFx_ = GlitchFx::Repeat;
    int      pendStartW_ = 0, pendGLen_ = 0, pendGDur_ = 0;
    float    pendPitch_ = 2.0f; bool pendReverse_ = false;

    // click-free render spine
    float    wetLevel_ = 0.f, wetTarget_ = 0.f;
    int      xf_ = 0, xfLen_ = 2, seamInterval_ = 1 << 30;
    double   lastIdx_ = 0.0, lastStep_ = 0.0, tailIdx_ = 0.0, tailStep_ = 0.0; bool haveLast_ = false;

    // deja-vu fire lock
    bool     lockFire_[64] = { false }; bool lockFireValid_[64];

    // clock
    bool     haveClock_ = false; long long nextStep_ = 0; double nextBoundary_ = 0.0, freePpq_ = 0.0;

    ArpRng   rng_;
};

} // namespace wc
