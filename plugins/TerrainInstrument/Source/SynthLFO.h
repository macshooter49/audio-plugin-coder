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
        return applyPolarity (out);
    }

    // For tempo-sync mode: set phase directly from host transport (Batch 2 uses this).
    void setPhaseFromTransport (float phase01) noexcept { phase_ = wrap01 (phase01); }

    float currentPhase() const noexcept { return phase_; }

    // Current modulation value at the present phase WITHOUT advancing — for per-block
    // (frame/warp/fold/pitch) modulation that's computed before the per-sample loop.
    float peek() const noexcept { return applyPolarity (shapeAt (wrap01 (phase_ + s_.phaseOffset))); }

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
    LFOSettings s_;
};

} // namespace wc
