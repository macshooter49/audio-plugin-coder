#pragma once
// =============================================================================
//  FlowArp.h  —  Terrain Instrument · FLOW mode 1 (ARP) engine
//  Waves Crate
//
//  Header-only, NO JUCE. Shared by the processor (audio) and the offline test,
//  so the unit test and the audible path run IDENTICAL math (same discipline as
//  SynthModConfig.h / ModCore_test.cpp).
//
//  FLOW sits between incoming MIDI and the synth voices: it consumes held notes
//  + host transport and EMITS note-on/off events (with sample offsets) that the
//  processor feeds to the JUCE Synthesiser. This file is the ARP "math":
//
//   FRONT-PANEL PERFORMANCE LAYER — five knobs, each normalized [0,1] and passed
//   in as the EFFECTIVE value (base + LFO modulation already summed by the caller,
//   so 10-LFO modulation of any knob is a clean add — base->effective pattern):
//     RATE  -> step division (1/1 … 1/32)                     [arpBeatsPerStep]
//     GATE  -> note length as a fraction of the step          [arpGateFrac]
//     VARY  -> order-shuffle + octave-jump probability         [applied per step]
//     TRAJ  -> direction + octave range (ordered preset ramp)  [arpTraj]
//     MORPH -> chord voicing / inversion spread                [arpVoicing]
//
//  RT-safe: fixed-size buffers, no allocation, no locks on the audio path.
//  Timing is re-derived from ppqPosition every block (NO accumulating sample
//  counter) so it never drifts and stays bar-anchored across hosts.
//
//  Build test:  g++ -std=c++17 Source/FlowArp_test.cpp -o /tmp/flowarp && /tmp/flowarp
// =============================================================================

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace wc
{

// ── capacities (RT-safe upper bounds) ──────────────────────────────────────
static constexpr int kArpMaxHeld    = 16;   // max simultaneously held notes
static constexpr int kArpMaxOct     = 4;    // max octave range
static constexpr int kArpMaxSeq     = kArpMaxHeld * kArpMaxOct;  // 64
static constexpr int kArpMaxPending = 64;   // pending note-offs in flight
static constexpr int kArpMaxEvents  = 96;   // events emitted per block

// ── small helpers (local; header is self-contained) ─────────────────────────
inline float  arpClamp01 (float v) noexcept { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline int    arpClampi  (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline int    arpQuantIdx(float norm, int n) noexcept     { return arpClampi((int) std::lround (arpClamp01(norm) * (float)(n - 1)), 0, n - 1); }

// ── RATE: knob -> beats-per-step (quarter-note units). Quantized, musical. ───
//    Straight ladder 1/1 … 1/256. The SEQ engine uses THIS via arpBeatsPerStep
//    (kept stable so FlowSeq + its proof are untouched). The ARP front knob uses
//    the RICH ladder below (straight + dotted + triplet).
static constexpr float kArpRate[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 0.015625f };
static constexpr int   kArpRateN  = (int) (sizeof (kArpRate) / sizeof (float));
inline float arpBeatsPerStep (float rateKnob) noexcept { return kArpRate[ arpQuantIdx (rateKnob, kArpRateN) ]; }

// ── RATE (RICH): the ARP front-knob ladder — straight + dotted (.) + triplet (T),
//    1/1 … 1/256, ordered slow→fast (descending beats, strictly monotonic so the
//    knob always speeds up as it climbs). Dotted = ×1.5, triplet = ×2/3. The UI
//    readout RATE_DIV_RICH must stay byte-for-byte in lockstep with this list
//    (same order + count) — picture == sound. Top end (1/256 ≈ 128 Hz @120BPM) buzzes.
static constexpr float kArpRateRich[] = {
    4.0f,             // 1/1
    3.0f,             // 1/2.   dotted half
    2.0f,             // 1/2
    1.5f,             // 1/4.   dotted quarter
    2.0f*2.0f/3.0f,   // 1/2T   half triplet    (1.3333)
    1.0f,             // 1/4
    0.75f,            // 1/8.   dotted eighth
    1.0f*2.0f/3.0f,   // 1/4T   quarter triplet (0.6667)
    0.5f,             // 1/8
    0.375f,           // 1/16.  dotted sixteenth
    0.5f*2.0f/3.0f,   // 1/8T   eighth triplet  (0.3333)
    0.25f,            // 1/16
    0.1875f,          // 1/32.  dotted 32nd
    0.25f*2.0f/3.0f,  // 1/16T  sixteenth triplet (0.16667)
    0.125f,           // 1/32
    0.125f*2.0f/3.0f, // 1/32T  32nd triplet    (0.08333)
    0.0625f,          // 1/64
    0.03125f,         // 1/128
    0.015625f         // 1/256
};
static constexpr int   kArpRateRichN  = (int) (sizeof (kArpRateRich) / sizeof (float));
inline float arpBeatsPerStepRich (float rateKnob) noexcept { return kArpRateRich[ arpQuantIdx (rateKnob, kArpRateRichN) ]; }

// ── GATE: knob -> note length as fraction of the step (5%..98%). ─────────────
inline float arpGateFrac (float gateKnob) noexcept { return 0.05f + 0.93f * arpClamp01 (gateKnob); }

// ── TRAJ: direction + octave range as one ordered "reach" ramp. ──────────────
enum class ArpDir : int { Up = 0, Down, UpDown, Converge, Random };
struct TrajPreset { ArpDir dir; int octaves; };
static constexpr TrajPreset kArpTraj[] = {
    { ArpDir::Up,       1 },   // 0.00  tight, rising
    { ArpDir::Down,     1 },
    { ArpDir::UpDown,   1 },
    { ArpDir::Up,       2 },
    { ArpDir::Down,     2 },
    { ArpDir::UpDown,   2 },
    { ArpDir::Converge, 2 },
    { ArpDir::Up,       3 },
    { ArpDir::Down,     3 },
    { ArpDir::UpDown,   3 },
    { ArpDir::Converge, 3 },
    { ArpDir::Random,   4 },   // 1.00  wide, chaotic
};
static constexpr int kArpTrajN = (int) (sizeof (kArpTraj) / sizeof (TrajPreset));
inline TrajPreset arpTraj (float trajKnob) noexcept { return kArpTraj[ arpQuantIdx (trajKnob, kArpTrajN) ]; }

// ── tiny deterministic RNG (xorshift32) so VARY is reproducible in the test ──
struct ArpRng
{
    uint32_t s = 0x9E3779B9u;
    inline void  seed (uint32_t v) noexcept { s = v ? v : 0x9E3779B9u; }
    inline uint32_t next () noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float unit () noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    inline int   below (int n) noexcept { return n > 0 ? (int) (next() % (uint32_t) n) : 0; }
};

// ── MORPH: voicing/inversion SPREAD. note i gets +round(morph*i) octaves. ────
//    morph=0 -> chord as held;  morph=1 -> fully spread (each note an octave higher
//    than the previous). Monotonic widening of the voicing. Writes a sorted set.
struct NoteSet { int n[kArpMaxSeq]; int count = 0; };

inline void arpSortAsc (int* a, int count) noexcept
{
    for (int i = 1; i < count; ++i) { int k = a[i], j = i - 1; while (j >= 0 && a[j] > k) { a[j+1] = a[j]; --j; } a[j+1] = k; }
}

inline void arpVoicing (const int* held, int heldCount, float morphKnob, NoteSet& out) noexcept
{
    int tmp[kArpMaxHeld]; int c = arpClampi (heldCount, 0, kArpMaxHeld);
    for (int i = 0; i < c; ++i) tmp[i] = held[i];
    arpSortAsc (tmp, c);
    const float m = arpClamp01 (morphKnob);
    out.count = c;
    for (int i = 0; i < c; ++i)
        out.n[i] = tmp[i] + 12 * (int) std::lround (m * (float) i);
    arpSortAsc (out.n, out.count);
}

// ── build the play sequence from a voiced set + direction + octave range ─────
inline void arpSequence (const NoteSet& voiced, ArpDir dir, int octaves, NoteSet& seq) noexcept
{
    seq.count = 0;
    if (voiced.count <= 0) return;
    octaves = arpClampi (octaves, 1, kArpMaxOct);

    // expand across octaves, ascending
    int ex[kArpMaxSeq]; int exN = 0;
    for (int o = 0; o < octaves && exN < kArpMaxSeq; ++o)
        for (int i = 0; i < voiced.count && exN < kArpMaxSeq; ++i)
            ex[exN++] = voiced.n[i] + 12 * o;
    arpSortAsc (ex, exN);

    switch (dir)
    {
        case ArpDir::Up:
            for (int i = 0; i < exN; ++i) seq.n[seq.count++] = ex[i];
            break;
        case ArpDir::Down:
            for (int i = exN - 1; i >= 0; --i) seq.n[seq.count++] = ex[i];
            break;
        case ArpDir::UpDown: // ascend then descend, no endpoint repeat
            for (int i = 0; i < exN; ++i) seq.n[seq.count++] = ex[i];
            for (int i = exN - 2; i >= 1 && seq.count < kArpMaxSeq; --i) seq.n[seq.count++] = ex[i];
            break;
        case ArpDir::Converge: { // outside-in: lo, hi, lo+1, hi-1 …
            int lo = 0, hi = exN - 1; bool low = true;
            while (lo <= hi && seq.count < kArpMaxSeq) { seq.n[seq.count++] = low ? ex[lo++] : ex[hi--]; low = !low; }
            break;
        }
        case ArpDir::Random: // structure is ascending; per-step RNG reorders at play time
            for (int i = 0; i < exN; ++i) seq.n[seq.count++] = ex[i];
            break;
    }
    if (seq.count <= 0) seq.n[seq.count++] = ex[0];
}

// ── per-step note pick: applies direction + VARY (shuffle + octave jump). ────
//    stepIndex is the ABSOLUTE step counter (from ppq=0) so patterns are
//    bar-anchored. Returns a MIDI note (clamped 0..127).
inline int arpPickNote (const NoteSet& seq, ArpDir dir, long long stepIndex,
                        float varyKnob, int octaveRange, ArpRng& rng) noexcept
{
    if (seq.count <= 0) return -1;
    const float vary = arpClamp01 (varyKnob);

    int idx;
    if (dir == ArpDir::Random)              idx = rng.below (seq.count);
    else                                    idx = (int) (((stepIndex % seq.count) + seq.count) % seq.count);

    // VARY: order shuffle — with probability `vary`, jump to a random seat
    if (vary > 0.0f && dir != ArpDir::Random && rng.unit() < vary)
        idx = rng.below (seq.count);

    int note = seq.n[idx];

    // VARY: octave jump — with probability vary*0.6 add a random octave within range+1
    if (vary > 0.0f && rng.unit() < vary * 0.6f)
    {
        int span = arpClampi (octaveRange, 1, kArpMaxOct);
        note += 12 * rng.below (span + 1);
    }
    return arpClampi (note, 0, 127);
}

// ── PPQ step boundaries inside this block (drift-free, bar-anchored). ────────
//    Re-derives every boundary from ppqStart; never accumulates a sample count.
struct StepHit { int sampleOffset; long long stepIndex; double ppq; };
inline int arpStepsInBlock (double ppqStart, double bpm, double sampleRate, int numSamples,
                            float beatsPerStep, StepHit* out, int maxOut) noexcept
{
    if (beatsPerStep <= 0.0 || bpm <= 0.0 || sampleRate <= 0.0 || numSamples <= 0) return 0;
    const double bps         = bpm / 60.0;            // beats / second
    const double ppqPerSample = bps / sampleRate;     // beats advanced per sample
    const double ppqEnd      = ppqStart + ppqPerSample * (double) numSamples;

    long long idx = (long long) std::ceil (ppqStart / beatsPerStep - 1e-9);
    int count = 0;
    for (; count < maxOut; ++idx)
    {
        const double stepPpq = (double) idx * (double) beatsPerStep;
        if (stepPpq >= ppqEnd - 1e-12) break;
        int off = (int) std::llround ((stepPpq - ppqStart) / ppqPerSample);
        if (off < 0) off = 0;
        if (off >= numSamples) continue;
        out[count++] = { off, idx, stepPpq };
    }
    return count;
}

// ── one emitted event the processor turns into a juce::MidiMessage ───────────
struct ArpEvent { bool on; int note; int vel; int sampleOffset; };

// =============================================================================
//  EXTENSION-CARD LANE LAYER (fb105) — per-step pattern data + depth params.
//
//  All per-step randomness comes from a STATELESS HASH of the absolute step
//  index (never a mutable RNG): decisions are identical regardless of block
//  size or where block boundaries fall, patterns are loop-locked (same seed →
//  same gaps every bar), and everything is provable in the offline test.
// =============================================================================
static constexpr int kArpLaneMax = 32;   // engine capacity; the card sends 16

inline uint32_t arpHash (uint32_t a, uint32_t b, uint32_t c) noexcept
{
    uint32_t h = a * 0x9E3779B9u; h ^= (b + 0x7F4A7C15u) * 0x85EBCA6Bu; h ^= (c + 0x165667B1u) * 0xC2B2AE35u;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}
inline float arpHash01 (uint32_t a, uint32_t b, uint32_t c) noexcept
{ return (float) (arpHash (a, b, c) >> 8) * (1.0f / 16777216.0f); }

inline float arpLerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
// curve knob → exponent: 0.5 = 1.0 (neutral); range ~0.35 … 2.83
inline float arpCurveExp (float c) noexcept { return std::pow (2.0f, (arpClamp01 (c) - 0.5f) * 3.0f); }
inline float arpShape (float v, float curve) noexcept { return std::pow (arpClamp01 (v), arpCurveExp (curve)); }

// per-step pattern arrays — mirrors the card's P bag (pitch row 0..6 / −1 rest,
// gate .05..1, vel 0..1, oct −1/0/+1, ratchet 1..4, prob 0..1, wt 0..1).
// Defaults = the card's seeded constellation, so DSP and picture agree before
// the card is ever opened (picture == sound).
struct ArpLaneData
{
    int   steps = 16;
    float pitch[kArpLaneMax], gate[kArpLaneMax], vel[kArpLaneMax], oct[kArpLaneMax];
    float ratchet[kArpLaneMax], prob[kArpLaneMax], wt[kArpLaneMax];

    ArpLaneData() noexcept
    {
        static constexpr float seed[16] = { 0,1,3,3,5,3,1,1,0,3,3,6,5,5,1,0 };
        for (int i = 0; i < kArpLaneMax; ++i)
        {
            pitch[i]   = seed[i % 16];
            gate[i]    = 0.72f; vel[i] = 0.8f; oct[i] = 0.0f;
            ratchet[i] = 1.0f;  prob[i] = 1.0f; wt[i] = 0.5f;
        }
    }
};

// the card's scalar controls — every knob/stepper/toggle, normalized [0,1]
// unless noted. Defaults match the shipped card defaults.
struct ArpExtParams
{
    int   dir = 0;          // 0 Up · 1 Down · 2 Up-Dn · 3 Random  (card Direction)
    int   octaves = 2;      // 1..4                                 (card Octaves)
    bool  sorted = true;    // true = low→high, false = as-played   (card Sorted)
    float swing = 0.12f, mroll = 0.25f, timbre = 0.6f, glide = 0.15f;   // MOTION
    // PITCH: row span (semis), contour bend, snap-to-chord chance, slide chance
    float pRange = 0.5f, pCurve = 0.5f, pQuant = 0.6f, pSlide = 0.2f;
    // GATE: master length, contrast, humanize, tie chance
    float gLen = 0.65f, gCurve = 0.4f, gRand = 0.0f, gSlide = 0.15f;
    // VEL: lane depth, contrast, humanize, floor
    float vRange = 0.7f, vCurve = 0.5f, vRand = 0.12f, vFloor = 0.2f;
    // OCT: lane reach (×4 oct), constant bias (±4), random jump, alt-step zigzag
    float oRange = 0.25f, oBias = 0.5f, oRand = 0.0f, oSpread = 0.0f;
    // ROLL: global roll count, per-hit decay, spacing curve, programmed-roll odds
    float rCount = 0.33f, rDecay = 0.4f, rCurve = 0.3f, rAmt = 0.5f;
    // CHANCE: lane depth, bias, dice seed (0..16), per-loop mutation
    float cAmt = 0.8f, cBias = 0.5f, cSeed = 0.44f, cDrift = 0.0f;
    // WAVE: send depth, contrast, slew, humanize
    float wDepth = 0.6f, wCurve = 0.45f, wSlide = 0.25f, wRand = 0.1f;
};

// =============================================================================
//  FlowArp — monophonic arp state machine wrapping the pure helpers above.
//
//  ONE note sounds at a time (an arpeggiator plays a chord's notes in sequence).
//  Two correctness rules that the first version got wrong:
//
//   • CLOCK: runs on an INTERNAL free-running clock when the host transport is
//     stopped, and locks to host PPQ when it's playing — handed off seamlessly
//     (the free clock tracks host+block while playing, so STOP continues from the
//     exact position). So holding keys makes sound even with transport stopped.
//
//   • NOTE-OFFS CAN NEVER STRAND: the currently-sounding note is closed right
//     before the next note-on (legato-cut, never overlapping) and at its gate
//     time; releasing all keys closes it immediately. No PPQ-keyed pending queue
//     that a transport-stop/loop could freeze before it fires.
//
//  knobs: rate,gate,vary,traj,morph normalized [0,1] (EFFECTIVE values).
// =============================================================================
class FlowArp
{
public:
    void reset() noexcept
    {
        held_.count = 0; latched_.count = 0; latchActive_ = false;
        curNote_ = -1; curOffPpq_ = 0.0; freePpq_ = 0.0; lastVel_ = 100; rng_.seed (0x12345678u);
        actN_ = 0; lastKey_ = (long long) -0x7FFFFFFFFFLL; lastPos_ = 0.0;
        waveCur_ = waveTarget_ = 0.5f; vizStepF_ = 0.0f; vizNote_ = -1; vizVel_ = 0;
        vizCount_ = 0; vizActive_ = false;              // ext_/lanes_/extOn_ persist (config, not runtime)
    }

    void setLatch (bool on) noexcept
    {
        latchEnabled_ = on;
        if (! on) { latched_.count = 0; latchActive_ = false; }
    }

    void noteOn (int note, int vel) noexcept
    {
        lastVel_ = arpClampi (vel, 1, 127);
        if (latchEnabled_)
        {
            if (! latchActive_ || held_.count == 0) { latched_.count = 0; latchActive_ = true; }
            addUnique (latched_, note);
        }
        addUnique (held_, note);
    }

    void noteOff (int note) noexcept { removeNote (held_, note); }  // latched copy persists

    // Release everything and clear state — call when FLOW switches OFF (or to a mode that
    // isn't ARP) so the synth never hangs on the last arp note. Emits the pending note-off(s).
    int releaseAll (ArpEvent* out, int maxOut) noexcept
    {
        int n = 0;
        if (curNote_ >= 0 && n < maxOut) { out[n++] = { false, curNote_, 0, 0 }; curNote_ = -1; }
        for (int a = 0; a < actN_ && n < maxOut; ++a) out[n++] = { false, act_[a].note, 0, 0 };
        actN_ = 0;
        held_.count = 0; latched_.count = 0; latchActive_ = false;
        return n;
    }

    // ── extension-card layer (fb105) ────────────────────────────────────────
    // setExt() flips the engine into lane mode; the classic 5-knob path stays
    // bit-exact until the first call (the offline test's old proofs untouched).
    void setExt   (const ArpExtParams& x) noexcept { ext_ = x; extOn_ = true; }
    void setLanes (const ArpLaneData& l)  noexcept { lanes_ = l; }

    // viz read-back (audio thread writes, processor copies to atomics after process)
    float     vizStepF()     const noexcept { return vizStepF_; }   // playhead: lane step + frac
    int       vizNote()      const noexcept { return vizNote_; }
    int       vizVel()       const noexcept { return vizVel_; }
    uint32_t  vizFireCount() const noexcept { return vizCount_; }
    bool      vizActive()    const noexcept { return vizActive_; }
    // wave lane output, bipolar ±0.5 max, already scaled by Depth × Timbre
    float     waveMod()      const noexcept
    { return (waveCur_ - 0.5f) * arpClamp01 (ext_.wDepth) * arpClamp01 (ext_.timbre); }

    // Returns number of events written to `out` (capacity maxOut).
    int process (float rate, float gate, float vary, float traj, float morph,
                 double hostPpq, double bpm, double sampleRate, int numSamples,
                 bool playing, ArpEvent* out, int maxOut) noexcept
    {
        if (extOn_)
            return processExt (rate, gate, vary, traj, morph, hostPpq, bpm,
                               sampleRate, numSamples, playing, out, maxOut);
        return processClassic (rate, gate, vary, traj, morph, hostPpq, bpm,
                               sampleRate, numSamples, playing, out, maxOut);
    }

private:
    // the pre-fb105 5-knob path, kept BIT-EXACT (the offline test's old proofs
    // run against this; production always runs processExt via setExt).
    int processClassic (float rate, float gate, float vary, float traj, float morph,
                        double hostPpq, double bpm, double sampleRate, int numSamples,
                        bool playing, ArpEvent* out, int maxOut) noexcept
    {
        int n = 0;
        const double sr  = (sampleRate > 0.0 ? sampleRate : 44100.0);
        const double bp  = (bpm > 0.0 ? bpm : 120.0);
        const double ppqPerSample = (bp / 60.0) / sr;
        const double blockBeats   = ppqPerSample * (double) numSamples;

        // clock: host when playing, internal free-run when stopped — seamless handoff.
        const double pos    = playing ? hostPpq : freePpq_;
        const double ppqEnd = pos + blockBeats;
        freePpq_ = (playing ? hostPpq : freePpq_) + blockBeats;

        const NoteSet& src = (latchEnabled_ && latched_.count > 0) ? latched_ : held_;

        // nothing held -> release the sounding note, silence
        if (src.count <= 0)
        {
            if (curNote_ >= 0 && n < maxOut) { out[n++] = { false, curNote_, 0, 0 }; curNote_ = -1; }
            return n;
        }

        const TrajPreset tp = arpTraj (traj);
        NoteSet voiced; arpVoicing (src.n, src.count, morph, voiced);
        NoteSet seq;    arpSequence (voiced, tp.dir, tp.octaves, seq);
        if (seq.count <= 0)
        {
            if (curNote_ >= 0 && n < maxOut) { out[n++] = { false, curNote_, 0, 0 }; curNote_ = -1; }
            return n;
        }

        const float beats = arpBeatsPerStepRich (rate);   // ARP uses the rich straight/dotted/triplet ladder
        const float gFrac = arpGateFrac (gate);

        StepHit hits[kArpMaxEvents];
        const int hitN = arpStepsInBlock (pos, bp, sr, numSamples, beats, hits, kArpMaxEvents);

        for (int h = 0; h < hitN && n + 1 < maxOut; ++h)
        {
            const int onOff = hits[h].sampleOffset;

            // close the current note before this new one (legato-cut, never overlap)
            if (curNote_ >= 0)
            {
                int offOff = (int) std::llround ((curOffPpq_ - pos) / ppqPerSample);
                if (offOff < 0)     offOff = 0;
                if (offOff > onOff)  offOff = onOff;
                out[n++] = { false, curNote_, 0, offOff };
                curNote_ = -1;
            }

            const int note = arpPickNote (seq, tp.dir, hits[h].stepIndex, vary, tp.octaves, rng_);
            if (note < 0) continue;
            out[n++] = { true, note, lastVel_, onOff };
            curNote_   = note;
            curOffPpq_ = hits[h].ppq + (double) beats * (double) gFrac;
        }

        // gate-off for the current note if it ends within this block (creates the gap)
        if (curNote_ >= 0)
        {
            int offOff = (int) std::llround ((curOffPpq_ - pos) / ppqPerSample);
            if (curOffPpq_ < ppqEnd && offOff >= 0 && offOff < numSamples && n < maxOut)
            {
                out[n++] = { false, curNote_, 0, offOff };
                curNote_ = -1;
            }
        }
        return n;
    }

    // ── as-played-aware voicing/sequence (sorted=true reproduces the classics) ──
    static void buildVoicing (const NoteSet& src, float morph, bool sorted, NoteSet& out) noexcept
    {
        int tmp[kArpMaxHeld]; const int c = arpClampi (src.count, 0, kArpMaxHeld);
        for (int i = 0; i < c; ++i) tmp[i] = src.n[i];
        if (sorted) arpSortAsc (tmp, c);
        const float m = arpClamp01 (morph);
        out.count = c;
        for (int i = 0; i < c; ++i) out.n[i] = tmp[i] + 12 * (int) std::lround (m * (float) i);
        if (sorted) arpSortAsc (out.n, out.count);
    }
    static void buildSequence (const NoteSet& voiced, ArpDir dir, int octaves, bool sorted, NoteSet& seq) noexcept
    {
        seq.count = 0;
        if (voiced.count <= 0) return;
        octaves = arpClampi (octaves, 1, kArpMaxOct);
        int ex[kArpMaxSeq]; int exN = 0;
        for (int o = 0; o < octaves && exN < kArpMaxSeq; ++o)
            for (int i = 0; i < voiced.count && exN < kArpMaxSeq; ++i)
                ex[exN++] = voiced.n[i] + 12 * o;
        if (sorted) arpSortAsc (ex, exN);
        switch (dir)
        {
            case ArpDir::Up:     for (int i = 0; i < exN; ++i) seq.n[seq.count++] = ex[i]; break;
            case ArpDir::Down:   for (int i = exN - 1; i >= 0; --i) seq.n[seq.count++] = ex[i]; break;
            case ArpDir::UpDown:
                for (int i = 0; i < exN; ++i) seq.n[seq.count++] = ex[i];
                for (int i = exN - 2; i >= 1 && seq.count < kArpMaxSeq; --i) seq.n[seq.count++] = ex[i];
                break;
            case ArpDir::Converge: {
                int lo = 0, hi = exN - 1; bool low = true;
                while (lo <= hi && seq.count < kArpMaxSeq) { seq.n[seq.count++] = low ? ex[lo++] : ex[hi--]; low = !low; }
                break;
            }
            case ArpDir::Random: for (int i = 0; i < exN; ++i) seq.n[seq.count++] = ex[i]; break;
        }
        if (seq.count <= 0) seq.n[seq.count++] = ex[0];
    }

    // =========================================================================
    //  processExt — the lane engine (fb105). Same clock discipline as classic
    //  (host ppq playing / freePpq_ stopped, boundaries re-derived every block),
    //  but every step runs the 7-lane pipeline and every dice roll is a
    //  stateless hash of the absolute step → block-size independent, loop-locked.
    // =========================================================================
    int processExt (float rate, float gate, float vary, float traj, float morph,
                    double hostPpq, double bpm, double sampleRate, int numSamples,
                    bool playing, ArpEvent* out, int maxOut) noexcept
    {
        int n = 0;
        const double sr  = (sampleRate > 0.0 ? sampleRate : 44100.0);
        const double bp  = (bpm > 0.0 ? bpm : 120.0);
        const double ppqPerSample = (bp / 60.0) / sr;
        const double blockBeats   = ppqPerSample * (double) numSamples;
        const double pos    = playing ? hostPpq : freePpq_;
        const double ppqEnd = pos + blockBeats;
        freePpq_ = (playing ? hostPpq : freePpq_) + blockBeats;

        const float  beats   = arpBeatsPerStepRich (rate);
        const double stepSec = (double) beats * 60.0 / bp;
        const int    steps   = arpClampi (lanes_.steps, 1, kArpLaneMax);

        auto emitOff = [&] (int note, int off)
        { if (n < maxOut) out[n++] = { false, note, 0, arpClampi (off, 0, numSamples - 1) }; };
        auto offAt = [&] (double ppq) { return (int) std::llround ((ppq - pos) / ppqPerSample); };

        // classic-path leftover (mode was switched into ext mid-note): close it
        if (curNote_ >= 0) { emitOff (curNote_, 0); curNote_ = -1; }

        // host loop/relocate jumped backwards → re-arm the refire guard
        if (pos < lastPos_ - 0.25) lastKey_ = (long long) -0x7FFFFFFFFFLL;
        lastPos_ = pos;

        // sanity: an off stranded far in the future (host loop jump) closes now
        for (int a = 0; a < actN_; )
        {
            if (act_[a].offPpq > pos + 8.0) { emitOff (act_[a].note, 0); act_[a] = act_[--actN_]; }
            else ++a;
        }

        // time-based playhead for the viz (advances through rests and skips alike)
        {
            double m = std::fmod (ppqEnd / (double) beats, (double) steps);
            if (m < 0) m += steps;
            vizStepF_ = (float) m;
        }

        const NoteSet& src = (latchEnabled_ && latched_.count > 0) ? latched_ : held_;
        vizActive_ = src.count > 0;
        if (src.count <= 0)
        {
            for (int a = 0; a < actN_; ++a) emitOff (act_[a].note, 0);
            actN_ = 0;
            return n;
        }

        // direction/octaves: the TRAJ macro (turned up or LFO-swept) overrides the card
        ArpDir dir; int octaves;
        if (traj > 0.004f) { const TrajPreset tp = arpTraj (traj); dir = tp.dir; octaves = tp.octaves; }
        else
        {
            static constexpr ArpDir dmap[4] = { ArpDir::Up, ArpDir::Down, ArpDir::UpDown, ArpDir::Random };
            dir = dmap[arpClampi (ext_.dir, 0, 3)];
            octaves = arpClampi (ext_.octaves, 1, kArpMaxOct);
        }

        NoteSet voiced; buildVoicing (src, morph, ext_.sorted, voiced);
        NoteSet seq;    buildSequence (voiced, dir, octaves, ext_.sorted, seq);
        if (seq.count <= 0)
        {
            for (int a = 0; a < actN_; ++a) emitOff (act_[a].note, 0);
            actN_ = 0;
            return n;
        }

        const float    gScale = arpGateFrac (gate) * arpLerp (0.35f, 1.6f, ext_.gLen);
        const float    sw     = arpClamp01 (ext_.swing) * 0.45f * beats;   // odd steps land later
        const uint32_t seedI  = (uint32_t) std::lround (arpClamp01 (ext_.cSeed) * 16.0f);
        const int   rollN     = 1 + (int) std::lround (arpClamp01 (ext_.rCount) * 3.0f);
        const int   octMult   = (int) std::lround (arpClamp01 (ext_.oRange) * 4.0f);
        const int   octBias   = (int) std::lround ((arpClamp01 (ext_.oBias) - 0.5f) * 8.0f);
        const int   octZig    = (int) std::lround (arpClamp01 (ext_.oSpread) * 2.0f);
        const float pExp      = arpCurveExp (ext_.pCurve);
        const int   pSemis    = (int) std::lround (arpClamp01 (ext_.pRange) * 12.0f);
        const float rExp      = arpCurveExp (ext_.rCurve);

        // ── scan absolute steps whose (swung) sub-hits land inside this block ──
        for (long long idx = (long long) std::floor (pos / (double) beats) - 1; ; ++idx)
        {
            const double baseT = (double) idx * (double) beats;
            if (baseT >= ppqEnd || n >= maxOut - 2) break;
            const double    stepT = baseT + ((idx & 1) ? (double) sw : 0.0);
            const int       li    = (int) (((idx % steps) + steps) % steps);
            const long long loop  = (long long) std::floor ((double) idx / (double) steps);
            const uint32_t  ui    = (uint32_t) (idx & 0x7FFFFFFF);

            // REST: pitch row −1 = programmed silence (prior note gates off on its own)
            if (lanes_.pitch[li] < -0.5f) continue;

            // CHANCE: loop-locked dice at Drift 0; Drift mutates a fraction per loop
            {
                const float effP = arpClamp01 (arpLerp (1.0f, arpClamp01 (lanes_.prob[li]), arpClamp01 (ext_.cAmt))
                                               + (arpClamp01 (ext_.cBias) - 0.5f));
                float dice = arpHash01 (seedI, (uint32_t) li, 0xC1u);
                if (ext_.cDrift > 0.0f && arpHash01 (seedI ^ 0xD00Du, (uint32_t) li, (uint32_t) loop) < ext_.cDrift)
                    dice = arpHash01 (seedI, (uint32_t) li, 0xC2u + (uint32_t) loop);
                if (dice >= effP) continue;
            }

            // walk the sequence (VARY = hash-dice shuffle + octave jump)
            int pick;
            if (dir == ArpDir::Random) pick = (int) (arpHash (ui, 0xA1u, seedI) % (uint32_t) seq.count);
            else                       pick = (int) (((idx % seq.count) + seq.count) % seq.count);
            if (vary > 0.0f && dir != ArpDir::Random && arpHash01 (ui, 0x5Fu, 0u) < vary)
                pick = (int) (arpHash (ui, 0x60u, 1u) % (uint32_t) seq.count);
            int note = seq.n[pick];
            if (vary > 0.0f && arpHash01 (ui, 0x61u, 0u) < vary * 0.6f)
                note += 12 * (int) (arpHash (ui, 0x62u, 0u) % (uint32_t) (octaves + 1));

            // PITCH lane: contoured row offset, then chance-of-snap to a chord tone
            {
                const float c = (arpClamp01 (lanes_.pitch[li] / 6.0f) - 0.5f) * 2.0f;   // row 3 = 0
                const float shaped = (c >= 0 ? 1.0f : -1.0f) * std::pow (std::fabs (c), pExp);
                const int semis = (int) std::lround (shaped * (float) pSemis);
                int cand = note + semis;
                if (semis != 0 && ext_.pQuant > 0.0f && arpHash01 (ui, 0x71u, seedI) < ext_.pQuant)
                {
                    int best = seq.n[0], bd = 999;
                    for (int s = 0; s < seq.count; ++s)
                    { const int d = std::abs (seq.n[s] - cand); if (d < bd) { bd = d; best = seq.n[s]; } }
                    cand = best;
                }
                note = cand;
            }

            // OCT lane: reach × row + bias + random jump + alternating zigzag
            {
                int oc = (int) std::lround (lanes_.oct[li]) * octMult + octBias;
                if (ext_.oRand > 0.0f && arpHash01 (ui, 0x81u, seedI) < ext_.oRand)
                    oc += (arpHash (ui, 0x82u, 0u) & 1u) ? 1 : -1;
                if (octZig > 0 && (idx & 1)) oc += octZig;
                note += 12 * oc;
            }
            note = arpClampi (note, 0, 127);

            // VEL lane: contrast → depth blend → humanize → floor
            int vel;
            {
                const float lv = arpShape (lanes_.vel[li], ext_.vCurve);
                float v = (float) lastVel_ * arpLerp (1.0f, lv, arpClamp01 (ext_.vRange));
                v *= 1.0f + (arpHash01 (ui, 0x91u, seedI) - 0.5f) * arpClamp01 (ext_.vRand);
                v = std::max (v, arpClamp01 (ext_.vFloor) * 100.0f);
                vel = arpClampi ((int) std::lround (v), 1, 127);
            }

            // GATE lane: shaped length + humanize; TIE legatos into the next step
            float gateBeats; bool slide = false;
            {
                float g = gScale * arpShape (lanes_.gate[li], ext_.gCurve);
                g *= 1.0f + (arpHash01 (ui, 0xA5u, seedI) - 0.5f) * arpClamp01 (ext_.gRand);
                gateBeats = beats * std::min (std::max (g, 0.03f), 1.45f);
                if (ext_.gSlide > 0.0f && arpHash01 (ui, 0xB1u, seedI) < ext_.gSlide)
                    gateBeats = beats * 1.02f;
                if (ext_.pSlide > 0.0f && arpHash01 (ui, 0xB7u, seedI) < ext_.pSlide)
                    slide = true;
            }

            // ROLL lane: programmed rolls (lane × odds) or the global Roll pressure
            int rc = 1;
            {
                const int rl = arpClampi ((int) std::lround (lanes_.ratchet[li]), 1, 4);
                if (rl > 1) { if (arpHash01 (ui, 0xC5u, seedI) < arpClamp01 (ext_.rAmt)) rc = rl; }
                else if (ext_.mroll > 0.0f && arpHash01 (ui, 0xC7u, seedI) < arpClamp01 (ext_.mroll) * 0.6f)
                    rc = rollN;
                if (rc > 1) slide = false;
            }

            // WAVE lane target latches on the fire (slewed at block rate below)
            const float wTarget = arpClamp01 (arpShape (lanes_.wt[li], ext_.wCurve)
                                              + (arpHash01 (ui, 0xD1u, seedI) - 0.5f) * arpClamp01 (ext_.wRand));

            for (int k = 0; k < rc && n < maxOut - 1; ++k)
            {
                const float f0 = (k == 0)      ? 0.0f : std::pow ((float) k / (float) rc, rExp);
                const float f1 = (k == rc - 1) ? 1.0f : std::pow ((float) (k + 1) / (float) rc, rExp);
                const double subT = stepT + (double) beats * (double) f0;
                if (subT < pos || subT >= ppqEnd) continue;
                const long long key = idx * 8 + k;
                if (key <= lastKey_) continue;                 // boundary-jitter refire guard
                lastKey_ = key;

                const int on = arpClampi ((int) std::llround ((subT - pos) / ppqPerSample), 0, numSamples - 1);

                // close notes whose gate already ended; then slide-or-cut the rest
                for (int a = 0; a < actN_; )
                {
                    if (act_[a].offPpq <= subT + 1e-9)
                    { emitOff (act_[a].note, std::min (offAt (act_[a].offPpq), on)); act_[a] = act_[--actN_]; }
                    else ++a;
                }
                if (slide && k == 0)
                {
                    // slide: predecessors ring past this on by the glide overlap
                    const double ov = subT + std::max (0.02, (double) arpClamp01 (ext_.glide) * (double) beats * 0.8);
                    for (int a = 0; a < actN_; ++a) act_[a].offPpq = std::min (act_[a].offPpq, ov);
                }
                else
                {
                    for (int a = 0; a < actN_; ++a) emitOff (act_[a].note, on);   // legato-cut
                    actN_ = 0;
                }
                for (int a = 0; a < actN_; )                    // same-pitch retrigger guard
                {
                    if (act_[a].note == note) { emitOff (note, on); act_[a] = act_[--actN_]; }
                    else ++a;
                }

                const int kv = arpClampi ((int) std::lround ((float) vel
                                 * std::pow (1.0f - 0.7f * arpClamp01 (ext_.rDecay), (float) k)), 1, 127);
                if (n < maxOut) out[n++] = { true, note, kv, on };

                const double gb = (rc > 1)
                    ? std::min ((double) gateBeats, (double) beats * (double) ((f1 - f0) * 0.85f))
                    : (double) gateBeats;
                if (actN_ < kArpActMax) act_[actN_++] = { note, subT + std::max (0.01, gb) };
                else                    { emitOff (act_[0].note, on); act_[0] = { note, subT + std::max (0.01, gb) }; }

                vizNote_ = note; vizVel_ = kv; ++vizCount_;
                waveTarget_ = wTarget;
            }
        }

        // gate-offs that land inside this block (creates the inter-note gaps)
        for (int a = 0; a < actN_; )
        {
            if (act_[a].offPpq < ppqEnd && n < maxOut)
            { emitOff (act_[a].note, offAt (act_[a].offPpq)); act_[a] = act_[--actN_]; }
            else ++a;
        }

        // WAVE slew: Slide knob = fraction of a step to reach the new target
        {
            const double blockSec = (double) numSamples / sr;
            const float  t = arpClamp01 (ext_.wSlide) * (float) stepSec;
            const float  a = (t <= 0.0005f) ? 1.0f : (float) (1.0 - std::exp (-blockSec / (double) t));
            waveCur_ += (waveTarget_ - waveCur_) * a;
        }
        return n;
    }

    static void addUnique (NoteSet& s, int note) noexcept
    {
        for (int i = 0; i < s.count; ++i) if (s.n[i] == note) return;
        if (s.count < kArpMaxSeq) s.n[s.count++] = note;
    }
    static void removeNote (NoteSet& s, int note) noexcept
    {
        for (int i = 0; i < s.count; ++i) if (s.n[i] == note) { for (int j = i; j < s.count - 1; ++j) s.n[j] = s.n[j+1]; --s.count; return; }
    }

    NoteSet held_, latched_;
    int     curNote_     = -1;      // the single note currently sounding (-1 = none)
    double  curOffPpq_   = 0.0;     // when it should release (absolute PPQ)
    double  freePpq_     = 0.0;     // internal clock position (transport-stopped fallback)
    bool    latchEnabled_ = false, latchActive_ = false;
    int     lastVel_     = 100;
    ArpRng  rng_;

    // ── fb105 extension-card state ──────────────────────────────────────────
    static constexpr int kArpActMax = 8;
    struct Act { int note; double offPpq; };
    bool         extOn_ = false;    // flipped by setExt(); classic path until then
    ArpExtParams ext_;
    ArpLaneData  lanes_;
    Act          act_[kArpActMax]; int actN_ = 0;   // sounding notes (ties/slides overlap)
    long long    lastKey_ = (long long) -0x7FFFFFFFFFLL;   // refire guard (idx*8+k)
    double       lastPos_ = 0.0;
    float        waveCur_ = 0.5f, waveTarget_ = 0.5f;      // WAVE lane slew state
    float        vizStepF_ = 0.0f;
    int          vizNote_ = -1, vizVel_ = 0;
    uint32_t     vizCount_ = 0;
    bool         vizActive_ = false;
};

} // namespace wc
