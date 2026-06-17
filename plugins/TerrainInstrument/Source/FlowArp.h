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
//    1/1, 1/2, 1/4, 1/8, 1/16, 1/32  (straight; dotted/triplet live in the card).
static constexpr float kArpRate[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
static constexpr int   kArpRateN  = (int) (sizeof (kArpRate) / sizeof (float));
inline float arpBeatsPerStep (float rateKnob) noexcept { return kArpRate[ arpQuantIdx (rateKnob, kArpRateN) ]; }

// ── GATE: knob -> note length as fraction of the step (5%..98%). ─────────────
inline float arpGateFrac (float gateKnob) noexcept { return 0.05f + 0.93f * arpClamp01 (gateKnob); }

// ── TRAJ: direction + octave range as one ordered "reach" ramp. ──────────────
enum class ArpDir : int { Up = 0, Down, UpDown, Converge, Random };
struct TrajPreset { ArpDir dir; int octaves; };
static constexpr TrajPreset kArpTraj[] = {
    { ArpDir::Up,       1 },   // 0.00  tight, rising
    { ArpDir::Up,       2 },
    { ArpDir::UpDown,   2 },
    { ArpDir::Converge, 2 },
    { ArpDir::Down,     3 },
    { ArpDir::UpDown,   3 },
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
//  FlowArp — thin stateful engine wrapping the pure helpers above.
//  Holds: held / latched notes, latch flag, pending note-offs, last velocity.
//  process() is called once per block; it returns the events for this block.
// =============================================================================
class FlowArp
{
public:
    void reset() noexcept
    {
        held_.count = 0; latched_.count = 0; latchActive_ = false;
        pendingN_ = 0; lastVel_ = 100; rng_.seed (0x12345678u);
    }

    void setLatch (bool on) noexcept
    {
        // Turning latch off clears the latched copy; pending offs still flush.
        latchEnabled_ = on;
        if (! on) { latched_.count = 0; latchActive_ = false; }
    }

    void noteOn (int note, int vel) noexcept
    {
        lastVel_ = arpClampi (vel, 1, 127);
        if (latchEnabled_)
        {
            // first key after silence starts a fresh latched chord
            if (! latchActive_ || held_.count == 0) { latched_.count = 0; latchActive_ = true; }
            addUnique (latched_, note);
        }
        addUnique (held_, note);
    }

    void noteOff (int note) noexcept
    {
        removeNote (held_, note);          // latched copy persists until latch off / new chord
    }

    // Returns number of events written to `out` (capacity kArpMaxEvents).
    // knobs: rate,gate,vary,traj,morph all normalized [0,1] (EFFECTIVE values).
    int process (float rate, float gate, float vary, float traj, float morph,
                 double ppqStart, double bpm, double sampleRate, int numSamples,
                 bool playing, ArpEvent* out, int maxOut) noexcept
    {
        int n = 0;
        const double bps = (bpm > 0.0 ? bpm : 120.0) / 60.0;
        const double ppqPerSample = bps / (sampleRate > 0.0 ? sampleRate : 44100.0);
        const double ppqEnd = ppqStart + ppqPerSample * (double) numSamples;

        // 1) flush any pending note-offs that fall inside this block (always, even if stopped)
        n += flushPendingOffs (ppqStart, ppqEnd, ppqPerSample, out, maxOut, n);

        // transport stopped, or nothing to play -> emit only the flushed offs
        const NoteSet& src = (latchEnabled_ && latched_.count > 0) ? latched_ : held_;
        if (! playing || src.count == 0) return n;

        // 2) build voicing + sequence for this block (cheap; reflects current knobs/chord)
        const TrajPreset tp = arpTraj (traj);
        NoteSet voiced; arpVoicing (src.n, src.count, morph, voiced);
        NoteSet seq;    arpSequence (voiced, tp.dir, tp.octaves, seq);
        if (seq.count == 0) return n;

        const float  beats = arpBeatsPerStep (rate);
        const float  gFrac = arpGateFrac (gate);

        // 3) place step boundaries in this block
        StepHit hits[kArpMaxEvents];
        const int hitN = arpStepsInBlock (ppqStart, bpm, sampleRate, numSamples, beats, hits, kArpMaxEvents);

        for (int h = 0; h < hitN && n < maxOut; ++h)
        {
            const int note = arpPickNote (seq, tp.dir, hits[h].stepIndex, vary, tp.octaves, rng_);
            if (note < 0) continue;

            // note-on now
            out[n++] = { true, note, lastVel_, hits[h].sampleOffset };

            // schedule note-off at stepPpq + beats*gate (may land in a later block)
            const double offPpq = hits[h].ppq + (double) beats * (double) gFrac;
            const int offInBlock = (int) std::llround ((offPpq - ppqStart) / ppqPerSample);
            if (offPpq < ppqEnd && offInBlock >= 0 && offInBlock < numSamples && n < maxOut)
                out[n++] = { false, note, 0, arpClampi (offInBlock, 0, numSamples - 1) };
            else
                pushPendingOff (note, offPpq);
        }
        return n;
    }

private:
    static void addUnique (NoteSet& s, int note) noexcept
    {
        for (int i = 0; i < s.count; ++i) if (s.n[i] == note) return;
        if (s.count < kArpMaxSeq) s.n[s.count++] = note;
    }
    static void removeNote (NoteSet& s, int note) noexcept
    {
        for (int i = 0; i < s.count; ++i) if (s.n[i] == note) { for (int j = i; j < s.count - 1; ++j) s.n[j] = s.n[j+1]; --s.count; return; }
    }
    void pushPendingOff (int note, double ppq) noexcept
    {
        if (pendingN_ < kArpMaxPending) { pending_[pendingN_].note = note; pending_[pendingN_].ppq = ppq; ++pendingN_; }
    }
    int flushPendingOffs (double ppqStart, double ppqEnd, double ppqPerSample,
                          ArpEvent* out, int maxOut, int startN) noexcept
    {
        int added = 0;
        for (int i = 0; i < pendingN_; )
        {
            if (pending_[i].ppq < ppqEnd && (startN + added) < maxOut)
            {
                int off = (int) std::llround ((pending_[i].ppq - ppqStart) / ppqPerSample);
                if (off < 0) off = 0;
                out[startN + added] = { false, pending_[i].note, 0, off };
                ++added;
                pending_[i] = pending_[--pendingN_];   // swap-remove
            }
            else ++i;
        }
        return added;
    }

    struct Pending { int note; double ppq; };
    NoteSet  held_, latched_;
    Pending  pending_[kArpMaxPending];
    int      pendingN_   = 0;
    bool     latchEnabled_ = false, latchActive_ = false;
    int      lastVel_    = 100;
    ArpRng   rng_;
};

} // namespace wc
