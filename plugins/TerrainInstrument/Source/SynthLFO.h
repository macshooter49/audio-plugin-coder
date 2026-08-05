#pragma once
// =============================================================================
//  SynthLFO.h  —  Terrain Instrument · per-voice LFO (Batch 1)
//  Waves Crate
//
//  Header-only, NO JUCE dependency (like TerrainEnvelope.h) so it can be unit-
//  tested offline with a plain g++ build. SynthVoice (which uses JUCE) includes
//  this and owns one SynthLFO per LFO slot, per voice.
//
//  Contract (locked in the batch plan):
//    - A free-running LFO is a phase accumulator: phase += hz/SR; wrap to [0,1).
//    - Tempo-synced Hz: straightHz = (BPM/60) * (4/denom);
//      triplet = *3/2 (period *2/3); dotted = /1.5 (period *1.5); bars = /(4*bars).
//    - Output is BIPOLAR [-1,+1]. Polarity can fold it to unipolar +/- if asked.
//    - Trigger modes decide phase behaviour on note-on (Batch 1 ships Free + Trig;
//      Sync/Env/Sustain-loop are wired in the enum + honoured in Batch 2).
//    - The render path NEVER reads anything but the returned value; the matrix in
//      SynthVoice accumulates value*depth into the effective destination.
// =============================================================================

#include <cmath>
#include <cstdint>

