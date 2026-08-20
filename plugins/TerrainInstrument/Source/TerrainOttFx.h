#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TerrainOttFx.h — fb420+. ONE instance of the FX-rack OTT device (chain kind 12).
// Contract: Design/fx4/CONTRACT.md §2 (locked interface) · Bible: Design/OTT-BUILD-BIBLE.md
// Shared core: DynamicsCore.h (with TerrainCompressFx.h) · Harness: dynamics_cert.cpp
//
// ── WHAT THIS IS, IN ONE PARAGRAPH ──────────────────────────────────────────
// Three bands. Each band runs TWO gain computers AT ONCE: a downward one above an upper
// threshold and an UPWARD one below a lower threshold. The band's dynamic range is squeezed
// from both ends into a narrow window, per frequency band, with fast per-band ballistics. The
// audible result is that every band's output level is nearly constant regardless of input —
// a "sheet" of that band, dense, forward, loud-feeling at equal RMS. Multiply that across three
// bands with different thresholds and ballistics and you have the sound the genre is built on.
//
// ── 🔑 WHERE THE "AIR" COMES FROM — the mechanism, with numbers ─────────────
// A 110 Hz saw at the Terrain bus puts very little ABSOLUTE energy above 2.5 kHz: harmonic 23
// and up, each ≥ 27 dB below the fundamental, so the high band sits at −45…−50 dBFS on bright
// material and −60…−80 on a dark pad. Put an upward computer there at T_up = −66 dBFS, slope
// 0.8, and a dark pad's high band collects 0.8·(−66 −(−80)) = +11.2 dB OF PURE TOP, applied only
// while the band is quiet, released in 15 ms — the shimmer breathes into every gap. Meanwhile
// the ∞:1 downward ceiling at −56 dBFS pins bright material's HF at a constant level.
// So the top end is NOT an EQ shelf. It is a program-dependent shelf whose gain GROWS AS THE
// SOURCE GETS DARKER, plus a brick wall that keeps HF always at the same level. An EQ can only
// trade one for the other. That difference is measurable and the harness measures it.
//
// ── ⚠️ THE MIX LAW (bible §4.3) — a real trap, gated, not assumed ───────────
// Σ bands = AP2(f_lo)·AP2(f_hi)·x EVEN AT ZERO COMPRESSION, because LR4 recombination is an
// ALLPASS, not the identity. Blend that against the RAW dry at Mix 50 % and you get comb
// notches at both crossovers — the documented flaw every OTT clone that skipped this grew a
// complaint thread about. The fix costs 4 SVF ticks: the dry path goes through THE SAME
// AP2(f_lo)→AP2(f_hi) cascade. Then wet and dry differ by GAIN ALONE and Mix is clean at every
// setting. The harness proves it as a perfect-reconstruction NULL, not as a flatness eyeball.
//
// ── ⚠️ THE FLOOR GATE (bible §4.6) — a stability requirement, not polish ────
// With no input the upward computer pins at its cap and amplifies the noise floor forever. So
// below −78 dBFS the upward gain smoothstep-ramps back to unity over 12 dB. Silence in, unity
// gain, silence out. NOT a comparator — a decaying tail crossing a hard threshold gate-flutters.
//
// ── THE CEILING (R11) ───────────────────────────────────────────────────────
// Ableton's fixed OTT preset is the FLOOR of this device's Amount range, not its ceiling: it
// lands at Amount 0.5. Above 0.5 the two thresholds close on each other until, at Amount 1.0,
// T_up MEETS T_dn — the jaws weld shut, both slopes go to ∞:1 / 20:1, and the band's output is
// a CONSTANT level regardless of input. A 48 dB staircase comes out inside a couple of dB and a
// −70 dBFS noise bed is lifted into a wall. The floor gate still holds below −78, so true
// silence is still silent. Both ends are gated in the harness.
//
// Zero latency · zero lookahead · no oversampling, ever · feed-forward only (no loop exists).
// No allocation · no locks · no std::function reachable from processStereo. Denormal-flushed.
// ─────────────────────────────────────────────────────────────────────────────

#include "DynamicsCore.h"

namespace tw {

class TerrainOttFx
{
public:
    // ── identity ─────────────────────────────────────────────────────────────
    static constexpr int kNumTypes  = 8;
    static constexpr int kNumChars  = 8;
    static constexpr int kNumStereo = 3;     // the back dropdown-2 axis
    static constexpr int kBands     = 3;     // the Viz is locked at 3 (contract §2)

    enum TypeId { T_OVERTOP = 0, T_GENTLE, T_HEAVY, T_SHEEN, T_BASSSAFE, T_SURGE, T_TWOBAND, T_STAGGER };

    static const char* const* typeNames() noexcept
    {
        static const char* const N[kNumTypes] =
            { "Over Top", "Gentle", "Heavy", "Sheen", "Bass Safe", "Surge", "Two Band", "Stagger" };
        return N;
    }

    /** The back dropdown-2 axis. NOT `Type` (R6). Each entry is a different DETECTION TOPOLOGY —
     *  one shared clamp, two independent clamps, or two clamps on a rotated basis. Physics. */
    static const char* const* stereoNames() noexcept
    {
        static const char* const N[kNumStereo] = { "Linked", "Free Pair", "Mid-Side" };
        return N;
    }

    static const char* const* charNames (int type) noexcept
    {
        static const char* const C[kNumTypes][kNumChars] = {
        /* Over Top */ { "Straight Up", "Sharp Ears",  "Long Ears",   "Wide Corner",
                         "One Detector","Slow Low",    "Twice Deep",  "Full Crest" },
        /* Gentle   */ { "Round Corner","Slow Hands",  "Long Window", "Half Slopes",
                         "Long Tail",   "Soft Top",    "Even Bands",  "Barely There" },
        /* Heavy    */ { "Welded Shut", "Band Clip",   "No Clip",     "Deeper Jaws",
                         "Fast Grind",  "Peak Grab",   "Wall Ears",   "Total Squeeze" },
        /* Sheen    */ { "Top Sheet",   "Higher Split","Lower Split", "Glass Ceiling",
                         "Slow Shimmer","Fast Shimmer","Dark Source", "Sheen Wall" },
        /* Bass Safe*/ { "Anchor Low",  "Mono Low",    "Slower Low",  "Low Ceiling",
                         "Reese Guard", "Free Low",    "Wide Corner Low","Tight Low" },
        /* Surge    */ { "Tail Riser",  "Deep Riser",  "Tied Rise",   "Fast Riser",
                         "Capped Riser","Top Riser",   "Mean Ears",   "Riser Wall" },
        /* Two Band */ { "Body Sparkle","Low Split",   "High Split",  "Hard Body",
                         "Soft Body",   "Sparkle Wall","Slow Pair",   "Fast Pair" },
        /* Stagger  */ { "Time Spread", "Wider Spread","Narrow Spread","Reverse Spread",
                         "Slow Anchor", "Fast Top",    "Deep Spread", "Spread Wall" } };
        const int t = dyn::clampi (type, 0, kNumTypes - 1);
        return C[t];
    }

    // ═════ THE LABELS. THIS HEADER IS THE SINGLE SOURCE OF TRUTH (FIXES.md §3) ═══════════
    static const char* deviceName() noexcept { return "OTT"; }
    /** FRONT: three heroes, then Mix. Order matches Params::amount/speed/topLift/mix. */
    static const char* const* frontNames() noexcept
    { static const char* const N[4] = { "Amount", "Chase", "Top Lift", "Mix" }; return N; }
    /** BACK: the 8 knobs, 4x2, in b1..b8 order. */
    static const char* const* backNames() noexcept
    { static const char* const N[8] = { "Low Cross", "High Cross", "Raise", "Press",
                                        "Grip", "Bass", "Mids", "Treble" }; return N; }
    /** The two back dropdowns: [0] = Character (chassis), [1] = the SECOND AXIS. Never `Type`. */
    static const char* const* dropdownNames() noexcept
    { static const char* const N[2] = { "Character", "Stereo" }; return N; }
    /** The ONE front pill. */
    static const char* pillName() noexcept { return "Crest"; }
    static constexpr int kNumFront = 4, kNumBack = 8;

