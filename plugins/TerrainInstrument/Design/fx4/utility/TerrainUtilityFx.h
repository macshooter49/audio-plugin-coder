#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  TerrainUtilityFx — THE UTILITY STRIP (rack device kind 14). fb450: REBUILT AS A CHANNEL STRIP.
//
//  fb444–fb448 shipped a desk-channel fantasy: eight Characters, six Wirings, a hinge, a bleed, a
//  slope, a clamp. Max, on the installed build, at the end of the week the rack was finished:
//    "the cut at doesn't do anything, mono above doesn't do anything, crossover doesn't do anything,
//     slope barely, shape does not, drive only makes it QUIETER, the characters don't do anything and
//     wiring doesn't do anything … remove character and wiring … utility is supposed to be pragmatic:
//     basic things to help your signal wherever it's going … Serum 2's utility has a low pass, a high
//     pass, polarity … the next time I load up utility I need to hear a damn near change."
//  He was right on every count, and the Drive one was a design error, not a taste: the makeup was an
//  exact 1/drive (so the small-signal slope stayed 1), which put the top of the knob 21 dB DOWN. A
//  drive that gets quieter is a mute with a story.
//
//  So this is now the thing a utility IS — a channel strip — and the law that governs every control
//  here is Max's hard rule: EVERY KNOB AND EVERY LAMP MAKES AN AUDIBLE DIFFERENCE, ON A MONO SOURCE,
//  at its first increment and at its end (fb325 dramaticism · fb444 dead-travel). Mono Below is the
//  one exception by physics (it acts on the SIDE; a mono source has none) and it says so.
//
//  FRONT (unchanged): `Gain` (-60..+30 dB fader, unity SNAPPED at 2/3, a glided -inf at 0) ·
//  `Width` (0 % mono · 100 % neutral · 300 %, mid never zero — the bounded contrast with Widen R11) ·
//  `Pan` (constant power) · `Mix` (equal power, both endpoints EXACT).
//  LAMPS: `Flip L` · `Flip R` (polarity) · `Swap` — AT THE OUTPUT, after Pan, so it swaps whatever
//  the strip produced (a swap at the input of a mono source is nothing; at the output of a panned one
//  it is the whole image). CHASSIS: `Mono` (sum) · `Dim` (-20 dB).
//  BACK 8 — the strip, in signal order after the fader:
//    b1 `High Pass`  2-pole Butterworth (TPT SVF), 40 Hz .. 4 kHz log; 0 = OFF (a crossfaded detent:
//                    the first increment is ALREADY 40 Hz, no dead travel). 12 dB/oct.
//    b2 `Low Pass`   2-pole, 300 Hz .. 16 kHz log; 1 = OFF. 12 dB/oct.
//    b3 `Bass`       low shelf at 100 Hz, ±12 dB, centre = 0 (bit-exact bypass).
//    b4 `Air`        high shelf at 8 kHz, ±12 dB, centre = 0.
//    b5 `Mono Below` side high-pass, 0 = OFF, 50 Hz .. 1.5 kHz (the club-prep bass mono; side-only by
//                    construction — the Mid is untouched, the stereo above the corner survives).
//    b6 `Rotate`     mid/side rotation ±40° (bounded at 40 so cos >= 0.766 and the mid CANNOT be zeroed).
//                    On a MONO source it manufactures side — the one geometry control that is audible
//                    on mono, which is why it stayed when the rest of the M/S desk went.
//    b7 `Haas`       ±20 ms: left of centre delays the RIGHT channel (the image leans LEFT), right of
//                    centre delays the LEFT. Centre = 0 = bit-exact. A fractional delay with a GLIDED
//                    length (the comb-click law) — the classic "make a mono thing wide" move, and it is
//                    placed BEFORE the Mono sum on purpose: press Mono and you hear what a Haas does to
//                    mono compatibility, which is the lesson every engineer learns once.
//    b8 `Drive`      0 = bit-exact bypass; the first increment is already +12 dB into a tanh (the bus is
//                    -26 dBFS and nothing under +12 dB is audible there — the fb315 timidity law), the top is
//                    +40 dB = a square. LOUDNESS MAKEUP: the makeup is
//                    the gain that holds a sine at the bus level (-26 dBFS, fb313) at constant PEAK
//                    through the curve — so the drive gets DENSER and slightly LOUDER (a square at the
//                    same peak is +3 dB RMS over a sine), never quieter. Output ≤ 1.0 on any input
//                    (BIBO: makeup·tanh ≤ kPk/tanh(kPk) ≈ 1.0).
//  TYPES (header pill, unchanged roster, cardinality frozen at 8): how `Width` acts —
//    Strip (mid/side scaling) · Turn (a rotation: widening a MONO source) · Outer (side-only gain;
//    the mono fold-down invariant) · Canopy (Width acts above 350 Hz, the bass stays put) · Cellar
//    (the mirror: below 350 Hz). The hinge is FIXED at 350 Hz now that the Crossover knob is gone.
//  REMOVED (fb450): Character, Wiring, Shape, Slope, Mono Above, Crossover, the DC lamp (a 15 Hz DC
//  block is inaudible BY NATURE — which on this rack is a dead control). Their params are gone from
//  the plugin too: a declared parameter that nothing reads is the fb444 lie.
//
//  SIGNAL ORDER:  in → Flip L/R → Haas → Mono → Gain (×Dim) → High Pass → Low Pass → Bass → Air →
//                 Drive → [Mono Below → Width (per Type) → Rotate] → Pan → Swap → Mix
//
//  LAWS KEPT FROM fb444/fb445 (all measured then, all re-gated in utl_cert):
//    · every on/off section CROSSFADES (Xfade, snapped to an exact 0), state stays warm underneath;
//    · every continuous control GLIDES and SNAPS to its exact target (a float one-pole stalls);
//    · the matrices (flip/sum at the input, swap at the output) are SMOOTHED 2x2s, never branches;
//    · the default path is BIT-EXACT transparent (every optional section behind a per-block bool);
//    · the Mix endpoints are branched, not computed (cos(pi/2) is not zero in float);
//    · the fader's unity is snapped; its zero is a true, glided -inf.
//  CPU: the transparent strip costs ~8 mults/sample/pair; fully loaded ~60. Never oversampled: the only
//  nonlinearity is the tanh drive, on a bus that is limited downstream (the house convention).
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

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
inline float flushd  (float v) noexcept { return std::fabs (v) < 1e-25f ? 0.0f : v; }

