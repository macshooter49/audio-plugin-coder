#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  TerrainSplitterFx — THE BAND SPLITTER (rack device kind 14, fb445)
//
//  Serum 2 ships three splitter rows (`Splitter L/H`, `L/M/H`, `M/S`) because its
//  rack is a flat module list. We ship ONE device with a Type dropdown — the same
//  call the Distortion made for 23 modes and the Reverb for 9
//  (SPLITTER-BUILD-BIBLE.md §0). The card names a split; each LANE can then own
//  its own effects — a Distortion on the mids, a Filter on the highs.
//
//  THIS FILE IS ONLY THE DSP. The chain plumbing that routes downstream devices
//  into lanes is built separately. What lives here: split cleanly into N lanes,
//  hand those lanes out, take them back, recombine — and the per-lane trims that
//  make the device useful the moment it is dropped in, before anything is routed.
//
//  ═══ THE WHOLE BALLGAME: PERFECT RECONSTRUCTION ═══════════════════════════════
//  A splitter that combs on recombination is useless no matter how nice the UI
//  is. Everything below is arranged around one number: sum of all lanes, no lane
//  processing, nulled against the phase-matched dry. spl_cert §A prints it first
//  for every Type x Slope, and the bar is −100 dB.
//
//  WHAT IS REUSED VERBATIM, not reinvented (the parent mandate, and DynamicsCore
//  is already certified at −134.8 dB on exactly this identity):
//    · `tw::dyn::LR4`   (DynamicsCore.h:297) — the shipped Linkwitz-Riley 4th
//      order on the shipped Simper TPT SVF. It IS the Slope 24 dB/oct path.
//    · `tw::dyn::Svf1::ap()` (:287) — the matching AP2. It IS the Slope 24
//      alignment and dry-path filter.
//    · The identity at :250-254 — `LP4 + HP4 == AP2(fc)` exactly — is why the
//      Mix law here is provable rather than hoped.
//    · `TerrainOttFx.h:485-500` is the 3-band cascade done correctly, and the
//      line that matters is `alignLow_.ap(lo)`: the LOW band bypasses split-2, so
//      it must eat the SAME AP2(f_hi) the upper legs ate or the recombination
//      combs around the upper crossover. :468-471 runs the dry through the
//      identical cascade so a Mix crossfade stays phase-matched.
//    · `dyn::Glide`, `dyn::flushd`, `dyn::db2lin`, `dyn::clampf` — the house
//      smoothing / denormal / dB idioms, unchanged.
//
//  ═══ THE SLOPE LAWS — four crossovers, four different sum identities ═════════
//  The second back dropdown is `Slope`, and it is PHYSICS, not tone (fb345). The
//  arithmetic differs per order and getting it wrong is the classic "my multiband
//  sounds phasey" bug, so it is spelled out:
//
//    6 dB/oct  (1st order complementary):  LP1 + HP1 == x   EXACTLY.
//              No allpass anywhere. The dry path is the input, untouched. This is
//              the only slope that nulls bit-exact, and it is why it exists.
//    12 dB/oct (LR2 = BW1 squared):        LP2 + HP2 has a NOTCH at fc.
//              LP2 − HP2 == AP1(fc). ⚠️ THE HIGH LEG IS POLARITY-INVERTED inside
//              `split()`. Forget that and the sum notches at every crossover —
//              LR2's classic trap. Alignment/dry filter = AP1 = 2·LP1(x) − x.
//    24 dB/oct (LR4 = BW2 squared):        LP4 + HP4 == AP2(fc). No inversion.
//              This is `dyn::LR4` + `dyn::Svf1::ap`, the certified path, DEFAULT.
//    48 dB/oct (LR8 = BW4 squared):        LP8 + HP8 == AP4(fc). No inversion.
//              BW4 = (s²+0.765367s+1)(s²+1.847759s+1); D(s)·D(−s) = 1 + s⁸ for
//              n=4, so the sum is D(−s)/D(s) = the product of those two sections'
//              allpasses. Four LP sections + four HP sections + a 2-section AP4.
//
//    The general rule, so nobody has to re-derive it: for LR of order 2n the sum
//    is allpass when n is EVEN and needs the high leg inverted when n is ODD.
//    n = 1 (12 dB) is the only odd order shipped, and it is the only inversion.
//
//  ═══ THE TREE, AND WHERE THE ALIGNMENT ALLPASSES GO ══════════════════════════
//  Low-first, exactly the OTT shape, generalised to 4 lanes:
//
//      in ─┬─ LP@f0 ── AP@f1 ── AP@f2 ────────► SUB      (4-lane only)
//          └─ HP@f0 ─┬─ LP@f1 ── AP@f2 ───────► LOW
//                    └─ HP@f1 ─┬─ LP@f2 ──────► MID
//                              └─ HP@f2 ──────► HIGH
//
//  Every band eats the allpass of every crossover ABOVE it; the top band gets
//  none. Then sum(lanes) == AP(f0)·AP(f1)·AP(f2)·x, and the DRY runs the same
//  three allpasses through its OWN filter instances — an independent computation
//  of the same transfer function, which is what makes the §A null a real
//  measurement and not a tautology.
//
//  ⚠️ ZERO LATENCY, and linear phase is BANNED here forever. The fb305 main-send
//  exclusion subtracts the routed dry SAMPLE-ALIGNED; a linear-phase FIR split
//  would misalign it and the dry would leak back phase-smeared. Every path in
//  this file is zero-latency IIR. Reported latency: 0 samples.
//
//  ═══ THE ORDERING LAW — how lanes are stopped from overlapping ═══════════════
//  Two crossovers that cross produce a band that is negative-width: the "mid"
//  lane becomes a resonator and the sum bumps. Absolute frequency knobs plus a
//  clamp is the WRONG fix — it buys ordering by making the top of one knob's
//  travel a repeated value, which is the plateau the house law forbids (and is
//  the exact defect mutation testing found in the Bode engine twice).
//
//  So the crossovers are not independent: `Split` is the LOWEST crossover and
//  `Span` is the RATIO between successive ones — f1 = f0·r, f2 = f0·r².
//  Ordering is enforced BY CONSTRUCTION (r >= 1.4 always), never by a clamp.
//  And `Span`'s own top is renormalised against the headroom the current `Split`
//  leaves: r_max = min(40, (10 kHz / f0)^(1/(N−2))). The knob's RANGE moves so
//  that no knob POSITION is ever a clamp repeat. `Split`'s own range is picked
//  per Type so the renormalisation can never collapse below r_min:
//      2 lanes  25 Hz … 10 kHz     3 lanes  25 Hz … 2 kHz     4 lanes  25 Hz … 700 Hz
//  10 kHz is the shared ceiling for every derived crossover, and it is under the
//  0.245·fs clamp inside `Svf1::setLR` at 44.1 kHz (10.80 kHz) — so the filter
//  clamp NEVER bites at any supported rate, and the mapping is rate-independent
//  (a preset means the same Hz at 44.1 and 96).
//
//  Both ends are useful, which is the other half of the law: at 25 Hz the top
//  lane is nearly the whole program; at 10 kHz the bottom lane is. Sweeping
//  `Split` live is crossover-as-send-amount — a genuinely new gesture.
//
//  ═══ THE MIX LAW — LINEAR, not equal-power, and here is why ══════════════════
//  Bode crossfades equal-power because its wet and dry are uncorrelated. Here
//  they are PHASE-MATCHED by construction: at default trims wet and dry are the
//  same signal. An equal-power fade would then BOOST +3 dB at Mix 50 %. So the
//  fade is LINEAR: out = dry + mix·(wet − dry). At default trims the output is
//  bit-invariant across the whole Mix travel — spl_cert gates that, and it is
//  the strongest possible statement of "Mix cannot comb".
//  Mix 0 with the device ON is the ALLPASS-ROTATED input, not the raw input.
//  That is honest, not a bug: every IIR splitter on earth shares it, and the
//  alternative (blending un-rotated dry against rotated wet) is a perfect notch
//  at every crossover. Byte-identical bypass is the Power switch's job, outside.
//
//  ═══ CHASSIS BUDGET (fb275) — what got the 8, and what was cut ═══════════════
//  Front: `Split` · `Balance` · `Spread` + `Mix`.
//  Back:  2 dropdowns + exactly 8 knobs. This engine exposes MORE than 8 — up to
//  4 lanes x {Gain, Width, Pan, Mute, Solo, Flip} plus 3 crossovers. The chassis
//  cannot reach that, so the projection is stated here rather than discovered:
//
//    THE 8 (slots are fixed; only the lane NOUN relabels per Type):
//      b1 Lane 1 Gain   b2 Lane 2 Gain   b3 Lane 3 Gain   b4 Lane 4 Gain
//      b5 Span          b6 Lane 1 Width  b7 Top Lane Width  b8 Top Lane Pan
//
//    WHY THESE 8. Gain is the only per-lane control with no substitute — kill a
//    lane and the device is a multiband mute; there is no other way to reach it.
//    Span is the second crossover (and the third), i.e. it is the only way a
//    3- or 4-lane Type is anything other than one fixed shape. Width and Pan are
//    given to the BOTTOM and TOP lanes only because that is where they are
//    audible: mono-ing the bottom and widening the top is the entire club-prep
//    move, and width on a mid lane is the least useful cell in the matrix.
//
//    THE SLOTS THAT GO UNBOUND, stated rather than left for the UI to discover:
//      · A 2-lane Type has no lane 3, no lane 4 and no Span, so b3/b4/b5 are
//        unbound. Fill them by RELABEL, not by inventing DSP: b3 -> `Lane 1 Pan`
//        (otherwise unreachable), and b4/b5 mirror the front `Split` and
//        `Balance` — the house mirror-binding law, the Delay's back `Time L`
//        precedent (`index.html:7485`). A mirror is not a dead control.
//      · A 3-lane Type has no lane 4, so b4 relabels to `Lane 2 Width`.
//      · ⚠️ Mid/Side b6 (`Lane 1 Width`) IS A DEAD CELL and MUST relabel. The
//        Mid lane is handed out as (M, M) — perfectly correlated BY CONSTRUCTION
//        — so it has no Side component for a Width control to act on. Measured
//        at exactly 0.000 dB across the whole travel, and spl_cert skips it by
//        name rather than gating a control that is not there. `Side Width` (b7)
//        is the width control in that Type, and it is the honest one.
//
//    WHAT WAS CUT, and where it went instead:
//      · Mute / Solo / Phase-flip — they are SWITCHES. Putting a boolean on a
//        knob breaks the switch law, so all three live on the card's lane strip
//        as per-lane glyphs, which is also where the user already looks for them.
//        The engine supports them fully (`laneMute/laneSolo/laneFlip`).
//      · Width and Pan on the MIDDLE lanes (2 cells at 3 lanes, 4 at 4 lanes) —
//        unreachable from the chassis, and they sit at their unity defaults. The
//        engine takes them so automation and a future lane-strip expander can.
//      · Per-lane `Slip` (Haas micro-delay). It is in the bible, it is not in
//        this brief, and it would need a per-lane delay line — an allocation and
//        a reconstruction hazard bought for a control nobody asked for. Cut.
//      · Per-lane delay compensation: NOT NEEDED and deliberately absent. Every
//        lane in every Type is zero-latency; there is nothing to compensate.
//      · A `Character` axis. A router has no tone to voice. Eight characters on
//        a splitter would be eight pointless buttons (fb325 rule 3), so the
//        second dropdown is `Slope` — real physics — and the FIRST back dropdown
//        slot is `Solo` (Off · Lane 1..4), a view onto engine state that already
//        exists rather than new DSP invented to fill a hole.
//
//  CPU: the sample cost is dominated by Slope. Per channel per crossover:
//  1 one-pole (6 dB) · 5 one-poles (12 dB) · 3 SVF sections (24 dB) · 8 SVF
//  sections (48 dB), plus one alignment filter of the same order per band-above
//  pair and per dry stage. Worst case (4 lanes, 48 dB/oct) is 3x8 + 6x2 = 36 SVF
//  sections per channel. The trim path is 6 one-pole glides per lane. Crossover
//  coefficients are recomputed every 32 samples from a per-sample glided LOG
//  frequency, not every sample — a tan() per section per sample would be the
//  whole budget for a knob movement nobody can hear at 0.67 ms granularity.
//  NEVER oversampled: every path here is LTI (filters, gains, a matrix). Nothing
//  generates a harmonic, so there is nothing to alias.
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <cstdint>
#include <algorithm>

