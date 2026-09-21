#pragma once
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  FlowShaper.h — THE TERRAIN SHAPER (tp71). The Flow Chop card's slot, the Glitch card's chassis,
//  a shaper for a screen.
//
//  Max: "ShaperBox 3 and Gross Beat had a baby … everything should be DAW locked … as soon as you
//  press play it instantly shapes, no offset time." So there is nothing in here that could leave the
//  grid: NO fires, NO anchors, NO chance. Every lane is a drawn shape read at the host's position —
//  phase = (ppq / cycleBeats) mod 1 — every sample, and applied to its target. Press play at bar 1
//  and the shape is at bar 1; a loop wrap lands on the shape exactly.
//
//  Eight lanes, one shape each, all running at once (multi-lane): the card carries a volume gate, a
//  filter sweep and a halftime in one instance.
//    Volume  — a gain, smoothed so a square edge is click-free
//    Time    — Gross Beat's law: the shape is the READ POSITION within the cycle, from a ring of the
//              last cycles. A straight ramp is unity, half the slope is halftime, a repeating saw is
//              a stutter, backwards is reverse. Every jump is crossfaded.
//    Filter  — cutoff 40 Hz → 20 kHz (log) on a 2-pole SVF (LP / HP / BP / Notch); the rack roster
//              is wired by the processor when a roster type is chosen (see FilterLaneSlot)
//    Pan     — position L → R, constant-power or linear
//    Repeat  — the Glitch's Repeat as a shape: while the shape is up, a slice captured on the cycle's
//              grid loops; the shape's HEIGHT is the slice length on the grid ladder (higher = shorter)
//    Drive   — soft / hard / fold / tube, the shape on the drive amount; the rack's distortion roster
//              is wired by the processor when a roster type is chosen
//    Phaser  — a 6-stage allpass whose centre the shape moves, with feedback (Liquid is not our word)
//    Crush   — bit depth and sample rate together, the shape sets how far both go
//
//  Zero allocation on the audio thread. The Time / Repeat rings are armed lazily on the message
//  thread (the tp63 lazy law) and published through an atomic pointer.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace wc
{
static constexpr int kShaperLanes = 8;
static constexpr int kShaperT     = 2048;        // the baked table: one cycle, end to end (index kShaperT = the value AT the cycle's end)
enum class ShaperLaneId : int { Volume = 0, Time, Filter, Pan, Repeat, Drive, Phaser, Crush };

// the rate ladder — one cycle of the shape, in beats (4 = 1 bar)
static constexpr float kShaperRateBeats[8] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };
static constexpr int   kShaperRateN = 8;
static constexpr int   kShaperRateDefault = 4;   // 1 bar

struct ShaperLane
{
    bool  on      = false;
    float depth   = 1.0f;     // how much of the shape's range the target gets
    float smooth  = 0.25f;    // 0.2 → 40 ms one-pole on the read value
    float phase   = 0.0f;     // cycles, 0..1
    float tension = 0.5f;     // the curve between shape points: 0 = dip early, 1 = late
    float floor_  = 0.0f;     // the shape never reads below this
    float blend   = 1.0f;     // this lane's own wet/dry
    float swing   = 0.0f;     // stretches the first half of the cycle
    int   rate    = kShaperRateDefault;
    int   grid    = 16;
    int   mode    = 0;        // target mode (filter type, pan law, drive type, phaser mode, time range)
    float k[4]    = { 0.5f, 0.5f, 0.5f, 0.5f };   // the target's own knobs
    // the shape, BAKED from the editor's breakpoints (the LFO's own law: pinned ends, per-segment tension) on the
    // message thread. table[0] is the value at phase 0, table[kShaperT] the value at phase 1 — a unity ramp for the
    // Time lane reads 0 → 1 exactly, and a grid step sits exactly on its grid line.
    float table[kShaperT + 1] = {};
    void fill (float (*f) (double)) { for (int i = 0; i <= kShaperT; ++i) table[i] = f ((double) i / kShaperT); }
};
struct ShaperState { ShaperLane lanes[kShaperLanes]; };