// One-pole low-pass. g in [0,1), so y converges to x — cut-only by construction.
struct LP1
{
    float g = 0.0f, z = 0.0f;
    void  reset() noexcept { z = 0.0f; }
    void  setG   (float gg) noexcept { g = gg; }
    inline float process (float x) noexcept { z += g * (x - z); z = flushd (z); return z; }
};

// ── THE CROSSOVER (kept from fb445 — see the fb445 note in git history: `x - LP3(x)` is NOT a
//    three-pole high-pass; each section here is its own complementary pair, and the corner is
//    compensated for the order so the -3 dB point stays where it was put). Used by Mono Below
//    (the side HPF) and by the Canopy / Cellar hinge. Order fixed at 2 poles now (the Slope knob is gone).
struct XOver
{
    LP1 s[3];
    float ord = 0.0f;                 // 0 = 1 pole ... 2 = 3 poles
    void reset() noexcept { for (int i = 0; i < 3; ++i) s[i].reset(); }
    void setup (float hz, float slope01, float fs) noexcept
    {
        ord = 2.0f * clamp01 (slope01);
        static const float kComp[3] = { 1.0f, 1.5538f, 1.9615f };   // -3 dB alignment
        const int   i    = ord < 1.0f ? 0 : 1;
        const float f    = ord - (float) i;
        const float comp = kComp[i] + f * (kComp[i + 1] - kComp[i]);
        const float fc   = std::max (1.0f, std::min (hz / comp, 0.45f * fs));
        float g = 1.0f - std::exp (-6.2831853f * fc / fs);
        g = std::max (0.0f, std::min (1.0f, g));
        for (int k = 0; k < 3; ++k) s[k].setG (g);
    }
    inline float hp (float x) noexcept
    {
        const float h1 = x  - s[0].process (x);
        const float h2 = h1 - s[1].process (h1);
        const float h3 = h2 - s[2].process (h2);
        const int   i  = ord < 1.0f ? 0 : 1;
        const float f  = ord - (float) i;
        const float a  = (i == 0 ? h1 : h2);
        const float b  = (i == 0 ? h2 : h3);
        return a + f * (b - a);
    }
};

// ── A 2-pole Butterworth TPT state-variable filter (Simper). lp and hp from one tick. The same
//    topology the Splitter's LR4 and DynamicsCore's Svf1 run — written here because this header
//    includes nothing (the cert law).
struct Svf2
{
    float ic1 = 0.0f, ic2 = 0.0f, g = 0.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    static constexpr float k = 1.4142136f;
    void reset() noexcept { ic1 = ic2 = 0.0f; }
    void set (float fc, float fs) noexcept
    {
        const float f = std::max (5.0f, std::min (fc, 0.45f * fs));
        g = std::tan (3.14159265f * f / fs);
        const float den = 1.0f + g * (g + k);
        a1 = 1.0f / den; a2 = g * a1; a3 = g * a2;
    }
    inline void tick (float v0, float& lp, float& hp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushd (2.0f * v1 - ic1);
        ic2 = flushd (2.0f * v2 - ic2);
        lp = v2; hp = v0 - k * v1 - v2;
    }
};

