#pragma once
// =============================================================================
//  FlowSeq.h  —  Terrain Instrument · FLOW mode 2 (SEQ) — FULL ENGINE
//  Waves Crate
//
//  Header-only, NO JUCE. RT-safe (fixed buffers, no alloc, no locks).
//  Sibling of FlowArp.h — reuses its clock helpers, NoteSet, ArpRng, clamps,
//  and the proven rate ladder. The ARP derives a pattern; the SEQ plays a DRAWN
//  pattern over the HELD CHORD (degree-indexed), and now carries the full feature
//  set of a top-tier modern sequencer (Serum 2 / Vital / Elektron / Cthulhu /
//  Marbles lineage). The extension-card UI binds to this model.
//
//  ── FEATURE MAP (every "feat") ───────────────────────────────────────────────
//  PER-STEP NOTE GRID (SeqStep[32], one length):
//    • on/off trig · degree (chord/scale) · octave · gate · velocity
//    • trig CONDITION  {Always, Prob%, Ratio A:B, Fill, !Fill, 1st, !1st, Pre, !Pre}
//    • RATCHET count (+ rate via gate) + mode {Repeat, ChordUp, ChordDown} + vel ramp
//    • SLIDE (portamento/legato, no env retrig)  · TIE (sustain across boundary)
//    • ACCENT (vel boost + accent mod flag)       · MICRO-TIMING (±half step)
//    • PLAY mode {Single, Chord(block), Strum} + voicing/inversion + strum spread
//    • chord-tone REACH (probability-weighted degree selection, the Cthulhu Rand-Sel)
//    • per-step PARAMETER LOCKS (sparse, hold-until-next-lock) → matrix destinations
//  GLOBAL / PATTERN:
//    • 10 DIRECTION modes {Fwd,Rev,PingPong,Pendulum,Random,RandomSkip,Brownian,
//      Shuffle,Converge,Diverge}  · SWING (MORPH)  · RATE ladder (1/1…1/256)
//    • SCALE/KEY lock + quantize (chord-degree mode OR scale-degree mode)
//  GENERATIVE:
//    • déjà-vu lockable loop (separately for RHYTHM trigs and PITCH selection),
//      Spread + Bias on the random distribution, reseed
//    • Euclidean generator · Humanize (timing+velocity jitter) · Mutate
//  MODULATION (sequencer-as-mod-source — beats Serum 2, which keeps them separate):
//    • N independent MOD LANES, bipolar, each own length + rate + smoothing
//      → polymeter/polyrhythm AND first-class matrix mod sources
//
//  Front-panel 5 knobs (effective = base + LFO-mod, already summed upstream):
//    RATE→division · GATE→global gate scale · VARY→probability amount ·
//    TRAJ→direction · MORPH→swing.  Deep params live on the card.
//
//  process() emits a note-event stream (SeqEvent[]), updates mod-lane outputs
//  (read via modValue), and resolves active parameter locks (read via the lock
//  getters). The processor/CC applies mod-lane values + locks to real params and
//  realises slide/tie as voice-side portamento/legato.
//
//  Build test: g++ -std=c++17 Source/FlowSeq_test.cpp -o /tmp/flowseq && /tmp/flowseq
// =============================================================================

#include "FlowArp.h"   // ArpEvent, NoteSet, ArpRng, arpBeatsPerStep, arpGateFrac, arpSortAsc, arpClamp01/i, kArpRate
#include <cmath>