    static_assert (kNumTypes > 0 && kNumChars == 8, "roster/table must move together");

    // ── the locked Params (CONTRACT §2). Front three named per device:
    //        amount = Amount · speed = Chase · topLift = Top Lift
    //    Back eight:  b1 Low Cross · b2 High Cross · b3 Raise · b4 Press
    //                 b5 Grip      · b6 Bass       · b7 Mids  · b8 Treble
    //    axis        = the SECOND back dropdown, `Stereo` (0..2, see stereoNames()).
    struct Params
    {
        int   type = 0, character = 0;
        int   axis = 0;                                          // Stereo
        float amount = 0.50f, speed = 0.50f, topLift = 0.25f;    // FRONT 3
        float mix    = 1.0f;                                     // 1.0 = FULLY WET
        float b1 = 0.4689f, b2 = 0.4406f, b3 = 0.667f, b4 = 0.667f,
              b5 = 0.5f,    b6 = 0.5f,    b7 = 0.5f,   b8 = 0.5f;
        bool  tempoSync = false; double bpm = 120.0;   // unused: ballistics are not musical time
        // The ONE front pill, `Crest` (RENAMES.md: the EQ's `Bite`/`Bite Hz` band grammar owns
        // that word). When a transient arrives, hold the UPWARD computer at unity for 10 ms so
        // attacks keep their crest instead of being pre-inflated by the lift that was riding the
        // gap before them. Default OFF, additive, safe if never wired.
        bool  crest = false;
    };

    struct Viz
    {
        float grDb[kBands] {};      // SIGNED per band: +ve = downward reduction, −ve = upward LIFT
        float xoverHz[2] {};        // the live crossovers (0 = band unused, i.e. Two Band)
        float bandDb[kBands] {};    // per-band level, dBFS
        float lvl = 0.0f;           // 0..1 output level for the idle-dim / playing-bright law
    };

    // ═════════════════════════════════════════════════════════════════════════
    void prepare (double sampleRate, int /*maxBlock*/) noexcept
    {
        fs_ = (sampleRate > 8000.0) ? (float) sampleRate : 48000.0f;
        lvlA_ = dyn::coefTau (0.030f, fs_);
        gXlo_.setTau (0.030f, fs_); gXhi_.setTau (0.030f, fs_); gClip_.setTau (0.020f, fs_);
        gMs_.setTau (0.020f, fs_);
        gMix_.setTau (0.010f, fs_);
        aGl_   = dyn::coefTau (0.015f, fs_);      // every threshold/slope/makeup glide
        // fb431 — 1.5 s. Slow on purpose: this must read SECTION level, never programme
        // dynamics. A fast tracker would follow a staircase down and quietly undo the very
        // compression the staircase is there to measure.
        // 10 ms LINEAR down / 40 ms back. 4 ms was the bible's number and it is too fast: a
        // linear fade to zero in 4 ms IS 25 % of the wet removed inside the first millisecond,
        // which this harness reads as 1.94 dB/ms all on its own. The dry rides the same ramp.
        dipDn_  = 1.0f / (fs_ * 0.010f);
        dipUpS_ = 1.0f / (fs_ * 0.040f);
        // The same transition slew limit COMPRESS uses: for 40 ms after a Type / Character /
        // Stereo change the per-band gain may move no faster than 1.5 dB/ms, and at no other
        // time. `Stagger`'s `Wider Spread` puts the low band's release at 24 SECONDS; leaving it
        // for a normal spread let a crawling envelope snap to target — 8.01 dB inside one
        // millisecond, measured. A follower coefficient is not a smoother.
        slewPS_ = 1.5f / (fs_ * 0.001f);
        xfLen_  = (int) (fs_ * 0.040f);
        xfN_    = 0;
        bAtk_ = dyn::coefTau (0.003f, fs_);      // Bite transient detector, MoogDelay ducker grammar
        bRel_ = dyn::coefTau (0.120f, fs_);
        biteHoldN_ = (int) (fs_ * 0.010f);
        biteRelA_  = dyn::coefTau (0.005f, fs_);
        vizEvery_  = (int) (fs_ / 60.0f); if (vizEvery_ < 32) vizEvery_ = 32;
        primed_ = false;
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            splitLo_[c].reset(); splitHi_[c].reset();
            alignLow_[c].reset(); dryLo_[c].reset(); dryHi_[c].reset();
            for (int b = 0; b < kBands; ++b) { envDn_[c][b] = 0.0f; envUp_[c][b] = 0.0f; pre_[c][b].reset(); gDnPrev_[c][b] = 0.0f; grMem_[c][b] = 0.0f; gdbZ_[c][b] = 0.0f; }
            bf_[c] = 0.0f; bs_[c] = 0.0f; biteCnt_[c] = 0; biteG_[c] = 1.0f;
        }
        lvlZ_ = 0.0f; vizCtr_ = 0; dip_ = 1.0f; dipDir_ = 0; pendOn_ = false; xfN_ = 0;
        dryX_ = (nBands_ == 3) ? 1.0f : 0.0f;
        viz_ = Viz();
    }

    // ═════ per BLOCK ═════════════════════════════════════════════════════════
    /** 🚨 THE TREE SWAP (FIXES.md §1 OTT 1). A 3-band tree and a 2-band tree are different
     *  filter graphs with different states. The first draft set `dip_ = 0.0f` — an INSTANT
     *  mute of the wet path, i.e. the exact discontinuity the dip was written to prevent; the
     *  comment above it said "4 ms down / 30 ms back" and the code did neither. It was invisible
     *  because the click gate divided by the engine's own t=0 start-up burst.
     *  Now: a band-count change is DEFERRED. The wet path ramps LINEARLY to zero over 4 ms, the
     *  new configuration is applied at the bottom (arithmetic only — no allocation, ~50 flops,
     *  once per user action), and the wet ramps back over 30 ms. The DRY path's allpass cascade
     *  crossfades over the same 30 ms, so Mix < 1 does not step either. */
    void setParams (const Params& pin) noexcept
    {
        const int reqBands = typeSpec (dyn::clampi (pin.type, 0, kNumTypes - 1)).nBands;
        if (primed_ && (pin.type != prevType_ || pin.character != prevChar_ || pin.axis != prevAxis_))
            xfN_ = xfLen_;
        prevType_ = pin.type; prevChar_ = pin.character; prevAxis_ = pin.axis;
        if (primed_ && reqBands != nBands_)
        { pend_ = pin; pendOn_ = true; dipDir_ = -1; return; }
        applyParams (pin);
    }

