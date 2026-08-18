#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TerrainPhaserFx.h — fb395+. ONE instance of the FX-rack PHASER device (chain kind 8).
// Contract: Design/fx3/CONTRACT.md §2 (locked interface) · Bible: Design/PHASER-BUILD-BIBLE.md
// Harness:  Design/fx3/phaser/phaser_cert.cpp   (compiles, runs, prints real numbers)
//
// ── WHAT A PHASER IS, AND WHY THE USUAL METRICS LIE ABOUT IT ────────────────
// A phaser is a cascade of ALL-PASS sections. |A(f)| = 1 at every frequency, always. The
// audible effect only exists because the all-pass branch is SUMMED with the straight branch:
// phase rotation becomes cancellation. So:
//   · the wet-only magnitude spectrum of a phaser is FLAT — measuring it proves nothing;
//   · sample-difference RMS is enormous for an inaudible change (fb282: 102 % "divergence",
//     0.02 dB of real magnitude change, and Max heard NOTHING).
// Everything measurable about this device therefore lives in the NOTCH GEOMETRY of the summed
// output, and in how that geometry MOVES: notch count · centre frequencies · spacing ratios ·
// notch depth · inter-notch peak gain · sweep range in octaves · rise/fall asymmetry ·
// monotonic spectral drift · step flatness · non-harmonic modulation lines · FM sidebands ·
// pitch deviation in cents. The harness gates on those and on nothing else.
//
// ── THE SUM IS INSIDE THE EFFECT, NOT IN THE MIX KNOB ───────────────────────
//   phaserOut = x + apBlend · (A{x} − x)          apBlend 0.5 = classic · 1.0 = vibrato
//   out       = mixDry · x + mixWet · comp · phaserOut
// This matters three times:
//   1. NOTCH DEPTH IS MONOTONIC IN MIX (0 → −∞). Had Mix itself been the dry/wet summer, notch
//      depth would peak at 50 and vanish at 100 — non-monotonic by construction, and Mix 100
//      would have sounded LESS phased than Mix 50. Law 1 would have been unfixable.
//   2. "Mix 100 % = zero dry" becomes EXACTLY measurable: at Mix 1.0 the output at a notch is
//      precisely the bypass-dry leak, so the measured null depth IS the residual in dB.
//   3. apBlend 1.0 (pure all-pass, no sum) is the Uni-Vibe vibrato voicing and is a CHARACTER
//      bit — the same knob set gives you both machines.
//   `comp` is a correlation-aware trim on the equal-power crossfade: a phaser's wet is 50 % dry
//   by construction, so a naive equal-power blend rings +1 dB at mid-mix and −3 dB at Mix 100.
//   comp = 1/‖(mixDry + mixWet(1−apBlend), mixWet·apBlend)‖ makes the Mix knob level-flat and
//   is exactly 1.0 at Mix 0 (bit-transparent) — measured, not assumed.
//
// ── FLOOR IS A CLAMP, NOT A FILTER (this cost the first build every deep notch) ──
// The first version high-passed the (A{x} − x) difference at the Floor frequency. That is fatal:
// a perfect null needs the difference to be EXACTLY −2x at the notch, and ANY filter in that
// path rotates it. A 20 Hz one-pole capped every null in the device at about −37 dB, and a
// 1 kHz Floor capped a 5 kHz notch at −14 dB. Measured, deleted, replaced: Floor now raises the
// sweep's lower bound so THE LOWEST NOTCH CANNOT GO BELOW IT. Below the lowest notch the cascade
// phase is small, A ≈ 1, and the output is the dry signal anyway — so the bass is left alone by
// physics instead of by a filter, and the nulls stay infinite.
//
// ── RECYCLED, NOT REINVENTED (law 10) ───────────────────────────────────────
//   · `filters::AllpassStage` / `filters::fastTanh` — TerrainFilters.h:878 / :42, verbatim.
//   · the 20-entry sync division list — cloned WHOLE from PluginProcessor.cpp:3479 incl. "Free".
//   · the swap dip / state re-seat idiom — FilterFxEngine.h:120-136 (fb345), not a new fader.
//   · the one-clock law (fb342) — ONE accumulator; L/R, LFO-B and the S+H clock read it with
//     offsets and ratios. Two accumulators plus a rate glide is the DIGITAL Spread bug again.
//
// ── MANDATORY SAFETY, APPLIED AT BIRTH ──────────────────────────────────────
//   · A 1st-order all-pass has H(DC) = +1, so the feedback loop passes DC with gain k and at
//     k = 0.95 LATCHES any offset ×20 (the Phase-G DC-latch class). A 10 Hz one-pole HP lives
//     INSIDE the loop on every topology. Not optional, not discovered in certification.
//   · The Color drive's 1/g makeup is INSIDE the loop, so sat'(0)·makeup = 1 and cranking Color
//     can never push loop gain past k. Cross-feed clamps the PRODUCT of both taps.
//   · TOPOLOGY-CLASS PARAMS ARE DOUBLE-BUFFERED. stage count, loop wiring, LFO shape and
//     apBlend are staged in `t*` fields and committed only at the bottom of the swap dip. The
//     first build applied the new stage count immediately while the old stagger table was still
//     live — a wrong-but-bounded cascade ran at full gain for ~2 ms and the click test caught it
//     at −43 dBFS.
//   · Every recirculating state is denormal-flushed; ScopedNoDenormals is assumed absent.
//
// No allocation, no locks, no std::function anywhere reachable from processStereo.
// ─────────────────────────────────────────────────────────────────────────────

