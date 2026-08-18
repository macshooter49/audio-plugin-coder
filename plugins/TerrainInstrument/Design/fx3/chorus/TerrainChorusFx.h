#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  TerrainChorusFx.h — fb395. ONE instance of the FX-rack CHORUS device (chain kind 6).
//
//  Header-only, pure C++ (no JUCE), tw:: namespace, the locked Design/fx3/CONTRACT.md §2
//  interface. Offline-certified by chorus_cert.cpp, which lives next to this file.
//
//  ── WHAT A CHORUS IS, IN THIS FILE ───────────────────────────────────────────
//  One stereo circular buffer. Up to four READ TAPS per channel whose lengths move.
//  Every legendary chorus is exactly three choices — how many taps, what moves them, and
//  what the delay path does to the tone — so the Type dropdown walks the first two axes
//  through history and the back panel exposes the third everywhere.
//
//  ── THE BOUNDARY (CONTRACT §4) ───────────────────────────────────────────────
//  Chorus owns MULTI-TAP, DELAY-MODULATED, LOW feedback: pitch shimmer and stereo width.
//  Flanger owns short delay + high feedback + through-zero. Phaser owns cascaded allpass
//  with NO delay line. Our feedback ceiling is 0.82 (0.65 on Micro) and there is no
//  dry-path delay, so through-zero is structurally out of reach here — by design.
//
//  ── R7: THE LEGACY CHORUS IS TYPE 0 ──────────────────────────────────────────
//  Max: "That's already your vintage chorus... use the parameters that it already has and
//  place it on the other chorus." Type 0 `Vintage` reproduces Source/TerrainChorus.h's
//  voicing and its three parameter meanings (AMOUNT -> Depth + Mix, WIDTH -> Width,
//  CHARACTER -> the locked-rate / recon-LP Character rows).
//
//  ⚠️ AND ITS BUG IS NOW A CHARACTER, NOT A DEFAULT. TerrainChorus.h declares
//  RIGHT_PHASE_OFFSET = pi (:19) but ALSO skews the right LFO rate by
//  RIGHT_RATE_RATIO = 1.07 (:18, applied :56). Two accumulators at a 1.07 ratio do not
//  hold a pi offset: the relative phase rotates at 0.07*f_L, so the pair passes through
//  IN-PHASE every 1/(0.07*f_L) seconds — 4.8 s at the legacy 1.5 Hz, 17.9 s at 0.4 Hz.
//  MEASURED, not assumed: chorus_cert §B. That rotation is a real, ownable sound (the
//  width breathes in and out over ~10 s), so it is KEPT as the Vintage identity and the
//  honest fixed version ships beside it as the `Locked` Character (skew 1.000 = genuine
//  antiphase). The user can hear the bug and the fix, A/B, in one dropdown.
//  NOT copied: WET_GAIN 2.5 (+8 dB of un-budgeted make-up), the tanh/sinh "compander"
//  (measured a static x1.47 = +3.35 dB trim at the -26 dBFS bus with ZERO
//  level-dependence — a costume), the unglided setParams, the jassert-only buffer guard.
//
//  ── HOUSE LAWS HONOURED STRUCTURALLY ─────────────────────────────────────────
//  * NO CLICKS — every continuous param one-poles at 15 ms; the DELAY LENGTH ITSELF
//    glides (comb-click law) and so does everything that FEEDS it, including Phase, which
//    is why the tap table stores phase COEFFICIENTS and multiplies them by the smoothed
//    value per sample instead of rebuilding the layout on a knob move.
//    Type/Character switches fade-swap-recover (8 ms dip -> swap -> 30 ms recover).
//  * MIX 1.0 = FULLY WET, ZERO DRY — equal power; the dry gain is cos(pi/2), exactly 0.
//  * MONO-SAFE — Width (M/S) touches the WET ONLY; the wet pair always carries mid
//    energy, so Width 0 is mono, never silence. The one polarity flip in the roster
//    (Pedal / Wet Flip) is a Character, is BADGED by charIsMonoHostile(), and is measured
//    rather than hidden.
//  * NOTHING FREE-RUNS — the feedback loop input AND the BBD noise are both multiplied by
//    env/(env+0.003); silence in, silence out.
//  * ONE-CLOCK LAW — every tap that must hold a FIXED phase relationship reads ONE master
//    accumulator plus an offset. The only second accumulator is the Vintage clock skew,
//    where a ROTATING relationship is the entire character.
//  * NO OVERSAMPLING, EVER — the only nonlinearity is the gentle BBD poly; oversampling
//    buys nothing where there is no significant nonlinearity to alias.
//  * ZERO LATENCY, ZERO LOOKAHEAD — structural, and it is a RACK-WIDE constraint, not a
//    preference: the main-send exclusion sums subtract the routed dry sample-aligned, so a
//    latency-reporting device makes the host delay-compensate the plugin while the
//    subtracted dry stays un-delayed and leaks back phase-smeared. NEVER call
//    setLatencySamples from this device, and never add lookahead to it.
// ═══════════════════════════════════════════════════════════════════════════════

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace tw {

class TerrainChorusFx
{
public:
    // ── identity ─────────────────────────────────────────────────────────────
    static constexpr int kNumTypes = 8;
    static constexpr int kNumChars = 8;
    // ⚠️ fb342 birth-cardinality law: a JUCE AudioParameterChoice's option count is fixed
    // at construction and state-format-breaking to grow later. DECLARE THE APVTS CHOICE AT
    // kNumTypeSlots (8 live + 4 reserved, greyed in the UI, clamped to 0 by setParams).
    // The ENGINE only knows the live roster; the reserved slots are a param/UI concern.
    static constexpr int kNumTypeSlots = 12;

    static const char* const* typeNames() noexcept
    {
        static const char* const n[kNumTypes] =
        { "Vintage", "June", "Pedal", "Trio", "Ensemble", "Micro", "Wow", "Dark" };
        return n;
    }

    static const char* const* charNames (int type) noexcept
    {
        static const char* const n[kNumTypes][kNumChars] =
        {
            { "Classic", "Slow", "Fast", "Deep", "Wide 106", "Locked", "Thick", "Hiss" },
            { "I", "II", "I Plus II", "Manual", "Aged", "Clean", "Wide 106", "Deep" },
            { "Chorus", "Vibrato", "Grit", "Slow Amp", "Fast Amp", "Wet Flip", "Warm", "Thin" },
            { "Preset", "Manual", "Enhance", "Sides", "Centre", "Syrup", "Rack 86", "Glassy" },
            { "Solina", "RS 202", "Choir", "Random", "Dark Wine", "Brass", "Slow Tide", "Phase Wide" },
            { "Studio I", "Studio II", "Wander", "Dual Mono", "Layers", "Tape Head", "Chorale", "Subtle" },
            { "Cassette", "Vinyl 33", "Vinyl 45", "Dictaphone", "Pro Reel", "Dying Deck", "Underwater", "Breeze" },
            { "Standard", "Double", "Murk", "Pumped", "Hissy", "Cheap", "Slap Wide", "Regen Box" }
        };
        return n[type < 0 ? 0 : (type >= kNumTypes ? kNumTypes - 1 : type)];
    }

    static_assert (kNumTypes > 0 && kNumChars == 8, "roster/table must move together");

    // The ONE Character in the roster whose wet partially nulls on a mono fold-down.
    // Shipped LABELLED, never hidden ("no playing safe"); the harness measures how bad.
    static bool charIsMonoHostile (int type, int chr) noexcept
    { return type == 2 && chr == 5; }                       // Pedal / Wet Flip

    // ── tempo sync: the 20-entry list cloned WHOLE from PluginProcessor.cpp:3479,
    //    "Free" at index 0 included. Identical divisions and identical labels in all three
    //    fx3 devices (CONTRACT §4). A 19-entry list starting at "4 bar" is off by one and
    //    every saved Rate reads one division fast.
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
    //  FRONT 3 + Mix:  rate = Rate · depth = Depth · feedback = Feedback · mix = Mix
    //  BACK 8:  b1 Time · b2 Detune · b3 Width · b4 Flutter
    //           b5 Drift · b6 Colour · b7 Low Keep · b8 Phase
    struct Params
    {
        int   type = 0, character = 0;
        float rate = 0.35f, depth = 0.5f, feedback = 0.0f;
        float mix  = 0.5f;
        float b1=0.5f,b2=0.0f,b3=0.7f,b4=0.25f,b5=0.0f,b6=0.5f,b7=0.0f,b8=1.0f;
        bool  tempoSync = false; double bpm = 120.0;
    };

    struct Viz
    {
        float lfo = 0.0f;          // -1..+1 instantaneous sweep — THE needle
        float lvl = 0.0f;          // wet level 0..1
        float notch[8] {};         // first comb null of each live tap, Hz (0 = unused)
        float depthNow = 0.0f;     // effective excursion, ms
    };

    // ═════════ lifecycle ═════════════════════════════════════════════════════
    void prepare (double sampleRate, int /*maxBlock*/) noexcept
    {
        fs_ = (float) (sampleRate > 1000.0 ? sampleRate : 48000.0);

        // §3.2 — SIZE FROM THE WORST READ, NEVER FROM THE MAX BASE. Worst legal case is
        // Dark: base 40 ms x 1.6 R stagger = 64 ms, + excursion 6x1.8 + flutter 0.6 +
        // drift 2.5 + interpolator guard ~= 80 ms. 140 ms with the hard clamp below cannot
        // wrap; 64 kB per instance at 48 k is free. (The legacy engine's 4096 samples plus a
        // jassert-only guard IS this bug already shipped: a release build falls through to
        // the % wrap and reads the write head — metallic garbage, not a crash.)
        int need = (int) std::ceil (0.140f * fs_) + 8;
        int sz = 1024; while (sz < need) sz <<= 1;
        bufL_.assign ((size_t) sz, 0.0f);
        bufR_.assign ((size_t) sz, 0.0f);
        bufN_ = sz; mask_ = sz - 1;

        smK_   = 1.0f - std::exp (-1.0f / (0.015f * fs_));    // 15 ms — the house ramp
        envK_  = 1.0f - std::exp (-1.0f / (0.020f * fs_));    // 20 ms input presence
        cmpK_  = 1.0f - std::exp (-1.0f / (0.005f * fs_));    // 5 ms rectifier average
        lvlK_  = 1.0f - std::exp (-1.0f / (0.060f * fs_));
        dipDn_ = 1.0f - std::exp (-1.0f / (0.008f * fs_));    // 8 ms dip down
        dipUp_ = 1.0f - std::exp (-1.0f / (0.045f * fs_));    // 45 ms recover
        srUpK_ = 1.0f - std::exp (-1.0f / (0.040f * fs_));    // Wow glide: rise 40 ms ...
        srDnK_ = 1.0f - std::exp (-1.0f / (0.400f * fs_));    // ... fall 400 ms (a warped record)
        dcR_   = 1.0f - (6.2831853f * 12.0f / fs_);           // fs-aware 12 Hz DC blocker
        peA_   = onePole (3000.0f);
        cmpGlide_ = 1.0f - std::exp (-1.0f / (0.040f * fs_));   // 40 ms — Character comp glide                           // CE-2 pre/de-emphasis corner

        reset();
        recalc();
    }

    void reset() noexcept
    {
        std::fill (bufL_.begin(), bufL_.end(), 0.0f);
        std::fill (bufR_.begin(), bufR_.end(), 0.0f);
        wr_ = 0;
        mph_ = 0.0f; sph_ = 0.0f; fph_ = 0.0f; vph_ = 0.0f;
        wph_[0] = 0.0f; wph_[1] = 0.25f; wph_[2] = 0.5f;
        for (int i = 0; i < 8; ++i) triZ_[i] = 0.0f;
        for (int i = 0; i < kNumSR; ++i) { sr_[i].st = 0.0f; sr_[i].tg = 0.0f; sr_[i].ph = (float) i * 0.137f; }
        rng_ = 0x2545F491u;
        for (int c = 0; c < 2; ++c)
        {
            reconZ_[c] = recon2_[c] = recon3_[c] = nzZ_[c] = lkZ_[c] = lkW_[c] = deZ_[c] = peZ_[c] = 0.0f;
            cmpEnv_[c] = expEnv_[c] = fbTap_[c] = 0.0f;
            dcX_[c] = dcY_[c] = dcXo_[c] = dcYo_[c] = q_[c] = 0.0f;
            dbgDelay_[c] = 0.0f;
        }
        envIn_ = 0.0f; lvlSm_ = 0.0f; dip_ = 1.0f;
        pendType_ = -1; pendChar_ = -1;
        seeded_ = false;
    }

    void setParams (const Params& p) noexcept
    {
        const int t = clampi (p.type, 0, kNumTypes - 1);
        const int c = clampi (p.character, 0, kNumChars - 1);

        if (t != type_ || c != char_)
        {
            if (! seeded_) { type_ = t; char_ = c; }          // first call: adopt, do not dip
            else { pendType_ = t; pendChar_ = c; }            // else fade-swap-recover
        }
        p_ = p;
        recalc();
    }

    // ── the block. IN-PLACE, wet+dry per Mix. ────────────────────────────────
    void processStereo (float* L, float* R, int n) noexcept
    {
        if (L == nullptr || R == nullptr || n <= 0) return;

        if (! seeded_)
        {   // snap the smoothers so the first block is correct, not a ramp from zero
            rateSm_ = rateTg_; rkSm_ = rkTg_; baseSm_ = baseTg_; depthSm_ = depthTg_;
            widthSm_ = widthTg_; flutSm_ = flutTg_; colorSm_ = colorTg_; phaseSm_ = phaseTg_;
            detSm_ = detTg_; driftSm_ = driftTg_; fbSm_ = fbTg_; lkSm_ = lkTg_; mixSm_ = mixTg_;
            compSm_ = compK_;
            seeded_ = true;
        }

        int i = 0;
        while (i < n)
        {
            const TypeSpec& T = SPEC[type_];
            const CharSpec& C = CHAR[type_][char_];
            const int   nT   = nTap_;
            const float limHi = (float) bufN_ - 6.0f;

            for (; i < n; ++i)
            {
                // ── 0. fade-swap-recover ──────────────────────────────────────
                if (pendType_ >= 0)
                {
                    dip_ += dipDn_ * (0.02f - dip_);
                    if (dip_ < 0.05f)
                    {
                        type_ = pendType_; char_ = pendChar_;
                        pendType_ = -1; pendChar_ = -1;
                        // re-seat from CURRENT state, never from zero: a zeroed compander
                        // env after a swap pops as it re-converges (the fb345 re-seat law).
                        recalc();
                        // ...and SNAP the continuous smoothers to the new Type's targets at
                        // the dip floor. At -34 dB a delay jump is inaudible, whereas letting
                        // them glide for 15 ms WHILE the wet fades back in over 30 ms means
                        // the recovery rides a moving comb — measured +3.13 dB of overshoot
                        // on Pedal->Trio. This is DistortionEngine.h:226's "a flush is silent,
                        // not a ramp" applied to a Type swap.
                        rateSm_ = rateTg_; rkSm_ = rkTg_; baseSm_ = baseTg_; depthSm_ = depthTg_;
                        widthSm_ = widthTg_; flutSm_ = flutTg_; colorSm_ = colorTg_;
                        phaseSm_ = phaseTg_; detSm_ = detTg_; driftSm_ = driftTg_;
                        fbSm_ = fbTg_; lkSm_ = lkTg_; compSm_ = compK_;
                        break;                               // re-bind T / C, keep going
                    }
                }
                else dip_ += dipUp_ * (1.0f - dip_);

                // ── 1. per-sample glide. The delay LENGTH glides, and so does every
                //      term that feeds it — including Phase (comb-click law).
                rateSm_  += smK_ * (rateTg_  - rateSm_);
                rkSm_    += smK_ * (rkTg_    - rkSm_);
                baseSm_  += smK_ * (baseTg_  - baseSm_);
                depthSm_ += smK_ * (depthTg_ - depthSm_);
                widthSm_ += smK_ * (widthTg_ - widthSm_);
                flutSm_  += smK_ * (flutTg_  - flutSm_);
                colorSm_ += smK_ * (colorTg_ - colorSm_);
                phaseSm_ += smK_ * (phaseTg_ - phaseSm_);
                detSm_   += smK_ * (detTg_   - detSm_);
                driftSm_ += smK_ * (driftTg_ - driftSm_);
                fbSm_    += smK_ * (fbTg_    - fbSm_);
                lkSm_    += smK_ * (lkTg_    - lkSm_);
                mixSm_   += smK_ * (mixTg_   - mixSm_);

                const float inL = L[i], inR = R[i];

                // ── 2. input presence — nothing free-runs ─────────────────────
                const float rect = 0.5f * (std::fabs (inL) + std::fabs (inR));
                envIn_ += envK_ * (rect - envIn_);
                const float gate = envIn_ / (envIn_ + 0.003f);

                // ── 3. modulators ────────────────────────────────────────────
                const float rHz = rateSm_;
                mph_ += rHz / fs_;                    if (mph_ >= 1.0f) mph_ -= 1.0f;
                if (skewOn_) { sph_ += (rHz * skew_) / fs_; if (sph_ >= 1.0f) sph_ -= 1.0f; }
                else sph_ = mph_;
                fph_ += fastHz_ / fs_;                if (fph_ >= 1.0f) fph_ -= 1.0f;
                if (T.phaseMode == 6)
                {   // Wow's tape stack rides the Rate knob, so Rate is never dead there
                    const float k = rHz / 0.5f;
                    wph_[0] += (0.6f * k) / fs_;      if (wph_[0] >= 1.0f) wph_[0] -= 1.0f;
                    wph_[1] += (2.2f * k) / fs_;      if (wph_[1] >= 1.0f) wph_[1] -= 1.0f;
                    wph_[2] += (7.0f * k) / fs_;      if (wph_[2] >= 1.0f) wph_[2] -= 1.0f;
                    vph_    += C.x1 / fs_;            if (vph_ >= 1.0f) vph_ -= 1.0f;   // revolution warp
                }
                // band-limited random WALKS (hold + slew). A per-sample noise smoother is
                // not a walk (the Worn-walk law) and measures near-zero drama.
                {
                    const float srRate = 1.6f + 3.0f * driftSm_;
                    for (int s = 0; s < kNumSR; ++s) tickSR (sr_[s], srRate, T.phaseMode == 6);
                }

                // ── 4. the line input — compander COMPRESS ───────────────────
                float lineL = T.monoLine ? 0.5f * (inL + inR) : inL;
                float lineR = T.monoLine ? lineL : inR;
                if (T.preEmph) { lineL = preEmph (lineL, peZ_[0]); lineR = T.monoLine ? lineL : preEmph (lineR, peZ_[1]); }
                compSm_ += cmpGlide_ * (compK_ - compSm_);   // a Character that jumps the compander
                if (compSm_ > 0.001f)                        // exponent BANGS (+6.7 dB measured)
                {
                    cmpEnv_[0] += cmpK_ * (std::fabs (lineL) - cmpEnv_[0]);
                    lineL *= companderGain (cmpEnv_[0], -0.5f * compSm_);
                    if (T.monoLine) lineR = lineL;
                    else { cmpEnv_[1] += cmpK_ * (std::fabs (lineR) - cmpEnv_[1]);
                           lineR *= companderGain (cmpEnv_[1], -0.5f * compSm_); }
                }

                // ── 5. read the taps ─────────────────────────────────────────
                const float excSamp   = depthSm_ * excMs_   * 0.001f * fs_;
                const float fastSamp  = (T.fastIntrinsicMs + flutSm_ * T.fastKnobMs) * C.fastMul * 0.001f * fs_;
                const float driftSamp = driftSm_ * 2.5f * 0.001f * fs_;
                // fb397 — recentre rather than cross: with the opened-out excursion the window
                // can exceed the base, and a read at ~0 ms is indistinguishable from dry.
                const float baseSamp  = std::max (baseSm_ * 0.001f * fs_,
                                                  excSamp + 0.30f * 0.001f * fs_);

                float wet[2] = { 0.0f, 0.0f };

                // ═══ the micro-shift reader — Micro's whole engine, and a constant pitch
                //     split on every other Type (Detune is never a dead knob anywhere).
                //
                // 🛑 THE BIBLE'S §3.5 MATH IS WRONG AND THIS IS THE CORRECTION. It specifies
                //    "a raised-cosine crossfade of period T = 40 ms" with "excursion r*T ~=
                //    0.24 ms at 10 cents — tiny, glitch-free". Built exactly as written, that
                //    reader SHIFTS NOTHING. Proof, and it is not subtle:
                //
                //    A sawtooth-reset delay gives y = x(t)*exp(j*w*r*T*saw(t)), and a periodic
                //    factor of period T can only put energy on the LINES f +- k/T. The
                //    dominant line is k = round(f*r*T) — so the delivered shift is quantised
                //    to 1/T = 25 Hz, while the WANTED shift at 10 cents / 440 Hz is 2.5 Hz.
                //    round(0.1) = 0 ⇒ k = 0 ⇒ the output sits on the carrier, dead on pitch,
                //    with a 25 Hz tremolo. Measured standalone AND in this engine: 440.00 Hz
                //    in, 440.00 Hz out at every Detune from 6 to 50 cents. The bible's whole
                //    "keep the excursion tiny" premise is what breaks it, and its crossfade-
                //    comb table (853 Hz at 50 cents) is computed on the same broken geometry.
                //
                //    THE FIX: the free parameter is the ramp SPAN, and the crossfade period
                //    FOLLOWS from it — Tw = span / r. A 40 ms span at 10 cents is a 6.9 s
                //    ramp, i.e. 0.145 Hz line spacing against a 2.5 Hz shift: 17 lines of
                //    resolution instead of 0.1. Measured after the fix: 6 c -> +5.97,
                //    12 c -> +12.00, 25 c -> +25.00, 50 c -> +50.03 cents at a 50 ms span.
                //
                //    The real artifact is therefore NOT a comb at 1/(r*T) — it is the FLAM:
                //    at the equal-gain instant the two heads are span/2 apart. That is the
                //    honest cost of a delay-line shifter (it is why the H3000 preset is called
                //    LAYERED shift), it is bounded by the span, and four heads quarter it.
                const float cents  = detSm_;
                int   heads = 1;
                float spanSamp = 0.0f, hard = 0.0f, rc[2] = { 0.0f, 0.0f };
                int   dirc[2] = { 1, -1 };
                if (cents > 0.02f)
                {
                    // span scales in from 0 over the first 8 cents, so engaging Detune never
                    // steps the effective delay (which would be a click by another name)
                    float spanMs = (T.phaseMode == 4) ? (45.0f - 25.0f * rkSm_) : 32.0f;
                    if (T.phaseMode == 4) spanMs *= (1.0f + 0.12f * flutSm_ * triW (fph_));
                    spanMs *= std::min (1.0f, cents * 0.125f);
                    spanSamp = spanMs * 0.001f * fs_;
                    heads = (C.x1 >= 3.5f || cents > 25.0f) ? 4 : 2;
                    hard  = (T.phaseMode == 4) ? C.x2 : 0.0f;

                    // exact per-direction slopes: UP wants d' = -(2^(c/1200) - 1),
                    // DOWN wants d' = +(1 - 2^(-c/1200)). They are not the same number, and
                    // using one for both leaves the down side 1.5 cents flat at 50 cents.
                    const bool dual = (T.phaseMode == 4 && (C.flags & kDualMono)) != 0;
                    rc[0] = std::exp2 (cents / 1200.0f) - 1.0f;
                    rc[1] = dual ? rc[0] : (1.0f - std::exp2 (-cents / 1200.0f));
                    dirc[0] = 1; dirc[1] = dual ? 1 : -1;
                    for (int c = 0; c < 2; ++c)
                    { q_[c] += rc[c] / std::max (1.0f, spanSamp); if (q_[c] >= 1.0f) q_[c] -= 1.0f; }
                }

                // ⚠️ THE TAP DELAYS DO NOT DEPEND ON THE HEAD — only the head OFFSET does.
                // Computing them inside the head loop cost a sine and a wave-shaper per head
                // per tap for nothing (and was 30 % of the worst Type's budget).
                float dT[2][kMaxTaps];
                const int nGeom = sameGeom_ ? 1 : 2;
                for (int c = 0; c < nGeom; ++c)
                    for (int t = 0; t < nT; ++t)
                        dT[c][t] = tapDelay (c, t, baseSamp, excSamp, fastSamp, driftSamp, T, C, limHi);
                if (sameGeom_) for (int t = 0; t < nT; ++t) dT[1][t] = dT[0][t];
                const float dFirst[2] = { dT[0][0], dT[1][0] };
                dbgDelay_[0] = dT[0][0] * 1000.0f / fs_;
                dbgDelay_[1] = dT[1][0] * 1000.0f / fs_;

                {   int nz = 0;
                    for (int c = 0; c < 2 && nz < 8; ++c)
                        for (int t = 0; t < nT && nz < 8; ++t)
                            viz_.notch[nz++] = 0.5f * fs_ / std::max (2.0f, dT[c][t]);
                    for (int k = nz; k < 8; ++k) viz_.notch[k] = 0.0f; }

                // head window per channel — L ramps up, R ramps down, at DIFFERENT slopes,
                // so the two crossfades are genuinely independent
                float g[2][kMaxHeads] = { { 1.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f } };
                float ginv[2] = { 1.0f, 1.0f };
                if (heads > 1)
                    for (int c = 0; c < 2; ++c)
                    {
                        float gsum = 0.0f;
                        for (int h = 0; h < heads; ++h)
                        {
                            float qh = q_[c] + (float) h / (float) heads; if (qh >= 1.0f) qh -= 1.0f;
                            float w = 0.5f - 0.5f * std::cos (6.2831853f * qh);
                            if (hard > 0.001f) w = w * (1.0f - hard) + hard * (w * w * w);   // DMX de-glitch
                            g[c][h] = w; gsum += w;
                        }
                        ginv[c] = 1.0f / std::max (1.0e-6f, gsum);
                    }

                // ⚠️ read-sharing is legal ONLY when the two channels' tap GEOMETRY is
                // identical and only their gains differ (Trio: three taps off one line,
                // panned). Pedal's second read has its own offset and Ensemble's right
                // channel is phase-rotated — sharing there silently deleted their stereo
                // and pinned L/R correlation at exactly +1.000. The 96-cell liveness sweep
                // is what caught it: Pedal/Phase and Ensemble/Phase both read 0.00.
                const bool shareReads = sameGeom_ && (T.monoLine != 0) && heads == 1;

                if (shareReads)
                {
                    float v[kMaxTaps];
                    for (int t = 0; t < nT; ++t) v[t] = readH (bufL_.data(), dT[0][t]);
                    for (int c = 0; c < 2; ++c)
                    { float a = 0.0f; for (int t = 0; t < nT; ++t) a += tap_[c][t].gain * v[t]; wet[c] = a; }
                }
                else
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        const float* buf = (T.monoLine || c == 0) ? bufL_.data() : bufR_.data();
                        float acc = 0.0f;
                        if (heads == 1)
                        {
                            for (int t = 0; t < nT; ++t) acc += tap_[c][t].gain * readH (buf, dT[c][t]);
                        }
                        else
                        {
                            // dual/quad-head constant-slope pitch reader. Output frequency
                            // ratio is 1 - d'(t), so an UP-shift must CONSUME delay.
                            // ⚠️ The ramp always lives in [base, base + span] — never below
                            // base — so an up-shift cannot walk the read head into the write
                            // head no matter how short Time is. UP starts at base+span and
                            // falls; DOWN starts at base and rises. LEFT takes +cents, RIGHT
                            // -cents (Dual Mono makes both +, a thickener not a widener).
                            for (int h = 0; h < heads; ++h)
                            {
                                float qh = q_[c] + (float) h / (float) heads; if (qh >= 1.0f) qh -= 1.0f;
                                const float off = (dirc[c] > 0) ? spanSamp * (1.0f - qh) : spanSamp * qh;
                                float s = 0.0f;
                                for (int t = 0; t < nT; ++t)
                                    s += tap_[c][t].gain * readH (buf, clampf (dT[c][t] + off, 2.0f, limHi));
                                acc += g[c][h] * ginv[c] * s;
                            }
                        }
                        wet[c] = acc;
                    }
                }

                // ── 6. the colour chain, then THE FEEDBACK TAP ───────────────
                //  ⛔ the tap sits BEFORE the expander and BEFORE the de-emphasis. Both are
                //  unbounded / up to x2 and would push the loop past unity exactly when the
                //  note dies (a self-oscillating "breather"). Real BBD pedals regenerate
                //  from the BBD output too. In-loop: 1.035 (Hermite ripple) x 1.094 (worst
                //  poly slope) x 1.000 (LP) = 1.132; 0.93/1.132 = 0.82 = the knob ceiling.
                // Colour is resolved PER SAMPLE from the SMOOTHED knob, not per block from
                // the target: a per-block coefficient step on a knob ride is a zipper, and
                // Colour moves both a drive and a cutoff at once.
                const float gritK = 1.0f + (1.0f - colorSm_) * gritSpan_;
                const float gritI = 1.0f / gritK;
                const float colHz = clampf (colBase_ * std::exp2 (colorSm_ * 3.1521f), 700.0f, 18000.0f);
                const float lkA   = (lkSm_ > 22.0f) ? onePoleFast (lkSm_) : 0.0f;

                for (int c = 0; c < 2; ++c)
                {
                    float w = wet[c];
                    if (gritK > 1.02f) w = poly (w * gritK) * gritI;

                    // clock-darkening: the cutoff tracks the INSTANTANEOUS delay. trk = 1 on
                    // Dark — its brightness BREATHES at the LFO rate, and that is the one
                    // thing about Dark that no knob on June can imitate. trk = 0 on
                    // June/Vintage, whose recon cutoff is static within a block.
                    float hz = colHz;
                    if (T.trk > 0.001f)
                    {
                        const float ratio = baseSamp / std::max (2.0f, dFirst[c]);
                        hz *= (T.trk >= 0.999f) ? ratio
                                                : std::exp2 (T.trk * std::log2 (std::max (1.0e-4f, ratio)));
                        hz = clampf (hz, 700.0f, 18000.0f);
                    }
                    // ⚠️ Dark runs the REAL BBD reconstruction chain (3 poles), not one.
                    //    Measured: a ONE-pole tracking d(t) over 0.85 octave changes 8 kHz by
                    //    only ~5 dB — less than the brightness wobble every Type already gets
                    //    from the interpolator under a sweep, so Dark did NOT separate from
                    //    June (7.96 vs 8.04 dB at matched geometry). At 18 dB/oct the same
                    //    cutoff travel is ~15 dB, which is the difference between "a tint" and
                    //    "the murk breathes". The per-stage corner is scaled by 1/0.5098 so
                    //    the -3 dB point is unchanged and only the SLOPE differs.
                    const float a = onePoleFast (T.reconPoles == 3 ? hz * 1.9616f : hz);
                    reconZ_[c] = flush (reconZ_[c] + a * (w - reconZ_[c]));
                    w = reconZ_[c];
                    if (T.reconPoles == 3)
                    {
                        recon2_[c] = flush (recon2_[c] + a * (w - recon2_[c])); w = recon2_[c];
                        recon3_[c] = flush (recon3_[c] + a * (w - recon3_[c])); w = recon3_[c];
                    }

                    // DC blocker BEFORE the tap. The Raffel poly's +1/8 is a DC offset BY
                    // DESIGN; at grit 8 that is +0.0156 per pass, and a DC term inside a
                    // 0.82 loop converges to 0.087 — it would shift the poly's operating
                    // point and latch the rail. Blocking inside the loop is the fix.
                    const float y = w - dcX_[c] + dcR_ * dcY_[c];
                    dcX_[c] = w; dcY_[c] = flush (y);
                    w = dcY_[c];

                    fbTap_[c] = w;                              // ← THE TAP POINT

                    if (T.preEmph) w = deEmph (w, deZ_[c]);
                    if (compSm_ > 0.001f)
                    {
                        expEnv_[c] += cmpK_ * (std::fabs (w) - expEnv_[c]);
                        w *= companderGain (expEnv_[c], +0.5f * compSm_);
                    }
                    if (noiseAmp_ > 0.0f)
                    {   // BBD hiss rides the input envelope: a powered, routed, SILENT
                        // chorus is silent. Both directions of that law have shipped as bugs.
                        nzZ_[c] = flush (nzZ_[c] + a * (rand11() - nzZ_[c]));
                        w += nzZ_[c] * noiseAmp_ * gate * 2.0f;
                    }
                    // a SECOND blocker at the output: the in-loop one sits before the tap
                    // and so cannot catch what the expander adds after it. A time-varying
                    // gain riding an asymmetric waveform rectifies, and Dark measured 1.8 %
                    // of program as DC at grit 100 % + Feedback 90 % with only one blocker.
                    const float y2 = w - dcXo_[c] + dcR_ * dcYo_[c];
                    dcXo_[c] = w; dcYo_[c] = flush (y2);
                    wet[c] = dcYo_[c];
                }

                // ── 7. write (feedback joins here, env-gated) ────────────────
                {
                    const float f = fbSm_ * gate;
                    float nwL, nwR;
                    if (T.monoLine)
                    {
                        const float m = 0.5f * (fbTap_[0] + fbTap_[1]);
                        nwL = nwR = softClip (lineL + f * m);
                    }
                    else
                    {
                        nwL = softClip (lineL + f * fbTap_[0]);
                        nwR = softClip (lineR + f * fbTap_[1]);
                    }
                    bufL_[(size_t) wr_] = flush (nwL);
                    bufR_[(size_t) wr_] = flush (nwR);
                    wr_ = (wr_ + 1) & mask_;
                }

                // ── 8. Low Keep — a REAL 2-band split, not a wet high-pass.
                //      ⚠️ The first build just high-passed the wet, which measured
                //      correctly-monotonic and was still WRONG: at Mix 100 % that deleted the
                //      low band from the output entirely (-21 dB at 90 Hz) because there is
                //      no dry left to carry it. MicroShift's Focus applies the AFFECTED
                //      signal to the high band only — the low band stays DRY. So the low band
                //      is split off the INPUT, bypasses the effect, and only its WIDTH follows
                //      Mix (stereo dry -> centred dry). Level is constant across the whole Mix
                //      travel, and at Mix 0 the algebra below is bit-exact.
                float loL = 0.0f, loR = 0.0f;
                if (lkA > 0.0f)
                {
                    lkZ_[0] = flush (lkZ_[0] + lkA * (inL - lkZ_[0])); loL = lkZ_[0];
                    lkZ_[1] = flush (lkZ_[1] + lkA * (inR - lkZ_[1])); loR = lkZ_[1];
                    lkW_[0] = flush (lkW_[0] + lkA * (wet[0] - lkW_[0])); wet[0] -= lkW_[0];
                    lkW_[1] = flush (lkW_[1] + lkA * (wet[1] - lkW_[1])); wet[1] -= lkW_[1];
                }

                // ── 9. Width — M/S on the WET ONLY. Widening the DRY is the classic
                //      mono-collapse bug; and a wet pair that is pure SIDE outputs silence
                //      at Width 0 and nulls in mono, which is how the pre-audit Pedal recipe
                //      deleted itself. No Type here produces a pure-side wet.
                if (C.flags & kWetFlipR) wet[1] = -wet[1];      // Pedal / Wet Flip ⚠ mono-hostile
                {
                    const float M = 0.5f * (wet[0] + wet[1]);
                    const float S = 0.5f * (wet[0] - wet[1]) * (widthSm_ * 1.6f);
                    wet[0] = M + S; wet[1] = M - S;
                }

                // fb397 — feedback near 1.0 adds up to +30 dB of comb resonance; without this
                // "wild" would just read as "louder", which is the loudness trap, not drama.
                const float trim = T.wetTrim * C.lvl * dip_ * (1.0f - 0.35f * fbSm_);
                const float wL = wet[0] * trim, wR = wet[1] * trim;

                // ── 10. Mix — equal power. At mix 1.0 the dry gain is cos(pi/2) = 0 EXACTLY.
                //       The protected low band rides ON TOP at unity: `loL*(1-dg)` restores
                //       whatever the equal-power law took off it, and `(loM-loL)*m` centres
                //       it as the effect comes up. With Low Keep off, loL = loR = 0 and this
                //       reduces to the plain crossfade, bit-exactly.
                const float m  = (C.flags & kWetOnly) ? 1.0f : mixSm_;
                const float dg = std::cos (m * 1.5707963f);
                const float wg = std::sin (m * 1.5707963f);
                const float loM = 0.5f * (loL + loR);
                L[i] = inL * dg + wL * wg + loL * (1.0f - dg) + (loM - loL) * m;
                R[i] = inR * dg + wR * wg + loR * (1.0f - dg) + (loM - loR) * m;

                // ── 11. telemetry for the card ───────────────────────────────
                lvlSm_ += lvlK_ * (0.5f * (std::fabs (wL) + std::fabs (wR)) - lvlSm_);
                viz_.lvl = std::min (1.0f, lvlSm_ * 14.0f);
                viz_.lfo = (T.phaseMode == 4) ? (2.0f * q_[0] - 1.0f)
                                              : (T.wave == 1 ? sinW (mph_) : triW (mph_));
                viz_.depthNow = depthSm_ * excMs_;
            }
        }
    }

    const Viz& viz() const noexcept { return viz_; }

    // ── read-outs for the card and the harness ───────────────────────────────
    float liveRateHz() const noexcept { return rateSm_; }
    float liveBaseMs() const noexcept { return baseSm_; }
    float liveCents()  const noexcept { return detSm_; }
    float liveDelayMs (int ch) const noexcept { return dbgDelay_[ch & 1]; }
    int   liveTaps()   const noexcept { return nTap_; }

