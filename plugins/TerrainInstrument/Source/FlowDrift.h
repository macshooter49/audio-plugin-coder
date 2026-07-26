#pragma once
// =============================================================================
//  FlowDrift.h  —  Terrain Instrument · FLOW mode DRIFT  ("HUMAN")
//  Waves Crate
//
//  Header-only, NO JUCE. RT-safe in process() (allocation only in prepare()).
//  Sibling of FlowArp.h / FlowChop.h / FlowGlitch.h.
//
//  WHAT IT IS
//  DRIFT/HUMAN is a performance mode (FLOW mode 4) that makes NO sound — it is a
//  generative MODULATION source. It produces organic, evolving motion on several
//  independent lanes (bipolar -1..+1) routed into the mod matrix. Assign lanes to
//  cutoff, wavetable position, pan, FX — the patch breathes and evolves by itself.
//  Marbles / Buchla "Source of Uncertainty" lineage, beat-locked + déjà-vu-lockable.
//
//  Cardinal rules (both PROVEN): stay BOUNDED in [-1,1] (never blow up a knob) and be
//  ZIPPER-FREE when smoothed (GATE high => tiny per-sample change).
//
//  SHAPES (TRAJ selects) — refined per Marbles / Buchla 26x / Wogglebug research:
//    Walk       bounded random walk (reflecting)            — smooth wandering
//    SampleHold stepped random, holds between changes        — classic S&H
//    Smooth     cosine-interpolated random nodes             — smooth random curve
//    Drunk      reflecting walk, livelier step               — drunk-walk
//    Value      2-octave VALUE noise (honestly named; was "Perlin")
//    Chaos      logistic map, regime-mapped (period-doubling -> chaos)
//    Human      Ornstein-Uhlenbeck mean-reverting process    — *breathing*, centered (NEW)
//    Pink       1/f (pink) noise, Paul Kellet 3-pole          — natural fluctuation (NEW)
//    Woggle     stepped + decaying-sinusoid edges (Wogglebug) — organic ring (NEW)
//
//  KNOBS (effective = base + LFO mod, summed upstream):
//    RATE -> step grid (ladder)         GATE -> glide smoothing (0 stepped, 1 glassy)
//    VARY -> volatility / character (per shape)
//    TRAJ -> shape select               MORPH-> depth (output amplitude)
//  Card depth: lane count, déjà-vu (two-sided loop lock), loop length, SPREAD
//  (distribution width), BIAS (skew), seed, per-lane bipolar/unipolar.
//
//  DÉJÀ-VU (Marbles-correct): the loop lock stores the RNG *state* (a seed), not the
//  finished values, and REGENERATES the loop — so SPREAD/BIAS/VARY still reshape a
//  "locked" loop live. Two-sided: 0..noon = recycle probability (lock); noon..max =
//  shuffle probability (replay a random past loop). A 1-lane locked loop = frozen.
//
//  Build test: g++ -std=c++17 Source/FlowDrift_test.cpp -o /tmp/fd && /tmp/fd
// =============================================================================

#include "FlowArp.h"
#include <cmath>

namespace wc
{

// order 0..5 preserved from the original build (Perlin renamed Value); Human/Pink/Woggle appended.
enum class DriftShape : int { Walk = 0, SampleHold, Smooth, Drunk, Value, Chaos, Human, Pink, Woggle };
static constexpr int kDriftShapeN = 9;
static constexpr int kDriftLanes  = 8;     // max lanes
static constexpr int kDriftHist   = 8;     // déjà-vu shuffle history depth (past loops per lane)

inline DriftShape driftShape (float traj) noexcept { return (DriftShape) arpQuantIdx (traj, kDriftShapeN); }

class FlowDrift
{
public:
    FlowDrift() { reseedAll (0xD41F7000u); }

    // -- lifecycle ----------------------------------------------------------------
    void prepare (double sampleRate) noexcept
    {
        sr_ = (sampleRate > 0.0 ? sampleRate : 44100.0);
        reset();
    }