#include "TerrainFilters.h"

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace tw {

class TerrainPhaserFx
{
public:
    // ── identity ─────────────────────────────────────────────────────────────
    static constexpr int kNumTypes  = 9;
    static constexpr int kNumChars  = 8;
    static constexpr int kMaxStages = 16;   // all-pass cascade ceiling
    static constexpr int kMaxBank   = 12;   // Barber notch-bank ceiling
    static constexpr int kNumDivs   = 20;

    enum TypeId { T_NINETY = 0, T_STONE, T_DUO, T_TWELVE, T_KRAUT, T_VIBE, T_BARBER, T_ENVY, T_STEPS };

    static const char* const* typeNames() noexcept
    {
        static const char* const N[kNumTypes] =
            { "Ninety", "Stone", "Duo", "Twelve", "Kraut", "Vibe", "Barber", "Envy", "Steps" };
        return N;
    }

    static const char* const* charNames (int type) noexcept
    {
        static const char* const C[kNumTypes][kNumChars] = {
        /* Ninety */ { "Script 74", "Block 78", "Two Stage", "Eight Stage",
                       "Slow Lamp", "Sine Sweep", "Wide Stagger", "Negative" },
        /* Stone  */ { "Color Off", "Color On", "Deep Sweep", "Two Loop Stages",
                       "Hot OTA", "Cold OTA", "Six Stage", "Inverted" },
        /* Duo    */ { "Series 1:1.33", "Series 3:4", "Parallel 1:1.33", "Parallel Golden",
                       "Counter", "Wide Duo", "Slow B", "Cross Feed" },
        /* Twelve */ { "Full Range", "Hi Range", "Six Pole", "Sixteen Pole",
                       "Resonant", "Hollow", "Aux Out", "Fast Hollow" },
        /* Kraut  */ { "Slow Bulbs", "Fast Bulbs", "Hard Skew", "Reverse Skew",
                       "Twelve Bulb", "Four Bulb", "Hot Loop", "Cold Loop" },
        /* Vibe   */ { "Chorus Lamp", "Vibrato Lamp", "Cold Bulb", "Hot Bulb",
                       "Eight Cap", "Even Caps", "Wide Caps", "Vibrato Deep" },
        /* Barber */ { "Rise 8", "Rise 12", "Fall 8", "Fall 12",
                       "Rise Wide", "Fall Narrow", "Sharp Notch", "Deep Rise" },
        /* Envy   */ { "Fast Grab", "Slow Swell", "Transient", "Smooth Follow",
                       "Four Stage", "Ten Stage", "Quack", "Sink" },
        /* Steps  */ { "Random 8", "Random Wide", "Ladder Up", "Ladder Down",
                       "Pendulum", "Register", "Drunk", "Trance Gate" } };
        const int t = (type < 0 ? 0 : (type >= kNumTypes ? kNumTypes - 1 : type));
        return C[t];
    }

    static_assert (kNumTypes > 0 && kNumChars == 8, "roster/table must move together");

    // ── the locked Params (CONTRACT §2). Front three, named per device:
    //      rate = Rate · depth = Depth · feedback = Feedback (0..1 MAGNITUDE — the SIGN is a
    //      Character bit; see ROSTER.md "why the feedback knob is not bipolar").
    //    Back eight: b1 Center · b2 Stages · b3 Spread · b4 Stereo · b5 Touch · b6 Lag ·
    //                b7 Floor · b8 Color
    struct Params
    {
        int   type = 0, character = 0;
        float rate = 0.35f, depth = 0.5f, feedback = 0.0f;
        float mix  = 0.5f;
        float b1=0.5f,b2=0.5f,b3=0.5f,b4=0.5f,b5=0.5f,b6=0.5f,b7=0.5f,b8=0.5f;
        bool  tempoSync = false; double bpm = 120.0;
    };

    struct Viz
    {
        float lfo = 0.0f;           // −1..+1 instantaneous sweep — THE needle
        float lvl = 0.0f;           // wet level 0..1
        float notch[8] {};          // notch centres in Hz, 0 = unused
        float depthNow = 0.0f;      // effective excursion, OCTAVES (phaser units)
    };

    // ═════════════════════════════════════════════════════════════════════════
    void prepare (double sampleRate, int /*maxBlock*/) noexcept
    {
        fs_ = (sampleRate > 8000.0 ? (float) sampleRate : 48000.0f);
        buildLut();
        hpA_  = 1.0f - std::exp (-6.2831853f * 10.0f / fs_);        // 10 Hz in-loop AC couple
        lvlA_ = 1.0f - std::exp (-1.0f / (fs_ * 0.030f));
        dipDn_ = 1.0f - std::exp (-1.0f / (fs_ * 0.004f));           // ~4 ms down
        dipUp_ = 1.0f - std::exp (-1.0f / (fs_ * 0.100f));           // ~100 ms back
        vizEvery_ = (int) (fs_ / 60.0f); if (vizEvery_ < 32) vizEvery_ = 32;
        planKey_ = -1; stagKey_ = -1;
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c) { uA_[c].reset(); uB_[c].reset(); bank_[c].reset(); }
        phase_ = 0.0f; inc_ = 0.0f;
        lagL_ = lagR_ = 0.0f;
        shL_ = shR_ = 0.0f; shPrevL_ = shPrevR_ = 0.0f;
        stepIdxL_ = stepIdxR_ = 0; stepDirL_ = stepDirR_ = 1;
        regL_ = 0xA3u; regR_ = 0x5Cu; rngS_ = 0x1234567u;
        env_ = envFast_ = envSlow_ = 0.0f;
        lvlSm_ = 0.0f;
        dip_ = 1.0f; pendingKey_ = -1;
        seeded_ = false;
        bankCtr_ = 0; vizCtr_ = 0;
        viz_ = Viz();
    }

    // ── per block. Everything transcendental that is not per-sample lives here.
    void setParams (const Params& pin) noexcept
    {
        Params p = pin;
        p.type      = clampi (p.type, 0, kNumTypes - 1);
        p.character = clampi (p.character, 0, kNumChars - 1);
        pr_ = p;

        const CharSpec& cs = charSpec (p.type, p.character);
        const TypeSpec& ts = typeSpec (p.type);

        // ── stage count. The knob spans the TYPE's own honest range (never a clamp plateau);
        //    a Character shifts the whole window, it never pins the knob dead.
        const int lo = clampi (ts.loSt + cs.stageBias, 2, kMaxStages);
        const int hi = clampi (ts.hiSt + cs.stageBias, 2, kMaxStages);
        int st = (int) std::lround ((float) lo + clamp01 (p.b2) * (float) (hi - lo));
        st = clampi (st, 2, kMaxStages);
        if (p.type == T_BARBER) st = clampi (st, 4, kMaxBank);

        // ── STAGED (topology class) — committed only at the bottom of the swap dip
        tStages_  = st;
        tTopo_    = cs.topo;
        tLfo_     = cs.lfo;
        tAlt_     = cs.alt;
        tApBlend_ = cs.apBlend;
        tSkew_    = cs.skew;
        tRatioB_  = cs.ratioB;
        const int key = (p.type * 97 + p.character) * 32 + tStages_;
        if (key != planKey_) pendingKey_ = key;

        // ── Spread → stagger law. NOT part of the plan key: the stagger changes coefficients
        //    only, never the state layout, so it must not trigger a dip on every knob degree.
        const float sp = clamp01 (p.b3);
        spreadOct_ = std::log2 (1.0f + 3.0f * sp * sp) * cs.staggerMul;   // geometric r = 1 + 3t²
        vibeScale_ = (0.35f + 1.30f * sp) * cs.staggerMul;           // Vibe: cap-ratio exponent scale
        barberIv_  = (0.35f + 1.35f * sp) * cs.staggerMul;           // Barber: interval, octaves
        const int sk = (p.type * 97 + p.character) * 4096
                     + (int) std::lround (sp * 200.0f) * 32 + stages_;
        if (sk != stagKey_ && planKey_ >= 0 && pendingKey_ < 0) { stagKey_ = sk; buildStagger(); }

        // ── Center / Floor / sweep window, all in OCTAVES (log2 Hz). No per-sample log2.
        floorHz_ = 20.0f * std::pow (50.0f, clamp01 (p.b7));          // 20 Hz → 1 kHz
        const float centerHz = 40.0f * std::pow (225.0f, clamp01 (p.b1));   // 40 Hz → 9 kHz
        floorOct_  = std::log2 (floorHz_);
        octMax_    = std::log2 (0.45f * fs_);
        centerOct_ = std::log2 (centerHz);

        depthOct_ = 4.5f * std::pow (clamp01 (p.depth), 0.8f) * cs.depthMul;
        if (p.type == T_BARBER) depthOct_ = 0.0f;   // Depth = NOTCH DEPTH here, never an f0 saw
        touchOct_   = ((clamp01 (p.b5) - 0.5f) * 2.0f) * 8.0f;
        envBaseOct_ = ts.envBase;

        // ── Feedback: magnitude from the knob, SIGN from the Character.
        const float mag = std::fabs (cs.fbBias);
        const float sgn = (cs.fbBias < 0.0f ? -1.0f : 1.0f);
        fbK_ = clampf (sgn * (mag + clamp01 (p.feedback) * (0.998f - mag)), -0.998f, 0.998f);
        envToFb_ = (p.type == T_ENVY) ? cs.skew : 0.0f;   // Envy reuses `skew` as env→feedback

        // ── Color: in-loop LP + in-loop drive, makeup INSIDE the loop
        const float col = clamp01 (p.b8);
        float colHz = 18000.0f * std::pow (800.0f / 18000.0f, col);
        if (p.type == T_STONE) colHz *= 0.35f;      // the OTA's own bandwidth
        colorA_ = 1.0f - std::exp (-6.2831853f * colHz / fs_);
        loopDrv_    = std::max (1.0f, cs.loopDrv * (1.0f + col * 15.0f));
        invLoopDrv_ = 1.0f / loopDrv_;
        loopG_ = gAtHz (clampf (centerHz * 0.7f, 20.0f, 0.45f * fs_));   // Stone's extra loop stage

        // ── Lag (b6): ONE motion time constant — lamp thermal lag, S+H glide, envelope speed.
        const float lagS = (0.004f + 0.196f * clamp01 (p.b6)) * cs.lagMul;
        lagA_ = 1.0f - std::exp (-1.0f / (fs_ * std::max (5.0e-4f, lagS)));
        const float atkS = (0.001f + 0.059f * clamp01 (p.b6)) * cs.lagMul;
        const float relS = (0.030f + 0.570f * clamp01 (p.b6)) * cs.lagMul;
        envAtk_ = 1.0f - std::exp (-1.0f / (fs_ * std::max (2.0e-4f, atkS)));
        envRel_ = 1.0f - std::exp (-1.0f / (fs_ * std::max (5.0e-3f, relS)));

        // ── Stereo (b4): LFO phase offset AND a per-channel centre split. The split is what
        //    keeps the effect alive in a mono fold-down — a pure phase offset near 180° lets the
        //    L notch fill the R notch and the phasing audibly vanishes (bible §6, pitfall #8).
        const float stw = clamp01 (p.b4);
        stPhase_ = wrap01 (stw * 0.44f + cs.stPhase);                // 0 → 158°, + Character
        stSplit_ = stw * 0.42f;                                       // ±0.42 octave at 100

        // ── Rate. One accumulator; the increment glides, the phase never jumps.
        float hz;
        if (p.tempoSync)
        {
            const int   idx   = clampi ((int) std::lround (clamp01 (p.rate) * (kNumDivs - 1)), 0, kNumDivs - 1);
            const float beats = divBeats (idx);
            const float bpm   = (float) (pr_.bpm > 1.0 ? pr_.bpm : 120.0);
            hz = (beats > 0.0f) ? (bpm / 60.0f) / beats : 0.7f;
        }
        else hz = 0.01f * std::pow (2000.0f, clamp01 (p.rate));       // 0.01 → 20 Hz, log
        if (p.type == T_TWELVE) hz *= cs.ratioB;                      // Hi Range → 250 Hz
        incTgt_ = hz / fs_;

        // ── Barber bank constants
        barberDir_  = (cs.alt == 1) ? -1.0f : 1.0f;
        barberQ_    = (2.0f + 22.0f * clamp01 (p.feedback)) * cs.qMul;
        barberLmax_ = -(4.0f + 66.0f * clamp01 (p.depth) * cs.depthMul);   // dB, −4 → −70

        // ── Mix: equal power, then the correlation-aware trim (see the header note)
        mixDry_ = std::cos (clamp01 (p.mix) * 1.5707963f);
        mixWet_ = std::sin (clamp01 (p.mix) * 1.5707963f);
        const float a = mixDry_ + mixWet_ * (1.0f - apBlend_), b = mixWet_ * apBlend_;
        mixComp_ = 1.0f / std::sqrt (std::max (1.0e-6f, a * a + b * b));
    }

    // ═════════════════════════════════════════════════════════════════════════
    void processStereo (float* L, float* R, int numSamples) noexcept
    {
        if (pendingKey_ >= 0 && planKey_ < 0) commit();               // first block: no dip

        for (int i = 0; i < numSamples; ++i)
        {
            // derived from the ACTIVE topology, which can commit mid-block at the dip floor
            const int   nA     = (topo_ >= 4 && topo_ <= 7) ? std::max (2, stages_ / 2) : stages_;
            const float octMin = std::min (floorOct_ + notchAdj_, octMax_ - 0.5f);
            const float inL = L[i], inR = R[i];
            if (! seeded_) { seedStates (inL, inR); seeded_ = true; }

            // ── 1. topology swap under a dip (fb345). Nothing topological changes until the
            //       dip floor; the cascade is never wrong-length at full gain.
            if (pendingKey_ >= 0)
            {
                dip_ += (0.006f - dip_) * dipDn_;
                if (dip_ < 0.05f)
                { commit(); for (int c = 0; c < 2; ++c) { uA_[c].reset(); uB_[c].reset(); bank_[c].reset(); } }
            }
            else dip_ += (1.0f - dip_) * dipUp_;

            // ── 2. ONE clock
            inc_ += (incTgt_ - inc_) * 0.0015f;
            phase_ += inc_; if (phase_ >= 1.0f) phase_ -= 1.0f;

            // ── 3. envelope follower (peak / smooth / transient per Character)
            const float rect = std::max (std::fabs (inL), std::fabs (inR));
            env_ += (rect > env_ ? envAtk_ : envRel_) * (rect - env_);
            float det = env_;
            if (pr_.type == T_ENVY && altSrc_ == 1)
            { envSlow_ += 0.0006f * (rect - envSlow_); det = envSlow_; }
            else if (pr_.type == T_ENVY && altSrc_ == 2)
            {
                envFast_ += 0.02f   * (rect - envFast_);
                envSlow_ += 0.0012f * (rect - envSlow_);
                det = std::max (0.0f, envFast_ - envSlow_) * 2.5f;
            }
            // BUS REALITY (law 1): the FX bus program peaks near 0.2 lin (−26 dBFS RMS, crest ~4).
            // A hard clamp at env/0.05 would sit pinned at 1.0 on any normal program and the
            // Touch knob would read as dead. A soft knee always moves and never plateaus.
            const float gEnv  = det * 10.0f;
            const float env01 = gEnv / (1.0f + gEnv);

            // ── 4. motion source: shape → lamp lag, per channel, from ONE accumulator
            const float rawL = lfoValue (phase_, false);
            const float phR  = wrap01 (phase_ + stPhase_);
            const float rawR = lfoValue (phR, true);
            lagL_ += (rawL - lagL_) * lagA_;
            lagR_ += (rawR - lagR_) * lagA_;

            // ── 5. the sweep, in octaves.  FLOOR IS A CLAMP: the lowest notch cannot go under it.
            const float envOct = (envBaseOct_ + touchOct_) * env01;
            float octL = clampf (centerOct_ + depthOct_ * lagL_ + envOct + stSplit_, octMin, octMax_);
            float octR = clampf (centerOct_ + depthOct_ * lagR_ + envOct - stSplit_, octMin, octMax_);

            // Envy `Quack`: the envelope also opens the resonance.
            const float kNow = clampf (fbK_ + envToFb_ * env01 * (fbK_ < 0.0f ? -1.0f : 1.0f), -0.998f, 0.998f);

            float pL, pR;
            if (topo_ == 8)
            {
                if (--bankCtr_ <= 0) { bankCtr_ = 16; updateBank (octL, octR); }
                pL = inL + apBlend_ * (bank_[0].process (inL) - inL);
                pR = inR + apBlend_ * (bank_[1].process (inR) - inR);
            }
            else
            {
                pL = runTopology (0, inL, octL, nA, kNow, lagL_);
                pR = runTopology (1, inR, octR, nA, kNow, lagR_);
            }

            // transparent soft limiter (the in-tree PhaserCore form) — bounds the +9 dB resonant
            // peaks without touching program level.
            pL = 4.0f * filters::fastTanh (0.25f * pL);
            pR = 4.0f * filters::fastTanh (0.25f * pR);

            // dip only the PROCESSED delta, so a swap never mutes the dry
            pL = inL + (pL - inL) * dip_;
            pR = inR + (pR - inR) * dip_;

            L[i] = mixComp_ * (mixDry_ * inL + mixWet_ * pL);
            R[i] = mixComp_ * (mixDry_ * inR + mixWet_ * pR);

            lvlSm_ += lvlA_ * (std::max (std::fabs (pL - inL), std::fabs (pR - inR)) - lvlSm_);
            if (++vizCtr_ >= vizEvery_) { vizCtr_ = 0; publish (octL); }
        }
    }

    const Viz& viz() const noexcept { return viz_; }

    // the sync division table, cloned WHOLE from PluginProcessor.cpp:3479 including "Free" at
    // index 0 (a 19-entry list starting at "4 bar" reads every saved Rate one division fast).
    static float divBeats (int i) noexcept
    {
        static const float B[kNumDivs] = { 0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 3.0f, 1.3333f, 1.0f,
                                           1.5f, 0.6667f, 0.5f, 0.75f, 0.3333f, 0.25f, 0.375f,
                                           0.1667f, 0.125f, 0.0625f, 0.03125f, 0.015625f };
        return B[i < 0 ? 0 : (i >= kNumDivs ? kNumDivs - 1 : i)];
    }

private:
    // ═════ helpers ═══════════════════════════════════════════════════════════
    static inline int   clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static inline float clamp01 (float v) noexcept { return clampf (v, 0.0f, 1.0f); }
    static inline float wrap01 (float v) noexcept { v -= std::floor (v); return v; }
    static inline float flush (float v) noexcept { return (v > -1.0e-25f && v < 1.0e-25f) ? 0.0f : v; }

    // ═════ character / type tables ═══════════════════════════════════════════
    // Every field is PHYSICS: stage count, LFO shape, loop wiring, stagger law, lamp time,
    // detector law, sweep polarity. Nothing here is a tone control (law R4 / fb345).
    struct CharSpec
    {
        int8_t stageBias;   // shifts the Stages knob's whole window
        int8_t lfo;         // 0 tri · 1 sine · 2 hypertri · 3 LDR-skew · 4 saw · 5 S+H
        int8_t topo;        // 0 plain · 1 +1 loop AP · 2 +2 loop AP · 3 nonlinear loop ·
                            // 4 duo series · 5 duo parallel · 6 duo wide · 7 duo cross-feed · 8 notch bank
        int8_t alt;         // Steps: S+H source · Barber: 1 = descend · Envy: detector law
        float  fbBias;      // SIGNED feedback floor; sign() flips the peak geography
        float  scatter;     // per-stage break scatter, octaves
        float  staggerMul;  // multiplies the Spread-derived stagger / interval
        float  lagMul;      // multiplies the Lag time constant
        float  ratioB;      // Duo: LFO-B ratio (negative = inverted) · Twelve: rate-top multiplier
        float  apBlend;     // 0.5 classic sum · 1.0 all-pass only (vibrato)
        float  depthMul;    // excursion multiplier
        float  loopDrv;     // in-loop drive multiplier (OTA hardness)
        float  skew;        // LDR duty warp −1..+1 · Envy: env→feedback amount
        float  qMul;        // Barber notch Q multiplier
        float  stPhase;     // extra stereo LFO phase, turns
    };
    struct TypeSpec { int8_t loSt, hiSt; float envBase; };

    static const TypeSpec& typeSpec (int t) noexcept
    {
        static const TypeSpec T[kNumTypes] = {
            /* Ninety */ {  2, 10, 0.0f },
            /* Stone  */ {  2, 10, 0.0f },
            /* Duo    */ {  4, 16, 0.0f },
            /* Twelve */ {  6, 16, 0.0f },
            /* Kraut  */ {  4, 16, 0.0f },
            /* Vibe   */ {  4, 16, 0.0f },
            /* Barber */ {  4, 12, 0.0f },
            /* Envy   */ {  2, 12, 2.6f },   // the envelope is pre-wired hot; Touch adds on top
            /* Steps  */ {  2, 12, 0.0f } };
        return T[clampi (t, 0, kNumTypes - 1)];
    }

    static const CharSpec& charSpec (int t, int c) noexcept
    {
        //           stg lfo top alt   fbBias  scat  stag   lag   ratioB apB  dpth  drv   skew  qM   stPh
        static const CharSpec C[kNumTypes][kNumChars] = {
        // ── Ninety — 4 identical JFET stages, triangle. Spread 0 collapses to identical breaks,
        //    which is where the real pedal's 5.83:1 two-notch law lives (58.5 / 340.8 Hz).
        { {  0,0,0,0,  0.00f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Script 74  (no fb resistor)
          {  0,0,0,0,  0.40f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Block 78   (R28 regeneration)
          { -2,0,0,0,  0.00f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Two Stage  (ONE notch)
          {  4,0,0,0,  0.25f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Eight Stage
          {  0,0,0,0,  0.10f,0.06f,1.00f,8.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Slow Lamp
          {  0,1,0,0,  0.15f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Sine Sweep
          {  0,0,0,0,  0.15f,0.06f,2.40f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Wide Stagger
          {  0,0,0,0, -0.45f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f } },// Negative (peaks land ON the notches)

        // ── Stone — OTA cascade with a DEDICATED extra all-pass in the FEEDBACK path. That extra
        //    stage changes the loop phase law, so the resonant peaks sit where a 4-stage loop
        //    structurally cannot put them: Stone is not Ninety-with-feedback.
        { {  0,2,1,0,  0.00f,0.05f,1.00f,1.00f, 1.0f,0.5f,1.00f,2.0f, 0.0f,1.0f,0.0f },  // Color Off
          {  0,2,1,0,  0.55f,0.05f,1.00f,1.00f, 1.0f,0.5f,1.40f,2.5f, 0.0f,1.0f,0.0f },  // Color On (fb AND depth, like the switch)
          {  0,2,1,0,  0.30f,0.05f,1.00f,1.00f, 1.0f,0.5f,1.90f,1.0f, 0.0f,1.0f,0.0f },  // Deep Sweep
          {  0,2,2,0,  0.45f,0.05f,1.00f,1.00f, 1.0f,0.5f,1.00f,1.0f, 0.0f,1.0f,0.0f },  // Two Loop Stages
          {  0,2,1,0,  0.40f,0.05f,1.00f,1.00f, 1.0f,0.5f,1.00f,4.0f, 0.0f,1.0f,0.0f },  // Hot OTA
          {  0,2,1,0,  0.75f,0.05f,1.00f,2.50f, 1.0f,0.5f,1.00f,1.0f, 0.0f,1.0f,0.0f },  // Cold OTA
          {  2,2,1,0,  0.30f,0.05f,1.00f,1.00f, 1.0f,0.5f,1.00f,1.0f, 0.0f,1.0f,0.0f },  // Six Stage
          {  0,2,1,0, -0.60f,0.05f,1.00f,1.00f, 1.0f,0.5f,1.00f,1.5f, 0.0f,1.0f,0.0f } },// Inverted

        // ── Duo — TWO cascades, TWO sweep generators, ONE accumulator (the one-clock law).
        { {  0,1,4,0,  0.00f,0.05f,1.00f,1.00f, 1.3333f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Series 1:1.33
          {  0,1,4,0,  0.30f,0.05f,1.00f,1.00f, 0.7500f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Series 3:4
          {  0,1,5,0,  0.25f,0.05f,1.00f,1.00f, 1.3333f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Parallel 1:1.33
          {  0,1,5,0,  0.25f,0.05f,1.00f,1.00f, 1.6180f,0.5f,1.4f,1.0f, 0.0f,1.0f,0.0f },  // Parallel Golden
          {  0,1,4,0,  0.35f,0.05f,1.00f,1.00f,-1.0000f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Counter (B inverted)
          {  0,1,6,0,  0.25f,0.05f,1.00f,1.00f, 1.3333f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },  // Wide Duo (B on R only)
          {  0,1,4,0,  0.30f,0.05f,1.00f,1.00f, 0.2500f,0.5f,1.6f,1.0f, 0.0f,1.0f,0.0f },  // Slow B
          {  0,1,7,0,  0.55f,0.05f,1.00f,1.00f, 1.5000f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f } },// Cross Feed

        // ── Twelve — the MF-103. 12 poles = 6 notches, and the ONLY Type whose Rate reaches
        //    AUDIO RATE (250 Hz), which is where phaser FM sidebands come from.
        { {  0,1,0,0,  0.00f,0.03f,1.00f,1.00f, 1.0f,0.5f,1.00f,1.0f, 0.0f,1.0f,0.0f },  // Full Range
          {  0,1,0,0,  0.35f,0.03f,1.00f,1.00f,12.5f,0.5f,0.55f,1.0f, 0.0f,1.0f,0.0f },  // Hi Range  → 250 Hz
          { -6,1,0,0,  0.20f,0.03f,1.00f,1.00f, 1.0f,0.5f,1.00f,1.0f, 0.0f,1.0f,0.0f },  // Six Pole
          {  6,1,0,0,  0.30f,0.03f,1.00f,1.00f, 1.0f,0.5f,1.00f,1.0f, 0.0f,1.0f,0.0f },  // Sixteen Pole
          {  0,1,0,0,  0.80f,0.03f,1.00f,1.00f, 1.0f,0.5f,1.00f,1.0f, 0.0f,1.0f,0.0f },  // Resonant
          {  0,1,0,0, -0.65f,0.03f,1.00f,1.00f, 1.0f,0.5f,1.00f,1.0f, 0.0f,1.0f,0.0f },  // Hollow
          {  0,1,0,0,  0.30f,0.03f,1.00f,1.00f, 1.0f,0.5f,1.00f,1.0f, 0.0f,1.0f,0.5f },  // Aux Out (counter-phase R)
          {  0,1,0,0, -0.50f,0.03f,1.00f,1.00f,12.5f,0.5f,0.55f,1.0f, 0.0f,1.0f,0.0f } },// Fast Hollow

        // ── Kraut — Schulte: LDR duty-warped sweep, lamp lag, NONLINEAR filter in the loop.
        { {  0,3,3,0,  0.00f,0.06f,1.00f,3.00f, 1.0f,0.5f,1.0f,1.5f, 0.50f,1.0f,0.0f },  // Slow Bulbs
          {  0,3,3,0,  0.45f,0.06f,1.00f,0.30f, 1.0f,0.5f,1.0f,1.5f, 0.30f,1.0f,0.0f },  // Fast Bulbs
          {  0,3,3,0,  0.35f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.5f, 2.00f,1.0f,0.0f },  // Hard Skew
          {  0,3,3,0,  0.35f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.5f,-2.00f,1.0f,0.0f },  // Reverse Skew
          {  4,3,3,0,  0.30f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.5f, 0.50f,1.0f,0.0f },  // Twelve Bulb
          { -4,3,3,0,  0.30f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.5f, 0.50f,1.0f,0.0f },  // Four Bulb
          {  0,3,3,0,  0.60f,0.06f,1.00f,1.00f, 1.0f,0.5f,1.0f,4.5f, 0.70f,1.0f,0.0f },  // Hot Loop
          {  0,3,0,0, -0.55f,0.06f,1.00f,1.80f, 1.0f,0.5f,1.0f,1.0f, 0.70f,1.0f,0.0f } },// Cold Loop (linear loop)

        // ── Vibe — the four measured Uni-Vibe capacitors (0.015 µF / 0.22 µF / 470 pF / 4.7 nF).
        //    Breaks ∝ 1/C ⇒ 0.616× / 0.042× / 19.66× / 1.966× of the geometric centre: inharmonic
        //    by build, which is why a Uni-Vibe THROBS where a Phase 90 SWOOSHES.
        { {  0,0,0,0,  0.00f,0.03f,1.00f,3.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },   // Chorus Lamp
          {  0,0,0,0,  0.00f,0.03f,1.00f,2.00f, 1.0f,1.0f,1.0f,1.0f, 0.0f,1.0f,0.0f },   // Vibrato Lamp (all-pass only)
          {  0,0,0,0,  0.20f,0.03f,1.00f,12.0f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },   // Cold Bulb
          {  0,0,0,0,  0.20f,0.03f,1.00f,0.12f, 1.0f,0.5f,1.4f,1.0f, 0.0f,1.0f,0.0f },   // Hot Bulb
          {  4,0,0,0,  0.20f,0.03f,1.00f,2.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },   // Eight Cap
          {  0,0,0,0,  0.25f,0.03f,0.30f,2.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },   // Even Caps
          {  0,0,0,0,  0.25f,0.03f,1.75f,2.00f, 1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },   // Wide Caps
          {  0,0,0,0,  0.00f,0.03f,1.00f,0.50f, 1.0f,1.0f,1.7f,1.0f, 0.0f,1.0f,0.0f } }, // Vibrato Deep

        // ── Barber — DAFx-15 Method 1. M cascaded 2nd-order NOTCHES, one interval apart,
        //    sawtooth centre + raised-cosine depth window. No feedback loop, no delay line.
        //    apBlend 1.0: the bank IS the wet, there is no all-pass sum to make.
        { {  0,4,8,0,  0.0f,0.0f,1.00f,1.0f,1.0f,1.0f,1.0f,1.0f, 0.0f,1.00f,0.0f },      // Rise 8
          {  4,4,8,0,  0.0f,0.0f,1.00f,1.0f,1.0f,1.0f,1.0f,1.0f, 0.0f,1.00f,0.0f },      // Rise 12
          {  0,4,8,1,  0.0f,0.0f,1.00f,1.0f,1.0f,1.0f,1.0f,1.0f, 0.0f,1.00f,0.0f },      // Fall 8
          {  4,4,8,1,  0.0f,0.0f,1.00f,1.0f,1.0f,1.0f,1.0f,1.0f, 0.0f,1.00f,0.0f },      // Fall 12
          {  0,4,8,0,  0.0f,0.0f,1.60f,1.0f,1.0f,1.0f,1.0f,1.0f, 0.0f,1.00f,0.0f },      // Rise Wide
          {  0,4,8,1,  0.0f,0.0f,0.55f,1.0f,1.0f,1.0f,1.0f,1.0f, 0.0f,1.00f,0.0f },      // Fall Narrow
          {  0,4,8,0,  0.0f,0.0f,0.80f,1.0f,1.0f,1.0f,1.0f,1.0f, 0.0f,3.00f,0.0f },      // Sharp Notch
          { -2,4,8,0,  0.0f,0.0f,1.20f,1.0f,1.0f,1.0f,1.0f,1.0f, 0.0f,0.45f,0.0f } },    // Deep Rise (fewer, fatter)

        // ── Envy — the motion source IS the circuit (Eventide made the sweep source a
        //    first-class selector in 1971). `skew` carries env→feedback for Quack.
        { {  0,0,0,0,  0.00f,0.05f,1.00f,0.20f, 1.0f,0.5f,1.0f,1.0f, 0.00f,1.0f,0.0f },  // Fast Grab
          {  0,0,0,0,  0.30f,0.05f,1.00f,3.50f, 1.0f,0.5f,1.0f,1.0f, 0.00f,1.0f,0.0f },  // Slow Swell
          {  0,0,0,2,  0.40f,0.05f,1.00f,0.50f, 1.0f,0.5f,1.0f,1.0f, 0.00f,1.0f,0.0f },  // Transient
          {  0,0,0,1,  0.30f,0.05f,2.20f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.00f,1.0f,0.0f },  // Smooth Follow
          { -4,0,0,0,  0.30f,0.05f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.00f,1.0f,0.0f },  // Four Stage
          {  4,0,0,0,  0.30f,0.05f,1.00f,1.00f, 1.0f,0.5f,1.0f,1.0f, 0.00f,1.0f,0.0f },  // Ten Stage
          {  0,0,0,0,  0.55f,0.05f,1.00f,0.25f, 1.0f,0.5f,1.0f,1.0f, 0.35f,1.0f,0.0f },  // Quack (env → resonance)
          {  0,0,0,0, -0.60f,0.05f,1.00f,1.20f, 1.0f,0.5f,1.0f,1.0f, 0.00f,1.0f,0.0f } },// Sink

        // ── Steps — sample & hold clocked by Rate; Lag is the glide between holds.
        { {  0,5,0,0,  0.00f,0.05f,1.00f,1.0f,1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },     // Random 8
          {  0,5,0,1,  0.30f,0.05f,1.00f,1.0f,1.0f,0.5f,1.5f,1.0f, 0.0f,1.0f,0.0f },     // Random Wide (16 levels, deeper)
          {  0,5,0,2,  0.35f,0.05f,1.00f,1.0f,1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },     // Ladder Up
          {  0,5,0,3,  0.35f,0.05f,1.00f,1.0f,1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },     // Ladder Down
          {  0,5,0,4,  0.35f,0.05f,1.00f,1.0f,1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },     // Pendulum
          {  0,5,0,5,  0.40f,0.05f,1.00f,1.0f,1.0f,0.5f,1.0f,1.0f, 0.0f,1.0f,0.0f },     // Register
          {  0,5,0,6,  0.35f,0.05f,1.00f,1.0f,1.0f,0.5f,0.8f,1.0f, 0.0f,1.0f,0.0f },     // Drunk
          {  0,5,0,7,  0.50f,0.05f,1.00f,1.0f,1.0f,0.5f,1.4f,1.0f, 0.0f,1.0f,0.0f } } }; // Trance Gate
        return C[clampi (t, 0, kNumTypes - 1)][clampi (c, 0, kNumChars - 1)];
    }

    // ═════ the tan LUT — no transcendentals on the coefficient path ══════════
    static constexpr int kLut = 2048;

    void buildLut() noexcept
    {
        lutLo_ = std::log2 (10.0f);
        lutHi_ = std::log2 (0.4995f * fs_);
        lutScale_ = (float) (kLut - 1) / (lutHi_ - lutLo_);
        for (int i = 0; i < kLut; ++i)
        {
            const float f = std::exp2 (lutLo_ + (float) i / lutScale_);
            const float t = std::tan (3.14159265f * f / fs_);
            lut_[i] = (t - 1.0f) / (t + 1.0f);
        }
    }
    inline float gAt (float log2f) const noexcept
    {
        float x = (log2f - lutLo_) * lutScale_;
        if (x < 0.0f) x = 0.0f; else if (x > (float) kLut - 1.001f) x = (float) kLut - 1.001f;
        const int i = (int) x; const float fr = x - (float) i;
        return lut_[i] + fr * (lut_[i + 1] - lut_[i]);
    }
    inline float gAtHz (float hz) const noexcept { return gAt (std::log2 (std::max (10.0f, hz))); }

    // ═════ the all-pass unit: one cascade + its own loop ═════════════════════
    struct NlLp2   // Kraut's nonlinear filter in the feedback path (the ChowPhaser topology)
    {
        float s1 = 0.0f, s2 = 0.0f, g = 0.10f, R = 0.55f;
        inline float process (float x) noexcept
        {
            const float hp = (x - (2.0f * R + g) * s1 - s2) / (1.0f + 2.0f * R * g + g * g);
            const float bp = g * hp + s1;  s1 = flush (g * hp + bp);
            const float lp = g * bp + s2;  s2 = flush (g * bp + lp);
            return filters::fastTanh (lp * 1.6f) * 0.625f;
        }
        void reset() noexcept { s1 = s2 = 0.0f; }
    };

    struct Unit
    {
        filters::AllpassStage ap[kMaxStages];
        filters::AllpassStage loopAp[2];
        NlLp2 nl;
        float fbS = 0.0f, hpS = 0.0f, lpS = 0.0f;
        void reset() noexcept
        {
            for (auto& a : ap) a.reset();
            for (auto& a : loopAp) a.reset();
            nl.reset(); fbS = hpS = lpS = 0.0f;
        }
    };

    // one cascade + its feedback loop. Returns the cascade output; updates the loop state.
    inline float unit (Unit& u, float x, int n, float oct, float k, float fbTap,
                       int loopExtra, bool nlLoop) noexcept
    {
        float v = x + k * fbTap;
        for (int i = 0; i < n; ++i)
        {
            u.ap[i].g = gAt (oct + stageOct_[i]);
            v = u.ap[i].process (v);
            u.ap[i].y1 = flush (u.ap[i].y1);
        }
        float t = v;
        for (int i = 0; i < loopExtra; ++i) { u.loopAp[i].g = loopG_; t = u.loopAp[i].process (t); }
        if (nlLoop) t = u.nl.process (t);
        u.lpS += colorA_ * (t - u.lpS); u.lpS = flush (u.lpS); t = u.lpS;           // in-loop LP
        if (loopDrv_ > 1.0001f) t = filters::fastTanh (t * loopDrv_) * invLoopDrv_; // makeup INSIDE
        u.hpS += hpA_ * (t - u.hpS); u.hpS = flush (u.hpS);
        u.fbS = flush (t - u.hpS);                                                  // 10 Hz AC couple
        return v;
    }

    // returns the PHASER OUTPUT (the summed signal), not the raw cascade — the series and
    // cross-feed topologies must sum phasor B against phasor A's OUTPUT, not against the
    // device input, or their nulls are not nulls.
    inline float runTopology (int ch, float x, float oct, int nA, float k, float lag) noexcept
    {
        Unit& a = uA_[ch];
        Unit& b = uB_[ch];
        const int  extra  = (topo_ == 1 ? 1 : (topo_ == 2 ? 2 : 0));
        const bool nlLoop = (topo_ == 3);

        if (topo_ < 4)
        {
            const float v = unit (a, x, nA, oct, k, a.fbS, extra, nlLoop);
            return x + apBlend_ * (v - x);
        }

        // ── Duo. LFO B rides the SAME accumulator; ratio < 0 means "inverted", not "second clock".
        float lagB;
        if (ratioB_ < 0.0f) lagB = -lag;
        else                lagB = lfoValue (wrap01 (phase_ * ratioB_ + (ch ? stPhase_ : 0.0f)), ch != 0);
        const float octB = clampf (centerOct_ + depthOct_ * lagB + (ch ? -stSplit_ : stSplit_),
                                   std::min (floorOct_ + notchAdj_, octMax_ - 0.5f), octMax_);

        if (topo_ == 5)   // parallel — the two combs ADD
        {
            const float vA = unit (a, x, nA, oct,  k, a.fbS, 0, false);
            const float vB = unit (b, x, nA, octB, k, b.fbS, 0, false);
            return x + apBlend_ * (0.5f * (vA + vB) - x);
        }
        if (topo_ == 6)   // wide — phasor B lives on the RIGHT channel only
        {
            const float vA = unit (a, x, nA, oct, k, a.fbS, 0, false);
            const float yA = x + apBlend_ * (vA - x);
            if (ch == 0) return yA;
            const float vB = unit (b, yA, nA, octB, k, b.fbS, 0, false);
            return yA + apBlend_ * (vB - yA);
        }
        if (topo_ == 7)   // cross-feed — A's loop is tapped from B. Clamp the PRODUCT of the taps.
        {
            const float kx = clampf (k, -0.92f, 0.92f);
            const float vA = unit (a, x, nA, oct,  kx, b.fbS, 0, false);
            const float yA = x + apBlend_ * (vA - x);
            const float vB = unit (b, yA, nA, octB, kx, a.fbS, 0, false);
            return yA + apBlend_ * (vB - yA);
        }
        // topo_ == 4 : series — the two combs MULTIPLY
        const float vA = unit (a, x, nA, oct, k, a.fbS, 0, false);
        const float yA = x + apBlend_ * (vA - x);
        const float vB = unit (b, yA, nA, octB, k, b.fbS, 0, false);
        return yA + apBlend_ * (vB - yA);
    }

    // ═════ the Barber notch bank (DAFx-15 Method 1) ══════════════════════════
    struct Biq
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        inline float p (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = flush (y);
            return y;
        }
        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; b0 = 1; b1 = b2 = a1 = a2 = 0; }
    };
    struct Bank
    {
        Biq f[kMaxBank];
        float pos[kMaxBank] {};
        void reset() noexcept { for (auto& q : f) q.reset(); for (auto& p : pos) p = 0.0f; }
        inline float process (float x) noexcept { for (auto& q : f) x = q.p (x); return x; }
    };

    void updateBank (float octL, float octR) noexcept
    {
        const int M = clampi (stages_, 4, kMaxBank);
        const float f0L = std::exp2 (octL), f0R = std::exp2 (octR);
        const float uL = (barberDir_ > 0.0f ? phase_ : 1.0f - phase_) * (float) M;
        const float uR = (barberDir_ > 0.0f ? wrap01 (phase_ + stPhase_)
                                            : 1.0f - wrap01 (phase_ + stPhase_)) * (float) M;
        for (int ch = 0; ch < 2; ++ch)
        {
            Bank& bk = bank_[ch];
            const float u  = (ch == 0 ? uL : uR);
            const float f0 = (ch == 0 ? f0L : f0R);
            for (int m = 0; m < M; ++m)
            {
                float pos = std::fmod ((float) m + u, (float) M);
                if (pos < 0.0f) pos += (float) M;
                // THE WRAP (the paper's own warning). The depth window is EXACTLY 0 dB at both
                // edges, so a section is an algebraic identity there — and a biquad with b == a
                // outputs its input EXACTLY when y1 == x1 and y2 == x2, for ANY coefficients. So
                // handing the wrapping section its own input history makes the jump click-free
                // by construction rather than by a fade. Measured at −110 dBFS.
                if (std::fabs (pos - bk.pos[m]) > 0.5f * (float) M)
                { bk.f[m].y1 = bk.f[m].x1; bk.f[m].y2 = bk.f[m].x2; }
                bk.pos[m] = pos;

                const float fc  = clampf (f0 * std::exp2 (pos * barberIv_), 20.0f, 0.45f * fs_);
                const float gDb = barberLmax_ * 0.5f * (1.0f - std::cos (6.2831853f * pos / (float) M));
                const float G   = std::pow (10.0f, gDb / 20.0f);
                const float w0  = 6.2831853f * fc / fs_;
                const float dw  = w0 / std::max (0.5f, barberQ_);       // constant-Q bandwidth
                const float beta = std::tan (std::min (1.50f, dw * 0.5f));
                const float d = 1.0f + beta;
                Biq& q = bk.f[m];
                q.b0 = (1.0f + G * beta) / d;
                q.b1 = -2.0f * std::cos (w0) / d;
                q.b2 = (1.0f - G * beta) / d;
                q.a1 = q.b1;
                q.a2 = (1.0f - beta) / d;
            }
            for (int m = M; m < kMaxBank; ++m)
            { Biq& q = bk.f[m]; q.b0 = 1; q.b1 = q.b2 = q.a1 = q.a2 = 0; }
        }
    }

    // ═════ motion ════════════════════════════════════════════════════════════
    inline float lfoValue (float ph, bool right) noexcept
    {
        switch (lfoShape_)
        {
            case 1:  return std::sin (6.2831853f * ph);
            case 2:  { const float t = tri (ph); return (t - 0.15f * t * t * t) * 1.176f; }
            case 3:  return tri (warp (ph, skew_));      // LDR duty warp — see below
            case 4:  return 2.0f * ph - 1.0f;
            case 5:  return sampleHold (ph, right);
            default: return tri (ph);
        }
    }
    static inline float tri (float ph) noexcept
    { return ph < 0.5f ? (-1.0f + 4.0f * ph) : (3.0f - 4.0f * ph); }

    // The LDR/lamp warp has to move the PEAK, not reshape each half — an LDR's attack and decay
    // differ, so the sweep spends more of the cycle going one way than the other. Warping the
    // PHASE and then taking the triangle does exactly that: at skew +1 the rise takes 80 % of the
    // cycle and the fall 20 %. (Warping inside each half, which the first build did, leaves the
    // duty cycle at 50/50 and the harness measured 1.13:1 asymmetry — i.e. nothing.)
    // Rational (exponential-bias) curve, never a power curve — the house law.
    static inline float warp (float u, float s) noexcept
    {
        const float q = std::exp2 (2.0f * s);
        return u / (u + q * (1.0f - u) + 1.0e-9f);
    }

    inline float sampleHold (float ph, bool right) noexcept
    {
        float& hold = right ? shR_ : shL_;
        float& prev = right ? shPrevR_ : shPrevL_;
        if (ph < prev)   // the clock ticked
        {
            int&   idx = right ? stepIdxR_ : stepIdxL_;
            int&   dir = right ? stepDirR_ : stepDirL_;
            uint32_t& reg = right ? regR_ : regL_;
            switch (altSrc_)
            {
                case 0:  hold = quant (rnd(), 8);  break;
                case 1:  hold = quant (rnd(), 16); break;
                case 2:  idx = (idx + 1) & 7; hold = -1.0f + 2.0f * (float) idx / 7.0f; break;
                case 3:  idx = (idx + 7) & 7; hold = -1.0f + 2.0f * (float) idx / 7.0f; break;
                case 4:  idx += dir; if (idx >= 7) { idx = 7; dir = -1; } if (idx <= 0) { idx = 0; dir = 1; }
                         hold = -1.0f + 2.0f * (float) idx / 7.0f; break;
                case 5:  { const uint32_t bit = ((reg >> 7) ^ ((rnd() > 0.875f) ? 1u : 0u)) & 1u;
                           reg = ((reg << 1) | bit) & 0xFFu;
                           hold = -1.0f + 2.0f * (float) reg / 255.0f; } break;
                case 6:  hold = clampf (hold + (rnd() > 0.0f ? 0.2857f : -0.2857f), -1.0f, 1.0f); break;
                default: hold = (hold > 0.0f ? -1.0f : 1.0f); break;
            }
        }
        prev = ph;
        return hold;
    }
    inline float rnd() noexcept
    { rngS_ = rngS_ * 1664525u + 1013904223u; return ((float) (rngS_ >> 8) / 8388608.0f) - 1.0f; }
    static inline float quant (float v, int n) noexcept
    { return -1.0f + 2.0f * std::floor (clampf (v * 0.5f + 0.5f, 0.0f, 0.9999f) * (float) n) / (float) (n - 1); }

    // ═════ plan commit / stagger ═════════════════════════════════════════════
    void commit() noexcept
    {
        planKey_ = pendingKey_; pendingKey_ = -1;
        stages_ = tStages_; topo_ = tTopo_; lfoShape_ = tLfo_; altSrc_ = tAlt_;
        apBlend_ = tApBlend_; skew_ = tSkew_; ratioB_ = tRatioB_;
        buildStagger();
        stagKey_ = -1;
        // the Mix trim depends on apBlend, which only exists after the commit
        const float a = mixDry_ + mixWet_ * (1.0f - apBlend_), b = mixWet_ * apBlend_;
        mixComp_ = 1.0f / std::sqrt (std::max (1.0e-6f, a * a + b * b));
    }

    void buildStagger() noexcept
    {
        const bool duo = (topo_ >= 4 && topo_ <= 7);
        const int  n   = duo ? std::max (2, stages_ / 2) : stages_;
        const CharSpec& cs = charSpec (pr_.type, pr_.character);

        // deterministic per-stage scatter (JFET Vgs / LDR track mismatch) — a STATIC offset
        // pattern, never noise: real units are mismatched, not noisy.
        uint32_t s = 0x9E3779B9u ^ (uint32_t) (pr_.type * 131 + pr_.character * 17);
        auto nx = [&s]() { s = s * 1664525u + 1013904223u; return ((float) (s >> 8) / 8388608.0f) - 1.0f; };

        if (pr_.type == T_VIBE)
        {
            // log2 of 0.616 / 0.042 / 19.66 / 1.966 — the measured cap ratios, breaks ∝ 1/C
            static const float capOct[4] = { -0.6989f, -4.5735f, 4.2973f, 0.9752f };
            for (int k = 0; k < n; ++k)
                stageOct_[k] = capOct[k & 3] * vibeScale_ + 0.5f * (float) (k >> 2) + cs.scatter * nx();
        }
        else
        {
            for (int k = 0; k < n; ++k)
                stageOct_[k] = ((float) k - (float) (n - 1) * 0.5f) * spreadOct_ + cs.scatter * nx();
        }
        for (int k = n; k < kMaxStages; ++k) stageOct_[k] = 0.0f;
        solveNotchFactors (n);
        // FLOOR = the LOWEST NOTCH, not the centre. notchNu_[0] < 1, so this raises the clamp.
        notchAdj_ = (topo_ == 8 || nNotch_ == 0) ? 0.0f : -std::log2 (notchNu_[0]);
    }

    // Notch positions of a cascade with breaks f_k = fc · 2^stageOct[k]:
    //   Φ(f) = Σ −2·atan(f / f_k),  notch where Φ = −(2m+1)π.
    // Scaling every f_k by a common factor scales the notch frequencies identically, so the
    // factors ν_m = f_notch/fc are CONSTANTS of the plan. Solved once; the card reads them free.
    void solveNotchFactors (int n) noexcept
    {
        nNotch_ = std::min (8, n / 2);
        for (int m = 0; m < nNotch_; ++m)
        {
            const double target = -(2.0 * m + 1.0) * 3.14159265358979;
            double lo = -18.0, hi = 18.0;
            for (int it = 0; it < 44; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                const double nu = std::pow (2.0, mid);
                double phi = 0.0;
                for (int k = 0; k < n; ++k)
                    phi += -2.0 * std::atan (nu / std::pow (2.0, (double) stageOct_[k]));
                if (phi > target) lo = mid; else hi = mid;
            }
            notchNu_[m] = (float) std::pow (2.0, 0.5 * (lo + hi));
        }
        for (int m = nNotch_; m < 8; ++m) notchNu_[m] = 0.0f;
    }

    void seedStates (float l, float r) noexcept
    {
        // re-entry seed (pitfall #6): never re-enter with zeroed history, or the first block is a
        // comb of onset spikes. Seed x1/y1 with the live input (the SubOsc.h:63-82 precedent).
        for (int c = 0; c < 2; ++c)
        {
            const float v = (c == 0 ? l : r);
            for (auto& a : uA_[c].ap)     { a.x1 = v; a.y1 = v; }
            for (auto& a : uB_[c].ap)     { a.x1 = v; a.y1 = v; }
            for (auto& a : uA_[c].loopAp) { a.x1 = v; a.y1 = v; }
            for (auto& a : uB_[c].loopAp) { a.x1 = v; a.y1 = v; }
        }
    }

    void publish (float octNow) noexcept
    {
        viz_.lfo = clampf (lagL_, -1.0f, 1.0f);
        viz_.lvl = clampf (lvlSm_ * 8.0f, 0.0f, 1.0f);
        viz_.depthNow = depthOct_;
        const float fc = std::exp2 (octNow);
        if (topo_ == 8)
        {
            const int M = clampi (stages_, 4, kMaxBank);
            for (int m = 0; m < 8; ++m)
                viz_.notch[m] = (m < M) ? clampf (fc * std::exp2 (bank_[0].pos[m] * barberIv_),
                                                  20.0f, 0.45f * fs_) : 0.0f;
        }
        else
            for (int m = 0; m < 8; ++m)
                viz_.notch[m] = (m < nNotch_) ? clampf (fc * notchNu_[m], 20.0f, 0.45f * fs_) : 0.0f;
    }

    // ═════ state ═════════════════════════════════════════════════════════════
    Params pr_;
    Viz    viz_;

    float fs_ = 48000.0f;
    float lut_[kLut] {};
    float lutLo_ = 0.0f, lutHi_ = 0.0f, lutScale_ = 1.0f;

    Unit uA_[2], uB_[2];
    Bank bank_[2];

    float stageOct_[kMaxStages] {};
    float notchNu_[8] {};
    int   nNotch_ = 0;
    float notchAdj_ = 0.0f;
    int   planKey_ = -1, pendingKey_ = -1, stagKey_ = -1;

    // ── ACTIVE topology (committed at the dip floor) ──
    int   stages_ = 4, topo_ = 0, lfoShape_ = 0, altSrc_ = 0;
    float apBlend_ = 0.5f, skew_ = 0.0f, ratioB_ = 1.0f;
    // ── STAGED targets (written by setParams) ──
    int   tStages_ = 4, tTopo_ = 0, tLfo_ = 0, tAlt_ = 0;
    float tApBlend_ = 0.5f, tSkew_ = 0.0f, tRatioB_ = 1.0f;

    float phase_ = 0.0f, inc_ = 0.0f, incTgt_ = 0.0f;
    float lagL_ = 0.0f, lagR_ = 0.0f, lagA_ = 0.01f;
    float shL_ = 0.0f, shR_ = 0.0f, shPrevL_ = 0.0f, shPrevR_ = 0.0f;
    int   stepIdxL_ = 0, stepIdxR_ = 0, stepDirL_ = 1, stepDirR_ = 1;
    uint32_t regL_ = 0xA3u, regR_ = 0x5Cu, rngS_ = 0x1234567u;

    float env_ = 0.0f, envFast_ = 0.0f, envSlow_ = 0.0f, envAtk_ = 0.01f, envRel_ = 0.001f;
    float lvlSm_ = 0.0f, lvlA_ = 0.001f;

    float centerOct_ = 9.2f, floorOct_ = 4.3f, octMax_ = 14.4f;
    float depthOct_ = 2.0f, touchOct_ = 0.0f, envBaseOct_ = 0.0f, envToFb_ = 0.0f;
    float spreadOct_ = 0.585f, vibeScale_ = 1.0f, barberIv_ = 1.0f, floorHz_ = 20.0f;
    float fbK_ = 0.0f, colorA_ = 1.0f, loopDrv_ = 1.0f, invLoopDrv_ = 1.0f, loopG_ = 0.0f;
    float hpA_ = 0.0013f;
    float mixDry_ = 0.7071f, mixWet_ = 0.7071f, mixComp_ = 1.0f;
    float stPhase_ = 0.0f, stSplit_ = 0.0f;
    float barberDir_ = 1.0f, barberQ_ = 8.0f, barberLmax_ = -20.0f;

    float dip_ = 1.0f, dipDn_ = 0.005f, dipUp_ = 0.00035f;
    bool  seeded_ = false;
    int   bankCtr_ = 0, vizCtr_ = 0, vizEvery_ = 800;
};

} // namespace tw