class FlowShaper
{
public:
    void prepare (double sampleRate)
    {
        sr_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        for (auto& c : ch_) c = Ch{};
        phL_.assign (6, AP{}); phR_.assign (6, AP{});
        smooth_.fill (0.0f); tsPos_ = 0.0; tsOld_ = 0.0; tsLastBehind_ = 0.0; stepping_ = false; tsXf_ = 0; tsXfN_ = 1; holdN_ = 0;
        repHold_ = false; repLen_ = 0; repPos_ = 0; repStartW_ = 0;
        ringW_ = 0; ringFilled_ = 0;
    }
    /** Message thread. The Time and Repeat lanes need a ring of the last cycles: 16 beats at the
        slowest musical tempo is ~16 s — armed once, kept for the instance (the tp63 lazy law). */
    void armRing()
    {
        if (ring_ != nullptr) return;
        const int n = (int) std::ceil (sr_ * 16.0);
        auto r = std::make_shared<Ring>(); r->L.assign ((size_t) n, 0.0f); r->R.assign ((size_t) n, 0.0f); r->n = n;
        ringOwner_ = r; ring_.store (r.get(), std::memory_order_release);
    }
    bool ringArmed() const noexcept { return ring_.load (std::memory_order_acquire) != nullptr; }
    /** Message thread: publish a new state snapshot (the audio thread swaps to it at the next block). */
    void setState (std::shared_ptr<const ShaperState> s) { stateOwner_[(stateSeq_++) & 1] = s; state_.store (s.get(), std::memory_order_release); }

    /** Audio thread, per block: the lane's parameters (On / Depth / Rate / Mode) override the snapshot's. */
    void setLaneCtl (int lane, bool on, float depth, int rate, int mode) noexcept
    { auto& c = ctl_[lane & 7]; c.set = true; c.on = on; c.depth = depth; c.rate = rate; c.mode = mode; }
    // ── viz: the phase each lane is at (0..1) and the value it read, for the screen's playhead ──
    float vizPhase (int lane) const noexcept { return vizPh_[lane & 7]; }
    float vizValue (int lane) const noexcept { return vizV_[lane & 7]; }

    // ── the shape reader: a lane's shape at a phase, with its phase offset, swing and tension ──
    static float readShape (const ShaperLane& L, double ph) noexcept
    {
        ph -= std::floor (ph);
        if (L.swing > 0.0f) { const double s = L.swing * 0.45; ph = ph < 0.5 ? ph * (1.0 + s) : 0.5 * (1.0 + s) + (ph - 0.5) * (1.0 - s); }
        double ph2 = ph + (double) L.phase; ph2 -= std::floor (ph2);
        const double x = ph2 * kShaperT; int i = (int) x; if (i > kShaperT - 1) i = kShaperT - 1; const float f = (float) (x - i);
        float v = L.table[i] + (L.table[i + 1] - L.table[i]) * f;
        // Tension here is the LANE's curve on the read value (0 = dips early, 1 = late); the per-segment tension lives
        // in the editor's breakpoints and is already baked in.
        const float tn = L.tension;
        if (tn < 0.5f)      v = std::pow (v, 1.0f + (0.5f - tn) * 3.0f);
        else if (tn > 0.5f) v = 1.0f - std::pow (1.0f - v, 1.0f + (tn - 0.5f) * 3.0f);
        return v < L.floor_ ? L.floor_ : v;
    }