    void reset() noexcept
    {
        haveClock_ = false; nextStep_ = 0; nextBoundary_ = 0.0; freePpq_ = 0.0; lastBeats_ = -1.f;
        stepPhase_ = 0.0; stepLen_ = 1.0; phase2_ = 0.0;
        reseedAll (seed_);
        for (int l = 0; l < kDriftLanes; ++l)
        {
            Lane& L = lane_[l];
            L.out = 0.0; L.outScaled = 0.0; L.target = 0.0;
            L.v0 = L.rng.unit() * 2.0 - 1.0; L.v1 = L.rng.unit() * 2.0 - 1.0;
            L.v0b = L.rng.unit() * 2.0 - 1.0; L.v1b = L.rng.unit() * 2.0 - 1.0;
            L.chaos = 0.30 + 0.40 * L.rng.unit();
            L.x = 0.0; L.pb0 = L.pb1 = L.pb2 = 0.0; L.wFrom = 0.0; L.wTo = 0.0;
            snapLane (l); for (int h = 0; h < kDriftHist; ++h) hist_[l][h] = snap_[l];
            histW_[l] = 0;
        }
        haveSnap_ = false;
    }

    // -- configuration (card -> engine) -------------------------------------------
    void setNumLanes (int n) noexcept { numLanes_ = n < 1 ? 1 : (n > kDriftLanes ? kDriftLanes : n); }
    void setSeed (uint32_t s) noexcept { seed_ = s; }
    void setDejavu (float d) noexcept { dejavu_ = arpClamp01 (d); }      // 0 free · 0.5 locked · 1 shuffled-loop
    void setLoopLen (int n) noexcept { loopLen_ = n < 1 ? 1 : (n > 64 ? 64 : n); }
    void setLaneSpread (float s) noexcept { laneSpread_ = arpClamp01 (s); } // reserved (independent lanes by default)
    void setShapeOverride (int s) noexcept { shapeOv_ = s; }              // -1 = follow TRAJ
    void setBipolar (bool on) noexcept { bipolar_ = on; }                 // false => 0..1 unipolar out
    void setSpread (float s) noexcept { spread_ = arpClamp01 (s); }       // distribution width (Marbles X): 0 constant · .5 unity · 1 extremes/gates
    void setBias  (float b) noexcept { bias_   = arpClamp01 (b); }        // distribution skew low/high

    int       numLanes() const noexcept { return numLanes_; }
    float     value (int lane) const noexcept { return (lane >= 0 && lane < kDriftLanes) ? (float) lane_[lane].outScaled : 0.f; }
    DriftShape activeShape() const noexcept { return curShape_; }