    void applyParams (const Params& pin) noexcept
    {
        Params p = pin;
        p.type      = dyn::clampi (p.type,      0, kNumTypes - 1);
        p.character = dyn::clampi (p.character, 0, kNumChars - 1);
        p.axis      = dyn::clampi (p.axis,      0, kNumStereo - 1);
        pr_ = p;

        const TypeSpec& ts = typeSpec (p.type);
        const CharSpec& cs = charSpec (p.type, p.character);

        nBands_  = ts.nBands;
        stereo_  = p.axis;
        det_     = (cs.det >= 0) ? cs.det : ts.det;
        bandLink_ = cs.bandLink != 0;
        lowMono_  = (cs.lowMono != 0) ? (cs.lowMono > 0) : (ts.lowMono != 0);
        upHold_   = (cs.upHold != 0) || p.crest;

        // ── CROSSOVERS. Clamped, and f_hi ≥ 4·f_lo enforced: below two octaves of separation the
        //    mid band thins to a phase sliver and the band trims stop meaning anything.
        float xlo = dyn::expMap (dyn::clampf (p.b1, 0.0f, 1.0f),
                                 (nBands_ == 2 ? 150.0f : 30.0f), (nBands_ == 2 ? 2000.0f : 300.0f))
                    * ts.xloMul * cs.xloMul;
        float xhi = dyn::expMap (dyn::clampf (p.b2, 0.0f, 1.0f), 1000.0f, 8000.0f) * ts.xhiMul * cs.xhiMul;
        // Top Lift moves the high split DOWN so more spectrum counts as "high" (that is a physical
        // change, not a tone tweak — a wider band means more content sees the upward computer).
        const float topA = dyn::clampf (p.topLift, 0.0f, 1.0f);
        xhi *= (1.0f - 0.25f * topA);
        xlo = dyn::clampf (xlo, 25.0f, 0.20f * fs_);
        xhi = dyn::clampf (xhi, 4.0f * xlo, 0.40f * fs_);
        xloTgt_ = xlo; xhiTgt_ = xhi;

        // ── CHASE. ONE knob, all six followers — how hard they chase the signal.
        //    0 = ×20 slower (breathing walls),
        //    1 = ×0.05 (the gain tracks the waveform itself: the deliberate destruction decade),
        //    0.5 = the Type's own calibration. 400:1 of travel, log, no dead zone anywhere.
        const float spd = dyn::clampf (p.speed, 0.0f, 1.0f);
        const float tMul = std::pow (10.0f, (0.5f - spd) * 2.6f);

        // ── AMOUNT. ≤ 0.5 scales the slopes off zero; > 0.5 drives BOTH slopes to their maxima
        //    AND closes the two thresholds on each other until they MEET. That meeting is the
        //    ceiling: above and below one single level, everything is pinned to it.
        const float amt = dyn::clampf (p.amount, 0.0f, 1.0f);
        const float lo01 = (amt <= 0.5f) ? std::pow (amt * 2.0f, 1.2f) : 1.0f;
        const float u    = (amt >  0.5f) ? (amt - 0.5f) * 2.0f : 0.0f;

        const float raise = dyn::clampf (p.b3, 0.0f, 1.0f) * 1.5f;   // Raise: 0..150 % of the Type
        const float press = dyn::clampf (p.b4, 0.0f, 1.0f) * 1.5f;   // Press: 0..150 %
        const float grip  = (dyn::clampf (p.b5, 0.0f, 1.0f) - 0.5f) * 36.0f;   // ±18 dB
        const float trim[3] = { (dyn::clampf (p.b6, 0.0f, 1.0f) - 0.5f) * 24.0f,
                                (dyn::clampf (p.b7, 0.0f, 1.0f) - 0.5f) * 24.0f,
                                (dyn::clampf (p.b8, 0.0f, 1.0f) - 0.5f) * 24.0f };

        for (int b = 0; b < kBands; ++b)
        {
            float mkTop = 0.0f;                    // fb431 — the top half's EXTRA, gated apart
            // 🔴🔴 fb432 — THE UPWARD COMPUTER HAD NEVER RUN. This is the bug behind "it
            //    sounds NOTHING like a real OTT". Upward compression lifts what is BELOW its
            //    threshold, and these thresholds were placed BELOW the programme, so on real
            //    material every band sat ABOVE T_up and the upward lane was idle — measured on
            //    the engine's own meters, a bus-level chord, Amount 1.0:
            //         band   level   T_up    T_dn    grDb
            //         low    -27.1   -45.0   -46.0  +18.93
            //         mid    -15.0   -37.0   -37.0  +22.00
            //         high   -26.7   -42.5   -43.5  +16.81
            //    grDb is SIGNED and every one of them is POSITIVE: pure downward reduction on
            //    all three bands. What shipped was a downward multiband compressor wearing the
            //    name. The header's own voicing paragraph reasons from "the high band sits at
            //    -45..-50 dBFS"; the engine measures it at -26.7, so the whole table was placed
            //    against an assumption that was ~20 dB wrong, and the threshold must sit ABOVE
            //    the band for the lift to have anything to pull against.
            //    Swept, high-band gain / bands lifting at Amount 1.0:
            //       +0 -> +2.42 dB, 0 of 3   ·  +10 -> +12.42, 0 of 3  ·  +20 -> +22.21, 2 of 3
            //       +26 -> +27.85, 3 of 3    ·  +32 -> +33.55, 3 of 3
            //    +26 is where all three bands lift. The per-Type table's INTERNAL relationships
            //    are untouched — this corrects where the whole window sits, nothing else.
            // 🔴🔴 fb432 (2) — AND THE BANDS HAVE TO BE DRIVEN TO A COMMON LEVEL. Each band's
            //    threshold was individually tuned to where THAT band already sits, which
            //    PRESERVES the spectral balance — the exact opposite of what an OTT does. The
            //    band-to-band differences are compressed toward the Type's own mean, so every
            //    band is hauled toward one target and the spectrum flattens into a sheet.
            //    Measured on a bus-level chord, dry band spread 13.1 dB:
            //      per-band thresholds (shipped)  spread 9.6, high band only +3 dB over low
            //      common window                  spread 7.8, high band +24.8 vs mid +13.7
            //    The Type still owns WHERE its window sits; it no longer owns the tilt.
            const float um = (ts.tup[0] + ts.tup[1] + ts.tup[2]) * (1.0f / 3.0f);
            const float dm = (ts.tdn[0] + ts.tdn[1] + ts.tdn[2]) * (1.0f / 3.0f);
            float tup = um + 34.0f + (ts.tup[b] - um) * 0.35f + cs.tupOff - grip;
            float tdn = dm + 34.0f + (ts.tdn[b] - dm) * 0.35f + cs.tdnOff - grip;
            float sdn = ts.sdn[b] * cs.dnMul * press * lo01;
            float sup = ts.sup[b] * cs.upMul * raise * lo01;
            // fb432 — the makeup was carrying the whole effect while the lift sat idle;
            //    with the lift doing its job it is a trim again, not the mechanism.
            float mk  = ts.mk[b] * lo01 * 0.15f;

            // ── the high band is where Top Lift lives: threshold RAISED toward the program,
            //    slope steepened, makeup added. §2.4's whole mechanism, on one knob.
            if (b == nBands_ - 1)
            {
                tdn += 10.0f * topA;
                tup += 14.0f * topA;
                sup *= (1.0f + 1.2f * topA);
                mk  += 4.0f * topA;
                capDbTop_ = 24.0f + 12.0f * topA;    // and the cap grows, so the knob stays alive
            }

            if (u > 0.0f)
            {
                sdn = dyn::lerpf (sdn, 1.00f, u);
                sup = dyn::lerpf (sup, 0.95f, u);
                tdn -= 6.0f * u;                       // the wall drops a little...
                // ⚠️ fb431 — AND IT STAYS 6. Dropping it to 3 makes the knob monotone at a
                //    much lower makeup, but the wall IS the R11 property: at 3 the 46 dB
                //    staircase gate went red (10.37 % surviving against a <= 10 % bar). The
                //    compression stays; the makeup is what was wrong.
                tup  = dyn::lerpf (tup, tdn, u);       // ...and the floor rises to MEET it
                // 🔴 fb431 — THE TOP HALF OF `Amount` RAN BACKWARDS. The line above drops the
                //    wall by 6 dB and this one used to hand back 3, calling itself
                //    "half-compensated: denser, a touch louder". Measured, it was QUIETER: the
                //    knob peaked at Amount 0.6 (+3.68 dB, +5.95 dB of high band) and fell to
                //    +0.09 / +3.42 at 1.0 — turning OTT UP took the air away, which with the
                //    level bug above is the whole of "it doesn't do anything". The wall drop is
                //    only half of it; the slopes also go to inf:1 over this range and pin the
                //    output at the wall instead of letting it follow. Swept for a MONOTONE
                //    knob at a fixed input: 3 -> +0.09 (hump) · 6 -> +3.09 (still falling)
                //    · 9 -> +6.09 · 12 -> +9.09 total with +12.42 dB of high band. 12 is the
                //    first value where the top of the knob is unambiguously the most extreme
                //    place on it, which is what R11 asks of a maximum.
                //    Swept for the SMALLEST value that makes the knob monotone at a fixed
                //    input (total dB at Amount 0.4/0.6/0.8/1.0):
                //      3 -> 3.39 4.28 1.99 0.09  the shipped hump
                //      6 -> 3.39 4.28 3.79 3.09  still falling
                //      7 -> 3.39 4.48 4.39 4.09  still falling
                //      8 -> 3.39 4.67 4.99 5.09  MONOTONE
                //    8 it is: the top of the knob is now the most extreme place on it, which
                //    is what R11 asks of a maximum, and the high band rises with it.
                mkTop = 8.0f * u;
            }

            tdnT_[b]  = tdn;
            tupT_[b]  = std::min (tup, tdn);           // never cross
            sdnT_[b]  = dyn::clampf (sdn, 0.0f, 1.0f);
            supT_[b]  = dyn::clampf (sup, 0.0f, 0.95f);
            mkT_[b]    = mk + trim[b];
            mkTopT_[b] = mkTop;
            capDb_[b] = (b == nBands_ - 1) ? std::max (cs.upCap, capDbTop_) : cs.upCap;

            // ballistics: base × Speed × Character, with the per-band SPREAD exponent applied
            // around the mid band (spread 0 = every band identical; 2.2 = exaggerated).
            const float aRef = ts.atk[1], rRef = ts.rel[1];
            float aMs = aRef * std::pow (ts.atk[b] / aRef, cs.spread) * cs.atkMul * tMul;
            float rMs = rRef * std::pow (ts.rel[b] / rRef, cs.spread) * cs.relMul * tMul;
            if (b == nBands_ - 1) { aMs *= cs.hiTimeMul; rMs *= cs.hiTimeMul; }
            // Vital's kMinSampleEnvelope = 5: the fastest gain slew is bounded to ~5 samples of
            // smoothing. There is NO separate gain smoother in this device class — the follower
            // IS the smoother, so this floor is the whole anti-click strategy. Do not remove it.
            const float nA = std::max (5.0f, aMs * 0.001f * fs_);
            const float nR = std::max (5.0f, rMs * 0.001f * fs_);
            aAtk_[b] = 1.0f / (nA + 1.0f);
            aRel_[b] = 1.0f / (nR + 1.0f);
            atkMs_[b] = nA * 1000.0f / fs_;
            relMs_[b] = nR * 1000.0f / fs_;
        }
        knee_    = (cs.kneeDb >= 0.0f) ? cs.kneeDb : ts.knee;
        clipHd_  = (cs.clipHead < 0.0f) ? ts.clipHead : cs.clipHead;
        lowUpOff_ = (cs.lowUp != 0) ? (cs.lowUp < 0) : (ts.lowUpOff != 0);
        hiTiltDb_ = cs.hiTilt;
        aPre_    = dyn::coefTau ((det_ == 1 ? 0.025f : 0.060f), fs_);
        deepRel_ = cs.deepRel != 0;
        aMem_    = dyn::coefTau (0.300f, fs_);
        mixTgt_  = dyn::clampf (p.mix, 0.0f, 1.0f);
        sOff_    = (stereo_ == 2) ? -6.0f : 0.0f;

        nBandsPrev_ = nBands_;

        if (!primed_)
        {
            gXlo_.snap (xloTgt_); gXhi_.snap (xhiTgt_); gMix_.snap (mixTgt_);
            for (int b = 0; b < kBands; ++b)
            { tdn_[b] = tdnT_[b]; tup_[b] = tupT_[b]; sdn_[b] = sdnT_[b];
              sup_[b] = supT_[b]; mkDb_[b] = mkT_[b]; mkTopDb_[b] = mkTopT_[b]; }
            nBandsPrev_ = nBands_; dip_ = 1.0f; dipDir_ = 0; dryX_ = (nBands_ == 3) ? 1.0f : 0.0f;
            gClip_.snap ((clipHd_ < 900.0f) ? 1.0f : 0.0f); gMs_.snap ((stereo_ == 2) ? 1.0f : 0.0f);
            prevType_ = p.type; prevChar_ = p.character; prevAxis_ = p.axis;
            primed_ = true;
            applyXover (xloTgt_, xhiTgt_);
        }
        viz_.xoverHz[0] = xloTgt_;
        viz_.xoverHz[1] = (nBands_ == 3) ? xhiTgt_ : 0.0f;
    }