// JUCE-FREE, verified: DynamicsCore.h includes only <cmath> <cstdint> <cstring>
// <algorithm> (its lines 39-42). The cert compiles this header against the shim
// with nothing else on the include path.
#include "DynamicsCore.h"

namespace tw {
namespace spl_detail {

constexpr float kPi = 3.14159265358979f;

// 4th-order Butterworth denominator sections: (s²+k1·s+1)(s²+k2·s+1), k = 1/Q.
constexpr float kBw4A = 0.765367f;      // Q = 1.30656
constexpr float kBw4B = 1.847759f;      // Q = 0.54120

// ── TPT one-pole. Carries the 6 dB law outright and the 12 dB law by cascade. ──
struct Op1
{
    float G = 0.0f, z = 0.0f;
    void  reset() noexcept { z = 0.0f; }
    void  set (float fcHz, float fs) noexcept
    {
        const float f = dyn::clampf (fcHz, 5.0f, 0.245f * fs);
        const float g = std::tan (kPi * f / fs);
        G = g / (1.0f + g);
    }
    inline float lp (float x) noexcept
    { const float v = (x - z) * G; const float y = v + z; z = dyn::flushd (y + v); return y; }
    inline float hp (float x) noexcept { return x - lp (x); }
};

// ── Simper TPT SVF with a SETTABLE k. `dyn::Svf1` hardcodes k = √2, which is
//    exactly right for LR4 and useless for the two BW4 sections the 48 dB law
//    needs, so this is that same six lines with k as an argument. Slope 24 does
//    NOT use this — it uses the shipped, certified `dyn::LR4` / `dyn::Svf1`. ──
struct Svf2
{
    float ic1 = 0.0f, ic2 = 0.0f, g = 0.0f, k = 1.4142136f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    void  reset() noexcept { ic1 = ic2 = 0.0f; }
    void  set (float fcHz, float fs, float kk) noexcept
    {
        const float f = dyn::clampf (fcHz, 5.0f, 0.245f * fs);
        g = std::tan (kPi * f / fs); k = kk;
        const float den = 1.0f + g * (g + k);
        a1 = 1.0f / den; a2 = g * a1; a3 = g * a2;
    }
    inline void tick (float v0, float& lp, float& hp, float& bp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = dyn::flushd (2.0f * v1 - ic1);
        ic2 = dyn::flushd (2.0f * v2 - ic2);
        lp = v2; hp = v0 - k * v1 - v2; bp = v1;
    }
    inline float lp (float x) noexcept { float l, h, b; tick (x, l, h, b); return l; }
    inline float hp (float x) noexcept { float l, h, b; tick (x, l, h, b); return h; }
    inline float ap (float x) noexcept { float l, h, b; tick (x, l, h, b); return x - 2.0f * k * b; }
};

// ── ONE CROSSOVER, any of the four Slope laws. `split` returns the two legs
//    already sign-corrected, so `lo + hi` is the slope's allpass identity for
//    every order and the caller never has to remember which one inverts. ──
struct Xover
{
    int   ord = 4;                  // 1 · 2 · 4 · 8  ==  6 · 12 · 24 · 48 dB/oct
    Op1   o1lo[2], o1hi[2];         // 6 dB uses [0]; 12 dB cascades both
    dyn::LR4 lr4;                   // 24 dB — the SHIPPED, CERTIFIED path
    Svf2  s8lo[4], s8hi[4];         // 48 dB

