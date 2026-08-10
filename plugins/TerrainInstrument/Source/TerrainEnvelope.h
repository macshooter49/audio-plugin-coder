// =============================================================================
//  TerrainEnvelope.h  —  Terrain Instrument DAHDSR envelope generator
// -----------------------------------------------------------------------------
//  One per-voice, per-sample envelope: Delay → Attack → Hold → Decay → Sustain
//  → Release, with a per-segment curve/tension control, click-free retrigger,
//  note-off-from-any-stage, and an optional LOOP mode that turns the envelope
//  into a tempo-syncable cycling modulator (env-as-LFO).
//
//  DESIGN CONTRACT (mirrors the Terrain DSP playbook):
//   • base→effective: the OWNER sets effective stage values each block (already
//     summed with modulation + velocity). This class consumes effective values
//     only and LATCHES per-segment timing at the moment a segment begins
//     (HALion pattern) — so modulating a time mid-segment never glitches the
//     in-progress ramp. Sustain LEVEL may be updated continuously (it's a held
//     target) and is handled safely.
//   • click-free: retrigger does NOT snap to zero — attack ramps from the
//     current output toward the peak. Note-off from any stage releases from the
//     current level. No discontinuities.
//   • curve: per-segment tension in [-1,+1]. 0 = linear; >0 = fast-start/ease-out
//     (musical for decay/release); <0 = slow-start/ease-in. Implemented as a
//     pure shaping function of normalized segment progress so it is exact,
//     allocation-free, and independent of sample rate.
//   • NO JUCE dependency — header-only, offline-compilable, unit-testable.
//
//  Times are in SECONDS (owner converts ms / beat-divisions before setting).
//  Output is in [0,1] for unipolar envelopes; the owner applies polarity/depth.
// =============================================================================
#pragma once

#include <cmath>
#include <algorithm>

namespace terrain
{

class TerrainEnvelope
{
public:
    enum class Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release };

    TerrainEnvelope() = default;

