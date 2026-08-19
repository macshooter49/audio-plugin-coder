#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TerrainCompressFx.h — fb420+. ONE instance of the FX-rack COMPRESS device (chain kind 11).
// Contract: Design/fx4/CONTRACT.md §2 (locked interface) · Bible: Design/COMPRESSOR-BUILD-BIBLE.md
// Shared core: DynamicsCore.h (with TerrainOttFx.h) · Harness: dynamics_cert.cpp
//
// ── THE ONE LAW THAT SINKS COPIED COMPRESSOR DESIGNS ────────────────────────
// Every threshold in every manual is stated against a 0 dBFS program. Terrain's FX bus is
// −26 dBFS for a single note. Copy an LA-2A's −20 dB threshold and the program never crosses
// it — the device ships DEAD, and every gate you write passes because the engine is "correct".
// So: the detector is lifted +26.02 dB and ALL internal thresholds are in **dBp**
// (0 dBp = −26.02 dBFS = single note; the reference chord = +6 dBp). DynamicsCore.h owns the
// constant. The audio path is untouched — only the detector is lifted.
//
// ── EIGHT TOPOLOGIES, NOT EIGHT VOICINGS ────────────────────────────────────
// A compressor is four blocks: detector → gain computer → ballistics → gain element. The
// lineages differ by WHICH BLOCK DOMINATES, and that is what the Type dropdown carries:
//   Exact     FF · exact knee · exponential            — the curve you set is the curve you get
//   Bus       FF · diode LEVEL-ADAPTIVE attack + dual-pool auto release (Glue/SSL law)
//   FET 76    FB · 20–800 µs attack · odd-harmonic gain element (1176)
//   Opto      FB · TWO release pools + a 10 s MEMORY integrator (T4 cell)
//   Vari-Mu   FB · slope GROWS WITH GR (remote-cutoff tube) · release to 25 s
//   OverEasy  FF · true RMS · 12 dB knee · and the ratio knob continues PAST ∞ into slope −1
//   Ride      FF · downward AND upward in one band (the OTT boundary type)
//   Limit     FF · ∞ slope · peak+hold · softclip catch
// Feedback vs feedforward is a real wiring difference (`d[n] = |y[n−1]|`, tapped PRE-colour so
// the gain element's own harmonics never re-enter the detector — that is a fizz loop).
//
// ── ZERO LATENCY, NO LOOKAHEAD, EVER (contract §2) ──────────────────────────
// The fb305 main-send exclusion math subtracts the routed dry from the mix SAMPLE-ALIGNED. A
// device that delays its wet path misaligns the subtraction and the dry leaks back phase-
// smeared. So `Limit` controls overshoot the fb264 way — a 0.8 ms-class one-pole attack,
// stereo-linked, plus a soft-clip safety catch — and EATS ~1 dB of overshoot as the documented
// price. Measured in the harness, printed, not hidden.
//
// ── THE CEILING (R11) ───────────────────────────────────────────────────────
// Ratio reaches ∞:1 at knob 1.0 exactly (slope s = t^0.85, s = 1 ⇒ 1/R = 0). OverEasy keeps
// going to s = 2 (output FALLS 1 dB per input dB — dbx's shipped "Infinity+"). Attack reaches
// 20 µs on FET 76. Push at 100 puts the threshold 39 dB inside the program. At those settings
// a 48 dB input staircase comes out inside 2 dB and an 80 Hz sine is torn into a > 10 % THD
// buzz because the gain tracks the waveform INSIDE its own period. That is not a bug; it is
// the top of the range, and the harness gates it.
//
// No allocation · no locks · no std::function reachable from processStereo. Denormal-flushed.
// ─────────────────────────────────────────────────────────────────────────────

#include "DynamicsCore.h"

namespace tw {

class TerrainCompressFx
{
public:
    // ── identity ─────────────────────────────────────────────────────────────
    static constexpr int kNumTypes = 8;
    static constexpr int kNumChars = 8;
    static constexpr int kNumDetect = 5;          // the back dropdown-2 axis
    static constexpr int kKnee      = 32;         // Viz transfer-curve points

    enum TypeId { T_EXACT = 0, T_BUS, T_FET, T_OPTO, T_VARIMU, T_OVEREASY, T_RIDE, T_LIMIT };

    static const char* const* typeNames() noexcept
    {
        static const char* const N[kNumTypes] =
            { "Exact", "Bus", "FET 76", "Opto", "Vari-Mu", "OverEasy", "Ride", "Limit" };
        return N;
    }

    /** The back dropdown-2 axis. NOT `Type` (R6) — this is what the compressor HEARS, and
     *  every entry is a different rectifier/averager, i.e. physics. */
    static const char* const* detectNames() noexcept
    {
        static const char* const N[kNumDetect] = { "Native", "Peak", "Average", "Patient", "Spike" };
        return N;
    }

    static const char* const* charNames (int type) noexcept
    {
        static const char* const C[kNumTypes][kNumChars] = {
        /* Exact    */ { "Precise",     "Soft Touch",  "Loose Grip", "Blunt",
                         "Deep Release","Line Attack", "Poise",      "Judder" },
        /* Bus      */ { "Quad Bus",    "Hand Set",    "Two Easy",   "Ten Punchy",
                         "Fast City",   "Big Desk",    "Pump Bus",   "No Diode" },
        /* FET 76   */ { "Blackface",   "Blue Stripe", "All Buttons","Twenty Lock",
                         "Loose Four",  "Broken Bias", "Waiting Fet","Two Pass" },
        /* Opto     */ { "Cell Classic","Fresh Cell",  "Tired Cell", "Quick Cell",
                         "Even Pools",  "Crystal",     "Tube Stage", "Bright Ears" },
        /* Vari-Mu  */ { "Studio 670",  "Time One",    "Time Four",  "Auto Peaks",
                         "Long Haul",   "Push Pull",   "Lateral",    "Triode Soft" },
        /* OverEasy */ { "Over Easy",   "Hard 160",    "Infinity",   "Infinity Plus",
                         "Slow Window", "Crush RMS",   "Decilinear", "Anti" },
        /* Ride     */ { "Level Rider", "Deep Floor",  "Only Up",    "Only Down",
                         "Fast Clamp",  "Slow Iron",   "Bright Bias","Vocal Sit" },
        /* Limit    */ { "Clean Wall",  "Soft Ceiling","Hard Stop",  "Pump Limit",
                         "Loud War",    "Clip Guard",  "Springy",    "Porous" } };
        const int t = dyn::clampi (type, 0, kNumTypes - 1);
        return C[t];
    }

    // ═════ THE LABELS. THIS HEADER IS THE SINGLE SOURCE OF TRUTH (FIXES.md §3) ═══════════
    // The card, the roster, the worklet and the harness all read these. `Cassette` played
    // `Studio` through four rounds of green measurement because a label lived only in markdown;
    // nothing on this device may print a word that is not in one of these arrays.
    static const char* deviceName() noexcept { return "Compress"; }
    /** FRONT: three heroes, then Mix. Index order matches Params::push/ratio/lift/mix. */
    static const char* const* frontNames() noexcept
    { static const char* const N[4] = { "Push", "Ratio", "Lift", "Mix" }; return N; }
    /** BACK: the 8 knobs, 4x2, in b1..b8 order (fb275 chassis). */
    static const char* const* backNames() noexcept
    { static const char* const N[8] = { "Attack", "Release", "Round", "Hear Cut",
                                        "Edge", "Cling", "Tie", "Burn" }; return N; }
    /** The two back dropdowns: [0] = Character (chassis), [1] = the SECOND AXIS. Never `Type`. */
    /** fb426 — IS THIS CELL A LOCKED-RATIO IDENTITY? Asked by the R11 cell gate, answered from
        the CharSpec so the exemption is DERIVED and cannot drift from the roster. A hand-typed
        list of names is the second table that goes stale (it is why `charNames()` was deleted).
        Two shapes qualify, and only these two:
          · the Character AUTHORS a slope cap below unity — it IS a ratio (`Loose Grip` 2.5:1,
            `Twenty Lock` 20:1, `Two Easy` 2:1, `Loose Four` 4:1). Asking it to reach ∞:1 asks it
            to stop being itself.
          · the Character switches the DOWNWARD computer off (F_UPONLY, `Only Up`) — there is no
            downward curve for a downward staircase to measure.
        `Anti` is NOT here: its cap is ABOVE unity (the dbx Infinity+ negative zone), and it is
        gated on the SIGN of the transfer curve instead. Exempting something must change WHAT you
        measure, never WHETHER you measure. */
    static bool ratioLocked (int type, int chr) noexcept
    {
        const auto& cs = charSpec (type, chr);
        if (cs.slopeCap > 0.0f && cs.slopeCap < 1.0f) return true;
        if (cs.flags & F_UPONLY)                      return true;
        return false;
    }

    static const char* const* dropdownNames() noexcept
    { static const char* const N[2] = { "Character", "Detect" }; return N; }
    /** The ONE front pill. */
    static const char* pillName() noexcept { return "Auto"; }
    static constexpr int kNumFront = 4, kNumBack = 8;

    static_assert (kNumTypes > 0 && kNumChars == 8, "roster/table must move together");

    // ── the locked Params (CONTRACT §2). Front three named per device (fx3 precedent —
    //    the phaser renamed f1..f3 to rate/depth/feedback):
    //        push = Push · ratio = Ratio · lift = Lift
    //    Back eight:  b1 Attack · b2 Release · b3 Round · b4 Hear Cut
    //                 b5 Edge   · b6 Cling  · b7 Tie   · b8 Burn
    //    axis        = the SECOND back dropdown, `Detect` (0..4, see detectNames()).
    struct Params
    {
        int   type = 0, character = 0;
        int   axis = 0;                                   // Detect
        float push = 0.20f, ratio = 0.50f, lift = 0.25f;  // FRONT 3
        float mix  = 1.0f;                                // 1.0 = FULLY WET
        float b1 = 0.61f, b2 = 0.63f, b3 = 0.25f, b4 = 0.0f,
              b5 = 0.5f,  b6 = 0.0f,  b7 = 1.0f,  b8 = 0.0f;
        bool  tempoSync = false; double bpm = 120.0;      // unused: ballistics are not musical time
        // The ONE front pill (fx3 precedent: the phaser added `invert`, the flanger `motion`).
        // Auto-makeup, chord-calibrated, 70 % / 300 ms. Default OFF — full compensation turns a
        // Push sweep into a timbre-only change and deletes the "louder AND denser" that reads as
        // power. Additive with a false default, so integration is safe even if never wired.
        bool  autoMakeup = false;
    };