    void reset() noexcept
    {
        for (int i = 0; i < 2; ++i) { o1lo[i].reset(); o1hi[i].reset(); }
        lr4.reset();
        for (int i = 0; i < 4; ++i) { s8lo[i].reset(); s8hi[i].reset(); }
    }
    void set (float fcHz, float fs, int order) noexcept
    {
        ord = order;
        if (order <= 2)
        {
            for (int i = 0; i < 2; ++i) { o1lo[i].set (fcHz, fs); o1hi[i].set (fcHz, fs); }
        }
        else if (order == 4) lr4.set (fcHz, fs);
        else
        {
            static const float kk[4] = { kBw4A, kBw4B, kBw4A, kBw4B };
            for (int i = 0; i < 4; ++i) { s8lo[i].set (fcHz, fs, kk[i]); s8hi[i].set (fcHz, fs, kk[i]); }
        }
    }
    inline void split (float x, float& lo, float& hi) noexcept
    {
        if      (ord == 1) { lo = o1lo[0].lp (x); hi = x - lo; }            // lo + hi == x
        else if (ord == 2)                                                  // ⚠️ HI INVERTED
        { lo = o1lo[1].lp (o1lo[0].lp (x)); hi = -(o1hi[1].hp (o1hi[0].hp (x))); }
        else if (ord == 4) lr4.split (x, lo, hi);                           // the certified path
        else
        {
            float t = x; for (int i = 0; i < 4; ++i) t = s8lo[i].lp (t); lo = t;
            t = x;       for (int i = 0; i < 4; ++i) t = s8hi[i].hp (t); hi = t;
        }
        // ⚠️ NOT LOAD-BEARING, and Design/fx4/splitter/MUTATION.md says so rather
        // than counting it as coverage. Deleting these two flushes turns ZERO
        // gates red: every state in the cascade is already flushed at 1e-20, and
        // the output reaches EXACTLY zero at 1.064 s of silence with or without
        // them (measured, both ways). They are kept because they make the split
        // stage's output contract explicit and cost two compares — but no gate
        // covers them, and pretending otherwise would be the lie fb421 is about.
        lo = dyn::flushd (lo); hi = dyn::flushd (hi);
    }
};

// ── The allpass that matches one crossover at one Slope. Used twice: on every
//    band that bypassed a crossover above it, and on the dry path. Identity at
//    6 dB, because a 1st-order complementary pair has no phase error to undo. ──
struct AlignAP
{
    int   ord = 4;
    Op1   p1;
    dyn::Svf1 a2;
    Svf2  a4a, a4b;