    // ── lifecycle ───────────────────────────────────────────────────────────
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
        susSmCoef_  = 1.0 - std::exp (-1.0 / (0.0025 * sampleRate_)); // fb204: 2.5ms sustain glide
        reset();
    }

    void reset() noexcept
    {
        stage_      = Stage::Idle;
        level_      = 0.0;
        susSm_      = sustain_;   // fb204 — glide snaps to target at rest
        segPos_     = 0.0;
        segLen_     = 0.0;
        segStart_   = 0.0;
        segTarget_  = 0.0;
        gateOn_     = false;
    }

    // ── per-segment EFFECTIVE parameters (owner sets each block, already
    //    base+modulation summed). Times in seconds, levels/curves normalized. ──
    void setDelay   (double seconds) noexcept { delaySec_   = clampTime (seconds); }
    void setAttack  (double seconds) noexcept { attackSec_  = clampTime (seconds); }
    void setHold    (double seconds) noexcept { holdSec_    = clampTime (seconds); }
    void setDecay   (double seconds) noexcept { decaySec_   = clampTime (seconds); }
    void setSustain (double level)   noexcept { sustain_    = clamp01  (level);   }
    void setRelease (double seconds) noexcept { releaseSec_ = clampTime (seconds); }
    // fb297 — DECLICK FLOOR: minimum release length (seconds). release=0 on a gate/square amp env
    // otherwise steps sustain→0 in ONE sample = a note-off click. The owner sets this on the AMP env
    // only (~5 ms) so "zero release" stays tight but the VCA always ramps down. 0 = off (other envs).
    void setMinRelease (double seconds) noexcept { minReleaseSec_ = seconds < 0.0 ? 0.0 : seconds; }

    // curve/tension per time-segment, each in [-1,+1]
    void setAttackCurve  (double t) noexcept { curveA_ = clampCurve (t); }
    void setDecayCurve   (double t) noexcept { curveD_ = clampCurve (t); }
    void setReleaseCurve (double t) noexcept { curveR_ = clampCurve (t); }

    // LOOP: when true, on reaching the end of Decay (sustain point) the envelope
    // re-triggers Attack instead of holding Sustain — a cycling A(H)D source.
    // Release still fires on note-off and exits the loop cleanly.
    void setLoop (bool shouldLoop) noexcept { loop_ = shouldLoop; }

    // ── note events ──────────────────────────────────────────────────────────
    // Click-free: starts from the CURRENT level (retrigger/legato safe).
    void noteOn() noexcept
    {
        gateOn_ = true;
        if (stage_ == Stage::Idle) susSm_ = sustain_;   // fb204 — fresh note starts on-target
        if (delaySec_ > 0.0)
            enterStage (Stage::Delay);
        else
            enterStage (Stage::Attack);
    }

    // Release from wherever we are, using the CURRENT level as the start.
    void noteOff() noexcept
    {
        gateOn_ = false;
        if (stage_ != Stage::Idle && stage_ != Stage::Release)
            enterStage (Stage::Release);
    }

    bool isActive() const noexcept { return stage_ != Stage::Idle; }
    Stage stage()  const noexcept { return stage_; }
    double level() const noexcept { return level_; }

    // Fraction [0,1] through the CURRENT segment (for the UI follower's x-position).
    double segFraction() const noexcept
    {
        if (segLen_ <= 1.0) return 1.0;
        const double f = segPos_ / segLen_;
        return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    }

    // ── per-sample tick — returns the envelope value in [0,1] ─────────────────
    double tick() noexcept
    {
        // fb204 — SUSTAIN GLIDE (2.5ms): the owner pushes a modulated sustain once per
        // block; consuming it raw staircased the level (audible zipper when an LFO rides
        // Sus — worst on the AMP env, where sustain IS the gain). One one-pole here
        // smooths every consumer: decay retarget, sustain stage, and the matrix taps.
        susSm_ += (sustain_ - susSm_) * susSmCoef_;
        switch (stage_)
        {
            case Stage::Idle:
                return 0.0;

            case Stage::Delay:
                // flat at the start level (usually 0); advance time only.
                if (advanceSeg())
                    enterStage (Stage::Attack);
                level_ = segStart_;             // delay holds the entry level
                return level_;

            case Stage::Attack:
            {
                const bool done = advanceSeg();
                level_ = shape (segStart_, segTarget_, segPos_ / segLenSafe(), curveA_);
                if (done) { level_ = segTarget_; enterStage (Stage::Hold); }
                return level_;
            }

            case Stage::Hold:
                if (advanceSeg())
                    enterStage (Stage::Decay);
                level_ = 1.0;                   // hold sits at peak
                return level_;

            case Stage::Decay:
            {
                const bool done = advanceSeg();
                // decay target tracks the (possibly changing) sustain level (fb204: glided)
                segTarget_ = susSm_;
                level_ = shape (segStart_, segTarget_, segPos_ / segLenSafe(), curveD_);
                if (done)
                {
                    level_ = susSm_;
                    if (loop_ && gateOn_) enterStage (Stage::Attack);   // env-as-LFO
                    else                  enterStage (Stage::Sustain);
                }
                return level_;
            }

            case Stage::Sustain:
                // hold at the live sustain target (fb204: glided — no block staircase)
                level_ = susSm_;
                return level_;

            case Stage::Release:
            {
                const bool done = advanceSeg();
                level_ = shape (segStart_, 0.0, segPos_ / segLenSafe(), curveR_);
                if (done) { level_ = 0.0; enterStage (Stage::Idle); }
                return level_;
            }
        }
        return 0.0;
    }