    // -- main: advance the generator; write per-sample lane values ----------------
    void process (float rate, float gate, float vary, float traj, float morph,
                  double hostPpq, double bpm, double sampleRate,
                  float* outLanes, int numSamples, bool playing) noexcept
    {
        if (numSamples <= 0) return;
        const double SR  = (sampleRate > 0.0 ? sampleRate : sr_);
        const double BP  = (bpm > 0.0 ? bpm : 120.0);
        const double pps = (BP / 60.0) / SR;
        const float  beats = arpBeatsPerStepRich (rate);
        stepLen_ = (double) beats / pps;                     // samples per step
        if (stepLen_ < 1.0) stepLen_ = 1.0;

        curShape_ = (shapeOv_ >= 0 && shapeOv_ < kDriftShapeN) ? (DriftShape) shapeOv_ : driftShape (traj);
        const double vol   = (double) arpClamp01 (vary);
        const double depth = (double) arpClamp01 (morph);
        const double glide = glideCoef (arpClamp01 (gate));
        const double dPhase  = 1.0 / stepLen_;
        const double dPhase2 = 2.0 / stepLen_;
        const bool   cont = (curShape_ == DriftShape::Human || curShape_ == DriftShape::Pink || curShape_ == DriftShape::Woggle);

        double p = playing ? hostPpq : freePpq_;
        const long long curStepP = (long long) std::floor (p / (double) beats);
        if (! haveClock_) { nextStep_ = curStepP; nextBoundary_ = (double) nextStep_ * (double) beats; haveClock_ = true; lastBeats_ = beats; }
        else if (beats != lastBeats_ || nextStep_ > curStepP + 2 || nextStep_ < curStepP - 2)
        {
            // fb122/fb128 — THE STRANDED-CLOCK LAW (fb107 class): re-anchor on any grid
            // change OR transport jump (DAW loop wraps stranded the wander mid-play).
            lastBeats_ = beats;
            nextStep_ = curStepP + 1;
            nextBoundary_ = (double) nextStep_ * (double) beats;
        }

        for (int i = 0; i < numSamples; ++i, p += pps)
        {
            int guard = 0;
            while (p >= nextBoundary_ && guard++ < 64)
            {
                onBoundary (nextStep_, vol);
                ++nextStep_;
                nextBoundary_ = (double) nextStep_ * (double) beats;
            }

            // advance smooth-node phases
            stepPhase_ += dPhase; if (stepPhase_ > 1.0) stepPhase_ = 1.0;
            phase2_ += dPhase2;
            if (phase2_ >= 1.0)
            {
                phase2_ -= 1.0;
                for (int l = 0; l < numLanes_; ++l) { lane_[l].v0b = lane_[l].v1b; lane_[l].v1b = (lane_[l].rng.unit() * 2.0 - 1.0) * (0.3 + 0.7 * vol); }
            }

            for (int l = 0; l < numLanes_; ++l)
            {
                Lane& L = lane_[l];
                double raw;
                switch (curShape_)
                {
                    case DriftShape::Smooth: raw = cosInterp (L.v0, L.v1, stepPhase_); break;
                    case DriftShape::Value:  raw = 0.65 * cosInterp (L.v0, L.v1, stepPhase_) + 0.35 * cosInterp (L.v0b, L.v1b, phase2_); break;

                    case DriftShape::Human:  // Ornstein-Uhlenbeck: dX = -theta*X*dt + sigma*sqrt(dt)*N  -> bounded breathing
                    {
                        const double theta = 2.0;                       // mean-reversion speed
                        const double sigma = 0.6 + 3.4 * vol;           // volatility
                        const double n = (L.rng.unit() + L.rng.unit() - 1.0);  // triangular ~N(0, .) cheap
                        L.x += -theta * L.x * dPhase + sigma * std::sqrt (dPhase) * n;
                        raw = std::tanh (1.1 * L.x);                    // bounded + organic saturation
                    } break;

                    case DriftShape::Pink:   // Paul Kellet 3-pole economy pink filter on triangular white
                    {
                        const double w = (L.rng.unit() + L.rng.unit() - 1.0);
                        L.pb0 = 0.99765 * L.pb0 + w * 0.0990460;
                        L.pb1 = 0.96300 * L.pb1 + w * 0.2965164;
                        L.pb2 = 0.57000 * L.pb2 + w * 1.0526913;
                        double pk = (L.pb0 + L.pb1 + L.pb2 + w * 0.1848) * 0.50 * (0.4 + 0.6 * vol);
                        raw = (pk > 1.0) ? 1.0 : (pk < -1.0 ? -1.0 : pk);
                    } break;

                    case DriftShape::Woggle: // damped sinusoid settling wFrom -> wTo across the step
                    {
                        const double ph    = stepPhase_;
                        const double decay = 3.0 + 5.0 * (1.0 - vol);   // less vol => settles faster (fewer rings)
                        const double rings = 1.5 + 6.0 * vol;           // more vol => more rings
                        const double env   = std::exp (-decay * ph);
                        double r = L.wTo + (L.wFrom - L.wTo) * env * std::cos (3.14159265358979 * rings * ph);
                        raw = (r > 1.0) ? 1.0 : (r < -1.0 ? -1.0 : r);
                    } break;

                    default: raw = L.target; break;     // Walk / SampleHold / Drunk / Chaos hold the step target
                }

                // one-pole glide (GATE): smooths everything; near-passthrough at GATE 0, glassy at GATE 1.
                // Universal glide is what guarantees the zipper-free bound for the continuous shapes too.
                L.out += (raw - L.out) * glide;
                if (L.out >  1.0) L.out =  1.0;
                if (L.out < -1.0) L.out = -1.0;

                // distribution shaping (Marbles X): SPREAD width, then BIAS skew. Both bounded.
                double o = applyBias (applySpread (L.out, spread_), bias_);
                o *= depth;                                // MORPH depth
                if (! bipolar_) o = o * 0.5 + 0.5;         // unipolar 0..1
                L.outScaled = o;
                if (outLanes) outLanes[(size_t) l * (size_t) numSamples + (size_t) i] = (float) o;
            }
            (void) cont;
        }

        freePpq_ = (playing ? hostPpq : freePpq_) + pps * (double) numSamples;
    }

private:
    struct Lane
    {
        double out = 0.0, outScaled = 0.0, target = 0.0;
        double v0 = 0.0, v1 = 0.0, v0b = 0.0, v1b = 0.0;
        double chaos = 0.5;
        double x = 0.0;                          // Human (OU) state
        double pb0 = 0.0, pb1 = 0.0, pb2 = 0.0;  // pink filter poles
        double wFrom = 0.0, wTo = 0.0;           // woggle step endpoints
        ArpRng rng;
    };