    struct Viz
    {
        float grDb = 0.0f;              // gain reduction, +ve = reduction
        float inDb = -60.0f;            // input peak, dBp
        float outDb = -60.0f;           // output peak, dBp
        float knee[kKnee] {};           // static transfer curve, OUTPUT dBp for input −60…+12 dBp
        float lvl = 0.0f;               // 0..1 wet level, for the idle-dim / playing-bright law
    };

    // ═════════════════════════════════════════════════════════════════════════
    void prepare (double sampleRate, int /*maxBlock*/) noexcept
    {
        fs_ = (sampleRate > 8000.0) ? (float) sampleRate : 48000.0f;
        // fb342 punch follower, block-rate corrected off its 48 k constants.
        const float sr = 48000.0f / fs_;
        pAtk_ = 1.0f - std::pow (1.0f - 0.0064f,  sr);
        pRel_ = 1.0f - std::pow (1.0f - 0.00026f, sr);
        lvlA_ = dyn::coefTau (0.030f, fs_);
        mkA_  = dyn::coefTau (0.300f, fs_);      // auto-makeup slew (§3.7)
        // ── THE TRANSITION SLEW LIMIT ────────────────────────────────────────────────
        // Seeding the smoothers and fading every discrete re-wiring got the worst Type
        // transition from 21.47 to 2.05 dB of gain moved in one millisecond. What is left is
        // not a step at all: leaving a 2nd-order (RS_DAMPED) smoother, the new first-order one
        // starts from the SAME gain reduction but the outgoing 2nd-order state was LAGGING its
        // own target, and FET 76 closes that gap at its own 0.19 ms attack — 3.3 dB inside one
        // millisecond. There is no seed that removes it: the gap is what a 2nd-order smoother
        // IS. So the gain is slew-limited to 1.5 dB/ms for 40 ms after a Type/Character/Detect
        // change, and ONLY then — the limiter is disarmed the rest of the time, so a real
        // 20 µs attack on real programme material is untouched. This is the house law ("smooth
        // every parameter change over 10-30 ms") applied to the parameter that is a whole
        // topology.
        slewPS_ = 1.5f / (fs_ * 0.001f);
        xfLen_  = (int) (fs_ * 0.040f);
        xfN_    = 0;
        memA_ = dyn::coefTau (10.0f,  fs_);      // Opto T4 memory integrator
        gPush_.setTau (0.020f, fs_);  gLift_.setTau (0.020f, fs_);
        gSlope_.setTau (0.020f, fs_); gKnee_.setTau (0.020f, fs_);
        gTie_.setTau  (0.020f, fs_);  gHeat_.setTau (0.020f, fs_);
        gMix_.setTau  (0.010f, fs_);  gHc_.setTau   (0.030f, fs_);
        // The upward lane and the soft-clip catch are the only two gain paths that do NOT go
        // through the ballistics, so switching them on/off is a STEP unless they are faded.
        gUpOn_.setTau (0.020f, fs_);  gClip_.setTau (0.020f, fs_);
        gKind_.setTau (0.020f, fs_);  gAsym_.setTau (0.020f, fs_); gColG_.setTau (0.020f, fs_);
        gFbMix_.setTau (0.020f, fs_);   // fb425: the feedback→feedforward crossover at the top of Ratio
        gTwoS_ .setTau (0.020f, fs_);   // fb426: the two-pass slope lift over the same last 10 % of Ratio
        // Every DISCRETE re-wiring of the gain computer gets a 20 ms fade of its own. A boolean
        // that changes `grT` in one sample is not smoothed by the ballistics — FET 76's attack
        // reaches 20 µs and Limit's is 1.1 ms, so "the follower will take care of it" is false.
        gLeaky_.setTau (0.020f, fs_); gPlat_.setTau (0.020f, fs_); gTwoP_.setTau (0.020f, fs_);
        gKnAu_.setTau (0.020f, fs_);  gMs_.setTau (0.020f, fs_);   gDnOn_.setTau (0.020f, fs_);
        gTilt_.setTau (0.020f, fs_);  gVarMu_.setTau (0.020f, fs_); gOptoF_.setTau (0.020f, fs_);
        gEdge_.setTau (0.020f, fs_);
        vizEvery_ = (int) (fs_ / 60.0f); if (vizEvery_ < 32) vizEvery_ = 32;
        primed_ = false;
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            hc_[c].reset(); tilt_[c].reset(); ms_[c].reset(); dc_[c].reset();
            fbZ_[c] = 0.0f; fbS_[c] = 0.0f; peakHold_[c] = 0.0f; holdCnt_[c] = 0;
            gr_[c] = 0.0f; grF_[c] = 0.0f; grS_[c] = 0.0f; mem_[c] = 0.0f;
            v2_[c] = 0.0f; y2_[c] = 0.0f; upZ_[c] = 0.0f;
            pf_[c] = 0.0f; ps_[c] = 0.0f; latch_[c] = 0; latchHold_[c] = 0.0f;
            gdbZ_[c] = 0.0f;
        }
        xfN_ = 0;
        mkZ_ = 0.0f; lvlZ_ = 0.0f; vizCtr_ = 0;
        viz_ = Viz();
        // knee[] is meaningful even at idle (curves-must-move law) — fill on the first setParams.
    }