namespace wc
{

// LFO ARC L1 — drawn-shape table length. Tables carry kLfoTableN+1 floats with
// [kLfoTableN] == [0] so the cycle-wrap linear interp never branches.
constexpr int kLfoTableN = 256;

// ── LFO built-in shapes (Batch 1 set; custom-curve mode arrives in Batch 4) ──
enum class LFOShape : int
{
    Sine = 0,
    Triangle,
    SawUp,      // ramp up   -1 -> +1
    SawDown,    // ramp down +1 -> -1
    Square,
    SampleHold, // idx 5 — fb239 DUNE: repurposed to an ever-going free-running wander (no cycle lock)
    Random,     // smooth random — cosine-interpolated wander between per-cycle targets
    Custom,     // LFO ARC L1 — drawn breakpoint shape (table baked by the owner; setCustomTable)
    Path,       // idx 8 — fb239 PATH: free 2D drawing, baked to the table by arc-length (owner) — DSP-identical to Custom
    Lorenz,     // idx 9 — fb239 (fb240 renamed from Eddy): Lorenz strange attractor, free-running per-sample
    Rossler,    // idx 10 — fb239 (fb240 renamed from Vortex): Rossler strange attractor, free-running
    NumShapes
};

// fb239 — free-running shapes ignore phase/cycle: they carry continuous state advanced by
// the rate. The owner (processor) uses this to skip the transport-phase lock for them.
inline bool isFreeRunShape (LFOShape s) noexcept
{
    return s == LFOShape::SampleHold || s == LFOShape::Lorenz || s == LFOShape::Rossler;
}

// ── Trigger / retrigger behaviour (Vital's taxonomy) ──
enum class LFOTrigger : int
{
    Free = 0,   // never resets; continuous phase, shared feel across notes
    Trig,       // reset to startPhase on every note-on (per-voice)
    Sync,       // phase locked to host transport (set externally; no note reset)
    Env,        // one-shot: run once to the end, then hold (Batch 2 honours fully)
    SustainLoop // run intro once, then loop body until note-off (Batch 2)
};

// ── Output polarity ──
enum class LFOPolarity : int { Bipolar = 0, UniPlus, UniMinus };

// Immutable per-LFO settings. Copied into the voice's LFO on config swap.
struct LFOSettings
{
    LFOShape    shape    = LFOShape::Sine;
    bool        sync     = false;     // true = tempo-synced, false = free Hz
    float       rateHz   = 1.0f;      // free-mode rate
    int         syncIdx  = 5;         // index into SyncDivisions (see SynthModConfig.h)
    float       startPhase = 0.0f;    // [0,1) phase at note-on for Trig/Env modes
    float       phaseOffset = 0.0f;   // [0,1) read-phase shift — "slides" the waveform L/R
    LFOTrigger  trigger  = LFOTrigger::Free;
    LFOPolarity polarity = LFOPolarity::Bipolar;
    // fb228 — L5 MOTION (blob-fed; the card's controls)
    int   direction = 0;      // 0 Fwd · 1 Rev · 2 PingPong (phase remap at every read)
    float loopPt    = -1.0f;  // Env mode: >=0 = play once to the end, then LOOP the tail from here
    float riseMs    = 0.0f;   // per-note depth fade-in from the start value (Serum RISE)
    float delayMs   = 0.0f;   // per-note hold before the LFO starts moving
    float smoothMs  = 0.0f;   // user output smoothing (stretches the house 2.5ms slew)
    float swing     = 0.0f;   // 0..1 — alternate cycles long/short (2:1 triplet feel at 1)
    int   tripDot   = 0;      // 0 straight · 1 triplet (x1.5 rate) · 2 dotted (x2/3 rate)
    bool  reseed    = false;  // fb245 — reseed chaos/S&H/RNG on every note-on (per-note variety; default off = the free-running wander)
    // depth lives on the *route* (Assignment), not here — one LFO can drive many
    // destinations at different depths. This struct is the *shape generator*.
};

class SynthLFO
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr_ = (sampleRate > 0.0 ? sampleRate : 44100.0);
        // deterministic per-instance RNG seed; reseeded per note for variety later
        rngState_ = 0x2545F4914F6CDD1DULL;
        // fb142-lfo — OUTPUT SLEW (the fb126 comb law: a modulated value must GLIDE,
        // never snap). ~2.5ms one-pole on the returned value turns every step the
        // discontinuous shapes make (S&H hold jump, Square edge, Saw wrap, Random
        // re-roll, note-on phase reset, transport re-lock) into a short fade — the
        // click Max hears on S&H/Saw/Square→cutoff dies here, at the source, so all
        // ~366 destinations benefit at once. Sine/Triangle pass through ~unchanged
        // (<0.05% at LFO rates). Reset snaps (RESET == LOAD-DEFAULT): first output
        // after prepare() takes the raw value, no glide from stale state.
        slewRate_ = 1.0f / (0.0025f * (float) sr_);
        slewK1_   = 1.0f - std::exp (-slewRate_);
        slew_     = 0.0f;
        slewInit_ = false;
        freeRunSeeded_ = false;   // fb239 — first free-run setSettings will seed (honours startPhase)
        seedFreeRun();            // deterministic state (reset == load-default)
    }

    void setSettings (const LFOSettings& s) noexcept { s_ = s;
        // fb228 — SMOOTH = the house slew with a user tau (never below the 2.5ms declick floor)
        const float tau = (s_.smoothMs > 2.5f ? s_.smoothMs * 0.001f : 0.0025f);
        slewRate_ = 1.0f / (tau * (float) sr_);
        slewK1_   = 1.0f - std::exp (-slewRate_);
        // fb239 — seed the free-run engine ONCE on entry into a chaos/dune shape (so startPhase is
        // honoured and the Free-mode mirror starts cleanly); never reseed per-block or the swirl freezes.
        if (isFreeRunShape (s_.shape)) { if (! freeRunSeeded_) { seedFreeRun(); freeRunSeeded_ = true; } }
        else                            freeRunSeeded_ = false; }

    // LFO ARC L1 — wire the drawn-shape table (kLfoTableN+1 floats, owner-managed stable
    // storage; content updates in place at block top so edits reach every consumer with
    // zero per-block copies). Null = triangle fallback, so an unwired Custom stays sane.
    void setCustomTable (const float* t) noexcept { customTable_ = t; }

    // hz: the resolved frequency for THIS block (free rate, or synced Hz computed
    // by the owner from BPM). Kept as an explicit argument so the LFO stays free of
    // transport knowledge — SynthModConfig::syncedHz() does that conversion.
    void setFrequency (float hz) noexcept { hz_ = (hz > 0.0f ? hz : 0.0f); }

    // Note-on: apply the trigger mode's phase policy.
    void noteOn() noexcept
    {
        delayLeft_ = (int) (s_.delayMs * 0.001f * (float) sr_);          // fb228 — DELAY
        riseInc_   = (s_.riseMs > 1.0f) ? 1.0f / (s_.riseMs * 0.001f * (float) sr_) : 0.0f;
        riseGain_  = (riseInc_ > 0.0f) ? 0.0f : 1.0f;                    // fb228 — RISE
        swingOdd_  = false;
        switch (s_.trigger)
        {
            case LFOTrigger::Trig:
            case LFOTrigger::Env:
            case LFOTrigger::SustainLoop:
                phase_ = wrap01 (s_.startPhase);
                finished_ = false;
                stepPrev_ = nextRandom();               // two distinct seeds so Random wanders
                stepHeld_ = nextRandom();               // from cycle one
                heldPhaseQuadrant_ = -1;
                seedFreeRun();                          // fb239 — Trig/Env restart the swirl/wander per note
                break;
            case LFOTrigger::Free:
            case LFOTrigger::Sync:
            default:
                // leave phase running
                break;
        }
        if (s_.reseed) reseedForNote();   // fb245 — per-note reseed fires on EVERY note (even Free-triggered chaos, which never hits the switch above)
        riseBase_ = shapeAt (dirP (wrap01 (phase_ + s_.phaseOffset)));   // fb228 — RISE fades in FROM the start value (Serum grammar)
    }

    // Advance one sample and return the modulation value.
    float processSample() noexcept
    {
        if (delayLeft_ > 0)   // fb228 — DELAY: hold at the start value, phase frozen
        {
            --delayLeft_;
            return slewAdvance (applyPolarity (applyRise (shapeAt (dirP (wrap01 (phase_ + s_.phaseOffset))))), 1.0f);
        }
        if (riseGain_ < 1.0f) { riseGain_ += riseInc_; if (riseGain_ > 1.0f) riseGain_ = 1.0f; }
        if (isFreeRunShape (s_.shape))   // fb239 — chaos/dune: integrate the continuous system, ignore phase
        {
            advanceFreeRun (1.0f);
            return slewAdvance (applyPolarity (applyRise (freeOut_)), 1.0f);
        }
        const float out = applyRise (shapeAt (dirP (wrap01 (phase_ + s_.phaseOffset))));   // phaseOffset slides the read point · dirP maps direction

        // advance
        if (! (s_.trigger == LFOTrigger::Env && finished_))
        {
            const float inc = effInc();   // fb228 — trip/dot + swing live in the increment
            phase_ += inc;
            if (phase_ >= 1.0f)
            {
                // one cycle completed
                if (s_.trigger == LFOTrigger::Env)
                {
                    if (s_.loopPt >= 0.0f && s_.loopPt < 0.999f)   // fb228 — LOOPBACK: intro once, then cycle the tail
                    {
                        const float span = 1.0f - s_.loopPt;
                        phase_ = s_.loopPt + std::fmod (phase_ - 1.0f, span);
                        swingOdd_ = ! swingOdd_;
                        stepPrev_ = stepHeld_; stepHeld_ = nextRandom();
                    }
                    else { phase_ = 1.0f; finished_ = true; }   // hold at end (one-shot)
                }
                else
                {
                    phase_ -= std::floor (phase_);
                    swingOdd_ = ! swingOdd_;
                    stepPrev_ = stepHeld_;        // remember last target for smooth Random
                    stepHeld_ = nextRandom();     // new S&H / Random step at the wrap
                }
            }
        }
        return slewAdvance (applyPolarity (out), 1.0f);   // fb142-lfo — glide, never snap
    }

    // CPU: advance the phase by n samples WITHOUT evaluating the shape — for LFOs that
    // nothing consumes per-sample this block (the per-block mod matrix reads peek(), which
    // only needs phase). S&H/Random roll one new step per wrap so peek() stays plausible.
    void skipSamples (int n) noexcept
    {
        if (delayLeft_ > 0) { const int eat = (n < delayLeft_ ? n : delayLeft_); delayLeft_ -= eat; n -= eat; }   // fb228 — DELAY holds
        if (riseGain_ < 1.0f && n > 0) { riseGain_ += riseInc_ * (float) n; if (riseGain_ > 1.0f) riseGain_ = 1.0f; }
        if (isFreeRunShape (s_.shape))   // fb239 — advance the swirl/wander, keep peek() live & non-stale
        {
            if (n > 0) advanceFreeRun ((float) n);
            slewAdvance (applyPolarity (applyRise (freeOut_)), (float) (n > 0 ? n : 1));
            return;
        }
        if (n > 0 && ! (s_.trigger == LFOTrigger::Env && finished_))
        {
            const float inc = effInc();
            phase_ += inc * (float) n;
            if (phase_ >= 1.0f)
            {
                if (s_.trigger == LFOTrigger::Env)
                {
                    if (s_.loopPt >= 0.0f && s_.loopPt < 0.999f)
                    {
                        const float span = 1.0f - s_.loopPt;
                        phase_ = s_.loopPt + std::fmod (phase_ - 1.0f, span);
                        swingOdd_ = ! swingOdd_;
                        stepPrev_ = stepHeld_; stepHeld_ = nextRandom();
                    }
                    else { phase_ = 1.0f; finished_ = true; }
                }
                else
                {
                    phase_ -= std::floor (phase_);
                    swingOdd_ = ! swingOdd_;
                    stepPrev_ = stepHeld_;
                    stepHeld_ = nextRandom();
                }
            }
        }
        // fb142-lfo — advance the output slew across the skipped span (n-sample compound
        // coefficient = same trajectory the per-sample path walks), so peek() stays smooth
        // AND non-stale for block-rate consumers. Runs even when a finished Env holds, or
        // a mid-glide slew would strand slightly off the held end value forever.
        slewAdvance (applyPolarity (applyRise (shapeAt (dirP (wrap01 (phase_ + s_.phaseOffset))))), (float) (n > 0 ? n : 1));
    }

    // For tempo-sync mode: set phase directly from host transport (Batch 2 uses this).
    void setPhaseFromTransport (float phase01) noexcept
    {
        const float np = wrap01 (phase01);
        // fb142-lfo — the transport-locked branch is the ONLY advance a sync'd global LFO
        // gets while playing (the owner teleports phase per block instead of calling
        // processSample), so the output slew must move HERE or peek() strands stale.
        // Elapsed samples come from the phase delta (dp·sr/hz — exact while the transport
        // rolls continuously); a transport JUMP (loop wrap, relocate) clamps the estimate
        // and simply GLIDES — the phase-jump value click dies with the shape-step click.
        float dp = np - phase_; dp -= std::floor (dp);            // wrapped forward delta
        float nSamp = (hz_ > 1.0e-6f) ? dp * (float) sr_ / hz_ : 256.0f;
        if (nSamp < 1.0f) nSamp = 1.0f; else if (nSamp > 8192.0f) nSamp = 8192.0f;
        phase_ = np;
        if (isFreeRunShape (s_.shape))   // fb239 — sync'd chaos/dune: integrate by elapsed time, no phase meaning
        {
            advanceFreeRun (nSamp);
            slewAdvance (applyPolarity (applyRise (freeOut_)), nSamp);
            return;
        }
        slewAdvance (applyPolarity (shapeAt (wrap01 (phase_ + s_.phaseOffset))), nSamp);
    }

    float currentPhase() const noexcept { return phase_; }

    // fb239 — free-run 2D viz projection (Eddy/Vortex trajectory · Dune drift). Range ~[-1,1].
    float chaosVX() const noexcept { return vx_; }
    float chaosVY() const noexcept { return vy_; }

    // Current modulation value at the present phase WITHOUT advancing — for per-block
    // (frame/warp/fold/pitch) modulation that's computed before the per-sample loop.
    // fb142-lfo — returns the SLEWED tracker (every advance path keeps it current), so
    // block-rate consumers ride the same click-free glide; raw until the first advance.
    float peek() const noexcept
    {
        return slewInit_ ? slew_ : applyPolarity (applyRise (shapeAt (dirP (wrap01 (phase_ + s_.phaseOffset)))));
    }