    void reset() noexcept { p1.reset(); a2.reset(); a4a.reset(); a4b.reset(); }
    void set (float fcHz, float fs, int order) noexcept
    {
        ord = order;
        if (order == 2) p1.set (fcHz, fs);
        else if (order == 4) a2.setLR (fcHz, fs);
        else if (order == 8) { a4a.set (fcHz, fs, kBw4A); a4b.set (fcHz, fs, kBw4B); }
    }
    inline float process (float x) noexcept
    {
        if (ord == 1) return x;                                        // nothing to undo
        if (ord == 2) return dyn::flushd (2.0f * p1.lp (x) - x);       // AP1 = (1−s)/(1+s)
        if (ord == 4) return dyn::flushd (a2.ap (x));                  // AP2 — the shipped one
        return dyn::flushd (a4b.ap (a4a.ap (x)));                      // AP4 = two BW4 sections
    }
};

} // namespace spl_detail

// ═════════════════════════════════════════════════════════════════ the device
struct TerrainSplitterFx
{
    // Rack Law C — CARDINALITY IS FROZEN AT BIRTH. fb373: a choice param
    // normalised on the DROPDOWN's option count instead of the PARAM's
    // cardinality silently hands you a different machine, and it stayed
    // bit-identical through four rounds of green measurement. Declare the full
    // width on day one and grow into it.
    static constexpr int kNumTypes  = 8;     // 5 shipped, 3 reserved (see below)
    static constexpr int kNumReal   = 5;
    static constexpr int kNumSlopes = 4;
    static constexpr int kMaxLanes  = 4;

    // The Type roster. Index is the param value and MUST NOT be reordered.
    enum Type { kLowHigh = 0, kLowMidHigh = 1, kSubLowMidHigh = 2, kMidSide = 3, kLeftRight = 4 };
    static constexpr int kDefaultType = kLowMidHigh;      // the Serum shape

    static const char* const* typeNames() noexcept
    {
        static const char* const n[kNumTypes] =
        { "Low / High", "Low / Mid / High", "Sub / Low / Mid / High",
          "Mid / Side", "Left / Right", "Reserved", "Reserved", "Reserved" };
        return n;
    }
    static const char* const* slopeNames() noexcept
    {
        static const char* const n[kNumSlopes] = { "6 dB", "12 dB", "24 dB", "48 dB" };
        return n;
    }
    /** Lane labels for a Type — the card's lane strip reads these. No duplicate
     *  label anywhere in the device (the no-doubles law): the Type name is a
     *  slash-joined list of exactly these, so the strip and the pill agree. */
    static const char* const* laneNames (int type) noexcept
    {
        static const char* const lh[4] = { "Low",  "High", "",     "" };
        static const char* const lmh[4]= { "Low",  "Mid",  "High", "" };
        static const char* const slmh[4]={ "Sub",  "Low",  "Mid",  "High" };
        static const char* const ms[4] = { "Mid",  "Side", "",     "" };
        static const char* const lr[4] = { "Left", "Right","",     "" };
        switch (resolveType (type))
        { case kLowHigh: return lh;  case kLowMidHigh: return lmh;
          case kSubLowMidHigh: return slmh; case kMidSide: return ms; default: return lr; }
    }

    static constexpr int laneCountFor (int type) noexcept
    {
        return (resolveType (type) == kSubLowMidHigh) ? 4
             : (resolveType (type) == kLowMidHigh)    ? 3 : 2;
    }
    /** Reserved Type slots alias the DEFAULT rather than falling off the roster.
     *  An out-of-roster automation value must not be able to produce a machine
     *  that is silent or half-built — the fb373 failure mode exactly. */
    static constexpr int resolveType (int t) noexcept
    { return (t < 0 || t >= kNumReal) ? kDefaultType : t; }