// ── RBJ shelving biquad (S = 1), transposed direct form II. Designed per block from a GLIDED dB so a
//    knob sweep is a sequence of small coefficient steps, never a jump. At 0 dB the design is an exact
//    identity (b == a), and the section is SKIPPED anyway (the per-block bool), so bypass is bit-exact.
struct Shelf
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
    void reset() noexcept { z1 = z2 = 0.0f; }
    void design (bool high, float dB, float fc, float fs) noexcept
    {
        const float A  = std::pow (10.0f, dB / 40.0f);
        const float w  = 6.2831853f * std::max (5.0f, std::min (fc, 0.45f * fs)) / fs;
        const float cs = std::cos (w), sn = std::sin (w);
        const float al = sn * 0.70710678f;                 // S = 1: alpha = sn/2 * sqrt(2)
        const float sq = 2.0f * std::sqrt (A) * al;
        float B0, B1, B2, A0, A1, A2;
        if (! high)
        {
            B0 =      A * ((A + 1.0f) - (A - 1.0f) * cs + sq);
            B1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
            B2 =      A * ((A + 1.0f) - (A - 1.0f) * cs - sq);
            A0 =           (A + 1.0f) + (A - 1.0f) * cs + sq;
            A1 =   -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
            A2 =           (A + 1.0f) + (A - 1.0f) * cs - sq;
        }
        else
        {
            B0 =      A * ((A + 1.0f) + (A - 1.0f) * cs + sq);
            B1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
            B2 =      A * ((A + 1.0f) + (A - 1.0f) * cs - sq);
            A0 =           (A + 1.0f) - (A - 1.0f) * cs + sq;
            A1 =    2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
            A2 =           (A + 1.0f) - (A - 1.0f) * cs - sq;
        }
        const float n = 1.0f / A0;
        b0 = B0 * n; b1 = B1 * n; b2 = B2 * n; a1 = A1 * n; a2 = A2 * n;
    }
    inline float tick (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = flushd (b1 * x - a1 * y + z2);
        z2 = flushd (b2 * x - a2 * y);
        return y;
    }
};

// ── A short fractional delay (the Haas). Linear interpolation; the read position GLIDES (set by the
//    caller per sample), which is the comb-click law: a delay length that jumps clicks, one that
//    glides pitches for a moment and settles — the Delay device's own rule.
struct Haas
{
    std::vector<float> buf; int w = 0, mask = 0;
    void prepare (int maxSamples)
    {
        int sz = 16; while (sz < maxSamples + 4) sz <<= 1;
        buf.assign ((size_t) sz, 0.0f); mask = sz - 1; w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline float tick (float x, float dSamples) noexcept
    {
        if (buf.empty()) return x;
        buf[(size_t) w] = x;
        const float rp = (float) w - std::max (0.0f, dSamples);
        const int   i0 = (int) std::floor (rp);
        const float fr = rp - (float) i0;
        const float y0 = buf[(size_t) (i0 & mask)], y1 = buf[(size_t) ((i0 + 1) & mask)];
        w = (w + 1) & mask;
        return y0 + fr * (y1 - y0);
    }
};

// 🚨 fb445 — A FLOAT ONE-POLE STALLS, AND IT STALLS SHORT OF ITS TARGET (see git history for the
//    measurement: the polarity matrix parked at -0.999964). The snap sits ABOVE the stall point.
inline float glide (float v, float t, float a) noexcept
{
    const float d = t - v;
    if (std::fabs (d) <= 1.0e-4f * std::max (1.0f, std::fabs (t))) return t;
    return v + a * d;
}

// Every optional section crossfades in and out; "off" is a snapped fact, not an asymptote.
struct Xfade
{
    float target = 0.0f, v = 0.0f;
    void  reset() noexcept { v = target; }
    inline float step (float a) noexcept
    {
        v = glide (v, target, a);
        if (target == 0.0f && v < 1.0e-5f) v = 0.0f;
        return v;
    }
};

} // namespace utl_detail

// ═══════════════════════════════════════════════════════════════ the device
struct TerrainUtilityFx
{
    // ── Rack Law C: cardinality is FROZEN AT BIRTH (fb373).
    static constexpr int kNumTypeSlots = 8;   // the param's cardinality, FROZEN
    static constexpr int kNumTypes     = 5;   // live today
    static_assert (kNumTypes <= kNumTypeSlots, "roster wider than its param");