namespace wc
{

static constexpr int kSeqMaxSteps        = 32;
static constexpr int kSeqModLanes        = 4;     // independent bipolar mod lanes
static constexpr int kSeqMaxRatchet      = 8;
static constexpr int kSeqMaxLocksPerStep = 4;
static constexpr int kSeqMaxActiveLocks  = 16;
static constexpr int kSeqMaxOpen         = 32;    // notes sounding at once (block chords + ratchet tails)
static constexpr int kSeqMaxEvents       = 256;   // note events emitted per block

// ── trig conditions ──────────────────────────────────────────────────────────
enum class SeqCond : int { Always = 0, Prob, Ratio, Fill, NotFill, First, NotFirst, Pre, NotPre };
static constexpr int kSeqCondN = 9;
struct StepCond { SeqCond type = SeqCond::Always; float prob = 1.0f; int a = 1, b = 1; };

enum class StepPlay    : int { Single = 0, Chord, Strum };
enum class RatchetMode : int { Repeat = 0, ChordUp, ChordDown };

struct StepLock { int paramId = -1; float value = 0.0f; };

struct SeqStep
{
    bool        on        = true;
    int         degree    = 0;        // index into sorted held chord (chord mode) or scale (scale mode)
    int         octave    = 0;
    float       gate      = 0.5f;     // 0..1 (× global GATE)
    float       vel       = 0.8f;     // 0..1 (× note velocity)
    StepCond    cond;                 // trig condition
    int         ratchet   = 1;        // 1 = single hit; >1 = retriggers
    RatchetMode ratMode   = RatchetMode::Repeat;
    float       ratVelRamp= 0.0f;     // -1..+1 velocity fade across the ratchet burst
    bool        slide     = false;    // portamento/legato into next, no env retrigger
    bool        tie       = false;    // sustain through the next step boundary
    bool        accent    = false;    // velocity boost + accent flag for the matrix
    float       micro     = 0.0f;     // -0.5..+0.5 of a step (nudge)
    StepPlay    play      = StepPlay::Single;
    float       strum     = 0.0f;     // 0..1 spread amount (sign of (strum-0.5) chooses up/down)
    int         voicing   = 0;        // inversion/voicing index for Chord/Strum
    int         reach     = 0;        // chord-tone reach: 0 = exact degree; >0 = weighted pick up to +reach
    StepLock    locks[kSeqMaxLocksPerStep];
    int         numLocks  = 0;
};

struct SeqModLane
{
    float val[kSeqMaxSteps] = { 0 };  // bipolar -1..+1
    int   length  = 16;
    float rate    = 1.0f;             // multiplier vs main step rate (>1 faster)
    float smooth  = 0.0f;             // 0 stepped … →1 smoothed (per-block one-pole slew)
    float current = 0.0f;             // runtime smoothed output
};

// ── direction ────────────────────────────────────────────────────────────────
enum class SeqDir : int { Forward = 0, Reverse, PingPong, Pendulum, Random, RandomSkip, Brownian, Shuffle, Converge, Diverge };
static constexpr int kSeqDirN = 10;
inline SeqDir seqDir (float traj) noexcept { return (SeqDir) arpQuantIdx (traj, kSeqDirN); }

// pendulum/converge/diverge are pure; random/brownian/shuffle keep state in the class.
inline int seqDeterministicPos (SeqDir dir, long long k, int len) noexcept
{
    if (len <= 1) return 0;
    switch (dir)
    {
        case SeqDir::Forward:  return (int) (k % len);
        case SeqDir::Reverse:  return (int) (len - 1 - (k % len));
        case SeqDir::PingPong: { const long long p = 2 * len; long long m = k % p; return (int) (m < len ? m : p - 1 - m); }       // repeats endpoints
        case SeqDir::Pendulum: { const long long p = 2 * len - 2; long long m = k % p; return (int) (m < len ? m : p - m); }       // no endpoint repeat
        case SeqDir::Converge: { // 0, len-1, 1, len-2, ...
            const long long i = k % len; const int half = (int) (i / 2);
            return (int) ((i & 1) ? (len - 1 - half) : half); }
        case SeqDir::Diverge:  { // middle-out: mid, mid+1, mid-1, mid+2, mid-2 ...
            const long long i = k % len; const int mid = len / 2; const int s = (int) ((i + 1) / 2);
            const int p = (i & 1) ? (mid + s) : (mid - s);
            return p < 0 ? 0 : (p > len - 1 ? len - 1 : p); }
        default: return (int) (k % len);
    }
}

// ── swing clock (drift-free, bar-anchored; odd steps delayed by swing) ─────────
struct SeqHit { int sampleOffset; long long stepIndex; double ppq; };
inline int seqStepsInBlock (double pos, double bpm, double sr, int numSamples,
                            float beats, float swing, SeqHit* out, int maxOut) noexcept
{
    if (beats <= 0.f || bpm <= 0.0 || sr <= 0.0 || numSamples <= 0) return 0;
    const double pps    = (bpm / 60.0) / sr;
    const double ppqEnd = pos + pps * (double) numSamples;
    const double sw     = (double) arpClamp01 (swing > 0.9f ? 0.9f : swing) * ((double) beats * 0.5);
    long long idx = (long long) std::floor (pos / (double) beats) - 1;
    int n = 0;
    for (; n < maxOut; ++idx)
    {
        const double nominal = (double) idx * (double) beats;
        const double t = nominal + ((idx & 1LL) ? sw : 0.0);
        if (t >= ppqEnd) { if (nominal >= ppqEnd) break; else continue; }
        if (t < pos) continue;
        int off = (int) std::llround ((t - pos) / pps);
        if (off < 0) off = 0;
        if (off >= numSamples) off = numSamples - 1;   // clamp — never drop a boundary step
        out[n++] = { off, idx, t };
    }
    return n;
}

// ── scale helpers ────────────────────────────────────────────────────────────
inline int seqScaleSize (uint16_t mask) noexcept { int c = 0; for (int i = 0; i < 12; ++i) if (mask & (1u << i)) ++c; return c; }
inline int seqScaleNote (int root, uint16_t mask, int degree) noexcept
{
    const int sz = seqScaleSize (mask);
    if (sz <= 0) return root + degree;
    const int oct = (degree >= 0) ? degree / sz : -(((-degree) + sz - 1) / sz);
    const int idx = ((degree % sz) + sz) % sz;
    int found = 0, semi = 0;
    for (int i = 0; i < 12; ++i) if (mask & (1u << i)) { if (found == idx) { semi = i; break; } ++found; }
    return root + 12 * oct + semi;
}

// ── Euclidean (bucket method) → fills `on` flags ──────────────────────────────
inline void seqEuclid (int steps, int pulses, int rot, bool* out) noexcept
{
    steps  = arpClampi (steps, 1, kSeqMaxSteps);
    pulses = arpClampi (pulses, 0, steps);
    int bucket = 0;
    bool tmp[kSeqMaxSteps] = { false };
    for (int i = 0; i < steps; ++i) { bucket += pulses; if (bucket >= steps) { bucket -= steps; tmp[i] = true; } }
    for (int i = 0; i < steps; ++i) { int j = ((i - rot) % steps + steps) % steps; out[i] = tmp[j]; }
}

// distribution-shaped chord-tone offset in [0, reach]; Spread widens, Bias skews.
inline int seqWeightedOffset (int reach, float spread, float bias, ArpRng& rng) noexcept
{
    if (reach <= 0) return 0;
    float u = rng.unit();
    const float bexp = (bias <= 0.5f) ? (1.0f + (0.5f - bias) * 4.0f)
                                      : (1.0f / (1.0f + (bias - 0.5f) * 4.0f));
    u = std::pow (u, bexp);
    const float r = (float) reach * (0.25f + 0.75f * arpClamp01 (spread));
    int off = (int) std::lround (u * r);
    return off < 0 ? 0 : (off > reach ? reach : off);
}

// SEQ note event — richer than ArpEvent: carries legato (slide/tie) + accent.
struct SeqEvent { bool on; int note; int vel; int sampleOffset; bool legato; bool accent; };

// =============================================================================
//  FlowSeq
// =============================================================================
class FlowSeq
{
public:
    FlowSeq() { setDefaultPattern(); reseed(); }