    // ═════════════════════════════════════════════════════════════════════════
    void processStereo (float* L, float* R, int n) noexcept
    {
        if (n <= 0) return;

        // Crossover coefficients are recomputed at the BLOCK edge with per-sample state
        // continuity (the shipped filter law — no re-anchor needed at Q = 1/√2).
        const float xl = gXlo_.proc (xloTgt_), xh = gXhi_.proc (xhiTgt_);
        applyXover (xl, xh);

        // 🔬 fb431 — A PROGRAMME TRACKER WAS TRIED HERE AND IS THE WRONG FIX. Every
        //    threshold in this file is absolute dBFS, calibrated for the -26 dBFS Terrain bus,
        //    so feeding it hotter leaves every band above T_up, the UPWARD computer never
        //    engages, and the OTT degenerates into a downward compressor that REMOVES top end
        //    (measured on a saw chord at Amount 0.5: -32.5 dBFS in -> +10.98 dB and +10.75 dB
        //    of high band; -13.7 dBFS in -> -4.67 and -6.39). Making the window follow the
        //    programme DOES fix that, and it also destroys the thing R11 gates: a wall means
        //    the output does not depend on the input, and a window that chases the input is
        //    the definition of depending on it. Measured: "every Type walls at Amount 100"
        //    went 3 of 8 red. `Grip` (+-18 dB on both thresholds) is already the offset for
        //    this, and in the rack the input IS the bus level by construction. Left absolute.
        float grAcc[kBands] = { 0.0f, 0.0f, 0.0f }, lvAcc[kBands] = { 0.0f, 0.0f, 0.0f };
        int   accN = 0;

        for (int i = 0; i < n; ++i)
        {
            // ── the deferred tree swap, resolved per sample ──────────────────────────────
            if (dipDir_ < 0)
            {
                dip_ -= dipDn_;
                if (dip_ <= 0.0f)
                {
                    dip_ = 0.0f; dipDir_ = 1;
                    if (pendOn_) { pendOn_ = false; applyParams (pend_); applyXover (gXlo_.value(), gXhi_.value()); }
                }
            }
            else if (dip_ < 1.0f) { dip_ += dipUpS_; if (dip_ > 1.0f) { dip_ = 1.0f; dipDir_ = 0; } }
            const float dxT = (nBands_ == 3) ? 1.0f : 0.0f;
            if (dryX_ != dxT) { dryX_ += (dxT > dryX_) ? dipUpS_ : -dipUpS_;
                                dryX_ = dyn::clampf (dryX_, 0.0f, 1.0f); }
            const int NB = nBands_;
            if (xfN_ > 0) --xfN_;
            const float clipF = gClip_.proc ((clipHd_ < 900.0f) ? 1.0f : 0.0f);

            const float inL = L[i], inR = R[i];
            const float mix = gMix_.proc (mixTgt_);

            // ── the PHASE-MATCHED dry (bible §4.3). Same two allpasses the band tree imposes,
            //    so wet and dry differ by GAIN ALONE and Mix cannot comb at any setting.
            // the second allpass runs ALWAYS and is crossfaded, so a band-count change cannot
            // step the dry path's phase either (it would, at any Mix below 1.0).
            const float dL0 = dryLo_[0].ap (inL), dR0 = dryLo_[1].ap (inR);
            const float dL1 = dryHi_[0].ap (dL0), dR1 = dryHi_[1].ap (dR0);
            const float dL = dL0 + (dL1 - dL0) * dryX_;
            const float dR = dR0 + (dR1 - dR0) * dryX_;

            // ── stereo basis. Mid-Side is a genuine topology swap: two trees on a rotated
            //    basis, with the S thresholds 6 dB deeper (side energy is that much lower on
            //    this bus — the offset makes M and S see the SAME `over` and `under`, so the
            //    processing is spectral rather than an accidental widener).
            // The M/S basis is a SIGNAL PATH, not a gain, so it crossfades — rotating it in one
            // sample is a waveform step and no gain limiter can see it (measured 5.25 dB/ms).
            const float msF = gMs_.proc ((stereo_ == 2) ? 1.0f : 0.0f);
            float c0 = inL, c1 = inR;
            if (msF > 1.0e-4f)
            { const float m = 0.7071068f * (inL + inR), sd = 0.7071068f * (inL - inR);
              c0 += msF * (m - c0); c1 += msF * (sd - c1); }

            float band[2][kBands];
            for (int c = 0; c < 2; ++c)
            {
                const float x = (c == 0) ? c0 : c1;
                float lo, rest;
                splitLo_[c].split (x, lo, rest);
                if (NB == 3)
                {
                    float mid, hi;
                    splitHi_[c].split (rest, mid, hi);
                    band[c][0] = alignLow_[c].ap (lo);   // the LOW band bypasses split-2, so it
                    band[c][1] = mid;                    // must eat the same AP2(f_hi) or the
                    band[c][2] = hi;                     // recombination combs around f_hi
                }
                else { band[c][0] = lo; band[c][1] = rest; band[c][2] = 0.0f; }
            }

            // ── the Crest transient detector (broadband — a transient IS broadband, and one
            //    detector per channel is ⅓ the cost of one per band for the same behaviour).
            if (upHold_)
                for (int c = 0; c < 2; ++c)
                {
                    const float m = std::fabs ((c == 0) ? inL : inR);
                    bf_[c] += (m - bf_[c]) * (m > bf_[c] ? bAtk_ : bRel_);
                    bs_[c] += (m - bs_[c]) * bRel_ * 0.25f;
                    if (bf_[c] > 4.0f * bs_[c] + 1.0e-6f) { biteCnt_[c] = biteHoldN_; biteG_[c] = 0.0f; }
                    else if (biteCnt_[c] > 0) { --biteCnt_[c]; }
                    else biteG_[c] += (1.0f - biteG_[c]) * biteRelA_;
                }

            // ── every threshold, slope and makeup glides PER SAMPLE (law 4). Slope jumps
            //    modulate gain directly; a block-edge step is a 2.7 ms zipper at 128/48 k.
            float x2b[kBands][2];
            for (int b = 0; b < NB; ++b)
            {
                tdn_[b]  += (tdnT_[b] - tdn_[b])  * aGl_;
                tup_[b]  += (tupT_[b] - tup_[b])  * aGl_;
                sdn_[b]  += (sdnT_[b] - sdn_[b])  * aGl_;
                sup_[b]  += (supT_[b] - sup_[b])  * aGl_;
                mkDb_[b] += (mkT_[b]  - mkDb_[b]) * aGl_;
                mkTopDb_[b] += (mkTopT_[b] - mkTopDb_[b]) * aGl_;

                for (int c = 0; c < 2; ++c)
                {
                    float sg = band[c][b];
                    if (b == NB - 1 && hiTiltDb_ != 0.0f) sg *= dyn::db2lin (hiTiltDb_);
                    x2b[b][c] = sg * sg;
                }
                if (b == 0 && lowMono_) { const float m = 0.5f * (x2b[b][0] + x2b[b][1]); x2b[b][0] = x2b[b][1] = m; }
                if (stereo_ == 0)       { const float m = std::max (x2b[b][0], x2b[b][1]); x2b[b][0] = x2b[b][1] = m; }
            }
            // ONE DETECTOR: every band driven by the LOUDEST band's energy — the console's
            // one-sidechain-for-everything law, which turns the device into a full-band ducker.
            if (bandLink_)
                for (int c = 0; c < 2; ++c)
                {
                    float m = x2b[0][c];
                    for (int b = 1; b < NB; ++b) m = std::max (m, x2b[b][c]);
                    for (int b = 0; b < NB; ++b) x2b[b][c] = m;
                }

            float wet[2] = { 0.0f, 0.0f };
            for (int b = 0; b < NB; ++b)
            {
                const float x2[2] = { x2b[b][0], x2b[b][1] };
                for (int c = 0; c < 2; ++c)
                {
                    float e = x2[c] + 1.0e-20f;                 // denormal floor AT THE SOURCE
                    if (det_ == 1 || det_ == 2) e = pre_[c][b].proc (e, aPre_);   // RMS pre-average

                    const float off = (c == 1 ? sOff_ * msF : 0.0f);
                    const float Tdn = tdn_[b] + off, Tup = tup_[b] + off;
                    const float tdn2 = dyn::db2lin (2.0f * Tdn);    // threshold² in the MS domain
                    const float tup2 = dyn::db2lin (2.0f * Tup);

                    // ── Vital's two clamped envelopes. This is NOT a cosmetic detail: with ONE
                    //    shared envelope the upward lane has to traverse the whole way down from
                    //    program level before it engages (≈ 230 ms after a loud note at the stock
                    //    28 ms release), and the tail-bloom that IS this effect arrives late and
                    //    limp. Clamped, the up-env sits AT its threshold while the band is loud
                    //    and starts falling the instant the band does — bloom in ~1 release.
                    float& eD = envDn_[c][b]; float& eU = envUp_[c][b];
                    // det 3 = PEAK ears: the follower attacks instantly, so the computers see
                    // the crest instead of the mean square — grabbier, and a different rectifier.
                    const float aA = (det_ == 3) ? 1.0f : aAtk_[b];
                    float aR = aRel_[b];
                    if (deepRel_) aR *= 1.0f / (1.0f + std::min (24.0f, grMem_[c][b]) * (1.0f / 4.0f));
                    eD += (e - eD) * (e > eD ? aA : aR);
                    eU += (e - eU) * (e > eU ? aA : aR);
                    eD = dyn::flushd (eD); eU = dyn::flushd (eU);
                    const float eDc = (eD > tdn2) ? eD : tdn2;
                    const float eUc = (eU < tup2) ? eU : tup2;

                    const float Ldn = dyn::ms2db (eDc);
                    const float Lup = dyn::ms2db (eUc);

                    float gDn = dyn::grDown (Ldn, Tdn, sdn_[b], knee_);
                    gDnPrev_[c][b] = gDn;
                    // a 300 ms memory of how hard this band has been working — it is what
                    // keeps `Long Tail` slow AFTER the note, which is the whole idea.
                    grMem_[c][b] += (gDn - grMem_[c][b]) * aMem_;
                    float gUp = 0.0f;
                    if (!(b == 0 && lowUpOff_))
                    {
                        gUp = dyn::liftUp (Lup, Tup, sup_[b], capDb_[b])
                              * dyn::floorGate (Lup, -78.0f, 12.0f);
                        if (upHold_) gUp *= biteG_[c];
                    }

                    // 🔴 fb431 — THE TOP HALF'S EXTRA MAKEUP RIDES THE FLOOR GATE. The gate
                    //    two lines up stops the UPWARD lift amplifying silence; the makeup was
                    //    added unconditionally, which was survivable at +3 dB and is not at +9
                    //    — a dithered -96 dBFS floor came out at -67.4 dBFS.
                    //    ⚠️ Gating the WHOLE makeup (tried first) fixes that and costs the
                    //    stereo modes: the quiet side channel and the quiet bands lose their
                    //    makeup too, so the M/S difference stops surviving a mono fold —
                    //    measured 4.37 dB -> 1.06. Only the EXTRA is gated.
                    float gdb = -gDn + gUp + mkDb_[b]
                              + mkTopDb_[b] * dyn::floorGate (Lup, -78.0f, 12.0f);
                    if (xfN_ > 0)
                        gdb = gdbZ_[c][b] + dyn::clampf (gdb - gdbZ_[c][b], -slewPS_, slewPS_);
                    gdbZ_[c][b] = gdb;
                    float g = dyn::db2lin (gdb);
                    if (g > 400.0f) g = 400.0f;                 // Vital's kMaxExpandMult idiom
                    float y = band[c][b] * g;

                    // Heavy's per-band ceiling: a cubic soft clip a few dB above the downward
                    // threshold. Bounded H3, and it is what makes "welded shut" audible as a
                    // TEXTURE rather than just a flat meter.
                    // Heavy's per-band ceiling FADES in and out over 20 ms. Inserting a clipper
                    // in one sample is a waveform step no gain limit can catch: Surge (no clip,
                    // slopes 0) → Heavy (clip at T+3 dB, slopes 1.0) measured 4.54 dB/ms.
                    // 🚨 fb425 — `Amount` RAN BACKWARDS, and this ceiling was why.
                    // The ceiling used to be `db2lin (Tdn + clipHd_ + mkDb_[b])`, i.e. a fixed
                    // headroom above WHERE A SLOPE OF 1 WOULD PIN THE BAND. That reference is
                    // only true when the downward computer is actually pinning: `mkDb_` is
                    // `ts.mk[b] * lo01` and `sdn_` is `... * lo01`, so at Amount 0 BOTH are
                    // zero — the band gets NO gain reduction and the ceiling collapses by the
                    // whole makeup (21/24/20 dB on `Heavy`) onto the untouched signal. Measured
                    // on 220 Hz at −10.5 dBFS: THD 35.75 % at Amount 0 against 2.51 % at
                    // Amount 100, and a peak 21 dB LOWER at the bottom of the knob than in the
                    // middle. The knob's zero was its most destructive setting.
                    //
                    // Two independent corrections, and both are needed:
                    //  1. THE CEILING RIDES THE REALISED OUTPUT, not the pinned reference.
                    //     `Ldn + gdb` is where the gain computer has actually put this band's
                    //     envelope this sample. `slack` is how far that sits ABOVE the pinned
                    //     reference — identically 0 when s = 1 (because then Ldn − gDn = Tdn),
                    //     so `Heavy` at Amount 100 is BIT-IDENTICAL to before, and it opens by
                    //     exactly the dynamic range the slope has NOT removed everywhere else.
                    //  2. THE CLIP BLEND FOLLOWS THE DOWNWARD SLOPE. The clipper is the
                    //     welding, and with no gain reduction there is nothing to weld. `sdn_`
                    //     is the per-sample-glided slope, so this cannot click, and it is 1 at
                    //     Amount 100 — again bit-identical there.
                    // Amount 0 (or Press 0) is therefore NEUTRAL by construction on every Type
                    // and every Character, not by calibration.
                    const float cf = clipF * sdn_[b];
                    if (cf > 1.0e-4f)
                    {
                        const float outDb = Ldn + gdb;            // realised band output, dB
                        const float pinDb = Tdn + mkDb_[b];       // where s = 1 pins it
                        const float slack = (outDb > pinDb) ? (outDb - pinDb) : 0.0f;
                        const float lim = dyn::db2lin (pinDb + clipHd_ + slack);
                        const float t = y / lim;
                        const float yc = (t > -1.5f && t < 1.5f) ? lim * (t - t * t * t * (1.0f / 6.75f))
                                                                 : lim * (t > 0.0f ? 1.0f : -1.0f);
                        y += cf * (yc - y);
                    }
                    wet[c] += y;

                    // the viz band level is the FOLLOWER, not the instantaneous x²: the mean of
                    // log|x|² over a 16 ms window under-reads a sine by ~9 dB (every zero crossing
                    // contributes −∞), which made the first draft's high band read −111 dBFS on a
                    // probe that genuinely had none — a detector that lies the same way every time.
                    if (c == 0) { grAcc[b] += (gDn - gUp); lvAcc[b] += dyn::ms2db (eD); }
                }
            }

            float wL = wet[0], wR = wet[1];
            if (msF > 1.0e-4f)
            { const float l = 0.7071068f * (wL + wR), r = 0.7071068f * (wL - wR);
              wL += msF * (l - wL); wR += msF * (r - wR); }
            if (dip_ < 0.99999f) { wL *= dip_; wR *= dip_; }

            L[i] = dL + (wL - dL) * mix;     // linear crossfade — wet and dry are CORRELATED here,
            R[i] = dR + (wR - dR) * mix;     // so equal-power would bump +3 dB at Mix 50.

            const float pk = std::max (std::fabs (L[i]), std::fabs (R[i]));
            lvlZ_ += (pk - lvlZ_) * lvlA_;
            ++accN;
            if (++vizCtr_ >= vizEvery_)
            {
                const float inv = (accN > 0) ? 1.0f / (float) accN : 0.0f;
                for (int b = 0; b < kBands; ++b)
                {
                    viz_.grDb[b]   = (b < NB) ? grAcc[b] * inv : 0.0f;
                    viz_.bandDb[b] = (b < NB) ? lvAcc[b] * inv : -120.0f;
                    grAcc[b] = lvAcc[b] = 0.0f;
                }
                viz_.lvl = dyn::clampf (lvlZ_ / dyn::kBusNomLin * 0.5f, 0.0f, 1.0f);
                accN = 0; vizCtr_ = 0;
            }
        }
    }