    // ═════ per BLOCK. Everything transcendental that is not per-sample lives here (fb-era
    //       law: hoisting setParams took 18 fx3 instances from 17.8 % to 14.1 % of a core).
    void setParams (const Params& pin) noexcept
    {
        Params p = pin;
        p.type      = dyn::clampi (p.type,      0, kNumTypes - 1);
        p.character = dyn::clampi (p.character, 0, kNumChars - 1);
        p.axis      = dyn::clampi (p.axis,      0, kNumDetect - 1);
        if (primed_ && (p.type != prevType_ || p.character != prevChar_ || p.axis != prevAxis_))
            xfN_ = xfLen_;
        prevType_ = p.type; prevChar_ = p.character; prevAxis_ = p.axis;
        pr_ = p;

        const TypeSpec& ts = typeSpec (p.type);
        const CharSpec& cs = charSpec (p.type, p.character);

        // ── THRESHOLD (Push). T_dBp = +9 − 48·push^0.9. The ^0.9 keeps the travel near-linear
        //    in dB (the ear hears dB) with a slight stretch at the top so the last 20 % still
        //    visibly deepens. push 0 → +9 dBp (3 dB above the CHORD: zero GR, bit-transparent);
        //    push 1 → −39 dBp (everything above the noise floor is over threshold).
        const float push = dyn::clampf (p.push, 0.0f, 1.0f);
        tTgt_ = 9.0f - 48.0f * std::pow (push, 0.9f) + cs.thrOff;

        // ── SLOPE (Ratio). s = 1 − 1/R, LINEAR in the knob under a ^0.85 taper, so every degree
        //    of travel adds the same audible dB of squash. s = 1 is ∞:1 and it is REACHED.
        //    OverEasy's top 15 % continues s: 1 → 2 (output slope 0 → −1, dbx "Infinity+").
        const float rk = dyn::clampf (p.ratio, 0.0f, 1.0f);
        float s;
        if (ts.slopeMax > 1.5f)                                   // OverEasy
            s = (rk <= 0.85f) ? std::pow (rk / 0.85f, 0.85f)
                              : 1.0f + (rk - 0.85f) * (1.0f / 0.15f);
        else
            s = std::pow (rk, 0.85f);
        s *= cs.slopeMul;
        if (ts.forceSlope > 0.0f) s = ts.forceSlope + (1.0f - ts.forceSlope) * std::pow (rk, 0.85f);
        if (s < cs.slopeFloor) s = cs.slopeFloor;

        // ── fb425 / R11 — ∞:1 MUST WALL, AND ON A FEEDBACK TOPOLOGY IT DID NOT ─────────────
        // A feedback detector tastes the OUTPUT, so the loop solves
        //     y_db = x_db − s·(y_db − T)   ⇒   y_db = (x_db + s·T) / (1 + s)
        // and at s = 1 — the knob's own ∞:1 — the CLOSED-LOOP slope is exactly 0.5. Half the
        // dynamic range survives at any Ratio. That is authentic: a real 1176 or LA-2A behaves
        // exactly this way, and it is why they are described as "self-limiting". It is also
        // POLITE at 100 %, which R11 forbids, and measured: FET 76 0.4683, Opto 0.5608,
        // Vari-Mu 0.4763 of a 48 dB staircase still standing at Push/Ratio 100.
        //
        // Max's ruling this round is to BREAK the authenticity at the very top and keep it
        // everywhere else. Over the last 10 % of the knob:
        //   · the detector CROSSES OVER from the output tap to the input tap, so the loop
        //     opens and the static curve is the one the user set. With d = m·y + (1−m)·x,
        //         y = [x(1 − s + s·m) + s·T] / (1 + s·m)
        //     which is monotone in m and lands on y = T (slope 0, a true wall) at m = 0.
        //   · a Type whose OWN cap is below 1 (Opto's 0.833 = 6:1, an authentic T4 maximum) has
        //     that cap lifted to 1 over the same 10 %. A cap a Character authored stays put —
        //     `Twenty Lock`, `Anti`, `Loose Grip` are locked-ratio identities by name.
        // Cost, accepted explicitly: the last ~10 % of Ratio on FET 76 / Opto / Vari-Mu is no
        // longer what the hardware does. Below 0.90 nothing here moves at all — `wallS` is
        // exactly 0 — so every ballistic, drift and distinctness measurement in the roster is
        // untouched. Smoothstep, so the join at 0.90 is C1 and the knob has no corner.
        const float wall  = dyn::clampf ((rk - 0.90f) * 10.0f, 0.0f, 1.0f);
        const float wallS = wall * wall * (3.0f - 2.0f * wall);
        float sCapW = (cs.slopeCap > 0.0f) ? cs.slopeCap : ts.slopeCap;
        if (sCapW < 1.0f) sCapW += wallS * (1.0f - sCapW);
        const float sCap = sCapW;
        sTgt_ = dyn::clampf (s, 0.0f, sCap);

        // ── KNEE (Round)
        kneeTgt_ = dyn::clampf (ts.kneeLo + (ts.kneeHi - ts.kneeLo) * dyn::clampf (p.b3, 0.0f, 1.0f)
                                + cs.kneeAdd, 0.0f, 54.0f);
        // fb426 — THE KNEE COLLAPSES INTO THE WALL. A soft knee at ∞:1 is a contradiction in
        // terms: the curve is still bending over the knee's whole width, so the transfer never
        // actually goes flat. Extending the R11 gate from 8 Types to 64 CELLS caught exactly
        // this on five Characters that carry a wide knee or an asymmetry — `Blue Stripe` 0.0650,
        // `Broken Bias` 0.0785, `Crystal` 0.0673 (a 14 dB knee), `Deep Floor`, `Vocal Sit` — all
        // just over the 0.05 bar and all for the same reason. Rides the SAME wallS as the
        // feedback crossover, so below Ratio 0.90 the knee is untouched and every knee
        // measurement in the roster still holds.
        kneeTgt_ *= (1.0f - wallS);

        // ── BALLISTICS. Each Type spans its OWN honest window (never a clamp plateau); a
        //    Character shifts the window, it never pins the knob dead.
        const float atkMs = dyn::expMap (dyn::clampf (p.b1, 0.0f, 1.0f),
                                         ts.atkLo * cs.atkLoW, ts.atkHi * cs.atkHiW) * cs.atkMul;
        const float relMs = dyn::expMap (dyn::clampf (p.b2, 0.0f, 1.0f),
                                         ts.relLo * cs.relLoW, ts.relHi * cs.relHiW) * cs.relMul;
        atkMs_ = std::max (atkMs, 1000.0f / fs_);          // one-sample floor
        relMs_ = std::max (relMs, 1000.0f / fs_);
        aA_ = dyn::coefTau (atkMs_ * 0.001f, fs_);
        aR_ = dyn::coefTau (relMs_ * 0.001f, fs_);
        aAfast_ = dyn::coefTau (atkMs_ * 0.0002f, fs_);    // Bus diode: ×5 faster on big overs
        // 🚨 fb425 LAW 1 — THE DUAL POOL NEVER READ THE RELEASE KNOB. These three were fixed
        // constants (0.150 / 0.150 / 2.500 s), and `RS_DUAL`'s sample loop uses ONLY them — so
        // `Release` was BIT-IDENTICALLY DEAD on the Bus Type's DEFAULT Character and on four
        // more cells. The dual pool is "one fast constant and one much slower one, whichever
        // has more reduction wins"; that is a SHAPE, not a pair of magic numbers. Both pools now
        // ride the knob and keep the 16.7:1 spacing the fixed pair had, so the mechanism is
        // unchanged and the control exists.
        aRfast_ = dyn::coefTau (relMs_ * 0.001f, fs_);                       // Bus/Vari-Mu dual pool
        aSlowA_ = dyn::coefTau (relMs_ * 0.001f, fs_);                       // ... its SLOW pool's fill rate
        aRslow_ = dyn::coefTau (std::min (25.0f, relMs_ * 0.0167f), fs_);    // ... and its recovery
        aOptoA_ = dyn::coefTau (atkMs_ * 0.001f, fs_);     // T4 charge — the Attack KNOB
        aOptoF_ = dyn::coefTau (atkMs_ <= 0.0f ? 0.06f : (relMs_ * 0.001f), fs_);  // fast pool = Release knob
        w2_     = 6.2831853f / std::max (0.001f, relMs_ * 0.001f) / fs_;           // damped-2nd rate
        // 🚨 fb425 LAW 1 — and the 2nd-order smoother read ONLY the release knob, so `Attack`
        // was BIT-IDENTICALLY DEAD on `Poise`, `Judder` and `All Buttons`. A damped 2nd-order
        // system has one natural frequency; making it ASYMMETRIC (ω_atk while the reduction is
        // growing, ω_rel while it recovers) is the standard way to give it two, and it keeps ζ
        // — the controlled overshoot wobble that IS these Characters — exactly as authored.
        // ⚠️ the floor here is ONE SAMPLE, not the 1 ms `w2_` uses: FET 76's attack window is
        //    0.02…0.8 ms and a 1 ms floor would clamp BOTH ends of the knob to the same number
        //    — which is exactly how the first version of this fix left `Attack` still dead.
        w2a_    = 6.2831853f / std::max (1.0f / fs_, atkMs_ * 0.001f) / fs_;
        zeta_   = cs.zeta;

        // ── DETECTOR. `Auto` = the Type's native ears; the axis dropdown overrides.
        // R6/fb418: `Detect` owns detection OUTRIGHT. `Native` (axis 0) defers to the Type's
        // own ears; every other entry names the rectifier. NO Character may override this —
        // a Character that silently re-pointed the detector made the card's visible label
        // disagree with the DSP, which is the fb373 failure mode.
        det_ = (p.axis == 0) ? ts.nativeDet : axisToDet (p.axis);
        float winMs = 10.0f;
        switch (det_)
        {
            case D_RMS5:   winMs = 5.0f;  break;
            case D_RMS10:  winMs = 10.0f; break;
            case D_RMS50:  winMs = 50.0f; break;
            case D_RMSWIN: winMs = dyn::expMap (dyn::clampf (p.b1, 0.0f, 1.0f), 1.0f, 80.0f)
                                   * cs.rmsWinMul; break;
            default: break;
        }
        aRms_    = dyn::coefTau (winMs * 0.001f, fs_);
        holdLen_ = (int) (fs_ * 0.005f);                   // Spike: 5 ms peak hold
        // FB topologies get a mandatory ≥ 0.1 ms one-pole on the tap (§3.4 limit-cycle clamp).
        aFb_     = dyn::coefTau (0.0001f, fs_);
        isFb_    = (ts.topo != 0);
        // ...and the crossover itself: 1 = pure feedback tap, 0 = pure feedforward (see above).
        fbMixTgt_ = isFb_ ? (1.0f - wallS) : 0.0f;
        // fb426 — F_TWOPASS AND THE WALL. `Blunt` / `Two Pass` are "the curve applied twice at
        // HALF slope", so at Ratio 100 they landed on an out-slope of ~0.5 — half the dynamic
        // range surviving at ∞:1, 15x their siblings, on the exact ruling Max made this arc
        // (100 % must be destructive, authenticity yields). The identity is the CASCADE, not the
        // politeness, so the half-slope rides the SAME wallS the feedback crossover uses: both
        // passes reach full slope only in the last 10 % of Ratio, and below 0.90 nothing moves.
        twoSTgt_ = 0.5f + 0.5f * wallS;

        // ── HEAR CUT — a 1-pole HP in the DETECTOR ONLY. The compressor stops HEARING lows,
        //    so bass stops pumping the whole patch. Audio path untouched.
        hcHz_ = (p.b4 <= 0.002f) ? 0.0f : dyn::expMap (p.b4, 20.0f, 500.0f);
        if (cs.hearCutMin > 0.0f) hcHz_ = std::max (hcHz_, cs.hearCutMin);
        hcA_  = dyn::coefHz (hcHz_, fs_);
        // detector HF tilt (Opto's frequency dependence / Ride's Bright Bias)
        tiltAmt_ = cs.tiltDb;
        tiltA_   = dyn::coefHz (2000.0f, fs_);

        edge_  = (dyn::clampf (p.b5, 0.0f, 1.0f) - 0.5f) * 2.0f;
        latchN_ = (int) (fs_ * dyn::clampf (p.b6, 0.0f, 1.0f) * 0.250f);
        tieTgt_ = (cs.linkForce >= 0.0f) ? cs.linkForce : dyn::clampf (p.b7, 0.0f, 1.0f);
        heatTgt_ = dyn::clampf (std::max (dyn::clampf (p.b8, 0.0f, 1.0f), cs.heatFloor), 0.0f, 1.0f);
        if (primed_ && ts.heatKind != heatKind_) { heatKindOld_ = heatKind_; gKind_.snap (0.0f); }
        heatKind_ = ts.heatKind;
        asym_    = (ts.heatKind == H_ASYM) || (cs.flags & F_ASYM) != 0;

        liftTgt_ = dyn::clampf (p.lift, 0.0f, 1.0f) * 24.0f;
        mixTgt_  = dyn::clampf (p.mix,  0.0f, 1.0f);

        // ── upward lane (Ride only) — §3.6 + the −45 dBp silence gate
        upOn_ = (ts.upward != 0) && ((cs.flags & F_DNONLY) == 0);
        dnOn_ = (ts.upward == 0) || ((cs.flags & F_UPONLY) == 0);
        // 🔑 THE FIRST DRAFT WAS TIMID HERE AND THE HARNESS CAUGHT IT (FINDINGS §3). The bible's
        // T_up = T − 18 dB with a fixed 3:1 upward slope and a −45 dBp gate lifted a −40 dBp
        // probe by 0.53 dB — a control you cannot hear, on the one Type whose entire identity is
        // that quiet things come back UP. R11: the maximum is where it stops being useful.
        // T_up now sits 6 dB under the downward threshold (both jaws close on a NARROW window,
        // which is what single-band levelling means) and the upward slope rides the SAME ratio
        // law as the downward one, so Ratio 100 drives both to ∞ and the device plays at ONE
        // level. Measured lift on that probe: 10.5 dB.
        tUp_  = tTgt_ - 6.0f + cs.upThrOff;
        sUp_  = std::pow (rk, 0.85f) * 0.95f * cs.upSlopeMul;
        upCap_ = cs.upCap;

        // ── auto makeup, from the STATIC CURVE at the CHORD (fb249). Never from a live output
        //    tracker — that lifts the floor between notes (the Diode-2 Auto trap).
        const bool autoFull = (cs.flags & F_AUTOFULL) != 0;
        if (pr_.autoMakeup || autoFull)
            mkTgt_ = std::min (24.0f, (autoFull ? 1.0f : 0.7f)
                                      * dyn::grDown (dyn::kChordDbp, tTgt_, sTgt_, kneeTgt_));
        else
            mkTgt_ = 0.0f;

        varMuK_ = cs.grSlopeK + ts.grSlopeK;
        const int newShape = (cs.relShape >= 0) ? cs.relShape : ts.relShape;
        // 🚨 THE CLICK THAT SHIPPED IN THE FIRST DRAFT (FIXES.md §1 COMPRESS 1).
        // RS_EXP/RS_ADAPT write the live GR into `gr_`; RS_DUAL reads `grF_/grS_`, RS_OPTO reads
        // `grF_/grS_/mem_`, RS_DAMPED reads `y2_/v2_`. Change Type or Character and the NEWLY
        // SELECTED smoother starts from whatever it held last time it ran — zero on a fresh
        // instance — so 10.96 dB of gain reduction collapsed to 0.00 inside ONE BLOCK: a
        // +11 dB step, mid-note. The state is the SAME PHYSICAL QUANTITY in every shape, so
        // the fix is to seed the new one from the live one. Gated over all 8x8 Type and all
        // 8x8 Character transitions, on program material, at five switch phases.
        if (primed_ && newShape != relShape_) seedShape (newShape);
        relShape_ = newShape;
        deepRel_  = (cs.flags & F_DEEPREL) != 0;
        kneeAuto_ = (cs.flags & F_KNEEAUTO) != 0;
        plateau_  = (cs.flags & F_PLATEAU) != 0;
        twoPass_  = (cs.flags & F_TWOPASS) != 0;
        msDet_    = (cs.flags & F_MSDET) != 0;
        clipOn_   = (ts.clipCatch != 0) || (cs.flags & F_CLIPON) != 0;
        // The overshoot NET. `DelayEngine::softClip` is linear below ±1.4 and tanh-bounded
        // above, so scaling by (ceiling + 1 dB)/1.4 puts the knee exactly 1 dB over the ceiling
        // — 1.4 full scale is 47 dB above a −54 dBFS ceiling and would never have engaged.
        // This is the ZERO-LATENCY price made bounded instead of hoped for.
        // ⚠️ derived from the LIVE glided threshold/lift/makeup in the sample loop, not from the
        // targets: `Loud War` switches auto-makeup on, which moves the target 12 dB while the
        // applied makeup crawls to it over 300 ms — so a ceiling built from the TARGET ran 12 dB
        // ahead of the gain it is supposed to catch, and the clip engaged or disengaged in the
        // wrong place. Measured at the switch: 3.97 dB of level moved in one millisecond.
        clipHeadDb_ = 1.0f + dyn::kBusNomDb;
        leaky_    = (cs.flags & F_LEAKY) != 0;
        lineAtk_  = (cs.flags & F_LINEATK) != 0;
        optoMem_  = cs.memMul;
        // `Even Pools` shifts the mix toward the SLOW pool, which is what makes the two-stage
        // release audible at all (the first draft moved it toward the FAST pool — i.e. LESS
        // two-stage — and the character measured 0.76× JND from the default).
        optoMixF_ = ((cs.flags & F_EVENPOOL) != 0) ? 0.28f : 0.55f;

        if (!primed_)
        {
            gPush_.snap (tTgt_); gLift_.snap (liftTgt_); gSlope_.snap (sTgt_);
            gKnee_.snap (kneeTgt_); gTie_.snap (tieTgt_); gHeat_.snap (heatTgt_);
            gMix_.snap (mixTgt_);  gHc_.snap (hcA_);      mkZ_ = mkTgt_;
            gUpOn_.snap (upOn_ ? 1.0f : 0.0f); gClip_.snap (clipOn_ ? 1.0f : 0.0f);
            gKind_.snap (1.0f); gAsym_.snap (asym_ ? 1.0f : 0.0f);
            gLeaky_.snap (leaky_ ? 1.0f : 0.0f); gPlat_.snap (plateau_ ? 1.0f : 0.0f);
            gTwoP_.snap (twoPass_ ? 1.0f : 0.0f); gKnAu_.snap (kneeAuto_ ? 1.0f : 0.0f);
            gMs_.snap (msDet_ ? 1.0f : 0.0f); gDnOn_.snap (dnOn_ ? 1.0f : 0.0f);
            gTilt_.snap (tiltAmt_); gVarMu_.snap (varMuK_); gOptoF_.snap (optoMixF_);
            gEdge_.snap (edge_); gFbMix_.snap (fbMixTgt_); gTwoS_.snap (twoSTgt_);
            heatKindOld_ = heatKind_;
            primed_ = true;
        }
        fillKnee();
    }

