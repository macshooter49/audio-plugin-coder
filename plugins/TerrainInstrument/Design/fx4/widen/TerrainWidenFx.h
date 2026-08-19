#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  TerrainWidenFx.h — fb420. ONE instance of the FX-rack WIDEN device (chain kind 10).
//
//  Header-only, pure C++ (no JUCE), tw:: namespace, the locked Design/fx4/CONTRACT.md §2
//  interface. Offline-certified by widen_cert.cpp, which lives next to this file.
//
//  ── WHAT WIDEN IS, IN THIS FILE ──────────────────────────────────────────────
//  N COPIES OF THE INPUT that differ in PITCH, in TIME, and in STEREO PLACEMENT.
//  Not a chorus. The shipped Chorus (chain kind 6) is ONE audible cyclic voice pair;
//  this is a CROWD, and every Type here either runs >= 4 simultaneous copies or has no
//  cyclic voice at all (Shift is static, Blur is phase-only, Bands is spectral).
//
//  ── THE TWO LINEAGES MAX ASKED FOR (HYPER-BUILD-BIBLE §1.2-1.3) ──────────────
//  HYPER  = Roland JP-8000 Super Saw (1996) as an AUDIO effect: detuned voices
//           oscillating sharp/flat in pitch, uneven fan, measured mix law.  -> `Stack`
//  DIMENSION = Roland SDD-320 Dimension D (1979): a BBD chorus wired so it does NOT
//           sound like one — TRIANGLE LFO in ANTIPHASE across the channels, and the
//           delayed signal cross-mixed to the OTHER channel with OPPOSITE POLARITY.
//           -> `Twin`  (name: `Dimension` is Serum's own menu string, CONTRACT R3;
//            `Cross` is already a Route option on the shipped flanger — no doubles.)
//
//  🔑 THE TRIANGLE IS NOT NEGOTIABLE (bible §1.2 item 3, verbatim Arturia).
//     A triangle has CONSTANT |slope| ⇒ a CONSTANT pitch offset whose SIGN FLIPS at the
//     apexes. That reads as DETUNE, not vibrato, and it is THE Dimension tell. A
//     trapezoid with flat tops has ZERO slope on the flats = ZERO detune, so the detune
//     would periodically DROP OUT — a tri-modal pitch histogram with a big zero spike
//     instead of the bimodal +-c signature. The ONLY legitimate slew work here is the
//     ~1.5 ms apex smoothing below (a slope STEP is a tick). §D of the cert proves the
//     histogram is bimodal, and the `Wobble` Character (triangle -> sine) is the A/B that
//     shows what a zero-slope apex costs.
//
//  🔑 THE CONSTANT-CENTS LAW (bible §3.1 — "the single most important equation").
//     A delay modulated by d(t) = A*sin(2*pi*f*t) shifts pitch by its SLOPE:
//         ratio(t) = 1 - d'(t),  peak cents = 1200*log2(1 + 2*pi*f*A)
//     Serum exposes A and f RAW, so turning Rate up doubles the cents at the same Detune —
//     their knob lies. Here the KNOB IS CENTS and the depth is solved for:
//         A = (2^(c/1200) - 1) / (2*pi*f)        [sine]
//         A = (2^(c/1200) - 1) / (4*f)           [triangle: |d'| = 4*A*f]
//     🔧 AND WE DIVERGE FROM THE BIBLE HERE, DELIBERATELY. §3.1 clamps A <= 0.9*base_v,
//     which at the default 0.3 Hz caps the 9.7 ms voice at 28 cents — the Detune knob
//     would die a third of the way up at every useful Rate, which is a dead knob (law 1)
//     AND a failed R11 ceiling. We do the opposite: THE BASE GROWS TO THE CENTS.
//         base_eff = max(base_v * Offset, A/0.85 + 1.5 ms)
//     so the read head still never approaches the write head, the cents stay HONEST at
//     every Rate, and the only cost is that slow+deep settings sit further back in time
//     (which is what a doubler does anyway). A is hard-capped at 180 ms; above that the
//     achieved cents are published in viz().voiceCents and fall honestly.
//
//  ── HOUSE LAWS HONOURED STRUCTURALLY ─────────────────────────────────────────
//  * MIX 1.0 = FULLY WET, ZERO DRY — equal power, dry gain is cos(pi/2), exactly 0.
//  * NO CLICKS — every continuous param one-poles at 18 ms; every DELAY LENGTH glides
//    (comb-click law); Type/Character/Field switches fade-swap-recover (8 ms dip ->
//    swap+reset -> 40 ms recover); Voices fade in/out over 30 ms.
//  * ZERO LATENCY, ZERO LOOKAHEAD — structural, and RACK-WIDE: the main-send exclusion
//    sums subtract the routed dry SAMPLE-ALIGNED. Never report latency from this device.
//    (This is also why the phase-vocoder `PV Glass` Character in bible §2.3 is CUT: its
//    32 ms is uncompensatable and turns a micro-double into a slapback.)
//  * NOTHING FREE-RUNS — the feedback loop input is multiplied by env/(env+0.003).
//  * NO ALLOCATION anywhere reachable from processStereo. prepare() allocates; reset()
//    never does. (fb415 caught a malloc on the audio thread from exactly this shape.)
//  * NO OVERSAMPLING — every path is linear time-VARIANT (delays, allpasses, gains).
//    LTV makes sidebands, not harmonic aliasing stacks; the only nonlinearity is the
//    in-loop tanh on a -26 dBFS bus.
//  * MONO IS MEASURED, NEVER ASSUMED. `L = dry+wet, R = dry-wet` folds to `2*dry` and the
//    wet cancels COMPLETELY (CONTRACT law 5). Every Type and every Field option is folded
//    and measured in the cert; the two mono-hostile Fields and the two mono-hostile
//    Characters are TAGGED by fieldIsMonoHostile()/charIsMonoHostile(), never hidden.
//  * R11 — NO CEILING. Width 1.0 is a full 90-degree M/S rotation: mid = 0, side = sqrt2*S.
//    The wet is then PURE SIDE and a mono fold-down DELETES it. That is not a bug, it is
//    the top of the knob: width goes PAST mono-destruction, by construction, and §R of the
//    cert gates it with substance (corr <= -0.9 AND mono fold <= -25 dB AND the stereo
//    output still >= -12 dB of the Width 0.5 reference, so silence cannot pass the gate).
// ═══════════════════════════════════════════════════════════════════════════════

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace tw {

class TerrainWidenFx
{
public:
    // ── identity ─────────────────────────────────────────────────────────────
    static constexpr int kNumTypes = 6;
    static constexpr int kNumChars = 8;
    // ⚠️ fb342 birth-cardinality law: an AudioParameterChoice's option count is fixed at
    // construction and state-format-breaking to grow later. DECLARE SYN_WID_TYPE AT
    // kNumTypeSlots (6 live + 2 reserved, greyed in the UI, clamped by setParams).
    static constexpr int kNumTypeSlots = 8;
    static constexpr int kNumFields    = 6;      // back dropdown 2 — the `axis` slot
    static constexpr int kMaxVoices    = 8;

    static const char* const* typeNames() noexcept
    {
        static const char* const n[kNumTypes] =
        { "Stack", "Twin", "Shift", "Double", "Blur", "Bands" };
        return n;
    }

    static const char* const* charNames (int type) noexcept
    {
        static const char* const n[kNumTypes][kNumChars] =
        {   // Stack — the JP-8000 crowd
            { "JP Classic", "Even Fan", "Analog Drift", "Tight", "Wide Fan",
              "Octave Bloom", "Sub Anchor", "Three Phase" },
            // Twin — the SDD-320 antiphase pair
            { "Duo", "Quad", "Mode Two", "Mode Three", "No Compander",
              "Dark BBD", "Wobble", "Hex" },
            // Shift — the static cents fan
            { "Silk", "Punch", "Warble", "Fifth Up", "Down Double",
              "Wide Slap", "Gritty", "Octave Pair" },
            // Double — discrete voices, aperiodic walk
            { "Vocal", "Wide Room", "Tape ADT", "Tight Inst", "Loose Crowd",
              "Static Twins", "Slapback", "Seasick" },
            // Blur — dual allpass cascades, magnitude-flat by construction
            { "Smooth Six", "Deep Twelve", "Velvet", "Low Anchor", "Air Only",
              "Seed B", "Seed C", "Counter" },
            // Bands — complementary band alternation
            { "Coarse", "Fine", "Tilted", "Rotor Slow", "Rotor Fast",
              "Guard", "Low Split", "Hard Split" }
        };
        return n[type < 0 ? 0 : (type >= kNumTypes ? kNumTypes - 1 : type)];
    }

    // Back dropdown 2. NOT `Type` (CONTRACT R6 — `Type` is the header pill and fb418
    // removed exactly this duplicate from all three fx3 devices). This axis is the
    // PLACEMENT LAW: what stereo matrix the finished wet is poured through. Every option
    // is a different matrix, i.e. different PHYSICS, never a tone.
    static const char* const* fieldNames() noexcept
    {
        static const char* const f[kNumFields] =
        { "Direct", "Alternate", "Orbit", "Swap", "Side Only", "Collapse" };
        return f;
    }

    static_assert (kNumTypes > 0 && kNumChars == 8, "roster/table must move together");

    // ── the mono manifest. SHIPPED LABELLED, never hidden ("no playing safe"), and the
    //    cert measures how bad each one is rather than trusting this table.
    //    Field 4 `Side Only` forces mid = 0: a mono fold DELETES the wet, by definition.
    //    Field 2 `Orbit` sweeps THROUGH antiphase once per orbit — hostile at the moment
    //    it passes, which is the whole sound; tagged so the card can warn.
    static bool fieldIsMonoHostile (int field) noexcept
    { return field == 4 || field == 2; }

    //  Twin/`Hex`      — cross-mix coefficient pushed past 1.0: the wet mid inverts.
    //  Blur/`Counter`  — path B's allpass coefficients are negated, so the two cascades
    //                    sit near phase opposition and the mono fold notches deeply.
    static bool charIsMonoHostile (int type, int chr) noexcept
    { return (type == 1 && chr == 7); }        // Twin/`Hex` only.
    // 🔧 Blur/`Counter` WAS tagged here on the reasoning that negated path-B coefficients
    // sit near phase opposition. MEASURED, it is the opposite: its mono deviation is 2.16 dB
    // against `Smooth Six`'s 3.54 dB — negating the coefficients moves the break frequencies'
    // phase sign, which is a different room, not a more hostile one. The tag was a guess and
    // the measurement removed it. (§J prints both numbers.)

    // ── TYPE-LEVEL mono manifest. Three of the six mechanisms cost mono energy BY
    //    CONSTRUCTION and the card must be able to say so:
    //      Twin   — the inverted cross-mix SUBTRACTS the channels. That subtraction IS the
    //               width; a fold-down cancels (1-k) of the wet mid. Measured -11.6 dB at
    //               Mix 1.0 / Amount 0.7. The real SDD-320 is never run at 100 % wet.
    //      Double — discrete 17..61 ms copies comb in mono; the walk moves the notches but
    //               not fast enough to average out inside a listening window.
    //      Blur   — two allpass cascades sum to a comb. DAFx-24 measures dual-allpass mono
    //               ripple at "1 to 2 dB" with mild settings; ours runs up to 18 stages at
    //               0.42 octaves of divergence because R11 asks for the extreme, and the
    //               ripple grows with it.
    //    The other three are mono-safe and it is worth knowing WHY: Stack's combs MOVE and
    //    average out, Shift's copies are static but tiny, and Bands reconstructs EXACTLY.
    static bool typeIsMonoLossy (int type) noexcept
    { return type == 1 || type == 3 || type == 4; }

    // ── tempo sync: the 20-entry list cloned WHOLE from PluginProcessor.cpp:3479, "Free"
    //    at index 0 included, identical to all three fx3 devices (CONTRACT §4).
    //    ⚠️ SHIPPED OFF FOR v1 (bible §11 Q6, decided): the house table spans 4 bar ->
    //    1/256 = 0.125..128 Hz at 120 BPM, and above ~15 Hz an LFO on a delay line stops
    //    being a widener and becomes audible FM. The plumbing is here and correct; the
    //    UI does not expose it. tempoSync=true is honoured and CLAMPED into the device's
    //    0.03..14 Hz range so a preset that sets it can never make FM by accident.
    static constexpr int kNumDivs = 20;
    static const char* const* divNames() noexcept
    {
        static const char* const d[kNumDivs] =
        { "Free","4 bar","2 bar","1 bar","1/2","1/2D","1/2T","1/4","1/4D","1/4T",
          "1/8","1/8D","1/8T","1/16","1/16D","1/16T","1/32","1/64","1/128","1/256" };
        return d;
    }
    static float divBeats (int i) noexcept
    {
        static const float B[kNumDivs] = { 0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 3.0f, 1.3333f, 1.0f,
                                           1.5f, 0.6667f, 0.5f, 0.75f, 0.3333f, 0.25f, 0.375f,
                                           0.1667f, 0.125f, 0.0625f, 0.03125f, 0.015625f };
        return B[i < 0 ? 0 : (i >= kNumDivs ? kNumDivs - 1 : i)];
    }

    // ── the locked Params block (CONTRACT §2) ────────────────────────────────
    //  f1 = amount   f2 = width   f3 = rate      (FRONT 3 heroes, named per device)
    //  axis          = `field`     (BACK DROPDOWN 2)
    //  BACK 8: b1 Voices · b2 Spread · b3 Offset · b4 Wander
    //          b5 Low Keep · b6 Tone · b7 Feedback · b8 Balance
    struct Params
    {
        int   type = 0, character = 0;
        int   field = 0;                    // == the locked `axis` slot
        float amount = 0.35f;               // f1 — relabelled per Type in the UI
        float width  = 0.50f;               // f2 — 0.5 is EXACTLY neutral (theta = 45 deg)
        float rate   = 0.35f;               // f3
        float mix    = 0.50f;
        float b1=0.50f, b2=0.85f, b3=0.50f, b4=0.00f,
              b5=0.00f, b6=0.50f, b7=0.00f, b8=0.50f;
        bool  tempoSync = false; double bpm = 120.0;
        int   syncDiv   = 10;               // only read when tempoSync
        // FRONT PILL. Integration wires the rising edge to note-on. The read POSITION is
        // slew-capped (bible §2.1 correction) so the zap sounds identical at every Detune.
        bool  retrig = false;
    };

    // CONTRACT §2 — the exact per-device Viz, no more, no less.
    struct Viz
    {
        float corr = 1.0f;                  // -1..+1 running stereo correlation of the OUTPUT
        float voicePan[kMaxVoices]  {};     // -1..+1 live placement of each copy
        float voiceCents[kMaxVoices]{};     // live pitch offset of each copy, cents
        float widthNow = 0.0f;              // 0..1 measured side/(mid+side) of the wet
        float lvl = 0.0f;                   // 0..1 wet level
    };

    // ═════════ lifecycle ═════════════════════════════════════════════════════
    void prepare (double sampleRate, int /*maxBlock*/) noexcept
    {
        fs_ = (float) (sampleRate > 1000.0 ? sampleRate : 48000.0);

        // SIZE FROM THE WORST READ, NEVER FROM THE NOMINAL BASE.
        //   worst base_eff = kMaxBaseMs (200) * Offset 2.5 -> capped at kMaxBaseMs
        //   + worst depth   = kMaxDepthMs (180)
        //   + Shift's granular span (<= 70 ms) + interpolator guard
        // 460 ms with the hard clamp in readH() cannot wrap.
        // 🔧 CPU, MEASURED: this was 0.470 s = 256 kB of ring per instance, and 16
        // scattered Hermite reads per sample across that footprint is CACHE-bound, not
        // flop-bound (Shift measured 23.5 us/block for ~200 flops/sample). Capping the base
        // at 0.130 s halves the working set. The price is stated: at the very bottom of the
        // Rate range the 130-cent target is no longer reachable (the depth clamps and the
        // achieved cents fall to ~32), and that fall is published in viz().voiceCents.
        int need = (int) std::ceil (0.320f * fs_) + 8;
        int sz = 1024; while (sz < need) sz <<= 1;
        bufL_.assign ((size_t) sz, 0.0f);
        bufR_.assign ((size_t) sz, 0.0f);
        bufN_ = sz; mask_ = sz - 1;

        smK_   = 1.0f - std::exp (-1.0f / (0.018f * fs_));   // 18 ms — the house ramp
        envK_  = 1.0f - std::exp (-1.0f / (0.020f * fs_));   // input presence
        lvlK_  = 1.0f - std::exp (-1.0f / (0.060f * fs_));
        corrK_ = 1.0f - std::exp (-1.0f / (0.050f * fs_));
        vfK_   = 1.0f - std::exp (-1.0f / (0.030f * fs_));   // voice fade in/out
        dipDn_ = 1.0f - std::exp (-1.0f / (0.005f * fs_));   // 5 ms down to -46 dB (~26 ms)
        dipUp_ = 1.0f - std::exp (-1.0f / (0.040f * fs_));   // 40 ms recover
        apexK_ = 1.0f - std::exp (-1.0f / (0.0015f * fs_));  // 1.5 ms triangle apex round
        cmpK_  = 1.0f - std::exp (-1.0f / (0.005f * fs_));   // compander rectifier
        expK_  = 1.0f - std::exp (-1.0f / (0.080f * fs_));   // expander release
        dcR_   = 1.0f - (6.2831853f * 10.0f / fs_);          // 10 Hz loop DC blocker

        peA0_  = onePoleA (700.0f);      // the SDD-320 pre/de-emphasis corner
        bassA_ = onePoleA (150.0f);      // the documented dry-bass compensation corner
        altA_  = onePoleA (700.0f);      // Field `Alternate` crossover

        reset();
        recalc();
    }

    void reset() noexcept
    {
        std::fill (bufL_.begin(), bufL_.end(), 0.0f);
        std::fill (bufR_.begin(), bufR_.end(), 0.0f);
        wr_ = 0;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            ph_[v] = (float) v / (float) kMaxVoices + 0.037f * (float) ((v * 7) % 5);
            if (ph_[v] >= 1.0f) ph_[v] -= 1.0f;
            triZ_[v] = 0.0f; q_[v] = 0.0f; res_[v] = 0.0f; prevD_[v] = -1.0f;
            wk_[v].st = 0.0f; wk_[v].tg = 0.0f; wk_[v].ph = 0.113f * (float) (v + 1);
            wkp_[v].st = 0.0f; wkp_[v].tg = 0.0f; wkp_[v].ph = 0.071f * (float) (v + 3);
            vfd_[v] = (v == 0 ? 1.0f : 0.0f);
        }
        orbit_ = 0.0f; rotPh_ = 0.0f;
        for (int k = 0; k < kMaxAP; ++k) { apA_[k].x = apA_[k].y = 0.0f; apB_[k].x = apB_[k].y = 0.0f; apCa_[k] = apCaT_[k]; apCb_[k] = apCbT_[k]; }
        for (int k = 0; k < kMaxBands; ++k) bz_[k] = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            lkZ_[c] = 0.0f; toneZ_[c] = 0.0f; fbZ_[c] = 0.0f; fbSt_[c] = 0.0f;
            dcX_[c] = dcY_[c] = 0.0f; peZ_[c] = deZ_[c] = 0.0f; darkZ_[c] = 0.0f;
            cmpE_[c] = expE_[c] = 0.0f; altZ_[c] = 0.0f; bassZ_[c] = 0.0f;
        }
        envIn_ = 0.0f; lvlSm_ = 0.0f; dip_ = 1.0f;
        cLL_ = cRR_ = 1.0e-9f; cLR_ = 0.0f;
        pendType_ = -1; pendChar_ = -1; pendField_ = -1;
        rng_ = 0x9E3779B9u; lastRetrig_ = false; seeded_ = false;
        viz_ = Viz{};
    }

    void setParams (const Params& p) noexcept
    {
        const int t = clampi (p.type,      0, kNumTypes  - 1);
        const int c = clampi (p.character, 0, kNumChars  - 1);
        const int f = clampi (p.field,     0, kNumFields - 1);

        if (t != type_ || c != char_ || f != field_)
        {
            if (! seeded_) { type_ = t; char_ = c; field_ = f; }   // first call: adopt, no dip
            else { pendType_ = t; pendChar_ = c; pendField_ = f; } // else fade-swap-recover
        }
        if (p.retrig && ! lastRetrig_) fireRetrig();
        lastRetrig_ = p.retrig;

        p_ = p;
        recalc();                       // PER BLOCK, never per sample (CONTRACT §2)
    }

    // ── the block. IN-PLACE, wet+dry per Mix. ────────────────────────────────
    void processStereo (float* L, float* R, int n) noexcept
    {
        if (L == nullptr || R == nullptr || n <= 0) return;

        if (! seeded_)
        {   // snap the smoothers so the first block is correct, not a ramp from zero
            amtSm_ = amtTg_; widSm_ = widTg_; rateSm_ = rateTg_; mixSm_ = mixTg_;
            sprSm_ = sprTg_; offSm_ = offTg_; wanSm_ = wanTg_; lkSm_ = lkTg_;
            tonSm_ = tonTg_; fbSm_ = fbTg_; balSm_ = balTg_;
            for (int v = 0; v < kMaxVoices; ++v)
            { vfd_[v] = (v < nV_ ? 1.0f : 0.0f);
              baseG_[v] = baseS_[v]; depG_[v] = depS_[v]; panG_[v] = pan_[v]; gainG_[v] = gain_[v];
              plG_[v] = plT_[v]; prG_[v] = prT_[v]; }
            vNormG_ = vNorm_; xkG_ = xkTg_; tonL_ = tonLTg_; tonH_ = tonHTg_;
            seeded_ = true;
        }

        int i = 0;
        while (i < n)
        {
            const TypeSpec& T = SPEC[type_];
            const float limHi = (float) bufN_ - 8.0f;

            for (; i < n; ++i)
            {
                // ── 0. fade-swap-recover ─────────────────────────────────────
                if (pendType_ >= 0)
                {
                    // -46 dB, not -26. A Field change can swap `Side Only` (pure side, tiny
                    // on a near-mono wet) for `Collapse` (pure mid, large) — an 11x level
                    // change, measured — and a 0.05 floor leaves 5 % of that as a step.
                    dip_ += dipDn_ * (0.0f - dip_);
                    if (dip_ < 0.005f)
                    {
                        const bool structural = (pendType_ != type_);
                        type_ = pendType_; char_ = pendChar_; field_ = pendField_;
                        pendType_ = pendChar_ = pendField_ = -1;
                        recalc();
                        // At -34 dB a delay jump is inaudible, whereas gliding for 18 ms
                        // WHILE the wet fades back over 40 ms means the recovery rides a
                        // MOVING comb (measured +3.1 dB of overshoot on the fx3 chorus).
                        amtSm_ = amtTg_; sprSm_ = sprTg_; offSm_ = offTg_; wanSm_ = wanTg_;
                        lkSm_ = lkTg_; tonSm_ = tonTg_; fbSm_ = fbTg_; balSm_ = balTg_;
                        rateSm_ = rateTg_;
                        for (int v = 0; v < kMaxVoices; ++v)
                        { vfd_[v] = (v < nV_ ? 1.0f : 0.0f);
                          baseG_[v] = baseS_[v]; depG_[v] = depS_[v]; panG_[v] = pan_[v]; gainG_[v] = gain_[v];
                          plG_[v] = plT_[v]; prG_[v] = prT_[v]; }
                        vNormG_ = vNorm_; xkG_ = xkTg_; tonL_ = tonLTg_; tonH_ = tonHTg_;
                        if (structural)
                        {   // a Type change re-seats the whole machine: allpass states, the
                            // band tree and the granular window all belong to the OLD Type.
                            for (int k = 0; k < kMaxAP; ++k) { apA_[k].x = apA_[k].y = 0.0f; apB_[k].x = apB_[k].y = 0.0f; }
                            for (int k = 0; k < kMaxBands; ++k) bz_[k] = 0.0f;
                            for (int c = 0; c < 2; ++c) { fbSt_[c] = 0.0f; fbZ_[c] = 0.0f; }
                        }
                        break;                              // re-bind T, keep going
                    }
                }
                else dip_ += dipUp_ * (1.0f - dip_);

                // ── 1. per-sample glide (comb-click law: everything that feeds a delay
                //      LENGTH glides, not just the length itself) ─────────────
                amtSm_  += smK_ * (amtTg_  - amtSm_);
                widSm_  += smK_ * (widTg_  - widSm_);
                rateSm_ += smK_ * (rateTg_ - rateSm_);
                mixSm_  += smK_ * (mixTg_  - mixSm_);
                sprSm_  += smK_ * (sprTg_  - sprSm_);
                offSm_  += smK_ * (offTg_  - offSm_);
                wanSm_  += smK_ * (wanTg_  - wanSm_);
                lkSm_   += smK_ * (lkTg_   - lkSm_);
                tonSm_  += smK_ * (tonTg_  - tonSm_);
                fbSm_   += smK_ * (fbTg_   - fbSm_);
                balSm_  += smK_ * (balTg_  - balSm_);

                const float inL = L[i], inR = R[i];

                // ── 2. input presence — nothing free-runs ────────────────────
                const float rect = 0.5f * (std::fabs (inL) + std::fabs (inR));
                envIn_ += envK_ * (rect - envIn_);
                const float gate = envIn_ / (envIn_ + 0.003f);

                // ── 3. LOW KEEP — split first, re-join last, immune to Width.
                //      "Everything below this frequency stays mono and centred" is the
                //      device's own mono guard, and it only means that if the low band
                //      never enters the widening matrix at all.
                float hiL = inL, hiR = inR, loM = 0.0f;
                if (lkA_ > 0.0f)
                {
                    lkZ_[0] += lkA_ * (inL - lkZ_[0]);
                    lkZ_[1] += lkA_ * (inR - lkZ_[1]);
                    hiL = inL - lkZ_[0]; hiR = inR - lkZ_[1];
                    loM = 0.5f * (lkZ_[0] + lkZ_[1]);
                }

                // ── 4. the feedback return (env-gated, DC-blocked, damped) ───
                float fbInL = 0.0f, fbInR = 0.0f;
                if (fbSm_ > 1.0e-4f)
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        float s = fbSt_[c];
                        s = std::tanh (s * 1.4f) * 0.714286f;      // bounded, unit slope at 0
                        fbZ_[c] += fbDampA_ * (s - fbZ_[c]);       // 6 dB/oct at 7 kHz
                        s = fbZ_[c];
                        const float y = s - dcX_[c] + dcR_ * dcY_[c];   // 10 Hz AC couple
                        dcX_[c] = s; dcY_[c] = y;
                        (c == 0 ? fbInL : fbInR) = y * fbSm_ * gate;
                    }
                }

                // ── 5. write the line ───────────────────────────────────────
                const float lineL = hiL + fbInL;
                const float lineR = hiR + fbInR;
                bufL_[(size_t) wr_] = lineL;
                bufR_[(size_t) wr_] = lineR;

                float wetL = 0.0f, wetR = 0.0f;

                vizTick_ = (vizTick_ + 1) & 63;
                glideGeom();

                switch (T.family)
                {
                    case 0: procVoices (limHi, wetL, wetR); break;  // Stack/Shift/Double
                    case 1: procTwin   (lineL, lineR, limHi, wetL, wetR); break;  // the SDD-320 pair
                    case 2: procBlur   (lineL, lineR,        wetL, wetR); break;  // allpass decorrelation
                    default:procBands  (lineL, lineR,        wetL, wetR); break;  // band alternation
                }

                wr_ = (wr_ + 1) & mask_;

                // ── 6. wet TONE tilt (wet only — the MicroShift `Focus` job) ─
                tonL_ += smK_ * (tonLTg_ - tonL_);
                tonH_ += smK_ * (tonHTg_ - tonH_);
                if (std::fabs (tonL_ - 1.0f) > 0.002f)
                {
                    toneZ_[0] += toneA_ * (wetL - toneZ_[0]);
                    toneZ_[1] += toneA_ * (wetR - toneZ_[1]);
                    wetL = toneZ_[0] * tonL_ + (wetL - toneZ_[0]) * tonH_;
                    wetR = toneZ_[1] * tonL_ + (wetR - toneZ_[1]) * tonH_;
                }

                // the feedback tap sits HERE — after the machine, before Field and before
                // Width. Width outside the loop, always: a side boost inside it multiplies
                // the loop gain and the cap stops meaning anything (bible pitfall 9).
                fbSt_[0] = wetL; fbSt_[1] = wetR;

                // ── 7. FIELD — the placement matrix (back dropdown 2) ───────
                applyField (wetL, wetR);

                // ── 8. WIDTH — equal-power M/S rotation. theta = Width * pi/2:
                //      0.0 -> mono (M' = sqrt2*M, S' = 0)
                //      0.5 -> EXACTLY neutral (unity, bit-transparent)
                //      1.0 -> SIDE ONLY (mid = 0) = past mono-destruction. R11.
                {
                    const float m = 0.5f * (wetL + wetR), s = 0.5f * (wetL - wetR);
                    const float th = widSm_ * 1.5707963f;
                    const float mg = 1.4142136f * std::cos (th);
                    const float sg = 1.4142136f * std::sin (th);
                    const float mm = m * mg, ss = s * sg;
                    wetL = mm + ss; wetR = mm - ss;
                }

                // the protected low band rides ON TOP, mono, un-rotated
                wetL += loM; wetR += loM;

                const float trim = T.trim * cTrim_ * dip_;
                const float wL = wetL * trim, wR = wetR * trim;

                // ── 9. Mix — equal power. At mix 1.0 the dry gain is cos(pi/2) = 0.
                const float dg = std::cos (mixSm_ * 1.5707963f);
                const float wg = std::sin (mixSm_ * 1.5707963f);
                const float oL = inL * dg + wL * wg;
                const float oR = inR * dg + wR * wg;
                L[i] = oL; R[i] = oR;

                // ── 10. telemetry for the card (CONTRACT §2 Viz) ────────────
                cLL_ += corrK_ * (oL * oL - cLL_);
                cRR_ += corrK_ * (oR * oR - cRR_);
                cLR_ += corrK_ * (oL * oR - cLR_);
                viz_.corr = cLR_ / std::sqrt (std::max (1.0e-12f, cLL_ * cRR_));
                lvlSm_ += lvlK_ * (0.5f * (std::fabs (wL) + std::fabs (wR)) - lvlSm_);
                viz_.lvl = std::min (1.0f, lvlSm_ * 14.0f);
                {
                    const float mm = std::fabs (0.5f * (wL + wR));
                    const float ss = std::fabs (0.5f * (wL - wR));
                    viz_.widthNow = ss / std::max (1.0e-9f, mm + ss);
                }
            }
        }
    }

    const Viz& viz() const noexcept { return viz_; }

    // ── read-outs for the card and the harness ───────────────────────────────
    float liveRateHz()   const noexcept { return rateSm_ * SPEC[type_].rateMul; }
    int   liveVoices()   const noexcept { return nV_; }
    float liveBaseMs (int v) const noexcept { return baseS_[clampi (v,0,kMaxVoices-1)] * 1000.0f / fs_; }
    float liveTargetCents (int v) const noexcept { return cents_[clampi (v,0,kMaxVoices-1)]; }
    float liveDelayMs (int v) const noexcept { return dbgD_[clampi (v,0,kMaxVoices-1)] * 1000.0f / fs_; }
    float liveCrossK()   const noexcept { return xkG_; }
    float dbgDip()       const noexcept { return dip_; }
    int   dbgType()      const noexcept { return type_; }
    int   liveStages()   const noexcept { return nAP_; }
    int   liveBands()    const noexcept { return nB_; }

