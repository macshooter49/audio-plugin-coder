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
/** tp80 — KINDS AND POSITIONS ARE NO LONGER THE SAME NUMBER.
    There are eight POSITIONS in the chain (eight tiles on the card) and seventeen KINDS a position can hold.
    Max: "we're going to make each of these effects available to shape … reverb, tape, widen, multiband,
    granular, delay, bode, chorus … and a noise engine too, we already have our noise sample library."
    A kind still cannot appear twice, so each one keeps exactly one set of parameters, one set of DSP state and
    one clock — everything stays indexed by KIND, and `slot[]` just says which eight are in the chain and in
    what order. The nine that are not placed cost their (small) lane struct and nothing else: their engines are
    armed lazily, only when a lane of that kind is actually lit (the tp63 law — Delay, Granular and Bode each
    carry ~8.4 MB of ring at 48 k and a card drawing a volume gate must never pay for them). */
static constexpr int kShaperSlots = 8;           // positions in the chain
static constexpr int kShaperLanes = 18;          // KINDS (one lane struct, one parameter set, one clock each)
static constexpr int kShaperT     = 2048;        // the baked table: one cycle, end to end (index kShaperT = the value AT the cycle's end)
enum class ShaperLaneId : int { Volume = 0, Time, Filter, Pan, Repeat, Drive, Phaser, Crush,
                                Reverb, Delay, Chorus, Widen, Multi, Tape, Grain, Bode, Noise, Flanger };
/* tp91 — FLANGER, a lane of its own. Max: "flanger is a whole different beast than a phaser and it deserves its
   own dedicated panel." It is a LEND like the other borrowed kinds — the rack card's TerrainFlangerFx, thirty-two of
   its voicings (Tape Zero · Jet · BBD · Endless, eight characters each) — and like ShaperBox's LiquidShaper the
   drawn line is the SWEEP (the comb's Centre), fed through TerrainFlangerFx::setSweep, never a send. Targets:
   Feedback (bipolar, 0.5 = none) · Centre · Range · Stereo. The Phaser lane's own flange types stay where saved
   patches expect them. */
/* tp82 — NOISE closes the roster, and it is the one that was not a lend: there was no standalone noise engine
   to borrow, because the generator lived inside SynthVoice's render. It was lifted into TerrainNoise.h, which
   the instrument and this lane now share (one generator, not two), and Source/TerrainNoise_test.cpp proves the
   move changed nothing bit for bit across all thirteen colours.
   ⚠️ It is also the only kind that ADDS signal instead of processing it — the shape is the noise's LEVEL. */

// the rate ladder — one cycle of the shape, in beats (4 = 1 bar)
static constexpr float kShaperRateBeats[8] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };
static constexpr int   kShaperRateN = 8;
static constexpr int   kShaperRateDefault = 4;   // 1 bar
/* tp89 — a dry+wet sum halved loses ~3 dB on broadband material (the notches are real cancellation),
   so this is the makeup that puts the Phaser lane back at unity. CALIBRATED against the level audit,
   not guessed: the lane has to read 0.00 dB against the same render with it dark. */
static constexpr float kPhMakeup = 0.85f;   // measured: unscaled +7.45 dB · halved +1.43 dB · x0.85 lands on 0

/* 🚨 tp90 — THE LOUDNESS LAW, PER TYPE. Max: "nothing should be making anything quieter or louder … unless
   they're shaped, or the volume knob or the drive knob." Every type of a lane is its own engine voicing, so
   each one sits at its own level: the audit (Tests/au_shaper_level.cpp) measured the Phaser's 28 types from
   -10.6 to +5.8 dB, the Tape's from -15.5 to -0.2, the Chorus's every one below -2. This table is the trim
   that puts each back at unity (Multiband: at its +5.5 dB push, the same push for every type).

   ⚠️ CALIBRATED, NOT GUESSED, AND ON TWO SOURCES. Each entry is minus the MEAN of what the type did to a held
   chord and to broadband noise, knobs at rest, shape open. One source is not enough: a sustained sine and
   its own echo partly cancel, so the same Delay read -3.07 dB on a sine chord and +2.88 on noise.
   ⚠️ The trim rides the amount of the effect that is IN (in dB, so half the send is half the trim), which
   keeps a closed shape a wire. Lanes that are not here are exempt by design — see the audit's header.
   Regenerate with Tests/au_shaper_level.cpp's CAL lines; never hand-edit one number. */
static constexpr int kShaperTrimTypes = 32;   // tp91 — the Flanger lane's 32 voicings
static constexpr float kShaperTrimDb[kShaperLanes][kShaperTrimTypes] = {
    /* TRIM-TABLE-BEGIN */
    {},
    {},
    {},
    {},
    {},
    {},
    { 0.38f, 2.44f, -0.90f, -0.48f, 0.15f, 2.29f, 5.93f, 6.78f, 3.64f, 2.91f, 6.50f, 6.03f, 5.32f, 4.26f, 3.71f, 3.54f, 3.81f, 4.07f, -0.64f, 2.46f, -3.24f, 4.12f, -3.25f, -3.21f, -3.21f, -4.81f, -0.64f, 0.65f },
    { -0.02f, 0.04f, 0.20f, 7.83f, 11.23f, -2.59f, 6.03f, 4.36f, 2.84f, 12.45f },
    { 1.31f, -2.16f, -2.29f, -4.15f },
    { 0.20f, -0.14f, -0.15f, 0.86f },
    { 5.33f, 5.75f, 6.85f, 6.30f, 5.67f, 4.21f, 5.85f, 9.74f },
    { 0.10f, 3.45f, 0.41f, 0.61f, 0.32f, 1.37f },
    { -0.88f, 3.29f, 1.02f, -5.41f, 0.50f },
    { 6.37f, 13.12f, 12.50f, 17.56f, 4.01f },
    { -0.50f, -0.37f, -0.47f, -0.97f, 0.09f, -0.98f, -1.15f, -0.67f },
    { 0.51f, 1.61f, 0.75f, 4.29f, 0.95f, 2.98f, 4.55f, -1.82f },
    {},
    { 2.80f, -0.37f, 2.50f, 3.19f, 1.84f, 3.84f, 2.89f, 3.00f, -1.11f, -1.19f, -1.31f, 2.52f, -0.70f, -1.33f, -2.07f, -0.61f, 3.26f, 2.18f, 3.98f, 4.01f, 3.28f, 2.43f, 4.49f, 5.38f, 1.92f, 2.04f, 1.26f, 1.21f, 1.87f, 2.47f, 0.92f, -0.67f }
    /* TRIM-TABLE-END */
};
inline float shaperTrim (int lane, int mode, float amt) noexcept
{
    if (lane < 0 || lane >= kShaperLanes || mode < 0 || mode >= kShaperTrimTypes) return 1.0f;
    const float db = kShaperTrimDb[lane][mode];
    if (db == 0.0f) return 1.0f;
    const float a = amt < 0.0f ? 0.0f : (amt > 1.0f ? 1.0f : amt);
    return std::exp (db * a * 0.115129255f);   // 10^(dB·a/20)
}