    // ═════════════════════════════════════════════════════════════════════════
    void processStereo (float* L, float* R, int n) noexcept
    {
        if (n <= 0) return;

        for (int i = 0; i < n; ++i)
        {
            const float dryL = L[i], dryR = R[i];

            // ── glides (law 4). Thresholds and makeup glide IN dB; gain is applied linear.
            const float T    = gPush_.proc (tTgt_);
            const float sBas = gSlope_.proc (sTgt_);
            const float W0   = gKnee_.proc (kneeTgt_);
            const float tie  = gTie_.proc (tieTgt_);
            const float heat = gHeat_.proc (heatTgt_);
            const float mix  = gMix_.proc (mixTgt_);
            const float lift = gLift_.proc (liftTgt_);
            const float hcA  = gHc_.proc (hcA_);
            const float upFade = gUpOn_.proc (upOn_ ? 1.0f : 0.0f);
            const float fDn    = gDnOn_.proc (dnOn_    ? 1.0f : 0.0f);
            const float fLeak  = gLeaky_.proc (leaky_  ? 1.0f : 0.0f);
            const float fPlat  = gPlat_.proc (plateau_ ? 1.0f : 0.0f);
            const float fTwo   = gTwoP_.proc (twoPass_ ? 1.0f : 0.0f);
            const float twoS   = gTwoS_.proc (twoSTgt_);            // fb426
            const float fKnAu  = gKnAu_.proc (kneeAuto_? 1.0f : 0.0f);
            const float fMs    = gMs_.proc   (msDet_   ? 1.0f : 0.0f);
            const float tiltG  = gTilt_.proc (tiltAmt_);
            const float vmK    = gVarMu_.proc (varMuK_);
            const float optoF  = gOptoF_.proc (optoMixF_);
            const float edgeG  = gEdge_.proc (edge_);
            const float clipFade = gClip_.proc (clipOn_ ? 1.0f : 0.0f);
            mkZ_ += (mkTgt_ - mkZ_) * mkA_;

            // ── DETECTOR SOURCE. Feedback topologies tap the OUTPUT, one sample late, and
            //    PRE-COLOUR (post-colour would feed the gain element's own harmonics back into
            //    its own detector — a fizz loop, §11.5).
            // fb425: a BLEND, not a switch. `fbM` is 1 on a feedback Type below Ratio 0.90
            // and travels to 0 at Ratio 1.0 (the R11 wall). It is glided, so it is also the
            // first thing in this device that makes an FF↔FB Type change continuous.
            const float fbM = gFbMix_.proc (fbMixTgt_);
            float dl = dryL + fbM * (fbZ_[0] - dryL);
            float dr = dryR + fbM * (fbZ_[1] - dryR);
            if (fMs > 1.0e-4f) { const float m = 0.7071068f * (dl + dr), s2 = 0.7071068f * (dl - dr);
                                 dl += fMs * (m - dl); dr += fMs * (s2 - dr); }

            if (hcA > 0.0f) { dl = hc_[0].proc (dl, hcA); dr = hc_[1].proc (dr, hcA); }
            if (tiltG != 0.0f)
            {
                // +N dB/oct above 2 kHz on what it HEARS: highs duck the patch (free de-esser).
                const float tl = tilt_[0].proc (dl, tiltA_), tr = tilt_[1].proc (dr, tiltA_);
                dl += tiltG * tl; dr += tiltG * tr;
            }

            float aL = std::fabs (dl), aR2 = std::fabs (dr);
            switch (det_)
            {
                case D_PEAK: break;
                case D_SPIKE:
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        float& h = peakHold_[c]; int& k = holdCnt_[c];
                        const float a = (c == 0) ? aL : aR2;
                        if (a >= h) { h = a; k = holdLen_; }
                        else if (--k <= 0) { h = a; k = 0; }
                    }
                    aL = peakHold_[0]; aR2 = peakHold_[1];
                    break;
                }
                default:   // every RMS class
                {
                    const float e0 = ms_[0].proc (aL * aL, aRms_);
                    const float e1 = ms_[1].proc (aR2 * aR2, aRms_);
                    aL  = std::sqrt (e0 < 0.0f ? 0.0f : e0);
                    aR2 = std::sqrt (e1 < 0.0f ? 0.0f : e1);
                    break;
                }
            }

            // FEEDBACK topologies get a mandatory 0.1 ms one-pole on the RECTIFIED tap (§3.4).
            // Without it the loop limit-cycles once attack < 1 sample. With it, the loop is a
            // monotone contraction: g ≤ 1 always ⇒ max static loop gain = 1.0 at zero GR, and
            // zero input ⇒ detector 0 ⇒ GR → 0 ⇒ silence. Nothing free-runs, by construction.
            // (It smooths the RECTIFIED value, not the signal — a 0.1 ms one-pole on the raw
            //  signal is a 1.6 kHz lowpass and would silently re-voice every FB Type's ears.)
            if (isFb_)
            { aL  = (fbS_[0] += (aL  - fbS_[0]) * aFb_);
              aR2 = (fbS_[1] += (aR2 - fbS_[1]) * aFb_); }

            // ── TIE (stereo link). 100 = one clamp for both (solid image, SSL law);
            //    0 = each side breathes alone (wide, lopsided). Gain-only ⇒ mono-safe either way.
            const float mx = (aL > aR2) ? aL : aR2;
            const float d0 = tie * mx + (1.0f - tie) * aL;
            const float d1 = tie * mx + (1.0f - tie) * aR2;