    const Viz& viz() const noexcept { return viz_; }

    // ── introspection the harness uses (NOT part of the integration surface) ──
    float thresholdDn (int b) const noexcept { return tdn_[dyn::clampi (b, 0, 2)]; }
    float thresholdUp (int b) const noexcept { return tup_[dyn::clampi (b, 0, 2)]; }
    float slopeDn (int b)     const noexcept { return sdn_[dyn::clampi (b, 0, 2)]; }
    float slopeUp (int b)     const noexcept { return sup_[dyn::clampi (b, 0, 2)]; }
    float attackMs (int b)    const noexcept { return atkMs_[dyn::clampi (b, 0, 2)]; }
    float releaseMs (int b)   const noexcept { return relMs_[dyn::clampi (b, 0, 2)]; }
    int   bands()             const noexcept { return nBands_; }

private:
    struct TypeSpec
    {
        float xloMul, xhiMul;
        int   nBands;
        float tdn[3], tup[3];        // dBFS
        float sdn[3], sup[3];
        float mk[3];                 // makeup dB
        float atk[3], rel[3];        // ms, at Speed 0.5
        float knee;
        int   det;                   // 0 = branching mean-square · 1 = 25 ms RMS pre-average
        float clipHead;              // per-band soft clip at T_dn + this; 999 = off
        int   lowUpOff, lowMono;
    };