enum class ShaperTrig : int { Sync = 0, Free, Audio, Midi };

/** tp72 — the rack's engines, lent to the lanes. The processor implements this; a slot that is not armed yet
    returns false and the lane runs its built-in fallback. `which`: filter slots 0 = Filter lane, 1 = Phaser lane,
    2 = Crush lane; drive slots 0 = Drive lane, 1 = Crush lane. All calls are per sample, audio thread. */
struct ShaperExt
{
    virtual ~ShaperExt() = default;
    /** tp81 — `punch` is FilterFxEngine's own envelope kick, which the Filter lane never reached; it is that
        lane's fourth target. Nothing new was written for it — the engine has always had it. */
    virtual bool filter (int which, int engine, float cut01, float res, float drive, float poles, int charIdx, float spread, float punch, float& l, float& r) noexcept = 0;
    /** mix: the lane's wet/dry — the rack's distortion delays its wet by its resampler and aligns the dry INSIDE, so the
        engine owns the mix and the lane replaces (a crossfade outside would comb). The lane passes blend × fade-in. */
    /** tp81 — `knee` is DistortionEngine::setKnee, the clipper's corner. Same story: already there, never reached,
        and it is the Drive lane's fourth target. */
    virtual bool drive  (int which, int mode, float drive01, float tone, int character, float bias, float knee, float mix, float& l, float& r) noexcept = 0;
    /** tp80 — ONE DOOR FOR EVERY BORROWED RACK EFFECT. Max: "we're going to make each of these effects available
        to shape … I just want you to make mini versions of these that I can shape."

        Rather than a virtual per engine, the lane hands over what it knows and the PROCESSOR — which is where the
        engines live — decides what to drive with it. That keeps this header free of any engine's Params struct
        (so the offline proof still builds with no JUCE) and makes a new kind eight lines here instead of a block.

        `kind`  ShaperLaneId — which engine
        `s`     the shape's read value, 0..1, already smoothed and scaled by the lane's Depth. This is THE
                rhythm: it drives the one control that makes the effect move (a reverb's send, a delay's throw,
                a chorus's depth, a shifter's interval), and the processor owns that mapping.
        `k`     the Target tab's knobs, k[0..3] — four per kind, Max: "I want everyone to have exactly four".
        `mode`  the lane's type choice (a reverb type, a tape machine, a noise colour).
        `blend` the lane's own wet/dry, on its own. ⚠️ tp81 — this used to arrive ALREADY multiplied by the shape,
                which quietly forced every kind to spend the shape on its wet amount. That is right for a reverb
                or a delay (the shape is the send) and wrong for a frequency shifter, where the shape wants to be
                the INTERVAL while the wet level stays put. The two arrive apart now and each case decides.
                THE ENGINE STILL OWNS THE MIX and returns l/r fully blended, exactly as drive() does and for the
                same reason: several of these align their dry internally, so a crossfade out here would comb.
        Returns false when that engine has not been armed yet, and the lane passes the audio through untouched. */
    virtual bool fx (int kind, float s, const float* k, int mode, float blend, float& l, float& r) noexcept = 0;
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
    /* ⚠️ tp81 — A NEW TARGET MUST BOOT NEUTRAL. Hold at its old 0.5 held the gate open and every Volume bar in
       the offline proof went red at once: the knob was new, but the LANE was not, and a default is behaviour.
       Volume Hold 0 (off) · Filter Punch 0 (off) · Phaser Centre 0.2 (= the 200 Hz it always swept from) ·
       Crush Stereo 0 (both channels on one counter, the old mono grit) · the Tone / Tilt pair 0.5 (flat). */
    { 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f }, { 0.3f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f }, { 0.3f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f },
    { 0.3f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.0f, 0.2f, 0.5f, 0.5f }, { 0.5f, 0.5f, 1.0f, 0.0f, 0.5f, 0.5f },
    /* tp80 — the nine borrowed lanes, four targets each (see ShaperExt::fx):
       Reverb  Size · Decay · Tone · Diffuse      Delay  Time · Feedback · Tone · Width
       Chorus  Rate · Depth · Feedback · Voice   Widen  Amount · Width · Rate · Axis       Multi  Speed · Lift · Character · Range
       Tape    Flutter · Drive · Age · Width      Grain  Size · Density · Pitch · Spread    Bode   Range · Feedback · Spread · Blur
       Noise   Tone · Width · Scan · Drive */
    { 0.45f, 0.5f, 0.4f, 0.7f, 0.5f, 0.5f },  { 0.375f, 0.35f, 0.5f, 0.6f, 0.5f, 0.5f },
    { 0.35f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f },  { 0.5f, 0.5f, 0.35f, 0.5f, 0.5f, 0.5f },   { 0.5f, 0.25f, 0.0f, 0.5f, 0.5f, 0.5f },
    { 0.3f, 0.3f, 0.3f, 0.2f, 0.5f, 0.5f },   { 0.25f, 0.4f, 0.5f, 0.5f, 0.5f, 0.5f },   { 0.5f, 0.3f, 0.6f, 0.0f, 0.5f, 0.5f },
    { 0.6f, 0.6f, 1.0f, 0.0f, 0.5f, 0.5f },
    /* tp91 — Flanger  Feedback · Centre · Range · Stereo. Feedback 0.75 is +35 % regeneration on the engine's bipolar
       scale: the jet is audible from the first draw, and 0.5 is there for a dry comb. */
    { 0.75f, 0.5f, 0.6f, 0.3f, 0.5f, 0.5f } };
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
    int slot[kShaperSlots] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    ShaperState() { for (int ln = 0; ln < kShaperLanes; ++ln) for (int q = 0; q < 6; ++q) lanes[ln].k[q] = kShaperKDefault[ln][q]; }
    /** A blob can be old, partial or hand-edited, and this array INDEXES A SWITCH on the audio thread. Anything
        that is not eight DISTINCT kinds in range falls back to the first eight. */
    static void sanitise (int* sl) noexcept
    {
        bool seen[kShaperLanes] = {}; bool ok = true;
        for (int i = 0; i < kShaperSlots; ++i)
        { const int v = sl[i]; if (v < 0 || v >= kShaperLanes || seen[v]) { ok = false; break; } seen[v] = true; }
        if (! ok) for (int i = 0; i < kShaperSlots; ++i) sl[i] = i;
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
        repHold_ = false; repLen_ = 0; repPos_ = 0; repStartW_ = 0; repRead_ = 0.0; repLastP_ = 0.0; tsSlew_ = -1.0;
        ringW_ = 0; ringFilled_ = 0; soundAge_ = 0; silentRun_ = 0; tsRatio_ = 1.0; tsPrevBehind_ = -1.0; tsRate_ = 1.0;
        blInit();   // tp88 — the band-limit table, built off the audio thread
        for (auto& f : freePh_) f = 0.0; envFast_ = envSlow_ = 0.0f; refr_ = 0; noteAt_ = -1;
        for (auto& c : crushLp_) c = 0.0f; for (auto& b : bassLp_) b = 0.0f; volEnv_ = 1.0f; volPrev_ = 1.0f; punchEnv_ = 0.0f; punchRefr_ = 0; fltPrev_ = 0.0f; fltPunch_ = 0.0f; fltRefr_ = 0;
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

    /** tp83 — THE CHAIN IS PARAMETERS NOW. It used to travel only in the shaperJson blob, which meant the host
        could not automate it and — the reason this moved — no offline harness could PLACE a kind, so none of the
        borrowed effects could be certified by ear-equivalent measurement. Eight choice parameters, pushed here
        every block exactly as the lane controls are. Until something pushes, the snapshot's own order stands. */
    void setSlots (const int* s) noexcept
    { for (int q = 0; q < kShaperSlots; ++q) slotCtl_[q] = s[q]; ShaperState::sanitise (slotCtl_); slotSet_ = true; }

    /** Audio thread, per block: the lane's parameters (On / Depth / Rate / Mode) override the snapshot's. */
    void setLaneCtl (int lane, bool on, float depth, int rate, int mode, int trig = 0) noexcept
    { /* ⚠️ tp80 — THIS WAS `ctl_[lane & 7]`. The mask was sized to the eight lanes that existed when it was
         written, so the moment the roster grew the five new lanes wrapped straight onto the first five and wrote
         their own (OFF) state over Volume, Time, Filter, Pan and Repeat — the whole Shaper went inert while every
         parameter still read correct. A mask is not a bounds check; it silently aliases instead of rejecting.
         (The same shape of bug sat in ShaperRoster::filter's `fltLive[which & 3]`, fixed at tp79.) */
      if ((unsigned) lane >= (unsigned) kShaperLanes) return;
      auto& c = ctl_[lane]; c.set = true; c.on = on; c.depth = depth; c.rate = rate; c.mode = mode; c.trig = trig; }
    // ── viz: the phase each lane is at (0..1) and the value it read, for the screen's playhead ──
    float vizPhase (int lane) const noexcept { return vizPh_[(unsigned) lane < (unsigned) kShaperLanes ? lane : 0]; }
    float vizValue (int lane) const noexcept { return vizV_[(unsigned) lane < (unsigned) kShaperLanes ? lane : 0]; }

    // tp72 — the lane's Smooth (0.2 → 40 ms one-pole) on the value every shaped lane reads; the Volume lane has its own
    // attack / release law below, the Time and Repeat lanes read the raw shape (their jumps are the point)
    float smoothed (int ln, float v, float smooth) noexcept
    {
        if (smooth <= 0.005f) { smooth_[ln] = v; return v; }
        const float ms = 0.2f + 250.0f * smooth * smooth; const float a = std::exp (-1.0f / ((float) sr_ * ms * 0.001f));   // tp74 — up to 250 ms: heard, not implied
        smooth_[ln] = v + (smooth_[ln] - v) * a; return smooth_[ln];
    }
    // ── the shape reader: a lane's shape at a phase, with its phase offset, swing and tension ──
    /* 🚨 tp95 — THE TIME LANE'S READ HEAD WAS JITTERING, AND JITTER IS THE "BITCRUSH". Max: "time depth at 100%
       still has that weird bitcrushed sound when there's ramps." The offset is shape × cycle × Range × Depth, and
       the shape came back as a 32-bit float: at a 4-bar cycle that multiplies its last-digit rounding by up to
       384 000 samples — ~0.01 of a sample of RANDOM read-head motion every sample. Read-head jitter is phase
       noise: MEASURED −47 dB of grit under a 5 kHz tone on a 4-bar ramp, −67 dB on a 1-beat one (the cycle
       length was the tell), and it scaled with Depth, which is why 100 sounded crushed and 50 cleaner. The
       Time lane reads its shape here in DOUBLE, end to end; every other lane keeps the float read below. */
    static double readShapeD (const ShaperLane& L, double ph) noexcept
    {
        ph -= std::floor (ph);
        if (L.swing > 0.0f) { const double s = L.swing * 0.45; ph = ph < 0.5 ? ph * (1.0 + s) : 0.5 * (1.0 + s) + (ph - 0.5) * (1.0 - s); }
        double ph2 = ph + (double) L.phase; ph2 -= std::floor (ph2);
        const double x = ph2 * kShaperT; int i = (int) x; if (i > kShaperT - 1) i = kShaperT - 1; const double f = x - i;
        double v = (double) L.table[i] + ((double) L.table[i + 1] - (double) L.table[i]) * f;
        const double tn = L.tension;
        if (tn < 0.5)      v = std::pow (v, 1.0 + (0.5 - tn) * 3.0);
        else if (tn > 0.5) v = 1.0 - std::pow (1.0 - v, 1.0 + (tn - 0.5) * 3.0);
        return v < (double) L.floor_ ? (double) L.floor_ : v;
    }
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
        int slot[kShaperSlots];
        for (int q = 0; q < kShaperSlots; ++q) slot[q] = slotSet_ ? slotCtl_[q] : S0->slot[q];
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
            /* 🚨 tp87 — HOW MUCH SOUND IS IN THE RING, not how many samples were written. Max: "it's
               supposed to either shift pitches or stutter, right now it's just doing a bunch of DROPOUT
               CLICKS."  MEASURED and reproduced: on his 16-tread shape the first three seconds of a phrase
               were 2.5 s of SILENCE in pieces — the lane was reaching back past the moment the sound
               started and faithfully replaying the nothing that was there. `ringFilled_` could not catch
               it because it counts samples WRITTEN, and an instrument that has been sitting open has a ring
               completely full of silence. `soundAge_` is the age of the oldest sample that belongs to the
               sound now playing, so the offset can be held inside it and the lane chops what you are
               ACTUALLY playing from the first note.
               ⚠️ A short gap is part of the music — a drum loop is mostly gaps — so only a silence run
               longer than a second counts as "the sound has not started"; anything shorter keeps counting. */
            {
                const float lvl = std::max (std::fabs (l), std::fabs (r));
                if (lvl > 1.0e-4f) silentRun_ = 0;
                else if (silentRun_ < (int) sr_ * 2) ++silentRun_;
                if (silentRun_ > (int) sr_) soundAge_ = 0;                       // a second of nothing: the sound has not started
                else if (ring != nullptr && soundAge_ < ring->n) ++soundAge_;
            }

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
                    const double s = readShapeD (*TL.L, p);   // tp95 — double: see readShapeD
                    const double rangeMul = (TL.mode == 1 ? 0.5 : TL.mode == 2 ? 2.0 : 1.0) * std::pow (4.0, ((double) TL.L->k[2] - 0.5) * 2.0);   // Range: the step × the knob (k2: ¼ … ×4, 0.5 = ×1)
                    /* 🚨 tp86 — THE SHAPE IS THE TIME OFFSET, NOT AN ABSOLUTE POSITION IN THE CYCLE.
                       ShaperBox 3's TimeShaper (and Gross Beat) read the vertical as HOW FAR BACK FROM NOW: the
                       top line is the present, so a FLAT line plays NORMALLY, a line falling at the grey
                       guideline's gradient is STOPPED, and a RISING line plays FASTER.
                       rate = 1 + slope × Range, straight out of d(offset)/dt.

                       ⚠️ Until tp86 this lane read the shape as the absolute position WITHIN THE CYCLE
                       (`behind = nowBeats − s·cyc·Range`). That re-anchors the read head to NOW at every cycle
                       boundary — `nowBeats` is zero there, so `behind` could only come out ≤ 0 and was clamped to
                       the head. MEASURED, and it is exactly what Max heard: a constant delay decayed into a
                       freeze (the best drawable "play normally, one beat late" averaged **0.755×** rather than
                       1.000×, because the first beat of every cycle was frozen), and **the average speed over a
                       cycle could never exceed 1.000×** however the shape was drawn — so ShaperBox's sustained
                       200% riser was not expressible at all. One re-anchoring caused both.
                       MEASURED AFTER: flat at the top 1.000× with no lag · flat at half 1.000× held a constant
                       half-cycle behind · rising 0→1 a sustained **2.000×** · falling at the guideline 0.000×. */
                    double behind = (1.0 - s) * cyc * rangeMul * (double) TL.depth;   // 1 = now · 0 = a full Range ago
                    if (behind < 0.0) behind = 0.0;                                             // the future is still unreadable
                    double behindF = behind * fpb;
                    /* tp87 — the wall is whichever is smaller: what the ring HOLDS, and what of it is the
                       sound now playing. Past either, the lane plays the PRESENT (tp86) rather than
                       freezing — so a phrase starts chopping immediately and fills in as it gets history. */
                    const double maxB = std::min ((double) (ringFilled_ > 4 ? ringFilled_ - 4 : 0), (double) soundAge_);
                    /* 🚨 tp86 — AN OFFSET THE BUFFER CANNOT MEET GOES LIVE, IT DOES NOT FREEZE AT THE EDGE.
                       Max: "I press play and it drops out and then it comes in two bars later — I hate that
                       shit, ShaperBox doesn't do that, it's very reactive."  He is right and this was the
                       cause. Clamping to `maxB` pins the read to the OLDEST sample in the ring, and the oldest
                       sample does not move — the read position stands still while the write head runs away
                       from it, so the whole first bar was a frozen near-DC smear. MEASURED: a constant
                       half-bar offset rendered -24.8 dBFS for exactly one bar and then snapped to -9.0.
                       Falling back to the PRESENT keeps the audio playing at full level and full speed until
                       there is enough history to honour the shape, and the jump law crossfades the one step
                       where the effect engages. Nothing ever drops out. */
                    if (behindF > maxB) behindF = 0.0;
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
                            const double s2 = readShapeD (*TL.L, p2);
                            double bt = (1.0 - s2) * cyc * rangeMul * (double) TL.depth; if (bt < 0.0) bt = 0.0;   // tp86 — the same offset model, or the jump lands somewhere else
                            stepTarget_ = (bt * fpb > maxB) ? 0.0 : bt * fpb;   // tp86 — the same rule for the jump's target: live, never frozen at the edge
                        }
                        behindF = stepTarget_;
                    }
                    else
                    {
                        stepping_ = false;
                        if (tsXf_ <= 0 && std::fabs (((double) ringW_ - behindF) - (tsPos_ + 1.0)) > 6.0)
                        { tsOld_ = tsPos_ + 1.0; tsXfN_ = std::max (48, (int) (sr_ * 0.001 * (0.5 + TL.L->k[0] * 4.5))); tsXf_ = tsXfN_; }
                    }
                    /* tp88 — THE PLAYBACK RATE, for the band-limit. It is exactly how fast the read walks
                       the source: 1 minus the change in `behind` per sample. A JUMP is not a rate (the jump
                       law owns those), so a step leaves the ratio where it stood, and the ratio is smoothed
                       so a new tread does not switch kernels with a click. ⚠️ Max found this by ear before
                       the maths did: "I turned the depth down to 50, it sounds WAY better, 100 sounds off."
                       Depth scales the offset, so it scales the RATE — a full rise is 2x at depth 100 and
                       1.5x at 50, and half the speed-up is a quarter of the folding. */
                    if (tsPrevBehind_ < -0.5) tsPrevBehind_ = behindF;
                    { const double dRate = behindF - tsPrevBehind_; tsPrevBehind_ = behindF;
                      if (std::fabs (dRate) <= 4.0)
                      { const double r = std::fabs (1.0 - dRate);
                        const double want = r < 1.0 ? 1.0 : (r > 8.0 ? 8.0 : r);
                        tsRatio_ += (want - tsRatio_) * 0.01;
                        tsRate_  += ((r > 8.0 ? 8.0 : r) - tsRate_) * 0.01; } }   // tp95 — the same rate, NOT clamped at unity
                    /* tp95 — AND BELOW UNITY THE READ UPSAMPLES, which leaves IMAGES. tp88 band-limited only the fast side;
                       a slowing ramp (a tape stop, a half-speed fall) went through the cubic, whose images fold back
                       inharmonic: MEASURED −49 dB at 0.25x and −54 dB at 0.5x under a 5 kHz tone. The same windowed sinc
                       at a hair past unity (cutoff ~21.8 kHz) is an interpolator that rejects them. Within ±2 % of normal
                       speed the cubic stays: there is nothing to fold, and it costs a fifth as much. */
                    const bool slowBL = tsRate_ < 0.98;
                    const double blR = slowBL ? 1.1 : tsRatio_;
                    const double pos = (double) ringW_ - behindF;
                    float tl, tr;
                    if (tsRatio_ > 1.02 || slowBL)   // above unity the read DECIMATES and below it UPSAMPLES: both are band-limited
                    { tl = rdBL (ring->L, ring->n, behindF, ringW_, blR, maxB);
                      tr = rdBL (ring->R, ring->n, behindF, ringW_, blR, maxB); }
                    else
                    { tl = rd (ring->L, ring->n, pos, ringW_); tr = rd (ring->R, ring->n, pos, ringW_); }
                    if (tsXf_ > 0)
                    { const float w = (float) tsXf_ / (float) tsXfN_;
                      const double obh = (double) ringW_ - tsOld_;
                      const float ol = (tsRatio_ > 1.02 || slowBL) ? rdBL (ring->L, ring->n, obh, ringW_, blR, maxB) : rd (ring->L, ring->n, tsOld_, ringW_);
                      const float orr = (tsRatio_ > 1.02 || slowBL) ? rdBL (ring->R, ring->n, obh, ringW_, blR, maxB) : rd (ring->R, ring->n, tsOld_, ringW_);
                      tl = tl * (1 - w) + ol * w; tr = tr * (1 - w) + orr * w; tsOld_ += 1.0; --tsXf_; }
                    /* tp81 — TONE (k3), the Time lane's fourth target: a one-pole on what the head reads. Below
                       0.5 it darkens the repeat, above it lifts the top back out. 0.5 is exactly flat. */
                    {
                        const float tn = TL.L->k[3];
                        if (std::fabs (tn - 0.5f) > 0.01f)
                        {
                            const float fc = 250.0f + 14000.0f * tn * tn;
                            const float a = 1.0f - std::exp (-2.0f * 3.14159265f * fc / (float) sr_);
                            tmLp_[0] += a * (tl - tmLp_[0]); tmLp_[1] += a * (tr - tmLp_[1]);
                            if (tn < 0.5f) { tl = tmLp_[0]; tr = tmLp_[1]; }
                            else { const float g = (tn - 0.5f) * 3.0f; tl += (tl - tmLp_[0]) * g; tr += (tr - tmLp_[1]) * g; }
                        }
                    }
                    tsPos_ = pos;
                    l = l + (tl - l) * TL.L->blend; r = r + (tr - r) * TL.L->blend;
                    vizPh_[1] = (float) p; vizV_[1] = (float) s;
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
                    /* 🚨 tp90 — A SHAPE HELD UP ACROSS THE LOOP POINT NEVER LET GO. Capture only happened on the
                       rising edge of `want`, so a flat-top shape captured ONE slice and looped it forever — and
                       if the lane lit in the silence before the first note, that slice was silence: the level
                       audit read the whole lane at -226 dB on a held chord. The grid restarts at every cycle, so
                       the slice does too: a wrap of the lane's phase is a fresh capture. */
                    const bool wrapped = p < repLastP_ - 0.5; repLastP_ = p;
                    if (want && (! repHold_ || wrapped))
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
                        /* tp88 — REPEAT PITCHES THE SLICE TOO, and a pitched-UP pass is the same decimation
                           the Time lane was folding on. Here the rate is known outright (2^(semis/12)), so
                           the band-limit needs no estimate at all. Below unity the cubic is still right. */
                        const double rBL = rate > 1.02 ? rate : 0.0;
                        const double maxBh = (double) (ringFilled_ > 4 ? ringFilled_ - 4 : 0);
                        float rl, rr;
                        if (rBL > 0.0) { rl = rdBL (ring->L, ring->n, (double) ringW_ - rp, ringW_, rBL, maxBh);
                                         rr = rdBL (ring->R, ring->n, (double) ringW_ - rp, ringW_, rBL, maxBh); }
                        else           { rl = rd (ring->L, ring->n, rp, ringW_); rr = rd (ring->R, ring->n, rp, ringW_); }
                        if (inLoop < seam) { const float w = 0.5f - 0.5f * std::cos ((float) (inLoop / seam) * 3.14159265f); const double rp2 = rp + (RP.mode == 1 ? -repLen_ : repLen_); const float s2l = rBL > 0.0 ? rdBL (ring->L, ring->n, (double) ringW_ - rp2, ringW_, rBL, maxBh) : rd (ring->L, ring->n, rp2, ringW_); const float s2r = rBL > 0.0 ? rdBL (ring->R, ring->n, (double) ringW_ - rp2, ringW_, rBL, maxBh) : rd (ring->R, ring->n, rp2, ringW_); rl = rl * w + s2l * (1 - w); rr = rr * w + s2r * (1 - w); }
                        rl *= gain; rr *= gain;
                        repPos_ += 1.0; repRead_ += rate; if (repRead_ >= (double) repLen_ * (pass + 1)) repRead_ = (double) repLen_ * (pass + 1);   // a pitched pass ends where the unpitched one does
                        /* tp81 — TONE (k3), the Repeat lane's fourth target: a one-pole on what the head reads. Below
                           0.5 it darkens the slice, above it lifts the top back out. 0.5 is exactly flat. */
                        {
                            const float tn = RP.L->k[3];
                            if (std::fabs (tn - 0.5f) > 0.01f)
                            {
                                const float fc = 250.0f + 14000.0f * tn * tn;
                                const float a = 1.0f - std::exp (-2.0f * 3.14159265f * fc / (float) sr_);
                                rpLp_[0] += a * (rl - rpLp_[0]); rpLp_[1] += a * (rr - rpLp_[1]);
                                if (tn < 0.5f) { rl = rpLp_[0]; rr = rpLp_[1]; }
                                else { const float g = (tn - 0.5f) * 3.0f; rl += (rl - rpLp_[0]) * g; rr += (rr - rpLp_[1]) * g; }
                            }
                        }
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
                    if (ext != nullptr && ext->drive (0, DR.mode, s, tone, (int) (DR.L->k[2] * 7.99f), DR.L->k[3], DR.L->k[4], DR.L->blend * fadeIn, wl, wr))
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
                        done = ext->filter (2, kShaperCrushRoster[CR.mode - 3], 1.0f - 0.9f * s, 0.3f + 0.6f * CR.L->k[0], 0.0f, 1.0f, 0, 0.0f, 0.0f, wl, wr);
                    else if (CR.mode >= 7 && CR.mode <= 9 && ext != nullptr)
                    {
                        done = ext->drive (1, kShaperCrushDist[CR.mode - 7], s, 0.5f + 0.5f * CR.L->k[1], (int) (CR.L->k[0] * 7.99f), 0.5f, 0.5f, CR.L->blend * (s < 0.25f ? s * 4.0f : 1.0f), wl, wr);
                        if (done) { const float tg = shaperTrim (7, CR.mode, s * CR.L->blend);   // tp90 — the crush is IN as far as the shape is
                                    l = wl * tg; r = wr * tg; vizPh_[7] = (float) p; vizV_[7] = s; return;   /* tp79 — the label this jumped to sat at this block's own tail */ }   // the engine mixed its own dry
                    }
                    if (! done)
                    {
                        const int m = CR.mode <= 2 ? CR.mode : 0;
                        const float bits = m == 2 ? 16.0f : 16.0f - (4.0f + 8.0f * CR.L->k[0]) * s; const float q = std::pow (2.0f, bits - 1.0f);
                        const int hold = m == 1 ? 1 : 1 + (int) (s * (2.0f + 30.0f * CR.L->k[1]));   // Mode: Bits + Rate · Bits · Rate
                        /* tp81 — STEREO (k3), the Crush lane's fourth target: the right channel holds on its OWN
                           counter, up to twice as long, so the aliasing sits across the image instead of dead
                           centre. At 0 both counters are the same number and this is bit-for-bit the old mono
                           grit. (The two channels were sharing holdN_, which is why crush had no width at all.) */
                        const int holdR = 1 + (int) ((float) (hold - 1) * (1.0f + CR.L->k[3]));
                        if (holdN_  <= 0) { holdL_ = std::round (l * q) / q; holdN_  = hold;  }
                        if (holdNR_ <= 0) { holdR_ = std::round (r * q) / q; holdNR_ = holdR; }
                        --holdN_; --holdNR_;
                        wl = holdL_; wr = holdR_;
                    }
                    const float tone = CR.L->k[2];
                    if (tone < 0.98f)
                    {   // the tone rides the shape: at shape 0 the lane stays a wire
                        const float fc = 300.0f * std::pow (60.0f, tone); const float a = 1.0f - std::exp (-6.2831853f * fc / (float) sr_);
                        crushLp_[0] += a * (wl - crushLp_[0]); crushLp_[1] += a * (wr - crushLp_[1]);
                        const float in = s < 0.25f ? s * 4.0f : 1.0f; wl += (crushLp_[0] - wl) * in; wr += (crushLp_[1] - wr) * in;
                    }
                    { const float tg = shaperTrim (7, CR.mode, s * CR.L->blend);   // tp90
                      l = (l + (wl - l) * CR.L->blend) * tg; r = (r + (wr - r) * CR.L->blend) * tg; }
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
                    float cut01 = 1.0f - 0.9f * FL.depth * (1.0f - s);   // 20·1000^cut01 Hz: 1 = 20 kHz, 0.1 = 40 Hz
                    /* tp92 — PUNCH (k5) WAS DEAD. It reached the engine as FilterFxEngine::Params::punch, a BOOL that turns
                       its envelope follower into a transient detector — and this lane runs that follower at zero depth,
                       so 0.03 and 0.97 were the same nothing (the plugin sweep read it at its own noise floor). Now it is
                       what the Volume lane's Punch is: every OPENING of the drawn cutoff overshoots, up to +2.5 octaves,
                       and decays in 12 ms — the spit on a filter gate's attack. */
                    fltPrev_ += (s - fltPrev_) * (1.0f - std::exp (-1.0f / ((float) sr_ * 0.005f)));
                    if (fltRefr_ > 0) --fltRefr_; else if (s - fltPrev_ > 0.15f) { fltPunch_ = 1.0f; fltRefr_ = (int) (sr_ * 0.02); }
                    if (FL.L->k[5] > 0.01f) { fltPunch_ *= std::exp (-1.0f / ((float) sr_ * 0.012f)); cut01 = std::min (1.0f, cut01 + 0.25f * FL.L->k[5] * fltPunch_); }
                    else fltPunch_ = 0.0f;
                    float wl = l, wr = r;
                    if (! (ext != nullptr && ext->filter (0, FL.mode, cut01, FL.L->k[0], FL.L->k[1], FL.L->k[2], (int) (FL.L->k[3] * 5.99f), FL.L->k[4], FL.L->k[5], wl, wr)))
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
                    if (ext != nullptr && ext->filter (1, kShaperPhaserRoster[ri], (0.14f + 0.30f * PH.L->k[3]) + 0.7f * s * PH.depth, 0.15f + 0.8f * PH.L->k[0], PH.L->k[2], 1.0f, 0, PH.L->k[1], 0.0f, wl, wr))
                    { const float tg = shaperTrim (6, PH.mode, PH.L->blend);   // tp90 — the per-type level law
                      l = (l + (wl - l) * PH.L->blend) * tg; r = (r + (wr - r) * PH.L->blend) * tg; phDone = true; }
                    vizPh_[6] = (float) p; vizV_[6] = s;
                }
                if (PH.on && ! phDone && PH.mode == 1)
                {
                    const double p = lanePhase (PH, 6, beat);
                    const float s = smoothed (6, readShape (*PH.L, p), PH.L->smooth);
                    const float fb = 0.2f + 0.72f * PH.L->k[0], st = PH.L->k[1] * 0.5f;
                    /* tp92 — CENTRE (k3) WAS DEAD ON THIS TYPE: the built-in flanger never read it. It scales where the sweep
                       sits, ×0.7 .. ×5, and at its rest (0.2) the factor is exactly 1 — every saved patch sounds as it did.
                       Held under 36 ms so the ×1.15 stereo stretch stays inside the 2048-sample line. */
                    const float dms = std::min (36.0f, (0.3f + 7.7f * s * PH.depth) * std::exp2 ((PH.L->k[3] - 0.2f) * 3.0f));
                    for (int c = 0; c < 2; ++c)
                    {
                        float* D = c ? flR_ : flL_; const float dsm = dms * (c ? (1.0f + st * 0.3f) : (1.0f - st * 0.3f));
                        const float rp = (float) flW_ - dsm * (float) sr_ * 0.001f; const int i0 = (int) std::floor (rp); const float f = rp - (float) i0;
                        const float w = D[(i0 + 4096) & 2047] * (1 - f) + D[(i0 + 1 + 4096) & 2047] * f;
                        const float x = c ? r : l;
                        /* tp92 — DRIVE (k2) WAS DEAD ON THE TWO BUILT-INS: only the roster types were handed it. The Filter
                           lane's own built-in drive, recycled: the saturation goes INTO the line, so the regeneration growls. */
                        const float xd = PH.L->k[2] > 0.0f ? std::tanh (x * (1.0f + 6.0f * PH.L->k[2])) / (1.0f + PH.L->k[2]) : x;
                        D[flW_ & 2047] = xd + w * fb;
                        /* tp89 — and the built-in flanger was the same unscaled sum (+7.03 dB, 1.99x peak). */
                        const float mx = PH.L->blend;
                        const float gN = kPhMakeup / (1.0f + mx);
                        if (c) r = (r + (w - x * 0.15f) * mx) * gN; else l = (l + (w - x * 0.15f) * mx) * gN;
                    }
                    flW_ = (flW_ + 1) & 2047;
                    { const float tg = shaperTrim (6, 1, PH.L->blend); l *= tg; r *= tg; }   // tp90
                    vizPh_[6] = (float) p; vizV_[6] = s;
                }
                else if (PH.on && ! phDone)
                {
                    const double p = lanePhase (PH, 6, beat);
                    const float s = smoothed (6, readShape (*PH.L, p), PH.L->smooth);
                    const float fb = 0.2f + 0.72f * PH.L->k[0];
                    // tp81 — CENTRE (k3), the Phaser lane's fourth target: where the sweep starts, 60 Hz .. 1 kHz
                    const float fc = (40.0f + 800.0f * PH.L->k[3]) * std::pow (40.0f, s * PH.depth);   // k3 = 0.2 lands on the 200 Hz it always started from
                    const float st = PH.L->k[1] * 0.35f;   // stereo: the right channel's centre sits a little higher
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& A = c ? phR_ : phL_; const float fcc = fc * (c ? (1.0f + st) : (1.0f - st));
                        const float t = std::tan (3.14159265f * std::min (fcc, (float) sr_ * 0.45f) / (float) sr_), gg = (1.0f - t) / (1.0f + t);
                        float x = (c ? r : l) + ch_[c].phfb * fb * 0.6f;
                        if (PH.L->k[2] > 0.0f) x = std::tanh (x * (1.0f + 6.0f * PH.L->k[2])) / (1.0f + PH.L->k[2]);   // tp92 — Drive, as above
                        float y = x;
                        for (int q = 0; q < 6; ++q) { AP& st2 = A[(size_t) q]; const float o = -gg * y + st2.x1 + gg * st2.y1; st2.x1 = y; st2.y1 = o; y = o; }
                        ch_[c].phfb = y;
                        /* 🚨 tp89 — A PHASER IS DRY PLUS ALLPASS, AND SUMMING THEM AT FULL LEVEL APPROACHES 2x.
                           Max: "the phaser really distorts and clips, it's shit … nothing should be making
                           anything quieter or louder." MEASURED: +7.45 dB rms and a 2.47x peak against the
                           same render dark — that is the clipping, and it was simply an unscaled sum.
                           Summed at half it is the textbook phaser; the 3 dB a notch filter genuinely loses
                           is made up so the lane lands where the law says it should, at unity. */
                        const float mx = PH.L->blend;
                        const float gN = kPhMakeup / (1.0f + mx);
                        if (c) r = (r + y * mx) * gN; else l = (l + y * mx) * gN;
                    }
                    { const float tg = shaperTrim (6, 0, PH.L->blend); l *= tg; r *= tg; }   // tp90
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
                    /* tp81 — TILT (k3), the Pan lane's fourth target: the highs swing wider than the lows, or the
                       other way. A pan with a spectrum to it, which is what makes a stereo move feel like a room
                       rather than a knob. 0.5 leaves the field exactly as the pan left it. */
                    {
                        const float ti = PN.L->k[3];
                        if (std::fabs (ti - 0.5f) > 0.01f)
                        {
                            const float a = 1.0f - std::exp (-2.0f * 3.14159265f * 700.0f / (float) sr_);
                            pnLo_[0] += a * (wl - pnLo_[0]); pnLo_[1] += a * (wr - pnLo_[1]);
                            const float hL = wl - pnLo_[0], hR = wr - pnLo_[1], g = (ti - 0.5f) * 2.0f;
                            const float mH = 0.5f * (hL + hR), sH = 0.5f * (hL - hR) * (1.0f + g);
                            const float mL = 0.5f * (pnLo_[0] + pnLo_[1]), sL = 0.5f * (pnLo_[0] - pnLo_[1]) * (1.0f - g);
                            wl = (mH + sH) + (mL + sL); wr = (mH - sH) + (mL - sL);
                        }
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
                    float gRaw = 1.0f - VL.depth * (1.0f - s);
                    /* tp81 — HOLD (k3), the Volume lane's fourth target: the gate stays open after the shape has
                       already fallen, up to 250 ms, so a short drawn opening can ring on. At 0, the old behaviour. */
                    if (VL.L->k[3] > 0.01f)
                    {
                        if (gRaw >= volHoldV_) { volHoldV_ = gRaw; volHoldN_ = (int) ((float) sr_ * 0.25f * VL.L->k[3]); }
                        else if (volHoldN_ > 0) { --volHoldN_; gRaw = volHoldV_; }
                        else volHoldV_ = gRaw;
                    }
                    else { volHoldV_ = gRaw; volHoldN_ = 0; }
                    const float base = 0.2f + 250.0f * VL.L->smooth * VL.L->smooth;
                    /* tp92 — ATTACK / RELEASE HAD NO TOP. Both were a multiple of the lane's Smooth, so at its rest (0.2)
                       the whole knob spanned ~1-19 ms and at a sharp Smooth under a millisecond: the offline sweep read
                       Attack 0.16 dB and Release 0.30 dB of envelope, end to end, on a 1/16 gate. The upper half now opens
                       to +400 ms — a real swell, a real tail. Below the middle, and at the rest point, nothing moved. */
                    const float kt = gRaw > smooth_[0] ? VL.L->k[0] : VL.L->k[1];
                    const float up = kt > 0.5f ? (kt - 0.5f) * 2.0f : 0.0f;
                    const float ms = base * (0.1f + 1.9f * kt) + 400.0f * up * up;
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
            /* tp80 — THE LENT LANES. Every rack effect a position can hold runs through one helper: read the
               shape, hand it to the processor's engine, take back what it returns. The differences between a
               reverb and a frequency shifter are all on the far side of ShaperExt::fx, where the engines are. */
            auto applyLent = [&] (int K)
            {
                const LaneView& V = view_[K]; if (! V.on) return;
                const double p = lanePhase (V, K, beat);
                const float s = smoothed (K, readShape (*V.L, p), V.L->smooth) * V.depth;
                float wl = l, wr = r;
                if (ext != nullptr && ext->fx (K, s, V.L->k, V.mode, V.L->blend, wl, wr)) { l = wl; r = wr; }
                vizPh_[K] = (float) p; vizV_[K] = s;
            };
            for (int sl = 0; sl < kShaperSlots; ++sl)
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
                    case  8: case  9: case 10: case 11:
                    case 12: case 13: case 14:
                    case 15: case 16: case 17: applyLent (slot[sl]); break;   // Reverb · Delay · Chorus · Widen · Multiband · Tape · Granular · Bode · Noise · Flanger
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
    /* 🚨 tp88 — READING FASTER THAN REALTIME IS A DECIMATION, AND A DECIMATION WITHOUT A BAND-LIMIT FOLDS.
       Max: "double time has a weird bitcrush bug … it's bitcrushed and distorted when it shifts up, and it's
       almost detuned as well." That is aliasing, exactly: at 2x every partial above SR/4 comes back mirrored
       around Nyquist, inharmonic, which is precisely what a bitcrusher sounds like. MEASURED before this:
       a 13 / 15 / 17 kHz tone read at 2x vanished from 2f and reappeared at |SR-2f| at FULL amplitude —
       120 to 141 dB of alias — while the same tones at 1x left those bins empty.
       The cure has to be applied WHERE the resampling happens, so `rd` stays the cubic (right at rate <= 1)
       and this takes over above it: a Blackman-windowed sinc whose cutoff is 1/ratio, which is the textbook
       decimation filter. One table, built once: the kernel is K(u) = sinc(u)·w(u) over a fixed u, and a tap
       at source distance t reads K(t/ratio) — so widening the kernel and lowering the cutoff are the same
       act, and no sin() is called per sample. */
    static constexpr int kBlZeros = 16;                 // kernel half-width, in sinc zeros
    static constexpr int kBlRes   = 512;               // table points per zero
    static inline float blTab_[2 * kBlZeros * kBlRes + 1] = {};
    static inline bool  blReady_ = false;
    static void blInit() noexcept
    {
        if (blReady_) return;
        const int N = 2 * kBlZeros * kBlRes;
        for (int i = 0; i <= N; ++i)
        {
            const double u = (double) (i - N / 2) / (double) kBlRes;      // -kBlZeros .. +kBlZeros
            const double x = M_PI * u;
            const double sinc = (std::fabs (x) < 1e-9) ? 1.0 : std::sin (x) / x;
            const double wp = (u + kBlZeros) / (2.0 * kBlZeros);          // 0..1 across the window
            const double win = 0.42 - 0.5 * std::cos (2.0 * M_PI * wp) + 0.08 * std::cos (4.0 * M_PI * wp);
            blTab_[i] = (float) (sinc * win);
        }
        blReady_ = true;
    }
    static inline float blK (double u) noexcept        // the kernel, by table lookup
    {
        const double a = (u + kBlZeros) * kBlRes;
        if (a <= 0.0 || a >= (double) (2 * kBlZeros * kBlRes)) return 0.0f;
        const int i = (int) a; const float f = (float) (a - i);
        return blTab_[i] + (blTab_[i + 1] - blTab_[i]) * f;
    }
    /** Band-limited read for rate > 1. `behind` is how far back the head is, so a tap's own distance behind
        is known exactly and taps that would reach the UNWRITTEN future, or past what the ring holds, are
        dropped and the rest renormalised. */
    static float rdBL (const std::vector<float>& b, int n, double behind, int wr, double ratio, double maxBehind) noexcept
    {
        const double pos = (double) wr - behind;
        const double base = std::floor (pos);
        const double frac = pos - base;
        const int half = (int) std::ceil (kBlZeros * ratio);
        double acc = 0.0, norm = 0.0;
        for (int k = -half + 1; k <= half; ++k)
        {
            const double t = (double) k - frac;                 // source samples from the read point
            const double tapBehind = behind + frac - (double) k;
            if (tapBehind < 0.0 || tapBehind > maxBehind) continue;   // the future, or older than the ring holds
            const double c = blK (t / ratio);
            if (c == 0.0) continue;
            long idx = (long) base + k; idx %= n; if (idx < 0) idx += n;
            acc += c * (double) b[(size_t) idx]; norm += c;
        }
        return (float) (std::fabs (norm) > 1.0e-6 ? acc / norm : 0.0);
    }
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
    int slotCtl_[kShaperSlots] = { 0, 1, 2, 3, 4, 5, 6, 7 }; bool slotSet_ = false;   // tp83 — the chain, from its parameters
    double sr_ = 48000.0;
    std::atomic<const ShaperState*> state_ { nullptr };
    std::shared_ptr<const ShaperState> stateOwner_[2]; unsigned stateSeq_ = 0;
    std::shared_ptr<Ring> ringOwner_; std::atomic<Ring*> ring_ { nullptr };
    int ringW_ = 0, ringFilled_ = 0;
    int soundAge_ = 0, silentRun_ = 0;   // tp87 — how much of the ring is the sound that is playing now
    double tsRatio_ = 1.0, tsPrevBehind_ = -1.0;   // tp88 — the playback rate the band-limit follows
    double tsRate_ = 1.0;                          // tp95 — the same rate, unclamped, so a SLOW read is band-limited too
    Ch ch_[2]; std::vector<AP> phL_, phR_;
    std::array<float, kShaperLanes> smooth_ {};
    double tsPos_ = 0, tsOld_ = 0, tsLastBehind_ = 0, stepTarget_ = 0, tsPrevTgt_ = -1.0; int tsXf_ = 0, tsXfN_ = 1; bool stepping_ = false;
    float holdL_ = 0, holdR_ = 0; int holdN_ = 0, holdNR_ = 0;   // tp81 — the right channel crushes on its OWN counter (Crush's Stereo)
    float volHoldV_ = 0; int volHoldN_ = 0;                       // tp81 — the Volume lane's Hold
    float tmLp_[2] = {}, rpLp_[2] = {}, pnLo_[2] = {};            // tp81 — Time's Tone, Repeat's Tone, Pan's Tilt
    float flL_[2048] = {}, flR_[2048] = {}; int flW_ = 0;   // the Flanger mode's delay line (~43 ms at 48 k)
    bool repHold_ = false; int repLen_ = 0, repStartW_ = 0; double repPos_ = 0, repRead_ = 0, repLastP_ = 0;
    double tsSlew_ = -1.0;                       // tp72 — the Time lane's glided read position (-1 = not gliding)
    std::atomic<ShaperExt*> ext_ { nullptr };    // tp72 — the rack's engines, lent by the processor
    double freePh_[kShaperLanes] = {};           // tp72 — the Free / MIDI / Audio lanes' own clocks (one per KIND)
    float envFast_ = 0, envSlow_ = 0; int refr_ = 0, noteAt_ = -1;
    float crushLp_[2] = {}, bassLp_[2] = {}, volEnv_ = 1.0f, volPrev_ = 1.0f, punchEnv_ = 0.0f; int punchRefr_ = 0;
    float fltPrev_ = 0.0f, fltPunch_ = 0.0f; int fltRefr_ = 0;   // tp92 — the Filter lane's Punch
    float haasL_[1024] = {}, haasR_[1024] = {}; int haasW_ = 0;   // tp74 — the Pan lane's Haas delay
    float vizPh_[kShaperLanes] = {}, vizV_[kShaperLanes] = {};
};
} // namespace wc