    // Range law. Every derived crossover shares one ceiling so the mapping is
    // rate-independent AND the 0.245·fs clamp inside the SVFs can never bite
    // (0.245 · 44100 = 10.80 kHz > kFcTop).
    static constexpr float kFcTop   = 10000.0f;
    static constexpr float kFcBot   = 25.0f;
    static constexpr float kSpanMin = 1.4f;      // ORDERING, by construction
    static constexpr float kSpanMax = 40.0f;
    static constexpr float kGainTopDb = 12.0f;   // no playing safe: lanes can shout
    static constexpr float kGainBotDb = -60.0f;  // ... and 0 is a real kill, not −60
    static constexpr float kWidthMax  = 4.0f;    // 400 % after Spread — past useful

    struct Params
    {
        int   type  = kDefaultType;
        int   slope = 2;                 // 0=6 · 1=12 · 2=24 (default) · 3=48 dB/oct

        // ── FRONT ────────────────────────────────────────────────────────────
        float split   = 0.5f;            // 0..1 log → the LOWEST crossover
        float balance = 0.5f;            // lane energy morph, 0.5 = exact unity
        float spread  = 0.5f;            // width fan across the stack, 0.5 = unity
        float mix     = 1.0f;            // 1.0 = fully wet (house law)

        // ── BACK 8 ───────────────────────────────────────────────────────────
        float span = 0.4f;               // b5 — the ratio between crossovers
        float laneGain [kMaxLanes] = { 0.5f, 0.5f, 0.5f, 0.5f };   // b1..b4, 0.5 = 0 dB
        float laneWidth[kMaxLanes] = { 0.5f, 0.5f, 0.5f, 0.5f };   // b6/b7, 0.5 = 100 %
        float lanePan  [kMaxLanes] = { 0.5f, 0.5f, 0.5f, 0.5f };   // b8,    0.5 = centre

        // ── LANE STRIP (glyphs, not knobs — the switch law) ──────────────────
        bool laneMute[kMaxLanes] = { false, false, false, false };
        bool laneSolo[kMaxLanes] = { false, false, false, false };
        bool laneFlip[kMaxLanes] = { false, false, false, false };
    };

    // ── knob ↔ value helpers. Exposed because the UI, the preset bank and the
    //    cert must all agree on the mapping, and a mapping authored in three
    //    places is the fb373 defect waiting to happen. ─────────────────────────
    static float splitHzFor (int type, float knob) noexcept
    {
        const int N = laneCountFor (type);
        const float top = (N == 2) ? kFcTop : (N == 3) ? 2000.0f : 700.0f;
        return kFcBot * std::pow (top / kFcBot, dyn::clampf (knob, 0.0f, 1.0f));
    }
    static float splitKnobFor (int type, float hz) noexcept
    {
        const int N = laneCountFor (type);
        const float top = (N == 2) ? kFcTop : (N == 3) ? 2000.0f : 700.0f;
        return dyn::clampf (std::log (dyn::clampf (hz, kFcBot, top) / kFcBot)
                            / std::log (top / kFcBot), 0.0f, 1.0f);
    }
    /** THE ORDERING LAW, in one function. `Span`'s top follows the headroom the
     *  current `Split` leaves, so ordering is bought by construction and never by
     *  a clamp — which is what stops the top of this knob's travel from being a
     *  repeated value (the plateau the house law forbids). */
    static float spanRatioFor (int type, float splitHz, float knob) noexcept
    {
        const int nx = laneCountFor (type) - 1;              // crossover count
        if (nx < 2) return 1.0f;                             // 2 lanes: Span is relabelled
        const float rHead = std::pow (kFcTop / std::max (kFcBot, splitHz), 1.0f / (float) (nx - 1));
        const float rTop  = std::max (kSpanMin * 1.05f, std::min (kSpanMax, rHead));
        return kSpanMin * std::pow (rTop / kSpanMin, dyn::clampf (knob, 0.0f, 1.0f));
    }
    static float spanKnobFor (int type, float splitHz, float ratio) noexcept
    {
        const int nx = laneCountFor (type) - 1;
        if (nx < 2) return 0.0f;
        const float rHead = std::pow (kFcTop / std::max (kFcBot, splitHz), 1.0f / (float) (nx - 1));
        const float rTop  = std::max (kSpanMin * 1.05f, std::min (kSpanMax, rHead));
        return dyn::clampf (std::log (dyn::clampf (ratio, kSpanMin, rTop) / kSpanMin)
                            / std::log (rTop / kSpanMin), 0.0f, 1.0f);
    }
    /** Fader law. 0.5 is EXACTLY 0 dB (the double-click home), 1.0 is +12 dB, and
     *  0.0 is a TRUE kill — the bottom 4 % fades the −60 dB floor to zero so the
     *  knob does not step 60 dB into silence. */
    static float laneGainLin (float t) noexcept
    {
        t = dyn::clampf (t, 0.0f, 1.0f);
        if (t <= 0.0f) return 0.0f;
        const float d = (t < 0.5f) ? kGainBotDb * std::pow (1.0f - 2.0f * t, 1.5f)
                                   : (2.0f * kGainTopDb) * (t - 0.5f);
        float g = dyn::db2lin (d);
        if (t < 0.04f) g *= t * 25.0f;
        return g;
    }