private:
    // ── stage entry: latches segment length + start/target from CURRENT level ─
    void enterStage (Stage s) noexcept
    {
        stage_    = s;
        segPos_   = 0.0;
        segStart_ = level_;            // ALWAYS start the ramp from where we are

        switch (s)
        {
            case Stage::Delay:
                segLen_    = delaySec_ * sampleRate_;
                segTarget_ = segStart_;
                break;
            case Stage::Attack:
                segLen_    = attackSec_ * sampleRate_;
                segTarget_ = 1.0;
                if (segLen_ < 1.0) { level_ = 1.0; enterStage (Stage::Hold); return; }
                break;
            case Stage::Hold:
                segLen_    = holdSec_ * sampleRate_;
                segStart_  = 1.0; segTarget_ = 1.0; level_ = 1.0;
                if (segLen_ < 1.0) { enterStage (Stage::Decay); return; }
                break;
            case Stage::Decay:
                segLen_    = decaySec_ * sampleRate_;
                segStart_  = 1.0; segTarget_ = sustain_;
                if (segLen_ < 1.0)
                {
                    level_ = sustain_;
                    if (loop_ && gateOn_) { enterStage (Stage::Attack); return; }
                    enterStage (Stage::Sustain); return;
                }
                break;
            case Stage::Sustain:
                segLen_    = 0.0;
                segTarget_ = sustain_;
                break;
            case Stage::Release:
            {
                // fb297 — never a 1-sample step: a 0 (or sub-ms) release is floored to minReleaseSec_
                // so the amp VCA ramps down over a few ms (declick) instead of jumping sustain→0.
                const double relSec = (releaseSec_ > minReleaseSec_) ? releaseSec_ : minReleaseSec_;
                segLen_    = relSec * sampleRate_;
                segTarget_ = 0.0;
                if (segLen_ < 1.0) { level_ = 0.0; enterStage (Stage::Idle); return; }
                break;
            }
            case Stage::Idle:
                segLen_ = 0.0; level_ = 0.0; segStart_ = 0.0; segTarget_ = 0.0;
                break;
        }
    }

    // advance the segment clock by one sample; returns true if the segment is done
    bool advanceSeg() noexcept
    {
        segPos_ += 1.0;
        return segPos_ >= segLen_;
    }

    double segLenSafe() const noexcept { return (segLen_ > 1.0) ? segLen_ : 1.0; }

    // ── curve shaper: maps progress p∈[0,1] → eased p', then lerps start→target.
    //    tension t∈[-1,1]: 0 linear; t>0 fast-start/ease-out; t<0 slow-start.
    //
    //    Uses the VITAL/SURGE exponential bias  e = (exp(P·p) − 1)/(exp(P) − 1).
    //    This is the function both Vital and Surge XT independently converged on
    //    for envelope curvature. Unlike a power curve p^k, its slope is FINITE at
    //    BOTH endpoints for every P — so it can never blow up. It hits (0,0) and
    //    (1,1) exactly (normalized ratio) and is strictly monotonic. The SAME
    //    formula is mirrored in the WebUI renderer, so the graph is exactly what
    //    you hear. P = t·8 (the research-recommended 6–10 range). ───────────────
    //  CPU: exp(P) — the DENOMINATOR — depends only on the segment's curve t, not on p, so it
    //  is constant across the whole segment yet was being recomputed EVERY sample. We cache it
    //  and refresh only when t actually changes (a segment boundary). Result is identical (same
    //  formula, same division) — just ONE exp() per sample instead of two. t stays live via the
    //  `!=` compare, so mid-segment curve automation still applies (no behaviour change).
    double biasCurve (double p, double t) noexcept
    {
        if (std::fabs (t) < 1.0e-3) return p;        // linear (removable singularity)
        if (t != curveDenomT_)                       // curve changed → refresh the constant terms
        {
            curveDenomT_ = t;
            curveP_      = -t * 8.0;                  // t∈[-1,1] → P∈[8,-8]; +t = fast-rise (concave)
            curveDenom_  = std::exp (curveP_) - 1.0;  // constant across the segment
        }
        return (std::exp (curveP_ * p) - 1.0) / curveDenom_;
    }
    double shape (double start, double target, double p, double t) noexcept
    {
        p = clamp01 (p);
        const double e = biasCurve (p, t);
        return start + (target - start) * e;
    }

    static double clamp01   (double v) noexcept { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
    static double clampTime (double v) noexcept { return v < 0.0 ? 0.0 : (v > 60.0 ? 60.0 : v); }
    static double clampCurve(double v) noexcept { return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v); }

    // config (effective values, set by owner)
    double sampleRate_ = 48000.0;
    double delaySec_   = 0.0;
    double attackSec_  = 0.005;
    double holdSec_    = 0.0;
    double decaySec_   = 0.10;
    double sustain_    = 0.70;
    double susSm_      = 0.70;   // fb204 — glided sustain (2.5ms one-pole toward sustain_)
    double susSmCoef_  = 0.02;   // fb204 — set in prepare()
    double releaseSec_ = 0.20;
    double minReleaseSec_ = 0.0;   // fb297 — declick floor for the release (0 = off; amp env sets ~5ms)
    double curveA_     = 0.0;
    double curveD_     = 0.0;
    double curveR_     = 0.0;
    bool   loop_       = false;

    // runtime state
    Stage  stage_     = Stage::Idle;
    double level_     = 0.0;
    double segPos_    = 0.0;     // samples elapsed in current segment
    double segLen_    = 0.0;     // segment length in samples
    double segStart_  = 0.0;     // level at segment entry
    double segTarget_ = 0.0;     // level the segment ramps toward
    bool   gateOn_    = false;

    // biasCurve constant-term cache (refreshed only when the curve tension t changes)
    double curveDenomT_ = 2.0;   // last t seen (sentinel outside [-1,1] → forces first compute)
    double curveP_      = 0.0;   // -t·8 for the cached t
    double curveDenom_  = 1.0;   // exp(curveP_) − 1  (the per-segment constant denominator)
};

} // namespace terrain
