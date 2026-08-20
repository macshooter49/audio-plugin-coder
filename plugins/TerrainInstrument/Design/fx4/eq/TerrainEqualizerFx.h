#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  TerrainEqualizerFx.h — fb420. ONE instance of the FX-rack EQUALIZER (chain kind 9).
//
//  Header-only, pure C++ (no JUCE), tw:: namespace, the locked Design/fx4/CONTRACT.md §2
//  interface. Offline-certified by eq_cert.cpp, which lives next to this file.
//
//  ── WHAT THIS DEVICE IS ──────────────────────────────────────────────────────
//  FOUR fixed-role bands — LOW · BODY · BITE · AIR — and Q is NEVER a knob. Q is a LAW
//  owned by the Type. That is the whole chassis solve: a 4-band parametric wants 12 knobs
//  (4 x freq/gain/Q) and the fb275 back panel gives 8. Freq+gain per band = 8 = exactly
//  the grid; the Q law rides the Type; the ONE remaining degree of freedom per Type is the
//  P8 `Trait` slot, which relabels (Pinch / Slope / Taper / Dip / Silk / Pivot / Sting).
//
//  ── NOTHING IS BACK-ONLY ─────────────────────────────────────────────────────
//  Max's worry, verbatim: "I don't know what an EQ could possibly offer on the back...
//  it'll be annoying if we have a whole bunch of shit on the back panel that we can't see."
//  The answer is structural: the back 8 ARE the curve nodes. Dragging node k in X writes
//  b1/b3/b5/b7; dragging it in Y writes b2/b4/b6 (and the FRONT `Air` knob for node 3).
//  There is no parameter in this device that the front curve cannot reach, and no back
//  knob that does not move when you drag. The back panel is the numeric face of the curve.
//
//  ── THE CEILING (CONTRACT R11) ───────────────────────────────────────────────
//  +-30 dB per band, x Amount 200 % = +-60 dB of curve. Surgical `Pinch` takes Q to 40,
//  Chisel `Sting` to 64 and morphs deep cuts into TRUE notches (< -60 dB). At 100 % this is
//  not a mixing tool: one band can delete a region of the spectrum or lift it by a factor
//  of 1000. The harness gates this (eq_cert §K) on max spectral deviation, measured on the
//  OUTPUT SPECTRUM of pink noise, not on coefficient geometry.
//
//  ── THE v6 ENGINE IS NOT REUSED, AND THIS IS WHY (bible §0.1) ────────────────
//  Source/ParametricEQ.h calls updateAllCoefficients() ONCE PER SAMPLE from
//  processSample (:104), and every call does
//      *bandFilters[b].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(..)
//  for 7 bands + up to 8 HP/LP stages. makePeakFilter HEAP-ALLOCATES a ReferenceCounted
//  object every call: 9-15 audio-thread allocations per sample per channel, plus the full
//  sin/cos/pow design math at audio rate. That is a malloc on the audio thread at 48 kHz.
//  Here: design at CONTROL rate (every 32 samples, dirty-flagged), glide the 5 coefficients
//  per sample. Zero allocation anywhere reachable from processStereo.
//
//  🚨 ── THE NaN TRAP, CARRIED FORWARD VERBATIM (ParametricEQ.h:42-47) ─────────
//  "initialize smoothers to safe defaults BEFORE the first call to updateAllCoefficients().
//   Default current value is 0, which makes Q=0, which makes alpha = sin/(2*Q) = NaN inside
//   makePeakFilter. The biquad coefficients then become NaN and filter state is poisoned
//   for the lifetime of the instance."
//  Every smoother in this file is seeded with a setCurrentAndTargetValue equivalent
//  (seedSmoothers(), called from prepare() BEFORE the first design and again on the first
//  processStereo via seeded_) and every Q is clamped to >= kQMin. eq_cert §A gates it by
//  driving a block through a freshly prepared engine with NO setParams call at all.
//
//  ── HOUSE LAWS HONOURED STRUCTURALLY ─────────────────────────────────────────
//  * ZERO LATENCY, ZERO LOOKAHEAD. Minimum-phase IIR, no oversampling, no FIR. The fb305
//    main-send exclusion subtracts the routed dry SAMPLE-ALIGNED; a device that delays its
//    wet path makes the subtracted dry leak back phase-smeared. Linear phase is therefore
//    permanently rejected for this device, not deferred.
//  * NO OVERSAMPLING, EVER. An EQ is LTI - it cannot alias. Ableton's 2x "Hi-Quality"
//    exists only to reduce cramping; the matched design below achieves that for ~0 runtime
//    cost. The Dynamic type's gain ride is control-rate (<= 1.5 kHz update, envelope
//    bandwidth <= 60 Hz) so its sidebands are decades below anything oversampling helps.
//  * NULL IS BIT-EXACT. All gains 0 => every stage takes the exact identity coefficient set
//    (b = {1,0,0}, a = {0,0}) and the slant takes b1 == a1, so the output is the input, bit
//    for bit, with states pinned at 0. Amount 0 % nulls at ANY knob state. This is what
//    makes the device safe to leave in a chain (fb303 default-sound guarantee).
//  * MIX 1.0 = FULLY WET, ZERO DRY, and the mix law is LINEAR, not equal-power: dry and wet
//    here are 100 % correlated (same signal, minimum-phase filtered), so a sin/cos law
//    would bump +3 dB at 50 %.
//  * NO CLICKS. Every continuous param one-poles at control rate; the COEFFICIENTS
//    themselves glide per sample; Type / Character / Focus switches fade-swap-recover
//    (8 ms dip -> snap -> 30 ms recover), never ramp coefficients across an octave.
//  * NOTHING FREE-RUNS. There are NO feedback loops in this device, so there is no loop
//    gain to budget - stated, not hand-waved. Resonances are passive biquad decays: zero
//    input => exponential decay, never sustain. Dynamic envelopes idle at silence.
//  * DENORMALS. Recirculating states are flushed at every design boundary; assume
//    ScopedNoDenormals is NOT set. Chisel Q 64 rings into denormal range for ~200 ms after
//    every note without this.
// ═══════════════════════════════════════════════════════════════════════════════

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace tw {

class TerrainEqualizerFx
{
public:
    // ── identity ─────────────────────────────────────────────────────────────
    static constexpr int kNumTypes  = 7;
    static constexpr int kNumChars  = 8;
    // ⚠️ fb342 birth-cardinality law: an AudioParameterChoice's option count is fixed at
    // construction and state-format-breaking to grow later. DECLARE THE APVTS CHOICE AT
    // kNumTypeSlots (7 live + 5 reserved, greyed in the UI, clamped by setParams).
    static constexpr int kNumTypeSlots = 12;
    static constexpr int kNumFocus  = 5;
    static constexpr int kNumDropdowns = 2;          // the two back-panel dropdown HEADINGS
    static constexpr int kNumBands  = 4;
    // fb438 — THE FREE BELLS. Up to four extra bands the user ADDS on the card (double-click the curve)
    // and removes (right-click). Nodes only — the back-8 stay the four ROLE bands. Full-range,
    // constant-Q bells, surgical by design: the Type's character lives in Low/Body/Bite/Air, a free
    // band is a scalpel. OFF = stage off = bit-exact pass-through, so every unity gate stands.
    static constexpr int kNumFree   = 4;
    static constexpr int kNumNodes  = kNumBands + kNumFree;        // what the card draws
    static constexpr int kCurveBins = 96;

    // ═════════════════════════════════════════════════════════════════════════
    //  🔴 MUTATION HOOKS (FIXES.md §0). Compiling with one of these -D flags DELETES a
    //  mechanism a gate claims to protect. The cert then has to go RED. A gate that stays
    //  green under its own mutation is a BLOCKER, not a footnote — see MUTATION.md.
    //  These are compile-time only: with no -D, not one branch of this file changes.
    // ═════════════════════════════════════════════════════════════════════════
    static const char* mutationTag() noexcept
    {
#if   defined (EQ_MUT_NO_PIVOT)
        return "NO_PIVOT   — designSlant() reverts to the fb420 sliding-pivot shelf";
#elif defined (EQ_MUT_NO_RINGCAP)
        return "NO_RINGCAP — limitRing() body deleted, one-pole G clamp removed";
#elif defined (EQ_MUT_NO_SMOOTH)
        return "NO_SMOOTH  — all 11 smoothers tau->0, coefficient glide + Mix smoother deleted";
#elif defined (EQ_MUT_NO_DIP)
        return "NO_DIP     — the Type/Character/Focus fade-swap dip deleted";
#elif defined (EQ_MUT_NO_CEILING)
        return "NO_CEILING — ranges cut to a polite console: +-10 dB, +-8 Slant, Amount 100 %";
#elif defined (EQ_MUT_NO_DENORM)
        return "NO_DENORM  — the 1e-18 true-zero state flush deleted";
// ── fb425: three CELL-SCOPED mutants. Each breaks ONE cell of the 7x8 matrix and leaves
//    the other 55 perfect — which is precisely what a gate that samples the default cell
//    cannot see, and precisely what §Q must catch.
#elif defined (EQ_MUT_DEAD_CELL)
        return "DEAD_CELL  — `Trait` ignored on Open x `Soft Knee` ONLY (1 of 672 knob-cells)";
#elif defined (EQ_MUT_POLITE_CELL)
        return "POLITE_CELL— band gains clamped to +-10 dB on Passive x `Deep Atten` ONLY";
#elif defined (EQ_MUT_MIX_WET)
        return "MIX_WET    — mixTg_ forced to 1.0: the Mix knob is ignored, in every cell";
#elif defined (EQ_MUT_FLAT_FOCUS)
        return "FLAT_FOCUS — Focus option `Side` silently plays `Stereo` (a dead dropdown option)";
#else
        return nullptr;                                  // the real engine
#endif
    }

    static const char* const* typeNames() noexcept
    {
        static const char* const n[kNumTypes] =
        { "Surgical", "British", "American", "Passive", "Open", "Dynamic", "Chisel" };
        return n;
    }

    // 🔑 fb425 — THE TWO WORDS THE CARD PRINTS ABOVE THE DROPDOWNS.
    //  This device was the ONLY one of the four with no `dropdownNames()`: `kNumLabels`
    //  enumerated the five Focus OPTIONS but never the two HEADINGS the back panel prints
    //  above them, so `Character` and `Focus` sat outside the no-doubles gate, outside the
    //  worklet equality gate and outside the ROSTER coverage gate — three gates blind to
    //  two of the most visible strings on the card. Both are already ruled (`Focus` in
    //  CONTRACT §4 and in the fb423 SANCTIONED block; `Character` is chassis vocabulary
    //  shared by all four fx4 devices), so publishing them closes a coverage hole rather
    //  than opening a naming question. Siblings: TerrainCompressFx.h:113 {Character,Detect},
    //  TerrainOttFx.h:114 {Character,Stereo}.
    static const char* const* dropdownNames() noexcept
    {
        static const char* const n[kNumDropdowns] = { "Character", "Focus" };
        return n;
    }

    // BACK DROPDOWN 2 (Params::axis). NEVER `Type` - Type is the header pill (R6).
    // Focus changes WHICH SIGNAL the filters see, which is physics, not tone.
    static const char* const* focusNames() noexcept
    {
        static const char* const n[kNumFocus] = { "Stereo", "Mid", "Side", "Left", "Right" };
        return n;
    }

    // The P8 relabel. Every one of these changes CURVE MATH, not cosmetics.
    static const char* shapeName (int type) noexcept
    {
        static const char* const s[kNumTypes] =
        { "Pinch", "Slope", "Taper", "Dip", "Silk", "Pivot", "Sting" };
        return s[clampi (type, 0, kNumTypes - 1)];
    }

    // The back 8, in APVTS order. Column-major: each band's freq and gain are adjacent,
    // which is what makes "drag a node -> two knobs move" legible.
    static const char* const* backNames() noexcept
    {
        static const char* const n[8] =
        { "Low Hz", "Low", "Body Hz", "Body", "Bite Hz", "Bite", "Reach", "Trait" };
        return n;
    }
    static const char* const* frontNames() noexcept
    {
        static const char* const n[4] = { "Slant", "Air", "Amount", "Mix" };
        return n;
    }

    // 🔑 fb422 — THE SINGLE SOURCE OF TRUTH, MADE STRUCTURAL.
    //  This used to be a SECOND hand-typed table of the same 56 strings that `charSpec`
    //  already carries, and it had ALREADY DRIFTED: `charNames()[1][3]` said "Fixed Top"
    //  while `charSpec(1,3).nm` said "Iron Top" — the card would have printed one word and
    //  the DSP row that actually re-wires the filter carried another. That is the fb373
    //  geometry exactly (`Cassette` playing `Studio`), one file earlier in the pipeline.
    //  The table is GONE. There is now exactly one place a Character's name can live: the
    //  `nm` field of the CharSpec row that defines its physics. They cannot disagree.
    static const char* const* charNames (int type) noexcept
    {
        static const char* tbl[kNumTypes][kNumChars] = {};
        static const bool init = []
        {
            for (int t = 0; t < kNumTypes; ++t)
                for (int c = 0; c < kNumChars; ++c) tbl[t][c] = charSpec (t, c).nm;
            return true;
        }();
        (void) init;
        return tbl[clampi (type, 0, kNumTypes - 1)];
    }

    // ── EVERY user-visible string this device publishes, in ONE enumerable list. ──
    //  The card, the roster, the worklet and the NO-DOUBLES gate all read THIS. A label
    //  that is not in here is a label the doubles gate cannot see, which is how `Tilt`
    //  and `Sculpt` — both shipped TAPE FRONT KNOBS — survived a whole build round.
    //  Order: 7 Types · 4 front · 8 back · 7 `Trait` relabels · 2 dropdown headings ·
    //         5 Focus · 56 Characters.
    static constexpr int kNumLabels = kNumTypes + 4 + 8 + kNumTypes + kNumDropdowns + kNumFocus
                                    + kNumTypes * kNumChars;                 // = 89
    static_assert (kNumLabels == 89, "fb425: the two dropdown HEADINGS are published too");
    static const char* label (int i) noexcept
    {
        int k = clampi (i, 0, kNumLabels - 1);
        if (k < kNumTypes)              return typeNames()[k];               k -= kNumTypes;
        if (k < 4)                      return frontNames()[k];              k -= 4;
        if (k < 8)                      return backNames()[k];               k -= 8;
        if (k < kNumTypes)              return shapeName (k);                k -= kNumTypes;
        if (k < kNumDropdowns)          return dropdownNames()[k];           k -= kNumDropdowns;
        if (k < kNumFocus)              return focusNames()[k];              k -= kNumFocus;
        return charNames (k / kNumChars)[k % kNumChars];
    }
    // what SLOT a label occupies, for the doubles gate's precedence rule
    // (RENAMES.md: header pill > knob label > dropdown option > Character).
    static const char* labelSlot (int i) noexcept
    {
        int k = clampi (i, 0, kNumLabels - 1);
        if (k < kNumTypes)              return "Type pill";                  k -= kNumTypes;
        if (k < 4)                      return "front knob";                 k -= 4;
        if (k < 8)                      return "back knob";                  k -= 8;
        if (k < kNumTypes)              return "Trait relabel";              k -= kNumTypes;
        if (k < kNumDropdowns)          return "dropdown heading";           k -= kNumDropdowns;
        if (k < kNumFocus)              return "Focus option";
        return "Character";
    }

    static_assert (kNumTypes > 0 && kNumChars == 8, "roster/table must move together");

    // ── the locked Params block (CONTRACT §2) ────────────────────────────────
    //  FRONT 3 + Mix:  f1 = Slant · f2 = Air · f3 = Amount · mix = Mix
    //  BACK 8 (column-major, band pairs adjacent):
    //      b1 Low Hz · b2 Low · b3 Body Hz · b4 Body · b5 Bite Hz · b6 Bite
    //      b7 Reach  · b8 Trait
    //  EVERY default is 0.5 and 0.5 is the NEUTRAL point of every one of them, so the
    //  device boots provably flat and bit-exact. (mix defaults 1.0 by contract law.)
    struct Params
    {
        int   type = 0, character = 0;
        int   axis = 0;                                   // Focus: 0 Stereo 1 Mid 2 Side 3 L 4 R
        float f1 = 0.5f, f2 = 0.5f, f3 = 0.5f;            // Slant · Air · Amount
        float mix = 1.0f;
        float b1=0.5f,b2=0.5f,b3=0.5f,b4=0.5f,b5=0.5f,b6=0.5f,b7=0.5f,b8=0.5f;
        // fb438 — the free bells: (freq, gain) x 4 as 0..1 (freq 20 Hz * 1000^t, gain +-kBandDbSpan), and
        // an ON per band. Defaults: centre, 0 dB, OFF — a default device is the four role bands only.
        float x1=0.5f,x2=0.5f,x3=0.5f,x4=0.5f,x5=0.5f,x6=0.5f,x7=0.5f,x8=0.5f;
        bool  xOn1=false, xOn2=false, xOn3=false, xOn4=false;
        // An EQ has NO tempo-relevant time constant. The 4-bar..1/256 rule is satisfied by
        // ABSENCE, not by skipping it: the Dynamic type's ballistics are program-relative
        // milliseconds derived from each band's own centre frequency, which is a property of
        // the band, not of the song. These two fields are accepted and ignored.
        bool  tempoSync = false; double bpm = 120.0;
    };

    // ── the 60 Hz push (CONTRACT §2) ─────────────────────────────────────────
    //  curve[96]  magnitude of the WHOLE cascade in dB, on 96 LOG bins:
    //                 f(i) = 20 * 10^(3*i/95) Hz,  i = 0..95  =>  f(0) = 20 Hz exactly,
    //                 f(95) = 20000 Hz exactly, 31.667 bins per decade.
    //             It is evaluated from the LIVE, ALREADY-RAMPED coefficients (not from the
    //             knob values), so the drawn curve can never disagree with the audio - which
    //             is the failure mode every "response display" plugin ships with.
    //  nodeHz[4]  LOW · BODY · BITE · AIR centre/corner in Hz. AIR reports its TRUE corner,
    //             which reaches 40 kHz: the card clamps the dot to the right edge, it does
    //             not clamp the number.
    //  nodeDb[4]  the APPLIED gain of each band in dB - post Amount, post the Type's gain
    //             law, post the Dynamic ride. Under Dynamic the dots move with the audio,
    //             which is the Pro-Q/TDR grammar.
    //  lvl        output level 0..1 for the dead-feed fade (idle = dim).
    struct Viz
    {
        float curve[kCurveBins] {};
        float nodeHz[kNumNodes] { 100.0f, 550.0f, 3100.0f, 15500.0f, 632.0f, 632.0f, 632.0f, 632.0f };
        float nodeDb[kNumNodes] {};
        bool  nodeOn[kNumNodes] { true, true, true, true, false, false, false, false };   // fb438 — the free bells
        float lvl = 0.0f;
    };

    static float curveBinHz (int i) noexcept
    { return 20.0f * std::pow (10.0f, 3.0f * (float) clampi (i, 0, kCurveBins - 1) / (float) (kCurveBins - 1)); }

    // ═════════════════════════════════════════════════════════════════════════
    //  DESIGN MATH — public + static so the harness can gate it directly.
    // ═════════════════════════════════════════════════════════════════════════
    struct Coeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };

    // kind: 0 bell · 1 low shelf (2-pole) · 2 high shelf (2-pole)
    //       3 low shelf (1-pole) · 4 high shelf (1-pole)
    // The ANALOG PROTOTYPE magnitude SQUARED. This is the thing every digital design in
    // this file is trying to be, and it is what eq_cert measures the error against.
    static double protoMag2 (int kind, double f, double f0, double Q, double gDb) noexcept
    {
        if (gDb > -1e-12 && gDb < 1e-12) return 1.0;
        if (Q < 1e-4) Q = 1e-4;
        const double A = std::pow (10.0, gDb / 40.0);
        const double W = f / (f0 > 1e-6 ? f0 : 1e-6);
        const double W2 = W * W;
        switch (kind)
        {
            case 0: {                                   // peaking, constant-Q (mirror-exact)
                const double t = (1.0 - W2) * (1.0 - W2);
                const double n = t + (A * W / Q) * (A * W / Q);
                const double d = t + (W / (A * Q)) * (W / (A * Q));
                return n / (d > 1e-300 ? d : 1e-300);
            }
            case 1: {                                   // low shelf, 2-pole
                const double n = (A - W2) * (A - W2) + A * W2 / (Q * Q);
                const double d = (1.0 - A * W2) * (1.0 - A * W2) + A * W2 / (Q * Q);
                return A * A * n / (d > 1e-300 ? d : 1e-300);
            }
            case 2: {                                   // high shelf, 2-pole
                const double n = (1.0 - A * W2) * (1.0 - A * W2) + A * W2 / (Q * Q);
                const double d = (A - W2) * (A - W2) + A * W2 / (Q * Q);
                return A * A * n / (d > 1e-300 ? d : 1e-300);
            }
            case 3: {                                   // low shelf, 1-pole
                const double n = W2 + A * A, d = W2 + 1.0 / (A * A);
                return A * A * n / (d > 1e-300 ? d : 1e-300);
            }
            case 5: {
                // 🚨 fb422 — THE SLANT PROTOTYPE, CORRECTED. f0 here is the PIVOT, and the
                //  pivot is where the response is 0 dB. The old form
                //      (W2 g^2 + 1/g^2) / (W2 + 1)
                //  is a one-pole shelf whose CORNER is at f0, and such a shelf crosses
                //  unity at f0/g — so its pivot slid from 700 Hz down to 2.8 Hz as the knob
                //  opened, and 120 Hz travelled -4.75 dB then back UP to +8.55 dB. The
                //  fixed-pivot seesaw is
                //      |H|^2 = (1 + s^2 W^2) / (s^2 + W^2),   s = 10^(gDb/20), W = f/f0.
                //  At W = 1 it is exactly 1 for EVERY s. d|H|^2/ds has the sign of (W^4 - 1),
                //  so it is STRICTLY monotone in the gain at every frequency except the
                //  pivot itself — the property the old form did not have.
                const double s2 = std::pow (10.0, gDb / 10.0);
                return (1.0 + s2 * W2) / (s2 + W2);
            }
            default: {                                  // high shelf, 1-pole
                const double n = W2 + 1.0 / (A * A), d = W2 + A * A;
                return A * A * A * A * n / (d > 1e-300 ? d : 1e-300);
            }
        }
    }

    // ── RBJ cookbook (bilinear). EXACT at DC / f0 / Nyquist, but the bilinear map warps
    //    the frequency axis: above ~fs/8 the bell squashes asymmetrically and EVERY
    //    response is forced to a horizontal tangent at Nyquist. That forcing is the whole
    //    of "digital EQ harshness". Used ONLY below fs/24, where the warp is < 0.1 dB and
    //    the design is far better conditioned than the matched fit.
    static Coeffs designRbj (int kind, double f0, double Q, double gDb, double fs) noexcept
    {
        Coeffs c;
        if (gDb > -1e-12 && gDb < 1e-12) return c;
        if (Q < 1e-4) Q = 1e-4;
        const double A = std::pow (10.0, gDb / 40.0);
        const double w = 6.283185307179586 * clampd (f0, 1.0, 0.45 * fs) / fs;
        const double cw = std::cos (w), sw = std::sin (w);
        const double al = sw / (2.0 * Q);
        const double sA = std::sqrt (A);
        double b0, b1, b2, a0, a1, a2;
        if (kind == 0)
        {
            b0 = 1.0 + al * A; b1 = -2.0 * cw; b2 = 1.0 - al * A;
            a0 = 1.0 + al / A; a1 = -2.0 * cw; a2 = 1.0 - al / A;
        }
        else if (kind == 1)
        {
            b0 =      A * ((A + 1.0) - (A - 1.0) * cw + 2.0 * sA * al);
            b1 =  2.0*A * ((A - 1.0) - (A + 1.0) * cw);
            b2 =      A * ((A + 1.0) - (A - 1.0) * cw - 2.0 * sA * al);
            a0 =           (A + 1.0) + (A - 1.0) * cw + 2.0 * sA * al;
            a1 =    -2.0 * ((A - 1.0) + (A + 1.0) * cw);
            a2 =           (A + 1.0) + (A - 1.0) * cw - 2.0 * sA * al;
        }
        else
        {
            b0 =      A * ((A + 1.0) + (A - 1.0) * cw + 2.0 * sA * al);
            b1 = -2.0*A * ((A - 1.0) + (A + 1.0) * cw);
            b2 =      A * ((A + 1.0) + (A - 1.0) * cw - 2.0 * sA * al);
            a0 =           (A + 1.0) - (A - 1.0) * cw + 2.0 * sA * al;
            a1 =     2.0 * ((A - 1.0) - (A + 1.0) * cw);
            a2 =           (A + 1.0) - (A - 1.0) * cw - 2.0 * sA * al;
        }
        const double ia = 1.0 / a0;
        c.b0 = b0 * ia; c.b1 = b1 * ia; c.b2 = b2 * ia; c.a1 = a1 * ia; c.a2 = a2 * ia;
        return c;
    }

    // ── THE MATCHED (decramped) DESIGN — Vicanek's two ideas, implemented.
    //    1. POLES BY IMPULSE INVARIANCE. z = e^(sT) maps the analog pole EXACTLY: pole
    //       frequency and pole Q survive with no warping at all, which is precisely what
    //       the bilinear transform destroys near Nyquist.
    //    2. NUMERATOR BY CLOSED-FORM THREE-POINT MAGNITUDE FIT. Using the identity
    //         |c0 + c1 z^-1 + c2 z^-2|^2 = (c0+c1+c2)^2 - 4(c0c1 + 4c0c2 + c1c2)*phi
    //                                      + 16 c0c2 phi^2,   phi = sin^2(w/2)
    //       the numerator power is a QUADRATIC IN phi, so matching |H| at DC, at the fit
    //       frequency and at Nyquist is three linear-ish equations solved in closed form -
    //       no iteration, ~2x the DESIGN flops of RBJ and IDENTICAL runtime cost (it is
    //       still one biquad).
    //    Matched pole-zero alone is NOT enough and this was measured, not assumed: a
    //    16 kHz Q 2 +10 dB bell at 44.1 k designed by MPZ lands +6.97 dB at Nyquist where
    //    the analog prototype wants +3.82 dB. The numerator fit is what fixes it.
    // impulse-invariant map of ONE analog second-order section (freq, damping q) to a
    // digital pole/zero pair. Written in the overflow-safe form: for q > 1 the naive
    // -2*exp(-q*w)*cosh(sqrt(q^2-1)*w) overflows a double at q*w > 710, and this device
    // reaches q = 2857 (a -96 dB notch at Q 0.05). The identity
    //     -2 e^-qw cosh(sw) = -(e^-(q-s)w + e^-(q+s)w),   s = sqrt(q^2-1)
    // has no large intermediate at all.
    static void iiPair (double f, double q, double fs, double& c1, double& c2) noexcept
    {
        const double fc = clampd (f, 0.02, 0.47 * fs);
        const double w  = 6.283185307179586 * fc / fs;
        c2 = std::exp (-2.0 * q * w);
        if (q <= 1.0) c1 = -2.0 * std::exp (-q * w) * std::cos (std::sqrt (1.0 - q * q) * w);
        else { const double sq = std::sqrt (q * q - 1.0);
               c1 = -(std::exp (-(q - sq) * w) + std::exp (-(q + sq) * w)); }
        if (! std::isfinite (c1)) c1 = 0.0;
        if (! std::isfinite (c2)) c2 = 0.0;
    }

    // the analog prototype's pole and zero, in (frequency, damping) form. Both the peaking
    // and the two shelves are MIRROR-SYMMETRIC in gain by construction here: swapping the
    // sign of gDb swaps pole and zero exactly, which is what makes a -18 dB cut the precise
    // inverse of a +18 dB boost (constant-Q, the modern-EQ behaviour) instead of RBJ's
    // asymmetric peaking-cut, where a deep cut silently becomes octaves wide.
    static void protoPZ (int kind, double f0, double Q, double gDb, double fs,
                         double& fp, double& qp, double& fz, double& qz) noexcept
    {
        const double A = std::pow (10.0, gDb / 40.0);
        if (kind == 0) { fp = f0; qp = 1.0 / (2.0 * A * Q); fz = f0; qz = A / (2.0 * Q); }
        else if (kind == 1) { fp = f0 / std::sqrt (A); fz = f0 * std::sqrt (A); qp = qz = 1.0 / (2.0 * Q); }
        else                { fp = f0 * std::sqrt (A); fz = f0 / std::sqrt (A); qp = qz = 1.0 / (2.0 * Q); }
        (void) fs;
    }

    // 🔬 |H|^2 EVALUATED IN THE PHI BASIS, and this is not a style choice - it is a bug fix.
    //  The textbook form
    //      |B|^2 = b0^2+b1^2+b2^2 + 2(b0b1+b1b2)cos w + 2 b0b2 cos 2w
    //  is catastrophically ill-conditioned at low frequency: for a 90 Hz shelf at 48 k the
    //  three terms are ~6, ~8 and ~2 and they cancel to 2.5e-7. That is EIGHT digits gone,
    //  so a cosine table stored in FLOAT (7 digits) produces an error twice the size of the
    //  answer. MEASURED CONSEQUENCE: the drawn curve read +80 dB at 41 Hz where the band was
    //  set to +18 - the display was lying by 62 dB and only the curve-vs-spectrum gate in
    //  eq_cert §C caught it. In the phi basis (phi = sin^2(w/2)) the same quantity is
    //      (b0+b1+b2)^2 - 4(b0b1 + 4b0b2 + b1b2) phi + 16 b0b2 phi^2
    //  where the leading term IS the DC gain and the corrections are small - no cancellation
    //  at all. Same identity the numerator fit is built on.
    static double magSq (const Coeffs& c, double phi) noexcept
    {
        const double sb = c.b0 + c.b1 + c.b2, sa = 1.0 + c.a1 + c.a2;
        const double n = sb*sb - 4.0*(c.b0*c.b1 + 4.0*c.b0*c.b2 + c.b1*c.b2)*phi + 16.0*c.b0*c.b2*phi*phi;
        const double d = sa*sa - 4.0*(c.a1 + 4.0*c.a2 + c.a1*c.a2)*phi + 16.0*c.a2*phi*phi;
        return (d > 1e-300 ? std::max (0.0, n) / d : 1e300);
    }

    // ── THE MATCHED (decramped) DESIGN.
    //  Two candidates, and the design PICKS THE BETTER ONE against the analog prototype at
    //  10 fixed probe frequencies. This is not indecision - the two candidates fail in
    //  different, measured places:
    //
    //  (A) THREE-POINT MAGNITUDE FIT. Poles by impulse invariance (no warping at all), then
    //      the numerator in closed form from the identity
    //        |c0 + c1 z^-1 + c2 z^-2|^2 = (c0+c1+c2)^2 - 4(c0c1 + 4c0c2 + c1c2) phi
    //                                     + 16 c0c2 phi^2,   phi = sin^2(w/2)
    //      so |B|^2 is a QUADRATIC IN phi and matching |H| at DC / fFit / Nyquist is closed
    //      form. Superb where it is realisable. 🔬 MEASURED FAILURE: it is NOT always
    //      realisable - b0 and b2 are the roots of x^2 - Sx + W and for a high-shelf BOOST
    //      whose analog pole sits above Nyquist (16 kHz +18 dB at 48 k puts it at 26.9 kHz)
    //      the discriminant goes negative, the roots collapse to b0 = b2, the f0 constraint
    //      is silently lost and the error hits 41.7 dB. That number is why (B) exists.
    //
    //  (B) MATCHED POLE-ZERO. Poles AND zeros both by impulse invariance, one gain constant
    //      anchored at the prototype's FLAT end. Always realisable, always stable, exact
    //      resonance frequency and exact Q. 🔬 MEASURED FAILURE: it aliases the analog tail,
    //      so a 16 kHz Q2 +10 dB bell at 44.1 k lands +6.97 dB at Nyquist where the analog
    //      prototype wants +3.82 dB. That is why (A) exists.
    //
    //  Cost: ~600 extra flops per DESIGN, and designs run at kDesignBlk (32 samples) with a
    //  dirty flag, not per sample. Measured in eq_cert §M.
    static Coeffs designMatched (int kind, double f0, double Q, double gDb, double fs) noexcept
    {
        Coeffs c;
        if (gDb > -1e-12 && gDb < 1e-12) return c;
        if (Q < 1e-4) Q = 1e-4;
        double fp, qp, fz, qz;
        protoPZ (kind, f0, Q, gDb, fs, fp, qp, fz, qz);
        double a1, a2; iiPair (fp, qp, fs, a1, a2);

        // ── candidate A: the three-point magnitude fit ──────────────────────
        Coeffs A; bool haveA = false;
        {
            const double fFit = std::min (f0, 0.40 * fs);
            const double G0 = protoMag2 (kind, 0.0,      f0, Q, gDb);
            const double G1 = protoMag2 (kind, fFit,     f0, Q, gDb);
            const double GN = protoMag2 (kind, 0.5 * fs, f0, Q, gDb);
            const double sp = std::sin (3.141592653589793 * fFit / fs), phi = sp * sp;
            const double sumA = 1.0 + a1 + a2, difA = 1.0 - a1 + a2;
            const double R1 = sumA * std::sqrt (G0), R2 = difA * std::sqrt (GN);
            const double bb1 = 0.5 * (R1 - R2), S = 0.5 * (R1 + R2);
            const double PA = sumA * sumA - 4.0 * (a1 + 4.0 * a2 + a1 * a2) * phi + 16.0 * a2 * phi * phi;
            const double den = 16.0 * phi * (phi - 1.0);
            if (den < -1e-10)
            {
                const double W = (G1 * PA - R1 * R1 + 4.0 * phi * bb1 * S) / den;
                const double disc = S * S - 4.0 * W;
                if (disc >= 0.0)
                { const double r = std::sqrt (disc);
                  A.b0 = 0.5 * (S + r); A.b2 = S - A.b0; A.b1 = bb1; A.a1 = a1; A.a2 = a2;
                  haveA = finiteC (A); }
            }
        }

        // ── candidates B and D: matched pole-zero. Same poles and zeros, two different
        //    single-gain anchors (DC and Nyquist). They differ by a constant, so whichever
        //    end of the spectrum you nail, the other drifts - and which end matters depends
        //    on the band. Offering both and letting the error metric choose costs 5 lines
        //    and took the worst top-octave shelf error from 3.9 dB to under 2.
        Coeffs B, D; bool haveB = false, haveD = false;
        {
            double z1, z2; iiPair (fz, qz, fs, z1, z2);
            for (int e = 0; e < 2; ++e)
            {
                const double anchor = (e == 0) ? 0.0 : 0.5 * fs;
                const double tgt = protoMag2 (kind, anchor, f0, Q, gDb);
                const double nb = (e == 0) ? (1.0 + z1 + z2) : (1.0 - z1 + z2);
                const double na = (e == 0) ? (1.0 + a1 + a2) : (1.0 - a1 + a2);
                if (std::fabs (nb) < 1e-12 || std::fabs (na) < 1e-12) continue;
                const double k = std::sqrt (tgt) * na / nb;
                Coeffs& T = (e == 0) ? B : D;
                T.b0 = k; T.b1 = k * z1; T.b2 = k * z2; T.a1 = a1; T.a2 = a2;
                (e == 0 ? haveB : haveD) = finiteC (T);
            }
        }

        //  (C) THE PLAIN BILINEAR (RBJ). It is exact at DC / f0 / Nyquist and only its
        //      SKIRTS warp, so for a moderate bell high in the band it sometimes beats both
        //      of the above. Including it costs nothing and it never wins where the cramp
        //      actually matters - which the negative control in eq_cert §B1 proves by
        //      showing RBJ losing by 3 dB on the 16 kHz case.
        const Coeffs C3 = designRbj (kind, f0, Q, gDb, fs);
        const bool haveC = finiteC (C3);
        if (! haveA && ! haveB && ! haveD) return C3;

        // ── pick the best one, measured against the analog prototype ────────
        //  10 probes at FIXED fractions of fs, so cos(w) and cos(2w) are compile-time
        //  constants and the comparison costs no trig. Error is accumulated as
        //  (r/t + t/r), which is minimised at r == t and needs no logarithm.
        static const double FR[12] =
        { 0.0004, 0.0016, 0.0055, 0.016, 0.040, 0.075, 0.120, 0.190, 0.270, 0.340, 0.405, 0.455 };
        static const double PHI[12] = {
            0.00000157913, 0.00002526, 0.00029836, 0.00252333, 0.01552914, 0.05226423,
            0.12842966, 0.29229249, 0.53994013, 0.76248293, 0.91120266, 0.98029386 };
        double eA = haveA ? 0.0 : 1e300, eB = haveB ? 0.0 : 1e300;
        double eC = haveC ? 0.0 : 1e300, eD = haveD ? 0.0 : 1e300;
        for (int i = 0; i < 12; ++i)
        {
            const double t = protoMag2 (kind, FR[i] * fs, f0, Q, gDb);
            if (haveA) { const double r = std::max (1e-300, magSq (A,  PHI[i])); eA += (r / t + t / r); }
            if (haveB) { const double r = std::max (1e-300, magSq (B,  PHI[i])); eB += (r / t + t / r); }
            if (haveC) { const double r = std::max (1e-300, magSq (C3, PHI[i])); eC += (r / t + t / r); }
            if (haveD) { const double r = std::max (1e-300, magSq (D,  PHI[i])); eD += (r / t + t / r); }
        }
        if (eA <= eB && eA <= eC && eA <= eD) return A;
        if (eB <= eC && eB <= eD) return B;
        return (eC <= eD) ? C3 : D;
    }

    // 🔑 THE SHELF-Q CEILING — a physical limit, discovered by measurement, not a fudge.
    //  A 2-pole shelf's RESONANCE lives at its POLE pair, which sits at f0 * g^(1/4) for a
    //  high shelf: a 16 kHz corner at +30 dB puts it at 37.9 kHz, and at 44.1 k that is
    //  1.7x Nyquist. A minimum-phase digital filter CANNOT have a resonance above Nyquist,
    //  so any design that pretends otherwise produces a spurious peak at the band edge
    //  instead (clamping the pole frequency while keeping Q was exactly that bug: measured
    //  17.2 dB of error, a +4.4 dB bump where the analog prototype wanted a -12.8 dB
    //  undershoot notch). The honest answer is to TAPER the shelf Q toward 0.7 as its pole
    //  pair leaves the band. Consequence, stated in ROSTER.md: British `Slope` and Surgical
    //  `Pinch` progressively stop resonating the AIR band as Reach climbs past ~0.35 fs.
    //  They still act on LOW, where the pole is always in band. eq_cert §B measures where.
    static double usableQ (int kind, double f0, double Q, double gDb, double fs) noexcept
    {
        if (kind == 0 || kind >= 3) return Q;
        const double A = std::pow (10.0, gDb / 40.0);
        const double fp = (kind == 1) ? f0 / std::sqrt (A) : f0 * std::sqrt (A);
        const double r = fp / (0.5 * fs);
        if (r <= 0.70) return Q;
        double t = std::min (1.0, (r - 0.70) / 0.60);
        t = t * t * (3.0 - 2.0 * t);
        return Q + (0.70 - Q) * t;
    }

    // ── the hybrid law, one line: RBJ where it is exact and well conditioned, matched
    //    where the axis warps. Blended over one third of a decade so there is no seam at
    //    the boundary (both designs realise the SAME analog prototype, so in the blend
    //    band they are within ~0.05 dB of each other - eq_cert §B measures the seam).
    // 🔑 THE RING LAW — "nothing free-runs", made quantitative.
    //  A peaking BOOST's pole Q is not Q, it is A*Q: at +28 dB and Q 90 the poles sit at
    //  Q_p = 451 and the band rings for 1.80 s (measured, and it is why the section-L decay
    //  gate first read as a denormal failure). At the device's ceiling - +72 dB on a 20 Hz
    //  band at Q 90 - Q_p reaches 5670 and T60 would be TEN MINUTES: a drone, not a decay.
    //  A biquad is passive so it always terminates, but "always terminates" is not the same
    //  promise as "nothing free-runs". The pole RADIUS is therefore capped so that no
    //  resonance in this device can ring longer than kMaxRingSec, at any sample rate.
    static constexpr double kMaxRingSec = 3.0;
    static void limitRing (Coeffs& c, double fs) noexcept
    {
#ifdef EQ_MUT_NO_RINGCAP
        (void) c; (void) fs; return;                 // MUTATION: the pole-radius cap is gone
#else
        const double rMax = std::exp (-6.907755 / (kMaxRingSec * fs));   // -60 dB in kMaxRingSec
        const double r2Max = rMax * rMax;
        if (c.a2 > r2Max)
        { const double sc = std::sqrt (r2Max / c.a2); c.a1 *= sc; c.a2 = r2Max; }
#endif
    }

    static Coeffs designBand (int kind, double f0, double Q, double gDb, double fs) noexcept
    {
        if (gDb > -1e-12 && gDb < 1e-12) return Coeffs {};
        if (kind >= 3) return designOnePole (kind, f0, gDb, fs);
        const double fLo = fs / 48.0, fHi = fs / 24.0;
        if (f0 <= fLo) { Coeffs c = designRbj     (kind, f0, Q, gDb, fs); limitRing (c, fs); return c; }
        if (f0 >= fHi) { Coeffs c = designMatched (kind, f0, Q, gDb, fs); limitRing (c, fs); return c; }
        double t = (std::log (f0) - std::log (fLo)) / (std::log (fHi) - std::log (fLo));
        t = t * t * (3.0 - 2.0 * t);
        const Coeffs a = designRbj (kind, f0, Q, gDb, fs), b = designMatched (kind, f0, Q, gDb, fs);
        Coeffs c;
        c.b0 = a.b0 + t * (b.b0 - a.b0); c.b1 = a.b1 + t * (b.b1 - a.b1);
        c.b2 = a.b2 + t * (b.b2 - a.b2); c.a1 = a.a1 + t * (b.a1 - a.a1);
        c.a2 = a.a2 + t * (b.a2 - a.a2);
        limitRing (c, fs);
        return c;
    }

    // ── the 1-pole (6 dB/oct) shelf. Baxandall's slope: the Slant and every "gentle"
    //    Character live here. Bilinear, prewarped; the primitive takes the prewarped corner
    //    G = tan(pi f_corner / fs) DIRECTLY, because the Slant needs to place its corner by
    //    ARITHMETIC (see designSlant) and going back through an atan/tan round trip would
    //    put a rounding error exactly where the pivot has to be exact.
    //    gLoDb == gHiDb == 0 gives b1 == a1 BITWISE, which is what makes the Slant null
    //    exactly rather than approximately.
    //
    //  🔑 THE ONE-POLE POLE CAP. a1 = (G-1)/(G+1), so the pole radius is |a1| and it goes to
    //    1 as G leaves [G_min, G_max]. A real pole at r = 0.99999 is not a resonance, but it
    //    IS a 15-second decay, and "nothing free-runs" is a promise about the DEVICE, not
    //    about resonances only. G is therefore clamped to exactly the window in which
    //    |a1| <= rMax, the SAME rMax the biquad ring cap uses.
    static double onePoleGmax (double fs) noexcept
    {
#ifdef EQ_MUT_NO_RINGCAP
        (void) fs; return 1e18;                       // MUTATION: the cap is gone
#else
        const double r = std::exp (-6.907755 / (kMaxRingSec * fs));
        return (1.0 + r) / (1.0 - r);
#endif
    }
    static Coeffs designShelf1G (double G, double gLoDb, double gHiDb, double fs) noexcept
    {
        const double gMax = onePoleGmax (fs);
        Coeffs c;
        G = clampd (G, 1.0 / gMax, gMax);
        const double gL = std::pow (10.0, gLoDb / 20.0), gH = std::pow (10.0, gHiDb / 20.0);
        const double d  = 1.0 + G;
        c.b0 = (gL * G + gH) / d;
        c.b1 = (gL * G - gH) / d;
        c.a1 = (G - 1.0) / d;
        c.b2 = 0.0; c.a2 = 0.0;
        if (gLoDb == 0.0 && gHiDb == 0.0) { c.b0 = 1.0; c.b1 = c.a1; }      // exact identity
        return c;
    }
    static Coeffs designShelf1 (double f0, double gLoDb, double gHiDb, double fs) noexcept
    {
        return designShelf1G (std::tan (3.141592653589793 * clampd (f0, 1.0, 0.49 * fs) / fs),
                              gLoDb, gHiDb, fs);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  🚨 fb422 — THE SLANT (front hero 1, was `Tilt`). THE BUG AND THE FIX.
    //
    //  WAS:  designShelf1 (pivot, -g, +g).  A one-pole shelf with corner w0 and shoulder
    //        gains gL = 1/s, gH = s has |H| = 1 where
    //             gL^2 w0^2 + gH^2 w^2 = w0^2 + w^2  =>  w = w0 / s.
    //        So the 0 dB crossing was NOT the corner: it slid DOWN by the gain itself.
    //        At the shipped 700 Hz pivot the crossing walked 700 Hz -> 2.8 Hz as the knob
    //        opened, and everything the crossing swept past REVERSED DIRECTION. Measured by
    //        the integration owner at Amount default: 120 Hz fell to -4.75 dB at 65 % and
    //        then rose to +8.55 dB at 100 % — 13.31 dB of wrong-way travel; 37.3 dB at
    //        Amount 200 %. Turn it toward the treble and the bass comes back up.
    //        The old cert could not see it: it gated 8 kHz MINUS 80 Hz, a DIFFERENCE, and a
    //        difference stays monotone while both ends reverse in common mode.
    //
    //  IS:   put the CORNER where the crossing has to land: G_corner = s * G_pivot, in the
    //        PREWARPED (tangent) domain, so the pivot is exact in the DIGITAL filter and not
    //        merely in its analog prototype. Then
    //             |H(w)|^2 = (Gp^2 + s^2 t^2) / (s^2 Gp^2 + t^2),  t = tan(pi f / fs)
    //        which is exactly 1 at t = Gp for EVERY s, at EVERY sample rate, and whose
    //        derivative in s has the sign of (t^4 - Gp^4): strictly DOWN below the pivot,
    //        strictly UP above it, no reversal anywhere. Gated end-by-end in eq_cert §F1.
    //
    //  The price, stated: a 6 dB/oct seesaw is SLOPE-limited, so the in-band travel of an
    //  end is bounded by its distance from the pivot, not by the knob. That bound is what
    //  §K now measures per end, and it is why `Deep Pivot` / `Bright Pivot` exist.
    // ═════════════════════════════════════════════════════════════════════════
    static Coeffs designSlant (double fPivot, double gDb, double fs) noexcept
    {
        const double Gp = std::tan (3.141592653589793 * clampd (fPivot, 1.0, 0.49 * fs) / fs);
        const double s  = std::pow (10.0, gDb / 20.0);
#ifdef EQ_MUT_NO_PIVOT
        return designShelf1G (Gp, -gDb, gDb, fs);       // MUTATION: the fb420 sliding pivot
#else
        return designShelf1G (Gp * s, -gDb, gDb, fs);
#endif
    }

    // kind 3 = low shelf (6 dB/oct) · kind 4 = high shelf (6 dB/oct) · kind 5 = SLANT
    // (Baxandall's seesaw about a FIXED pivot: gain -t below and +t above, which is why the
    //  whole spectrum leans instead of one end stepping).
    static Coeffs designOnePole (int kind, double f0, double gDb, double fs) noexcept
    {
        if (kind == 5) return designSlant  (f0, gDb, fs);
        if (kind == 4) return designShelf1 (f0, 0.0,  gDb, fs);
        return             designShelf1 (f0, gDb,  0.0, fs);
    }

    static double magDb (const Coeffs& c, double f, double fs) noexcept
    {
        const double sp = std::sin (3.141592653589793 * f / fs);
        const double m = magSq (c, sp * sp);
        return (m > 1e-30) ? 10.0 * std::log10 (m) : -300.0;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  THE ROSTER TABLES
    // ═════════════════════════════════════════════════════════════════════════
    //  Band order everywhere: 0 LOW · 1 BODY · 2 BITE · 3 AIR.
    //  kind codes: 0 bell · 1 low shelf 2-pole · 2 high shelf 2-pole
    //              3 low shelf 1-pole · 4 high shelf 1-pole · 5 = "type default, but 24 dB/oct"
    enum Law { LawConstant = 0, LawBritish, LawProportional, LawPassive, LawOpen, LawDynamic, LawChisel };

    struct TypeSpec
    {
        float qBase[4];
        int   kind[4];
        int   law;
        float soften;      // gain-law softener strength (0 = exact dB)
    };

    struct CharSpec
    {
        const char* nm;
        float qm[4];       // per-band Q multiplier
        float fm[4];       // per-band frequency multiplier
        float gm[4];       // per-band gain multiplier
        int   kd[4];       // -1 = type default, else force kind (5 = steep)
        float pivotHz;      // Slant pivot, Hz
        float cutQ;        // extra Q multiplier applied to CUTS only (boost wide / cut narrow)
        float cutG;        // extra gain multiplier applied to CUTS only
        float e1, e2, e3;  // per-Type meanings, documented in ROSTER.md §Characters
    };

    static const TypeSpec& typeSpec (int t) noexcept
    {
        static const TypeSpec T[kNumTypes] =
        {   // qBase                 kind (LOW BODY BITE AIR)   law               soften
            { {0.90f,1.00f,1.00f,0.90f}, {1,0,0,2}, LawConstant,     0.00f },  // Surgical
            { {0.80f,1.00f,1.00f,0.80f}, {1,0,0,2}, LawBritish,      0.10f },  // British
            { {1.00f,1.00f,1.00f,1.00f}, {1,0,0,2}, LawProportional, 0.00f },  // American
            { {0.90f,0.80f,0.70f,0.80f}, {1,0,0,2}, LawPassive,      0.00f },  // Passive
            { {0.45f,0.40f,0.40f,0.50f}, {1,0,0,4}, LawOpen,         0.00f },  // Open
            { {0.90f,1.00f,1.00f,0.90f}, {1,0,0,2}, LawDynamic,      0.00f },  // Dynamic
            { {1.00f,1.00f,1.00f,1.00f}, {1,0,0,2}, LawChisel,       0.00f }   // Chisel
        };
        return T[clampi (t, 0, kNumTypes - 1)];
    }

    static const CharSpec& charSpec (int t, int c) noexcept
    {
        static const CharSpec C[kNumTypes][kNumChars] =
        {
        // ── 1 SURGICAL — e1/e2/e3 unused. The reference type: exact dB, constant Q.
        { { "Plain",        {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 0,0,0 },
          { "Tight",        {2.6f,2.6f,2.6f,2.6f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 0,0,0 },
          { "Broad",        {0.35f,0.35f,0.35f,0.35f},{1,1,1,1},    {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 0,0,0 },
          { "Steep",        {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   { 5,-1,-1, 5}, 700.0f, 1.0f,1.0f, 0,0,0 },
          { "Scoop",        {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 3.5f,1.0f, 0,0,0 },
          { "Deep Pivot",   {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 150.0f, 1.0f,1.0f, 0,0,0 },
          { "Bright Pivot", {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1},3000.0f, 1.0f,1.0f, 0,0,0 },
          { "Four Bells",   {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   { 0,-1,-1, 0}, 700.0f, 1.0f,1.0f, 0,0,0 } },

        // ── 2 BRITISH — e1 = Slope strength · e2 = gain softener on(1)/off(0) · e3 unused.
        { { "Desk",         {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,1,0 },
          { "Big Knob",     {1.0f,0.50f,0.50f,1.0f},{1,1,1,1},      {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,1,0 },
          { "Ahead",        {1.0f,2.2f,2.4f,1.0f},{1,1.5f,1.2f,1},  {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,1,0 },
          { "Iron Top",     {1.0f,1.0f,1.0f,1.9f},{1,1,1,0.78f},    {1,1,1,1.1f},{-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.6f,1,0 },
          { "Sub Iron",     {1.6f,1.0f,1.0f,1.0f},{0.42f,1,1,1},    {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.3f,1,0 },
          { "Steep Iron",   {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   { 5,-1,-1, 5}, 700.0f, 1.0f,1.0f, 1.0f,1,0 },
          { "Full Swing",   {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 0.45f,0,0 },
          { "Mid Rise",     {1.0f,0.65f,1.0f,1.0f},{1,1.9f,1,1},    {1,1.25f,1,1},{-1,-1,-1,-1},700.0f, 1.0f,1.0f, 1.0f,1,0 } },

        // ── 3 AMERICAN — e1 = exponent offset · e2 = 0 both / 1 boost-only / 2 cut-only
        //    e3 = shelves ride the law too (1) or stay constant-S (0).
        { { "Proportional", {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 0.0f,0,0 },
          { "Lasers",       {1.6f,1.6f,1.6f,1.6f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.2f,0,0 },
          { "Mellow",       {0.7f,0.7f,0.7f,0.7f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-0.55f,0,0 },
          { "Floor Lift",   {2.2f,2.2f,2.2f,2.2f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-0.30f,0,0 },
          { "Boost Only",   {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 0.0f,1,0 },
          { "Cut Only",     {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 0.0f,2,0 },
          { "Shelf Ride",   {1.4f,1.0f,1.0f,1.4f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 0.0f,0,1 },
          { "Bolt",         {2.2f,2.2f,2.2f,2.2f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 2.0f,0,0 } },

        // ── 4 PASSIVE — e1 = ride-along frequency ratio · e2 = 0 LOW only / 1 LOW+AIR /
        //    2 ride-along is a BELL instead of a shelf · e3 unused.
        { { "Baseline",     {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 2.2f,0,0 },
          { "Close Cut",    {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.4f,0,0 },
          { "Far Cut",      {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 4.4f,0,0 },
          { "Both Ends",    {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 2.2f,1,0 },
          { "Bell Top",     {1.0f,1.0f,1.0f,0.70f},{1,1,1,1},       {1,1,1,1},   {-1,-1,-1, 0}, 700.0f, 1.0f,1.0f, 2.2f,0,0 },
          { "Slow Top",     {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1, 4}, 700.0f, 1.0f,1.0f, 2.2f,0,0 },
          { "Deep Atten",   {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.45f,2.2f,0,0 },
          { "Revival",      {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 2.2f,2,0 } },

        // ── 5 OPEN — e1 = Silk octave offset · e2 = tanh knee (dB) · e3 unused.
        { { "Gloss",        {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,22.0f,0 },
          { "Very Wide",    {0.55f,0.55f,0.55f,0.55f},{1,1,1,1},    {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,22.0f,0 },
          { "Two Shelves",  {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1, 1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,22.0f,0 },
          { "Twin Shelf",   {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-1.0f,22.0f,0 },
          { "Deep Reach",   {1.0f,1.0f,1.0f,1.5f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-2.0f,22.0f,0 },
          { "Soft Knee",    {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,12.0f,0 },
          { "Hard Knee",    {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,90.0f,0 },
          { "Bell Air",     {1.0f,1.0f,1.0f,0.50f},{1,1,1,1},       {1,1,1,1},   {-1,-1,-1, 0}, 700.0f, 1.0f,1.0f, 1.0f,22.0f,0 } },

        // ── 6 DYNAMIC — e1 = ballistics scale · e2 = 0 normal / 1 inverted / 2 wideband
        //    detector / 3 peak hold · e3 = threshold window width in dB.
        { { "Program Ride", {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,0,14.0f },
          { "Quick",        {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 0.22f,0,14.0f },
          { "Lazy",         {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 5.0f,0,14.0f },
          { "Wideband",     {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,2,14.0f },
          { "Upward",       {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,1,14.0f },
          { "Hard Window",  {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,0, 2.5f },
          { "Soft Window",  {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,0,34.0f },
          { "Peak Keep",    {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f, 1.0f,3,14.0f } },

        // ── 7 SCULPT — e1 = notch-morph knee in dB · e2 = Q/gain coupling · e3 unused.
        { { "Resonator",    {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-18.0f,1.0f,0 },
          { "Scalpel",      {2.5f,2.5f,2.5f,2.5f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-12.0f,1.0f,0 },
          { "Triple Notch", {1.0f,1.0f,1.0f,1.0f},{1,2.0f,4.0f,1},  {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-14.0f,1.0f,0 },
          { "Gain Peak",    {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-18.0f,2.6f,0 },
          { "Shallow",      {1.0f,1.0f,1.0f,1.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-90.0f,1.0f,0 },
          { "Handset",      {1.0f,1.0f,1.0f,1.0f},{2.5f,1,1,0.35f}, {1,1,1,1},   { 0,-1,-1, 0}, 700.0f, 1.0f,1.0f,-18.0f,1.0f,0 },
          { "Sub Kill",     {3.0f,1.0f,1.0f,1.0f},{0.35f,1,1,1},    {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-10.0f,1.0f,0 },
          { "Tin",          {4.0f,4.0f,4.0f,4.0f},{1,1,1,1},        {1,1,1,1},   {-1,-1,-1,-1}, 700.0f, 1.0f,1.0f,-18.0f,1.8f,0 } }
        };
        return C[clampi (t, 0, kNumTypes - 1)][clampi (c, 0, kNumChars - 1)];
    }

    // ── param ranges, in one place so the harness and the UI cannot disagree ─────
    static float bandHz (int b, float t) noexcept
    {
        static const float lo[4] = {   20.0f,  100.0f,   700.0f,  6000.0f };
        static const float hi[4] = {  500.0f, 3000.0f, 14000.0f, 40000.0f };
        const int i = clampi (b, 0, 3);
        return lo[i] * std::pow (hi[i] / lo[i], clampf (t, 0.0f, 1.0f));
    }
#ifdef EQ_MUT_NO_CEILING
    // MUTATION: the ranges of a polite mixing EQ — +-10 dB a band, +-8 dB of Slant,
    // Amount that stops at 100 %. Every number a console gives you, and no more.
    static constexpr float kBandDbSpan = 10.0f;
    static constexpr float kSlantDbSpan = 8.0f;
    static constexpr float kAmountMax  = 1.0f;
#else
    static constexpr float kBandDbSpan = 30.0f;    // +-30 dB per band  (R11)
    static constexpr float kSlantDbSpan = 24.0f;   // +-24 dB seesaw    (R11)
    static constexpr float kAmountMax  = 2.0f;     // 200 %             (R11)
#endif
    static constexpr float kQMin       = 0.05f;
    static constexpr float kQMax       = 90.0f;
    static constexpr float kGainCeil   =  72.0f;   // hard design clamp, dB
    static constexpr float kGainFloor  = -90.0f;   // notch morph reaches here
    static constexpr int   kDesignBlk  = 32;       // samples between coefficient designs

    // Focus = Side processes ONLY the difference signal, so a mono fold-down cancels the
    // entire wet path. That is inherent to every M/S EQ ever built; it is TAGGED here and
    // MEASURED in eq_cert §H rather than hidden.
    static bool focusIsMonoHostile (int axis) noexcept { return axis == 2; }

    // ═════════════════════════════════════════════════════════════════════════
    //  LIFECYCLE
    // ═════════════════════════════════════════════════════════════════════════
    void prepare (double sampleRate, int /*maxBlock*/) noexcept
    {
        fs_ = (float) (sampleRate > 1000.0 ? sampleRate : 48000.0);

        // control-rate one-poles: the design cadence is kDesignBlk samples, so the
        // per-step coefficient is derived from the cadence, never from the sample rate
        // alone (otherwise the glide time changes with fs and the click gate at 96 k
        // measures a different device than the one at 44.1 k).
        const float step = (float) kDesignBlk / fs_;
        for (int i = 0; i < 8;  ++i) kSm_[i] = 1.0f - std::exp (-step / kTau[i]);
        for (int i = 8; i < kNumSm; ++i) kSm_[i] = 1.0f - std::exp (-step / kTau[i]);
        mixK_  = 1.0f - std::exp (-1.0f / (0.010f * fs_));
#ifdef EQ_MUT_NO_SMOOTH
        for (int i = 0; i < kNumSm; ++i) kSm_[i] = 1.0f;  // MUTATION: all smoothers, tau -> 0
        mixK_ = 1.0f;                                     // MUTATION: the Mix smoother, gone
#endif
        dipDn_ = 1.0f - std::exp (-1.0f / (0.008f * fs_));
        dipUp_ = 1.0f - std::exp (-1.0f / (0.030f * fs_));
        lvlK_  = 1.0f - std::exp (-1.0f / (0.060f * fs_));

        // the 96 log bins, trig precomputed: the viz push is then pure arithmetic.
        for (int i = 0; i < kCurveBins; ++i)
        {
            const double f = (double) curveBinHz (i);
            const double w = 6.283185307179586 * f / (double) fs_;
            const double sp = std::sin (0.5 * w);
            binPhi_[i] = sp * sp;
        }
        curveEvery_ = (int) (fs_ / 60.0f);          // ~60 Hz push, the house cadence
        if (curveEvery_ < 64) curveEvery_ = 64;

        // 🚨 THE NaN LAW (ParametricEQ.h:42-47, carried forward): seed every smoother to a
        // finite, musically-neutral value BEFORE the first design. A smoother that defaults
        // to 0 gives Q = 0, alpha = sin/(2Q) = NaN, and the coefficients stay poisoned for
        // the lifetime of the instance. Q is additionally floored at kQMin in resolve().
        seedSmoothers();
        reset();
        controlStep();
        snapCoeffs();
    }

    void reset() noexcept                                    // NO allocation, ever
    {
        for (int s = 0; s < kNumStages; ++s)
            for (int c = 0; c < 2; ++c) { st_[s].z1[c] = 0.0; st_[s].z2[c] = 0.0; }
        for (int b = 0; b < kNumBands; ++b)
        { d1_[b] = 0.0f; d2_[b] = 0.0f; env_[b] = 0.0f; ride_[b] = 1.0f; }
        envW_ = 0.0f; lvlSm_ = 0.0f; dip_ = 1.0f; ctr_ = 0; curveCtr_ = 0;
        pendType_ = -1; pendChar_ = -1; pendFocus_ = -1;
    }

    void setParams (const Params& p) noexcept                // PER BLOCK — cheap
    {
        const int t = clampi (p.type,      0, kNumTypes - 1);
        const int c = clampi (p.character, 0, kNumChars - 1);
#ifdef EQ_MUT_FLAT_FOCUS
        // MUTATION: `Side` silently becomes `Stereo` — one dropdown OPTION deleted, with the
        // label still on the card. Every structural Focus gate (bit-exact pass-through, the
        // M/S round trip, the mono fold) still passes; only a gate that compares the OPTIONS
        // TO EACH OTHER on every Type can see it.
        int f = clampi (p.axis, 0, kNumFocus - 1); if (f == 2) f = 0;
#else
        const int f = clampi (p.axis,      0, kNumFocus - 1);
#endif

        if (t != type_ || c != char_ || f != focus_)
        {
#ifdef EQ_MUT_NO_DIP
            // MUTATION: no fade-swap. Adopt the new filter bank inside the block.
            type_ = t; char_ = c; focus_ = f; flushFocusStates();
            for (int i = 0; i < kNumSm; ++i) sm_[i] = tg_[i];
            resolve(); designAll (true);
#else
            if (! seeded_ || isFlat())        // a flat device has nothing to crossfade:
            { type_ = t; char_ = c; focus_ = f; flushFocusStates(); }   // snap, stay bit-exact
            else { pendType_ = t; pendChar_ = c; pendFocus_ = f; }      // else fade-swap-recover
#endif
        }
        p_ = p;
        tg_[0] = clampf (p.b1, 0.0f, 1.0f);  tg_[1] = clampf (p.b2, 0.0f, 1.0f);
        tg_[2] = clampf (p.b3, 0.0f, 1.0f);  tg_[3] = clampf (p.b4, 0.0f, 1.0f);
        tg_[4] = clampf (p.b5, 0.0f, 1.0f);  tg_[5] = clampf (p.b6, 0.0f, 1.0f);
        tg_[6] = clampf (p.b7, 0.0f, 1.0f);  tg_[7] = clampf (p.b8, 0.0f, 1.0f);
        tg_[8] = clampf (p.f1, 0.0f, 1.0f);  tg_[9] = clampf (p.f2, 0.0f, 1.0f);
        tg_[10]= clampf (p.f3, 0.0f, 1.0f);
        tg_[11]= clampf (p.x1, 0.0f, 1.0f);  tg_[12]= clampf (p.x2, 0.0f, 1.0f);   // fb438 — free bells
        tg_[13]= clampf (p.x3, 0.0f, 1.0f);  tg_[14]= clampf (p.x4, 0.0f, 1.0f);
        tg_[15]= clampf (p.x5, 0.0f, 1.0f);  tg_[16]= clampf (p.x6, 0.0f, 1.0f);
        tg_[17]= clampf (p.x7, 0.0f, 1.0f);  tg_[18]= clampf (p.x8, 0.0f, 1.0f);
        xOn_[0] = p.xOn1; xOn_[1] = p.xOn2; xOn_[2] = p.xOn3; xOn_[3] = p.xOn4;
#ifdef EQ_MUT_MIX_WET
        mixTg_ = 1.0f;                                     // MUTATION: the Mix knob is ignored
#else
        mixTg_ = clampf (p.mix, 0.0f, 1.0f);
#endif
    }

    // ── the block. IN-PLACE, wet+dry per Mix. ────────────────────────────────
    void processStereo (float* L, float* R, int n) noexcept
    {
        if (L == nullptr || R == nullptr || n <= 0) return;
        if (! seeded_)
        {
            for (int i = 0; i < kNumSm; ++i) sm_[i] = tg_[i];
            mixSm_ = mixTg_; seeded_ = true;
            controlStep(); snapCoeffs();
        }

        const double S2 = 0.70710678118654752;
        int i = 0;
        while (i < n)
        {
            if (ctr_ <= 0) { controlStep(); ctr_ = kDesignBlk; }
            const int nn = std::min (n - i, ctr_);
            ctr_ -= nn;

            for (int k = 0; k < nn; ++k, ++i)
            {
                // 1. coefficient glide — the whole point of the control-rate design
                for (int s = 0; s < nAct_; ++s)
                {
                    Stage& S = st_[act_[s]];
                    S.cb0 += S.ib0; S.cb1 += S.ib1; S.cb2 += S.ib2;
                    S.ca1 += S.ia1; S.ca2 += S.ia2;
                }

                const float dl = L[i], dr = R[i];
                double a, b;
                switch (focus_)
                {
                    case 1:  a = (dl + dr) * S2; b = (dl - dr) * S2; break;   // Mid
                    case 2:  a = (dl - dr) * S2; b = (dl + dr) * S2; break;   // Side
                    case 4:  a = dr;             b = dl;             break;   // Right
                    default: a = dl;             b = dr;             break;   // Stereo / Left
                }

                // 2. the cascade. Channel slot 0 always runs; slot 1 only in Stereo focus.
                const bool both = (focus_ == 0);
                for (int s = 0; s < nAct_; ++s)
                {
                    Stage& S = st_[act_[s]];
                    { const double x = a; const double y = S.cb0 * x + S.z1[0];
                      S.z1[0] = S.cb1 * x - S.ca1 * y + S.z2[0];
                      S.z2[0] = S.cb2 * x - S.ca2 * y; a = y; }
                    if (both)
                    { const double x = b; const double y = S.cb0 * x + S.z1[1];
                      S.z1[1] = S.cb1 * x - S.ca1 * y + S.z2[1];
                      S.z2[1] = S.cb2 * x - S.ca2 * y; b = y; }
                }

                float wl, wr;
                switch (focus_)
                {
                    case 1:  wl = (float) ((a + b) * S2); wr = (float) ((a - b) * S2); break;
                    case 2:  wl = (float) ((b + a) * S2); wr = (float) ((b - a) * S2); break;
                    case 4:  wl = (float) b;              wr = (float) a;              break;
                    default: wl = (float) a;              wr = (float) b;              break;
                }

                // 3. the Type/Character/Focus fade-swap dip rides the WET only — the dry is
                //    never touched by a switch, which is why Mix < 100 % stays anchored.
#ifndef EQ_MUT_NO_DIP
                if (pendType_ >= 0) dip_ += dipDn_ * (0.02f - dip_);
                else                dip_ += dipUp_ * (1.0f - dip_);
                wl *= dip_; wr *= dip_;
#endif

                // 4. LINEAR mix. Dry and wet are 100 % correlated here (same signal, minimum
                //    phase); an equal-power sin/cos law would bump +3 dB at 50 %.
                mixSm_ += mixK_ * (mixTg_ - mixSm_);
                const float mw = mixSm_, md = 1.0f - mixSm_;
                const float ol = dl * md + wl * mw;
                const float orr = dr * md + wr * mw;
                L[i] = ol; R[i] = orr;

                const float pk = std::fabs (ol) > std::fabs (orr) ? std::fabs (ol) : std::fabs (orr);
                lvlSm_ += lvlK_ * (pk - lvlSm_);

                // 5. the Dynamic detectors, per sample, on the mono sum (LINKED: a per-channel
                //    detector would move the stereo image every time the program moved).
                if (typeSpec (type_).law == LawDynamic) detectorStep (0.5f * (dl + dr));
            }
        }
        viz_.lvl = clampf (lvlSm_ * 6.0f, 0.0f, 1.0f);
    }

    const Viz& viz() const noexcept { return viz_; }

    // exposed for the harness: what the engine currently believes it is doing
    float bandHzNow (int b) const noexcept { return viz_.nodeHz[clampi (b, 0, 3)]; }
    float bandDbNow (int b) const noexcept { return viz_.nodeDb[clampi (b, 0, 3)]; }

private:
    // ═════════════════════════════════════════════════════════════════════════
    static constexpr int kNumStages = 13;                    // 4 bands x 2 + slant + 4 free bells (fb438)
    static constexpr int kSlant = 8;
    static constexpr int kFree0 = 9;                         // fb438 — stages 9..12 are the free bells
    static constexpr int kNumSm = 19;                        // 11 + the free bells' 8 (fb438)

    struct Stage
    {
        bool  on = false;
        int   kind = 0;
        float f = 1000.0f, q = 1.0f, g = 0.0f;
        float lf = -1.0f, lq = -1.0f, lg = 1e9f;             // last designed inputs
        double cb0 = 1.0, cb1 = 0.0, cb2 = 0.0, ca1 = 0.0, ca2 = 0.0;
        double tb0 = 1.0, tb1 = 0.0, tb2 = 0.0, ta1 = 0.0, ta2 = 0.0;
        double ib0 = 0.0, ib1 = 0.0, ib2 = 0.0, ia1 = 0.0, ia2 = 0.0;
        double z1[2] {}, z2[2] {};
    };

    // 🔑 fb422 — the taus are ordered by GAIN AUTHORITY, and `Amount` was the exception that
    //  proved it wrong: it multiplies ALL FOUR band gains (+-60 dB of authority, 4x any
    //  single gain knob) and it had the SHORTEST tau in the table, 10 ms. An instantaneous
    //  Amount write (host automation, preset recall) measured 1.63 dB of wet-gain change in
    //  ONE sample — the only real click in the device. The number scales as 1/tau
    //  (10 ms 1.63 · 20 ms 0.85 · 30 ms 0.57), which is what proves it IS a smoothing fault
    //  and not physics — `Trait`, tested the same way, does NOT move with tau (20/60/150 ms
    //  read 7.20/7.97/7.50) because that one is a Q 40 resonator dumping stored energy.
    //  Amount now sits at 20 ms, the same as the widest-span controls, still inside the
    //  house 10-30 ms rule. eq_cert §J2 gates it.
    static constexpr float kTau[kNumSm] =
    { 0.020f, 0.012f, 0.020f, 0.012f, 0.020f, 0.012f, 0.020f, 0.020f,   // b1..b8
      0.015f, 0.012f, 0.020f,                                            // Slant, Air, Amount
      0.020f, 0.012f, 0.020f, 0.012f, 0.020f, 0.012f, 0.020f, 0.012f }; // fb438 — free bells (freq, gain) x 4

    static int   clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static double clampd (double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static bool  finiteC (const Coeffs& c) noexcept
    { return std::isfinite (c.b0) && std::isfinite (c.b1) && std::isfinite (c.b2)
          && std::isfinite (c.a1) && std::isfinite (c.a2); }

    void seedSmoothers() noexcept
    {
        for (int i = 0; i < kNumSm; ++i) { sm_[i] = 0.5f; tg_[i] = 0.5f; }
        mixSm_ = mixTg_ = 1.0f;
        seeded_ = false;
    }

    void flushFocusStates() noexcept
    {   // the un-focused channel slot must never re-enter carrying a minutes-old tail
        for (int s = 0; s < kNumStages; ++s) { st_[s].z1[1] = 0.0; st_[s].z2[1] = 0.0; }
    }

    bool isFlat() const noexcept
    {
        float m = std::fabs (slantDb_);
        for (int b = 0; b < kNumNodes; ++b) m = std::max (m, std::fabs (viz_.nodeDb[b]));   // fb438 — free bells count too
        return m < 0.5f;
    }

    void snapCoeffs() noexcept
    {
        for (int s = 0; s < kNumStages; ++s)
        {
            Stage& S = st_[s];
            S.cb0 = S.tb0; S.cb1 = S.tb1; S.cb2 = S.tb2; S.ca1 = S.ta1; S.ca2 = S.ta2;
            S.ib0 = S.ib1 = S.ib2 = S.ia1 = S.ia2 = 0.0;
        }
    }

    // ── the Dynamic detector: one SVF band-pass per band on the LINKED mono sum, rectified
    //    into an asymmetric one-pole. Attack = max(2 ms, 2/fc) so a 40 Hz band cannot chase
    //    its own waveform; release = 8 x attack.
    void detectorStep (float x) noexcept
    {
        const CharSpec& C = charSpec (type_, char_);
        const bool wide = (C.e2 > 1.5f && C.e2 < 2.5f);
        if (wide)
        {
            const float r = std::fabs (x);
            envW_ += (r > envW_ ? dAtk_[0] : dRel_[0]) * (r - envW_);
            if (envW_ < 1e-20f) envW_ = 0.0f;
            for (int b = 0; b < kNumBands; ++b) env_[b] = envW_;
            return;
        }
        for (int b = 0; b < kNumBands; ++b)
        {
            const float v3 = x - d2_[b];
            const float v1 = sA1_[b] * d1_[b] + sA2_[b] * v3;
            const float v2 = d2_[b] + sA2_[b] * d1_[b] + sA3_[b] * v3;
            d1_[b] = 2.0f * v1 - d1_[b];  d2_[b] = 2.0f * v2 - d2_[b];
            if (d1_[b] < 1e-20f && d1_[b] > -1e-20f) d1_[b] = 0.0f;
            if (d2_[b] < 1e-20f && d2_[b] > -1e-20f) d2_[b] = 0.0f;
            const float r = std::fabs (v1);
            env_[b] += (r > env_[b] ? dAtk_[b] : dRel_[b]) * (r - env_[b]);
            if (env_[b] < 1e-20f) env_[b] = 0.0f;
        }
    }

    static float widthMul (float s) noexcept
    {   // Surgical `Pinch`: a global Q multiplier with a CENTRE DETENT at 1.0x and a
        // destructive top (x40 => Q 40 on the 1.0 base). Asymmetric log so 0.5 is exactly 1.
        return s <= 0.5f ? 0.25f * std::pow (4.0f,  s * 2.0f)
                         :          std::pow (40.0f, (s - 0.5f) * 2.0f);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  RESOLVE — knobs to physics. Runs once per kDesignBlk samples, never per sample.
    // ═════════════════════════════════════════════════════════════════════════
    void resolve() noexcept
    {
        const TypeSpec& T = typeSpec (type_);
        const CharSpec& C = charSpec (type_, char_);
        const float amount = sm_[10] * kAmountMax;
#ifdef EQ_MUT_DEAD_CELL
        // MUTATION: `Trait` is ignored on exactly ONE cell — Open x `Soft Knee`.
        const float shape  = (type_ == 4 && char_ == 5) ? 0.0f : sm_[7];
#else
        const float shape  = sm_[7];
#endif

        float f[4], g[4], q[4];
        int   kd[4]; bool steep[4];

        f[0] = bandHz (0, sm_[0]) * C.fm[0];
        f[1] = bandHz (1, sm_[2]) * C.fm[1];
        f[2] = bandHz (2, sm_[4]) * C.fm[2];
        f[3] = bandHz (3, sm_[6]) * C.fm[3];
        g[0] = (sm_[1] * 2.0f - 1.0f) * kBandDbSpan * C.gm[0];
        g[1] = (sm_[3] * 2.0f - 1.0f) * kBandDbSpan * C.gm[1];
        g[2] = (sm_[5] * 2.0f - 1.0f) * kBandDbSpan * C.gm[2];
        g[3] = (sm_[9] * 2.0f - 1.0f) * kBandDbSpan * C.gm[3];   // AIR gain IS the front hero
        for (int b = 0; b < 4; ++b) if (g[b] < 0.0f) g[b] *= C.cutG;

        for (int b = 0; b < 4; ++b)
        {
            kd[b] = (C.kd[b] >= 0 ? C.kd[b] : T.kind[b]);
            steep[b] = (kd[b] == 5);
            if (steep[b]) kd[b] = T.kind[b];
        }

        // ── the Type's GAIN law (before Amount: Amount must always be a clean x2) ──
        if (T.law == LawBritish)
        {   // inductor swing compression: the top of the knob leans on itself
            for (int b = 0; b < 4; ++b)
            { const float u = std::fabs (g[b]) / kBandDbSpan;
              g[b] *= (1.0f - T.soften * C.e2 * u * u); }
        }
        else if (T.law == LawOpen)
        {   // the Sie-Q feel: a tanh knee, so "boost it forever" never gets shrill
            const float knee = C.e2;
            for (int b = 0; b < 4; ++b) g[b] = knee * std::tanh (g[b] / knee);
            g[3] *= 1.33f;                                    // Open's AIR reaches further
        }
        else if (T.law == LawChisel)
        {   // the last quarter of the DOWNWARD travel morphs the bell into a true notch
            for (int b = 0; b < 4; ++b)
                if (g[b] < C.e1)
                { const float sl = (kGainFloor - C.e1) / (-kBandDbSpan - C.e1);
                  g[b] = C.e1 + (g[b] - C.e1) * sl; }
        }
        else if (T.law == LawPassive)
        {   // the EQP-1A attenuator is deeper than its boost (17.5 vs 13.5 dB)
            for (int b = 0; b < 4; ++b) if (g[b] < 0.0f) g[b] *= 1.15f;
        }

        for (int b = 0; b < 4; ++b) g[b] = clampf (g[b] * amount, -96.0f, kGainCeil);
#ifdef EQ_MUT_POLITE_CELL
        // MUTATION: a polite console ceiling on exactly ONE cell — Passive x `Deep Atten`.
        if (type_ == 3 && char_ == 6)
            for (int b = 0; b < 4; ++b) g[b] = clampf (g[b], -10.0f, 10.0f);
#endif

        // ── the DYNAMIC ride: the ONE type allowed to be level-dependent (CONTRACT §4) ──
        if (T.law == LawDynamic)
        {
            const float Th = -26.0f + (shape * 2.0f - 1.0f) * 20.0f;   // program-anchored
            const float Wd = std::max (0.5f, C.e3);
            const bool  inv = (C.e2 > 0.5f && C.e2 < 1.5f);
            for (int b = 0; b < 4; ++b)
            {
                const float eDb = 20.0f * std::log10 (env_[b] + 1e-12f) + kDetCal;
                float u = clampf ((eDb - Th) / Wd + 0.5f, 0.0f, 1.0f);
                u = u * u * (3.0f - 2.0f * u);
                const bool up = (g[b] > 0.0f);
                float r = (up != inv) ? (1.0f - u) : u;
                ride_[b] = r; g[b] *= r;
            }
            // detector retune (band-pass follows the band, ballistics follow the band's fc)
            for (int b = 0; b < 4; ++b)
            {
                const float fc = clampf (f[b], 20.0f, 0.45f * fs_);
                const float gg = std::tan (3.14159265f * fc / fs_);
                const float kk = 1.0f / std::max (0.3f, 0.8f * (T.qBase[b] * C.qm[b]));
                sA1_[b] = 1.0f / (1.0f + gg * (gg + kk));
                sA2_[b] = gg * sA1_[b];
                sA3_[b] = gg * sA2_[b];
                const float atk = std::max (0.002f, 2.0f / fc) * std::max (0.05f, C.e1);
                float rel = 8.0f * atk; if (C.e2 > 2.5f) rel *= 10.0f;      // Peak Keep
                dAtk_[b] = 1.0f - std::exp (-1.0f / (atk * fs_));
                dRel_[b] = 1.0f - std::exp (-1.0f / (rel * fs_));
            }
        }
        else for (int b = 0; b < 4; ++b) ride_[b] = 1.0f;

        // ── the Type's Q LAW. Q is never a knob; this is the whole chassis solve. ──
        for (int b = 0; b < 4; ++b) q[b] = T.qBase[b] * C.qm[b];
        switch (T.law)
        {
            case LawConstant: { const float w = widthMul (shape);
                                for (int b = 0; b < 4; ++b) q[b] *= w; } break;
            case LawBritish:  { const float sq = 0.5f + shape * 4.0f * C.e1;
                                for (int b = 0; b < 4; ++b)
                                    if (kd[b] != 0) q[b] = sq * C.qm[b]; } break;
            case LawProportional:
            {   const float ex = clampf (0.4f + shape * 2.6f + C.e1, 0.15f, 6.0f);
                for (int b = 0; b < 4; ++b)
                {
                    const bool shelf = (kd[b] != 0);
                    if (shelf && C.e3 < 0.5f) continue;             // shelves stay constant-S
                    const float x = std::min (std::fabs (g[b]) / kBandDbSpan, 2.0f);
                    const bool  apply = (C.e2 < 0.5f)
                                     || (C.e2 > 0.5f && C.e2 < 1.5f && g[b] > 0.0f)
                                     || (C.e2 > 1.5f && g[b] < 0.0f);
                    const float base = shelf ? 0.70f : 0.55f;
                    const float span = shelf ? 2.50f : 13.0f;
                    q[b] = (apply ? base + span * std::pow (x, ex) : base) * C.qm[b];
                }
            } break;
            case LawPassive:  q[2] = 0.70f * C.qm[2]; break;        // the EQP bandwidth knob, wide
            case LawChisel:
            {   const float ring = 2.0f * std::pow (32.0f, shape);  // 2 .. 64
                for (int b = 0; b < 4; ++b)
                    q[b] = ring * (1.0f + C.e2 * std::min (std::fabs (g[b]), 60.0f) / 12.0f) * C.qm[b];
            } break;
            default: break;
        }
        for (int b = 0; b < 4; ++b)
        {
            if (g[b] < 0.0f) q[b] *= C.cutQ;                        // boosts wide, cuts narrow
            q[b] = clampf (q[b], kQMin, kQMax);
            q[b] = (float) usableQ (kd[b], (double) f[b], (double) q[b], (double) g[b], (double) fs_);
        }

        // ── build the stages ──
        for (int b = 0; b < 4; ++b)
        {
            Stage& s0 = st_[b * 2]; Stage& s1 = st_[b * 2 + 1];
            const bool live = std::fabs (g[b]) > 1e-4f;
            s0.on = live; s0.kind = kd[b]; s0.f = f[b]; s0.q = q[b]; s0.g = g[b];
            s1.on = false;

            if (live && steep[b] && (kd[b] == 1 || kd[b] == 2))
            {   // 24 dB/oct: two 2-pole shelves of half the gain, Butterworth Q pair
                s0.g = g[b] * 0.5f; s0.q = clampf (q[b] * 0.765f, kQMin, kQMax);
                s1.on = true; s1.kind = kd[b]; s1.f = f[b]; s1.g = g[b] * 0.5f;
                s1.q = clampf (q[b] * 1.848f, kQMin, kQMax);
            }
            else if (T.law == LawPassive && b == 0 && g[0] > 0.01f)
            {   // 🔑 THE PULTEC TRICK, and it is ONE knob: boosting the Low shelf also
                //   engages an attenuation shelf ABOVE it, so you get the hump AND the
                //   scoop that no ideal parametric can make.
                const float dip = shape * 1.2f;
                s1.on = true; s1.kind = (C.e2 > 1.5f ? 0 : 1);
                s1.f = clampf (f[0] * C.e1, 20.0f, 2000.0f);
                s1.q = (C.e2 > 1.5f ? 1.0f : 0.9f);
                s1.g = -0.7f * g[0] * dip;
            }
            else if (T.law == LawPassive && b == 3 && (C.e2 > 0.5f && C.e2 < 1.5f) && live)
            {   // `Both Ends`: the EQP high boost + high attenuator engaged together
                s1.on = true; s1.kind = 2;
                s1.f = clampf (f[3] / C.e1, 1000.0f, 40000.0f);
                s1.q = 0.8f; s1.g = -0.6f * g[3] * shape * 1.2f;
            }
            else if (T.law == LawOpen && b == 3 && live && shape > 0.001f)
            {   // `Silk`: the sheen on the sheen — a second matched shelf above Reach
                s1.on = true; s1.kind = kd[3];
                s1.f = clampf (f[3] * std::pow (2.0f, C.e1), 1000.0f, 60000.0f);
                s1.q = q[3]; s1.g = g[3] * 0.8f * shape;
            }
            s0.g = clampf (s0.g, -96.0f, kGainCeil);
            s1.g = clampf (s1.g, -96.0f, kGainCeil);
            if (std::fabs (s1.g) < 1e-4f) s1.on = false;

            viz_.nodeHz[b] = f[b];
            viz_.nodeDb[b] = g[b];
        }

        // ── Tilt: Baxandall's seesaw, one 1-pole high shelf of 2t dB plus a -t dB trim.
        //    6 dB/oct, so the whole spectrum leans instead of stepping.
        // ── fb438 — THE FREE BELLS (stages kFree0..+3). Full-range constant-Q bells: surgical by
        //    design (the Type's character lives in the four role bands; a free band is a scalpel).
        //    Surgical's Trait (width) still applies so the one knob that IS Q keeps its meaning; Amount
        //    scales them like every other gain; an OFF or 0 dB band is an OFF stage = bit-exact through.
        for (int k = 0; k < kNumFree; ++k)
        {
            Stage& S = st_[kFree0 + k];
            const float tF = sm_[11 + 2 * k], tG = sm_[12 + 2 * k];
            const float fHz = clampf (20.0f * std::pow (1000.0f, tF), 20.0f, 0.45f * fs_);
            const float gDb = clampf ((tG * 2.0f - 1.0f) * kBandDbSpan * amount, -96.0f, kGainCeil);
            const bool  live = xOn_[k] && std::fabs (gDb) > 1e-4f;
            float qf = clampf ((T.law == LawConstant ? widthMul (shape) : 1.0f), kQMin, kQMax);
            qf = (float) usableQ (0, (double) fHz, (double) qf, (double) gDb, (double) fs_);
            S.on = live; S.kind = 0; S.f = fHz; S.q = qf; S.g = gDb;
            viz_.nodeHz[kNumBands + k] = fHz;
            viz_.nodeDb[kNumBands + k] = xOn_[k] ? gDb : 0.0f;
            viz_.nodeOn[kNumBands + k] = xOn_[k];
        }

        slantDb_ = (sm_[8] * 2.0f - 1.0f) * kSlantDbSpan * amount;
        Stage& ts = st_[kSlant];
        ts.on = std::fabs (slantDb_) > 1e-4f;
        ts.kind = 5; ts.f = C.pivotHz; ts.q = 0.707f; ts.g = slantDb_;
    }

    // ═════════════════════════════════════════════════════════════════════════
    void designAll (bool snap) noexcept
    {
        const float inv = 1.0f / (float) kDesignBlk;
        nAct_ = 0;
        for (int s = 0; s < kNumStages; ++s)
        {
            Stage& S = st_[s];
            if (! S.on)
            {
                if (S.lg != 1e9f) { S.tb0 = 1.0; S.tb1 = S.tb2 = S.ta1 = S.ta2 = 0.0;
                                    S.lf = -1.0f; S.lq = -1.0f; S.lg = 1e9f; }
            }
            else if (S.f != S.lf || S.q != S.lq || S.g != S.lg)
            {
                const Coeffs c = (S.kind >= 3) ? designOnePole (S.kind, (double) S.f, (double) S.g, (double) fs_)
                                               : designBand    (S.kind, (double) S.f, (double) S.q, (double) S.g, (double) fs_);
                S.tb0 = c.b0; S.tb1 = c.b1; S.tb2 = c.b2; S.ta1 = c.a1; S.ta2 = c.a2;
                S.lf = S.f; S.lq = S.q; S.lg = S.g;
            }
            if (snap) { S.cb0 = S.tb0; S.cb1 = S.tb1; S.cb2 = S.tb2; S.ca1 = S.ta1; S.ca2 = S.ta2;
                        S.ib0 = S.ib1 = S.ib2 = S.ia1 = S.ia2 = 0.0f; }
            else      { const double dinv = (double) inv;
                        S.ib0 = (S.tb0 - S.cb0) * dinv; S.ib1 = (S.tb1 - S.cb1) * dinv;
                        S.ib2 = (S.tb2 - S.cb2) * dinv; S.ia1 = (S.ta1 - S.ca1) * dinv;
                        S.ia2 = (S.ta2 - S.ca2) * dinv; }

            // denormal flush on the recirculating state (assume no ScopedNoDenormals)
            for (int c = 0; c < 2; ++c)
            // the state is DOUBLE, so there are no denormals to chase here (they start at
            // 1e-308). This threshold exists for a different reason: a ring must reach EXACT
            // zero in finite time. It is far below anything downstream can express,
            // and it turns
            // "asymptotically approaching silence" into "silent". 1e-18 is -360 dBFS.
#ifndef EQ_MUT_NO_DENORM
            { if (S.z1[c] < 1e-18 && S.z1[c] > -1e-18) S.z1[c] = 0.0;
              if (S.z2[c] < 1e-18 && S.z2[c] > -1e-18) S.z2[c] = 0.0; }
#else
            { (void) c; }                                  // MUTATION: the flush is gone
#endif
            if (! std::isfinite (S.z1[0]) || ! std::isfinite (S.z2[0])
             || ! std::isfinite (S.z1[1]) || ! std::isfinite (S.z2[1]))
            { S.z1[0] = S.z2[0] = S.z1[1] = S.z2[1] = 0.0; }   // NaN in => NaN contained

            const bool idle = (S.cb0 == 1.0 && S.cb1 == 0.0 && S.cb2 == 0.0
                            && S.ca1 == 0.0 && S.ca2 == 0.0
                            && S.z1[0] == 0.0 && S.z2[0] == 0.0
                            && S.z1[1] == 0.0 && S.z2[1] == 0.0);
            if (! S.on && idle) continue;                // provably a no-op: skip it entirely
            act_[nAct_++] = (s == kSlant && nAct_ == 0) ? s : s;
        }
        // the slant must run FIRST (it is the baseline the bands sit on) — reorder in place
        for (int i = 1; i < nAct_; ++i) if (act_[i] == kSlant)
        { for (int j = i; j > 0; --j) act_[j] = act_[j - 1]; act_[0] = kSlant; break; }

    }

    void controlStep() noexcept
    {
        if (pendType_ >= 0 && dip_ < 0.05f)
        {   // at the dip floor: adopt, re-seat from CURRENT knob values, SNAP the design.
            // (fb345: a flush is silent, a ramp is not — never let the recovery ride a
            //  moving curve, or the fade-in overshoots.)
            type_ = pendType_; char_ = pendChar_; focus_ = pendFocus_;
            pendType_ = pendChar_ = pendFocus_ = -1;
            flushFocusStates();
            for (int i = 0; i < kNumSm; ++i) sm_[i] = tg_[i];
            resolve(); designAll (true);
            return;
        }
        for (int i = 0; i < 11; ++i) sm_[i] += kSm_[i] * (tg_[i] - sm_[i]);
        resolve();
#ifdef EQ_MUT_NO_SMOOTH
        designAll (true);            // MUTATION: coefficients SNAP; no per-sample glide
#else
        designAll (false);
#endif

        curveCtr_ -= kDesignBlk;
        if (curveCtr_ <= 0) { curveCtr_ = curveEvery_; pushCurve(); }
    }

    // The drawn curve is evaluated from the LIVE RAMPED COEFFICIENTS, never from the knob
    // values — so the display physically cannot disagree with the audio.
    void pushCurve() noexcept
    {
        for (int i = 0; i < kCurveBins; ++i)
        {
            const double phi = binPhi_[i];
            float acc = 0.0f;
            for (int s = 0; s < nAct_; ++s)
            {
                const Stage& S = st_[act_[s]];
                Coeffs c; c.b0 = S.cb0; c.b1 = S.cb1; c.b2 = S.cb2; c.a1 = S.ca1; c.a2 = S.ca2;
                acc += (float) (10.0 * std::log10 (std::max (1e-30, magSq (c, phi))));
            }
            viz_.curve[i] = clampf (acc, -120.0f, 80.0f);
        }
    }

    // ── the one measured constant in this file. A band-pass detector on a broadband
    //    program reads far below the program's full-band level, so anchoring the Dynamic
    //    threshold at -26 dBFS without compensating would mean the bands NEVER move (the
    //    26 dB deficit, one level down). eq_cert §G prints the raw band envelope for pink
    //    noise at -26 dBFS and this constant is set FROM that measurement.
    static constexpr float kDetCal = 14.0f;

    // state ------------------------------------------------------------------
    Params p_;
    Viz    viz_;
    float  fs_ = 48000.0f;
    int    type_ = 0, char_ = 0, focus_ = 0;
    int    pendType_ = -1, pendChar_ = -1, pendFocus_ = -1;
    bool   seeded_ = false;

    float  sm_[kNumSm] {}, tg_[kNumSm] {}, kSm_[kNumSm] {};
    bool   xOn_[kNumFree] {};                                // fb438 — the free bells' ON flags
    float  mixSm_ = 1.0f, mixTg_ = 1.0f, mixK_ = 0.01f;
    float  slantDb_ = 0.0f;
    float  dip_ = 1.0f, dipDn_ = 0.1f, dipUp_ = 0.03f;
    float  lvlSm_ = 0.0f, lvlK_ = 0.01f;
    int    ctr_ = 0, curveCtr_ = 0, curveEvery_ = 800;

    Stage  st_[kNumStages];
    int    act_[kNumStages] {}; int nAct_ = 0;

    double binPhi_[kCurveBins] {};

    // Dynamic detectors
    float  d1_[4] {}, d2_[4] {}, env_[4] {}, ride_[4] { 1,1,1,1 };
    float  sA1_[4] { 0.5f,0.5f,0.5f,0.5f }, sA2_[4] {}, sA3_[4] {};
    float  dAtk_[4] { 0.01f,0.01f,0.01f,0.01f }, dRel_[4] { 0.001f,0.001f,0.001f,0.001f };
    float  envW_ = 0.0f;
};

} // namespace tw