private:
    // ═════════ tables ════════════════════════════════════════════════════════
    //  family — WHICH MACHINE runs. This is the mechanism axis CONTRACT law 2 grades:
    //    0 voice crowd   (ring buffer, N reads)      Stack · Shift · Double
    //    1 antiphase pair(ring buffer, 2..8 lines, inverted cross-mix)   Twin
    //    2 allpass       (no ring; magnitude EXACTLY flat per channel)   Blur
    //    3 band tree     (no ring; perfect reconstruction, mono EXACT)   Bands
    //  mod — what moves the copies:
    //    0 sine LFO per voice (periodic, scattered rates)      Stack
    //    1 triangle, antiphase (constant |slope| = constant detune)   Twin
    //    2 NONE — static granular pitch readers                Shift
    //    3 band-limited random walk (APERIODIC)                Double
    struct TypeSpec
    {
        int   family, mod;
        float baseLoMs, baseHiMs;      // the voice base scatter, ms (pre-Offset)
        float maxCents;                // Amount 1.0 target, cents
        float centsCurve;              // t^curve
        float rateMul;                 // Type's own rate scaling
        float fbMax, trim;
    };

    static constexpr TypeSpec SPEC[kNumTypes] =
    {   // fam mod  baseLo baseHi maxC  curve  rateMul fbMax trim
        {  0,  0,   1.5f,  24.1f, 130.f, 1.60f, 1.00f, 0.90f, 1.00f },  // Stack
        {  1,  1,   5.0f,  15.5f,  28.f, 1.35f, 1.00f, 0.85f, 1.39f },  // Twin   (trim MEASURED at defaults)
        {  0,  2,   2.0f,  26.0f, 110.f, 1.40f, 1.00f, 0.85f, 0.98f },  // Shift
        {  0,  3,  17.0f,  61.0f,  62.f, 1.30f, 0.55f, 0.88f, 0.96f },  // Double
        {  2,  0,   0.0f,   0.0f,   0.f, 1.00f, 0.30f, 0.72f, 1.00f },  // Blur
        {  3,  0,   0.0f,   0.0f,   0.f, 1.00f, 0.60f, 0.80f, 0.72f }   // Bands  (trim MEASURED at defaults)
    };

    //  A Character RE-WIRES PHYSICS: fan shape, base scale, line count, modulator shape,
    //  filter topology, cross-mix depth. Never "a tone control".
    //  lvl is the MEASURED level makeup (fb343 preset-spread law) — a Character that
    //  changes the LEVEL is a volume knob wearing a costume.
    struct CharSpec
    {
        float baseMul, centsMul, rateMul, wanderAdd, spanMul;
        float x1, x2, x3;              // per-family extras, documented at the use site
        float lvl;
        int   flags;
    };
    enum : int { kEvenFan = 1, kOctTop = 2, kSubAnchor = 4, kThreePhase = 8,
                 kSineMod = 16, kCompOff = 32, kDarkBBD = 64, kAllNeg = 128,
                 kStaticWalk = 256, kNegPathB = 512, kTilt = 1024, kLinInterp = 2048 };

    static constexpr CharSpec CHAR[kNumTypes][kNumChars] =
    {
        // ── Stack.  x1 = per-voice rate jitter · x2 = octave-voice level · x3 = fan power
        { { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 0.00f, 0.00f, 1.00f, 1.000f, 0 },                       // JP Classic
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 0.00f, 0.00f, 1.00f, 1.000f, kEvenFan },                // Even Fan
          { 1.00f, 1.00f, 1.00f, 0.35f, 1.0f, 0.15f, 0.00f, 1.00f, 1.000f, 0 },                       // Analog Drift
          { 0.45f, 1.00f, 1.35f, 0.00f, 1.0f, 0.00f, 0.00f, 1.00f, 1.000f, 0 },                       // Tight
          { 2.20f, 1.00f, 0.70f, 0.00f, 1.0f, 0.00f, 0.00f, 1.00f, 0.955f, 0 },                       // Wide Fan
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 0.00f, 0.25f, 1.00f, 0.955f, kOctTop },                 // Octave Bloom
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 0.00f, 0.35f, 1.00f, 0.940f, kSubAnchor },              // Sub Anchor
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 0.00f, 0.00f, 1.00f, 0.975f, kThreePhase } },           // Three Phase
        // ── Twin.  x1 = line PAIRS (1..4) · x2 = cross-mix scale · x3 = BBD LP kHz (0 = off)
        { { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 1.0f, 1.00f, 0.0f, 1.000f, 0 },                         // Duo
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 2.0f, 1.00f, 0.0f, 0.900f, 0 },                         // Quad
          { 0.50f, 1.00f, 2.20f, 0.00f, 1.0f, 1.0f, 1.00f, 0.0f, 1.000f, 0 },                         // Mode Two   Arturia: 'delay times about half' — under the
                                                                                                          //            constant-cents law that IS a faster clock. baseMul
                                                                                                          //            alone measured ZERO change (the depth-driven base
                                                                                                          //            floor swallowed it).
          { 1.00f, 2.00f, 1.00f, 0.00f, 1.0f, 1.0f, 1.00f, 0.0f, 1.000f, 0 },                         // Mode Three Arturia: 'modulation intensity twice that of 1 and 2'
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 1.0f, 1.00f, 0.0f, 0.871f, kCompOff },                  // No Compander
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 1.0f, 1.00f, 4.0f, 1.040f, kDarkBBD },                  // Dark BBD
          { 1.00f, 2.50f, 1.00f, 0.00f, 1.0f, 1.0f, 1.00f, 0.0f, 1.000f, kSineMod },                  // Wobble
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 3.0f, 1.55f, 0.0f, 1.330f, 0 } },                       // Hex  (mono-hostile, TAGGED)
        // ── Shift.  x1 = extra-voice cents · x2 = extra-voice level · x3 = slap scale
        { { 1.00f, 1.00f, 1.00f, 0.00f, 1.00f,    0.0f, 0.00f, 1.00f, 1.000f, 0 },                    // Silk
          { 1.00f, 1.00f, 1.00f, 0.00f, 0.40f,    0.0f, 0.00f, 1.00f, 1.000f, 0 },                    // Punch
          { 1.00f, 1.00f, 1.00f, 0.55f, 1.00f,    0.0f, 0.00f, 1.00f, 1.000f, 0 },                    // Warble
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.00f,  700.0f, 0.18f, 1.00f, 0.975f, 0 },                    // Fifth Up
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.00f,    0.0f, 0.00f, 1.00f, 1.000f, kAllNeg },              // Down Double
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.00f,    0.0f, 0.00f, 2.30f, 0.985f, 0 },                    // Wide Slap
          { 1.00f, 1.00f, 1.00f, 0.00f, 0.27f,    0.0f, 0.00f, 1.00f, 1.000f, kLinInterp },           // Gritty
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.00f, 1200.0f, 0.25f, 1.00f, 0.965f, 0 } },                  // Octave Pair
        // ── Double.  x1 = walk bandwidth mul · x2 = voice-1 extra delay ms · x3 = static cents
        { { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 1.00f,  0.0f, 0.0f, 1.000f, 0 },                        // Vocal Two
          { 1.35f, 1.00f, 0.70f, 0.00f, 1.0f, 0.50f,  0.0f, 0.0f, 0.985f, 0 },                        // Wide Room  (was `Vocal Four`, whose coefficient row was
                                                                                                          //             IDENTICAL to Vocal Two — measured 0.00 change)
          { 1.00f, 2.00f, 0.60f, 0.00f, 1.0f, 0.55f,  0.0f, 0.0f, 0.985f, 0 },                        // Tape ADT
          { 0.50f, 1.00f, 1.00f, 0.00f, 1.0f, 1.00f,  0.0f, 0.0f, 1.000f, 0 },                        // Tight Inst
          { 1.80f, 1.60f, 1.00f, 0.00f, 1.0f, 1.00f,  0.0f, 0.0f, 0.975f, 0 },                        // Loose Crowd
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 1.00f,  0.0f, 8.0f, 1.000f, kStaticWalk },              // Static Twins
          { 1.00f, 1.00f, 1.00f, 0.00f, 1.0f, 1.00f, 31.0f, 0.0f, 0.990f, 0 },                        // Slapback
          { 1.00f, 2.50f, 1.30f, 0.00f, 1.0f, 1.00f,  0.0f, 0.0f, 0.960f, 0 } },                      // Seasick
        // ── Blur.  x1 = stages/Voice · x2 = fc lo Hz · x3 = fc hi Hz
        { { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 3.0f,  180.f, 5600.f, 1.000f, 0 },                        // Smooth Six
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 6.0f,  180.f, 5600.f, 1.000f, 0 },                        // Deep Twelve
          { 1.0f, 1.0f, 1.00f, 0.40f, 1.0f, 4.0f,   90.f, 9000.f, 1.000f, 0 },                        // Velvet
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 3.0f,  500.f, 6500.f, 1.000f, 0 },                        // Low Anchor
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 3.0f, 2000.f,10000.f, 1.000f, 0 },                        // Air Only
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 3.0f,  240.f, 7000.f, 1.000f, 0 },                        // Seed B
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 3.0f,  140.f, 4200.f, 1.000f, 0 },                        // Seed C
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 4.0f,  180.f, 5600.f, 1.000f, kNegPathB } },              // Counter (mono-hostile, TAGGED)
        // ── Bands.  x1 = bands/Voice · x2 = grid lo Hz · x3 = contrast law power
        { { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 1.00f, 140.f, 1.00f, 1.000f, 0 },                          // Coarse
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 2.00f, 140.f, 1.00f, 1.000f, 0 },                          // Fine
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 0.55f, 140.f, 1.00f, 1.000f, kTilt },                     // Tilted
          { 1.0f, 1.0f, 0.30f, 0.00f, 1.0f, 0.55f, 140.f, 1.00f, 1.000f, 0 },                         // Rotor Slow
          { 1.0f, 1.0f, 4.00f, 0.00f, 1.0f, 0.55f, 140.f, 1.00f, 1.000f, 0 },                         // Rotor Fast
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 0.55f, 140.f, 1.00f, 1.000f, 0 },                         // Guard  (contrast capped)
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 0.55f,  50.f, 1.00f, 1.000f, 0 },                         // Low Split
          { 1.0f, 1.0f, 1.00f, 0.00f, 1.0f, 0.55f, 140.f, 0.50f, 0.960f, 0 } }                        // Hard Split
    };

    static constexpr int kMaxAP = 24, kMaxBands = 16;

    // ═════════ derived-value computation — PER BLOCK, never per sample ═══════
    void recalc() noexcept
    {
        const TypeSpec& T = SPEC[type_];
        const CharSpec& C = CHAR[type_][char_];

        // ── Rate. 0.03..14 Hz log. tempoSync is honoured and CLAMPED into that range so
        //    a division that lands at 128 Hz can never turn a widener into an FM operator.
        float hz;
        if (p_.tempoSync)
        {
            const float beats = divBeats (clampi (p_.syncDiv, 1, kNumDivs - 1));
            hz = (float) (p_.bpm / 60.0) / std::max (0.0078125f, beats);
        }
        else hz = 0.03f * std::pow (14.0f / 0.03f, clamp01 (p_.rate));
        rateTg_ = clampf (hz, 0.02f, 14.0f);

        amtTg_ = clamp01 (p_.amount);
        widTg_ = clamp01 (p_.width);
        mixTg_ = clamp01 (p_.mix);
        sprTg_ = std::pow (clamp01 (p_.b2), 0.65f);   // linear left the first quarter under
                                                      // the audible step (1-corr moved 0.014)
        // 🐛 0.25 * 10^t put UNITY at t = 0.79, not at the knob centre — so the shipped
        // default (0.5) was quietly running every base delay at 0.79x, the depth clamped
        // against it, and the constant-cents law read 112 cents where it should read 130.
        // The §E rate-independence gate is what found it. 0.25 * 16^t is symmetric in
        // octaves about 1.0 and reaches 4x, which is further out at both ends.
        offTg_ = 0.25f * std::pow (16.0f, clamp01 (p_.b3));       // 0.25x .. 4.0x, unity at 0.5
        wanTg_ = std::pow (clamp01 (p_.b4), 0.70f) + C.wanderAdd;   // t^1.5 measured a dead first half
        lkTg_  = clamp01 (p_.b5);
        tonTg_ = 2.0f * clamp01 (p_.b6) - 1.0f;                   // -1..+1 tilt, 0.5 = flat
        // 🔧 the first version crossfaded wet against 2*LP and its centroid stopped moving
        // over the top half of the knob (725 -> 518 -> 327 -> 279 -> 270 Hz). A real tilt —
        // low band up, high band down, +-12 dB — moves all the way across.
        tonLTg_ = std::exp2 (tonTg_ *  2.0f);
        tonHTg_ = std::exp2 (tonTg_ * -2.0f);
        // 🔧 THE FEEDBACK TAPER IS SET IN dB OF BUILD-UP, NOT IN LOOP GAIN. A steady-state
        // regenerating loop builds by -20*log10(1-g), which is violently non-linear in g:
        // t^1.2 * 0.90 measured +0.07 / +0.29 / +0.86 dB for the first three quarters and
        // then +13.7 dB in the last one. With g = 1 - (1-gmax)^t the build-up is
        // -20*t*log10(1-gmax) — LINEAR in the knob, all the way to the same 0.90 ceiling.
        // 🔧 TAPER CALIBRATED TO THE MEASURED BUILD-UP, not to a formula. t^1.2*gmax gave
        // +0.07 / +0.29 / +0.86 dB for the first three quarters and +13.7 in the last.
        // 1-(1-gmax)^t is the right law for a COHERENT loop and still bunched, because this
        // loop is a bank of moving delays and recirculates INCOHERENTLY (power adds, not
        // amplitude). The measured curve (0.10 / 0.31 / 0.62 / 1.02 / 1.65 / 3.68 / 7.84 /
        // 11.16 / 13.16 / 14.90 dB at g = 0.21 .. 0.90) inverts to roughly gmax * t^0.35.
        fbTg_  = T.fbMax * std::pow (clamp01 (p_.b7), 0.35f);
        balTg_ = clamp01 (p_.b8);
        cTrim_ = C.lvl;
        walkHz_ = (0.4f + 2.4f * clamp01 (p_.rate)) * C.rateMul * C.x1;

        // Low Keep: 0 (off) -> 500 Hz, log above the dead zone.
        lkA_ = (lkTg_ < 0.02f) ? 0.0f
                               : onePoleA (40.0f * std::pow (500.0f / 40.0f, (lkTg_ - 0.02f) / 0.98f));
        toneA_   = onePoleA (900.0f);
        fbDampA_ = onePoleA (7000.0f);
        darkA_   = (C.x3 > 0.5f && type_ == 1) ? onePoleA (C.x3 * 1000.0f) : 0.0f;

        // ── VOICES = 3..8 COPIES, and the floor of 3 is a BOUNDARY DECISION, not a taper.
        //    Voice 0 is the un-modulated CENTRE read, so "2 copies" would be centre + ONE
        //    moving voice — which is a CHORUS, and the Chorus already shipped as chain kind
        //    6 (CONTRACT §4). The bottom of this knob is therefore centre + 2 movers.
        //    It is a REAL count: the harness counts echoes/sidebands, not the knob.
        nV_ = clampi (3 + (int) std::lround (clamp01 (p_.b1) * 5.0f), 3, kMaxVoices);

        const float rHz = std::max (0.02f, rateTg_ * T.rateMul * C.rateMul);
        const float amtC = std::pow (amtTg_, T.centsCurve) * T.maxCents * C.centsMul;

        // ── the JP-8000 measured fan (bible §1.1, cents from the measured ratios).
        //    Voice 0 is the CENTRE read (no modulation) — that is what makes `Balance`
        //    the measured centre/sides mix law and not a generic dry/wet.
        static const float kJP[kMaxVoices]   = { 0.0f, 0.34f, -0.34f, 1.04f, -1.12f, 1.77f, -2.02f, 2.40f };
        static const float kEven[kMaxVoices] = { 0.0f, 0.40f, -0.40f, 0.80f, -0.80f, 1.60f, -1.60f, 2.40f };
        static const float kBase[kMaxVoices] = { 1.5f, 9.7f, 13.1f, 17.3f, 21.9f, 11.3f, 15.7f, 24.1f };
        static const float kSlap[kMaxVoices] = { 2.0f, 8.0f, 12.5f, 17.0f, 22.0f, 6.0f, 15.0f, 26.0f };
        static const float kDbl [kMaxVoices] = { 17.f, 29.f, 41.f, 53.f, 23.f, 35.f, 47.f, 61.f };
        static const float kRho [kMaxVoices] = { 1.00f, 1.07f, 0.93f, 1.13f, 0.89f, 1.19f, 0.83f, 1.23f };

        const float* fan = (C.flags & kEvenFan) ? kEven : kJP;
        // 🔑 NORMALISE THE FAN TO THE LIVE VOICES. Measured first, then fixed: with the raw
        // JP offsets the outermost LIVE voice at Voices 4 only reached 43 % of the fan, so
        // Amount 100 % delivered 56 cents instead of 130 and the pan fan never left the
        // centre (correlation read +0.993 — a "crowd" that was mono). Normalising means the
        // outermost live voice always lands on the full Amount and on the full Spread, at
        // EVERY voice count, so neither knob's ceiling depends on another knob.
        float fanMax = 1.0e-6f;
        for (int v = 0; v < nV_; ++v) fanMax = std::max (fanMax, std::fabs (fan[v]));

        // Balance: the MEASURED JP-8000 mix law, verbatim (Szabo). Centre falls linearly,
        // sides rise as a parabola; they cross at b = 0.75.
        const float b = balTg_;
        // the measured JP-8000 centre law, EXTENDED. Szabo's polynomial only falls to
        // 0.444 at b = 1, which left the top quarter of the knob doing almost nothing
        // (side/mid read -11.15 / -10.37 / -10.06 dB over the last three steps). Past the
        // measured range we take the centre all the way OUT: Balance 100 % = copies only,
        // no anchor at all. R11.
        const float gC = clampf ((-0.55366f * b + 0.99785f) * std::sqrt (std::max (0.0f, 1.0f - b)), 0.0f, 1.2f);
        const float gS = clampf (-0.73764f * b * b + 1.2841f * b + 0.044372f, 0.0f, 1.4f);

        // 🔧 THE SIDES ARE NORMALISED BY sqrt(N-1). Without it the measured JP law's
        // side term is applied to every copy, so the copies out-power the centre by b ~ 0.4
        // and the top HALF of Balance moves side/mid by under 1 dB (-9.28 / -8.69 / -8.60 dB
        // measured). It also stops Voices being a volume knob, which it must never be.
        const float sideN = 1.0f / std::sqrt ((float) std::max (1, nV_ - 1));
        float pw = 0.0f;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            const bool live = (v < nV_);
            const float fn = clampf (fan[v] / fanMax, -1.0f, 1.0f);     // -1..+1 normalised fan
            const float w  = std::fabs (fn);                            // 0..1 fan weight

            // ── target cents ────────────────────────────────────────────────
            float c = 0.0f;
            if (T.mod == 0)            c = amtC * w;                    // Stack: modulated fan
            else if (T.mod == 1)       c = amtC;                        // Twin: one depth, both lines
            else if (T.mod == 2)       c = amtC * ((v == 0) ? 0.0f : (0.45f + 0.55f * w));
            else                       c = amtC * ((v == 0) ? 0.35f : (0.45f + 0.55f * w));
            if (T.mod == 2 && (C.flags & kAllNeg) == 0 && v > 0) { /* sign set below */ }
            cents_[v] = c;

            // ── depth from the CONSTANT-CENTS law, then grow the base to fit it ──
            float A = 0.0f;
            // 🐛 the effective rate is per-voice (the scattered rho fan). Solving the depth
            // with rHz*rho and then reading the achieved cents back with rHz alone
            // under-reported by exactly rho — 109.9 cents where the law delivers 130.0.
            rEff_[v] = rHz * ((T.mod == 3) ? C.rateMul : kRho[v]);
            if (T.mod == 0 || T.mod == 3)
                A = (std::exp2 (c / 1200.0f) - 1.0f) / (6.2831853f * rEff_[v]);
            else if (T.mod == 1)
                { rEff_[v] = rHz; A = (std::exp2 (c / 1200.0f) - 1.0f) / (4.0f * rHz); }
            else rEff_[v] = rHz;
            A = std::min (A, 0.110f);                                    // hard cap, 110 ms
            depS_[v] = A * fs_;

            float baseMs = (T.mod == 2) ? kSlap[v] : (T.mod == 3 ? kDbl[v] : kBase[v]);
            if (T.mod == 1) baseMs = 5.0f * (1.0f + 0.6f * (float) (v >> 1));
            baseMs *= C.baseMul;                 // Offset is applied AFTER the growth floor
            if (T.mod == 3 && v == 0) baseMs += C.x2;                    // Slapback
            if (T.mod == 2) baseMs = std::max (2.0f, baseMs * C.x3 - 0.5f * spanMs_ ());
            // 🔑 THE BASE GROWS TO THE CENTS — and then OFFSET SCALES THE RESULT.
            // First version applied Offset only to the nominal base, so at any real Amount
            // the depth-driven floor swallowed it and P3 was a DEAD KNOB over its lower half
            // (measured autocorr lag 51.4 / 51.4 / 34.4 ms across the sweep). Scaling after
            // the floor makes Offset the delay length, always. The price is stated, not
            // hidden: below 1.0 the copies get shorter than the excursion needs, so the
            // depth is clamped to 0.85 of the base and the ACHIEVED cents fall with it —
            // published in viz().voiceCents. Tight copies cannot detune as far. That is
            // physics, and it makes Offset a real voicing control instead of a trim.
            float baseS = std::max (baseMs * 0.001f * fs_, depS_[v] / 0.85f + 0.0015f * fs_);
            baseS *= offSm0_ ();
            baseS_[v] = clampf (baseS, 0.0015f * fs_, 0.130f * fs_);

            // achieved cents, published honestly (it can fall below the target only when
            // the 180 ms depth cap bites, i.e. very slow Rate + very deep Amount)
            const float Aeff = std::min (depS_[v] / fs_, 0.85f * baseS_[v] / fs_);
            achC_[v] = (T.mod == 1) ? 1200.0f * std::log2 (1.0f + 4.0f * rEff_[v] * Aeff)
                                    : 1200.0f * std::log2 (1.0f + 6.2831853f * rEff_[v] * Aeff);
            depS_[v] = Aeff * fs_;

            // ── static pitch ratio (Shift only) ─────────────────────────────
            float sc = 0.0f;
            if (T.mod == 2 && v > 0)
            {
                sc = c * (fn >= 0.0f ? 1.0f : -1.0f);    // the shift SIGN follows the pan:
                                                        // up-shifted copies sit left of the
                                                        // down-shifted ones (the H3000 recipe)
                if (C.flags & kAllNeg) sc = -std::fabs (c) * (0.6f + 0.4f * (float) (v & 1));
                if (C.x1 > 1.0f && v >= nV_ - 2) { sc = C.x1; }          // Fifth Up / Octave Pair —
                                                        // LIVE voices, same bug as kOctTop
            }
            if ((C.flags & kStaticWalk) && v > 0) sc = C.x3 * ((v & 1) ? 1.0f : -1.0f);
            // 🐛 was `v >= 6`, i.e. DEAD at every Voices count below 7 — and the default
            // is 6. Measured as a Character with EXACTLY zero effect. Address LIVE voices.
            if ((C.flags & kOctTop)    && v >= nV_ - 2 && v > 0) sc = 1200.0f;
            if ((C.flags & kSubAnchor) && v == 1) sc = -1200.0f;
            statC_[v] = sc;
            ratio_[v] = std::exp2 (sc / 1200.0f);

            // ── placement + gain ────────────────────────────────────────────
            // 🔑 THE CENTS FAN IS NORMALISED TO THE LIVE VOICES; THE PAN LADDER IS NOT.
            // Amount must read the same at every voice count (so the cents fan normalises),
            // but Voices must WIDEN as well as thicken, or the knob only fills in the middle
            // and no output metric can hear it (mono comb depth read 0.68 / 0.54 / 0.69 /
            // 0.75 / 0.57 dB across the whole sweep — noise). With an absolute ladder the
            // outermost copy walks out to the edge as the count rises: correlation falls
            // monotonically and the crowd audibly opens up.
            const float lad = 0.42f + 0.58f * (float) (v - 1) / 6.0f;   // one rung PER voice:
                                    // (v-1)/2 paired them up and adding one voice of a pair
                                    // widened nothing (1-corr stepped 0.004 then 0.044)
            float pan = (v == 0) ? 0.0f
                                 : (fn >= 0.0f ? 1.0f : -1.0f) * lad * sprSm0_ ();
            if ((C.flags & kSubAnchor) && v == 1) pan = 0.0f;            // the sub stays centre
            pan_[v] = pan;
            plT_[v] = std::sqrt (0.5f * (1.0f - pan));
            prT_[v] = std::sqrt (0.5f * (1.0f + pan));

            float g = (v == 0) ? gC : (gS * sideN);
            if (T.mod == 3) g *= std::pow (10.0f, -1.5f * (float) v / 20.0f);
            if (((C.flags & kOctTop) && v >= nV_ - 2 && v > 0) || ((C.flags & kSubAnchor) && v == 1)
                || (C.x1 > 1.0f && T.mod == 2 && v >= nV_ - 2 && v > 0))
                g *= (C.x2 > 0.0f ? C.x2 : 0.25f);
            gain_[v] = live ? g : 0.0f;
            if (live) pw += g * g;

            // rate jitter (Analog Drift) — a per-voice constant, re-diced only on reset
            rho_[v] = kRho[v] * (1.0f + C.x1 * (T.mod == 0 ? (rnd01 (v) - 0.5f) * 2.0f : 0.0f));
            if (C.flags & kThreePhase) rho_[v] = 1.0f;                   // locked 3-phase
        }

        // ── the KNOB-ONLY normaliser (bible §3.3, pitfall 3: a program-tracking
        //    normaliser flattens the level life you built — the Tape lesson).
        vNorm_ = 1.4142136f / std::sqrt (std::max (0.25f, pw));

        // ── Twin ─────────────────────────────────────────────────────────────
        // Voices sets the pair count (1..4 = 2..8 lines); the Character OFFSETS it, so
        // `Duo` at Voices 1-2 is the literal two-line SDD-320 and `Hex` is always +2 pairs.
        nPair_ = clampi ((nV_ + 1) / 2 + (int) C.x1 - 1, 1, 4);
        xkTg_ = clampf ((0.25f + 0.40f * amtTg_) * C.x2, 0.0f, 1.05f);

        // ── Blur: two allpass cascades whose break frequencies DIVERGE with Amount.
        //    Both paths stay EXACTLY allpass at every setting (|H| = 1 for any |g| < 1),
        //    so the per-channel magnitude is flat by construction — that is the Type's
        //    phase-independent discriminator and it cannot be tuned away.
        nAP_ = clampi ((int) std::lround (CHAR[type_][char_].x1 * (float) nV_), 2, kMaxAP);
        if (type_ == 4)                              // 🐛 was `type_ == 2` — the FAMILY index, not the TYPE index
        {
            // Scatter does TWO things, and the second one is what keeps the top of the
            // knob alive: it opens the cascade DOWNWARD. Decorrelation alone saturates —
            // correlation cannot go below zero for a phase-only decorrelator, so 0.875 and
            // 1.000 measured 0.081 and 0.083 and the last quarter was a plateau. Dragging
            // the lowest break frequency down by 4x puts real group delay into the low mids,
            // where the programme's energy actually is, and the smear keeps growing.
            const float lo = C.x2 * (1.0f - 0.75f * amtTg_), hi = C.x3;
            // 🔧 THE DIVERGENCE BUDGET IS SET BY WHERE THE PHASE WRAPS, and it was measured
            // three times before it was right. Correlation between two allpass cascades is
            // <cos(dphi(w))>, which OSCILLATES once dphi passes pi — at 2.5 octaves per stage
            // 1-|corr| read 0.00 / 0.88 / 0.53 / 0.91 / 0.96, i.e. the middle of the Scatter
            // knob went BACKWARDS. Engaging stages one at a time was tried next and still
            // turned over in the last quarter. The law that holds: N stages each contributing
            // an INDEPENDENT small phase delta give dphi_rms = delta*sqrt(N), so
            // corr ~= exp(-dphi_rms^2/2) — monotone in the knob for as long as dphi_rms stays
            // inside pi. With up to 18 stages that budgets 0.30 octaves per stage at 100 %.
            for (int k = 0; k < nAP_; ++k)
            {
                const float u = (nAP_ > 1) ? (float) k / (float) (nAP_ - 1) : 0.5f;
                const float div = 0.30f * amtTg_;
                const float f0 = lo * std::pow (hi / lo, u);
                const float sg = 0.5f + rnd01 (k * 3 + 1);               // 0.5..1.5
                float fa = f0 * std::exp2 (+div * sg);
                float fb = f0 * std::exp2 (-div * sg);
                fa *= (1.0f + 0.25f * wanSm0_ () * (rnd01 (k * 5 + 2) - 0.5f));
                fb *= (1.0f + 0.25f * wanSm0_ () * (rnd01 (k * 5 + 3) - 0.5f));
                fa = clampf (fa * offSm0_ (), 20.0f, 0.45f * fs_);
                fb = clampf (fb * offSm0_ (), 20.0f, 0.45f * fs_);
                apCaT_[k] = apCoef (fa);
                apCbT_[k] = apCoef (fb) * ((C.flags & kNegPathB) ? -1.0f : 1.0f);
            }
        }

        // ── Bands: a one-pole TREE. lp + (x - lp) = x EXACTLY at every coefficient, so
        //    the reconstruction is perfect and the mono sum of the alternation is the
        //    input, bit for bit. That is why this Type cannot damage a mono fold.
        nB_ = clampi ((int) std::lround (CHAR[type_][char_].x1 * (float) nV_), 2, kMaxBands);
        // sk alternates on a HALF-integer index, so an ODD band count leaves two same-sign
        // bands adjacent and the split stops being complementary. Force even.
        if (type_ == 5) nB_ = clampi (nB_ + (nB_ & 1), 2, kMaxBands);
        if (type_ == 5)                              // 🐛 same bug, same line of reasoning
        {
            const float lo = C.x2 * offSm0_ (), hi = 11000.0f * offSm0_ ();
            // 🔧 `Rate` SWEEPS THE GRID, it does not rotate the pattern. First attempt was
            // s_k = cos(2*pi*(k/2 + phase)), which looks like a rotating alternation and is
            // not one: for integer k, cos(pi*k + phi) = (-1)^k * cos(phi), so the phase
            // scales EVERY band's contrast by the same cos(phi) and at phi = 90 deg the split
            // vanishes entirely. Measured as correlation +0.88 where the model predicted
            // +0.17 — the "rotation" was a tremolo on the width. The honest mechanism is to
            // slide the CROSSOVER GRID by +-0.5 of a band spacing, which really does move the
            // split across the spectrum, is continuous, and never wraps.
            const float gsh = 0.5f * std::sin (6.2831853f * rotPh_) / (float) nB_;
            for (int k = 0; k < nB_ - 1; ++k)
            {
                const float u = (float) (k + 1) / (float) nB_ + gsh;
                bA_[k] = onePoleA (clampf (lo * std::pow (hi / lo, u), 20.0f, 0.45f * fs_));
            }
            bandHard_ = (C.x3 < 0.99f);          // `Hard Split` reaches full contrast early
            bandCap_  = (char_ == 5) ? 0.42f : 1.0f;   // `Guard` stops at g = 0.75: the polite counterexample
        }
    }

    // ═════════ the four machines ═════════════════════════════════════════════
    // 0 — the VOICE CROWD: Stack (sine LFO), Shift (static granular), Double (walk).
    void procVoices (float limHi, float& wetL, float& wetR) noexcept
    {
        const TypeSpec& T = SPEC[type_];
        const CharSpec& C = CHAR[type_][char_];
        const float rHz = std::max (0.02f, rateSm_ * T.rateMul * C.rateMul);
        float aL = 0.0f, aR = 0.0f;

        for (int v = 0; v < kMaxVoices; ++v)
        {
            const float tgt = (v < nV_) ? 1.0f : 0.0f;
            vfd_[v] += vfK_ * (tgt - vfd_[v]);                 // 30 ms fade in/out
            if (vfd_[v] < 1.0e-4f && tgt < 0.5f) { viz_.voiceCents[v] = 0.0f; continue; }

            float d = baseG_[v];
            float liveCents = 0.0f;

            if (T.mod == 0)                                    // ── Stack: sine LFO
            {
                float r = rHz * rho_[v];
                if (C.flags & kThreePhase)
                {   // the Solina law: three taps locked at 0/120/240 on a slow DEEP LFO
                    // plus a fast SHALLOW one — two lines in the modulation spectrum.
                    r = rHz * 0.75f;
                }
                ph_[v] += r / fs_; if (ph_[v] >= 1.0f) ph_[v] -= 1.0f;
                float s = std::sin (6.2831853f * (ph_[v] + (C.flags & kThreePhase ? (float) (v % 3) / 3.0f : 0.0f)));
                if (C.flags & kThreePhase)
                {   // fast shallow second LFO, 6.1 Hz-class, 12 % of the excursion
                    ph2_ += (6.1f * std::sqrt (std::max (0.05f, rHz))) / fs_;
                    if (ph2_ >= 1.0f) ph2_ -= 1.0f;
                    s = s * 0.88f + 0.12f * std::sin (6.2831853f * (ph2_ + (float) (v % 3) / 3.0f));
                }
                // retrig residual — the read POSITION is slew-capped, never the phase
                if (res_[v] != 0.0f)
                {
                    const float step = 0.5f / std::max (1.0f, depG_[v]);
                    res_[v] -= (res_[v] > 0.0f ? std::min (res_[v], step) : std::max (res_[v], -step));
                }
                // 🐛 THIS WAS `s -= res_[v]` AND IT DOUBLED THE MODULATION INSTEAD OF
                // CANCELLING IT. At the retrig instant the phase is forced to 0, so the new
                // sine is ~0 and the read position would step by dep*sin(phi_old). The
                // residual exists to HOLD the old offset and then bleed it away under the
                // slew cap — so it must be ADDED. Measured with the wrong sign: an exact
                // x2 output step at the retrig sample (peak d2 8.1e-02, 528x the static
                // floor). The click gate is what caught it; nothing else would have.
                s += res_[v];
                d += depG_[v] * s;
            }
            else if (T.mod == 3)                               // ── Double: random WALK
            {
                const float bwHz = walkHz_;
                tickWalk (wk_[v], bwHz);
                float s = wk_[v].st;
                if (C.flags & kStaticWalk) s = 0.0f;
                d += depG_[v] * s;
            }
            // (Shift needs no branch: its pitch is the granular reader's constant ratio,
            //  published from statC_ below, and its delay does not move at all.)

            // wander: a slow band-limited walk on the TIME of every copy (all families)
            if (wanSm_ > 1.0e-3f && T.mod != 3)
            {
                tickWalk (wk_[v], 0.35f + 1.2f * wanSm_);
                d += wanSm_ * 0.012f * fs_ * wk_[v].st;   // +-12 ms at 100 %: past 'humanise'
            }

            d = clampf (d, 2.0f, limHi);
            // THE TRUE instantaneous pitch, from the slope of the read position. The log2
            // is a VIZ cost only, so it runs on the 64-sample telemetry tick — 8 log2 per
            // sample was 30 % of the worst Type's budget for a number the card reads at 60 Hz.
            if (prevD_[v] < 0.0f) prevD_[v] = d;
            if (vizTick_ == 0)
            {
                liveCents = (T.mod == 2)
                    ? statC_[v]
                    : 1200.0f * std::log2 (clampf (1.0f - (d - prevD_[v]) * (1.0f / 64.0f), 0.25f, 4.0f))
                      + statC_[v];
                prevD_[v] = d;
                viz_.voiceCents[v] = liveCents;
            }
            dbgD_[v] = d;

            const float* buf = ((v & 1) == 0) ? bufL_.data() : bufR_.data();
            float y;

            if (T.mod == 2 && v > 0 && std::fabs (statC_[v]) > 0.02f)
            {
                // the two-head constant-ratio granular reader (the fx3 chorus correction:
                // the free parameter is the ramp SPAN, and the crossfade period FOLLOWS
                // from it — Tw = span / r. A fixed short period quantises the shift to
                // 1/T and delivers ZERO cents.)
                const float span = spanMs_ () * 0.001f * fs_;
                const float r = ratio_[v] - 1.0f;
                q_[v] += r / std::max (1.0f, span);
                if (q_[v] >= 1.0f) q_[v] -= 1.0f; else if (q_[v] < 0.0f) q_[v] += 1.0f;
                // ONE cosine, not two: the two heads are 180 deg apart on a raised cosine,
                // so w1 = 1 - w0 exactly and the gain sum is identically 1. (Two cosines and
                // a divide, per voice per sample, for a constant.)
                const float w0 = 0.5f - 0.5f * std::cos (6.2831853f * q_[v]);
                float q1 = q_[v] + 0.5f; if (q1 >= 1.0f) q1 -= 1.0f;
                const float o0 = (r > 0.0f) ? span * (1.0f - q_[v]) : span * q_[v];
                const float o1 = (r > 0.0f) ? span * (1.0f - q1)    : span * q1;
                const float d0 = clampf (d + o0, 2.0f, limHi), d1 = clampf (d + o1, 2.0f, limHi);
                y = (C.flags & kLinInterp)
                      ? (w0 * readL (buf, d0) + (1.0f - w0) * readL (buf, d1))
                      : (w0 * readH (buf, d0) + (1.0f - w0) * readH (buf, d1));
            }
            else y = readH (buf, d);

            // WANDER MOVES THE COPIES IN SPACE AS WELL AS IN TIME. Time jitter alone was
            // measurable on the delay trace and barely audible on the output (envelope
            // periodicity moved 0.004 across a whole quarter of the knob). Real crowds do
            // not stand still; a slow independent walk on each copy's PLACEMENT is what
            // makes the field breathe, and it is what the metric can hear.
            const float g = gainG_[v] * vfd_[v] * vNormG_;
            float gl = plG_[v], gr = prG_[v];
            if (wanSm_ > 1.0e-3f)
            {
                tickWalk (wkp_[v], 0.22f + 0.75f * wanSm_);
                const float pw = wanSm_ * 0.55f * wkp_[v].st;
                gl = clampf (gl - pw, 0.0f, 1.35f); gr = clampf (gr + pw, 0.0f, 1.35f);
            }
            aL += g * gl * y; aR += g * gr * y;

            viz_.voicePan[v]   = panG_[v];
        }
        wetL = aL; wetR = aR;
    }

    // 1 — TWIN: the SDD-320. Antiphase TRIANGLE lines + inverted cross-mix.
    //
    // 🚨 THE COMPANDER IS AFTER THE DELAY, AND THAT IS A BUG FIX, NOT A SHORTCUT.
    // The first build did it the textbook way — compress, write the COMPANDED line into the
    // shared ring, read, expand — and the click gate caught the consequence: the ring buffer
    // then holds DIFFERENT CONTENT depending on which Type is running, so a Type swap puts a
    // STEP into the buffer, and that step comes out one delay-time later, ~13 ms after the
    // dip has already recovered. Measured: peak second-difference 3.17e-02 against a 1.28e-04
    // static floor — 250x — and it vanished to 1.02e-03 with the `No Compander` Character,
    // which is what identified it. No dip length fixes this; the discontinuity is in the
    // stored signal, not in the gain.
    // The fix is structural: the ring is Type-independent, always. And the honest reading of
    // what was lost is: nothing. A compressor followed by its exact inverse around a delay is
    // an IDENTITY unless something is added in between — the SDD-320's compander exists to
    // hide BBD hiss, and this device injects no hiss (house law: no noise unless a param owns
    // it). The fx3 chorus measured exactly this on the legacy engine: its "compander" was a
    // static x1.47 trim with ZERO level dependence, a costume. So we keep the audible half —
    // a real 2:1 downward compressor on the wet, calibrated to the -26 dBFS bus — and drop
    // the half that was only ever cancelling it.
    void procTwin (float lineL, float lineR, float limHi, float& wetL, float& wetR) noexcept
    {
        const TypeSpec& T = SPEC[type_];
        const CharSpec& C = CHAR[type_][char_];
        const float rHz = std::max (0.02f, rateSm_ * T.rateMul * C.rateMul);
        (void) lineL; (void) lineR;

        float wl = 0.0f, wr = 0.0f;
        const int np = clampi (nPair_, 1, 4);
        const float gn = 1.0f / std::sqrt ((float) np);

        for (int p = 0; p < np; ++p)
        {
            // ── THE TRIANGLE. Constant |slope| ⇒ CONSTANT |cents|, sign flipping at the
            //    apexes. Rounded by a 1.5 ms one-pole so the slope STEP at the apex is not
            //    a tick — and by nothing else. A flat top would be zero slope = zero detune,
            //    which is what an earlier draft of the bible would have shipped.
            ph_[p] += (rHz * (1.0f + 0.13f * (float) p)) / fs_;
            if (ph_[p] >= 1.0f) ph_[p] -= 1.0f;
            float m = (C.flags & kSineMod) ? std::sin (6.2831853f * ph_[p])
                                           : triRaw (ph_[p]);
            triZ_[p] += apexK_ * (m - triZ_[p]);
            m = triZ_[p];
            const float inv = ((p & 1) ? -1.0f : 1.0f);        // the 2nd pair runs inverted
            const float base = baseG_[p * 2];
            const float dep  = depG_[p * 2];
            const float dL = clampf (base + dep * m * inv, 2.0f, limHi);
            const float dR = clampf (base - dep * m * inv, 2.0f, limHi);
            wl += gn * readH (bufL_.data(), dL);
            wr += gn * readH (bufR_.data(), dR);
            if (p == 0)
            {   // THE TRUE instantaneous pitch of each line, from the slope of its read
                // position — NOT from the triangle's sign, which would report a bimodal
                // detune whatever the modulator shape and make the sine A/B unfailable.
                if (prevD_[0] < 0.0f) { prevD_[0] = dL; prevD_[1] = dR; }
                if (vizTick_ == 0)
                {
                    viz_.voiceCents[0] = 1200.0f * std::log2 (clampf (1.0f - (dL - prevD_[0]) * (1.0f / 64.0f), 0.25f, 4.0f));
                    viz_.voiceCents[1] = 1200.0f * std::log2 (clampf (1.0f - (dR - prevD_[1]) * (1.0f / 64.0f), 0.25f, 4.0f));
                    prevD_[0] = dL; prevD_[1] = dR;
                }
                dbgD_[0] = dL; dbgD_[1] = dR;
                for (int z = 2; z < kMaxVoices; ++z) viz_.voiceCents[z] = 0.0f;
            }
        }

        // ── THE CROSS-MIX, verbatim Arturia: "the delayed signal is cross-mixed to the
        //    OTHER channel with OPPOSITE POLARITY." This is where the width comes from and
        //    it is also the single most mono-hostile line in the device: it subtracts the
        //    channels, so a fold-down cancels (1 - k) of the wet mid.
        const float k = xkG_;
        float cl = wl - k * wr;
        float cr = wr - k * wl;

        if (darkA_ > 0.0f)                                     // `Dark BBD` reconstruction LP
        { darkZ_[0] += darkA_ * (cl - darkZ_[0]); darkZ_[1] += darkA_ * (cr - darkZ_[1]);
          cl = darkZ_[0]; cr = darkZ_[1]; }

        if ((C.flags & kCompOff) == 0)
        {
            // 2:1 downward compression, knee at -12 dB RELATIVE TO THE -26 dBFS bus program
            // (bible §2.2). Never a literal dBFS constant copied out of the hardware world —
            // that lands 26 dB wrong here.
            cmpE_[0] += cmpK_ * (std::fabs (cl) - cmpE_[0]);
            cmpE_[1] += cmpK_ * (std::fabs (cr) - cmpE_[1]);
            cl *= compG (cmpE_[0], -0.5f);
            cr *= compG (cmpE_[1], -0.5f);
            // the box's tilt — "a kind of low-shelf EQ applied to the entire signal"
            peZ_[0] += peA0_ * (cl - peZ_[0]); peZ_[1] += peA0_ * (cr - peZ_[1]);
            cl = cl * 0.72f + peZ_[0] * 0.56f;
            cr = cr * 0.72f + peZ_[1] * 0.56f;
        }

        // "Normally this would result in a loss of lower frequencies, but the filtering
        //  circuit ... slightly boosts the bass" — the DOCUMENTED compensation, applied to
        //  the wet MID (not to the dry, so it still works at Mix 1.0 where there is no dry).
        float mm = 0.5f * (cl + cr), ss = 0.5f * (cl - cr);
        bassZ_[0] += bassA_ * (mm - bassZ_[0]);
        mm += 0.26f * bassZ_[0];                               // ~ +2 dB below 150 Hz
        wetL = mm + ss; wetR = mm - ss;
        viz_.voicePan[0] = -0.85f; viz_.voicePan[1] = 0.85f;
        for (int z = 2; z < kMaxVoices; ++z) viz_.voicePan[z] = 0.0f;
    }

    // 2 — BLUR: two allpass cascades. Per-channel magnitude is EXACTLY flat at every
    //     setting; only the PHASE differs, so the decorrelation cannot be mistaken for EQ.
    void procBlur (float lineL, float lineR, float& wetL, float& wetR) noexcept
    {
        const float m = 0.5f * (lineL + lineR), s0 = 0.5f * (lineL - lineR);
        float a = m, b = m;
        for (int k = 0; k < nAP_; ++k)
        {   // the COEFFICIENTS glide too: an allpass whose c steps emits Dc*(x - y1), which
            // on a fast Amount sweep is a click even though the magnitude never moves.
            apCa_[k] += smK_ * (apCaT_[k] - apCa_[k]);
            apCb_[k] += smK_ * (apCbT_[k] - apCb_[k]);
            a = ap (apA_[k], apCa_[k], a); b = ap (apB_[k], apCb_[k], b);
        }
        // Balance blends the un-scattered mid back in — a real centre anchor, not a dry/wet
        // EQUAL POWER, not a linear blend: the two paths are mutually decorrelated, so a
        // linear crossfade measured -5.63 dB in the middle (a Balance knob that is also a
        // volume knob — the fb343 law).
        const float th = (0.35f + 0.65f * balSm_) * 1.5707963f;   // higher Balance = MORE copies,
                                                                 // the same direction as the JP mix law
        const float ga = std::sin (th), gm = std::cos (th);
        a = a * ga + m * gm;
        b = b * ga + m * gm;
        wetL = a + s0; wetR = b - s0;
        for (int v = 0; v < kMaxVoices; ++v)
        { viz_.voicePan[v] = (v < nV_) ? ((v & 1) ? 0.9f : -0.9f) * sprSm0_ () : 0.0f;
          viz_.voiceCents[v] = 0.0f; }
    }

    // 3 — BANDS: complementary band alternation on a one-pole TREE.
    //     lp + (x - lp) = x EXACTLY ⇒ L + R = 2*x at every band gain ⇒ the mono fold is
    //     the input, bit for bit. Width and Field can still destroy it downstream; the
    //     TYPE cannot.
    void procBands (float lineL, float lineR, float& wetL, float& wetR) noexcept
    {
        const CharSpec& C = CHAR[type_][char_];
        const float m = 0.5f * (lineL + lineR), s0 = 0.5f * (lineL - lineR);
        rotPh_ += (rateSm_ * 0.25f * C.rateMul) / fs_; if (rotPh_ >= 1.0f) rotPh_ -= 1.0f;
        // the contrast follows the SMOOTHED Amount, per sample: a per-block step in a band
        // gain is an amplitude step in every band at once, i.e. a click.
        // 🔑 THE CONTRAST GOES PAST 1.0 — TO 1.8 — AND THAT IS THE R11 CEILING OF THIS TYPE.
        // The band gains are (1 + g) and (1 - g); they SUM TO 2 for ANY g, so the mono fold is
        // the input bit-for-bit at every contrast. Past g = 1 the quiet channel's gain goes
        // NEGATIVE: the two channels hold the same band in OPPOSITE POLARITY. Correlation
        // drives hard negative, the image tears itself apart — and mono is still exact.
        // Measured first: one-pole splits at g <= 1 only reached corr +0.915, i.e. a "split"
        // that was not split. Steeper crossovers would have cost the exact reconstruction;
        // over-driving the gain does not.
        const float bandCon_ = clampf ((bandHard_ ? std::sqrt (amtSm_) : amtSm_) * 1.8f * bandCap_, 0.0f, 1.8f);
        const float bandNorm_ = 1.0f / std::sqrt (1.0f + bandCon_ * bandCon_);

        float rest = m, al = 0.0f, ar = 0.0f;
        for (int k = 0; k < nB_; ++k)
        {
            float band;
            if (k < nB_ - 1) { bz_[k] += bA_[k] * (rest - bz_[k]); band = bz_[k]; rest -= band; }
            else band = rest;
            // s_k rotates continuously, so `Rate` sweeps the split across the spectrum
            float sk = (k & 1) ? -1.0f : 1.0f;              // hard complementary alternation
            if (C.flags & kTilt) sk *= (1.0f + 0.18f * ((k & 1) ? 1.0f : -1.0f));
            const float g = bandCon_ * sk;
            al += band * (1.0f + g);
            ar += band * (1.0f - g);
        }
        al *= bandNorm_; ar *= bandNorm_;
        const float th = (0.35f + 0.65f * balSm_) * 1.5707963f;
        const float ga = std::sin (th), gm = std::cos (th);
        wetL = al * ga + m * gm + s0;
        wetR = ar * ga + m * gm - s0;
        for (int v = 0; v < kMaxVoices; ++v)
        { viz_.voicePan[v] = (v < nB_) ? (((v & 1) ? -1.0f : 1.0f) * clamp01 (bandCon_ / 1.8f)) * sprSm0_ () : 0.0f;
          viz_.voiceCents[v] = 0.0f; }
    }

    // ═════════ the FIELD matrix (back dropdown 2) ════════════════════════════
    void applyField (float& wl, float& wr) noexcept
    {
        switch (field_)
        {
            case 0: default: break;                                       // Direct
            case 1:                                                       // Alternate
            {   // swap the channels ABOVE a crossover: frequency-dependent placement.
                // A swap does not change L+R, so this option is mono-EXACT.
                altZ_[0] += altA_ * (wl - altZ_[0]);
                altZ_[1] += altA_ * (wr - altZ_[1]);
                const float lo0 = altZ_[0], lo1 = altZ_[1];
                const float hi0 = wl - lo0,  hi1 = wr - lo1;
                wl = lo0 + hi1; wr = lo1 + hi0;
                break;
            }
            case 2:                                                       // Orbit
            {   // a genuine rotation of the L/R pair, advancing at Rate. It sweeps THROUGH
                // antiphase once per orbit — which is why it is tagged mono-hostile.
                orbit_ += (rateSm_ * 0.20f) / fs_; if (orbit_ >= 1.0f) orbit_ -= 1.0f;
                const float th = 6.2831853f * orbit_;
                const float c = std::cos (th), s = std::sin (th);
                const float a = wl * c - wr * s, b = wl * s + wr * c;
                wl = a; wr = b;
                break;
            }
            case 3: { const float t = wl; wl = wr; wr = t; break; }       // Swap
            case 4: { const float s = 0.5f * (wl - wr); wl = s; wr = -s; break; }   // Side Only
            case 5: { const float m = 0.5f * (wl + wr); wl = m; wr = m;  break; }   // Collapse
        }
    }

    // ═════════ retrig — the read POSITION is slew-capped, not the phase ══════
    void fireRetrig() noexcept
    {
        for (int v = 0; v < kMaxVoices; ++v)
        {
            res_[v] = std::sin (6.2831853f * ph_[v]);   // the offset we owe the read head
            ph_[v]  = 0.0f;
            q_[v]   = 0.0f;
        }
    }

    // ═════════ small parts ═══════════════════════════════════════════════════
    struct AP { float x, y; };
    struct Walk { float st, tg, ph; };

    static inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static inline int   clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    inline float onePoleA (float hz) const noexcept
    { return 1.0f - std::exp (-6.2831853f * clampf (hz, 1.0f, 0.45f * fs_) / fs_); }

    // first-order allpass, break frequency form. |H| = 1 for ANY |c| < 1 — that is the
    // whole point of the Blur Type and it is why its per-channel magnitude gate is a
    // construction proof, not a tuned constant.
    inline float apCoef (float hz) const noexcept
    { const float t = std::tan (3.14159265f * clampf (hz, 20.0f, 0.45f * fs_) / fs_);
      return clampf ((t - 1.0f) / (t + 1.0f), -0.97f, 0.97f); }
    static inline float ap (AP& st, float c, float x) noexcept
    { const float y = c * x + st.x - c * st.y; st.x = x; st.y = y; return y; }

    static inline float triRaw (float p) noexcept
    { return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p); }
    static inline float triSlope (float p) noexcept { return (p < 0.5f) ? 1.0f : -1.0f; }

    // 2:1 compander, calibrated RELATIVE to the -26 dBFS bus program (knee -12 dB re
    // program = 0.0125 linear). exp = -0.5 compresses, +0.5 expands: exactly inverse.
    static inline float compG (float env, float e) noexcept
    { const float k = 0.0125f; const float r = std::max (env, 1.0e-6f) / k;
      return (r <= 1.0f) ? 1.0f : std::pow (r, e); }

    inline void tickWalk (Walk& w, float hz) noexcept
    {   // hold + slew: a band-limited random WALK. A per-sample noise smoother is not a
        // walk (it correlates like a lowpass and reads as periodic to an autocorrelator).
        w.ph += hz / fs_;
        if (w.ph >= 1.0f) { w.ph -= 1.0f; w.tg = 2.0f * rndf() - 1.0f; }
        w.st += (w.tg - w.st) * clampf (hz * 6.0f / fs_, 1.0e-5f, 0.5f);
    }

    inline float rndf() noexcept
    { rng_ = rng_ * 1664525u + 1013904223u; return (float) (rng_ >> 8) * (1.0f / 16777216.0f); }
    static inline float rnd01 (int k) noexcept
    { uint32_t s = (uint32_t) (k * 2654435761u) ^ 0x85EBCA6Bu;
      s ^= s >> 15; s *= 2246822519u; s ^= s >> 13;
      return (float) (s >> 8) * (1.0f / 16777216.0f); }

    inline float readH (const float* b, float d) const noexcept
    {   // 4-point cubic Hermite (DelayEngine.h:240 idiom) — bright AND near-linear-phase
        d = clampf (d, 1.0f, (float) bufN_ - 6.0f);
        const int i = (int) d; const float f = d - (float) i;
        const int p = wr_ - i;
        const float ym1 = b[(size_t) ((p + 1) & mask_)];
        const float y0  = b[(size_t) ((p)     & mask_)];
        const float y1  = b[(size_t) ((p - 1) & mask_)];
        const float y2  = b[(size_t) ((p - 2) & mask_)];
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    inline float readL (const float* b, float d) const noexcept
    {   // deliberately WORSE — the `Gritty` Character's AM artifact is the point
        d = clampf (d, 1.0f, (float) bufN_ - 6.0f);
        const int i = (int) d; const float f = d - (float) i;
        const int p = wr_ - i;
        const float y0 = b[(size_t) (p & mask_)], y1 = b[(size_t) ((p - 1) & mask_)];
        return y0 + f * (y1 - y0);
    }

    // per-sample geometry glide. EVERY delay length, gain and pan is a TARGET computed
    // per block and a SMOOTHED value used per sample — a per-block step in a delay length
    // is a comb click by another name (the house comb-click law).
    inline void glideGeom() noexcept
    {
        for (int v = 0; v < kMaxVoices; ++v)
        {
            baseG_[v] += smK_ * (baseS_[v] - baseG_[v]);
            depG_[v]  += smK_ * (depS_[v]  - depG_[v]);
            panG_[v]  += smK_ * (pan_[v]   - panG_[v]);
            // the equal-power pan GAINS are glided, not recomputed: two sqrt per voice per
            // sample was 16 sqrt in the inner loop for a value that moves at 18 ms.
            plG_[v]   += smK_ * (plT_[v]   - plG_[v]);
            prG_[v]   += smK_ * (prT_[v]   - prG_[v]);
            gainG_[v] += smK_ * (gain_[v]  - gainG_[v]);
        }
        vNormG_ += smK_ * (vNorm_ - vNormG_);
        xkG_    += smK_ * (xkTg_  - xkG_);
    }

    inline float offSm0_() const noexcept { return offTg_; }
    inline float sprSm0_() const noexcept { return sprTg_; }
    inline float wanSm0_() const noexcept { return wanTg_; }
    inline float spanMs_() const noexcept
    { return 30.0f * CHAR[type_][char_].spanMul; }

    // ═════════ state ═════════════════════════════════════════════════════════
    Params p_{};
    Viz    viz_{};
    float  fs_ = 48000.0f;

    std::vector<float> bufL_, bufR_;
    int   bufN_ = 0, mask_ = 0, wr_ = 0;

    int   type_ = 0, char_ = 0, field_ = 0;
    int   pendType_ = -1, pendChar_ = -1, pendField_ = -1;
    bool  seeded_ = false, lastRetrig_ = false;

    // smoothers (targets computed in recalc, glided per sample)
    float amtTg_=0.35f, widTg_=0.5f, rateTg_=0.3f, mixTg_=0.5f, sprTg_=0.6f,
          offTg_=1.0f, wanTg_=0.0f, lkTg_=0.0f, tonTg_=0.0f, fbTg_=0.0f, balTg_=0.4f;
    float amtSm_=0.35f, widSm_=0.5f, rateSm_=0.3f, mixSm_=0.5f, sprSm_=0.6f,
          offSm_=1.0f, wanSm_=0.0f, lkSm_=0.0f, tonSm_=0.0f, fbSm_=0.0f, balSm_=0.4f;
    float smK_=0, envK_=0, lvlK_=0, corrK_=0, vfK_=0, dipDn_=0, dipUp_=0,
          apexK_=0, cmpK_=0, expK_=0, dcR_=0;
    float lkA_=0, toneA_=0, fbDampA_=0, darkA_=0;
    float tonLTg_=1.0f, tonHTg_=1.0f, tonL_=1.0f, tonH_=1.0f;
    float peA0_ = 0.0f, bassA_ = 0.0f, altA_ = 0.0f;

    // per-voice tables (recomputed per block)
    int   nV_ = 4, nPair_ = 1, nAP_ = 6, nB_ = 8;
    float baseS_[kMaxVoices]{}, depS_[kMaxVoices]{}, gain_[kMaxVoices]{}, pan_[kMaxVoices]{};
    float cents_[kMaxVoices]{}, achC_[kMaxVoices]{}, statC_[kMaxVoices]{}, ratio_[kMaxVoices]{};
    float rho_[kMaxVoices]{}, dbgD_[kMaxVoices]{}, rEff_[kMaxVoices]{}, prevD_[kMaxVoices]{};
    float ph_[kMaxVoices]{}, triZ_[kMaxVoices]{}, q_[kMaxVoices]{}, res_[kMaxVoices]{}, vfd_[kMaxVoices]{};
    Walk  wk_[kMaxVoices]{}, wkp_[kMaxVoices]{};
    float ph2_ = 0.0f, orbit_ = 0.0f, rotPh_ = 0.0f;
    float vNorm_ = 1.0f, cTrim_ = 1.0f, xkTg_ = 0.5f, xkG_ = 0.5f, walkHz_ = 1.0f;
    float baseG_[kMaxVoices]{}, depG_[kMaxVoices]{}, panG_[kMaxVoices]{}, gainG_[kMaxVoices]{};
    float plT_[kMaxVoices]{}, prT_[kMaxVoices]{}, plG_[kMaxVoices]{}, prG_[kMaxVoices]{};
    int   vizTick_ = 0;
    float vNormG_ = 1.0f;

    AP    apA_[kMaxAP]{}, apB_[kMaxAP]{};
    float apCa_[kMaxAP]{}, apCb_[kMaxAP]{}, apCaT_[kMaxAP]{}, apCbT_[kMaxAP]{};
    float bA_[kMaxBands]{}, bz_[kMaxBands]{};
    bool  bandHard_ = false; float bandCap_ = 1.0f;

    float lkZ_[2]{}, toneZ_[2]{}, fbZ_[2]{}, fbSt_[2]{}, dcX_[2]{}, dcY_[2]{},
          peZ_[2]{}, deZ_[2]{}, darkZ_[2]{}, cmpE_[2]{}, expE_[2]{}, altZ_[2]{}, bassZ_[2]{};
    float envIn_ = 0.0f, lvlSm_ = 0.0f, dip_ = 1.0f;
    float cLL_ = 1e-9f, cRR_ = 1e-9f, cLR_ = 0.0f;
    uint32_t rng_ = 0x9E3779B9u;
};

} // namespace tw
