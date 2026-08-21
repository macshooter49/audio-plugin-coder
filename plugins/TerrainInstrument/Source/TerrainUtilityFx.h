#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  TerrainUtilityFx — THE UTILITY STRIP (rack device kind 14, fb445)
//
//  Max, verbatim: "Utility is the most pragmatic use of the effects channel
//  rack... I kind of look at it like GLUE. Utility doesn't even have to have a
//  visualizer, I think Utility should just have a whole bunch of BUTTONS on the
//  front, because it's just like — what's that for? It's just utility."
//
//  So this is a mixing-desk channel strip, not an effect. Its job is the seven
//  things the plugin genuinely CANNOT do today (verified by grep, fb445 recon):
//
//    1. GAIN IN dB, INSIDE THE CHAIN. Nothing in the rack can re-level between
//       two devices. Master Output is post-everything; per-osc Level is
//       pre-rack. This one control justifies the device on its own. And t = 0
//       is a glided TRUE −inf — an automatable mute, not a floor.
//    2. POLARITY. Grep found NO user-facing polarity invert anywhere in the
//       plugin. `Flip L` / `Flip R` are it.
//    3. DC BLOCK AS A CONTROL. ~10 engines carry one internally; none exposes
//       it. Here it is a pill plus a tunable corner (`Rumble`).
//    4. MONO BELOW. Widen's `Low Keep` is fixed and per-device. There is no
//       full-band, tunable side-HPF mono-maker anywhere. There is now.
//    5. BALANCE OF THE CHAIN SIGNAL. Pan exists only pre-rack, per-osc.
//    6. A CHANNEL MATRIX. Swap, mono sum, difference, L->both, R->both, solos.
//    7. M/S ROTATION. `Twist`, bounded to +-40 degrees.
//
//  ── WHAT THIS DEVICE DELIBERATELY DOES **NOT** OWN ───────────────────────────
//  * `Width`. The WIDEN device (kind 10) owns that word and that math as a front
//    hero. More importantly Widen's Width at 1.0 is a full 90-degree M/S
//    rotation that KILLS THE MID ENTIRELY — a written contract (R11) whose cert
//    REQUIRES `corr <= -0.9` and a mono fold-down at <= -25 dB
//    (`Source/TerrainWidenFx.h:71-73`). That is the correct top of a creative
//    widener and the WRONG behaviour for a mixing desk. Utility therefore owns
//    the BOUNDED version under a different name — `Image`, 0 % mono / 100 %
//    neutral / 300 % wide — and the MID IS FLOORED AT -6 dB BY CONSTRUCTION so
//    a mono fold-down can never delete the signal. utl_cert §C proves both
//    halves of that contrast with numbers, side by side.
//  * `Tilt` / `Pivot`. The EQUALIZER owns spectral tilt as `Slant`. Cut entirely
//    — Utility has NO tone controls at all, which is the honest shape for a
//    strip that sits in a rack next to a 7-type EQ.
//  * A dropdown called `Route`. It collides with the Flanger's back dropdown 2,
//    with the Bode device's back dropdown 2, AND with the route-pill row every
//    card already has. The channel matrix is called `Wiring`.
//
// ═════════════════════════════════════════════════════════════════════════════
//  THE ROSTER — every name grepped against Source/ui/public/index.html AND
//  Source/*.{h,cpp} for 'Name', "Name" and >Name<. "FREE" = zero hits anywhere.
//  (The tree moves under us — Bode landed in index.html mid-build — so the
//  collision column records what was true at fb445 and how it was checked.)
//
//  HEADER PILL — `Type`  (kNumTypeSlots = 8 frozen · 5 live)   THE IMAGE LAW
//    Type is the header pill (`DEVS[].tp`), never a back dropdown (fb418).
//    0 `Strip`   FREE   desk M/S scaling. 0 % = mono, 100 % = neutral,
//                       300 % = wide; mid floored at -6 dB. THE DEFAULT.
//                       (`Desk` was the first choice and is TAKEN — it is an
//                        Equalizer Character, TerrainEqualizerFx.h:813.)
//    1 `Turn`    FREE   bounded CONSTANT-POWER rotation. Unlike `Strip` it
//                       manufactures side from mid, so it widens a MONO source.
//                       A different mechanism, not a different flavour.
//    2 `Outer`   FREE   side gain ONLY; the mid is bit-preserved, so a mono
//                       fold-down is INVARIANT under Image. (`Sides` TAKEN —
//                        a Chorus Character, TerrainChorusFx.h:97.)
//    3 `Canopy`  FREE   band-split: Image acts only ABOVE `Hinge`. The bass
//                       stays exactly where it was. (`Splay` TAKEN — a Harmonic
//                        sculpt mode, PluginProcessor.cpp:3054.)
//    4 `Cellar`  FREE   the mirror: Image acts only BELOW `Hinge`.
//                       (`Root` TAKEN — a Harmonic partial name, :3056.)
//    5-7         reserved slots. setParams clamps into 0..4. Cardinality is
//                FROZEN AT 8 on day one (fb373: a choice param normalised on a
//                dropdown's option count silently hands you a different
//                machine). Widen shipped 6 live of 8 the same way.
//
//  FRONT — 3 heroes + Mix
//    `Gain`   FREE*  -inf .. +30 dB, unity at exactly 2/3 travel. t = 0 is a
//                    glided true zero. (*The only hits are internal param-key
//                    fragments in the SYNTH's own EQ pane — `eqB1Gain`,
//                    'modulate-gain'. Not a label, not in the rack.)
//    `Image`  FREE   0 % mono .. 100 % neutral .. 300 % wide. Mid never zero.
//    `Steer`  FREE   balance, constant power, unity at centre, one channel
//                    fully muted at either end. (`Balance` TAKEN — a Widen back
//                     knob, index.html:8355. `Lean` TAKEN — a Harmonic partial,
//                     PluginProcessor.cpp:3055.)
//    `Mix`    shared slot every device has. 0 passes the dry BIT-IDENTICALLY.
//
//  BACK DROPDOWN 1 — `Character` (kNumChars = 8, all live)   THE GUARD
//    What the strip does when you push it into its rail. Every entry is a
//    MECHANISM, and every entry is STATIC (memoryless or filter-based): there
//    is no envelope follower, no attack, no release, no ratio anywhere in this
//    file. That is deliberate — CONTRACT R2 gives Compress "everything
//    single-band", and the line between a clipper and a compressor is exactly
//    the presence of a time constant. Utility stays on the clipper side of it.
//    0 `Cushion` FREE  C1 tanh ceiling; `Clamp` = the knee.
//    1 `Brick`   FREE  hard rail; `Clamp` blends FOLDBACK (0) .. clip (1).
//    2 `Coil`    FREE  transformer: only the band BELOW `Hinge` sees the curve;
//                      the top passes clean. (`Iron` TAKEN — a Bode Character,
//                       index.html:8429, landed mid-build.)
//    3 `Creep`   FREE  slew limit: the RATE of change is capped, so highs
//                      soften and lows pass; `Clamp` = the slew corner.
//                      (`Slew` TAKEN — a DIODE distortion back label, :9301.)
//    4 `Tuck`    FREE  asymmetric rails; `Clamp` = the asymmetry. Makes even
//                      harmonics AND a DC offset — which the `DC` pill, sitting
//                      downstream, is then there to catch. The two controls
//                      interact on purpose.
//    5 `Rail`    FREE  the level is max(|L|,|R|) and the gain goes to BOTH, so
//                      the image COLLAPSES under load. Real desk-bus behaviour.
//    6 `Ripple`  FREE  the excess above the rail is mirrored into the OTHER
//                      channel — the strip leaks when you push it.
//    7 `Fuse`    FREE  R11: `Rail` + foldback + a halved rail + 4x more drive,
//                      all at once. Past its design. Destructive on purpose.
//
//  BACK DROPDOWN 2 — `Wiring` (kNumWireSlots = 8 frozen · 6 live)  THE MATRIX
//    `Wiring` FREE as a dropdown key.
//    0 `Through`    FREE  identity.
//    1 `Difference` FREE  (L-R)/2 to BOTH. A correlated input goes SILENT —
//                         the mono-compatibility monitor.
//    2 `L To Both`  FREE
//    3 `R To Both`  FREE
//    4 `L Only`     FREE  left stays left, right is muted.
//    5 `R Only`     FREE
//    6-7            reserved. NOTE: `Swap` and `Sum` are deliberately NOT here
//                   — they are pills. A control that exists as both a pill and
//                   a dropdown entry on the same card is the exact "double" the
//                   house rule forbids.
//
//  BACK — 8 knobs (4x2, fb275)
//    `Strain`     FREE  drive into the Character's rail, 0 .. +48 dB, with an
//                       exact reciprocal makeup so sat'(0) == 1 (the Bode law).
//                       EXACTLY 0 is a bit-exact bypass of the whole guard.
//    `Clamp`      FREE  the Character's second axis (knee / fold / asymmetry /
//                       slew corner). Continuous — never a stepped selector.
//    `Mono Below` FREE  the side-path HPF corner. 0 is an OFF detent (faded,
//                       not switched); the first increment is already a real
//                       50 Hz bass-mono, and the top is 1.5 kHz.
//    `Slope`      FREE  the steepness of EVERY crossover in the device,
//                       1 pole .. 3 poles, CONTINUOUSLY blended (fb444: a
//                       floor()'d order is dead travel at the bottom).
//    `Twist`      FREE  M/S rotation, bipolar +-40 deg, centre = none.
//                       (`Rotate` TAKEN — a bit-crush overflow mode, :9331.)
//    `Rumble`     FREE  the DC / subsonic block corner, 2 .. 160 Hz.
//    `Bleed`      FREE  inter-channel crosstalk ABOVE `Hinge`, 0 .. 100 %.
//                       100 % = the top of the image fully coupled while the
//                       bottom stays wide — the exact inverse of `Mono Below`.
//    `Hinge`      FREE  the crossover `Bleed`, `Coil`, `Canopy` and `Cellar`
//                       all share, 60 Hz .. 6 kHz. (`Corner` TAKEN — a FOLD
//                        distortion back label, index.html:9302.)
//
//  FRONT PILLS — 6 booleans, separate from the 11. Max asked for buttons.
//    `Flip L` FREE · `Flip R` FREE   per-channel polarity.
//    `Trade`  FREE   exchange L and R. (`Swap` TAKEN — a Widen Field option.)
//    `Sum`    FREE   (L+R)/2 to both.
//    `DC`     FREE   engage the DC / subsonic block at `Rumble`.
//    `Dim`    FREE   a fixed, glided -20 dB. The desk button.
//
//  SIGNAL ORDER (and why):
//    dry tap -> Wiring -> Flip L/R -> Trade -> Sum -> Bleed -> Gain (x Dim)
//            -> Guard -> DC block -> Mono Below -> Image + Twist -> Steer -> Mix
//    The matrix and polarity come FIRST because they define what the signal IS.
//    The DC block sits AFTER the guard so it catches the DC that `Tuck` makes,
//    not only what arrived. Geometry comes after level because a rotation of a
//    clipped signal is not the same as clipping a rotated one, and the desk
//    order is the one people expect.
//
//  ⚠️ THE RAIL TRACKS THE FADER. `Gain` is upstream of the guard, so the rail
//     is `kRail * gain`: turning the fader up gives you MORE LEVEL, not more
//     clipping, and `Strain` is the only thing that decides how hard the strip
//     is pushed. Without this the two knobs fight and Gain stops being a gain.
//
//  🔥 TIMIDITY LAW (fb313). The FX bus sits near -26 dBFS. A guard whose rail is
//     at 0 dBFS never engages on that bus and its knob is dead for the first
//     half of its travel. `kRail` is therefore 0.05 == -26 dBFS: the FIRST
//     increment of `Strain` already shapes bus-level material, and +48 dB at
//     the top is a square wave. The escape hatch is `Strain == 0`, which is a
//     bit-exact bypass, not a soft one.
//
//  CPU: the transparent path is genuinely transparent — every optional section
//  (guard, DC, bleed, mono-below, the whole M/S block) is behind a per-block
//  bool computed in setParams, so a Utility card at its defaults costs 4 mults
//  and 2 compares per sample. Fully loaded it is ~90 mults/sample/pair, still
//  under the shipped Chorus. NEVER oversampled: the only nonlinearity is the
//  guard, and the guard is the thing the user asked for.
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace tw {

// ═════════════════════════════════════════════════════════════ local helpers
// Self-contained by law: no JUCE, no TerrainFilters.h, no DynamicsCore.h. The
// cert compiles this against the shim with nothing else on the include path.
namespace utl_detail {

inline float fastTanh (float x) noexcept
{
    if (x >  5.0f) return  1.0f;
    if (x < -5.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// One-pole low-pass. g in [0,1), so y converges to x — cut-only by construction.
struct LP1
{
    float g = 0.0f, z = 0.0f;
    void  reset() noexcept { z = 0.0f; }
    void  setG   (float gg) noexcept { g = gg; }
    // Denormal flush: ScopedNoDenormals is NOT assumed (CONTRACT §2). A one-pole
    // decaying toward zero is exactly where subnormals live and cost 100x.
    inline float process (float x) noexcept
    { z += g * (x - z); if (std::fabs (z) < 1e-25f) z = 0.0f; return z; }
};

// ── THE CROSSOVER. Three cascaded one-poles with a CONTINUOUS order blend.
//    fb444 is the reason this is not `for (k = 0; k < (int) order; ++k)`: Bode
//    shipped a `floor(blur * 4)` section count and the first sixth of that knob
//    was measurably, exactly dead. An order that steps also jumps the group
//    delay, which clicks. So all three sections always run (their state stays
//    warm) and `Slope` crossfades between the 1-, 2- and 3-pole outputs.
struct XOver
{
    LP1 s[3];
    float ord = 0.0f;                 // 0 = 1 pole ... 2 = 3 poles
    void reset() noexcept { for (int i = 0; i < 3; ++i) s[i].reset(); }
    void setHz (float hz, float fs) noexcept
    {
        const float f = std::max (2.0f, std::min (hz, 0.45f * fs));
        float g = 1.0f - std::exp (-6.2831853f * f / fs);
        g = std::max (0.0f, std::min (1.0f, g));
        for (int i = 0; i < 3; ++i) s[i].setG (g);
    }
    void setOrder (float t) noexcept { ord = 2.0f * clamp01 (t); }
    inline float lp (float x) noexcept
    {
        const float y1 = s[0].process (x);
        const float y2 = s[1].process (y1);
        const float y3 = s[2].process (y2);
        const int   i  = ord < 1.0f ? 0 : 1;
        const float f  = ord - (float) i;
        const float a  = (i == 0 ? y1 : y2);
        const float b  = (i == 0 ? y2 : y3);
        return a + f * (b - a);
    }
};

// One-pole DC / subsonic high-pass with a tunable corner.
struct DCBlock
{
    float R = 0.995f, xPrev = 0.0f, yPrev = 0.0f;
    void  reset() noexcept { xPrev = yPrev = 0.0f; }
    void  setHz (float hz, float fs) noexcept
    {
        R = std::exp (-6.2831853f * std::max (0.5f, hz) / fs);
        R = std::max (0.0f, std::min (0.99999f, R));
    }
    inline float process (float x) noexcept
    {
        float y = x - xPrev + R * yPrev;
        if (std::fabs (y) < 1e-25f) y = 0.0f;
        xPrev = x; yPrev = y;
        return y;
    }
};

// C1 soft ceiling: identity up to `a`, then a tanh bend that asymptotes to `c`.
//   y(a) = a and y'(a) = 1  ->  no corner, no click, exact ceiling at c.
// `a` is where the knee starts; a == c is an instantaneous hard corner.
inline float softCeil (float x, float c, float a) noexcept
{
    const float m = std::fabs (x);
    if (m <= a) return x;
    const float s = x < 0.0f ? -1.0f : 1.0f;
    const float w = c - a;
    if (w <= 1e-9f) return s * c;                       // degenerate == hard clip
    return s * (a + w * fastTanh ((m - a) / w));
}

// Hard rail with a continuous FOLDBACK <-> CLIP blend. fold = 0 is a full
// wavefolder (the excess is reflected, which is destructive and is the point);
// fold = 1 is a plain clip. Anything between is a partial reflection.
inline float railFold (float x, float c, float fold) noexcept
{
    const float m = std::fabs (x);
    if (m <= c) return x;
    const float s = x < 0.0f ? -1.0f : 1.0f;
    float over = m - c;
    // reflect repeatedly so an arbitrarily hot input stays bounded (BIBO).
    const float period = 2.0f * c;
    if (over > period) over -= period * std::floor (over / period);
    const float folded = (over <= c) ? (c - over) : (over - c);
    return s * (folded + fold * (c - folded));
}

// Every optional section in this device can be switched on and off by a pill or
// by a knob reaching zero, and switching a filter in or out of a live signal is
// a click. So nothing switches: each section CROSSFADES, and its state stays
// warm underneath. The snap at the bottom is what keeps "off" bit-exact — a
// one-pole never reaches zero, and "bypassed" has to be a fact, not an asymptote.
struct Xfade
{
    float target = 0.0f, v = 0.0f;
    void  reset() noexcept { v = target; }
    inline float step (float a) noexcept
    {
        v += a * (target - v);
        if (target == 0.0f && v < 1.0e-5f) v = 0.0f;
        return v;
    }
};

} // namespace utl_detail

// ═════════════════════════════════════════════════════════════════ the device
struct TerrainUtilityFx
{
    // ── Rack Law C: cardinality is FROZEN AT BIRTH. fb373 — a choice param
    //    normalised on the DROPDOWN's option count instead of the PARAM's
    //    cardinality silently played Cassette as Studio through four rounds of
    //    green measurement. Declare the final width on day one and grow into it.
    static constexpr int kNumTypeSlots = 8;   // the param's cardinality, FROZEN
    static constexpr int kNumTypes     = 5;   // live today
    static constexpr int kNumChars     = 8;   // all live
    static constexpr int kNumWireSlots = 8;   // FROZEN
    static constexpr int kNumWirings   = 6;   // live today

    static_assert (kNumTypes   <= kNumTypeSlots, "roster wider than its param");
    static_assert (kNumWirings <= kNumWireSlots, "roster wider than its param");
    static_assert (kNumChars   == 8,             "the house Character width");

    // ── ranges. Each ceiling is where the control stops being USEFUL, not where
    //    it stops being clean (fb325 / R11).
    static constexpr float kGainMinDb   = -60.0f;  // and t == 0 is a true -inf
    static constexpr float kGainMaxDb   =  30.0f;  // +30 dB on a -26 dBFS bus is
                                                   // +4 dBFS: it WILL hit the DAC.
    static constexpr float kGainUnityT  = 60.0f / 90.0f;          // exactly 0 dB
    static constexpr float kGainFoot    = 0.04f;   // below this the fader slides
                                                   // continuously into true zero
    static constexpr float kDimDb       = -20.0f;
    static constexpr float kImageMax    = 3.0f;    // 300 %: unlistenable on cans,
                                                   // which is the correct max
    static constexpr float kMidFloor    = 0.5f;    // -6 dB. THE CONTRACT: the mid
                                                   // is NEVER zero. Widen R11 is
                                                   // the opposite by design.
    static constexpr float kTwistMaxDeg = 40.0f;
    static constexpr float kMonoBelowMin =   50.0f;
    static constexpr float kMonoBelowHz  = 1500.0f; // forcing 1.5 kHz of spectrum
                                                    // to mono is absurd. Allowed.
    // fb445 — WHY THE BOTTOM IS A DETENT AND NOT 20 Hz. A side-HPF at 20 Hz is
    // inaudible, so a 20 Hz..1.2 kHz taper spends its first sixth doing nothing
    // measurable — the fb444 dead-travel failure, one knob over. `Mono Below`
    // at exactly 0 is OFF (crossfaded, so it does not click) and the very first
    // increment is already a real 50 Hz bass-mono.
    static constexpr float kRumbleMinHz = 2.0f;
    static constexpr float kRumbleMaxHz = 160.0f;  // eats the bass. Allowed.
    static constexpr float kHingeMinHz  = 60.0f;
    static constexpr float kHingeMaxHz  = 6000.0f;
    static constexpr float kStrainMaxDb = 48.0f;
    static constexpr float kRail        = 0.05f;   // -26 dBFS == THE BUS (fb313)

    struct Params
    {
        int   type = 0, chr = 0, wiring = 0;
        // front 3 + Mix
        float gain  = kGainUnityT;   // 0..1, 2/3 == unity, 0 == a glided -inf
        float image = 0.5f;          // 0 = mono · 0.5 = neutral · 1 = 300 %
        float steer = 0.5f;          // 0..1 bipolar balance
        float mix   = 1.0f;          // 0..1 equal power
        // back 8
        float strain    = 0.0f;      // 0..1 -> 0..+48 dB into the rail (0 = bypass)
        float clamp     = 0.5f;      // 0..1 the Character's second axis
        float monoBelow = 0.0f;      // 0..1 -> 20 Hz .. 1.2 kHz side HPF
        float slope     = 0.0f;      // 0..1 -> 1 .. 3 poles, continuous
        float twist     = 0.5f;      // 0..1 bipolar -> +-40 degrees
        float rumble    = 0.0f;      // 0..1 -> 2 .. 160 Hz
        float bleed     = 0.0f;      // 0..1 HF crosstalk
        float hinge     = 0.5f;      // 0..1 -> 60 Hz .. 6 kHz
        // pills
        bool  flipL = false, flipR = false, trade = false;
        bool  sum   = false, dc    = false, dim   = false;
    };

    static const char* const* typeNames() noexcept
    {
        static const char* const n[kNumTypeSlots] =
            { "Strip", "Turn", "Outer", "Canopy", "Cellar", "—", "—", "—" };
        return n;
    }
    static const char* const* charNames() noexcept
    {
        static const char* const n[kNumChars] =
            { "Cushion", "Brick", "Coil", "Creep", "Tuck", "Rail", "Ripple", "Fuse" };
        return n;
    }
    static const char* const* wiringNames() noexcept
    {
        static const char* const n[kNumWireSlots] =
            { "Through", "Difference", "L To Both", "R To Both", "L Only", "R Only", "—", "—" };
        return n;
    }

    // ── lifecycle ───────────────────────────────────────────────────────────
    // No delay line, no buffer, nothing to size — but prepare() is still the
    // only place anything may be computed from the sample rate, and reset()
    // never allocates (it cannot; there is nothing allocated).
    void prepare (double sampleRate, int instanceIndex = 0)
    {
        fs_ = (float) std::max (8000.0, sampleRate);
        (void) instanceIndex;          // nothing here is random or per-instance
        reset();
        setParams (p_);                // derive everything for the new rate
        mbXf_.reset(); bleedXf_.reset(); dcXf_.reset(); guardXf_.reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            bleedX_[c].reset(); coilX_[c].reset(); imgX_[c].reset();
            dcB_[c].reset(); slewZ_[c] = 0.0f;
        }
        msX_.reset();
        mbXf_.reset(); bleedXf_.reset(); dcXf_.reset(); guardXf_.reset();
        for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) matSm_[i][j] = matT_[i][j];
        gainSm_ = -1.0f; mixSm_ = -1.0f; steerLSm_ = 1.0f; steerRSm_ = 1.0f;
        mGainSm_ = 1.0f; sGainSm_ = 1.0f;
        corr_ = 0.0f; corrLL_ = corrRR_ = corrLR_ = 1e-9f;
        mGuard_ = 0.0f; mMid_ = 0.0f; mSide_ = 0.0f;
    }

    // ── per-block parameter intake (NEVER per sample) ───────────────────────
    void setParams (const Params& p) noexcept
    {
        p_ = p;

        // ── the matrix, built once per block. Wiring first (it defines what
        //    the signal IS), then polarity on the wiring's OUTPUT channels, then
        //    Trade, then Sum. That order is why `Flip L` + `Sum` gives you a
        //    difference — the classic desk trick, and it is meant to work here.
        {
            float m[2][2];
            switch (p.wiring < 0 ? 0 : (p.wiring >= kNumWirings ? 0 : p.wiring))
            {
                case 1: m[0][0]= 0.5f; m[0][1]=-0.5f; m[1][0]= 0.5f; m[1][1]=-0.5f; break; // Difference
                case 2: m[0][0]= 1.0f; m[0][1]= 0.0f; m[1][0]= 1.0f; m[1][1]= 0.0f; break; // L To Both
                case 3: m[0][0]= 0.0f; m[0][1]= 1.0f; m[1][0]= 0.0f; m[1][1]= 1.0f; break; // R To Both
                case 4: m[0][0]= 1.0f; m[0][1]= 0.0f; m[1][0]= 0.0f; m[1][1]= 0.0f; break; // L Only
                case 5: m[0][0]= 0.0f; m[0][1]= 0.0f; m[1][0]= 0.0f; m[1][1]= 1.0f; break; // R Only
                default:m[0][0]= 1.0f; m[0][1]= 0.0f; m[1][0]= 0.0f; m[1][1]= 1.0f; break; // Through
            }
            if (p.flipL) { m[0][0] = -m[0][0]; m[0][1] = -m[0][1]; }
            if (p.flipR) { m[1][0] = -m[1][0]; m[1][1] = -m[1][1]; }
            if (p.trade) { std::swap (m[0][0], m[1][0]); std::swap (m[0][1], m[1][1]); }
            if (p.sum)
            {
                const float a0 = 0.5f * (m[0][0] + m[1][0]);
                const float a1 = 0.5f * (m[0][1] + m[1][1]);
                m[0][0] = m[1][0] = a0; m[0][1] = m[1][1] = a1;
            }
            for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) matT_[i][j] = m[i][j];
        }

        type_ = p.type < 0 ? 0 : (p.type >= kNumTypes ? 0 : p.type);
        chr_  = p.chr  < 0 ? 0 : (p.chr  >= kNumChars ? kNumChars - 1 : p.chr);
        wire_ = p.wiring < 0 ? 0 : (p.wiring >= kNumWirings ? 0 : p.wiring);
        // 🔑 the clamp above is on kNumTypes / kNumWirings — the LIVE count —
        //    while the PARAM is kNumTypeSlots / kNumWireSlots wide. A reserved
        //    slot resolves to entry 0, loudly and predictably, instead of
        //    indexing off the end of a table (fb391: that was auval 139).

        // ── the fader.
        gainT_ = faderGain (p.gain);
        if (p.dim) gainT_ *= dbToLin (kDimDb);

        // ── the guard.
        strainOn_ = utl_detail::clamp01 (p.strain) > 0.0f;   // EXACTLY 0 = bypass
        drive_    = std::pow (10.0f, kStrainMaxDb * utl_detail::clamp01 (p.strain) / 20.0f);
        invDrive_ = 1.0f / drive_;
        clampT_   = utl_detail::clamp01 (p.clamp);
        // knee position: 2 % of the rail at Clamp 0 (very soft) up to the rail
        // itself at Clamp 1 (an instantaneous corner). Continuous the whole way.
        kneeFrac_ = 1.0f - 0.98f * (1.0f - clampT_);
        // Creep's slew corner. 20 kHz (no limiting anyone can hear) down to
        // 200 Hz (everything above a bass note is a triangle wave).
        slewHz_   = 20000.0f * std::pow (0.01f, clampT_);

        // ── the crossovers. Slope is shared by every split in the device, so a
        //    sloppy 1-pole bass-mono and a surgical 3-pole one are one knob.
        const float hingeHz = kHingeMinHz * std::pow (kHingeMaxHz / kHingeMinHz,
                                                      utl_detail::clamp01 (p.hinge));
        for (int c = 0; c < 2; ++c)
        {
            bleedX_[c].setHz (hingeHz, fs_); bleedX_[c].setOrder (p.slope);
            coilX_ [c].setHz (hingeHz, fs_); coilX_ [c].setOrder (p.slope);
            imgX_  [c].setHz (hingeHz, fs_); imgX_  [c].setOrder (p.slope);
            dcB_   [c].setHz (kRumbleMinHz * std::pow (kRumbleMaxHz / kRumbleMinHz,
                                                       utl_detail::clamp01 (p.rumble)), fs_);
        }
        const float mbT  = utl_detail::clamp01 (p.monoBelow);
        const float mbHz = kMonoBelowMin * std::pow (kMonoBelowHz / kMonoBelowMin, mbT);
        msX_.setHz (mbHz, fs_); msX_.setOrder (p.slope);
        mbXf_.target    = mbT > 0.0f ? 1.0f : 0.0f;

        bleedXf_.target = utl_detail::clamp01 (p.bleed);
        dcXf_.target    = p.dc ? 1.0f : 0.0f;
        guardXf_.target = strainOn_ ? 1.0f : 0.0f;

        // ── the imaging. w: 0 = mono · 1 = neutral · 3 = 300 %, with neutral at
        //    the CENTRE of the knob (Widen learned this the hard way — a 0
        //    default on a width control is full mono, not "no widening").
        const float t = utl_detail::clamp01 (p.image);
        imgW_ = (t <= 0.5f) ? (2.0f * t)
                            : (1.0f + (kImageMax - 1.0f) * 2.0f * (t - 0.5f));
        imageNeutral_ = std::fabs (imgW_ - 1.0f) < 1e-6f;

        // Mid and side gains for the scaling Types. THE MID FLOOR IS THE
        // CONTRACT: at 300 % the mid is 1/sqrt(3) = 0.577, never below kMidFloor,
        // so a mono fold-down still has something in it. Widen R11 goes the
        // other way ON PURPOSE and its cert demands the mid DIE; this one's cert
        // demands it live. Two different instruments, one piece of math.
        sGainT_ = imgW_;
        mGainT_ = imgW_ <= 1.0f ? 1.0f
                                : std::max (kMidFloor, 1.0f / std::sqrt (imgW_));

        // Turn: a bounded rotation. theta from -40 deg (collapse toward the
        // centre) through 0 (identity) to +40 deg (which manufactures side out
        // of a MONO source — the thing scaling can never do). Bounded at 40, not
        // 45, so cos(theta) >= 0.766: the mid CANNOT be zeroed by the rotation.
        const float th = (imgW_ <= 1.0f ? -(1.0f - imgW_)
                                        :  (imgW_ - 1.0f) / (kImageMax - 1.0f))
                       * kTwistMaxDeg * 0.017453293f;
        turnCos_ = std::cos (th); turnSin_ = std::sin (th);

        // Twist: the same rotation as an independent, bipolar control.
        const float tw = (2.0f * utl_detail::clamp01 (p.twist) - 1.0f)
                       * kTwistMaxDeg * 0.017453293f;
        twCos_ = std::cos (tw); twSin_ = std::sin (tw);
        twistNeutral_ = std::fabs (tw) < 1e-9f;

        // The whole M/S block can be skipped when nothing in it is doing
        // anything — that is what makes the default path BIT-EXACT.
        // (neutrality is decided PER SAMPLE below, because the on/off sections
        //  crossfade — a section that is still fading out is not yet neutral.
        //  At w == 1 the rotation angle is 0 and both band-split laws multiply
        //  both bands by 1, so this holds for EVERY Type.)

        // ── balance. Constant power, unity at the centre, and either end mutes
        //    a channel outright (that is where a balance control stops being
        //    useful, so that is where it stops).
        const float b = utl_detail::clamp01 (p.steer);
        if (std::fabs (b - 0.5f) < 1.0e-6f) { steerLT_ = steerRT_ = 1.0f; }   // exact centre
        else
        {
            steerLT_ = std::cos (0.7853982f * (1.0f + (2.0f * b - 1.0f))) * 1.41421356f;
            steerRT_ = std::sin (0.7853982f * (1.0f + (2.0f * b - 1.0f))) * 1.41421356f;
        }

        mixT_ = utl_detail::clamp01 (p.mix);
        if (mixSm_   < 0.0f) mixSm_   = mixT_;      // first block: no ramp
        if (gainSm_  < 0.0f) gainSm_  = gainT_;
    }

    // ── the audio ───────────────────────────────────────────────────────────
    inline void processStereo (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float dryL = inL, dryR = inR;

        // ── smoothing. Every continuous control glides; a jumped gain clicks
        //    and a jumped pan zips. ~15 ms on the level controls, ~25 ms on the
        //    geometry (a rotation that snaps is a stereo click).
        const float aG = 1.0f - std::exp (-1.0f / (0.015f * fs_));
        const float aM = 1.0f - std::exp (-1.0f / (0.025f * fs_));
        gainSm_   += aG * (gainT_   - gainSm_);
        // A one-pole never REACHES zero, and this fader's zero is a mute the
        // user will automate. Snap the last inaudible sliver so "0 is silence"
        // is a fact and not an asymptote.
        if (gainT_ == 0.0f && gainSm_ < 1.0e-6f) gainSm_ = 0.0f;
        mixSm_    += aM * (mixT_    - mixSm_);
        steerLSm_ += aM * (steerLT_ - steerLSm_);
        steerRSm_ += aM * (steerRT_ - steerRSm_);
        mGainSm_  += aM * (mGainT_  - mGainSm_);
        sGainSm_  += aM * (sGainT_  - sGainSm_);
        const float mbAmt    = mbXf_.step (aM);
        const float bleedAmt = bleedXf_.step (aM);
        const float dcAmt    = dcXf_.step (aM);
        const float guardAmt = guardXf_.step (aM);

        float x[2] = { inL, inR };

        // ═══ 1+2. THE MATRIX — Wiring, both polarity pills, Trade and Sum, all
        //        of it, as FOUR SMOOTHED COEFFICIENTS.
        //        🔑 fb445 — THE MATRIX IS SMOOTHED, NOT SWITCHED. Every one of
        //        these controls is a discrete jump in the signal path and every
        //        one of them clicks if you just branch on it: changing Wiring
        //        under a sustained note, hitting Sum, and above all flipping
        //        POLARITY, which is a full-scale step discontinuity — the
        //        loudest click this device could possibly make. Because they are
        //        all LINEAR combinations of (L, R) they compose into one 2x2
        //        matrix, so smoothing the four numbers smooths every transition
        //        between every combination, for 4 mults and 4 adds a sample.
        //        The snap keeps "identity" an exact fact once it has arrived.
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
            {
                float& v = matSm_[i][j];
                const float t = matT_[i][j];
                v += aM * (t - v);
                if (std::fabs (t - v) < 1.0e-7f) v = t;
            }
        {
            const float l0 = x[0], r0 = x[1];
            x[0] = matSm_[0][0] * l0 + matSm_[0][1] * r0;
            x[1] = matSm_[1][0] * l0 + matSm_[1][1] * r0;
        }

        // ═══ 3. BLEED — crosstalk, but only ABOVE the hinge. A desk leaks at
        //        high frequency (capacitive coupling), not at low, and the
        //        musical result is the exact inverse of Mono Below: the TOP of
        //        the image closes while the bottom stays wide.
        if (bleedAmt > 0.0f)
        {
            const float lo0 = bleedX_[0].lp (x[0]), hi0 = x[0] - lo0;
            const float lo1 = bleedX_[1].lp (x[1]), hi1 = x[1] - lo1;
            const float n   = 1.0f / (1.0f + bleedAmt);
            x[0] = lo0 + (hi0 + bleedAmt * hi1) * n;
            x[1] = lo1 + (hi1 + bleedAmt * hi0) * n;
        }

        // ═══ 4. THE FADER.
        x[0] *= gainSm_; x[1] *= gainSm_;

        // ═══ 5. THE GUARD. Bypassed BIT-EXACTLY at Strain == 0 — but faded
        //        out, not switched out, so the bypass itself does not click.
        if (guardAmt > 0.0f)
        {
            float gl = x[0], gr = x[1];
            applyGuard (gl, gr);
            x[0] += guardAmt * (gl - x[0]);
            x[1] += guardAmt * (gr - x[1]);
        }

        // ═══ 6. DC. After the guard, so it catches what `Tuck` makes.
        if (dcAmt > 0.0f)
        {
            x[0] += dcAmt * (dcB_[0].process (x[0]) - x[0]);
            x[1] += dcAmt * (dcB_[1].process (x[1]) - x[1]);
        }

        // ═══ 7. GEOMETRY. Skipped entirely when neutral, which is what keeps
        //        the default path bit-exact.
        if (! (imageNeutral_ && twistNeutral_ && mbAmt <= 0.0f))
        {
            float m = 0.5f * (x[0] + x[1]);
            float s = 0.5f * (x[0] - x[1]);

            // Mono Below: high-pass the SIDE. Below the corner the side goes to
            // zero, which IS mono; above it the stereo is untouched. One filter,
            // no phase damage to the mid at all.
            if (mbAmt > 0.0f) s -= mbAmt * msX_.lp (s);

            switch (type_)
            {
                case 1:                                    // Turn — rotation
                {
                    const float mm = m * turnCos_ - s * turnSin_;
                    const float ss = m * turnSin_ + s * turnCos_;
                    m = mm; s = ss;
                } break;
                case 2:  s *= sGainSm_;                        break;  // Outer
                case 3:                                                // Canopy
                {
                    const float mLo = imgX_[0].lp (m), sLo = imgX_[1].lp (s);
                    m = mLo + (m - mLo) * mGainSm_;
                    s = sLo + (s - sLo) * sGainSm_;
                } break;
                case 4:                                                // Cellar
                {
                    const float mLo = imgX_[0].lp (m), sLo = imgX_[1].lp (s);
                    m = (m - mLo) + mLo * mGainSm_;
                    s = (s - sLo) + sLo * sGainSm_;
                } break;
                default: m *= mGainSm_; s *= sGainSm_;         break;  // Strip
            }

            if (! twistNeutral_)
            {
                const float mm = m * twCos_ - s * twSin_;
                const float ss = m * twSin_ + s * twCos_;
                m = mm; s = ss;
            }

            x[0] = m + s; x[1] = m - s;
            mMid_ = std::fabs (m); mSide_ = std::fabs (s);
        }

        else { mMid_ = std::fabs (0.5f * (x[0] + x[1])); mSide_ = std::fabs (0.5f * (x[0] - x[1])); }

        // ═══ 8. BALANCE.
        x[0] *= steerLSm_; x[1] *= steerRSm_;

        // ═══ 9. MIX. Equal power, and Mix 0 is EXACTLY the dry: cos(0) == 1.0f
        //        and sin(0) == 0.0f, both exact, so nothing is added and nothing
        //        is scaled. A utility that cannot be A/B'd against itself
        //        bit-for-bit is not a utility.
        const float wg = std::sin (1.5707963f * mixSm_);
        const float dg = std::cos (1.5707963f * mixSm_);
        outL = dg * dryL + wg * x[0];
        outR = dg * dryR + wg * x[1];

        // running stereo correlation, leaky, for the meter
        const float lr = 0.9995f;
        corrLL_ = lr * corrLL_ + outL * outL;
        corrRR_ = lr * corrRR_ + outR * outR;
        corrLR_ = lr * corrLR_ + outL * outR;
        corr_   = corrLR_ / std::sqrt (std::max (1e-18f, corrLL_ * corrRR_));
    }

    // ── meters. fb432 — read the engine's OWN numbers, and the SIGN of them: a
    //    device whose mechanism never ran still measures "different".
    float meterGainDb()  const noexcept
        { return gainSm_ <= 0.0f ? -200.0f : 20.0f * std::log10 (gainSm_); }
    float meterGuardDb() const noexcept { return mGuard_; }   // <= 0, dB of reduction
    float meterCorr()    const noexcept { return corr_; }     // -1 .. +1
    float meterMid()     const noexcept { return mMid_;  }
    float meterSide()    const noexcept { return mSide_; }
    int   meterWiring()  const noexcept { return wire_;  }
    int   meterType()    const noexcept { return type_;  }
    float meterImageW()  const noexcept { return imgW_;  }    // 0..3, the real factor
    float meterMidGain() const noexcept { return mGainT_; }   // THE FLOORED one
    // 🔑 fb432 — READ THE SIGN, not the magnitude. A polarity control that has
    //    silently stopped inverting still measures "a signal is present". The
    //    matrix meter is the engine's own answer to "where did each channel go,
    //    and with which sign": row = output channel, col = input channel.
    float meterMatrix (int row, int col) const noexcept { return matSm_[row][col]; }
    float meterPolarity (int ch) const noexcept
    {
        // the sign the channel's DOMINANT input contribution carries
        return (std::fabs (matSm_[ch][0]) >= std::fabs (matSm_[ch][1]))
               ? (matSm_[ch][0] < 0.0f ? -1.0f : 1.0f)
               : (matSm_[ch][1] < 0.0f ? -1.0f : 1.0f);
    }

private:
    static float dbToLin (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    // THE FADER LAW. Linear in dB across the whole travel, so every position is
    // a different level and the top third is not a repeated clamp. Unity is
    // SNAPPED at 2/3 travel: 0 dB is the position a user double-clicks back to
    // and it has to be exactly 1.0, not 1.0000005.
    static float faderGain (float t) noexcept
    {
        t = utl_detail::clamp01 (t);
        if (t <= 0.0f) return 0.0f;                       // a true, glided -inf
        if (std::fabs (t - kGainUnityT) < 1.0e-4f) return 1.0f;
        const float db = kGainMinDb + (kGainMaxDb - kGainMinDb) * t;
        float g = std::pow (10.0f, db * 0.05f);
        // The bottom 4 % slides continuously to zero, so there is no step from
        // -60 dB to silence — the mute is a fade, at every automation speed.
        if (t < kGainFoot) { const float u = t / kGainFoot; g *= u * u; }
        return g;
    }

    // ── THE GUARD. Eight mechanisms, no time constants anywhere (CONTRACT R2:
    //    Compress owns single-band dynamics; what separates a clipper from a
    //    compressor is exactly the attack/release this file does not have).
    //    The rail TRACKS THE FADER so Gain stays a gain (see the header note).
    //    The makeup is an exact 1/drive, so sat'(0) == 1 and Strain can never
    //    become a second, secret level control.
    inline void applyGuard (float& l, float& r) noexcept
    {
        const float c = std::max (1e-7f, kRail * std::max (1e-6f, gainSm_));
        const float a = c * kneeFrac_;
        const float preL = l, preR = r;
        float dl = l * drive_, dr = r * drive_;

        switch (chr_)
        {
            case 1:                                              // Brick
                dl = utl_detail::railFold (dl, c, clampT_);
                dr = utl_detail::railFold (dr, c, clampT_);
                break;

            case 2:                                              // Coil
            {   // only the band BELOW the hinge sees the curve; the top passes.
                const float lo0 = coilX_[0].lp (dl), hi0 = dl - lo0;
                const float lo1 = coilX_[1].lp (dr), hi1 = dr - lo1;
                dl = utl_detail::softCeil (lo0, c, a) + hi0;
                dr = utl_detail::softCeil (lo1, c, a) + hi1;
            } break;

            case 3:                                              // Creep
            {   // a SLEW limit, not an amplitude limit: the rate of change is
                // capped, so a bass note walks through untouched and a hi-hat
                // turns into a triangle. Soft-limited so the corner never ticks.
                const float step = std::max (1e-9f, c * 6.2831853f * slewHz_ / fs_);
                for (int ch = 0; ch < 2; ++ch)
                {
                    float& v = (ch == 0 ? dl : dr);
                    const float d = v - slewZ_[ch];
                    slewZ_[ch] += step * utl_detail::fastTanh (d / step);
                    v = slewZ_[ch];
                }
            } break;

            case 4:                                              // Tuck
            {   // asymmetric rails — a single-supply stage. Even harmonics AND a
                // DC offset, which is exactly why the DC block sits downstream.
                const float cn = c * (1.0f - 0.8f * clampT_);
                dl = dl >= 0.0f ? utl_detail::softCeil (dl, c, a)
                                : utl_detail::softCeil (dl, cn, cn * kneeFrac_);
                dr = dr >= 0.0f ? utl_detail::softCeil (dr, c, a)
                                : utl_detail::softCeil (dr, cn, cn * kneeFrac_);
            } break;

            case 5:                                              // Rail
            {   // ONE detector for both channels, ONE gain to both: the image
                // collapses toward the centre under load. Desk-bus behaviour,
                // and audibly different from two independent limiters.
                const float lv = std::max (std::fabs (dl), std::fabs (dr));
                const float g  = lv > 1e-9f ? utl_detail::softCeil (lv, c, a) / lv : 1.0f;
                dl *= g; dr *= g;
            } break;

            case 6:                                              // Ripple
            {   // the excess above the rail is mirrored into the OTHER channel.
                const float yl = utl_detail::softCeil (dl, c, a);
                const float yr = utl_detail::softCeil (dr, c, a);
                const float el = dl - yl, er = dr - yr;
                dl = yl + 0.8f * er; dr = yr + 0.8f * el;
            } break;

            case 7:                                              // Fuse — R11
            {   // past its design: 4x more drive into a HALVED rail, one linked
                // detector, then a foldback on whatever is still standing. This
                // is meant to be unusable to most people and perfect for one.
                const float c2 = 0.5f * c;
                dl *= 4.0f; dr *= 4.0f;
                const float lv = std::max (std::fabs (dl), std::fabs (dr));
                const float g  = lv > 1e-9f ? utl_detail::softCeil (lv, c2, c2 * kneeFrac_) / lv : 1.0f;
                dl = utl_detail::railFold (dl * g, c2, clampT_);
                dr = utl_detail::railFold (dr * g, c2, clampT_);
                dl *= 0.25f; dr *= 0.25f;
            } break;

            default:                                             // Cushion
                dl = utl_detail::softCeil (dl, c, a);
                dr = utl_detail::softCeil (dr, c, a);
                break;
        }

        l = dl * invDrive_; r = dr * invDrive_;

        // the meter reads the REDUCTION the guard actually applied, signed.
        const float pre = std::max (std::fabs (preL), std::fabs (preR));
        const float pst = std::max (std::fabs (l),    std::fabs (r));
        if (pre > 1e-7f)
            mGuard_ = 0.98f * mGuard_ + 0.02f * (20.0f * std::log10 (std::max (1e-7f, pst / pre)));
    }

    Params p_ {};
    float  fs_ = 48000.0f;

    utl_detail::XOver   bleedX_[2], coilX_[2], imgX_[2], msX_;
    utl_detail::DCBlock dcB_[2];
    float slewZ_[2] { 0.0f, 0.0f };

    int   type_ = 0, chr_ = 0, wire_ = 0;
    float gainT_ = 1.0f, gainSm_ = -1.0f;
    float mixT_ = 1.0f, mixSm_ = -1.0f;
    float steerLT_ = 1.0f, steerRT_ = 1.0f, steerLSm_ = 1.0f, steerRSm_ = 1.0f;
    float drive_ = 1.0f, invDrive_ = 1.0f, clampT_ = 0.5f, kneeFrac_ = 0.51f;
    float slewHz_ = 20000.0f;
    float imgW_ = 1.0f, mGainT_ = 1.0f, sGainT_ = 1.0f, mGainSm_ = 1.0f, sGainSm_ = 1.0f;
    float turnCos_ = 1.0f, turnSin_ = 0.0f, twCos_ = 1.0f, twSin_ = 0.0f;
    utl_detail::Xfade mbXf_, bleedXf_, dcXf_, guardXf_;
    float matT_[2][2] { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    float matSm_[2][2] { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    bool  strainOn_ = false, imageNeutral_ = true, twistNeutral_ = true;

    float corr_ = 0.0f, corrLL_ = 1e-9f, corrRR_ = 1e-9f, corrLR_ = 1e-9f;
    float mGuard_ = 0.0f, mMid_ = 0.0f, mSide_ = 0.0f;
};

} // namespace tw