            float g[2];
            float grOut[2];
            for (int c = 0; c < 2; ++c)
            {
                const float dLin = (c == 0) ? d0 : d1;
                float xG = dyn::lin2db (dLin * dyn::kDetLift);      // → dBp

                // ── EDGE (the transient lane). + : the detector goes blind for the first ~3 ms
                //    of every hit, so transients escape untouched over a crushed sustain.
                //    − : attacks get +24 dB of EXTRA reduction — every note becomes a swell.
                float pTr = 0.0f;
                if (edgeG != 0.0f)
                {
                    const float m = std::fabs ((c == 0) ? dryL : dryR);
                    pf_[c] += (m - pf_[c]) * (m > pf_[c] ? pAtk_ : pRel_);
                    ps_[c] += (m - ps_[c]) * pRel_ * 0.25f;
                    // 🔑 A TRANSIENT IS A RATIO, NEVER A DIFFERENCE. The first draft used
                    // (fast − slow) normalised by the bus level: on a decaying pluck the slow
                    // envelope lags the decay for hundreds of ms, so the difference stayed large
                    // and `Edge` stopped being a transient control and became a broadband gain
                    // trim — the harness measured it running 34 dB the WRONG WAY. As a ratio it
                    // is level-independent by construction, and it goes to zero during a decay
                    // (where the slow envelope sits ABOVE the fast one) which is exactly right.
                    pTr = dyn::clampf ((pf_[c] / std::max (ps_[c], 1.0e-7f) - 1.0f) * 0.8f, 0.0f, 1.0f);
                    if (edgeG > 0.0f) xG -= 24.0f * pTr * edgeG;
                }

                // ── the static curve
                float W = W0;
                if (fKnAu > 1.0e-4f && gr_[c] < 12.0f) W += fKnAu * 26.0f * (1.0f - gr_[c] / 12.0f);
                float s = sBas;
                if (vmK != 0.0f) s = std::min (1.0f, s * (1.0f + gr_[c] * vmK));          // Vari-Mu
                if (fPlat > 1.0e-4f)                                                      // All Buttons
                {
                    const float sp = dyn::clampf (0.917f + 0.033f * std::sin (gr_[c] * 0.9f) + gr_[c] * 0.004f, 0.0f, 0.95f);
                    s += fPlat * (sp - s);
                }
                float grT = dyn::grDown (xG, T, s, W) * fDn;
                if (fLeak > 1.0e-4f && grT > 0.0f)
                {
                    // an over-limit LEAK: 6:1 above the ceiling instead of ∞ — keeps 2–3 dB of life
                    // fb425: the leak used an ABSOLUTE 0.833 slope, and `Limit`'s Ratio knob
                    // spans 0.90…1.00 — so `min()` picked the leak every time and `Ratio` was
                    // dead on `Porous`. The leak is "6:1 instead of the ∞ you asked for", i.e.
                    // a slope RELATIVE to the one that is set; expressed that way it is the
                    // same mechanism and the knob is underneath it again.
                    const float over = xG - T;
                    if (over > 0.0f) grT += fLeak * (std::min (grT, over * 0.833f * s) - grT);
                }
                if (fTwo > 1.0e-4f)
                {
                    // fb426 — this was NOT a cascade. It computed one second-pass reduction on a
                    // half-corrected input and DOUBLED it, which lands nowhere near "twice at half
                    // slope": measured out-slope 0.502 at s=1 against the 0.25 its own comment
                    // claims. A cascade is two stages, the second seeing what the first left.
                    const float sT = s * twoS;                       // half slope, full at the wall
                    const float g1 = dyn::grDown (xG,      T, sT, W) * fDn;
                    const float g2 = g1 + dyn::grDown (xG - g1, T, sT, W) * fDn;
                    grT += fTwo * (g2 - grT);
                }
                if (edgeG < 0.0f) grT += 24.0f * pTr * (-edgeG);
                grT = dyn::clampf (grT, 0.0f, 60.0f);

                // ── BALLISTICS, smoothing in the dB domain (the JAES low-ripple recommendation)
                float& GR = gr_[c];
                float aAe = aA_, aRe = aR_;
                if (relShape_ == RS_DUAL)
                {
                    // dual-pool auto release: short bursts recover fast, sustained GR recovers slow
                    float& F = grF_[c]; float& S = grS_[c];
                    F += (grT - F) * (grT > F ? aA_    : aRfast_);
                    S += (grT - S) * (grT > S ? aSlowA_ : aRslow_);   // the slow pool also FILLS slowly
                    GR = (F > S) ? F : S;
                }
                else if (relShape_ == RS_OPTO)
                {
                    // ── the T4 cell: two pools + a 10 s MEMORY integrator. The slow half
                    //    REMEMBERS how long the cell has been lit — after a long ride the tail
                    //    measurably slows. Nothing else in the device does this.
                    float& F = grF_[c]; float& S = grS_[c]; float& M = mem_[c];
                    const float tauS = (0.5f + 4.5f * std::min (1.0f, M / 6.0f)) * optoMem_;
                    const float aS   = dyn::coefTau (tauS, fs_);
                    F += (grT - F) * (grT > F ? aOptoA_ : aOptoF_);
                    S += (grT - S) * (grT > S ? aOptoA_ : aS);
                    GR = optoF * F + (1.0f - optoF) * S;
                    M += (GR - M) * memA_;
                }
                else if (relShape_ == RS_DAMPED)
                {
                    // 2nd-order damped smoother — a controlled release overshoot wobble.
                    float& v = v2_[c]; float& y = y2_[c];
                    const float a = std::min (0.4f, (grT > y) ? w2a_ : w2_);
                    v += a * (grT - y) - 2.0f * zeta_ * a * v;
                    y += a * v;
                    GR = dyn::clampf (y, 0.0f, 60.0f);
                }
                else
                {
                    if (relShape_ == RS_ADAPT && grT > GR)
                    {
                        // The Glue's documented diode trick: SMALL overs attack slowly, BIG overs
                        // attack fast. Coefficient-domain blend (an exp() per sample is not
                        // affordable) — monotonic in `over`, which is what the ear reads.
                        const float u = std::min (1.0f, (grT - GR) / 12.0f);
                        aAe = aA_ + (aAfast_ - aA_) * u;
                    }
                    if (deepRel_ && GR > 0.0f) aRe = aR_ / (1.0f + GR / 12.0f);
                    if (lineAtk_ && grT > GR)
                    {
                        // linear-in-dB attack RAMP (not RC): a punchier, squarer onset
                        const float step = 1000.0f / (atkMs_ * fs_) * 12.0f;
                        GR = std::min (grT, GR + step);
                    }
                    else GR += (grT - GR) * (grT > GR ? aAe : aRe);
                }

                // ── LATCH (hold): freeze the clamp before release runs — pump shaping and
                //    bass de-chatter. Bit-bypassed at 0.
                if (latchN_ > 0)
                {
                    if (grT >= latchHold_[c] - 1.0e-6f) { latchHold_[c] = grT; latch_[c] = latchN_; }
                    else if (latch_[c] > 0) { --latch_[c]; if (GR < latchHold_[c]) GR = latchHold_[c]; }
                    else latchHold_[c] = grT;
                }
                GR = dyn::flushd (dyn::clampf (GR, 0.0f, 60.0f));

                // ── the UPWARD lane (Ride). Feedforward ONLY — upward gain inside a feedback
                //    loop is a genuine runaway. The −45 dBp gate is a stability requirement:
                //    without it the lane free-runs on the noise floor and never dies with the note.
                float up = 0.0f;
                if (upFade > 1.0e-4f)
                    up = dyn::liftUp (xG, tUp_, sUp_, upCap_) * dyn::floorGate (xG, -55.0f, 12.0f) * upFade;
                // 🚨 fb425 LAW 1 — the upward lane used to be INSTANTANEOUS: `liftUp` off the
                // rectifier with no ballistics at all. On `Only Up` (which switches the downward
                // computer off) that left `Attack`, `Release` and `Cling` bit-identically dead,
                // and everywhere else it meant the lift snapped instead of breathing. It now
                // rides the SAME two constants, with the sign convention a levelling amplifier
                // has: the lift COMING OFF (the programme got loud) is the attack, the lift
                // coming back is the release.
                {
                    float& U = upZ_[c];
                    U += (up - U) * (up < U ? aAe : aRe);
                    up = U = dyn::flushd (U);
                }

                // ── GAIN CONTINUITY ACROSS A DISCRETE CONFIG CHANGE ─────────────────────
                // Seeding the smoothers (above) fixes the state that is the SAME quantity in
                // every shape. It does not fix the rest: the rectifier (`Detect`, and each
                // Type's native ears), the detector tilt, Vari-Mu's GR-dependent slope, the
                // plateau / two-pass / leaky / auto-knee curve variants and the M/S detector
                // basis ALL change the gain-computer's answer in one sample — and on FET 76,
                // whose attack reaches 20 µs, "the ballistics will smooth it" is false by two
                // orders of magnitude. Measured: 21.47 dB of gain moved inside one millisecond.
                // So: at the instant Type / Character / Detect changes, take the difference
                // between the gain that WAS applied and the gain the new configuration asks
                // for, and decay that offset to zero over 20 ms. The output is continuous at
                // the switch sample, the new configuration is live IMMEDIATELY (it still
                // responds to transients — this is not a freeze), and after 20 ms the offset
                // is gone. No dip, no hole, no lookahead.
                float gdb = -GR + up + lift + mkZ_;
                if (xfN_ > 0)
                {
                    const float d = gdb - gdbZ_[c];
                    gdb = gdbZ_[c] + dyn::clampf (d, -slewPS_, slewPS_);
                }
                gdbZ_[c] = gdb;
                grOut[c] = GR - up;
                g[c] = dyn::db2lin (gdb);
            }

            if (xfN_ > 0) --xfN_;

            float yl = dryL * g[0];
            float yr = dryR * g[1];