    // ── lifecycle ─────────────────────────────────────────────────────────────
    void reset() noexcept
    {
        held_.count = 0; latched_.count = 0; latchActive_ = false;
        openN_ = 0; pendN_ = 0; freePpq_ = 0.0; lastVel_ = 100;
        brownPos_ = 0; shufBuilt_ = false; prevCond_ = false;
        for (auto& l : modLanes_) l.current = 0.f;
        activeN_ = 0;
        rng_.seed (0x51E0BEEFu);
        reseed();
    }
    void reseed() noexcept { for (int i = 0; i < kSeqMaxSteps; ++i) { rollValid_[i] = false; trigValid_[i] = false; } }

    void setLatch (bool on) noexcept { latchEnabled_ = on; if (! on) { latched_.count = 0; latchActive_ = false; } }
    void setFill  (bool on) noexcept { fillActive_ = on; }

    // ── pattern + global params (UI → engine) ──────────────────────────────────
    void    setLength (int n) noexcept { length_ = arpClampi (n, 1, kSeqMaxSteps); }
    int     length () const noexcept   { return length_; }
    void    setStep (int i, const SeqStep& s) noexcept { if (i >= 0 && i < kSeqMaxSteps) steps_[i] = s; }
    SeqStep getStep (int i) const noexcept { return (i >= 0 && i < kSeqMaxSteps) ? steps_[i] : SeqStep{}; }