    // ── ranges. Each ceiling is where the control stops being USEFUL, not where it stops being clean.
    static constexpr float kGainMinDb    = -60.0f;
    static constexpr float kGainMaxDb    =  30.0f;
    static constexpr float kGainUnityT   = 60.0f / 90.0f;          // exactly 0 dB
    static constexpr float kGainFoot     = 0.04f;
    static constexpr float kDimDb        = -20.0f;
    static constexpr float kImageMax     = 3.0f;
    static constexpr float kMidFloor     = 0.5f;    // THE CONTRACT: the mid is NEVER zero (Widen R11 is the opposite by design)
    static constexpr float kRotateMaxDeg = 40.0f;
    static constexpr float kMonoBelowMin =   50.0f;
    static constexpr float kMonoBelowHz  = 1500.0f;
    static constexpr float kHpMinHz      =   40.0f;  // the first increment is already a real 40 Hz
    static constexpr float kHpMaxHz      = 4000.0f;  // past this a "high pass" is a telephone, which is where it stops being a utility
    static constexpr float kLpMinHz      =  300.0f;
    static constexpr float kLpMaxHz      = 16000.0f; // the last increment before OFF is already audible on anything bright
    static constexpr float kShelfDb      =   12.0f;
    static constexpr float kBassHz       =  120.0f;   // a 110 Hz fundamental sits INSIDE the shelf (the bass of a synth patch is the point)
    static constexpr float kAirHz        = 8000.0f;
    static constexpr float kHaasMaxMs    =   20.0f;
    static constexpr float kDriveMinDb   =   12.0f;   // THE TIMIDITY LAW (fb315): the bus is -26 dBFS, and a tanh does NOTHING audible under +12 dB of
                                                       // drive there — so the knob's first increment starts at +12 (crossfaded in from bypass), not at 0
    static constexpr float kDriveMaxDb   =   40.0f;   // 100x into the tanh: the top of the knob is a genuine square
    static constexpr float kHingeHz      =  350.0f;  // Canopy / Cellar: fixed now that the knob is gone
    static constexpr float kRail         = 0.05f;    // -26 dBFS == THE BUS (fb313)
    static constexpr float kPk           = kRail * 1.41421356f;   // the bus sine's PEAK — the drive's makeup reference

    struct Params
    {
        int   type = 0;
        // front 3 + Mix
        float gain  = kGainUnityT;   // 0..1, 2/3 == unity, 0 == a glided -inf
        float image = 0.5f;          // 0 = mono · 0.5 = neutral · 1 = 300 %
        float steer = 0.5f;          // 0..1 bipolar balance
        float mix   = 1.0f;          // 0..1 equal power
        // back 8 (b1..b8)
        float hp        = 0.0f;      // 0 = OFF · 40 Hz .. 4 kHz
        float lp        = 1.0f;      // 1 = OFF · 300 Hz .. 16 kHz
        float bass      = 0.5f;      // ±12 dB shelf @ 100 Hz, 0.5 = 0
        float air       = 0.5f;      // ±12 dB shelf @ 8 kHz,  0.5 = 0
        float monoBelow = 0.0f;      // 0 = OFF · 50 Hz .. 1.5 kHz side HPF
        float rotate    = 0.5f;      // ±40°, 0.5 = 0
        float haas      = 0.5f;      // ±20 ms, 0.5 = 0 (left of centre delays R → the image leans left)
        float drive     = 0.0f;      // 0 = bypass · 0 .. +36 dB into tanh, loudness-matched
        // lamps + chassis
        bool  flipL = false, flipR = false, swap = false;
        bool  sum   = false, dim   = false;
    };

    static const char* const* typeNames() noexcept
    {
        static const char* const n[kNumTypeSlots] =
            { "Strip", "Turn", "Outer", "Canopy", "Cellar", "—", "—", "—" };
        return n;
    }

    // ── the knob → value laws. Exposed because the UI readouts, the cert and the presets must all
    //    agree (a mapping authored in two places is the fb373 defect).
    static float hpHzFor       (float t) noexcept { t = utl_detail::clamp01 (t); return t <= 0.0f ? 0.0f : kHpMinHz * std::pow (kHpMaxHz / kHpMinHz, t); }
    static float lpHzFor       (float t) noexcept { t = utl_detail::clamp01 (t); return t >= 1.0f ? 0.0f : kLpMinHz * std::pow (kLpMaxHz / kLpMinHz, t); }
    static float shelfDbFor    (float t) noexcept { return (2.0f * utl_detail::clamp01 (t) - 1.0f) * kShelfDb; }
    static float monoBelowHzFor(float t) noexcept { t = utl_detail::clamp01 (t); return t <= 0.0f ? 0.0f : kMonoBelowMin * std::pow (kMonoBelowHz / kMonoBelowMin, t); }
    static float rotateDegFor  (float t) noexcept { return (2.0f * utl_detail::clamp01 (t) - 1.0f) * kRotateMaxDeg; }
    static float haasMsFor     (float t) noexcept { return (2.0f * utl_detail::clamp01 (t) - 1.0f) * kHaasMaxMs; }
    static float driveDbFor    (float t) noexcept { t = utl_detail::clamp01 (t); return t <= 0.0f ? 0.0f : kDriveMinDb + (kDriveMaxDb - kDriveMinDb) * t; }   // 0 = bypass; the first increment is already +12 dB