    static const TypeSpec& typeSpec (int t) noexcept
    {
        static const TypeSpec S[kNumTypes] = {
        /* Over Top — the Xfer/Vital calibration, RE-DERIVED against a MEASURED band level on   */
        /* this bus. Vital's −28/−25/−30 shifted by −20 dB gives −48/−45/−56 and 19/26/29 dB of */
        /* GR on the reference chord — well past the 8–18 dB the bible itself gates for, and    */
        /* 10.7 dB short of unity. Measured band envelopes are −27.1/−15.0/−26.7 dBFS, so the   */
        /* thresholds below land 12/14/13 dB of GR and the makeups match them. Believe the      */
        /* measurement (FINDINGS §2).                                                            */
        { 1.0f, 1.0f, 3, {-40,-31,-40}, {-45,-37,-46}, {0.90f,0.857f,1.0f}, {0.8f,0.8f,0.8f},
          {12,14,13}, {2.8f,1.4f,0.7f}, {40,28,15}, 2.0f, 0, 999.0f, 0, 0 },
        /* Gentle — 25 ms RMS pre-average (a much lower envelope), ×4 slower, 12 dB knee,       */
        /* slopes ×0.7. Thresholds 6 dB deeper to reach the pre-averaged level at all.          */
        { 1.0f, 1.0f, 3, {-46,-37,-46}, {-51,-43,-52}, {0.63f,0.60f,0.70f}, {0.56f,0.56f,0.56f},
          {10,10,8}, {11.2f,5.6f,2.8f}, {160,112,60}, 12.0f, 1, 999.0f, 0, 0 },
        /* Heavy — ∞:1 everywhere, 6 dB deeper again, up 0.9, plus per-band clippers.           */
        { 1.0f, 1.0f, 3, {-46,-37,-46}, {-51,-43,-52}, {1.0f,1.0f,1.0f}, {0.9f,0.9f,0.9f},
          {21,24,20}, {2.0f,1.0f,0.5f}, {30,20,11}, 0.0f, 0, 3.0f, 0, 0 },
        /* Sheen — X-High down to 1.8 k, the high band's floor moved toward the programme,      */
        /* 0.35/8 ms ballistics so the shimmer reads as texture rather than pumping.            */
        { 1.0f, 0.55f, 3, {-40,-31,-26}, {-45,-37,-28}, {0.90f,0.907f,1.0f}, {0.8f,0.8f,0.9f},
          {12,14,2}, {2.8f,1.4f,0.35f}, {40,28,8}, 2.0f, 0, 999.0f, 0, 0 },
        /* Bass Safe — low band: upward OFF, gentler slope, 10/120 ms, mono-summed detection,   */
        /* threshold 4 dB up. Every mix engineer's OTT complaint, fixed.                        */
        { 1.0f, 1.0f, 3, {-36,-31,-40}, {-45,-37,-46}, {0.75f,0.857f,1.0f}, {0.0f,0.8f,0.8f},
          {7,14,13}, {10.0f,1.4f,0.7f}, {120,28,15}, 2.0f, 0, 999.0f, 1, 1 },
        /* Surge — downward OFF everywhere. Pure upward: resurrection with no squash, and       */
        /* therefore makeup 0 — it is already unity on anything loud, by construction.          */
        { 1.0f, 1.0f, 3, {-40,-31,-40}, {-44,-39,-44}, {0.0f,0.0f,0.0f}, {0.85f,0.85f,0.85f},
          {0,0,0}, {5.6f,2.8f,1.4f}, {80,56,30}, 2.0f, 0, 999.0f, 0, 0 },
        /* Two Band — ONE split at 650 Hz. The mid disappears, so a loud 1 k and a quiet 5 k    */
        /* share ONE detector and DUCK EACH OTHER. That cross-band coupling is the whole point  */
        /* and it is something three bands physically cannot do.                                */
        { 1.0f, 1.0f, 2, {-32,-34,0}, {-38,-40,0}, {0.85f,1.0f,0.0f}, {0.8f,0.85f,0.0f},
          {13,14,0}, {2.0f,0.8f,1.0f}, {34,18,1}, 2.0f, 0, 999.0f, 0, 0 },
        /* Stagger — the bands DECOUPLE IN TIME (167:1 of ballistic spread vs Over Top's 4:1),  */
        /* so the spectrum MORPHS through the note instead of holding still.                    */
        { 1.0f, 1.0f, 3, {-40,-31,-40}, {-45,-37,-46}, {0.90f,0.857f,1.0f}, {0.8f,0.8f,0.8f},
          {12,14,8}, {40.0f,1.4f,0.10f}, {700,28,2.5f}, 2.0f, 0, 999.0f, 0, 0 } };
        return S[dyn::clampi (t, 0, kNumTypes - 1)];
    }