private:
    // fb228 — L5 motion helpers
    float dirP (float p) const noexcept   // direction: Fwd / Rev / PingPong (fold = fwd+back inside one cycle)
    {
        if (s_.direction == 1) return 1.0f - p;
        if (s_.direction == 2) return (p < 0.5f) ? p * 2.0f : 2.0f - p * 2.0f;
        return p;
    }
    float effInc() const noexcept         // trip/dot rate multiplier + swing's alternate-cycle stretch
    {
        float h = hz_;
        if      (s_.tripDot == 1) h *= 1.5f;
        else if (s_.tripDot == 2) h *= (2.0f / 3.0f);
        if (s_.swing > 0.001f)
            h /= (1.0f + (swingOdd_ ? -1.0f : 1.0f) * (s_.swing * 0.3333f));
        return h / (float) sr_;
    }
    float applyRise (float v) const noexcept   // RISE: fade from the note-on value toward the live shape
    {
        return (riseGain_ >= 1.0f) ? v : riseBase_ + (v - riseBase_) * riseGain_;
    }
    // Map phase [0,1) -> bipolar [-1,+1]
    float shapeAt (float p) const noexcept
    {
        switch (s_.shape)
        {
            case LFOShape::Sine:     return std::sin (p * 6.2831853071795864f);
            case LFOShape::Triangle: return 1.0f - 4.0f * std::fabs (p - 0.5f);  // -1..+1..-1
            case LFOShape::SawUp:    return 2.0f * p - 1.0f;
            case LFOShape::SawDown:  return 1.0f - 2.0f * p;
            case LFOShape::Square:   return (p < 0.5f) ? 1.0f : -1.0f;
            case LFOShape::SampleHold: return stepHeld_;
            case LFOShape::Random:
            {
                // cosine-interpolate prev -> held across the cycle: a smooth random wander
                const float m = 0.5f * (1.0f - std::cos (p * 3.14159265358979f));
                return stepPrev_ + (stepHeld_ - stepPrev_) * m;
            }
            case LFOShape::Custom:
            case LFOShape::Path:      // fb239 — Path traverses its arc-length-baked table exactly like Custom
            {
                if (customTable_ == nullptr)                        // unwired — triangle fallback
                    return 1.0f - 4.0f * std::fabs (p - 0.5f);
                const float f  = p * (float) kLfoTableN;            // p in [0,1) → f < kLfoTableN
                const int   i0 = (int) f;
                const float fr = f - (float) i0;
                return customTable_[i0] + fr * (customTable_[i0 + 1] - customTable_[i0]);
            }
            default:                 return 0.0f;
        }
    }

    float applyPolarity (float bip) const noexcept
    {
        switch (s_.polarity)
        {
            case LFOPolarity::UniPlus:  return 0.5f * (bip + 1.0f);  // [0,1]
            case LFOPolarity::UniMinus: return 0.5f * (bip - 1.0f);  // [-1,0]
            case LFOPolarity::Bipolar:
            default:                    return bip;                  // [-1,1]
        }
    }

    static float wrap01 (float p) noexcept { p -= std::floor (p); return p; }

    // fb142-lfo — one-pole toward target across n samples of wall-time (n=1 uses the
    // precomputed per-sample coefficient; block spans compound: 1-exp(-n/(τ·sr))).
    // First call after prepare() SNAPS — reset must equal load-default.
    float slewAdvance (float target, float n) noexcept
    {
        if (! slewInit_) { slew_ = target; slewInit_ = true; return slew_; }
        const float k = (n == 1.0f) ? slewK1_ : 1.0f - std::exp (-n * slewRate_);
        slew_ += k * (target - slew_);
        return slew_;
    }

    // ── fb239 — FREE-RUNNING ENGINES (chaos attractors + the ever-going dune wander) ──
    static float  clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static double clampd (double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    // integration + speed tuning (see the offline sim: bounded, non-repeating, lively at rate 2Hz)
    static constexpr double kLorenzSpeed = 7.0;   // attractor-time units per (Hz·second)
    static constexpr double kLorenzHMax  = 0.006; // Euler stability ceiling per sub-step
    static constexpr double kRosslerSpeed= 4.5;
    static constexpr double kRosslerHMax = 0.02;
    static constexpr int    kSubCap      = 16;
    static constexpr float  kDuneSpeed   = 2.4f;  // wander-phase units per (Hz·second)

    void seedFreeRun() noexcept
    {
        // deterministic start + startPhase jitter so poly voices diverge (chaos = sensitive to seed)
        const double j = (double) s_.startPhase;
        cx_ = 0.10 + 0.90 * j; cy_ = 0.0; cz_ = (s_.shape == LFOShape::Lorenz) ? 18.0 : 0.0;
        freeOut_ = 0.0f; vx_ = 0.0f; vy_ = 0.0f;
        duneAcc_  = 0.0f; duneSeg_  = 0.5f;  dunePrev_  = nextRandom(); duneTgt_  = nextRandom();
        duneAcc2_ = 0.0f; duneSeg2_ = 0.17f; dunePrev2_ = nextRandom(); duneTgt2_ = nextRandom();
    }

    // fb245 — per-note RESEED (only when s_.reseed): unlike seedFreeRun's deterministic start,
    // this kicks the free-run seed with a FRESH random draw so successive notes trace a
    // different path (chaos is seed-sensitive — a small kick diverges the whole trajectory).
    // The kick stays inside each attractor's basin, so it converges cleanly, never blows up.
    // The S&H/Random sequence re-rolls too. rngState_ keeps evolving, so note N+1 ≠ note N.
    void reseedForNote() noexcept
    {
        const double r0 = (double) nextRandom(), r1 = (double) nextRandom(), r2 = (double) nextRandom();
        cx_ = 0.10 + 0.90 * (double) s_.startPhase + 0.30 * r0;
        cy_ = 0.20 * r1;
        cz_ = (s_.shape == LFOShape::Lorenz) ? 18.0 + 4.0 * r2 : 0.0;
        freeOut_ = 0.0f; vx_ = 0.0f; vy_ = 0.0f;
        duneAcc_  = 0.0f; duneSeg_  = 0.5f;  dunePrev_  = nextRandom(); duneTgt_  = nextRandom();
        duneAcc2_ = 0.0f; duneSeg2_ = 0.17f; dunePrev2_ = nextRandom(); duneTgt2_ = nextRandom();
        stepPrev_ = nextRandom(); stepHeld_ = nextRandom();   // re-roll S&H/Random
    }

    void advanceFreeRun (float nSamp) noexcept
    {
        if      (s_.shape == LFOShape::Lorenz)  advanceLorenz  (nSamp);
        else if (s_.shape == LFOShape::Rossler) advanceRossler (nSamp);
        else                                   advanceDune    (nSamp);   // SampleHold slot = DUNE
    }

    void advanceLorenz (float nSamp) noexcept
    {
        const double SIG = 10.0, RHO = 28.0, BET = 8.0 / 3.0;
        double dtSim = (double) hz_ * kLorenzSpeed * (double) nSamp / sr_;
        int nsub = (int) std::ceil (dtSim / kLorenzHMax); if (nsub < 1) nsub = 1; if (nsub > kSubCap) nsub = kSubCap;
        const double h = dtSim / (double) nsub;
        for (int i = 0; i < nsub; ++i)
        {
            const double dx = SIG * (cy_ - cx_), dy = cx_ * (RHO - cz_) - cy_, dz = cx_ * cy_ - BET * cz_;
            cx_ += h * dx; cy_ += h * dy; cz_ += h * dz;
        }
        if (! std::isfinite (cx_) || std::fabs (cx_) > 60.0) { cx_ = 0.1; cy_ = 0.0; cz_ = 18.0; }   // blow-up guard
        freeOut_ = clampf ((float) (cx_ / 18.0), -1.0f, 1.0f);
        vx_ = clampf ((float) (cx_ / 22.0), -1.0f, 1.0f);
        vy_ = clampf ((float) ((cz_ - 24.0) / 26.0), -1.0f, 1.0f);   // x–z plane = the butterfly
    }

    void advanceRossler (float nSamp) noexcept
    {
        const double A = 0.2, B = 0.2, C = 5.7;
        double dtSim = (double) hz_ * kRosslerSpeed * (double) nSamp / sr_;
        int nsub = (int) std::ceil (dtSim / kRosslerHMax); if (nsub < 1) nsub = 1; if (nsub > kSubCap) nsub = kSubCap;
        const double h = dtSim / (double) nsub;
        for (int i = 0; i < nsub; ++i)
        {
            const double dx = -cy_ - cz_, dy = cx_ + A * cy_, dz = B + cz_ * (cx_ - C);
            cx_ += h * dx; cy_ += h * dy; cz_ += h * dz;
        }
        if (! std::isfinite (cx_) || std::fabs (cx_) > 40.0) { cx_ = 0.1; cy_ = 0.0; cz_ = 0.0; }
        freeOut_ = clampf ((float) (cx_ / 9.0), -1.0f, 1.0f);
        vx_ = clampf ((float) (cx_ / 11.0), -1.0f, 1.0f);
        vy_ = clampf ((float) (cy_ / 11.0), -1.0f, 1.0f);   // x–y plane = the single scroll
    }

    void advanceDune (float nSamp) noexcept
    {
        const float sp = hz_ * kDuneSpeed / (float) sr_ * nSamp;   // wander-phase advanced this call
        duneAcc_ += sp;                                            // main octave — irregular segments, smoothstep between
        while (duneAcc_ >= duneSeg_) { duneAcc_ -= duneSeg_; dunePrev_ = duneTgt_; duneTgt_ = nextRandom();
            duneSeg_ = 0.35f + 0.9f * (0.5f * (nextRandom() + 1.0f)); }
        const float t = (duneSeg_ > 1e-6f) ? duneAcc_ / duneSeg_ : 0.0f; const float sm = t * t * (3.0f - 2.0f * t);
        const float base = dunePrev_ + (duneTgt_ - dunePrev_) * sm;
        duneAcc2_ += sp * 3.3f;                                    // faster octave = 'turns into so many shapes'
        while (duneAcc2_ >= duneSeg2_) { duneAcc2_ -= duneSeg2_; dunePrev2_ = duneTgt2_; duneTgt2_ = nextRandom();
            duneSeg2_ = 0.12f + 0.30f * (0.5f * (nextRandom() + 1.0f)); }
        const float t2 = (duneSeg2_ > 1e-6f) ? duneAcc2_ / duneSeg2_ : 0.0f; const float sm2 = t2 * t2 * (3.0f - 2.0f * t2);
        const float oct2 = dunePrev2_ + (duneTgt2_ - dunePrev2_) * sm2;
        freeOut_ = clampf (base * 0.82f + oct2 * 0.28f, -1.0f, 1.0f);
        vx_ = base; vy_ = oct2;
    }

    // xorshift64* — cheap, deterministic, audio-thread safe (no allocation/locks)
    float nextRandom() noexcept
    {
        rngState_ ^= rngState_ >> 12;
        rngState_ ^= rngState_ << 25;
        rngState_ ^= rngState_ >> 27;
        uint64_t r = rngState_ * 0x2545F4914F6CDD1DULL;
        // top 24 bits -> [0,1) -> [-1,1)
        float u = static_cast<float> ((r >> 40) & 0xFFFFFF) / static_cast<float> (0x1000000);
        return 2.0f * u - 1.0f;
    }

    double      sr_        = 44100.0;
    float       hz_        = 1.0f;
    int   delayLeft_ = 0;                  // fb228 — DELAY countdown (samples)
    float riseGain_  = 1.0f;               // fb228 — RISE ramp 0->1
    float riseInc_   = 0.0f;
    float riseBase_  = 0.0f;               // the value RISE fades from
    bool  swingOdd_  = false;              // fb228 — SWING cycle parity
    float       phase_     = 0.0f;
    float       stepHeld_  = 0.0f;
    float       stepPrev_  = 0.0f;   // previous S&H target — smooth Random interpolates from it
    // fb239 — free-run state
    double      cx_ = 0.1, cy_ = 0.0, cz_ = 0.0;   // attractor coordinates
    float       freeOut_ = 0.0f, vx_ = 0.0f, vy_ = 0.0f;   // free-run output + 2D viz projection
    float       duneAcc_ = 0.0f, duneSeg_ = 0.5f,  dunePrev_ = 0.0f, duneTgt_ = 0.0f;
    float       duneAcc2_ = 0.0f, duneSeg2_ = 0.17f, dunePrev2_ = 0.0f, duneTgt2_ = 0.0f;
    bool        freeRunSeeded_ = false;
    bool        finished_  = false;
    int         heldPhaseQuadrant_ = -1;
    uint64_t    rngState_  = 0x2545F4914F6CDD1DULL;
    float       slew_      = 0.0f;    // fb142-lfo — slewed output tracker (what peek() serves)
    float       slewK1_    = 0.0091f; //   per-sample one-pole coefficient (~2.5ms @ 44.1k)
    float       slewRate_  = 0.0091f; //   1/(τ·sr) — compound coefficient base for n-sample spans
    bool        slewInit_  = false;   //   false until first advance after prepare() (snap once)
    const float* customTable_ = nullptr;   // LFO ARC L1 — drawn shape (owner-managed lifetime)
    LFOSettings s_;
};

} // namespace wc