    // ── lifecycle ───────────────────────────────────────────────────────────
    void prepare (double sampleRate, int instanceIndex = 0)
    {
        fs_ = (float) std::max (8000.0, sampleRate);
        (void) instanceIndex;
        const int maxD = (int) std::ceil (kHaasMaxMs * 0.001f * fs_) + 8;
        haas_[0].prepare (maxD); haas_[1].prepare (maxD);
        reset();
        setParams (p_);
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            hpF_[c].reset(); lpF_[c].reset(); bassF_[c].reset(); airF_[c].reset();
            imgX_[c].reset(); haas_[c].reset();
        }
        msX_.reset();
        hpXf_.reset(); lpXf_.reset(); bassXf_.reset(); airXf_.reset(); mbXf_.reset(); haasXf_.reset(); driveXf_.reset();
        for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) { inSm_[i][j] = inT_[i][j]; swSm_[i][j] = swT_[i][j]; }
        gainSm_ = -1.0f; mixSm_ = -1.0f; steerLSm_ = 1.0f; steerRSm_ = 1.0f;
        mGainSm_ = 1.0f; sGainSm_ = 1.0f; driveSm_ = 1.0f; haasSm_ = 0.0f;
        bassDbSm_ = bassDbT_; airDbSm_ = airDbT_; shelfCtr_ = 0; designShelves();
        corr_ = 0.0f; corrLL_ = corrRR_ = corrLR_ = 1e-9f; mMid_ = mSide_ = 0.0f; pkL_ = pkR_ = 0.0f;
    }

    // ── per-block parameter intake (NEVER per sample) ───────────────────────
    void setParams (const Params& p) noexcept
    {
        p_ = p;
        type_ = p.type < 0 ? 0 : (p.type >= kNumTypes ? 0 : p.type);   // the LIVE count, not the frozen width (fb373)

        // ── the INPUT matrix: polarity per channel and the Mono sum, as one smoothed 2x2.
        {
            const float l = p.flipL ? -1.0f : 1.0f, r = p.flipR ? -1.0f : 1.0f;
            if (p.sum) { inT_[0][0] = 0.5f * l; inT_[0][1] = 0.5f * r; inT_[1][0] = 0.5f * l; inT_[1][1] = 0.5f * r; }
            else       { inT_[0][0] = l; inT_[0][1] = 0.0f; inT_[1][0] = 0.0f; inT_[1][1] = r; }
        }
        // ── the OUTPUT swap, its own smoothed 2x2 (identity <-> swap).
        if (p.swap) { swT_[0][0] = 0.0f; swT_[0][1] = 1.0f; swT_[1][0] = 1.0f; swT_[1][1] = 0.0f; }
        else        { swT_[0][0] = 1.0f; swT_[0][1] = 0.0f; swT_[1][0] = 0.0f; swT_[1][1] = 1.0f; }

        // ── the fader (unity snapped, zero a glided -inf). Dim rides it.
        gainT_ = faderGain (p.gain);
        if (p.dim) gainT_ *= std::pow (10.0f, kDimDb / 20.0f);

        // ── the filters. OFF is a detent the section crossfades through; the state stays warm.
        const float hpHz = hpHzFor (p.hp), lpHz = lpHzFor (p.lp);
        hpXf_.target = hpHz > 0.0f ? 1.0f : 0.0f;
        lpXf_.target = lpHz > 0.0f ? 1.0f : 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            hpF_[c].set (hpHz > 0.0f ? hpHz : kHpMinHz, fs_);
            lpF_[c].set (lpHz > 0.0f ? lpHz : kLpMaxHz, fs_);
            imgX_[c].setup (kHingeHz, 0.5f, fs_);              // Canopy / Cellar hinge: 2 poles at 350 Hz
        }

        // ── the shelves: the TARGET dB is set here; the dB GLIDES on the engine's own 64-sample cadence
        //    inside processStereo and the biquads are redesigned there (a knob set once still ARRIVES; a
        //    knob swept is small coefficient steps, never a jump). The section crossfades in from 0 dB
        //    and is skipped at 0 (bit-exact bypass).
        bassDbT_ = shelfDbFor (p.bass); airDbT_ = shelfDbFor (p.air);
        bassXf_.target = std::fabs (bassDbT_) > 0.01f ? 1.0f : 0.0f;
        airXf_.target  = std::fabs (airDbT_)  > 0.01f ? 1.0f : 0.0f;
        if (shelfCtr_ < 0) { bassDbSm_ = bassDbT_; airDbSm_ = airDbT_; shelfCtr_ = 0; designShelves(); }   // first block: no ramp

        // ── Mono Below: the side HPF (2 poles), OFF at the detent.
        const float mbHz = monoBelowHzFor (p.monoBelow);
        msX_.setup (mbHz > 0.0f ? mbHz : kMonoBelowMin, 0.5f, fs_);
        mbXf_.target = mbHz > 0.0f ? 1.0f : 0.0f;

        // ── Rotate: a bounded M/S rotation, bipolar. Exact centre = identity (skipped).
        const float rd = rotateDegFor (p.rotate) * 0.017453293f;
        rotCos_ = std::cos (rd); rotSin_ = std::sin (rd);
        rotNeutral_ = std::fabs (rd) < 1e-9f;

        // ── Haas: the delay target in SAMPLES, signed (negative = the right channel is delayed).
        const float ms = haasMsFor (p.haas);
        haasT_ = ms * 0.001f * fs_;
        haasXf_.target = std::fabs (ms) > 1e-6f ? 1.0f : 0.0f;

        // ── Drive: 0 = bypass (crossfaded), else gain into tanh with loudness makeup derived from
        //    the GLIDED drive per sample (two independent smoothers would swell mid-glide — fb445).
        const float dDb = driveDbFor (p.drive);
        drive_ = std::pow (10.0f, dDb / 20.0f);
        driveXf_.target = dDb > 0.0f ? 1.0f : 0.0f;

        // ── the imaging (unchanged law). w: 0 = mono · 1 = neutral · 3 = 300 %, neutral at the CENTRE.
        const float t = utl_detail::clamp01 (p.image);
        imgW_ = (t <= 0.5f) ? (2.0f * t) : (1.0f + (kImageMax - 1.0f) * 2.0f * (t - 0.5f));
        imageNeutral_ = std::fabs (imgW_ - 1.0f) < 1e-6f;
        sGainT_ = imgW_;
        mGainT_ = imgW_ <= 1.0f ? 1.0f : std::max (kMidFloor, 1.0f / std::sqrt (imgW_));
        const float th = (imgW_ <= 1.0f ? -(1.0f - imgW_) : (imgW_ - 1.0f) / (kImageMax - 1.0f)) * kRotateMaxDeg * 0.017453293f;
        turnCos_ = std::cos (th); turnSin_ = std::sin (th);

        // ── balance. Constant power, unity at the centre, either end mutes a channel outright.
        const float b = utl_detail::clamp01 (p.steer);
        if (std::fabs (b - 0.5f) < 1.0e-6f) { steerLT_ = steerRT_ = 1.0f; }
        else
        {
            steerLT_ = std::cos (0.7853982f * (1.0f + (2.0f * b - 1.0f))) * 1.41421356f;
            steerRT_ = std::sin (0.7853982f * (1.0f + (2.0f * b - 1.0f))) * 1.41421356f;
        }

        mixT_ = utl_detail::clamp01 (p.mix);
        if (mixSm_  < 0.0f) mixSm_  = mixT_;      // first block: no ramp
        if (gainSm_ < 0.0f) gainSm_ = gainT_;
    }

    void designShelves() noexcept
    {
        for (int c = 0; c < 2; ++c) { bassF_[c].design (false, bassDbSm_, kBassHz, fs_); airF_[c].design (true, airDbSm_, kAirHz, fs_); }
    }

    // ── the audio ───────────────────────────────────────────────────────────
    inline void processStereo (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float dryL = inL, dryR = inR;
        // the shelves' dB glide + redesign, every 64 samples (≈ 1.3 ms): a 12 dB throw arrives in ~20 ms
        if (++shelfCtr_ >= 64)
        {
            shelfCtr_ = 0;
            const float nb = utl_detail::glide (bassDbSm_, bassDbT_, 0.12f), na = utl_detail::glide (airDbSm_, airDbT_, 0.12f);
            if (nb != bassDbSm_ || na != airDbSm_) { bassDbSm_ = nb; airDbSm_ = na; designShelves(); }
        }
        const float aG = 1.0f - std::exp (-1.0f / (0.015f * fs_));
        const float aM = 1.0f - std::exp (-1.0f / (0.025f * fs_));

        gainSm_ = utl_detail::glide (gainSm_, gainT_, aG);
        if (gainT_ == 0.0f && gainSm_ < 1.0e-6f) gainSm_ = 0.0f;   // "0 is silence" is a fact
        mixSm_    = utl_detail::glide (mixSm_,    mixT_,    aM);
        steerLSm_ = utl_detail::glide (steerLSm_, steerLT_, aM);
        steerRSm_ = utl_detail::glide (steerRSm_, steerRT_, aM);
        mGainSm_  = utl_detail::glide (mGainSm_,  mGainT_,  aM);
        sGainSm_  = utl_detail::glide (sGainSm_,  sGainT_,  aM);
        driveSm_  = utl_detail::glide (driveSm_,  drive_,   aM);
        haasSm_   = utl_detail::glide (haasSm_,   haasT_,   aM);
        const float hpAmt = hpXf_.step (aM), lpAmt = lpXf_.step (aM);
        const float bAmt  = bassXf_.step (aM), airAmt = airXf_.step (aM);
        const float mbAmt = mbXf_.step (aM), hAmt = haasXf_.step (aM), dAmt = driveXf_.step (aM);

        float x[2] = { inL, inR };

        // ═══ 1. THE INPUT MATRIX — polarity and the Mono sum, smoothed (a polarity flip is a full-scale
        //        step if you just branch on it; smoothing the four numbers smooths every transition).
        for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) inSm_[i][j] = utl_detail::glide (inSm_[i][j], inT_[i][j], aM);
        {
            const float l0 = x[0], r0 = x[1];
            x[0] = inSm_[0][0] * l0 + inSm_[0][1] * r0;
            x[1] = inSm_[1][0] * l0 + inSm_[1][1] * r0;
        }

        // ═══ 2. HAAS — one channel through a glided fractional delay, the other bit-exact. Before the
        //        Mono sum ON PURPOSE: press Mono and hear what a Haas does to mono compatibility.
        //        (The sum above is the input matrix — so strictly the order is Flip → Mono → Haas when
        //        Mono is down; with Mono up the sum has already happened and the delay offsets the two
        //        identical copies, which IS the Haas-then-mono comb. Either way the lesson plays.)
        if (hAmt > 0.0f)
        {
            const float d = std::fabs (haasSm_);
            if (haasSm_ < 0.0f) { const float y = haas_[1].tick (x[1], d); x[1] += hAmt * (y - x[1]); haas_[0].tick (x[0], 0.0f); }
            else                { const float y = haas_[0].tick (x[0], d); x[0] += hAmt * (y - x[0]); haas_[1].tick (x[1], 0.0f); }
        }
        else { haas_[0].tick (x[0], 0.0f); haas_[1].tick (x[1], 0.0f); }   // keep the lines warm (a cold line is a click when it joins)

        // ═══ 3. THE FADER (and Dim).
        x[0] *= gainSm_; x[1] *= gainSm_;

        // ═══ 4. HIGH PASS · 5. LOW PASS — 2-pole, crossfaded in from OFF.
        if (hpAmt > 0.0f) for (int c = 0; c < 2; ++c) { float lo, hi; hpF_[c].tick (x[c], lo, hi); x[c] += hpAmt * (hi - x[c]); }
        if (lpAmt > 0.0f) for (int c = 0; c < 2; ++c) { float lo, hi; lpF_[c].tick (x[c], lo, hi); x[c] += lpAmt * (lo - x[c]); }

        // ═══ 6. BASS · 7. AIR — the shelves, skipped at 0 dB (bit-exact).
        if (bAmt   > 0.0f) for (int c = 0; c < 2; ++c) { const float y = bassF_[c].tick (x[c]); x[c] += bAmt   * (y - x[c]); }
        if (airAmt > 0.0f) for (int c = 0; c < 2; ++c) { const float y = airF_[c].tick (x[c]);  x[c] += airAmt * (y - x[c]); }

        // ═══ 8. DRIVE — tanh with LOUDNESS MAKEUP. mk holds the bus sine's peak through the curve, so the
        //        top of the knob is a square wave at the SAME peak (+3 dB RMS), never 21 dB down.
        if (dAmt > 0.0f)
        {
            const float drv = std::max (1.0f, driveSm_);
            const float mk  = kPk / std::max (1e-6f, utl_detail::fastTanh (drv * kPk));
            for (int c = 0; c < 2; ++c) { const float y = mk * utl_detail::fastTanh (drv * x[c]); x[c] += dAmt * (y - x[c]); }
        }

        // ═══ 9. GEOMETRY — Mono Below, Width (per Type), Rotate. Skipped entirely when neutral.
        if (! (imageNeutral_ && rotNeutral_ && mbAmt <= 0.0f))
        {
            float m = 0.5f * (x[0] + x[1]);
            float s = 0.5f * (x[0] - x[1]);
            if (mbAmt > 0.0f) s += mbAmt * (msX_.hp (s) - s);          // below the corner the side dies = mono; the mid is untouched
            switch (type_)
            {
                case 1: { const float mm = m * turnCos_ - s * turnSin_; const float ss = m * turnSin_ + s * turnCos_; m = mm; s = ss; } break;   // Turn
                case 2:  s *= sGainSm_; break;                                                                                             // Outer
                case 3: { const float mHi = imgX_[0].hp (m), sHi = imgX_[1].hp (s); m = (m - mHi) + mHi * mGainSm_; s = (s - sHi) + sHi * sGainSm_; } break;   // Canopy: above 350
                case 4: { const float mHi = imgX_[0].hp (m), sHi = imgX_[1].hp (s); m = mHi + (m - mHi) * mGainSm_; s = sHi + (s - sHi) * sGainSm_; } break;   // Cellar: below 350
                default: m *= mGainSm_; s *= sGainSm_; break;                                                                              // Strip
            }
            if (! rotNeutral_) { const float mm = m * rotCos_ - s * rotSin_; const float ss = m * rotSin_ + s * rotCos_; m = mm; s = ss; }
            x[0] = m + s; x[1] = m - s;
            mMid_ = std::fabs (m); mSide_ = std::fabs (s);
        }
        else { mMid_ = std::fabs (0.5f * (x[0] + x[1])); mSide_ = std::fabs (0.5f * (x[0] - x[1])); }

        // ═══ 10. PAN.
        x[0] *= steerLSm_; x[1] *= steerRSm_;

        // ═══ 11. SWAP — at the output, smoothed.
        for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) swSm_[i][j] = utl_detail::glide (swSm_[i][j], swT_[i][j], aM);
        {
            const float l0 = x[0], r0 = x[1];
            x[0] = swSm_[0][0] * l0 + swSm_[0][1] * r0;
            x[1] = swSm_[1][0] * l0 + swSm_[1][1] * r0;
        }

        // ═══ 12. MIX — equal power, BOTH ENDPOINTS EXACT (cos(pi/2) is not zero in float).
        float wg, dg;
        if      (mixSm_ >= 1.0f) { wg = 1.0f; dg = 0.0f; }
        else if (mixSm_ <= 0.0f) { wg = 0.0f; dg = 1.0f; }
        else { wg = std::sin (1.5707963f * mixSm_); dg = std::cos (1.5707963f * mixSm_); }
        outL = dg * dryL + wg * x[0];
        outR = dg * dryR + wg * x[1];

        // meters: per-rail peaks (the card's rails), the correlation
        pkL_ = std::max (std::fabs (outL), pkL_ * 0.9993f);
        pkR_ = std::max (std::fabs (outR), pkR_ * 0.9993f);
        const float lr = 0.9995f;
        corrLL_ = lr * corrLL_ + outL * outL;
        corrRR_ = lr * corrRR_ + outR * outR;
        corrLR_ = lr * corrLR_ + outL * outR;
        corr_   = corrLR_ / std::sqrt (std::max (1e-18f, corrLL_ * corrRR_));
    }

    // ── meters (the engine's OWN numbers, fb432) ─────────────────────────────
    float meterGainDb()  const noexcept { return gainSm_ <= 0.0f ? -200.0f : 20.0f * std::log10 (gainSm_); }
    float meterCorr()    const noexcept { return corr_; }
    float meterMid()     const noexcept { return mMid_;  }
    float meterSide()    const noexcept { return mSide_; }
    int   meterType()    const noexcept { return type_;  }
    float meterImageW()  const noexcept { return imgW_;  }
    float meterMidGain() const noexcept { return mGainT_; }
    float meterPeakL()   const noexcept { return pkL_;   }
    float meterPeakR()   const noexcept { return pkR_;   }
    float meterHaasSamples() const noexcept { return haasSm_; }
    float meterDrive()   const noexcept { return driveSm_; }

    // The fader law (unchanged): -60..+30 dB, unity SNAPPED at 2/3, the bottom 4 % slides to a true zero.
    static float faderGain (float t) noexcept
    {
        t = utl_detail::clamp01 (t);
        if (t <= 0.0f) return 0.0f;
        if (std::fabs (t - kGainUnityT) < 1.0e-4f) return 1.0f;
        const float db = kGainMinDb + (kGainMaxDb - kGainMinDb) * t;
        float g = std::pow (10.0f, db * 0.05f);
        if (t < kGainFoot) { const float u = t / kGainFoot; g *= u * u; }
        return g;
    }

    Params p_ {};
    float  fs_ = 48000.0f;
    int    type_ = 0;

    utl_detail::Svf2  hpF_[2], lpF_[2];
    utl_detail::Shelf bassF_[2], airF_[2];
    utl_detail::XOver imgX_[2], msX_;
    utl_detail::Haas  haas_[2];
    utl_detail::Xfade hpXf_, lpXf_, bassXf_, airXf_, mbXf_, haasXf_, driveXf_;

    float inT_[2][2]  { { 1.0f, 0.0f }, { 0.0f, 1.0f } }, inSm_[2][2] { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    float swT_[2][2]  { { 1.0f, 0.0f }, { 0.0f, 1.0f } }, swSm_[2][2] { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    float gainT_ = 1.0f, gainSm_ = -1.0f;
    float mixT_ = 1.0f, mixSm_ = -1.0f;
    float steerLT_ = 1.0f, steerRT_ = 1.0f, steerLSm_ = 1.0f, steerRSm_ = 1.0f;
    float bassDbT_ = 0.0f, airDbT_ = 0.0f, bassDbSm_ = 0.0f, airDbSm_ = 0.0f; int shelfCtr_ = -1;
    float drive_ = 1.0f, driveSm_ = 1.0f;
    float haasT_ = 0.0f, haasSm_ = 0.0f;
    float imgW_ = 1.0f, mGainT_ = 1.0f, sGainT_ = 1.0f, mGainSm_ = 1.0f, sGainSm_ = 1.0f;
    float turnCos_ = 1.0f, turnSin_ = 0.0f, rotCos_ = 1.0f, rotSin_ = 0.0f;
    bool  imageNeutral_ = true, rotNeutral_ = true;

    float corr_ = 0.0f, corrLL_ = 1e-9f, corrRR_ = 1e-9f, corrLR_ = 1e-9f;
    float mMid_ = 0.0f, mSide_ = 0.0f, pkL_ = 0.0f, pkR_ = 0.0f;
};

} // namespace tw