    /** Audio thread. In place, stereo. hostPpq = the block's first sample in beats; playing = the
        host transport. With the transport stopped every lane holds the shape at phase 0. */
    void process (float* L, float* R, int n, double hostPpq, double bpm, bool playing, float mix = 1.0f) noexcept
    {
        const ShaperState* S0 = state_.load (std::memory_order_acquire);
        if (S0 == nullptr || n <= 0) return;
        // the block's lanes: the snapshot's tables and knobs, with the parameters' On / Depth / Rate / Mode laid over
        // (a copy of eight small structs; the tables are not copied — the lane view points into the snapshot)
        for (int ln = 0; ln < kShaperLanes; ++ln)
        { const ShaperLane& src = S0->lanes[ln]; LaneView& v = view_[ln]; v.L = &src; v.on = src.on; v.depth = src.depth; v.rate = src.rate; v.mode = src.mode;
          const Ctl& c = ctl_[ln]; if (c.set) { v.on = c.on; v.depth = c.depth; v.rate = c.rate; v.mode = c.mode; } }
        const double BP = bpm > 0.0 ? bpm : 120.0, pps = BP / 60.0 / sr_, fpb = sr_ / (BP / 60.0);
        Ring* ring = ring_.load (std::memory_order_acquire);
        const LaneView& VL = view_[0]; const LaneView& TL = view_[1]; const LaneView& FL = view_[2]; const LaneView& PN = view_[3];
        const LaneView& RP = view_[4]; const LaneView& DR = view_[5]; const LaneView& PH = view_[6]; const LaneView& CR = view_[7];
        const bool anyRing = (TL.on || RP.on) && ring != nullptr;
        // filter coefficients per block (the cutoff moves per sample, but the type / resonance per block)
        const float res = 1.0f - 0.95f * FL.L->k[0], kres = 2.0f * res;

        double beat = playing ? hostPpq : 0.0;
        for (int i = 0; i < n; ++i, beat += playing ? pps : 0.0)
        {
            float l = L[i], r = R[i];
            const float dl = l, dr = r;
            // ── the ring hears the INPUT (Time and Repeat read from it) ──
            if (anyRing) { ring->L[(size_t) ringW_] = l; ring->R[(size_t) ringW_] = r; }

            // ── TIME: the read position within the cycle (Gross Beat) ──
            if (TL.on && ring != nullptr)
            {
                const double cyc = kShaperRateBeats[TL.rate & 7];
                double p = beat / cyc; p -= std::floor (p);
                const float s = readShape (*TL.L, p);
                const float rangeMul = TL.mode == 1 ? 0.5f : TL.mode == 2 ? 2.0f : 1.0f;   // Range: 1 cycle · ½ · 2
                const double shapedBeats = (double) s * cyc * rangeMul;                 // where in the cycle the shape reads
                const double nowBeats    = p * cyc;                                     // where the cycle is
                double behind = (nowBeats - shapedBeats) * (double) TL.depth;           // can only read the PAST
                if (behind < 0.0) behind = 0.0;
                double behindF = behind * fpb; const double maxB = (double) (ringFilled_ > 4 ? ringFilled_ - 4 : 0);
                if (behindF > maxB) behindF = maxB;
                // ── THE JUMP LAW. A step in the shape is a ramp of one table cell (2 ms at a bar, 8 ms at four bars): the
                //  read position would SCRATCH backwards through it at tens of samples per sample. So the moment `behind`
                //  moves faster than 4 samples per sample the lane looks a cell and a half AHEAD for the settled target,
                //  lands there at once, and crossfades from the old stream over ~1 ms. The transient at the start of a
                //  repeated slice — the whole point of a stutter — is heard, and nothing scratches or clicks.
                const double dBeh = behindF - tsLastBehind_; tsLastBehind_ = behindF;
                if (std::fabs (dBeh) > 4.0)
                {
                    if (! stepping_)
                    {
                        stepping_ = true; tsOld_ = tsPos_ + 1.0;
                        tsXfN_ = std::max (48, (int) (sr_ * 0.001 * (0.5 + TL.L->k[0] * 4.5))); tsXf_ = tsXfN_;
                        double p2 = p + 1.5 / kShaperT; p2 -= std::floor (p2);
                        const float s2 = readShape (*TL.L, p2);
                        double bt = (p2 * cyc - (double) s2 * cyc * rangeMul) * (double) TL.depth; if (bt < 0.0) bt = 0.0;
                        stepTarget_ = std::min (bt * fpb, maxB);
                    }
                    behindF = stepTarget_;
                }
                else
                {
                    stepping_ = false;
                    if (tsXf_ <= 0 && std::fabs (((double) ringW_ - behindF) - (tsPos_ + 1.0)) > 6.0)
                    { tsOld_ = tsPos_ + 1.0; tsXfN_ = std::max (48, (int) (sr_ * 0.001 * (0.5 + TL.L->k[0] * 4.5))); tsXf_ = tsXfN_; }
                }
                const double pos = (double) ringW_ - behindF;
                float tl = rd (ring->L, ring->n, pos), tr = rd (ring->R, ring->n, pos);
                if (tsXf_ > 0) { const float w = (float) tsXf_ / (float) tsXfN_; tl = tl * (1 - w) + rd (ring->L, ring->n, tsOld_) * w; tr = tr * (1 - w) + rd (ring->R, ring->n, tsOld_) * w; tsOld_ += 1.0; --tsXf_; }
                tsPos_ = pos;
                l = l + (tl - l) * TL.L->blend; r = r + (tr - r) * TL.L->blend;
                vizPh_[1] = (float) p; vizV_[1] = s;
            }
            // ── REPEAT: while the shape is up, a slice from the grid loops; its height = the slice length ──
            if (RP.on && ring != nullptr)
            {
                const double cyc = kShaperRateBeats[RP.rate & 7];
                double p = beat / cyc; p -= std::floor (p);
                const float s = readShape (*RP.L, p) * RP.depth;
                const bool want = s > 0.02f;
                if (want && ! repHold_)
                {   // capture at this grid step's start: the slice began at the last grid line
                    const double stepBeats = cyc / (double) (RP.L->grid > 0 ? RP.L->grid : 16);
                    const double sinceStep = std::fmod (beat, stepBeats) * fpb;
                    repStartW_ = ringW_ - (int) sinceStep; repPos_ = 0.0; repHold_ = true;
                }
                if (want)
                {   // the slice length: the shape's height on the ladder 1/4 … 1/64 of a beat-cycle (higher = shorter)
                    const int div = 4 << (int) std::floor (s * 4.99f);                 // 4, 8, 16, 32, 64
                    const int len = std::max (32, (int) (cyc * fpb / (double) div));
                    if (len != repLen_) { repLen_ = len; }
                    double rp = (double) repStartW_ + std::fmod (repPos_, (double) repLen_);
                    // a short cosine seam at the loop point keeps it click-free
                    const double inLoop = std::fmod (repPos_, (double) repLen_); const int seam = std::min (64, repLen_ / 4);
                    float rl = rd (ring->L, ring->n, rp), rr = rd (ring->R, ring->n, rp);
                    if (inLoop < seam) { const float w = 0.5f - 0.5f * std::cos ((float) (inLoop / seam) * 3.14159265f); const double rp2 = rp + repLen_; rl = rl * w + rd (ring->L, ring->n, rp2) * (1 - w); rr = rr * w + rd (ring->R, ring->n, rp2) * (1 - w); }
                    repPos_ += 1.0;
                    const float mixr = RP.L->blend;
                    l = l + (rl - l) * mixr; r = r + (rr - r) * mixr;
                }
                else repHold_ = false;
                vizPh_[4] = (float) p; vizV_[4] = s;
            }
            if (anyRing) { ringW_ = (ringW_ + 1) % ring->n; if (ringFilled_ < ring->n) ++ringFilled_; }

            // ── DRIVE ──
            if (DR.on)
            {
                const double cyc = kShaperRateBeats[DR.rate & 7]; double p = beat / cyc; p -= std::floor (p);
                const float s = readShape (*DR.L, p) * DR.depth; const float g = 1.0f + 18.0f * s;
                const float mk = 1.0f / std::pow (g, 0.6f * (0.3f + 0.7f * DR.L->k[1]));
                auto sh = [&] (float x) noexcept { x *= g; switch (DR.mode) { case 1: return x < -1.f ? -1.f : (x > 1.f ? 1.f : x);
                                                                          case 2: return std::sin (x * 0.9f);
                                                                          case 3: return (x < 0 ? -1.f : 1.f) * (1.f - std::exp (-std::fabs (x))) * (1.f + 0.15f * x * x / (1.f + x * x));
                                                                          default: return std::tanh (x); } };
                // at shape 0 the lane is a WIRE (tanh alone would already colour a -6 dBFS sine); the shaped signal
                // fades in over the first quarter of the shape's travel, then the pre-gain does the rest
                const float in = s < 0.25f ? s * 4.0f : 1.0f;
                const float wl = l + (sh (l) * mk - l) * in, wr = r + (sh (r) * mk - r) * in;
                // Tone: a one-pole low shelf of the wet against the knob (0.5 = flat)
                const float tone = DR.L->k[0]; const float tc = 1.0f - std::exp (-2.0f * 3.14159265f * (400.0f + 12000.0f * tone * tone) / (float) sr_);
                ch_[0].tone += tc * (wl - ch_[0].tone); ch_[1].tone += tc * (wr - ch_[1].tone);
                const float tl2 = tone >= 0.5f ? wl : ch_[0].tone + (wl - ch_[0].tone) * (tone * 2.0f), tr2 = tone >= 0.5f ? wr : ch_[1].tone + (wr - ch_[1].tone) * (tone * 2.0f);
                l = l + (tl2 - l) * DR.L->blend; r = r + (tr2 - r) * DR.L->blend;
                vizPh_[5] = (float) p; vizV_[5] = s;
            }
            // ── CRUSH ──
            if (CR.on)
            {
                const double cyc = kShaperRateBeats[CR.rate & 7]; double p = beat / cyc; p -= std::floor (p);
                const float s = readShape (*CR.L, p) * CR.depth;
                const float bits = CR.mode == 2 ? 16.0f : 16.0f - (4.0f + 8.0f * CR.L->k[0]) * s; const float q = std::pow (2.0f, bits - 1.0f);
                const int hold = CR.mode == 1 ? 1 : 1 + (int) (s * (2.0f + 30.0f * CR.L->k[1]));   // Mode: Bits + Rate · Bits · Rate
                if (holdN_ <= 0) { holdL_ = std::round (l * q) / q; holdR_ = std::round (r * q) / q; holdN_ = hold; }
                --holdN_;
                l = l + (holdL_ - l) * CR.L->blend; r = r + (holdR_ - r) * CR.L->blend;
                vizPh_[7] = (float) p; vizV_[7] = s;
            }
            // ── FILTER (SVF, 2-pole; the roster slot, if wired, replaces this — see FilterLaneSlot) ──
            if (FL.on)
            {
                const double cyc = kShaperRateBeats[FL.rate & 7]; double p = beat / cyc; p -= std::floor (p);
                const float s = readShape (*FL.L, p);
                const float fc = 20000.0f * std::pow (40.0f / 20000.0f, FL.depth * (1.0f - s));
                const float g = std::tan (3.14159265f * std::min (fc, (float) sr_ * 0.45f) / (float) sr_);
                const float a1 = 1.0f / (1.0f + g * (g + kres)), a2 = g * a1, a3 = g * a2;
                for (int c = 0; c < 2; ++c)
                {
                    Ch& C = ch_[c]; float x = c ? r : l;
                    if (FL.L->k[1] > 0.0f) x = std::tanh (x * (1.0f + 6.0f * FL.L->k[1])) / (1.0f + FL.L->k[1]);
                    const float v3 = x - C.ic2, v1 = a1 * C.ic1 + a2 * v3, v2 = C.ic2 + a2 * C.ic1 + a3 * v3;
                    C.ic1 = 2 * v1 - C.ic1; C.ic2 = 2 * v2 - C.ic2;
                    float y; switch (FL.mode) { case 1: y = x - kres * v1 - v2; break; case 2: y = v1; break; case 3: y = x - kres * v1; break; default: y = v2; }
                    if (c) r = r + (y - r) * FL.L->blend; else l = l + (y - l) * FL.L->blend;
                }
                vizPh_[2] = (float) p; vizV_[2] = s;
            }
            // ── PHASER (or, in Flanger mode, a modulated short delay with feedback): the shape moves the centre ──
            if (PH.on && PH.mode == 1)
            {
                const double cyc = kShaperRateBeats[PH.rate & 7]; double p = beat / cyc; p -= std::floor (p);
                const float s = readShape (*PH.L, p);
                const float fb = 0.2f + 0.72f * PH.L->k[0], st = PH.L->k[1] * 0.5f;
                const float dms = 0.3f + 7.7f * s * PH.depth;
                for (int c = 0; c < 2; ++c)
                {
                    float* D = c ? flR_ : flL_; const float dsm = dms * (c ? (1.0f + st * 0.3f) : (1.0f - st * 0.3f));
                    const float rp = (float) flW_ - dsm * (float) sr_ * 0.001f; const int i0 = (int) std::floor (rp); const float f = rp - (float) i0;
                    const float w = D[(i0 + 4096) & 2047] * (1 - f) + D[(i0 + 1 + 4096) & 2047] * f;
                    const float x = c ? r : l; D[flW_ & 2047] = x + w * fb;
                    if (c) r = r + (w - x * 0.15f) * PH.L->blend; else l = l + (w - x * 0.15f) * PH.L->blend;
                }
                flW_ = (flW_ + 1) & 2047;
                vizPh_[6] = (float) p; vizV_[6] = s;
            }
            else if (PH.on)
            {
                const double cyc = kShaperRateBeats[PH.rate & 7]; double p = beat / cyc; p -= std::floor (p);
                const float s = readShape (*PH.L, p);
                const float fb = 0.2f + 0.72f * PH.L->k[0];
                const float fc = 200.0f * std::pow (40.0f, s * PH.depth);
                const float st = PH.L->k[1] * 0.35f;   // stereo: the right channel's centre sits a little higher
                for (int c = 0; c < 2; ++c)
                {
                    auto& A = c ? phR_ : phL_; const float fcc = fc * (c ? (1.0f + st) : (1.0f - st));
                    const float t = std::tan (3.14159265f * std::min (fcc, (float) sr_ * 0.45f) / (float) sr_), gg = (1.0f - t) / (1.0f + t);
                    const float x = (c ? r : l) + ch_[c].phfb * fb * 0.6f; float y = x;
                    for (int q = 0; q < 6; ++q) { AP& st2 = A[(size_t) q]; const float o = -gg * y + st2.x1 + gg * st2.y1; st2.x1 = y; st2.y1 = o; y = o; }
                    ch_[c].phfb = y;
                    if (c) r = r + y * PH.L->blend; else l = l + y * PH.L->blend;
                }
                vizPh_[6] = (float) p; vizV_[6] = s;
            }
            // ── PAN ──
            if (PN.on)
            {
                const double cyc = kShaperRateBeats[PN.rate & 7]; double p = beat / cyc; p -= std::floor (p);
                const float s = readShape (*PN.L, p); const float pan = (s * 2.0f - 1.0f) * PN.depth;
                float gl, gr;
                if (PN.mode == 1) { gl = 1.0f - (pan > 0 ? pan : 0); gr = 1.0f + (pan < 0 ? pan : 0); }
                else { const float th = (pan + 1.0f) * 0.78539816f; gl = std::cos (th) * 1.41421356f; gr = std::sin (th) * 1.41421356f; }
                l *= 1.0f + (gl - 1.0f) * PN.L->blend; r *= 1.0f + (gr - 1.0f) * PN.L->blend;
                vizPh_[3] = (float) p; vizV_[3] = s;
            }
            // ── VOLUME ──
            if (VL.on)
            {
                const double cyc = kShaperRateBeats[VL.rate & 7]; double p = beat / cyc; p -= std::floor (p);
                const float s = readShape (*VL.L, p);
                const float gRaw = 1.0f - VL.depth * (1.0f - s);
                const float ms = 0.2f + 40.0f * VL.L->smooth; const float a = std::exp (-1.0f / ((float) sr_ * ms * 0.001f));
                smooth_[0] = gRaw + (smooth_[0] - gRaw) * a; const float g = 1.0f + (smooth_[0] - 1.0f) * VL.L->blend;
                l *= g; r *= g;
                vizPh_[0] = (float) p; vizV_[0] = s;
            }
            L[i] = dl + (l - dl) * mix; R[i] = dr + (r - dr) * mix;
        }
    }

private:
    struct Ring { std::vector<float> L, R; int n = 0; };
    struct Ch { float ic1 = 0, ic2 = 0, tone = 0, phfb = 0; };
    struct AP { float x1 = 0, y1 = 0; };
    static float rd (const std::vector<float>& b, int n, double p) noexcept
    { double q = std::fmod (p, (double) n); if (q < 0) q += n; const int i0 = (int) q; const float f = (float) (q - i0); const int i1 = (i0 + 1 == n) ? 0 : i0 + 1; return b[(size_t) i0] * (1 - f) + b[(size_t) i1] * f; }