    void setScale (int root, uint16_t mask, bool useScale) noexcept { scaleRoot_ = arpClampi (root, 0, 127); scaleMask_ = mask; useScale_ = useScale; }
    void setGenerative (float dejavuRhythm, float dejavuPitch, float spread, float bias, float humanTime, float humanVel) noexcept
    {
        dejavuR_ = arpClamp01 (dejavuRhythm); dejavuP_ = arpClamp01 (dejavuPitch);
        spread_  = arpClamp01 (spread);       bias_    = arpClamp01 (bias);
        humanT_  = arpClamp01 (humanTime);    humanV_  = arpClamp01 (humanVel);
    }

    // mod lanes
    void  setModLane (int i, const SeqModLane& l) noexcept { if (i >= 0 && i < kSeqModLanes) modLanes_[i] = l; }
    void  setModStep (int lane, int step, float v) noexcept { if (lane >= 0 && lane < kSeqModLanes && step >= 0 && step < kSeqMaxSteps) modLanes_[lane].val[step] = v < -1 ? -1 : (v > 1 ? 1 : v); }
    void  setModLaneCfg (int lane, int len, float rate, float smooth) noexcept { if (lane >= 0 && lane < kSeqModLanes) { auto& L = modLanes_[lane]; L.length = arpClampi (len, 1, kSeqMaxSteps); L.rate = rate <= 0 ? 1.f : rate; L.smooth = arpClamp01 (smooth); } }
    float modValue (int i) const noexcept { return (i >= 0 && i < kSeqModLanes) ? modLanes_[i].current : 0.f; }

    // active parameter locks (processor reads + applies)
    int   activeLockCount () const noexcept { return activeN_; }
    int   activeLockParam (int i) const noexcept { return (i >= 0 && i < activeN_) ? active_[i].paramId : -1; }
    float activeLockValue (int i) const noexcept { return (i >= 0 && i < activeN_) ? active_[i].value : 0.f; }
    void  clearLocks () noexcept { activeN_ = 0; }

    // generators / one-shot edits (UI actions)
    void applyEuclid (int pulses, int rot) noexcept
    {
        bool on[kSeqMaxSteps]; seqEuclid (length_, pulses, rot, on);
        for (int i = 0; i < length_; ++i) steps_[i].on = on[i];
    }
    void humanizeReseed () noexcept { reseed(); }
    void mutate (float amount, uint32_t seed) noexcept    // nudge degrees / toggle trigs within bounds
    {
        ArpRng r; r.seed (seed ? seed : 0xA53Fu);
        const int n = length_;
        for (int i = 0; i < n; ++i)
        {
            if (r.unit() < amount * 0.5f) steps_[i].on = ! steps_[i].on;                 // flip some trigs
            if (r.unit() < amount)        steps_[i].degree += (r.below (3) - 1);          // ±1 scale/chord step
        }
    }

    // ── held notes ──────────────────────────────────────────────────────────────
    void noteOn (int note, int vel) noexcept
    {
        lastVel_ = arpClampi (vel, 1, 127);
        if (latchEnabled_) { if (! latchActive_ || held_.count == 0) { latched_.count = 0; latchActive_ = true; } addUnique (latched_, note); }
        addUnique (held_, note);
    }
    void noteOff (int note) noexcept { removeNote (held_, note); }