    // ── lifecycle. NOTHING IN THIS FILE ALLOCATES, EVER — there is no delay
    //    line, no lookahead and no buffer, so `prepare` only writes coefficients
    //    and `reset` only clears state (fb415: a malloc on the audio thread came
    //    from copying a prepare-in-processBlock shape). ─────────────────────────
    void prepare (double sampleRate, int /*maxBlock*/ = 0) noexcept
    {
        fs_ = (float) std::max (8000.0, sampleRate);
        for (int i = 0; i < kMaxLanes; ++i)
        {
            gGain_[i].setTau (0.015f, fs_); gGate_[i].setTau (0.015f, fs_);
            gWidth_[i].setTau (0.015f, fs_); gPanL_[i].setTau (0.015f, fs_);
            gPanR_[i].setTau (0.015f, fs_); gFlip_[i].setTau (0.012f, fs_);
        }
        gMix_.setTau (0.020f, fs_);
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < kMaxLanes - 1; ++k)
            { xo_[c][k].reset(); dryAP_[c][k].reset(); bandAP_[c][k].reset(); }
        }
        for (int i = 0; i < kMaxLanes; ++i)
        {
            gGain_[i].snap (1.0f); gGate_[i].snap (1.0f); gWidth_[i].snap (1.0f);
            gPanL_[i].snap (1.0f); gPanR_[i].snap (1.0f); gFlip_[i].snap (1.0f);
            lanePk_[i] = 0.0f;
        }
        gMix_.snap (1.0f);
        dryL_ = dryR_ = 0.0f;
        coefCount_ = 0; logPrimed_ = false; forceCoef_ = true; forceState_ = true;
    }

    // ── per-block parameter intake (NEVER per sample). fb350 THE POOL LAW: a
    //    second instance must make every per-block call the first one makes —
    //    everything derived lives HERE, so a pooled path that calls setParams
    //    cannot silently skip a resolve. ───────────────────────────────────────
    void setParams (const Params& p) noexcept
    {
        const int  t  = resolveType (p.type);
        const int  sl = (p.slope < 0) ? 0 : (p.slope >= kNumSlopes ? kNumSlopes - 1 : p.slope);
        static const int kOrderOf[kNumSlopes] = { 1, 2, 4, 8 };
        const int  ord = kOrderOf[sl];

        if (t != type_ || ord != order_) { forceCoef_ = true; forceState_ = true; }
        type_   = t;
        order_  = ord;
        nLanes_ = laneCountFor (t);
        matrix_ = (t == kMidSide || t == kLeftRight);

        // ── the crossovers. Ratio-derived, so ordering is structural. ─────────
        if (! matrix_)
        {
            const float f0 = splitHzFor (t, p.split);
            spanNow_ = spanRatioFor (t, f0, p.span);
            float f = f0;
            for (int k = 0; k < kMaxLanes - 1; ++k)
            {
                logTgt_[k] = std::log (std::max (kFcBot * 0.5f, f));
                if (k < nLanes_ - 2) f *= spanNow_;
            }
        }

        // ── Balance: a lane-energy morph whose CENTRE IS EXACT UNITY, which is
        //    what lets §A measure reconstruction without disabling it. At 0 the
        //    last lane is 60 dB down; at 1 the first is. Nothing plateaus,
        //    because the lanes in between move for every position of the travel.
        const float tilt = 2.0f * dyn::clampf (p.balance, 0.0f, 1.0f) - 1.0f;
        // 🔬 kBalLift EXISTS BECAUSE THE GATE FOUND A PLATEAU. Balance was
        //    cut-only (−60 dB on the losing lane) and its first third measured
        //    0.013 dB of change on the summed output: dropping a lane from −60
        //    to −40 dB under its neighbour is inaudible at both ends. So the
        //    winning lane now LIFTS as the loser dies — every position of the
        //    travel moves the sound, and 0.5 is still exactly unity.
        constexpr float kBalLift = 6.0f;                    // dB, on the winner
        // ── Spread: the frequency-dependent width FAN. 0 = every lane mono ·
        //    0.5 = every lane unity · 1 = bottom lane mono, top lane 200 %.
        const float sp   = 2.0f * dyn::clampf (p.spread, 0.0f, 1.0f) - 1.0f;

        bool anySolo = false;
        for (int i = 0; i < nLanes_; ++i) anySolo = anySolo || p.laneSolo[i];

        for (int i = 0; i < kMaxLanes; ++i)
        {
            const float pos = (nLanes_ <= 1) ? 0.0f : (float) i / (float) (nLanes_ - 1);
            const float far  = (tilt >= 0.0f) ? (1.0f - pos) : pos;
            const float att  = std::fabs (tilt) * (kGainBotDb * far + kBalLift * (1.0f - far));
            gainT_[i] = laneGainLin (p.laneGain[i]) * dyn::db2lin (att);

            const float fan = (sp <= 0.0f) ? (1.0f + sp) : (1.0f + sp * (2.0f * pos - 1.0f));
            widthT_[i] = dyn::clampf (2.0f * dyn::clampf (p.laneWidth[i], 0.0f, 1.0f) * fan,
                                      0.0f, kWidthMax);

            // Equal-power pan with a UNITY CENTRE (0.5 → 1.0 on both sides
            // exactly, so the default trim chain is the identity and §A can see
            // the reconstruction rather than a −3 dB pan law).
            const float th = dyn::clampf (p.lanePan[i], 0.0f, 1.0f) * 1.5707963f;
            panLT_[i] = 1.4142136f * std::cos (th);
            panRT_[i] = 1.4142136f * std::sin (th);

            // SOLO OVERRIDES MUTE, and multiple solos SUM. A lane that is both
            // muted and soloed SOUNDS — solo is an override, not a second mute.
            gateT_[i] = anySolo ? (p.laneSolo[i] ? 1.0f : 0.0f)
                                : (p.laneMute[i] ? 0.0f : 1.0f);
            // Polarity glides through zero — that IS the click-free way to flip.
            flipT_[i] = p.laneFlip[i] ? -1.0f : 1.0f;
        }

        mixT_ = dyn::clampf (p.mix, 0.0f, 1.0f);
        aLog_ = dyn::coefTau (0.015f, fs_);
    }

    // ── the audio ────────────────────────────────────────────────────────────
    // 🔑 SPLIT AND MERGE ARE A MATCHED PAIR: exactly one `mergeStereo` per
    //    `splitStereo`, in the SAME sample slot. `splitStereo` advances every
    //    filter state and stashes the phase-matched dry that `mergeStereo` needs
    //    for Mix; calling them out of step is not a level error, it is a
    //    misaligned dry and it combs. The host that processes lanes externally
    //    does: split → (its own lane FX) → merge, per sample.

    int laneCount() const noexcept { return nLanes_; }

    inline void splitStereo (float inL, float inR,
                             float laneL[kMaxLanes], float laneR[kMaxLanes]) noexcept
    {
        for (int i = 0; i < kMaxLanes; ++i) { laneL[i] = 0.0f; laneR[i] = 0.0f; }
        advanceXover();

        if (matrix_)
        {
            if (type_ == kMidSide)
            {
                // Blumlein's matrix, the ÷2 convention: decode needs no gain.
                // The Side lane is handed out as (S, −S) so that a stereo device
                // dropped into it sees a real, fully anti-correlated signal and
                // the merge stays a PLAIN SUM for every Type.
                const float m = 0.5f * (inL + inR), s = 0.5f * (inL - inR);
                laneL[0] = m; laneR[0] =  m;
                laneL[1] = s; laneR[1] = -s;
            }
            else { laneL[0] = inL; laneR[1] = inR; }   // Left / Right
            dryL_ = inL; dryR_ = inR;                  // no filters ⇒ dry IS the input
            return;
        }

        const int NX = nLanes_ - 1;
        for (int c = 0; c < 2; ++c)
        {
            float band[kMaxLanes] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float rest = (c == 0) ? inL : inR;
            for (int k = 0; k < NX; ++k) xo_[c][k].split (rest, band[k], rest);
            band[NX] = rest;

            // Every band eats the allpass of every crossover ABOVE it; the top
            // band gets none. Omit this and the sum dips around the upper
            // crossover — the classic "my multiband sounds phasey" bug.
            if (nLanes_ >= 3) band[0] = bandAP_[c][0].process (band[0]);
            if (nLanes_ >= 4)
            {
                band[0] = bandAP_[c][1].process (band[0]);
                band[1] = bandAP_[c][2].process (band[1]);
            }

            // The DRY runs the SAME allpass cascade through its OWN instances —
            // an independent computation of the same transfer function, which is
            // what makes the §A null a measurement and not a tautology.
            float d = (c == 0) ? inL : inR;
            for (int k = 0; k < NX; ++k) d = dryAP_[c][k].process (d);

            if (c == 0) { for (int i = 0; i < nLanes_; ++i) laneL[i] = band[i]; dryL_ = d; }
            else        { for (int i = 0; i < nLanes_; ++i) laneR[i] = band[i]; dryR_ = d; }
        }
    }

    inline void mergeStereo (const float laneL[kMaxLanes], const float laneR[kMaxLanes],
                             float& outL, float& outR) noexcept
    {
        float wl = 0.0f, wr = 0.0f;
        for (int i = 0; i < kMaxLanes; ++i)
        {
            // Every glide ticks for every lane, present or not (the Pool Law
            // shape) — a lane that starts contributing must not arrive with a
            // cold smoother and step.
            const float g  = gGain_ [i].proc (gainT_ [i]);
            const float ga = gGate_ [i].proc (gateT_ [i]);
            const float w  = gWidth_[i].proc (widthT_[i]);
            const float pl = gPanL_ [i].proc (panLT_ [i]);
            const float pr = gPanR_ [i].proc (panRT_ [i]);
            const float fp = gFlip_ [i].proc (flipT_ [i]);
            if (i >= nLanes_) { lanePk_[i] = 0.0f; continue; }

            float a = laneL[i], b = laneR[i];
            const float m = 0.5f * (a + b), s = 0.5f * (a - b);
            a = m + w * s; b = m - w * s;            // w == 1 is the exact identity
            const float k  = g * ga * fp;
            const float ol = a * pl * k, orr = b * pr * k;
            wl += ol; wr += orr;
            const float pk = std::max (std::fabs (ol), std::fabs (orr));
            lanePk_[i] = (pk > lanePk_[i]) ? pk : lanePk_[i] * 0.9995f;
        }

        // LINEAR, not equal-power — wet and dry are phase-matched, so equal-power
        // would boost +3 dB at Mix 50 %. See the Mix law at the top of the file.
        const float mx = gMix_.proc (mixT_);
        mxNow_ = mx;
        outL = dyn::flushd (dryL_ + mx * (wl - dryL_));
        outR = dyn::flushd (dryR_ + mx * (wr - dryR_));
    }

    /** The convenience path: split, apply the per-lane trims, merge. This is a
     *  working device before the chain routes anything into a lane. */
    inline void processStereo (float inL, float inR, float& outL, float& outR) noexcept
    {
        float lL[kMaxLanes], lR[kMaxLanes];
        splitStereo (inL, inR, lL, lR);
        mergeStereo (lL, lR, outL, outR);
    }

    // ── meters. fb432: read the engine's OWN numbers, and the SIGN of them — a
    //    device whose mechanism never ran still measures "different". ──────────
    float meterXoverHz  (int k) const noexcept
    { return (k < 0 || k > nLanes_ - 2) ? 0.0f : std::exp (logSm_[k]); }
    float meterSpan     ()      const noexcept { return spanNow_; }
    float meterLanePeak (int i) const noexcept { return (i < 0 || i >= kMaxLanes) ? 0.0f : lanePk_[i]; }
    /** The RESOLVED solo/mute gate — what actually multiplies the lane, not what
     *  the parameter asked for. This is the number that proves solo engaged. */
    float meterLaneGate (int i) const noexcept { return (i < 0 || i >= kMaxLanes) ? 0.0f : gGate_[i].value(); }
    float meterLaneGain (int i) const noexcept { return (i < 0 || i >= kMaxLanes) ? 0.0f : gGain_[i].value(); }
    float meterLaneWidth(int i) const noexcept { return (i < 0 || i >= kMaxLanes) ? 0.0f : gWidth_[i].value(); }
    float meterMix      ()      const noexcept { return mxNow_; }
    /** The phase-matched dry from the LAST splitStereo — the thing every lane
     *  must sum back to. spl_cert §A nulls against exactly this. */
    float dryAlignedL   ()      const noexcept { return dryL_; }
    float dryAlignedR   ()      const noexcept { return dryR_; }
    static constexpr int latencySamples() noexcept { return 0; }   // and always will be