    struct LaneView { const ShaperLane* L = nullptr; bool on = false; float depth = 1.0f; int rate = kShaperRateDefault, mode = 0; };
    struct Ctl { bool set = false, on = false; float depth = 1.0f; int rate = kShaperRateDefault, mode = 0; };
    LaneView view_[kShaperLanes]; Ctl ctl_[kShaperLanes];
    double sr_ = 48000.0;
    std::atomic<const ShaperState*> state_ { nullptr };
    std::shared_ptr<const ShaperState> stateOwner_[2]; unsigned stateSeq_ = 0;
    std::shared_ptr<Ring> ringOwner_; std::atomic<Ring*> ring_ { nullptr };
    int ringW_ = 0, ringFilled_ = 0;
    Ch ch_[2]; std::vector<AP> phL_, phR_;
    std::array<float, 8> smooth_ {};
    double tsPos_ = 0, tsOld_ = 0, tsLastBehind_ = 0, stepTarget_ = 0; int tsXf_ = 0, tsXfN_ = 1; bool stepping_ = false;
    float holdL_ = 0, holdR_ = 0; int holdN_ = 0;
    float flL_[2048] = {}, flR_[2048] = {}; int flW_ = 0;   // the Flanger mode's delay line (~43 ms at 48 k)
    bool repHold_ = false; int repLen_ = 0, repStartW_ = 0; double repPos_ = 0;
    float vizPh_[8] = {}, vizV_[8] = {};
};
} // namespace wc