private:
    // ═════════ tables ════════════════════════════════════════════════════════
    //  phaseMode — the Type's modulator family AND what the Phase knob re-homes to:
    //    0 Vintage  R modulator offset + a SKEWED second clock (the legacy 1.07)
    //    1 June     R modulator offset, ONE clock
    //    2 Pedal    Phase = the second READ OFFSET on one line (0 -> 1.2 ms)
    //    3 Trio     Phase = the tap phase SPREAD (unison -> 120 deg)
    //    4 Micro    Phase = the L/R stagger ratio (1.0 -> 1.5x); NO LFO at all
    //    5 Ensemble Phase = the R-channel rotation (0 -> 180 deg)
    //    6 Wow      R modulator offset; modulator = tape stack + asymmetric random walk
    //    7 Dark     R modulator offset; recon LP tracks d(t) per sample
    struct TypeSpec
    {
        int   taps, monoLine;
        float baseLoMs, baseHiMs;
        int   wave;                    // 0 tri, 1 sin, 2 lpTri
        float depthMs, stagger;
        float fastHz, fastIntrinsicMs, fastKnobMs;
        float trk;
        int   reconPoles;        // 1 = the short-line one-pole; 3 = the full BBD reconstruction chain
        float detuneFloorC, driftFloor;
        float fbMax, wetTrim;
        int   phaseMode, preEmph;
        float gritBase, compBase;
    };

    // rateHz > 0 LOCKS the base rate; the Rate knob then scales it +/-1 octave, so a
    // locked-rate Character never leaves a dead knob behind (law 1).
    struct CharSpec
    {
        float rateHz, depthMul, baseMul, hf, comp, noiseDb, gritMul, fastMul, driftMul;
        float x1, x2, x3;
        // ⚠️ MEASURED level makeup. A Character must change the TONE, never the LEVEL — the
        // fb343 preset-spread law. Before this field the roster measured a 7.5 dB spread on
        // Dark alone (Pumped +7.07 dB over Cheap), which is not a character, it is a volume
        // knob wearing one, and it is exactly what made Murk->Pumped BANG +5.9 dB mid-note.
        // Every value below is 10^(-measured/20) from a chord at the bus level, not a guess.
        float lvl;
        int   flags;
    };

    enum : int { kWetOnly = 1, kWetFlipR = 2, kExtraTap = 4, kStackOff = 8,
                 kForceLpTri = 16, kRandomPhase = 32, kDualMono = 64, kExtraLayer = 128 };

    static constexpr int kMaxTaps = 4, kMaxHeads = 4, kNumSR = 8;

    static constexpr TypeSpec SPEC[kNumTypes] =
    {
        // taps mono  baseLo baseHi  wave depth  stag   fastHz fInt   fKnob  trk   pol detC  drift  fbMax  trim  pm pe grit comp
        {  1,   0,     3.0f,  24.0f,  1,  5.10f, 0.05f,  5.5f, 0.00f, 0.50f, 0.00f, 1, 0.0f, 0.00f, 0.97f, 0.90f, 0, 0, 1.0f, 1.0f },  // Vintage
        {  1,   0,     1.0f,  12.0f,  0,  2.95f, 0.06f,  6.0f, 0.00f, 0.50f, 0.00f, 1, 0.0f, 0.00f, 0.97f, 0.91f, 1, 0, 1.0f, 1.0f },  // June
        {  1,   1,     1.7f,  14.7f,  2,  3.50f, 0.00f,  6.5f, 0.00f, 0.40f, 0.15f, 1, 0.0f, 0.00f, 0.97f, 0.97f, 2, 1, 1.0f, 1.0f },  // Pedal
        {  3,   1,     3.0f,  21.3f,  1,  4.00f, 0.08f,  4.5f, 0.10f, 0.60f, 0.00f, 1, 0.0f, 0.00f, 0.97f, 1.45f, 3, 0, 1.0f, 1.0f },  // Trio
        {  3,   1,     2.5f,  14.4f,  0,  1.80f, 0.07f,  6.25f,0.25f, 0.60f, 0.00f, 1, 0.0f, 0.00f, 0.97f, 1.46f, 5, 0, 1.0f, 1.0f },  // Ensemble
        {  1,   0,     4.0f,  20.0f,  1,  1.50f, 0.00f,  6.0f, 0.00f, 0.30f, 0.00f, 1, 6.0f, 0.00f, 0.90f, 1.09f, 4, 0, 1.0f, 1.0f },  // Micro
        {  1,   0,     2.5f,  19.6f,  0,  3.20f, 0.06f,  7.0f, 0.00f, 0.50f, 0.20f, 1, 0.0f, 0.35f, 0.97f, 0.91f, 6, 0, 1.0f, 1.0f },  // Wow
        {  1,   0,     6.0f,  40.0f,  0,  6.00f, 0.31f,  5.0f, 0.00f, 0.50f, 2.20f, 3, 0.0f, 0.00f, 0.97f, 0.84f, 7, 0, 1.0f, 1.5f }   // Dark
    };

    // Coefficients only — no code branches, no parameter writes (the VintageReverb.h
    // CharBias pattern). A Character re-wires PHYSICS: clock topology, tap count, LFO
    // shape, loop wiring. Never "a tone control".
    static constexpr CharSpec CHAR[kNumTypes][kNumChars] =
    {
        // ── Vintage.  x1 = R clock ratio (the legacy 1.07 that defeats the pi offset)
        { //  rate    dep    base   hf     comp   noise   grit   fast   drift  x1      x2     x3    flags
            { 1.13f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.070f, 0.0f, 1.00f,  0.996f, 0 },            // Classic
            { 0.40f, 1.15f, 1.00f, 0.42f, 1.20f, -58.f, 1.20f, 0.80f, 1.00f, 1.070f, 0.0f, 1.00f,  0.998f, 0 },            // Slow
            { 1.50f, 0.85f, 1.00f, 1.90f, 0.80f, -62.f, 0.85f, 1.20f, 1.00f, 1.070f, 0.0f, 1.00f,  1.012f, 0 },            // Fast
            { 0.75f, 1.80f, 1.25f, 0.90f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.070f, 0.0f, 1.00f,  1.003f, 0 },            // Deep
            { 1.13f, 1.00f, 1.00f, 1.10f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.140f, 0.0f, 1.00f,  1.006f, 0 },            // Wide 106
            { 1.13f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.000f, 0.0f, 1.00f,  0.999f, 0 },            // Locked  <- the fix, A/B-able
            { 0.90f, 1.30f, 1.15f, 0.75f, 1.30f, -58.f, 1.15f, 1.00f, 1.00f, 1.020f, 0.0f, 1.00f,  0.965f, 0 },            // Thick
            { 1.13f, 1.00f, 1.00f, 0.80f, 1.60f, -44.f, 1.10f, 1.00f, 1.00f, 1.070f, 0.0f, 1.00f,  0.912f, 0 }             // Hiss
        },
        // ── June.  x1 = R clock ratio (1.000 = the true one-clock antiphase)
        {
            { 0.513f,1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.000f, 0.0f, 1.00f,  0.997f, 0 },            // I
            { 0.863f,1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.000f, 0.0f, 1.00f,  0.996f, 0 },            // II
            { 9.750f,0.11f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.000f, 0.0f, 1.00f,  1.039f, kForceLpTri },  // I+II
            { 0.000f,1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.000f, 0.0f, 1.00f,  1.002f, 0 },            // Manual
            { 0.600f,1.00f, 1.00f, 0.45f, 2.00f, -54.f, 1.30f, 1.00f, 1.00f, 1.000f, 0.0f, 1.00f,  0.854f, 0 },            // Aged
            { 0.600f,1.00f, 1.00f, 2.60f, 0.00f,-200.f, 0.00f, 1.00f, 1.00f, 1.000f, 0.0f, 1.00f,  1.045f, 0 },            // Clean
            { 0.600f,1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.020f, 0.0f, 1.00f,  0.998f, 0 },            // Wide 106
            { 0.450f,1.80f, 2.00f, 0.90f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.000f, 0.0f, 1.00f,  0.991f, 0 }             // Deep
        },
        // ── Pedal.  x1 = wet HP floor Hz (Thin)
        {
            { 0.40f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f,   0.0f, 0.0f, 1.00f,  0.947f, 0 },            // Chorus
            { 0.90f, 1.30f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f,   0.0f, 0.0f, 1.00f,  0.947f, kWetOnly },     // Vibrato
            { 0.40f, 1.00f, 1.00f, 0.90f, 1.20f, -58.f, 4.00f, 1.00f, 1.00f,   0.0f, 0.0f, 1.00f,  1.064f, 0 },            // Grit
            { 0.80f, 1.10f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f,   0.0f, 0.0f, 1.00f,  0.947f, 0 },            // Slow Amp
            { 6.50f, 0.22f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f,   0.0f, 0.0f, 1.00f,  0.947f, 0 },            // Fast Amp
            { 0.40f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f,   0.0f, 0.0f, 1.00f,  0.942f, kWetFlipR },    // Wet Flip ⚠
            { 0.40f, 1.00f, 1.00f, 0.35f, 1.30f, -58.f, 1.10f, 1.00f, 1.00f,   0.0f, 0.0f, 1.00f,  0.945f, 0 },            // Warm
            { 0.40f, 1.00f, 1.00f, 1.60f, 0.80f, -62.f, 0.90f, 1.00f, 1.00f, 300.0f, 0.0f, 1.00f,  1.014f, 0 }             // Thin
        },
        // ── Trio.  x1 = centre-tap gain, x2 = side-tap gain
        {
            { 0.35f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 0.707f, 1.00f, 1.0f,  0.998f, 0 },            // Preset
            { 0.00f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 0.50f, 1.00f, 0.707f, 1.00f, 1.0f,  0.980f, 0 },            // Manual
            { 0.35f, 1.10f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 0.707f, 1.00f, 1.0f,  1.316f, kExtraTap },    // Enhance (4 taps)
            { 0.35f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 0.250f, 1.00f, 1.0f,  0.787f, 0 },            // Sides
            { 0.35f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.000f, 0.50f, 1.0f,  1.088f, 0 },            // Centre
            { 0.28f, 1.50f, 1.00f, 0.42f, 1.40f, -58.f, 1.20f, 1.00f, 1.00f, 0.707f, 1.00f, 1.0f,  0.996f, 0 },            // Syrup
            { 0.35f, 1.00f, 1.00f, 0.85f, 1.50f, -48.f, 1.10f, 1.00f, 1.00f, 0.707f, 1.00f, 1.0f,  0.990f, 0 },            // Rack 86
            { 0.50f, 0.70f, 1.00f, 2.60f, 0.00f,-200.f, 0.00f, 1.00f, 1.00f, 0.707f, 1.00f, 1.0f,  0.875f, 0 }             // Glassy
        },
        // ── Ensemble.  x1 = fast-bank rate multiplier, x2 = tap spread (1 = 120 deg)
        {
            { 0.66f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.0f,  0.983f, 0 },             // Solina
            { 0.66f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.60f, 1.00f, 1.00f, 1.00f, 1.0f,  1.044f, 0 },             // RS 202
            { 0.60f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.0f,  1.066f, kExtraTap },     // Choir (4 taps)
            { 0.66f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.0f,  0.925f, kRandomPhase },  // Random
            { 0.66f, 1.00f, 1.00f, 0.30f, 1.30f, -54.f, 1.20f, 1.00f, 1.00f, 1.00f, 1.00f, 1.0f,  1.005f, 0 },             // Dark Wine
            { 0.66f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 2.00f, 1.00f, 1.0f,  0.984f, 0 },             // Brass
            { 0.33f, 1.30f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.0f,  1.069f, 0 },             // Slow Tide
            { 0.66f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.00f, 0.75f, 1.0f,  0.942f, 0 }              // Phase Wide (90 deg)
        },
        // ── Micro.  x1 = forced head count (>=3.5 -> 4), x2 = crossfade hardness,
        //           x3 = detune multiplier
        {
            { 0.0f, 1.00f, 1.00f, 1.60f, 0.60f, -60.f, 0.30f, 1.00f, 1.00f, 0.0f, 0.00f, 1.00f,  0.999f, 0 },              // Studio I  (#231)
            { 0.0f, 1.40f, 1.35f, 0.70f, 0.80f, -58.f, 0.60f, 1.00f, 1.00f, 0.0f, 0.00f, 1.00f,  1.001f, 0 },              // Studio II (#519)
            { 0.0f, 3.00f, 1.00f, 1.00f, 0.60f, -58.f, 0.40f, 1.00f, 1.50f, 0.0f, 1.00f, 1.00f,  0.903f, 0 },              // Wander (DMX)
            { 0.0f, 1.00f, 1.00f, 1.60f, 0.60f, -60.f, 0.30f, 1.00f, 1.00f, 0.0f, 0.00f, 1.00f,  1.009f, kDualMono },      // Dual Mono
            { 0.0f, 1.00f, 1.00f, 1.60f, 0.60f, -60.f, 0.30f, 1.00f, 1.00f, 4.0f, 0.00f, 1.30f,  1.504f, kExtraLayer },    // Layers
            { 0.0f, 1.20f, 1.00f, 0.45f, 1.40f, -56.f, 1.20f, 1.00f, 1.50f, 0.0f, 0.30f, 1.00f,  0.959f, 0 },              // Tape Head
            { 0.0f, 1.60f, 1.20f, 1.20f, 0.60f, -60.f, 0.30f, 1.00f, 1.00f, 4.0f, 0.00f, 1.00f,  1.645f, 0 },              // Chorale
            { 0.0f, 0.60f, 0.60f, 1.60f, 0.40f, -62.f, 0.20f, 1.00f, 1.00f, 0.0f, 0.00f, 0.40f,  1.043f, 0 }               // Subtle
        },
        // ── Wow.  x1 = locked revolution warp Hz (0 = none), x2 = its depth (ms)
        {
            { 0.0f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 0.00f, 0.0f, 1.0f,  1.004f, 0 },               // Cassette
            { 0.0f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 0.55f, 1.4f, 1.0f,  1.002f, 0 },               // Vinyl 33
            { 0.0f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 0.75f, 1.4f, 1.0f,  0.997f, 0 },               // Vinyl 45
            { 0.0f, 1.00f, 1.00f, 0.30f, 1.20f, -50.f, 1.30f, 1.00f, 3.00f, 0.00f, 0.0f, 1.0f,  1.018f, 0 },               // Dictaphone
            { 0.0f, 0.70f, 1.00f, 1.30f, 0.90f, -62.f, 0.90f, 2.00f, 0.30f, 0.00f, 0.0f, 1.0f,  1.016f, 0 },               // Pro Reel
            { 0.0f, 1.00f, 1.00f, 0.80f, 2.00f, -52.f, 1.20f, 1.00f, 4.00f, 0.00f, 0.0f, 1.0f,  0.842f, 0 },               // Dying Deck
            { 0.0f, 1.50f, 1.00f, 0.18f, 1.20f, -58.f, 1.20f, 1.00f, 1.00f, 0.00f, 0.0f, 1.0f,  1.067f, 0 },               // Underwater
            { 0.0f, 1.00f, 1.00f, 1.10f, 1.00f, -60.f, 1.00f, 1.00f, 1.20f, 0.00f, 0.0f, 1.0f,  1.022f, kStackOff }        // Breeze
        },
        // ── Dark.  x1 = R stagger, x2 = clock jitter (ms), x3 = feedback floor
        {
            { 0.0f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.31f, 0.0f, 0.00f,  0.999f, 0 },              // Standard
            { 0.0f, 0.40f, 1.80f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.31f, 0.0f, 0.00f,  1.004f, 0 },              // Double
            { 0.0f, 1.00f, 1.00f, 0.35f, 1.00f, -60.f, 1.20f, 1.00f, 1.00f, 1.31f, 0.0f, 0.00f,  1.037f, 0 },              // Murk
            { 0.0f, 1.00f, 1.00f, 1.00f, 2.50f, -58.f, 1.00f, 1.00f, 1.00f, 1.31f, 0.0f, 0.00f,  0.443f, 0 },              // Pumped
            { 0.0f, 1.00f, 1.00f, 1.00f, 1.00f, -44.f, 1.00f, 1.00f, 1.00f, 1.31f, 0.0f, 0.00f,  0.999f, 0 },              // Hissy
            { 0.0f, 1.00f, 1.00f, 0.90f, 1.00f, -58.f, 1.10f, 1.00f, 1.00f, 1.31f, 0.4f, 0.00f,  1.053f, 0 },              // Cheap
            { 0.0f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.60f, 0.0f, 0.00f,  0.999f, 0 },              // Slap Wide
            { 0.0f, 1.00f, 1.00f, 1.00f, 1.00f, -60.f, 1.00f, 1.00f, 1.00f, 1.31f, 0.0f, 0.45f,  0.819f, 0 }               // Regen Box
        }
    };

    // ═════════ per-block resolve ═════════════════════════════════════════════
    //  Phase-dependent quantities are stored as COEFFICIENTS and multiplied by the
    //  SMOOTHED phase inside tapDelay(). Baking phaseTg_ into the layout here would jump
    //  a delay length on a knob move — a click, and the exact thing the comb-click law
    //  forbids.
    struct TapCfg { float phOff, phCoef, gain, baseMul, baseCoef, addCoefMs; int skewClock; };

    void recalc() noexcept
    {
        const TypeSpec& T = SPEC[type_];
        const CharSpec& C = CHAR[type_][char_];
        const Params&   p = p_;

        // ── Rate: free 0.02-20 Hz exponential (mid-knob 0.63 Hz), or the synced list.
        const float r01 = clamp01 (p.rate);
        rkTg_ = r01;
        float hz;
        if (p.tempoSync)
        {
            const int   idx   = clampi ((int) std::lround (r01 * (kNumDivs - 1)), 0, kNumDivs - 1);
            const float beats = divBeats (idx);
            const float bpm   = (float) (p.bpm > 1.0 ? p.bpm : 120.0);
            hz = (beats > 0.0f) ? (bpm / (60.0f * beats)) : (0.02f * std::pow (2000.0f, r01));
        }
        else hz = 0.02f * std::pow (2000.0f, r01);
        rateTg_ = clampf ((C.rateHz > 0.0f) ? (C.rateHz * std::exp2 ((r01 - 0.5f) * 2.0f)) : hz,
                          0.01f, 140.0f);

        // ── Time -> the per-TYPE window. §4.1: the APVTS default is ONE number; a Type
        //    reshapes the knob's MAPPING and NEVER writes a parameter (that would destroy
        //    the user's edits, break undo and fight automation).
        baseTg_ = T.baseLoMs * std::pow (T.baseHiMs / T.baseLoMs, clamp01 (p.b1)) * C.baseMul;

        // fb397 — NO PLAYING SAFE, but shaped. A flat 5x ceiling made the DEFAULT sound 5x deeper
        //   too, which swamped exactly the geometry that tells the Types apart (Vintage's stereo
        //   rotation 0.221 -> 0.107, Ensemble's second rate 0.152 -> 0.046). So the classic range
        //   is preserved to 60%% of the travel and the monstrous part lives above it: 0..0.6 is
        //   exactly what it always was, 0.6..1.0 opens out to 5x = ~30 ms of excursion.
        { const float x = clamp01 (p.depth);
          depthTg_ = (x <= 0.60f) ? x : (0.60f + (x - 0.60f) / 0.40f * (5.0f - 0.60f)); }
        widthTg_ = clamp01 (p.b3);
        flutTg_  = clamp01 (p.b4);
        colorTg_ = clamp01 (p.b6);
        phaseTg_ = clamp01 (p.b8);
        mixTg_   = clamp01 (p.mix);
        excMs_   = T.depthMs * C.depthMul;

        // Detune / Drift / Feedback FLOOR-reshape per Type, so a Type whose engine IS one
        // of those knobs (Micro=Detune, Wow=Drift) is alive at the stored default without
        // the Type ever writing a param.
        detTg_   = (T.detuneFloorC + clamp01 (p.b2) * (50.0f - T.detuneFloorC))
                 * (T.phaseMode == 4 ? C.x3 : 1.0f);
        driftTg_ = (T.driftFloor + clamp01 (p.b5) * (1.0f - T.driftFloor)) * C.driftMul;
        const float fbFloor = (T.phaseMode == 7) ? C.x3 : 0.0f;      // Dark / Regen Box
        fbTg_    = clampf (fbFloor + clamp01 (p.feedback) * (T.fbMax - fbFloor), 0.0f, T.fbMax);

        // Low Keep 20 Hz - 1 kHz log; Pedal/Thin raises the floor (the HP is recycled).
        lkTg_ = std::max (20.0f * std::pow (50.0f, clamp01 (p.b7)),
                          (T.phaseMode == 2 ? C.x1 : 0.0f));

        // ── the colour chain. Only the Character-scaled SPANS resolve here; the knob
        //    itself is applied per sample from colorSm_ so a Colour ride cannot zipper.
        gritSpan_ = 7.0f * T.gritBase * C.gritMul;
        colBase_  = 1800.0f * C.hf;
        compK_    = T.compBase * C.comp;
        noiseAmp_ = (C.noiseDb > -150.0f) ? (0.05f * std::pow (10.0f, C.noiseDb / 20.0f)) : 0.0f;
        fastHz_  = T.fastHz * ((T.phaseMode == 5) ? C.x1 : 1.0f);
        skew_    = ((T.phaseMode == 0 || T.phaseMode == 1) ? C.x1 : 1.0f);
        skewOn_  = (skew_ > 1.000001f || skew_ < 0.999999f);

        // ── the tap layout ───────────────────────────────────────────────────
        int nT = clampi (T.taps + ((C.flags & kExtraTap) ? 1 : 0), 1, kMaxTaps);
        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < kMaxTaps; ++t) tap_[c][t] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0 };

        switch (T.phaseMode)
        {
            case 0:   // Vintage — 1 tap/side, R rides the SKEWED clock
                nT = 1;
                tap_[0][0] = { 0.0f, 0.0f,  1.0f, 1.0f,             0.0f, 0.0f, 0 };
                tap_[1][0] = { 0.0f, 0.5f,  1.0f, 1.0f + T.stagger, 0.0f, 0.0f, 1 };
                break;
            case 1:   // June
            case 6:   // Wow  (same geometry, different modulator)
            case 7:   // Dark (same geometry, R stagger from x1, tracked recon LP)
                nT = 1;
                tap_[0][0] = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0 };
                tap_[1][0] = { 0.0f, 0.5f, 1.0f,
                               (T.phaseMode == 7 ? C.x1 : 1.0f + T.stagger), 0.0f, 0.0f, 0 };
                break;
            case 2:   // Pedal — ONE line read TWICE, CO-PHASE. Phase = the read offset.
                nT = 1;
                tap_[0][0] = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0 };
                tap_[1][0] = { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.2f, 0 };
                break;
            case 3:   // Trio — 3 (or 4) taps PANNED L/C/R; Phase = the tap spread
            {
                static const float pan[4][2] = { {1.0f,0.0f}, {0.7071f,0.7071f}, {0.0f,1.0f}, {0.7071f,0.7071f} };
                for (int t = 0; t < nT; ++t)
                {
                    const bool  mid = (t == 1 || t == 3);
                    const float g   = (mid ? C.x1 : C.x2) / (float) nT;
                    const float pc  = (float) t / (float) nT;
                    tap_[0][t] = { 0.0f, pc, g * pan[t][0], 1.0f + T.stagger * (float) t, 0.0f, 0.0f, 0 };
                    tap_[1][t] = { 0.0f, pc, g * pan[t][1], 1.0f + T.stagger * (float) t, 0.0f, 0.0f, 0 };
                }
                break;
            }
            case 5:   // Ensemble — the SAME taps SUMMED to both, R rotated by Phase
                for (int t = 0; t < nT; ++t)
                {
                    const float o = C.x2 * (float) t / (float) nT;
                    const float g = 1.0f / (float) nT;
                    tap_[0][t] = { o, 0.0f, g, 1.0f + T.stagger * (float) t, 0.0f, 0.0f, 0 };
                    tap_[1][t] = { o, 0.5f, g, 1.0f + T.stagger * (float) t, 0.0f, 0.0f, 0 };
                }
                break;
            case 4:   // Micro — no LFO; Phase = the L/R stagger ratio
                nT = (C.flags & kExtraLayer) ? 2 : 1;
                tap_[0][0] = { 0.0f, 0.0f, 1.00f, 1.00f, 0.0f,  0.0f, 0 };
                tap_[1][0] = { 0.0f, 0.0f, 1.00f, 1.00f, 0.5f,  0.0f, 0 };
                if (nT == 2)
                {   // Layers — a -12 dB pair a touch further out (4 reads/head)
                    tap_[0][1] = { 0.0f, 0.0f, 0.25f, 1.18f, 0.0f, 0.0f, 0 };
                    tap_[1][1] = { 0.0f, 0.0f, 0.25f, 1.18f, 0.5f, 0.0f, 0 };
                }
                break;
            default: break;
        }
        nTap_ = nT;

        // ⚠️ NORMALISE each channel's tap gains to sum 1. A Character that re-balances the
        // taps (Trio `Sides` drops the centre 12 dB, `Centre` drops the sides 6 dB) must
        // change the BALANCE, never the LEVEL — otherwise the Character dropdown is a
        // secret volume knob and the fb343 preset-spread lesson repeats. Type-level level
        // differences stay where they belong, in wetTrim.
        for (int c = 0; c < 2; ++c)
        {
            float s = 0.0f;
            for (int t = 0; t < nT; ++t) s += std::fabs (tap_[c][t].gain);
            if (s > 1.0e-6f) for (int t = 0; t < nT; ++t) tap_[c][t].gain /= s;
        }

        // Do the two channels share tap GEOMETRY (everything but gain)? Only then may the
        // reads be computed once. Trio does (three taps off one line, panned); Pedal
        // (second read offset), Ensemble (rotated phase) and Micro (stagger) do not, and
        // assuming they did deleted their stereo entirely.
        sameGeom_ = true;
        for (int t = 0; t < nT && sameGeom_; ++t)
        {
            const TapCfg& a = tap_[0][t]; const TapCfg& b = tap_[1][t];
            if (a.phOff != b.phOff || a.phCoef != b.phCoef || a.baseMul != b.baseMul
                || a.baseCoef != b.baseCoef || a.addCoefMs != b.addCoefMs || a.skewClock != b.skewClock)
                sameGeom_ = false;
        }
    }

    // ── the per-tap delay in samples, fully clamped (never a jassert-only guard) ──
    inline float tapDelay (int c, int t, float baseSamp, float excSamp, float fastSamp,
                           float driftSamp, const TypeSpec& T, const CharSpec& C, float limHi) noexcept
    {
        const TapCfg& tc = tap_[c][t];
        float d = baseSamp * (tc.baseMul + tc.baseCoef * phaseSm_)
                + tc.addCoefMs * phaseSm_ * 0.001f * fs_;
        const float o = tc.phOff + tc.phCoef * phaseSm_;
        const int   idx = (c * kMaxTaps + t) & 7;

        if (T.phaseMode == 4)
        {   // Micro: Depth is re-homed to a slow delay WANDER (the AMS/DMX axis) — no LFO.
            d += excSamp * sr_[idx].st;
        }
        else if (T.phaseMode == 6)
        {   // Wow: the TapeMachines cassette stack (Depth) + an ASYMMETRIC random walk
            //      (Drift: pitch sags fast, recovers slow, like a warped record) + an
            //      optional locked revolution warp (Vinyl 33/45).
            float stack = 0.0f;
            if (! (C.flags & kStackOff))
                stack = (2.0f * triW (wph_[0] + o) + 0.8f * sinW (wph_[1] + o) + 0.4f * sinW (wph_[2] + o)) * (1.0f / 3.2f);
            if (C.x1 > 0.01f) stack += (C.x2 / 3.2f) * sinW (vph_ + o);
            d += excSamp * stack;
            // the Flutter knob is live HERE TOO. The tape stack already owns a 7 Hz term,
            // but a knob that does nothing on one Type is a dead knob on that Type — the
            // 96-cell sweep measured Wow/Flutter at exactly 0.00 before this line existed.
            if (fastSamp > 1.0e-4f) d += fastSamp * sinW (fph_ + o);
            d += driftSamp * sr_[idx].st;
        }
        else
        {
            float w;
            if (C.flags & kRandomPhase) w = sr_[idx].st;                    // Ensemble / Random
            else                        w = shape (T.wave, (tc.skewClock ? sph_ : mph_) + o,
                                                   idx, (C.flags & kForceLpTri) != 0);
            d += excSamp * w;
            if (fastSamp > 1.0e-4f) d += fastSamp * sinW (fph_ + o);
            d += driftSamp * sr_[idx].st;
            if (T.phaseMode == 7 && C.x2 > 0.01f)                            // Dark / Cheap jitter
                d += C.x2 * 0.001f * fs_ * (0.5f * rand11());
        }
        d = clampf (d, 2.0f, limHi);
        if (t == 0) dbgDelay_[c & 1] = d * 1000.0f / fs_;
        return d;
    }

    // ═════════ primitives ════════════════════════════════════════════════════
    static inline int   clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static inline float clamp01 (float v) noexcept { return clampf (v, 0.0f, 1.0f); }
    static inline float flush (float x) noexcept { return (std::fabs (x) < 1.0e-20f) ? 0.0f : x; }

    // DelayEngine.h:315 verbatim — linear in the normal region, tanh past +/-1.4 (BIBO).
    static inline float softClip (float x) noexcept
    { return (x > 1.4f || x < -1.4f) ? std::tanh (x) : x; }

    inline float onePole (float hz) const noexcept
    {
        if (hz <= 0.0f) return 0.0f;
        if (hz >= fs_ * 0.49f) return 1.0f;
        return 1.0f - std::exp (-6.2831853f * hz / fs_);
    }
    // per-sample variant — a 3-term Pade of 1-exp(-w). Exact to 0.01 % below w = 0.5 and
    // within 2.6 % at the very top of the legal range, about 6x cheaper than std::exp, and
    // it is a VOICING filter, not a measurement filter.
    inline float onePoleFast (float hz) const noexcept
    {
        const float w = 6.2831853f * hz / fs_;
        if (w >= 4.0f) return 1.0f;
        const float s = w * (1.0f + w * (0.5f + w * (1.0f / 6.0f)));
        return s / (1.0f + s);
    }

    // TerrainChorus.h:129-143 — the house 4-point Hermite kernel, lifted verbatim. Linear
    // interpolation under modulation ripples HF (the JOS result); allpass interpolation
    // smears under fast modulation (Dattorro's own caveat) and its phase ripple is the
    // Serum-HQ dispersion the delay device deliberately rejected.
    inline float readH (const float* b, float d) const noexcept
    {
        float rp = (float) wr_ - d;
        if (rp < 0.0f) rp += (float) bufN_;
        const int   i0  = (int) rp;
        const float f   = rp - (float) i0;
        const int   m   = mask_;
        const float ym1 = b[(size_t) ((i0 - 1 + bufN_) & m)];
        const float y0  = b[(size_t) (i0 & m)];
        const float y1  = b[(size_t) ((i0 + 1) & m)];
        const float y2  = b[(size_t) ((i0 + 2) & m)];
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    static inline float triW (float p) noexcept
    { p -= std::floor (p); return p < 0.5f ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p); }
    static inline float sinW (float p) noexcept { return std::sin (6.2831853f * p); }

    // pitfall 14 — a raw triangle's slope discontinuity buzzes the delay derivative at high
    // Rate. The Juno's own LP'd triangle is the fix: one-pole at k*rate, always on. The
    // BBD-authentic triangle keeps |slope| constant, which is what makes the ear hear "two
    // detuned voices" instead of a wobble.
    inline float shape (int wave, float p, int idx, bool forceLp) noexcept
    {
        if (wave == 1 && ! forceLp) return sinW (p);
        const float raw = (wave == 1) ? sinW (p) : triW (p);
        const float k   = (wave == 2 || forceLp) ? 3.0f : 12.0f;
        const float a   = onePoleFast (std::max (0.05f, k * rateSm_));
        triZ_[idx] = flush (triZ_[idx] + a * (raw - triZ_[idx]));
        return triZ_[idx];
    }

    // §1.1 the Raffel/Smith BBD polynomial  f(x) = x - x^2/8 - x^3/18 + 1/8.
    // ⚠️ THE +1/8 IS A CONSTANT DC OFFSET, not a recentering: f(0) = +0.125 exactly, and for
    // zero-mean program the -x^2/8 term removes only E[x^2]/8 (~0.0002 at our bus) — six
    // hundred times smaller. It is subtracted ANALYTICALLY here rather than left for the DC
    // blocker, because a constant offset is not a signal:
    //   * measured: with it left in, a fresh engine emitted 0.125/k of DC that the 12 Hz
    //     blocker decayed over ~13 ms — a POWER-ON THUMP at -19 dBFS relative to program,
    //     which the Mix-100 dry-leak gate caught as a bogus "dry leak";
    //   * and inside a 0.82 feedback loop it converges to 0.125/k/(1-fb) = 0.087, which
    //     shifts the poly's own operating point and measured 1.8 % DC on Dark.
    // The blocker STAYS: the -x^2/8 term emits SIGNAL-dependent DC, which no constant can
    // cancel, and asymmetric Characters emit more.
    static inline float poly (float x) noexcept
    {
        x = clampf (x, -1.6f, 1.6f);
        return x - x * x * 0.125f - x * x * x * (1.0f / 18.0f);
    }

    // The NE570 is a LEVEL DETECTOR DRIVING GAIN, not a static curve. REF = 0.05 linear is
    // the -26 dBFS program anchor: the pair idles at unity on program, PUMPS on transients
    // and BREATHES on decays. (The legacy tanh/sinh pair measures a static x1.47 = +3.35 dB
    // trim at this level with ZERO level-dependence — a costume, and 3.4 dB of un-budgeted
    // make-up hiding inside a "character" block.)
    static inline float companderGain (float env, float e) noexcept
    {
        const float u = std::max (env, 1.0e-5f) * 20.0f;             // env / REF
        return clampf (std::exp2 (e * std::log2 (u)), 0.25f, 4.0f);
    }

    // CE-2 pre/de-emphasis: +6 dB above 3 kHz into the line, EXACTLY inverted after it (the
    // hiss-ducking pair). Both stages sit OUTSIDE the feedback tap — the de-emph is up to
    // x2 and would eat the whole loop-gain budget.
    inline float preEmph (float x, float& z) const noexcept
    { z += peA_ * (x - z); return 2.0f * x - z; }
    inline float deEmph (float y, float& z) const noexcept
    { const float u = (y + z * (1.0f - peA_)) / (2.0f - peA_); z += peA_ * (u - z); return u; }

    struct SR { float st, tg, ph; };
    inline void tickSR (SR& s, float rateHz, bool asym) noexcept
    {
        s.ph += rateHz / fs_;
        if (s.ph >= 1.0f) { s.ph -= 1.0f; s.tg = rand11(); }
        if (asym) s.st += (s.tg > s.st ? srUpK_ : srDnK_) * (s.tg - s.st);
        else      s.st += srUpK_ * 0.35f * (s.tg - s.st);
        s.st = flush (s.st);
    }

    inline float rand11() noexcept
    { rng_ = rng_ * 1664525u + 1013904223u; return (float) ((int32_t) rng_) * (1.0f / 2147483648.0f); }

    // ═════════ state ═════════════════════════════════════════════════════════
    std::vector<float> bufL_, bufR_;
    int   bufN_ = 0, mask_ = 0, wr_ = 0;
    float fs_ = 48000.0f;

    Params p_;
    int   type_ = 0, char_ = 0, pendType_ = -1, pendChar_ = -1;
    bool  seeded_ = false;

    float rateTg_ = 0.5f,  rateSm_ = 0.5f;
    float rkTg_   = 0.35f, rkSm_   = 0.35f;
    float baseTg_ = 8.5f,  baseSm_ = 8.5f;
    float depthTg_= 0.5f,  depthSm_= 0.5f;
    float widthTg_= 0.7f,  widthSm_= 0.7f;
    float flutTg_ = 0.25f, flutSm_ = 0.25f;
    float colorTg_= 0.5f,  colorSm_= 0.5f;
    float phaseTg_= 1.0f,  phaseSm_= 1.0f;
    float detTg_  = 0.0f,  detSm_  = 0.0f;
    float driftTg_= 0.0f,  driftSm_= 0.0f;
    float fbTg_   = 0.0f,  fbSm_   = 0.0f;
    float lkTg_   = 20.0f, lkSm_   = 20.0f;
    float mixTg_  = 0.5f,  mixSm_  = 0.5f;

    // per-block resolved
    // fb397 — Max: "we should be able to MAX OUT our amplifications... feedback at 100%% sounds
    //   crazy, like detuned... a tornado siren." The hard rule sets a knob's top at where the
    //   sound stops being USEFUL to somebody, not where the DSP stops being clean. 6 ms of
    //   excursion was clean and safe; Serum's chorus reaches ~30 ms and Max wants that travel.
    float excMs_ = 2.95f, gritSpan_ = 7.0f, colBase_ = 1800.0f;
    float compK_ = 1.0f, compSm_ = 1.0f, noiseAmp_ = 0.0f, fastHz_ = 6.0f, skew_ = 1.0f;
    float cmpGlide_ = 0.0006f;
    bool  skewOn_ = false, sameGeom_ = false;

    float smK_ = 0.001f, envK_ = 0.001f, cmpK_ = 0.004f, lvlK_ = 0.0003f;
    float dipDn_ = 0.0026f, dipUp_ = 0.0007f, srUpK_ = 0.0005f, srDnK_ = 0.00005f;
    float dcR_ = 0.9984f, peA_ = 0.32f;

    float mph_ = 0.0f, sph_ = 0.0f, fph_ = 0.0f, vph_ = 0.0f, wph_[3] = { 0.0f, 0.25f, 0.5f };
    float triZ_[8] = { 0,0,0,0,0,0,0,0 };
    SR    sr_[kNumSR] {};

    TapCfg tap_[2][kMaxTaps] {};
    int    nTap_ = 1;

    float reconZ_[2] {}, recon2_[2] {}, recon3_[2] {}, nzZ_[2] {}, lkZ_[2] {}, lkW_[2] {}, deZ_[2] {}, peZ_[2] {};
    float cmpEnv_[2] {}, expEnv_[2] {}, fbTap_[2] {}, dcX_[2] {}, dcY_[2] {}, dcXo_[2] {}, dcYo_[2] {}, q_[2] {};
    float envIn_ = 0.0f, lvlSm_ = 0.0f, dip_ = 1.0f;
    float dbgDelay_[2] = { 0.0f, 0.0f };
    uint32_t rng_ = 0x2545F491u;

    Viz viz_;
};

} // namespace tw
