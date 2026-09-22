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
//  tp72 — THE ROSTERS AND THE TRIGGERS. Max: "the filter needs to be able to have a way to choose the
//  filter type … ladder filter, acid 303 … we can also choose all of our distortions … these effects
//  night and day". The Filter lane's type is the rack's 118-engine roster, the Drive lane's type the
//  rack's 23 distortions, the Phaser lane picks from the roster's phasers / flangers / combs and the
//  Crush lane adds the roster's crushers — all through ShaperExt, an interface the PROCESSOR implements
//  with the shipped FilterFxEngine / DistortionEngine (this header stays JUCE-free so the offline proof
//  runs). Until the processor has armed an engine (lazily, on the message thread) the built-in fallback
//  runs, so a lane is never silent. Trigger per lane: Sync (the transport), Free (its own clock), MIDI
//  (restarts on the note-on, at its sample) and Audio (restarts on a transient of the input).
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

enum class ShaperTrig : int { Sync = 0, Free, Audio, Midi };

/** tp72 — the rack's engines, lent to the lanes. The processor implements this; a slot that is not armed yet
    returns false and the lane runs its built-in fallback. `which`: filter slots 0 = Filter lane, 1 = Phaser lane,
    2 = Crush lane; drive slots 0 = Drive lane, 1 = Crush lane. All calls are per sample, audio thread. */
struct ShaperExt
{
    virtual ~ShaperExt() = default;
    virtual bool filter (int which, int engine, float cut01, float res, float drive, float poles, int charIdx, float spread, float& l, float& r) noexcept = 0;
    /** mix: the lane's wet/dry — the rack's distortion delays its wet by its resampler and aligns the dry INSIDE, so the
        engine owns the mix and the lane replaces (a crossfade outside would comb). The lane passes blend × fade-in. */
    virtual bool drive  (int which, int mode, float drive01, float tone, int character, float bias, float mix, float& l, float& r) noexcept = 0;
};

// the Phaser lane's roster: indices into the rack's filter roster (tw::filters::Type / FLT_ENGINES), modes 2.. of the lane
static constexpr int kShaperPhaserRoster[26] = { 19, 72, 20, 73, 74, 99, 101, 103,        // Phaser 4P 6P 8P 12P 16P 24P 32P 48P
                                                 94, 95, 96, 97, 98, 100, 102, 104,      // their N (notch) variants
                                                 111, 112, 10, 11, 62, 63, 64, 65, 12, 75 };   // Flange + / -, Comb + / -, Wide, Octave, Fifth, Damp, Shimmer, Diffusor
static constexpr int kShaperPhaserRosterN = 26;
// the Crush lane: modes 0..2 built in (Bits + Rate · Bits · Rate), 3..6 the roster's crushers, 7..9 the distortion's digital family
static constexpr int kShaperCrushRoster[4] = { 23, 83, 84, 91 };   // Bit-Crush, Samp-Hold, Samp-Hold -, Radio
static constexpr int kShaperCrushDist[3]   = { 20, 21, 22 };       // Downsample, Bitcrush, Overflow

struct ShaperLane
{
    bool  on      = false;
    float depth   = 1.0f;     // how much of the shape's range the target gets
    float smooth  = 0.2f;     // 0.2 + 250·s² ms one-pole on the read value (0.2 = 10 ms, the gate's edge)
    float phase   = 0.0f;     // cycles, 0..1
    float tension = 0.5f;     // the curve between shape points: 0 = dip early, 1 = late
    float floor_  = 0.0f;     // the shape never reads below this
    float blend   = 1.0f;     // this lane's own wet/dry
    float swing   = 0.0f;     // stretches the first half of the cycle
    int   rate    = kShaperRateDefault;
    int   grid    = 16;
    int   mode    = 0;        // target mode (filter engine, pan law, distortion type, phaser roster entry, time range)
    int   trig    = 0;        // ShaperTrig
    float k[6]    = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };   // the target's own knobs (the Target tab — the lane's back panel)
    // the shape, BAKED from the editor's breakpoints (the LFO's own law: pinned ends, per-segment tension) on the
    // message thread. table[0] is the value at phase 0, table[kShaperT] the value at phase 1 — a unity ramp for the
    // Time lane reads 0 → 1 exactly, and a grid step sits exactly on its grid line.
    float table[kShaperT + 1] = {};
    void fill (float (*f) (double)) { for (int i = 0; i <= kShaperT; ++i) table[i] = f ((double) i / kShaperT); }
};
// tp72/tp74 — the Target tab's knobs, per lane, at rest: [k0 .. k5] (three knobs a lane, Max: "every target should have three
// parameters"; the steps ride k2/k3 on the Filter and k2 on Drive)
//   Volume  Attack · Release · Punch            Time    Fade · Glide · Range          Filter  Reso · Drive · Spread(k4) · Poles(k2) · Char(k3)
//   Pan     Width · Bass · Haas                 Repeat  Seam · Decay · Pitch          Drive   Tone · Makeup · Bias(k3) · Char(k2)
//   Phaser  Feedback · Stereo · Drive           Crush   Bits · Rate · Tone
static constexpr float kShaperKDefault[kShaperLanes][6] = {
    { 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f }, { 0.3f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f }, { 0.3f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f }, { 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f },
    { 0.3f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 1.0f, 0.5f, 0.5f, 0.5f } };