    int releaseAll (SeqEvent* out, int maxOut) noexcept
    {
        int n = 0;
        for (int i = 0; i < openN_ && n < maxOut; ++i) out[n++] = { false, open_[i].note, 0, 0, false, false };
        openN_ = 0; pendN_ = 0; held_.count = 0; latched_.count = 0; latchActive_ = false; activeN_ = 0;
        return n;
    }

    // ── process ─────────────────────────────────────────────────────────────────
    int process (float rate, float gate, float vary, float traj, float morph,
                 double hostPpq, double bpm, double sampleRate, int numSamples,
                 bool playing, SeqEvent* out, int maxOut) noexcept
    {
        int n = 0;
        const double SR  = (sampleRate > 0.0 ? sampleRate : 44100.0);
        const double BP  = (bpm > 0.0 ? bpm : 120.0);
        const double pps = (BP / 60.0) / SR;
        const double blockBeats = pps * (double) numSamples;
        const double pos    = playing ? hostPpq : freePpq_;
        const double ppqEnd = pos + blockBeats;
        freePpq_ = (playing ? hostPpq : freePpq_) + blockBeats;

        const float  beats = arpBeatsPerStep (rate);
        const float  gFrac = arpGateFrac (gate);
        const SeqDir dir   = seqDir (traj);

        // mod lanes advance on their own grids (block-rate sampling + slew)
        updateModLanes (pos, beats);

        const NoteSet& src = (latchEnabled_ && latched_.count > 0) ? latched_ : held_;
        if (src.count <= 0)
        {
            for (int i = 0; i < openN_ && n < maxOut; ++i) out[n++] = { false, open_[i].note, 0, 0, false, false };
            openN_ = 0; pendN_ = 0;
            return n;
        }

        int chord[kArpMaxHeld]; const int cN = arpClampi (src.count, 1, kArpMaxHeld);
        for (int i = 0; i < cN; ++i) chord[i] = src.n[i];
        arpSortAsc (chord, cN);

        // 1) detect step starts in this block; evaluate each once; schedule its atoms (absolute ppq)
        SeqHit hits[kSeqMaxEvents];
        const int hitN = seqStepsInBlock (pos, BP, SR, numSamples, beats, morph, hits, kSeqMaxEvents);
        for (int h = 0; h < hitN; ++h)
            scheduleStep (hits[h], dir, chord, cN, beats, gFrac, vary);

        // 2) emit pending atoms whose time falls inside [pos, ppqEnd), in ascending time order
        emitPending (pos, ppqEnd, pps, numSamples, out, n, maxOut);

        // 3) trailing: close notes whose gate ended in-block
        for (int i = 0; i < openN_; )
        {
            int off = (int) std::llround ((open_[i].offPpq - pos) / pps);
            if (open_[i].offPpq < ppqEnd && off >= 0 && n < maxOut)
            {
                if (off >= numSamples) off = numSamples - 1;
                out[n++] = { false, open_[i].note, 0, off, false, false };
                open_[i] = open_[--openN_];
            }
            else ++i;
        }
        return n;
    }

    // default starting pattern — 16 active steps walking up the chord
    void setDefaultPattern() noexcept
    {
        length_ = 16;
        for (int i = 0; i < kSeqMaxSteps; ++i) { SeqStep s; s.on = (i < 16); s.degree = i; s.gate = 0.5f; s.vel = 0.8f; steps_[i] = s; }
        for (auto& L : modLanes_) { L = SeqModLane{}; }
    }

private:
    // ── note group emission (close-before-open unless legato) ───────────────────
    // ── note atoms (scheduled at absolute ppq, emitted in the block they land in) ──
    struct Atom { double t; double offPpq; int note; int vel; bool legato; bool accent; bool closePrev; };
    struct OpenNote { int note; double offPpq; };