    // regeneration snapshot for déjà-vu (the RNG seed + the continuous-process anchors).
    struct Snap { uint32_t rngS; double out, target, chaos, x, pb0, pb1, pb2, v0, v1, v0b, v1b, wFrom, wTo; };

    static double cosInterp (double a, double b, double t) noexcept
    { const double m = 0.5 - 0.5 * std::cos (3.14159265358979 * t); return a + (b - a) * m; }

    static double reflect (double x) noexcept
    {
        for (int k = 0; k < 4 && (x > 1.0 || x < -1.0); ++k) { if (x > 1.0) x = 2.0 - x; else if (x < -1.0) x = -2.0 - x; }
        if (x > 1.0) x = 1.0;
        if (x < -1.0) x = -1.0;
        return x;
    }

    // SPREAD: 0 -> constant(0); 0.5 -> unity; 1 -> push to extremes (random gates). Bounded in [-1,1].
    static double applySpread (double x, double spread) noexcept
    {
        if (spread <= 0.5) return x * (spread * 2.0);
        double e = 1.0 - (spread - 0.5) * 1.8; if (e < 0.06) e = 0.06;
        double s = (x < 0.0) ? -1.0 : 1.0, a = std::fabs (x);
        return s * std::pow (a, e);
    }
    // BIAS: skew the whole distribution low/high. Bounded.
    static double applyBias (double x, double bias) noexcept
    {
        double o = x + (bias - 0.5) * 2.0;
        if (o > 1.0) o = 1.0;
        if (o < -1.0) o = -1.0;
        return o;
    }

    double glideCoef (double gate) const noexcept
    {
        const double glideSamples = gate * gate * 0.20 * sr_;   // up to 200 ms at GATE=1
        if (glideSamples < 1.0) return 1.0;
        return 1.0 - std::exp (-1.0 / glideSamples);
    }

    void snapLane (int l) noexcept
    {
        const Lane& L = lane_[l]; Snap& s = snap_[l];
        s.rngS = L.rng.s; s.out = L.out; s.target = L.target; s.chaos = L.chaos; s.x = L.x;
        s.pb0 = L.pb0; s.pb1 = L.pb1; s.pb2 = L.pb2;
        s.v0 = L.v0; s.v1 = L.v1; s.v0b = L.v0b; s.v1b = L.v1b; s.wFrom = L.wFrom; s.wTo = L.wTo;
    }
    void restore (int l, const Snap& s) noexcept
    {
        Lane& L = lane_[l];
        L.rng.s = s.rngS ? s.rngS : 0x9E3779B9u; L.out = s.out; L.target = s.target; L.chaos = s.chaos; L.x = s.x;
        L.pb0 = s.pb0; L.pb1 = s.pb1; L.pb2 = s.pb2;
        L.v0 = s.v0; L.v1 = s.v1; L.v0b = s.v0b; L.v1b = s.v1b; L.wFrom = s.wFrom; L.wTo = s.wTo;
    }
    void pushHist (int l) noexcept { hist_[l][histW_[l]] = snap_[l]; histW_[l] = (histW_[l] + 1) % kDriftHist; }