struct ShaperState
{
    ShaperLane lanes[kShaperLanes];
    float sense = 0.5f;   // the Audio trigger's sensitivity
    /** tp79 — THE CHAIN. slot[p] is the KIND that occupies position p, first position first.
        It is a PERMUTATION, not a free list: a kind appears exactly once, because placing one that is already
        down swaps the two positions. That is what lets every lane keep one set of parameters, one set of DSP
        state and one clock, all still indexed by KIND — reordering needs no state surgery at all.
        Default = the tile order the card draws, so the chain reads left to right exactly as it looks. (Before
        tp79 the order was fixed at Time · Repeat · Drive · Crush · Filter · Phaser · Pan · Volume, which nothing
        on screen showed; a fresh card now lights no lane at all, so a new patch cannot hear the difference.)
        It travels in the shaperJson blob with the breakpoints — drawn, not automated — so no parameter was added
        and no saved patch lost one. */
    int slot[kShaperLanes] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    ShaperState() { for (int ln = 0; ln < kShaperLanes; ++ln) for (int q = 0; q < 6; ++q) lanes[ln].k[q] = kShaperKDefault[ln][q]; }
    /** A blob can be old, partial or hand-edited, and this array INDEXES A SWITCH on the audio thread. Anything
        that is not a permutation of 0..kShaperLanes-1 falls back to the identity. */
    static void sanitise (int* sl) noexcept
    {
        bool seen[kShaperLanes] = {}; bool ok = true;
        for (int i = 0; i < kShaperLanes; ++i)
        { const int v = sl[i]; if (v < 0 || v >= kShaperLanes || seen[v]) { ok = false; break; } seen[v] = true; }
        if (! ok) for (int i = 0; i < kShaperLanes; ++i) sl[i] = i;
    }
};

class FlowShaper
{
public:
    void prepare (double sampleRate)
    {
        sr_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        for (auto& c : ch_) c = Ch{};
        phL_.assign (6, AP{}); phR_.assign (6, AP{});
        smooth_.fill (0.0f); tsPos_ = 0.0; tsOld_ = 0.0; tsLastBehind_ = 0.0; stepping_ = false; tsXf_ = 0; tsXfN_ = 1; holdN_ = 0;
        repHold_ = false; repLen_ = 0; repPos_ = 0; repStartW_ = 0; repRead_ = 0.0; tsSlew_ = -1.0;
        ringW_ = 0; ringFilled_ = 0;
        for (auto& f : freePh_) f = 0.0; envFast_ = envSlow_ = 0.0f; refr_ = 0; noteAt_ = -1;
        for (auto& c : crushLp_) c = 0.0f; for (auto& b : bassLp_) b = 0.0f; volEnv_ = 1.0f; volPrev_ = 1.0f; punchEnv_ = 0.0f; punchRefr_ = 0;
        for (auto& h : haasL_) h = 0.0f; for (auto& h : haasR_) h = 0.0f; haasW_ = 0;
    }
    /** The processor lends the rack's engines (may be null: every lane then runs its built-in). Set once, before processing. */
    void setExt (ShaperExt* e) noexcept { ext_.store (e, std::memory_order_release); }
    /** Audio thread, before process(): a note-on landed at this sample of the block (the MIDI trigger). */
    void noteOn (int sampleOffset) noexcept { noteAt_ = sampleOffset < 0 ? 0 : sampleOffset; }
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
    void setLaneCtl (int lane, bool on, float depth, int rate, int mode, int trig = 0) noexcept
    { auto& c = ctl_[lane & 7]; c.set = true; c.on = on; c.depth = depth; c.rate = rate; c.mode = mode; c.trig = trig; }
    // ── viz: the phase each lane is at (0..1) and the value it read, for the screen's playhead ──
    float vizPhase (int lane) const noexcept { return vizPh_[lane & 7]; }
    float vizValue (int lane) const noexcept { return vizV_[lane & 7]; }

    // tp72 — the lane's Smooth (0.2 → 40 ms one-pole) on the value every shaped lane reads; the Volume lane has its own
    // attack / release law below, the Time and Repeat lanes read the raw shape (their jumps are the point)
    float smoothed (int ln, float v, float smooth) noexcept
    {
        if (smooth <= 0.005f) { smooth_[ln] = v; return v; }
        const float ms = 0.2f + 250.0f * smooth * smooth; const float a = std::exp (-1.0f / ((float) sr_ * ms * 0.001f));   // tp74 — up to 250 ms: heard, not implied
        smooth_[ln] = v + (smooth_[ln] - v) * a; return smooth_[ln];
    }
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
        { const ShaperLane& src = S0->lanes[ln]; LaneView& v = view_[ln]; v.L = &src; v.on = src.on; v.depth = src.depth; v.rate = src.rate; v.mode = src.mode; v.trig = src.trig;
          const Ctl& c = ctl_[ln]; if (c.set) { v.on = c.on; v.depth = c.depth; v.rate = c.rate; v.mode = c.mode; v.trig = c.trig; } }
        const double BP = bpm > 0.0 ? bpm : 120.0, pps = BP / 60.0 / sr_, fpb = sr_ / (BP / 60.0);
        Ring* ring = ring_.load (std::memory_order_acquire);
        ShaperExt* ext = ext_.load (std::memory_order_acquire);
        // ── tp72 — THE TRIGGERS. A Sync lane reads the transport; the other three read their own clock (freePh_), which
        //    runs at the lane's rate from the tempo whether or not the transport plays. MIDI restarts it at the note-on's
        //    sample; Audio restarts it on a transient of the INPUT (fast minus slow envelope over a threshold set by the
        //    card's Sense, 40 ms refractory); Free never restarts.
        const int noteAt = noteAt_; noteAt_ = -1;
        const float sense = S0->sense;
        const float onsetThr = 0.02f + 0.5f * (1.0f - sense) * (1.0f - sense);
        const float aF = 1.0f - std::exp (-1.0f / ((float) sr_ * 0.001f)), aS = 1.0f - std::exp (-1.0f / ((float) sr_ * 0.050f));
        auto lanePhase = [&] (const LaneView& V, int ln, double beat) noexcept -> double
        {
            const double cyc = kShaperRateBeats[V.rate & 7];
            if (V.trig == (int) ShaperTrig::Sync) { double p = beat / cyc; return p - std::floor (p); }
            return freePh_[ln];
        };
        const LaneView& VL = view_[0]; const LaneView& TL = view_[1]; const LaneView& FL = view_[2]; const LaneView& PN = view_[3];
        const LaneView& RP = view_[4]; const LaneView& DR = view_[5]; const LaneView& PH = view_[6]; const LaneView& CR = view_[7];
        const bool anyRing = (TL.on || RP.on) && ring != nullptr;
        // tp79 — the chain, read once per block: which kind sits at each position. Sanitised on the way in, so
        // the per-sample switch can never be handed an index it cannot answer.
        int slot[kShaperLanes]; for (int q = 0; q < kShaperLanes; ++q) slot[q] = S0->slot[q];
        ShaperState::sanitise (slot);
        // filter coefficients per block (the cutoff moves per sample, but the type / resonance per block)
        const float res = 1.0f - 0.95f * FL.L->k[0], kres = 2.0f * res;