    void pushAtom (double t, double offPpq, int note, int vel, bool legato, bool accent, bool closePrev) noexcept
    {
        if (pendN_ < kSeqMaxEvents) pend_[pendN_++] = { t, offPpq, note, vel, legato, accent, closePrev };
    }

    // evaluate a step once and schedule all its atoms (ratchet × play-mode × strum)
    void scheduleStep (const SeqHit& hit, SeqDir dir, const int* chord, int cN,
                       float beats, float gFrac, float vary) noexcept
    {
        const long long S = hit.stepIndex;
        const int slot = patternPos (dir, S, length_);
        const SeqStep& st = steps_[slot];
        const long long loop = (length_ > 0) ? S / length_ : 0;

        bool fire = st.on && evalCondition (st, slot, loop, vary);
        prevCond_ = fire;                                  // for Pre/!Pre
        if (! fire) return;

        for (int L = 0; L < st.numLocks; ++L) writeLock (st.locks[L]);

        int playDeg = st.degree;
        if (st.reach > 0) playDeg += rollOffset (slot, st.reach);

        const int    rat      = arpClampi (st.ratchet, 1, kSeqMaxRatchet);
        const double subBeats = (double) beats / (double) rat;
        const double subGate  = subBeats * (double) (arpClamp01 (st.gate) * gFrac);   // gate length per sub-strike
        const bool   legato   = st.slide || st.tie;
        const int    accentVel= st.accent ? 127 : 0;

        // micro-timing (±half step) + optional timing humanize, as a ppq shift on the step start
        double t0 = hit.ppq + (double) arpClampi ((int) std::lround (st.micro * 100.f), -50, 50) * 0.01 * (double) beats;
        if (humanT_ > 0.f) t0 += (rng_.unit() - 0.5) * 2.0 * (double) humanT_ * (double) beats * 0.1;

        for (int r = 0; r < rat; ++r)
        {
            const double tr  = t0 + subBeats * (double) r;
            const double off = tr + subGate;
            const float  ramp = (rat > 1) ? (1.0f + st.ratVelRamp * ((float) r / (float) (rat - 1) - 0.5f) * 2.0f) : 1.0f;
            const int    vel  = arpClampi ((int) std::lround (velFor (st, accentVel) * ramp), 1, 127);

            if (st.play == StepPlay::Single)
            {
                int deg = playDeg;
                if (st.ratMode == RatchetMode::ChordUp)   deg = playDeg + r;   // walk up chord tones
                if (st.ratMode == RatchetMode::ChordDown) deg = playDeg - r;   // walk down chord tones
                pushAtom (tr, off, pitchFor (chord, cN, deg, st.octave), vel, legato, st.accent, /*closePrev*/ true);
            }
            else // Chord (block) / Strum: stamp the held chord in the chosen voicing
            {
                const double strumSpread = (st.play == StepPlay::Strum) ? (double) arpClamp01 (st.strum) * 0.20 * (double) beats : 0.0;
                for (int v = 0; v < cN; ++v)
                {
                    const int voiced = voiceShift (v, cN, st.voicing);
                    const int note   = arpClampi (chord[v] + 12 * (st.octave + voiced), 0, 127);
                    pushAtom (tr + strumSpread * (double) v, off, note, vel, legato, st.accent, /*closePrev*/ v == 0);
                }
            }
        }
    }

    // emit every pending atom whose time is in [pos, ppqEnd), ascending; close-before-open per strike
    void emitPending (double pos, double ppqEnd, double pps, int numSamples, SeqEvent* out, int& n, int maxOut) noexcept
    {
        while (n < maxOut)
        {
            int best = -1; double bestT = ppqEnd; bool bestClose = false;
            for (int i = 0; i < pendN_; ++i)
            {
                const double ti = pend_[i].t;
                if (ti >= ppqEnd) continue;
                if (best < 0 || ti < bestT - 1e-9 || (std::fabs (ti - bestT) < 1e-9 && pend_[i].closePrev && ! bestClose))
                { best = i; bestT = ti; bestClose = pend_[i].closePrev; }
            }
            if (best < 0) break;
            const Atom a = pend_[best];
            pend_[best] = pend_[--pendN_];                  // remove (order among remaining irrelevant; we re-scan)

            int off = (int) std::llround ((a.t - pos) / pps);
            if (off < 0) off = 0;
            if (off >= numSamples) off = numSamples - 1;

            if (a.closePrev && ! a.legato)                  // re-attack: close the previous strike
            {
                for (int i = 0; i < openN_ && n < maxOut; ++i) out[n++] = { false, open_[i].note, 0, off, false, false };
                openN_ = 0;
            }
            if (n < maxOut)
            {
                out[n++] = { true, a.note, a.vel, off, a.legato, a.accent };
                if (openN_ < kSeqMaxOpen) open_[openN_++] = { a.note, a.offPpq };
            }
        }
    }