private:
    static constexpr int kCoefEvery = 32;      // 0.67 ms at 48 k — see the CPU note

    /** Glide the crossovers in the LOG domain (octaves feel linear and a stepped
     *  knob is a zipper), and refresh the tan()-based coefficients on an
     *  amortised counter rather than every sample.
     *
     *  ⚠️ THE GLIDE IS NOT LOAD-BEARING and MUTATION.md records it as such.
     *  Deleting it turns zero gates red, and the reason is real rather than a
     *  weak gate: a TPT filter's coefficient change is STATE-CONTINUOUS, so a
     *  crossover snap produces no sample discontinuity at all — measured across
     *  a 5 %→95 % Split jump in ONE setParams, the peak |Δy| is 0.102 without
     *  the glide and 0.124 WITH it. It is kept because it is the house law for
     *  every continuous param and it becomes load-bearing the moment automation
     *  arrives at a coarser granularity than one 64-sample block.
     *
     *  The REFRESH, by contrast, is load-bearing and gated: delete it and every
     *  parameter still appears to work, because the first block already wrote
     *  the coefficients — only changes AFTER the first block stop resolving.
     *  That is fb350's pool law in miniature, and spl_cert §E measures it at
     *  26.4 dB of error. */
    inline void advanceXover() noexcept
    {
        bool refresh = forceCoef_;
        if (! logPrimed_)
        { for (int k = 0; k < kMaxLanes - 1; ++k) logSm_[k] = logTgt_[k]; logPrimed_ = true; refresh = true; }
        else
            for (int k = 0; k < kMaxLanes - 1; ++k) logSm_[k] += (logTgt_[k] - logSm_[k]) * aLog_;

        if (--coefCount_ <= 0) { coefCount_ = kCoefEvery; refresh = true; }
        if (! refresh) return;
        forceCoef_ = false;

        if (forceState_)
        {
            // A Slope or Type change swaps to a different filter bank; a stale
            // bank re-entering is a thump. Clear it, and fade the switch at the
            // caller (the house fade-swap-recover idiom).
            forceState_ = false;
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < kMaxLanes - 1; ++k)
                { xo_[c][k].reset(); dryAP_[c][k].reset(); bandAP_[c][k].reset(); }
        }

        const int NX = nLanes_ - 1;
        for (int k = 0; k < NX; ++k)
        {
            const float f = std::exp (logSm_[k]);
            for (int c = 0; c < 2; ++c) { xo_[c][k].set (f, fs_, order_); dryAP_[c][k].set (f, fs_, order_); }
        }
        // The band-alignment pairs: [0] = band 0 vs crossover 1 ·
        //                           [1] = band 0 vs crossover 2 ·
        //                           [2] = band 1 vs crossover 2.
        if (nLanes_ >= 3)
        { const float f1 = std::exp (logSm_[1]);
          for (int c = 0; c < 2; ++c) bandAP_[c][0].set (f1, fs_, order_); }
        if (nLanes_ >= 4)
        { const float f2 = std::exp (logSm_[2]);
          for (int c = 0; c < 2; ++c) { bandAP_[c][1].set (f2, fs_, order_); bandAP_[c][2].set (f2, fs_, order_); } }
    }

    float fs_ = 48000.0f, aLog_ = 0.002f;
    int   type_ = kDefaultType, nLanes_ = 3, order_ = 4, coefCount_ = 0;
    bool  matrix_ = false, forceCoef_ = true, forceState_ = true, logPrimed_ = false;

    spl_detail::Xover   xo_    [2][kMaxLanes - 1];
    spl_detail::AlignAP dryAP_ [2][kMaxLanes - 1];
    spl_detail::AlignAP bandAP_[2][kMaxLanes - 1];

    float logTgt_[kMaxLanes - 1] = { 5.35f, 6.9f, 8.3f };
    float logSm_ [kMaxLanes - 1] = { 5.35f, 6.9f, 8.3f };
    float spanNow_ = 1.0f;

    dyn::Glide gGain_[kMaxLanes], gGate_[kMaxLanes], gWidth_[kMaxLanes];
    dyn::Glide gPanL_[kMaxLanes], gPanR_[kMaxLanes], gFlip_[kMaxLanes], gMix_;
    float gainT_ [kMaxLanes] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float gateT_ [kMaxLanes] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float widthT_[kMaxLanes] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float panLT_ [kMaxLanes] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float panRT_ [kMaxLanes] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float flipT_ [kMaxLanes] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float mixT_ = 1.0f, mxNow_ = 1.0f;
    float dryL_ = 0.0f, dryR_ = 0.0f, lanePk_[kMaxLanes] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

} // namespace tw
