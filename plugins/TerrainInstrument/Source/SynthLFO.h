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

// ── LFO built-in shapes (Batch 1 set; custom-curve mode arrives in Batch 4) ──
enum class LFOShape : int
{
    Sine = 0,
    Triangle,
    SawUp,      // ramp up   -1 -> +1
    SawDown,    // ramp down +1 -> -1
    Square,
    SampleHold, // stepped random, one new value per cycle
    Random,     // smooth random — cosine-interpolated wander between per-cycle targets
    NumShapes
};

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
    }

    void setSettings (const LFOSettings& s) noexcept { s_ = s; }

    // hz: the resolved frequency for THIS block (free rate, or synced Hz computed
    // by the owner from BPM). Kept as an explicit argument so the LFO stays free of
    // transport knowledge — SynthModConfig::syncedHz() does that conversion.
    void setFrequency (float hz) noexcept { hz_ = (hz > 0.0f ? hz : 0.0f); }

    // Note-on: apply the trigger mode's phase policy.
    void noteOn() noexcept
    {
        switch (s_.trigger)
        {
            case LFOTrigger::Trig:
            case LFOTrigger::Env:
            case LFOTrigger::SustainLoop:
                phase_ = wrap01 (s_.startPhase);
                finished_ = false;
                stepPrev_ = nextRandom();               // two distinct seeds so Random wanders
                stepHeld_ = nextRandom();               // from cycle one (S&H just uses held)
                heldPhaseQuadrant_ = -1;
                break;
            case LFOTrigger::Free:
            case LFOTrigger::Sync:
            default:
                // leave phase running
                break;
        }
    }

    // Advance one sample and return the modulation value.
    float processSample() noexcept
    {
        const float out = shapeAt (wrap01 (phase_ + s_.phaseOffset));   // phaseOffset slides the read point

        // advance
        if (! (s_.trigger == LFOTrigger::Env && finished_))
        {
            const float inc = hz_ / static_cast<float> (sr_);
            phase_ += inc;
            if (phase_ >= 1.0f)
            {
                // one cycle completed
                if (s_.trigger == LFOTrigger::Env)
                {
                    phase_ = 1.0f;          // hold at end (one-shot)
                    finished_ = true;
                }
                else
                {
                    phase_ -= std::floor (phase_);
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
        if (! (s_.trigger == LFOTrigger::Env && finished_))
        {
            const float inc = hz_ / static_cast<float> (sr_);
            phase_ += inc * (float) n;
            if (phase_ >= 1.0f)
            {
                if (s_.trigger == LFOTrigger::Env) { phase_ = 1.0f; finished_ = true; }
                else
                {
                    phase_ -= std::floor (phase_);
                    stepPrev_ = stepHeld_;
                    stepHeld_ = nextRandom();
                }
            }
        }
        // fb142-lfo — advance the output slew across the skipped span (n-sample compound
        // coefficient = same trajectory the per-sample path walks), so peek() stays smooth
        // AND non-stale for block-rate consumers. Runs even when a finished Env holds, or
        // a mid-glide slew would strand slightly off the held end value forever.
        slewAdvance (applyPolarity (shapeAt (wrap01 (phase_ + s_.phaseOffset))), (float) n);
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
        slewAdvance (applyPolarity (shapeAt (wrap01 (phase_ + s_.phaseOffset))), nSamp);
    }

    float currentPhase() const noexcept { return phase_; }

    // Current modulation value at the present phase WITHOUT advancing — for per-block
    // (frame/warp/fold/pitch) modulation that's computed before the per-sample loop.
    // fb142-lfo — returns the SLEWED tracker (every advance path keeps it current), so
    // block-rate consumers ride the same click-free glide; raw until the first advance.
    float peek() const noexcept
    {
        return slewInit_ ? slew_ : applyPolarity (shapeAt (wrap01 (phase_ + s_.phaseOffset)));
    }

private:
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
    float       phase_     = 0.0f;
    float       stepHeld_  = 0.0f;
    float       stepPrev_  = 0.0f;   // previous S&H target — smooth Random interpolates from it
    bool        finished_  = false;
    int         heldPhaseQuadrant_ = -1;
    uint64_t    rngState_  = 0x2545F4914F6CDD1DULL;
    float       slew_      = 0.0f;    // fb142-lfo — slewed output tracker (what peek() serves)
    float       slewK1_    = 0.0091f; //   per-sample one-pole coefficient (~2.5ms @ 44.1k)
    float       slewRate_  = 0.0091f; //   1/(τ·sr) — compound coefficient base for n-sample spans
    bool        slewInit_  = false;   //   false until first advance after prepare() (snap once)
    LFOSettings s_;
};

} // namespace wc