    // ── condition evaluation ────────────────────────────────────────────────────
    bool evalCondition (const SeqStep& st, int slot, long long loop, float vary) noexcept
    {
        switch (st.cond.type)
        {
            case SeqCond::Always:   return true;
            case SeqCond::Prob:     return rollTrig (slot, lerpProb (st.cond.prob, vary));
            case SeqCond::Ratio:  { const int b = st.cond.b < 1 ? 1 : st.cond.b; const int a = arpClampi (st.cond.a, 1, b); return (int) (loop % b) == (a - 1); }
            case SeqCond::Fill:     return fillActive_;
            case SeqCond::NotFill:  return ! fillActive_;
            case SeqCond::First:    return loop == 0;
            case SeqCond::NotFirst: return loop != 0;
            case SeqCond::Pre:      return prevCond_;
            case SeqCond::NotPre:   return ! prevCond_;
        }
        return true;
    }
    static float lerpProb (float prob, float vary) noexcept { return 1.0f - arpClamp01 (vary) * (1.0f - arpClamp01 (prob)); }

    // déjà-vu rhythm: lock the trig decision per slot when dejavuR_ is high
    bool rollTrig (int slot, float chance) noexcept
    {
        if (trigValid_[slot] && rng_.unit() < dejavuR_) return trigCache_[slot];
        const bool f = rng_.unit() < chance;
        trigCache_[slot] = f; trigValid_[slot] = true; return f;
    }
    // déjà-vu pitch: lock the chord-tone offset per slot when dejavuP_ is high
    int rollOffset (int slot, int reach) noexcept
    {
        if (rollValid_[slot] && rng_.unit() < dejavuP_) return rollCache_[slot];
        const int off = seqWeightedOffset (reach, spread_, bias_, rng_);
        rollCache_[slot] = off; rollValid_[slot] = true; return off;
    }

    // ── pitch resolution (chord-degree or scale-degree) + voicing ───────────────
    int pitchFor (const int* chord, int cN, int degree, int octave) const noexcept
    {
        if (useScale_) return arpClampi (seqScaleNote (scaleRoot_, scaleMask_, degree) + 12 * octave, 0, 127);
        const int idx = ((degree % cN) + cN) % cN;
        const int oc  = octave + (int) std::floor ((double) degree / (double) cN);
        return arpClampi (chord[idx] + 12 * oc, 0, 127);
    }
    // inversion/voicing: octave-shift the idx-th chord tone (drop/spread style by index)
    static int voiceShift (int idx, int cN, int voicing) noexcept
    {
        if (voicing == 0 || cN <= 0) return 0;
        // simple inversions: raise the lowest |voicing| notes by an octave
        const int inv = ((voicing % cN) + cN) % cN;
        return (idx < inv) ? 1 : 0;
    }

    int velFor (const SeqStep& st, int accentVel) const noexcept
    {
        float base = arpClamp01 (st.vel) * (float) lastVel_;
        if (accentVel > 0) base = base * 0.6f + (float) accentVel * 0.4f;       // accent lifts toward 127
        if (humanV_ > 0.f) base += (rng_.unit() - 0.5f) * 2.f * humanV_ * 30.f; // ± up to ~30
        return arpClampi ((int) std::lround (base), 1, 127);
    }