    struct CharSpec
    {
        float atkMul, relMul, spread, hiTimeMul;
        float dnMul, upMul, tdnOff, tupOff;
        float kneeDb, upCap, hiTilt;
        float xloMul, xhiMul;
        int   det, bandLink, upHold, lowMono;
        float clipHead;
        int   deepRel, lowUp;      // deepRel: release slows with GR · lowUp: ±1 forces the low
                                   // band's upward computer on/off, 0 = follow the Type
    };

    static const CharSpec& charSpec (int t, int c) noexcept
    {
        #define OC(aM,rM,sp,hT, dn,up,to,uo, kn,cap,ti, xl,xh, dt,bl,uh,lm, ch) \
            { aM,rM,sp,hT, dn,up,to,uo, kn,cap,ti, xl,xh, dt,bl,uh,lm, ch, 0, 0 }
        #define OD(aM,rM,sp,hT, dn,up,to,uo, kn,cap,ti, xl,xh, dt,bl,uh,lm, ch, dr,lu) \
            { aM,rM,sp,hT, dn,up,to,uo, kn,cap,ti, xl,xh, dt,bl,uh,lm, ch, dr,lu }
        static const CharSpec C[kNumTypes][kNumChars] = {
        /* ── Over Top ─────────────────────────────────────────────────────────────────────── */
        { OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Straight Up
          OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,    3,0,0,0, -1),   // Sharp Ears (instant-attack peak ears)
          OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,    2,0,0,0, -1),   // Long Ears  (60 ms RMS)
          OC(1,1,1,1,      1,1,0,0,    44,24,0,   1,1,   -1,0,0,0, -1),   // Wide Corner
          OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,1,0,0, -1),   // One Detector
          OC(1,1,2.2f,1,   1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Slow Low
          OC(1,1,1,1,      1,1,-6,6,   -1,24,0,   1,1,   -1,0,0,0, -1),   // Twice Deep
          OC(0.35f,0.6f,1,1, 1,1,0,4,  -1,30,0,   1,1,   -1,0,1,0, -1) }, // Full Bite
        /* ── Gentle ───────────────────────────────────────────────────────────────────────── */
        { OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Round Corner
          OC(1,4.5f,1,1,   1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Slow Hands
          OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,    2,0,0,0, -1),   // Long Window
          OC(1,1,1,1,   0.5f,0.5f,0,0, -1,24,0,   1,1,   -1,0,0,0, -1),   // Half Slopes
          OD(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1, 1,0), // Long Tail (release slows with GR)
          OC(1,1,1,1,      1,1,0,0,    -1,24,-9,  1,1,   -1,0,0,0, -1),   // Soft Top
          OC(1,1,0.0f,1,   1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Even Bands
          OC(1,1,1,1, 0.35f,0.35f,6,-6,-1,24,0,   1,1,   -1,0,0,0, -1) }, // Barely There
        /* ── Heavy ────────────────────────────────────────────────────────────────────────── */
        { OC(1,1,1,1,      1,1,0,0,     0,24,0,   1,1,   -1,0,0,0, -1),   // Welded Shut
          OC(1,1,1,1,      1,1,0,0,     0,24,0,   1,1,   -1,0,0,0, 1.0f),  // Band Clip
          OC(1,1,1,1,      1,1,0,0,     0,24,0,   1,1,   -1,0,0,0, 999),  // No Clip (the ceiling stops clipping)
          OC(1,1,1,1,      1,1,-8,8,    0,30,0,   1,1,   -1,0,0,0, 6.0f),  // Deeper Jaws
          OC(0.25f,0.25f,1,1, 1,1,0,0,  0,24,0,   1,1,   -1,0,0,0, 6.0f),  // Fast Grind
          OC(1,1,1,1,      1,1,0,0,     0,24,0,   1,1,    3,0,0,0, 6.0f),  // Peak Grab
          OC(1,1,1,1,      1,1,0,0,     0,24,0,   1,1,   -1,1,0,0, 6.0f),  // Wall Ears
          OC(1,1,1,1,   1.2f,1.2f,-4,4, 0,30,0,   1,1,   -1,0,0,0, 2.0f) },// Total Squeeze
        /* ── Sheen ────────────────────────────────────────────────────────────────────────── */
        { OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Top Sheet
          OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,2.6f,-1,0,0,0, -1),   // Higher Split
          OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,0.6f,-1,0,0,0, -1),   // Lower Split
          OC(1,1,1,1,      1,1,0,-10,   0,10,0,   1,1,   -1,0,0,0, 4.0f),  // Glass Ceiling
          OC(1,1,1,4.0f,   1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Slow Shimmer
          OC(1,1,1,0.25f,  1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Fast Shimmer
          OC(1,1,1,1,      1,1,0,0,    -1,30,6,   1,1,   -1,0,0,0, -1),   // Dark Source
          OC(1,1,1,1,   1,1.25f,0,8,   -1,30,0,   1,1,   -1,0,0,0, -1) }, // Sheen Wall
        /* ── Bass Safe ────────────────────────────────────────────────────────────────────── */
        { OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,0,0,1, -1),   // Anchor Low
          OD(1,1,1.9f,1,   1,1,0,0,    -1,24,0,   1,1,   -1,0,0,1, -1, 0,0), // Mono Low
          OC(1,2.0f,1,1,   1,1,0,0,    -1,24,0,   1,1,   -1,0,0,1, -1),   // Slower Low
          OC(1,1,1,1,      1,1,-4,0,    0,24,0,   1,1,   -1,0,0,1, -1),   // Low Ceiling
          OC(1,1,1,1,   1.25f,1,0,0,   -1,24,0, 1.7f,1,  -1,0,0,1, -1),   // Reese Guard
          OD(1,1,0.0f,1,   1,1,0,0,    -1,24,0,   1,1,   -1,0,0,-1, -1, 0,1), // Free Low (the low band stops being special)
          OC(1,1,1,1,      1,1,0,0,    34,24,0,   1,1,   -1,0,0,1, -1),   // Wide Corner Low
          OC(0.4f,0.4f,1,1, 1,1,0,0,   -1,24,0, 0.6f,1,  -1,0,0,1, -1) }, // Tight Low
        /* ── Surge ────────────────────────────────────────────────────────────────────────── */
        { OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Tail Riser
          OC(1,1,1,1,      1,1.15f,0,6,-1,36,0,   1,1,   -1,0,0,0, -1),   // Deep Riser
          OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,1,0,0, -1),   // Tied Rise
          OC(0.25f,0.25f,1,1, 1,1,0,0, -1,24,0,   1,1,   -1,0,0,0, -1),   // Fast Riser
          OC(1,1,1,1,      1,1,0,0,    -1,5,0,    1,1,   -1,0,0,0, -1),   // Capped Riser
          OC(1,1,1,0.3f,   1,1.2f,0,4, -1,30,0,   1,0.7f,-1,0,0,0, -1),   // Top Riser
          OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,    2,0,0,0, -1),   // Mean Ears
          OC(1,1,1,1,   1,1.12f,0,10,  -1,36,0,   1,1,   -1,0,0,0, -1) }, // Riser Wall
        /* ── Two Band ─────────────────────────────────────────────────────────────────────── */
        { OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Body Sparkle
          OC(1,1,1,1,      1,1,0,0,    -1,24,0, 0.45f,1, -1,0,0,0, -1),   // Low Split  (~300 Hz)
          OC(1,1,1,1,      1,1,0,0,    -1,24,0, 2.2f,1,  -1,0,0,0, -1),   // High Split (~1.4 k)
          OC(1,1,1,1,   1.35f,1,-5,0,    0,24,0,   1,1,   -1,0,0,0, -1),   // Hard Body
          OC(1,1,1,1,   0.6f,1,0,0,    14,24,0,   1,1,   -1,0,0,0, -1),   // Soft Body
          OC(1,1,1,1,   1,1.2f,0,6,     0,30,0,   1,1,   -1,0,0,0, 4.0f),  // Sparkle Wall
          OC(2.5f,2.5f,1,1, 1,1,0,0,   -1,24,0,   1,1,   -1,0,0,0, -1),   // Slow Twin
          OC(0.2f,0.2f,1,1, 1,1,0,0,   -1,24,0,   1,1,   -1,0,0,0, -1) }, // Fast Twin
        /* ── Stagger ──────────────────────────────────────────────────────────────────────── */
        { OC(1,1,1,1,      1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Time Spread
          OC(1,1,2.1f,1,   1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Wider Spread
          OC(1,1,0.5f,1,   1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Narrow Spread
          OC(1,1,-1.0f,1,  1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Reverse Spread
          OC(1,1,1,1,      1,1,0,0,    -1,24,0, 1.8f,1,  -1,0,0,1, -1),   // Slow Anchor
          OC(1,1,1,0.25f,  1,1,0,0,    -1,24,0,   1,1,   -1,0,0,0, -1),   // Fast Top
          OC(1,1,1,1,      1,1,-6,6,   -1,30,0,   1,1,   -1,0,0,0, -1),   // Deep Spread
          OC(1,1,1.3f,1, 1,1.1f,-3,5,   0,30,0,   1,1,   -1,0,0,0, 5.0f) } };  // Spread Wall
        #undef OC
        #undef OD
        return C[dyn::clampi (t, 0, kNumTypes - 1)][dyn::clampi (c, 0, kNumChars - 1)];
    }

    void applyXover (float xl, float xh) noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            splitLo_[c].set (xl, fs_);
            dryLo_[c].setLR (xl, fs_);
            splitHi_[c].set (xh, fs_);          // set unconditionally: `dryHi_` runs in both
            alignLow_[c].setLR (xh, fs_);       // trees so the crossfade above has a live tap
            dryHi_[c].setLR (xh, fs_);
        }
    }

    // ═════ state ═════════════════════════════════════════════════════════════
    Params pr_;
    Viz    viz_;
    float  fs_ = 48000.0f;

    dyn::LR4  splitLo_[2], splitHi_[2];
    dyn::Svf1 alignLow_[2], dryLo_[2], dryHi_[2];
    dyn::MeanSquare pre_[2][kBands];
    dyn::Glide gXlo_, gXhi_, gMix_, gClip_, gMs_;

    float envDn_[2][kBands] {}, envUp_[2][kBands] {};
    float tdn_[kBands] {}, tup_[kBands] {}, sdn_[kBands] {}, sup_[kBands] {};
    float mkDb_[kBands] {}, mkTopDb_[kBands] {}, capDb_[kBands] {}, aAtk_[kBands] {}, aRel_[kBands] {};
    float tdnT_[kBands] {}, tupT_[kBands] {}, sdnT_[kBands] {}, supT_[kBands] {}, mkT_[kBands] {}, mkTopT_[kBands] {};
    float atkMs_[kBands] {}, relMs_[kBands] {};
    float bf_[2] {}, bs_[2] {}, biteG_[2] { 1.0f, 1.0f }, gDnPrev_[2][kBands] {}, grMem_[2][kBands] {};
    int   biteCnt_[2] {};

    float xloTgt_ = 88.3f, xhiTgt_ = 2500.0f, mixTgt_ = 1.0f, knee_ = 2.0f, clipHd_ = 999.0f;
    float aPre_ = 0.01f, hiTiltDb_ = 0.0f, sOff_ = 0.0f, lvlA_ = 0.001f, lvlZ_ = 0.0f;
    float bAtk_ = 0.05f, bRel_ = 0.002f, biteRelA_ = 0.05f, aGl_ = 0.01f, dip_ = 1.0f, aMem_ = 1.0e-4f, capDbTop_ = 24.0f;
    float dipDn_ = 0.005f, dipUpS_ = 0.0007f, dryX_ = 1.0f;
    Params pend_; int dipDir_ = 0; bool pendOn_ = false;
    float gdbZ_[2][kBands] {}, slewPS_ = 0.03125f;
    int   xfN_ = 0, xfLen_ = 1920, prevType_ = -1, prevChar_ = -1, prevAxis_ = -1;
    int   nBands_ = 3, nBandsPrev_ = 3, stereo_ = 0, det_ = 0, vizEvery_ = 800, vizCtr_ = 0, biteHoldN_ = 480;
    bool  bandLink_ = false, lowMono_ = false, lowUpOff_ = false, upHold_ = false, primed_ = false, deepRel_ = false;
};

} // namespace tw