            // ── the FB tap: the OUTPUT, one sample late, PRE-COLOUR. Post-colour would feed the
            //    gain element's own harmonics into its own detector — a fizz loop (§11.5).
            if (isFb_) { fbZ_[0] = yl; fbZ_[1] = yr; }

            // ── HEAT — the gain element's nonlinearity, SCALED BY CURRENT GR. No GR ⇒ bit-clean
            //    at any Burn setting, so it cannot sound on silence and it BREATHES with the
            //    compression (the measured signature of driven hardware).
            // ── THE GAIN ELEMENT, AND THE SWAP THAT USED TO STEP ────────────────────────
            // `heatKind_` and `asym_` are discrete: switching Type or Character changed the
            // waveshaper's CURVE and engaged/disengaged the DC blocker in one sample, at
            // whatever Burn depth the outgoing Character was holding. Disabling colour()
            // entirely dropped the worst Type transition from 5.53 to 1.53 dB/ms, which is how
            // this was found. Both are now crossfaded over 20 ms: the old kind and the new kind
            // are both evaluated while `kindF` travels 0→1 (a handful of flops, and ONLY during
            // a transition), and the DC blocker runs continuously with its own fade so it can
            // never be inserted or removed as a step.
            const float kindF = gKind_.proc (1.0f);
            const float asymF = gAsym_.proc (asym_ ? 1.0f : 0.0f);
            if (heat > 1.0e-4f || asymF > 1.0e-4f)
            {
                // the drive follows a 20 ms-smoothed GR. Off the raw ballistic state it was a
                // second discontinuity in disguise: leaving a 2nd-order smoother, `gr_` closes a
                // 3 dB gap in 0.5 ms and the waveshaper's DEPTH stepped with it — a waveform
                // change no gain slew limit can catch. Hardware heats up; it does not teleport.
                const float grSm = gColG_.proc (0.5f * (gr_[0] + gr_[1]));
                const float k0 = heat * std::min (1.0f, grSm / 12.0f);
                const float k1 = k0;
                float v0 = colourK (yl, k0, grSm, heatKind_);
                float v1 = colourK (yr, k1, grSm, heatKind_);
                if (kindF < 0.9999f)
                {
                    const float o0 = colourK (yl, k0, grSm, heatKindOld_);
                    const float o1 = colourK (yr, k1, grSm, heatKindOld_);
                    v0 = o0 + (v0 - o0) * kindF;
                    v1 = o1 + (v1 - o1) * kindF;
                }
                const float d0 = dc_[0].proc (v0), d1 = dc_[1].proc (v1);
                yl = v0 + (d0 - v0) * asymF;
                yr = v1 + (d1 - v1) * asymF;
            }
            // The soft-clip CEILING was derived from the target threshold, not the glided one,
            // so a Character with a threshold offset (Pump Limit −6 dB) moved the whole ceiling
            // in a single sample. It glides now, in the linear domain, like everything else.
            const float clipL = std::max (1.0e-9f, dyn::db2lin (T + lift + mkZ_ + clipHeadDb_) * (1.0f / 1.4f));
            if (clipFade > 1.0e-4f)
            { const float ic = 1.0f / clipL;
              yl += clipFade * (clipL * dyn::softClip (yl * ic) - yl);
              yr += clipFade * (clipL * dyn::softClip (yr * ic) - yr); }

            // ── the Mix crossfade is EXACTLY linear, and the dry is the untouched input, so
            //    "Mix 100 % = fully wet, ZERO dry" is provable, not hoped: (1 − mix) = 0.
            L[i] = dryL + (yl - dryL) * mix;
            R[i] = dryR + (yr - dryR) * mix;

            // ── viz (60 Hz, block-rate feel; cheap enough to run per-sample on 3 trackers)
            const float pk = std::max (std::fabs (L[i]), std::fabs (R[i]));
            lvlZ_ += (pk - lvlZ_) * lvlA_;
            if (++vizCtr_ >= vizEvery_)
            {
                vizCtr_ = 0;
                viz_.grDb = 0.5f * (grOut[0] + grOut[1]);
                viz_.inDb = dyn::lin2db (std::max (std::fabs (dryL), std::fabs (dryR)) * dyn::kDetLift);
                viz_.outDb = dyn::lin2db (pk * dyn::kDetLift);
                viz_.lvl  = dyn::clampf (lvlZ_ / dyn::kBusNomLin * 0.5f, 0.0f, 1.0f);
            }
        }
    }

    const Viz& viz() const noexcept { return viz_; }

    // ── introspection the harness uses (NOT part of the integration surface) ──
    float attackMs()  const noexcept { return atkMs_; }
    float releaseMs() const noexcept { return relMs_; }
    float thresholdDbp() const noexcept { return tTgt_; }
    float slope()     const noexcept { return sTgt_; }
    float ratio()     const noexcept { return (sTgt_ >= 0.99995f) ? 1.0e9f : 1.0f / (1.0f - sTgt_); }
    bool  isFeedback() const noexcept { return isFb_; }
    /** R6: which rectifier is actually running. Published so the harness can prove that NOTHING
     *  but `Detect` (and, at `Native`, the Type) decides it — the Characters `RMS Ears` and
     *  `Spike Ears` used to set `detForce`, which silently overrode the dropdown: pick RMS Ears,
     *  then Detect → Peak, and the card still read `RMS Ears` while a peak detector ran. A
     *  visible label disagreeing with the DSP is the fb373 failure mode and two controls on one
     *  axis is what fb418 fixed on the flanger. */
    int   detectId() const noexcept { return det_; }
    /** LIVE gain reduction, this sample. The Viz is a 60 Hz sampler (16.7 ms) and physically
     *  cannot resolve a 20 µs attack — the first draft of the harness measured every Type's
     *  attack as "16.0 ms" for exactly that reason, which is the Viz's period, not the DSP. */
    float grNow() const noexcept { return 0.5f * (gr_[0] + gr_[1]); }