    // ── direction with state for random/brownian/shuffle ────────────────────────
    int patternPos (SeqDir dir, long long S, int len) noexcept
    {
        if (len <= 1) return 0;
        switch (dir)
        {
            case SeqDir::Random:     return rng_.below (len);
            case SeqDir::RandomSkip: return (rng_.unit() < 0.5f) ? (int) (S % len) : rng_.below (len);
            case SeqDir::Brownian:   { int d = rng_.below (3) - 1; brownPos_ = arpClampi (brownPos_ + d, 0, len - 1); return brownPos_; }
            case SeqDir::Shuffle:    { ensureShuffle (len); return shuf_[(int) (S % len)]; }
            default:                 return seqDeterministicPos (dir, S, len);
        }
    }
    void ensureShuffle (int len) noexcept
    {
        if (shufBuilt_ && shufLen_ == len) return;
        for (int i = 0; i < len; ++i) shuf_[i] = i;
        for (int i = len - 1; i > 0; --i) { int j = rng_.below (i + 1); int t = shuf_[i]; shuf_[i] = shuf_[j]; shuf_[j] = t; }
        shufBuilt_ = true; shufLen_ = len;
    }

    // ── mod lanes ───────────────────────────────────────────────────────────────
    void updateModLanes (double pos, float beats) noexcept
    {
        for (auto& L : modLanes_)
        {
            const double laneSpacing = (double) beats / (double) (L.rate <= 0 ? 1.f : L.rate);
            const int slot = (int) (((long long) std::floor (pos / laneSpacing)) % L.length + L.length) % L.length;
            const float target = L.val[slot];
            const float alpha = 1.0f - arpClamp01 (L.smooth) * 0.98f;   // smooth=0 → instant; →1 → slow
            L.current += (target - L.current) * alpha;
        }
    }

    // ── parameter locks (hold-until-next-lock) ──────────────────────────────────
    void writeLock (const StepLock& lk) noexcept
    {
        if (lk.paramId < 0) return;
        for (int i = 0; i < activeN_; ++i) if (active_[i].paramId == lk.paramId) { active_[i].value = lk.value; return; }
        if (activeN_ < kSeqMaxActiveLocks) active_[activeN_++] = lk;
    }

    static void addUnique (NoteSet& s, int note) noexcept { for (int i = 0; i < s.count; ++i) if (s.n[i] == note) return; if (s.count < kArpMaxSeq) s.n[s.count++] = note; }
    static void removeNote (NoteSet& s, int note) noexcept { for (int i = 0; i < s.count; ++i) if (s.n[i] == note) { for (int j = i; j < s.count - 1; ++j) s.n[j] = s.n[j + 1]; --s.count; return; } }

    // ── state ───────────────────────────────────────────────────────────────────
    SeqStep    steps_[kSeqMaxSteps];
    int        length_ = 16;
    SeqModLane modLanes_[kSeqModLanes];

    int        scaleRoot_ = 60; uint16_t scaleMask_ = 0x0FFF; bool useScale_ = false;
    float      dejavuR_ = 1.f, dejavuP_ = 1.f, spread_ = 0.5f, bias_ = 0.5f, humanT_ = 0.f, humanV_ = 0.f;
    bool       fillActive_ = false;

    NoteSet    held_, latched_;
    OpenNote   open_[kSeqMaxOpen]; int openN_ = 0;
    Atom       pend_[kSeqMaxEvents]; int pendN_ = 0;
    double     freePpq_ = 0.0;
    bool       latchEnabled_ = false, latchActive_ = false;
    int        lastVel_ = 100;
    bool       prevCond_ = false;

    int        rollCache_[kSeqMaxSteps]; bool rollValid_[kSeqMaxSteps];
    bool       trigCache_[kSeqMaxSteps]; bool trigValid_[kSeqMaxSteps];

    int        brownPos_ = 0;
    int        shuf_[kSeqMaxSteps]; bool shufBuilt_ = false; int shufLen_ = 0;

    StepLock   active_[kSeqMaxActiveLocks]; int activeN_ = 0;

    mutable ArpRng rng_;
};

} // namespace wc
