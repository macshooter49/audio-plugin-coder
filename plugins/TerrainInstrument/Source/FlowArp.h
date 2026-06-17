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
//    1/1 … 1/256 straight (dotted/triplet live in the card). The top end is a
//    creative extreme — at 120 BPM, 1/256 ≈ 128 Hz, a near-static buzz.
static constexpr float kArpRate[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 0.015625f };
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
    // isn't ARP) so the synth never hangs on the last arp note. Emits the pending note-off.
    int releaseAll (ArpEvent* out, int maxOut) noexcept
    {
        int n = 0;
        if (curNote_ >= 0 && n < maxOut) { out[n++] = { false, curNote_, 0, 0 }; curNote_ = -1; }
        held_.count = 0; latched_.count = 0; latchActive_ = false;
        return n;
    }

    // Returns number of events written to `out` (capacity maxOut).
    int process (float rate, float gate, float vary, float traj, float morph,
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

        const float beats = arpBeatsPerStep (rate);
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

    NoteSet held_, latched_;
    int     curNote_     = -1;      // the single note currently sounding (-1 = none)
    double  curOffPpq_   = 0.0;     // when it should release (absolute PPQ)
    double  freePpq_     = 0.0;     // internal clock position (transport-stopped fallback)
    bool    latchEnabled_ = false, latchActive_ = false;
    int     lastVel_     = 100;
    ArpRng  rng_;
};

} // namespace wc