        double beat = playing ? hostPpq : 0.0;
        for (int i = 0; i < n; ++i, beat += playing ? pps : 0.0)
        {
            float l = L[i], r = R[i];
            const float dl = l, dr = r;
            // ── the triggers' clocks: a note at this sample, or an onset of the input, restarts the MIDI / Audio lanes ──
            {
                const float rect = std::max (std::fabs (dl), std::fabs (dr));
                envFast_ += aF * (rect - envFast_); envSlow_ += aS * (rect - envSlow_);
                bool onset = false;
                if (refr_ > 0) --refr_;
                else if (envFast_ - envSlow_ > onsetThr) { onset = true; refr_ = (int) (sr_ * 0.040); }
                bool retrigMidi = (noteAt == i), retrigAudio = onset;
                for (int ln = 0; ln < kShaperLanes; ++ln)
                {
                    const LaneView& V = view_[ln]; if (V.trig == (int) ShaperTrig::Sync) continue;
                    const bool rt = (V.trig == (int) ShaperTrig::Midi && retrigMidi) || (V.trig == (int) ShaperTrig::Audio && retrigAudio);
                    if (rt) freePh_[ln] = 0.0;
                    else { double f = freePh_[ln] + pps / kShaperRateBeats[V.rate & 7]; freePh_[ln] = f - std::floor (f); }
                }
            }
            // ── the ring hears the INPUT (Time and Repeat read from it) ──
            if (anyRing) { ring->L[(size_t) ringW_] = l; ring->R[(size_t) ringW_] = r; }

            // ══ tp79 — THE CHAIN IS THE SLOT ORDER ════════════════════════════════════════════════════
            //  Max: "these are in a chain obviously, so these can actually be per-routable … I right click on
            //  Time, boom, Multiband … I want to put Bode second, I want to put Pan third."
            //
            //  Every lane does exactly what it did and still lives under its own name. What changed is that the
            //  eight of them are no longer a FIXED sequence: `slot[]` says which KIND holds each position of the
            //  chain, and the dispatch below runs them in that order, first position first.
            //
            //  ⚠️ THE VIEWS STAY INDEXED BY KIND, NOT BY POSITION. A kind cannot appear twice — placing one that
            //  is already down SWAPS the two positions (Max: "I don't think they're duplicatable … I just put it
            //  in second") — so every lane still owns exactly one set of parameters, one set of DSP state and one
            //  freePh_ clock, all at its own index. That is the whole reason reordering the chain needs no state
            //  surgery: VL / TL / FL / PN / RP / DR / PH / CR below are the same references they always were.
            //
            //  The lambdas are declared inside the sample loop so each body captures l / r / dl / dr / beat / i
            //  exactly as it did when it was written inline, and the bodies are otherwise unchanged from the fixed
            //  sequence they replace. Two of them used to `goto` a label parked at their own tail; inside a lambda
            //  that jump is the plain `return` the label always meant. An inactive lane still costs one branch.
            auto applyTime = [&] ()
            {
                // ── TIME: the read position within the cycle (Gross Beat) ──
                if (TL.on && ring != nullptr)
                {
                    const double cyc = kShaperRateBeats[TL.rate & 7];
                    const double p = lanePhase (TL, 1, beat);
                    const float s = readShape (*TL.L, p);
                    const float rangeMul = (TL.mode == 1 ? 0.5f : TL.mode == 2 ? 2.0f : 1.0f) * std::pow (4.0f, (TL.L->k[2] - 0.5f) * 2.0f);   // Range: the step × the knob (k2: ¼ … ×4, 0.5 = ×1)
                    const double shapedBeats = (double) s * cyc * rangeMul;                 // where in the cycle the shape reads
                    const double nowBeats    = p * cyc;                                     // where the cycle is
                    double behind = (nowBeats - shapedBeats) * (double) TL.depth;           // can only read the PAST
                    if (behind < 0.0) behind = 0.0;
                    double behindF = behind * fpb; const double maxB = (double) (ringFilled_ > 4 ? ringFilled_ - 4 : 0);
                    if (behindF > maxB) behindF = maxB;
                    // tp72 — GLIDE (the Target tab's second knob): instead of cutting to a new read position, the head SLEWS
                    // there at a bounded speed — the transition is heard as a pitch bend (a tape scrub, a Gross Beat
                    // "slide"). At 0 the jump law below cuts as before.
                    const float glide = TL.L->k[1];
                    if (glide > 0.02f)
                    {
                        /* ⚠️ tp79 — GLIDE USED TO DESTROY THE PITCH LAW. It slew-limited the read position's TOTAL motion, but
                           that motion is the whole instrument: the shape's SLOPE is the playback rate, and playback rate is
                           pitch. Clamping the motion clamped the pitch. MEASURED, before: at Glide 0.7 the playback rate sat at
                           0.818 for every shape — slopes 0.75, 0.5 and 0.25 all came out identical, the octave never arrived,
                           and what you heard was the grid moving with the pitch nailed down.
                           Now the target's OWN velocity passes through untouched (that is the pitch) and only the ERROR a
                           discontinuity leaves behind is slew-limited (that is the slide). With nothing jumping the error is
                           zero and the lane tracks the shape exactly; after a jump it closes the gap at a bounded speed, which
                           is the tape scrub Glide was always meant to be. A velocity that is itself a jump — the cycle
                           wrapping, a step drawn into the shape — is not a playback rate, so it goes to the error term. */
                        const double lim = 0.02 + 6.0 * std::pow (1.0 - (double) glide, 3.0);   // samples of CATCH-UP per sample
                        if (tsSlew_ < 0.0) { tsSlew_ = behindF; tsPrevTgt_ = behindF; }
                        double vel = behindF - tsPrevTgt_; tsPrevTgt_ = behindF;
                        if (std::fabs (vel) > 4.0) vel = 0.0;   // a discontinuity is not a playback rate
                        double err = behindF - tsSlew_; if (err > lim) err = lim; else if (err < -lim) err = -lim;
                        tsSlew_ += vel + err; behindF = tsSlew_; tsLastBehind_ = behindF; stepping_ = false;
                    }
                    else { tsSlew_ = -1.0; tsPrevTgt_ = -1.0; }
                    // ── THE JUMP LAW. A step in the shape is a ramp of one table cell (2 ms at a bar, 8 ms at four bars): the
                    //  read position would SCRATCH backwards through it at tens of samples per sample. So the moment `behind`
                    //  moves faster than 4 samples per sample the lane looks a cell and a half AHEAD for the settled target,
                    //  lands there at once, and crossfades from the old stream over ~1 ms. The transient at the start of a
                    //  repeated slice — the whole point of a stutter — is heard, and nothing scratches or clicks.
                    const double dBeh = glide > 0.02f ? 0.0 : behindF - tsLastBehind_; tsLastBehind_ = behindF;
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
                    float tl = rd (ring->L, ring->n, pos, ringW_), tr = rd (ring->R, ring->n, pos, ringW_);
                    if (tsXf_ > 0) { const float w = (float) tsXf_ / (float) tsXfN_; tl = tl * (1 - w) + rd (ring->L, ring->n, tsOld_, ringW_) * w; tr = tr * (1 - w) + rd (ring->R, ring->n, tsOld_, ringW_) * w; tsOld_ += 1.0; --tsXf_; }
                    tsPos_ = pos;
                    l = l + (tl - l) * TL.L->blend; r = r + (tr - r) * TL.L->blend;
                    vizPh_[1] = (float) p; vizV_[1] = s;
                }
            };
            auto applyRepeat = [&] ()
            {
                // ── REPEAT: while the shape is up, a slice from the grid loops; its height = the slice length ──
                if (RP.on && ring != nullptr)
                {
                    const double cyc = kShaperRateBeats[RP.rate & 7];
                    const double p = lanePhase (RP, 4, beat);
                    const float s = readShape (*RP.L, p) * RP.depth;
                    const bool want = s > 0.02f;
                    if (want && ! repHold_)
                    {   // capture at this grid step's start: the slice began at the last grid line
                        const double stepBeats = cyc / (double) (RP.L->grid > 0 ? RP.L->grid : 16);
                        const double sinceStep = std::fmod (beat, stepBeats) * fpb;
                        repStartW_ = ringW_ - (int) sinceStep; repPos_ = 0.0; repRead_ = 0.0; repHold_ = true;
                    }
                    if (want)
                    {   // the slice length: the shape's height on the ladder 1/4 … 1/64 of a beat-cycle (higher = shorter)
                        const int div = 4 << (int) std::floor (s * 4.99f);                 // 4, 8, 16, 32, 64
                        const int len = std::max (32, (int) (cyc * fpb / (double) div));
                        if (len != repLen_) { repLen_ = len; }
                        // tp72 — the Target tab: Decay (k1) makes every pass quieter, Pitch (k2, 0.5 = none) moves every pass by up to
                        // ±6 semitones (the read runs faster or slower through the slice), Reverse (mode 1) plays the slice backwards
                        const int pass = (int) (repPos_ / (double) repLen_);
                        const float semis = (RP.L->k[2] - 0.5f) * 12.0f * (float) pass;
                        const double rate = std::pow (2.0, (double) semis / 12.0);
                        const double inLoop = std::fmod (repRead_, (double) repLen_);
                        const double rpIn = RP.mode == 1 ? (double) repLen_ - 1.0 - inLoop : inLoop;
                        double rp = (double) repStartW_ + rpIn;
                        const float gain = std::pow (1.0f - 0.9f * RP.L->k[1], (float) pass);
                        // a short cosine seam at the loop point keeps it click-free
                        const int seam = std::min (repLen_ / 4, 8 + (int) (120.0f * RP.L->k[0]));   // Seam (k0): the crossfade at the loop point
                        float rl = rd (ring->L, ring->n, rp, ringW_), rr = rd (ring->R, ring->n, rp, ringW_);
                        if (inLoop < seam) { const float w = 0.5f - 0.5f * std::cos ((float) (inLoop / seam) * 3.14159265f); const double rp2 = rp + (RP.mode == 1 ? -repLen_ : repLen_); rl = rl * w + rd (ring->L, ring->n, rp2, ringW_) * (1 - w); rr = rr * w + rd (ring->R, ring->n, rp2, ringW_) * (1 - w); }
                        rl *= gain; rr *= gain;
                        repPos_ += 1.0; repRead_ += rate; if (repRead_ >= (double) repLen_ * (pass + 1)) repRead_ = (double) repLen_ * (pass + 1);   // a pitched pass ends where the unpitched one does
                        const float mixr = RP.L->blend;
                        l = l + (rl - l) * mixr; r = r + (rr - r) * mixr;
                    }
                    else repHold_ = false;
                    vizPh_[4] = (float) p; vizV_[4] = s;
                }
            };
            auto applyDrive = [&] ()
            {
                // ── DRIVE: the rack's 23 distortions (mode = the type), the shape on the drive amount ──
                if (DR.on)
                {
                    const double p = lanePhase (DR, 5, beat);
                    const float s = smoothed (5, readShape (*DR.L, p), DR.L->smooth) * DR.depth;
                    const float tone = DR.L->k[0], makeup = 0.25f + 1.5f * DR.L->k[1];
                    float wl = l, wr = r;
                    const float fadeIn = s < 0.25f ? s * 4.0f : 1.0f;   // a wire at shape 0, whatever the type does at drive 0
                    if (ext != nullptr && ext->drive (0, DR.mode, s, tone, (int) (DR.L->k[2] * 7.99f), DR.L->k[3], DR.L->blend * fadeIn, wl, wr))
                    { l = wl * makeup; r = wr * makeup; vizPh_[5] = (float) p; vizV_[5] = s; return;   /* tp79 — the label this jumped to sat at this block's own tail */ }
                    else
                    {   // the built-in: a tanh family. At shape 0 the lane is a WIRE (tanh alone would already colour a -6 dBFS
                        // sine); the shaped signal fades in over the first quarter of the shape's travel, then the pre-gain does the rest
                        const float g = 1.0f + 18.0f * s;
                        const float mk = 1.0f / std::pow (g, 0.6f * (0.3f + 0.7f * DR.L->k[1]));
                        const int fam = DR.mode >= 5 && DR.mode <= 8 ? 1 : DR.mode >= 13 && DR.mode <= 15 ? 2 : DR.mode <= 4 ? 3 : 0;   // CLIP · FOLD · ANALOG · else soft
                        auto sh = [&] (float x) noexcept { x *= g; switch (fam) { case 1: return x < -1.f ? -1.f : (x > 1.f ? 1.f : x);
                                                                              case 2: return std::sin (x * 0.9f);
                                                                              case 3: return (x < 0 ? -1.f : 1.f) * (1.f - std::exp (-std::fabs (x))) * (1.f + 0.15f * x * x / (1.f + x * x));
                                                                              default: return std::tanh (x); } };
                        const float in = s < 0.25f ? s * 4.0f : 1.0f;
                        wl = l + (sh (l) * mk - l) * in; wr = r + (sh (r) * mk - r) * in;
                        // Tone: a one-pole low shelf of the wet against the knob (0.5 = flat)
                        const float tc = 1.0f - std::exp (-2.0f * 3.14159265f * (400.0f + 12000.0f * tone * tone) / (float) sr_);
                        ch_[0].tone += tc * (wl - ch_[0].tone); ch_[1].tone += tc * (wr - ch_[1].tone);
                        if (tone < 0.5f) { wl = ch_[0].tone + (wl - ch_[0].tone) * (tone * 2.0f); wr = ch_[1].tone + (wr - ch_[1].tone) * (tone * 2.0f); }
                    }
                    l = l + (wl - l) * DR.L->blend; r = r + (wr - r) * DR.L->blend;
                    vizPh_[5] = (float) p; vizV_[5] = s;
                }
            };
            auto applyCrush = [&] ()
            {
                // ── CRUSH: bits + rate built in; the roster's crushers (Bit-Crush, Samp-Hold, Radio) and the distortion's
                //    digital family through the rosters; Tone (k2) is a one-pole low-pass on the wet ──
                if (CR.on)
                {
                    const double p = lanePhase (CR, 7, beat);
                    const float s = smoothed (7, readShape (*CR.L, p), CR.L->smooth) * CR.depth;
                    float wl = l, wr = r; bool done = false;
                    if (CR.mode >= 3 && CR.mode <= 6 && ext != nullptr)
                        done = ext->filter (2, kShaperCrushRoster[CR.mode - 3], 1.0f - 0.9f * s, 0.3f + 0.6f * CR.L->k[0], 0.0f, 1.0f, 0, 0.0f, wl, wr);
                    else if (CR.mode >= 7 && CR.mode <= 9 && ext != nullptr)
                    {
                        done = ext->drive (1, kShaperCrushDist[CR.mode - 7], s, 0.5f + 0.5f * CR.L->k[1], (int) (CR.L->k[0] * 7.99f), 0.5f, CR.L->blend * (s < 0.25f ? s * 4.0f : 1.0f), wl, wr);
                        if (done) { l = wl; r = wr; vizPh_[7] = (float) p; vizV_[7] = s; return;   /* tp79 — the label this jumped to sat at this block's own tail */ }   // the engine mixed its own dry
                    }
                    if (! done)
                    {
                        const int m = CR.mode <= 2 ? CR.mode : 0;
                        const float bits = m == 2 ? 16.0f : 16.0f - (4.0f + 8.0f * CR.L->k[0]) * s; const float q = std::pow (2.0f, bits - 1.0f);
                        const int hold = m == 1 ? 1 : 1 + (int) (s * (2.0f + 30.0f * CR.L->k[1]));   // Mode: Bits + Rate · Bits · Rate
                        if (holdN_ <= 0) { holdL_ = std::round (l * q) / q; holdR_ = std::round (r * q) / q; holdN_ = hold; }
                        --holdN_;
                        wl = holdL_; wr = holdR_;
                    }
                    const float tone = CR.L->k[2];
                    if (tone < 0.98f)
                    {   // the tone rides the shape: at shape 0 the lane stays a wire
                        const float fc = 300.0f * std::pow (60.0f, tone); const float a = 1.0f - std::exp (-6.2831853f * fc / (float) sr_);
                        crushLp_[0] += a * (wl - crushLp_[0]); crushLp_[1] += a * (wr - crushLp_[1]);
                        const float in = s < 0.25f ? s * 4.0f : 1.0f; wl += (crushLp_[0] - wl) * in; wr += (crushLp_[1] - wr) * in;
                    }
                    l = l + (wl - l) * CR.L->blend; r = r + (wr - r) * CR.L->blend;
                    vizPh_[7] = (float) p; vizV_[7] = s;
                }
            };
            auto applyFilter = [&] ()
            {
                // ── FILTER: the rack's roster (mode = the engine, 0 = Ladder LP 24) through the processor's FilterFxEngine; the
                //    shape is the cutoff, 40 Hz → 20 kHz over the depth. Until the engine is armed: a 2-pole SVF low-pass ──
                if (FL.on)
                {
                    const double p = lanePhase (FL, 2, beat);
                    const float s = smoothed (2, readShape (*FL.L, p), FL.L->smooth);
                    const float cut01 = 1.0f - 0.9f * FL.depth * (1.0f - s);   // 20·1000^cut01 Hz: 1 = 20 kHz, 0.1 = 40 Hz
                    float wl = l, wr = r;
                    if (! (ext != nullptr && ext->filter (0, FL.mode, cut01, FL.L->k[0], FL.L->k[1], FL.L->k[2], (int) (FL.L->k[3] * 5.99f), FL.L->k[4], wl, wr)))
                    {
                        const float fc = 20.0f * std::pow (1000.0f, cut01);
                        const float g = std::tan (3.14159265f * std::min (fc, (float) sr_ * 0.45f) / (float) sr_);
                        const float a1 = 1.0f / (1.0f + g * (g + kres)), a2 = g * a1, a3 = g * a2;
                        const int svf = FL.mode == 6 || FL.mode == 2 ? 1 : FL.mode == 7 ? 2 : FL.mode == 8 ? 3 : 0;   // the roster's SVF HP / Ladder HP → high, BP → band, Notch → notch, else low
                        for (int c = 0; c < 2; ++c)
                        {
                            Ch& C = ch_[c]; float x = c ? r : l;
                            if (FL.L->k[1] > 0.0f) x = std::tanh (x * (1.0f + 6.0f * FL.L->k[1])) / (1.0f + FL.L->k[1]);
                            const float v3 = x - C.ic2, v1 = a1 * C.ic1 + a2 * v3, v2 = C.ic2 + a2 * C.ic1 + a3 * v3;
                            C.ic1 = 2 * v1 - C.ic1; C.ic2 = 2 * v2 - C.ic2;
                            float y; switch (svf) { case 1: y = x - kres * v1 - v2; break; case 2: y = v1; break; case 3: y = x - kres * v1; break; default: y = v2; }
                            if (c) wr = y; else wl = y;
                        }
                    }
                    l = l + (wl - l) * FL.L->blend; r = r + (wr - r) * FL.L->blend;
                    vizPh_[2] = (float) p; vizV_[2] = s;
                }
            };
            auto applyPhaser = [&] ()
            {
                // ── PHASER: modes 2.. are the rack roster's phasers / flangers / combs (kShaperPhaserRoster) through the
                //    processor's engine, the shape on their centre; 0 = the built-in 6-stage phaser, 1 = the built-in flanger ──
                bool phDone = false;
                if (PH.on && PH.mode >= 2)
                {
                    const double p = lanePhase (PH, 6, beat);
                    const float s = smoothed (6, readShape (*PH.L, p), PH.L->smooth);
                    float wl = l, wr = r;
                    const int ri = PH.mode - 2 < kShaperPhaserRosterN ? PH.mode - 2 : 0;
                    if (ext != nullptr && ext->filter (1, kShaperPhaserRoster[ri], 0.2f + 0.7f * s * PH.depth, 0.15f + 0.8f * PH.L->k[0], PH.L->k[2], 1.0f, 0, PH.L->k[1], wl, wr))
                    { l = l + (wl - l) * PH.L->blend; r = r + (wr - r) * PH.L->blend; phDone = true; }
                    vizPh_[6] = (float) p; vizV_[6] = s;
                }
                if (PH.on && ! phDone && PH.mode == 1)
                {
                    const double p = lanePhase (PH, 6, beat);
                    const float s = smoothed (6, readShape (*PH.L, p), PH.L->smooth);
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
                else if (PH.on && ! phDone)
                {
                    const double p = lanePhase (PH, 6, beat);
                    const float s = smoothed (6, readShape (*PH.L, p), PH.L->smooth);
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
            };
            auto applyPan = [&] ()
            {
                // ── PAN: position (Power / Linear) or, in Width mode, the stereo width; Width (k0) scales the sides first,
                //    Bass mono (k1) keeps the lows in the middle whatever the shape does ──
                if (PN.on)
                {
                    const double p = lanePhase (PN, 3, beat);
                    const float s = smoothed (3, readShape (*PN.L, p), PN.L->smooth);
                    float wl = l, wr = r;
                    {   // width first: 0.5 = as it is, 1 = twice the sides, 0 = mono
                        const float w = PN.L->k[0] * 2.0f; const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * w; wl = m + sd; wr = m - sd;
                    }
                    if (PN.mode == 2)
                    {   // the shape is the width: 0 = mono, 1 = double
                        const float w = 2.0f * s * PN.depth + (1.0f - PN.depth); const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * w; wl = m + sd; wr = m - sd;
                    }
                    else
                    {
                        const float pan = (s * 2.0f - 1.0f) * PN.depth; float gl, gr;
                        if (PN.mode == 1) { gl = 1.0f - (pan > 0 ? pan : 0); gr = 1.0f + (pan < 0 ? pan : 0); }
                        else { const float th = (pan + 1.0f) * 0.78539816f; gl = std::cos (th) * 1.41421356f; gr = std::sin (th) * 1.41421356f; }
                        wl *= gl; wr *= gr;
                    }
                    // tp74 — Haas (k2): the side the pan leaves is delayed up to 15 ms × the pan, so a swing has depth as well as level
                    haasL_[haasW_] = wl; haasR_[haasW_] = wr;
                    if (PN.L->k[2] > 0.01f && PN.mode != 2)
                    {
                        const float pan = (s * 2.0f - 1.0f) * PN.depth; const float d = PN.L->k[2] * std::fabs (pan) * 0.015f * (float) sr_;
                        const float dd = d > 1000.0f ? 1000.0f : d; const float rp = (float) haasW_ - dd; const int i0 = (int) std::floor (rp); const float f = rp - (float) i0;
                        if (pan > 0) wl = haasL_[(i0 + 2048) & 1023] * (1 - f) + haasL_[(i0 + 1 + 2048) & 1023] * f;
                        else         wr = haasR_[(i0 + 2048) & 1023] * (1 - f) + haasR_[(i0 + 1 + 2048) & 1023] * f;
                    }
                    haasW_ = (haasW_ + 1) & 1023;
                    if (PN.L->k[1] > 0.01f)
                    {   // bass mono: below 40..300 Hz the two sides share one centre
                        const float fc = 40.0f + 260.0f * PN.L->k[1]; const float a = 1.0f - std::exp (-6.2831853f * fc / (float) sr_);
                        bassLp_[0] += a * (wl - bassLp_[0]); bassLp_[1] += a * (wr - bassLp_[1]);
                        const float lm = 0.5f * (bassLp_[0] + bassLp_[1]); wl = wl - bassLp_[0] + lm; wr = wr - bassLp_[1] + lm;
                    }
                    l = l + (wl - l) * PN.L->blend; r = r + (wr - r) * PN.L->blend;
                    vizPh_[3] = (float) p; vizV_[3] = s;
                }
            };
            auto applyVolume = [&] ()
            {
                // ── VOLUME: a gain (Duck mode reads the shape upside down); Attack (k0) and Release (k1) scale the lane's
                //    smoothing for the rising and the falling edge, so a gate can snap open and fall slowly ──
                if (VL.on)
                {
                    const double p = lanePhase (VL, 0, beat);
                    float s = readShape (*VL.L, p); if (VL.mode == 1) s = 1.0f - s;
                    const float gRaw = 1.0f - VL.depth * (1.0f - s);
                    const float base = 0.2f + 250.0f * VL.L->smooth * VL.L->smooth;
                    const float ms = base * (0.1f + 1.9f * (gRaw > smooth_[0] ? VL.L->k[0] : VL.L->k[1]));
                    const float a = std::exp (-1.0f / ((float) sr_ * ms * 0.001f));
                    smooth_[0] = gRaw + (smooth_[0] - gRaw) * a; float g = 1.0f + (smooth_[0] - 1.0f) * VL.L->blend;
                    // tp74 — Punch (k2): every opening of the gate gets a 12 ms overshoot, up to +150 % — the transient a gate is for
                    // (a step in the shape is a ramp of one table cell — ~47 samples at a bar — so the edge is read against a 5 ms lag, 20 ms refractory)
                    volPrev_ += (gRaw - volPrev_) * (1.0f - std::exp (-1.0f / ((float) sr_ * 0.005f)));
                    if (punchRefr_ > 0) --punchRefr_; else if (gRaw - volPrev_ > 0.25f) { punchEnv_ = 1.0f; punchRefr_ = (int) (sr_ * 0.02); }
                    if (VL.L->k[2] > 0.01f) { punchEnv_ *= std::exp (-1.0f / ((float) sr_ * 0.012f)); g *= 1.0f + 1.5f * VL.L->k[2] * punchEnv_ * VL.L->blend; } else punchEnv_ = 0.0f;
                    l *= g; r *= g;
                    vizPh_[0] = (float) p; vizV_[0] = s;
                }
            };
            for (int sl = 0; sl < kShaperLanes; ++sl)
                switch (slot[sl])
                {
                    case 0: applyVolume(); break;
                    case 1: applyTime(); break;
                    case 2: applyFilter(); break;
                    case 3: applyPan(); break;
                    case 4: applyRepeat(); break;
                    case 5: applyDrive(); break;
                    case 6: applyPhaser(); break;
                    case 7: applyCrush(); break;
                    default: break;
                }
            /* ⚠️ tp79 — THE RING'S WRITE HEAD ADVANCES ONCE PER SAMPLE, FOR EVERYONE, OUTSIDE THE DISPATCH.
               It used to sit between the Repeat block and the Drive block at loop level; lifting the blocks into
               lambdas swallowed it into applyRepeat, so the head only moved when the REPEAT lane happened to be
               lit — and the Time lane, reading `ringW_ - behind`, then played at unity rate forever however the
               shape was drawn (measured: playback rate 1.000 at every slope). It belongs to the sample, not to a
               lane, so it lives here now, after every lane has read. */
            if (anyRing) { ringW_ = (ringW_ + 1) % ring->n; if (ringFilled_ < ring->n) ++ringFilled_; }
            L[i] = dl + (l - dl) * mix; R[i] = dr + (r - dr) * mix;
        }
    }

private:
    struct Ring { std::vector<float> L, R; int n = 0; };
    struct Ch { float ic1 = 0, ic2 = 0, tone = 0, phfb = 0; };
    struct AP { float x1 = 0, y1 = 0; };
    /** tp79 — CATMULL-ROM, not a straight line between two samples. Every read this lane makes at a slope other
        than 1 is a RESAMPLE, and 2-point linear is a poor resampler: it is a low-pass whose corner moves with the
        fractional position, so a pitched read comes back dull and grainy and modulates as it travels. Four points
        and a cubic cost a few multiplies, and are the choice fb530 already measured as worth it on full-band
        content. At slope exactly 1 the fraction is 0 and this returns b[i1] — bit-identical to the old read. */
    static float rd (const std::vector<float>& b, int n, double p, int wr) noexcept
    {
        double q = std::fmod (p, (double) n); if (q < 0) q += n;
        const int i1 = (int) q; const float f = (float) (q - i1);
        const int i0 = (i1 == 0) ? n - 1 : i1 - 1, i2 = (i1 + 1 == n) ? 0 : i1 + 1, i3 = (i2 + 1 == n) ? 0 : i2 + 1;
        /* ⚠️ tp79 — A CUBIC READS TWO SAMPLES AHEAD, AND AHEAD OF THE WRITE HEAD THERE IS NOTHING WRITTEN YET.
           i2 / i3 are the ring's future: at the head they hold silence on the first pass and a 16-second-old
           sample after it. Linear only ever touched one of them and only as the fraction approached 1; the cubic
           touches both, so a read sitting ON the head — which is exactly what a unity shape does — started
           interpolating towards stale memory (measured: unity's playback rate read 0.964 instead of 1.000).
           Within two samples of the head there is nothing to resample anyway, so the pair is the linear read,
           which at the head is the written sample itself and keeps unity bit-identical. */
        const int ahead = ((i3 - wr) % n + n) % n;   // 0 when i3 IS the head, small when it is just past it
        if (ahead <= 2) { const float ya = b[(size_t) i1], yb = b[(size_t) i2]; return ya + (yb - ya) * f; }
        const float y0 = b[(size_t) i0], y1 = b[(size_t) i1], y2 = b[(size_t) i2], y3 = b[(size_t) i3];
        const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float a2 = -0.5f * y0 + 0.5f * y2;
        return ((a0 * f + a1) * f + a2) * f + y1;
    }

    struct LaneView { const ShaperLane* L = nullptr; bool on = false; float depth = 1.0f; int rate = kShaperRateDefault, mode = 0, trig = 0; };
    struct Ctl { bool set = false, on = false; float depth = 1.0f; int rate = kShaperRateDefault, mode = 0, trig = 0; };
    LaneView view_[kShaperLanes]; Ctl ctl_[kShaperLanes];
    double sr_ = 48000.0;
    std::atomic<const ShaperState*> state_ { nullptr };
    std::shared_ptr<const ShaperState> stateOwner_[2]; unsigned stateSeq_ = 0;
    std::shared_ptr<Ring> ringOwner_; std::atomic<Ring*> ring_ { nullptr };
    int ringW_ = 0, ringFilled_ = 0;
    Ch ch_[2]; std::vector<AP> phL_, phR_;
    std::array<float, 8> smooth_ {};
    double tsPos_ = 0, tsOld_ = 0, tsLastBehind_ = 0, stepTarget_ = 0, tsPrevTgt_ = -1.0; int tsXf_ = 0, tsXfN_ = 1; bool stepping_ = false;
    float holdL_ = 0, holdR_ = 0; int holdN_ = 0;
    float flL_[2048] = {}, flR_[2048] = {}; int flW_ = 0;   // the Flanger mode's delay line (~43 ms at 48 k)
    bool repHold_ = false; int repLen_ = 0, repStartW_ = 0; double repPos_ = 0, repRead_ = 0;
    double tsSlew_ = -1.0;                       // tp72 — the Time lane's glided read position (-1 = not gliding)
    std::atomic<ShaperExt*> ext_ { nullptr };    // tp72 — the rack's engines, lent by the processor
    double freePh_[8] = {};                      // tp72 — the Free / MIDI / Audio lanes' own clocks
    float envFast_ = 0, envSlow_ = 0; int refr_ = 0, noteAt_ = -1;
    float crushLp_[2] = {}, bassLp_[2] = {}, volEnv_ = 1.0f, volPrev_ = 1.0f, punchEnv_ = 0.0f; int punchRefr_ = 0;
    float haasL_[1024] = {}, haasR_[1024] = {}; int haasW_ = 0;   // tp74 — the Pan lane's Haas delay
    float vizPh_[8] = {}, vizV_[8] = {};
};
} // namespace wc