private:
    // ═════ tables ════════════════════════════════════════════════════════════
    enum Det   { D_PEAK = 0, D_RMS5, D_RMS10, D_RMS50, D_RMSWIN, D_SPIKE };
    enum RelSh { RS_EXP = 0, RS_ADAPT, RS_DUAL, RS_OPTO, RS_DAMPED };
    enum Heat  { H_GENTLE = 0, H_FET, H_ASYM, H_DECI };

    enum : uint32_t {
        F_ASYM = 1u << 0, F_PLATEAU = 1u << 1, F_UPONLY = 1u << 2, F_DNONLY = 1u << 3,
        F_AUTOFULL = 1u << 4, F_CLIPON = 1u << 5, F_MSDET = 1u << 6, F_LINEATK = 1u << 7,
        F_DEEPREL = 1u << 8, F_KNEEAUTO = 1u << 9, F_TWOPASS = 1u << 10, F_LEAKY = 1u << 11,
        F_EVENPOOL = 1u << 12
    };

    static int axisToDet (int axis) noexcept
    {
        switch (axis) { case 1: return D_PEAK; case 2: return D_RMS10;
                        case 3: return D_RMS50; case 4: return D_SPIKE; default: return D_PEAK; }
    }

    struct TypeSpec
    {
        float atkLo, atkHi, relLo, relHi;      // ms windows the knobs span (log)
        uint8_t topo;                          // 0 = feedforward, 1 = feedback
        uint8_t nativeDet;
        float kneeLo, kneeHi;
        float slopeMax;                        // 2.0 unlocks the negative zone (OverEasy)
        float slopeCap;
        float forceSlope;                      // Limit: the Ratio knob spans [forceSlope, 1]
        uint8_t relShape;
        uint8_t upward;                        // Ride
        uint8_t clipCatch;                     // Limit
        uint8_t heatKind;
        float grSlopeK;                        // Vari-Mu: slope GROWS with GR
    };

    static const TypeSpec& typeSpec (int t) noexcept
    {
        // 🚨 fb425 R11 / LAW 1 — `Round`'s WINDOW, on three Types. `Bus` topped out at 12 dB of
        // knee, `FET 76` at 12 and `Limit` at SIX, and the full 0→100 travel of the knob measured
        // 0.05…0.49 dB on 12 of their 24 cells: a knee only bends the curve within ±W/2 of the
        // threshold, so a 6 dB window is a control you cannot hear at either end. That is the
        // polite-maximum failure R11 names, and the fix is the range, not the gate — 24 dB is a
        // genuinely soft-kneed console/limiter and it is where the control stops being useful.
        // `Exact` (48 dB) is unchanged and still the widest; nothing else moved.
        //
        // Release floors, stated honestly: the bible specifies 20 ms globally and calls it a
        // stability clamp. The loop-gain analysis (§3.4) shows the constraint only binds on the
        // FEEDBACK tap, so the feedforward Types get a 5 ms floor instead — that bottom decade
        // is where the gain tracks the waveform inside its own period and the compressor becomes
        // a waveshaper. R11: a polite maximum is a failed device. The FB Types keep 20 ms and
        // the harness runs 60 s of full-drive noise on every Type to prove it.
        //           atkLo  atkHi  relLo  relHi   topo  det        knLo knHi  sMax  sCap  force  relShape  up clip heat      grK
        static const TypeSpec S[kNumTypes] = {
        /*Exact  */ { 0.05f, 300.f,   5.f, 2500.f, 0, D_PEAK,   0.f, 48.f, 1.f, 1.f,   0.f, RS_EXP,   0,0, H_GENTLE, 0.f },
        /*Bus    */ { 0.01f,  30.f, 100.f, 1200.f, 0, D_PEAK,   0.f, 24.f, 1.f, 1.f,   0.f, RS_ADAPT, 0,0, H_GENTLE, 0.f },
        /*FET 76 */ { 0.02f,   0.8f, 50.f, 1100.f, 1, D_PEAK,   0.f, 24.f, 1.f, 1.f,   0.f, RS_EXP,   0,0, H_FET,    0.f },
        /*Opto   */ { 2.0f,   50.f,  40.f,  200.f, 1, D_RMS10,  6.f, 20.f, 1.f, 0.833f,0.f, RS_OPTO,  0,0, H_ASYM,   0.f },
        /*Vari-Mu*/ { 0.2f,   50.f, 200.f,25000.f, 1, D_RMS5,   2.f, 18.f, 1.f, 1.f,   0.f, RS_EXP,   0,0, H_ASYM,   0.0556f },
        /*OverEas*/ { 1.0f,   80.f,  40.f, 2000.f, 0, D_RMSWIN, 6.f, 24.f, 2.f, 2.f,   0.f, RS_EXP,   0,0, H_DECI,   0.f },
        /*Ride   */ { 0.5f,  100.f,  20.f, 1000.f, 0, D_RMS10,  0.f, 18.f, 1.f, 1.f,   0.f, RS_EXP,   1,0, H_GENTLE, 0.f },
        /*Limit  */ { 0.1f,    5.f,  20.f,  500.f, 0, D_SPIKE,  0.f, 24.f, 1.f, 1.f,   0.9f,RS_EXP,   0,1, H_GENTLE, 0.f } };
        return S[dyn::clampi (t, 0, kNumTypes - 1)];
    }

    /** A Character is a ROW OF PHYSICS, never a tone control. Every field below re-wires the
     *  detector, the curve, the ballistic SHAPE, the loop or the gain element. */
    struct CharSpec
    {
        float atkMul, relMul;                       // × the resolved time
        float atkLoW, atkHiW, relLoW, relHiW;       // × each END of the window (moves the range)
        float kneeAdd, slopeMul, slopeCap, slopeFloor, thrOff;
        float heatFloor, tiltDb, linkForce, memMul;
        float grSlopeK, zeta, rmsWinMul, hearCutMin;
        float upSlopeMul, upThrOff, upCap;
        int   relShape;                             // NO detForce: `Detect` owns detection (R6)
        uint32_t flags;
    };

    static const CharSpec& charSpec (int t, int c) noexcept
    {
        #define CS(aM,rM, aLW,aHW,rLW,rHW, kn,sM,sC,sF,th, ht,ti,lk,mm, gk,ze,rw,hc, uS,uT,uC, rs,fl) \
            { aM,rM, aLW,aHW,rLW,rHW, kn,sM,sC,sF,th, ht,ti,lk,mm, gk,ze,rw,hc, uS,uT,uC, rs,fl }
        static const CharSpec C[kNumTypes][kNumChars] = {
        /* ── Exact — the reference ruler. The curve you set is the curve you measure. ───────── */
        { CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, 0),                 // Precise
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, F_KNEEAUTO),        // Soft Touch
          CS(1,1, 1,1,1,1,  8,0.6f,0.6f,0,0, 0,0,-1,1, 0,0.70f,1,0, 1,0,24, -1, 0),            // Loose Grip  (was RMS Ears: slope capped 2.5:1 + 8 dB of extra knee)
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, F_TWOPASS),    // Blunt       (was Spike Ears: the curve applied twice at half slope)
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, F_DEEPREL),         // Deep Release
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, F_LINEATK),         // Line Attack
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,1.00f,1,0,  1,0,24, RS_DAMPED, 0),          // Poise
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.42f,1,0,  1,0,24, RS_DAMPED, 0) },        // Judder
        /* ── Bus — the console. Diode level-adaptive attack + dual-pool auto release. ───────── */
        { CS(1,1, 1,1,1,1,  4,1,0,0,0,     0,0,1,1,   0,0.70f,1,0,  1,0,24, RS_DUAL, 0),            // Quad Bus
          CS(1,1, 1,1,2,0.667f, 4,1,0,0,0, 0,0,1,1,   0,0.70f,1,0,  1,0,24, RS_EXP, 0),             // Hand Set
          CS(3,1, 1,1,1,1,  6,0.5f,0.5f,0,2, 0,0,1,1, 0,0.70f,1,0,  1,0,24, RS_DUAL, 0),            // Two Easy
          CS(1,1, 1,1,1,1,  2,1,0,0.75f,0, 0,0,1,1,   0,0.70f,1,0,  1,0,24, RS_ADAPT, 0),           // Ten Punchy
          CS(0.03f,1, 1,1,1,1, 2,1,0,0,0,  0,0,1,1,   0,0.70f,1,0,  1,0,24, RS_ADAPT, 0),           // Fast City
          CS(2.2f,1, 1,1,1,1, 4,1,0,0,0, 0.65f,0,1,1, 0,0.70f,1,0,  1,0,24, RS_DUAL, 0),            // Big Desk
          CS(1,0.12f, 1,1,1,1, 4,1,0,0,0,  0,0,1,1,   0,0.70f,1,0,  1,0,24, RS_EXP, 0),             // Pump Bus
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,1,1,   0,0.70f,1,0,  1,0,24, RS_EXP, 0) },           // No Diode
        /* ── FET 76 — feedback, microseconds, odd harmonics. ───────────────────────────────── */
        { CS(1,1, 1,1,1,1,  0,1,0,0,0,  0.20f,0,-1,1, 0,0.70f,1,0,  1,0,24, -1, 0),                 // Blackface
          CS(1,1, 1,1,1,1,  0,1,0,0,0,  0.55f,0,-1,1, 0,0.70f,1,0,  1,0,24, -1, F_ASYM),            // Blue Stripe
          CS(1,1, 1,1,1,1, 10,1,0,0,0,  0.45f,0,-1,1, 0,0.60f,1,0,  1,0,24, RS_DAMPED, F_PLATEAU),  // All Buttons
          CS(0.15f,1, 1,1,1,1, 0,4,0.95f,0.95f,-4, 0.30f,0,-1,1, 0,0.70f,1,0, 1,0,24, -1, 0),       // Twenty Lock
          CS(1,1, 1,1,1,1,  0,0.75f,0.75f,0,0, 0.15f,0,-1,1, 0,0.70f,1,0, 1,0,24, -1, F_DEEPREL),   // Loose Four
          CS(1,1, 1,1,1,1,  0,1,0,0,0,  0.65f,0,-1,1, 0,0.70f,1,0,  1,0,24, -1, F_ASYM),            // Broken Bias
          CS(1,1, 20,20,1,1, 0,1,0,0,0, 0.20f,0,-1,1, 0,0.70f,1,0,  1,0,24, -1, 0),                 // Waiting Fet
          CS(1,1, 1,1,1,1,  0,1,0,0,0,  0.20f,0,-1,1, 0,0.70f,1,0,  1,0,24, -1, F_TWOPASS) },       // Two Pass
        /* ── Opto — two release pools and a 10 s memory. ───────────────────────────────────── */
        { CS(1,1, 1,1,1,1,  0,1,0,0,0,  0,0.35f,-1,1,  0,0.70f,1,0, 1,0,24, -1, 0),                 // Cell Classic
          CS(1,0.30f, 1,1,1,1, 0,1,0,0,0, 0,0.35f,-1,0.18f, 0,0.70f,1,0, 1,0,24, -1, 0),            // Fresh Cell
          CS(1,4.5f, 1,1,1,1, 0,1,0,0,0, 0,0.35f,-1,7.0f,  0,0.70f,1,0, 1,0,24, -1, 0),             // Tired Cell
          CS(0.25f,0.25f, 1,1,1,1, 0,1,0,0,0, 0,0.35f,-1,0.45f, 0,0.70f,1,0, 1,0,24, -1, 0),        // Quick Cell
          CS(1,1.6f, 1,1,1,1, 0,1,0,0,0, 0,0.35f,-1,2.2f, 0,0.70f,1,0, 1,0,24, -1, F_EVENPOOL),     // Even Pools
          CS(4.0f,1.8f, 1,1,1,1, 14,1,0,0,0, 0,0.35f,-1,1.6f, 0,0.70f,1,0, 1,0,24, -1, 0),          // Crystal
          CS(1,1, 1,1,1,1,  0,1,0,0,0, 0.40f,0.35f,-1,1, 0,0.70f,1,0, 1,0,24, -1, 0),               // Tube Stage
          CS(1,1, 1,1,1,1,  0,1,0,0,0,  0,7.00f,-1,1,  0,0.70f,1,0, 1,0,24, -1, 0) },               // Bright Ears
        /* ── Vari-Mu — the curve that STEEPENS as you hit it harder. ───────────────────────── */
        { CS(1,1, 1,1,1,1,  0,1,0,0,0,  0.25f,0,-1,1,  0,0.70f,1,0, 1,0,24, -1, 0),                 // Studio 670
          CS(1,1, 1,0.1f,0.5f,0.036f, 0,1,0,0,0, 0.25f,0,-1,1, 0,0.70f,1,0, 1,0,24, -1, 0),         // Time One
          CS(1,1, 4,0.4f,7.5f,0.6f,   0,1,0,0,0, 0.25f,0,-1,1, 0,0.70f,1,0, 1,0,24, -1, 0),         // Time Four
          CS(1,1, 1,1,0.4f,0.16f, 0,1,0,0,0, 0.25f,0,-1,1, 0,0.70f,1,0, 1,0,24, RS_DUAL, 0),        // Auto Peaks
          CS(1,2.0f, 1,1,10,1, 0,1,0,0,0, 0.25f,0,-1,1, 0,0.70f,1,0, 1,0,24, RS_DUAL, 0),           // Long Haul
          CS(1,1, 1,1,1,1,  0,1,0,0,0,  0.60f,0,-1,1,  0,0.70f,1,0, 1,0,24, -1, F_ASYM),            // Push Pull
          CS(1,1, 1,1,1,1,  0,1,0,0,0,  0.25f,0,0,1,   0,0.70f,1,0, 1,0,24, -1, F_MSDET),           // Lateral
          CS(1,1, 1,1,1,1,  8,1,0.75f,0,0, 0.10f,0,-1,1, -0.0556f,0.70f,1,0, 1,0,24, -1, 0) },      // Triode Soft
        /* ── OverEasy — the wide knee, and the only ratio knob that goes PAST infinity. ────── */
        { CS(1,1, 1,1,1,1,  6,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, 0),                 // Over Easy
          CS(1,1, 1,1,1,1, -24,1,1,0,0,    0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, 0),                 // Hard 160
          CS(1,1, 1,1,1,1,  6,1.43f,1,0,0, 0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, 0),                 // Infinity
          CS(1,1, 1,1,1,1,  6,1.35f,2,0,0, 0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, 0),                 // Infinity Plus
          CS(1,1.8f, 1,1,1,1, 6,1,0,0,0,   0,0,-1,1,  0,0.70f,9.0f,0, 1,0,24, -1, 0),               // Slow Window
          CS(1,1, 1,1,1,1,  6,1,0,0,0,     0,0,-1,1,  0,0.70f,0.10f,0, 1,0,24, -1, 0),              // Crush RMS
          CS(1,1, 1,1,1,1,  6,1,0,0,0,  0.40f,0,-1,1, 0,0.70f,1,0,  1,0,24, -1, 0),                 // Decilinear
          CS(1,1, 1,1,1,1,  6,2,2,2,0,     0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, 0) },               // Anti
        /* ── Ride — single-band UP and DOWN at once (the OTT boundary Type). ───────────────── */
        { CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, 0),                 // Level Rider
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1.35f,10,36, -1, 0),            // Deep Floor
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1.2f,0,30, -1, F_UPONLY),       // Only Up
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,-1,1,  0,0.70f,1,0,  1,0,24, -1, F_DNONLY),          // Only Down
          CS(0.06f,0.25f, 1,1,1,1, 0,1,0,0,0, 0,0,-1,1, 0,0.70f,1,0, 1,0,24, -1, 0),                // Fast Clamp
          CS(2.0f,4.0f, 1,1,1,1, 0,1,0,0,0,  0,0,-1,1, 0,0.70f,1,0,  1,0,24, -1, 0),                // Slow Iron
          CS(1,1, 1,1,1,1,  0,1,0,0,0,  0,3.20f,-1,1,  0,0.70f,1,0,  1.15f,0,24, -1, 0),            // Bright Bias
          CS(1,0.5f, 1,1,1,1, 0,1,0,0,0,   0,-2.0f,-1,1,  0,0.70f,1,260.f, 1,-6,24, -1, 0) },       // Vocal Sit
        /* ── Limit — the wall. ∞ slope, peak+hold, soft-clip catch, ~1 dB of eaten overshoot. ── */
        { CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,1,1,   0,0.70f,1,0,  1,0,24, -1, 0),                 // Clean Wall
          CS(1,1, 1,1,1,1,  6,1,0,0,0,     0,0,1,1,   0,0.70f,1,0,  1,0,24, -1, 0),                 // Soft Ceiling
          CS(0.02f,1, 1,1,1,1, 0,1,0,0,0,  0,0,1,1,   0,0.70f,1,0,  1,0,24, -1, 0),                 // Hard Stop
          CS(1,1, 1,1,1,4.0f, 0,1,0,0,-6,  0,0,1,1,   0,0.70f,1,0,  1,0,24, -1, 0),                 // Pump Limit
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,1,1,   0,0.70f,1,0,  1,0,24, -1, F_AUTOFULL),        // Loud War
          CS(1,1, 1,1,1,1,  0,1,0,0,-3,    0,0,1,1,   0,0.70f,1,0,  1,0,24, -1, F_CLIPON),          // Clip Guard
          CS(1,2.5f, 1,1,1,1, 0,1,0,0,0,   0,0,1,1,   0,0.70f,1,0,  1,0,24, -1, F_DEEPREL),         // Springy
          CS(1,1, 1,1,1,1,  0,1,0,0,0,     0,0,1,1,   0,0.70f,1,0,  1,0,24, -1, F_LEAKY) } };       // Porous (was `Leaky`: shipped Distortion Diode-1 character, index.html:8680 — found by the WIDENED no-doubles corpus, not by RENAMES.md)
        #undef CS
        return C[dyn::clampi (t, 0, kNumTypes - 1)][dyn::clampi (c, 0, kNumChars - 1)];
    }

    // ═════ the gain element's nonlinearity ═══════════════════════════════════
    //
    // 🔑 THE TRAP THIS SOLVES, and it cost the first draft: the obvious implementation
    // saturates the OUTPUT, which is the signal AFTER the reduction — so the deeper the
    // compression, the QUIETER the drive into the saturator, and Burn gets *weaker* exactly
    // where hardware gets dirtier. Backwards. In a real FET/tube gain element the distortion
    // comes from the control voltage pushing the device into its nonlinear region, not from
    // signal level.
    // So the saturator sees a level-restored signal (`comp` undoes 70 % of the current GR) and
    // the same factor is undone after it — the fb419 law: the makeup is INSIDE, so the stage's
    // slope at zero is EXACTLY 1 and Burn can never move the overall gain, only the curvature.
    inline float colourK (float y, float k, float grDb, int kind) const noexcept
    {
        if (k <= 1.0e-5f) return y;
        const float comp = dyn::db2lin (grDb * 0.7f);
        const float inv  = dyn::kBusNomLin / comp;
        const float u    = y * comp * (1.0f / dyn::kBusNomLin);
        float sv;
        switch (kind)
        {
            case H_FET:  sv = dyn::fastTanh (u * 4.0f)  * 0.25f;                    break;  // hard, odd
            case H_ASYM: { const float a = u * 2.5f + 0.9f * u * std::fabs (u);
                           sv = dyn::fastTanh (a) * 0.4f; }                         break;  // H2-dominant
            case H_DECI: { const float m = std::fabs (u) * 3.0f;                            // log-domain error
                           sv = (u < 0.0f ? -1.0f : 1.0f) * std::log1p (m) * (1.0f / 3.0f); } break;
            default:     sv = dyn::fastTanh (u * 1.2f) * (1.0f / 1.2f);             break;  // gentle H3
        }
        return y + k * (sv * inv - y);
    }

    /** Hand the live gain reduction to whichever smoother is about to run. Called ONLY on a
     *  shape change, from setParams (message/audio block edge), never per sample. */
    void seedShape (int newShape) noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            const float g = gr_[c];
            grF_[c] = g; grS_[c] = g;          // RS_DUAL / RS_OPTO pools
            y2_[c]  = g; v2_[c]  = 0.0f;       // RS_DAMPED position + velocity (start at rest)
            if (newShape == RS_OPTO && mem_[c] < g) mem_[c] = g;   // T4 memory: at least this lit
        }
    }

    void fillKnee() noexcept
    {
        for (int i = 0; i < kKnee; ++i)
        {
            const float x = -60.0f + 72.0f * (float) i / (float) (kKnee - 1);   // dBp
            float o = x - (dnOn_ ? dyn::grDown (x, tTgt_, sTgt_, kneeTgt_) : 0.0f);
            if (upOn_) o += dyn::liftUp (x, tUp_, sUp_, upCap_) * dyn::floorGate (x, -55.0f, 12.0f);
            viz_.knee[i] = o + liftTgt_ + mkTgt_;
        }
    }

    // ═════ state ═════════════════════════════════════════════════════════════
    Params pr_;
    Viz    viz_;
    float  fs_ = 48000.0f;

    dyn::HP1        hc_[2], tilt_[2];
    dyn::MeanSquare ms_[2];
    dyn::DCBlock    dc_[2];
    dyn::Glide      gPush_, gLift_, gSlope_, gKnee_, gTie_, gHeat_, gMix_, gHc_, gUpOn_, gClip_;
    dyn::Glide      gKind_, gAsym_, gColG_, gFbMix_, gTwoS_;
    dyn::Glide      gLeaky_, gPlat_, gTwoP_, gKnAu_, gMs_, gDnOn_, gTilt_, gVarMu_, gOptoF_, gEdge_;
    int             heatKindOld_ = H_GENTLE;

    float fbZ_[2] {}, fbS_[2] {}, peakHold_[2] {}, gr_[2] {}, grF_[2] {}, grS_[2] {}, mem_[2] {};
    float v2_[2] {}, y2_[2] {}, pf_[2] {}, ps_[2] {}, latchHold_[2] {}, upZ_[2] {};
    float gdbZ_[2] {}, slewPS_ = 0.03125f;
    int   prevType_ = -1, prevChar_ = -1, prevAxis_ = -1, xfN_ = 0, xfLen_ = 1920;
    int   holdCnt_[2] {}, latch_[2] {};

    float tTgt_ = 9.0f, sTgt_ = 0.5f, kneeTgt_ = 6.0f, tieTgt_ = 1.0f, heatTgt_ = 0.0f, fbMixTgt_ = 0.0f;
    float twoSTgt_ = 0.5f;                                   // fb426 — F_TWOPASS slope multiplier
    float liftTgt_ = 0.0f, mixTgt_ = 1.0f, mkTgt_ = 0.0f, mkZ_ = 0.0f;
    float tUp_ = -9.0f, sUp_ = 0.0f, upCap_ = 24.0f;
    float atkMs_ = 10.0f, relMs_ = 250.0f;
    float aA_ = 0.1f, aR_ = 0.01f, aAfast_ = 0.5f, aRfast_ = 0.01f, aRslow_ = 0.001f;
    float aOptoA_ = 0.1f, aOptoF_ = 0.01f, aRms_ = 0.01f, aFb_ = 0.5f, aSlowA_ = 0.01f;
    float w2_ = 0.01f, w2a_ = 0.01f, zeta_ = 0.7f, varMuK_ = 0.0f, optoMem_ = 1.0f, optoMixF_ = 0.55f;
    float hcHz_ = 0.0f, hcA_ = 0.0f, tiltAmt_ = 0.0f, tiltA_ = 0.2f, clipHeadDb_ = 1.0f;
    float edge_ = 0.0f, pAtk_ = 0.0064f, pRel_ = 0.00026f, lvlA_ = 0.001f, lvlZ_ = 0.0f, mkA_ = 0.001f, memA_ = 1.0e-5f;
    int   det_ = D_PEAK, relShape_ = RS_EXP, heatKind_ = H_GENTLE;
    int   holdLen_ = 240, latchN_ = 0, vizEvery_ = 800, vizCtr_ = 0;
    bool  isFb_ = false, upOn_ = false, dnOn_ = true, deepRel_ = false, kneeAuto_ = false;
    bool  plateau_ = false, twoPass_ = false, msDet_ = false, clipOn_ = false, leaky_ = false;
    bool  lineAtk_ = false, asym_ = false, primed_ = false;
};

} // namespace tw