    void onBoundary (long long step, double vol) noexcept
    {
        if ((step % loopLen_) == 0)
        {
            const double recycle = (dejavu_ <= 0.5) ? (dejavu_ * 2.0) : 1.0;
            const double shuffle = (dejavu_ >  0.5) ? ((dejavu_ - 0.5) * 2.0) : 0.0;
            if (haveSnap_ && recycle > 0.0 && masterRng_.unit() < recycle)
            {
                for (int l = 0; l < kDriftLanes; ++l)
                {
                    if (shuffle > 0.0 && masterRng_.unit() < shuffle)
                        restore (l, hist_[l][(int) masterRng_.below (kDriftHist)]);   // shuffle: replay a random past loop
                    else
                        restore (l, snap_[l]);                                        // recycle: regenerate this loop (seeded)
                }
            }
            else
            {
                for (int l = 0; l < kDriftLanes; ++l) { snapLane (l); pushHist (l); }
                haveSnap_ = true;
            }
        }

        for (int l = 0; l < numLanes_; ++l)
        {
            Lane& L = lane_[l];
            switch (curShape_)
            {
                case DriftShape::Walk:   L.target = reflect (L.target + (L.rng.unit() * 2.0 - 1.0) * (0.15 + 0.85 * vol) * 0.5); break;
                case DriftShape::Drunk:  L.target = reflect (L.target + (L.rng.unit() * 2.0 - 1.0) * (0.30 + 0.70 * vol));      break;
                case DriftShape::SampleHold: if (L.rng.unit() < (0.05 + 0.95 * vol)) L.target = L.rng.unit() * 2.0 - 1.0;        break;
                case DriftShape::Chaos:  { const double r = 3.00 + 0.99 * vol; L.chaos = r * L.chaos * (1.0 - L.chaos); if (L.chaos < 0.0) L.chaos = 0.0; if (L.chaos > 1.0) L.chaos = 1.0; L.target = L.chaos * 2.0 - 1.0; } break;
                case DriftShape::Smooth: L.v0 = L.v1; L.v1 = (L.rng.unit() * 2.0 - 1.0) * (0.3 + 0.7 * vol); break;
                case DriftShape::Value:  L.v0 = L.v1; L.v1 = (L.rng.unit() * 2.0 - 1.0) * (0.3 + 0.7 * vol); break;
                case DriftShape::Woggle: L.wFrom = L.wTo; L.wTo = (L.rng.unit() * 2.0 - 1.0) * (0.4 + 0.6 * vol); break;
                case DriftShape::Human: case DriftShape::Pink: break;   // per-sample continuous (no step target)
            }
        }
        stepPhase_ = 0.0;
    }

    void reseedAll (uint32_t base) noexcept
    {
        masterRng_.seed (base ? base : 0xD41F7000u);
        for (int l = 0; l < kDriftLanes; ++l)
            lane_[l].rng.seed ((base ? base : 0xD41F7000u) + (uint32_t) (l + 1) * 0x9E3779B1u);  // decorrelated streams
    }

    // -- state --------------------------------------------------------------------
    double   sr_ = 44100.0;
    Lane     lane_[kDriftLanes];
    Snap     snap_[kDriftLanes]; bool haveSnap_ = false;
    Snap     hist_[kDriftLanes][kDriftHist]; int histW_[kDriftLanes] = {0};
    ArpRng   masterRng_;

    int      numLanes_ = 4, loopLen_ = 8, shapeOv_ = -1;
    float    dejavu_ = 0.0f, laneSpread_ = 1.0f, spread_ = 0.5f, bias_ = 0.5f;
    bool     bipolar_ = true;
    uint32_t seed_ = 0xD41F7000u;
    DriftShape curShape_ = DriftShape::Walk;

    bool     haveClock_ = false; long long nextStep_ = 0; double nextBoundary_ = 0.0, freePpq_ = 0.0;
    float lastBeats_ = -1.f;   // fb122: re-anchor the step clock on any grid change
    double   stepPhase_ = 0.0, stepLen_ = 1.0, phase2_ = 0.0;
};

} // namespace wc
