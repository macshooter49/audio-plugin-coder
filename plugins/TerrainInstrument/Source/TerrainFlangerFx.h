#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  TerrainFlangerFx.h — the FX-rack FLANGER device (chain kind 7). fx3/fb395+.
//
//  Six Types, eight Characters each, three heroes + Mix on the front and eight on
//  the back. Header-only, pure C++ (no JUCE), `tw::` namespace, the fx3 CONTRACT §2
//  interface exactly. Harness: flanger_cert.cpp (same directory), which compiles and
//  runs this file standalone and prints real numbers for every claim below.
//
//  ══ THE ONE ARCHITECTURAL DECISION EVERYTHING ELSE FOLLOWS ══════════════════
//  Every Type is a TWO-DECK machine: a REFERENCE read at a fixed τ0 and a LAG read
//  at τ0 + Δ(t). The wet is 0.5·(ref ± m·lag).
//
//  Why, in one paragraph, because it is the difference between shipping and not:
//  a flanger is `dry + delayed`. If the engine returns only the delayed leg, then at
//  Mix 100 % — which house law 3 says is FULLY WET — the comb VANISHES: 100 % of a
//  1 ms delayed copy sounds like the input. Every plugin flanger solves this by
//  putting a direct leg inside its own wet path, which then trips the "< −60 dB dry
//  residual at Mix 1.0" gate. The two-deck form solves BOTH at once: the direct leg
//  is a DELAYED direct leg, so
//      0.5·(e^{-jωτ0} + e^{-jω(τ0+Δ)}) = e^{-jωτ0} · 0.5·(1 + e^{-jωΔ})
//  — bit-identical comb MAGNITUDE to the classic feedforward flanger, and a pure
//  linear-phase τ0 in front of it. There is literally zero undelayed dry in the wet
//  path, so an impulse at Mix 1.0 reads y[0] == 0.0 EXACTLY (measured, not asserted).
//  And it is what tape flanging physically IS: two decks, neither of them "dry".
//  τ0 = 2 samples for the five short-delay Types (a 42 µs pure delay at 48 k, whose
//  own comb with the true dry at intermediate Mix has its first notch at 12 kHz —
//  inaudible) and 8 ms for Tape Zero, which needs the headroom for Δ < 0.
//
//  ══ INTERPOLATION — chosen, not inherited ═══════════════════════════════════
//  4-point cubic Hermite (Catmull-Rom), and no lower-quality path exists at any
//  setting. Three reasons, all of which the harness measures:
//   · ALLPASS is banned under modulation: its state memory scrambles when the
//     fractional delay moves, which is a click, not a colour (JOS / KVR consensus,
//     and TerrainChorus.h:129 already settled on Hermite in-house).
//   · LINEAR has a fractional-position-dependent HF droop (|H| = cos(πf·frac/fs)),
//     which means the two decks stop matching as soon as they sit at different
//     fractional offsets — the through-zero null then shallows at HF exactly where
//     the ear notices. It is the single measurable reason a TZF needs better than
//     linear, and it is why there is no Eco read here.
//   · Hermite's own error vanishes identically at frac = 0. Both τ0 values are
//     EXACT INTEGER sample counts, so the reference deck is interpolation-free, and
//     at Δ = 0 the lag deck lands on the same integer index — the null is then a
//     bit-exact cancellation independent of interpolator quality.
//
//  ══ THE LOOP-GAIN LEDGER (law 6) ════════════════════════════════════════════
//    user Feedback              ≤ 0.97
//    Hermite 4-pt read          ≤ 1.06 worst case (can exceed unity on alternating
//                                    -sign content — this is the trap that makes a
//                                    "stable 0.95" diverge)
//    damping LP / low-cut HP / DC blocker   ≤ 1.0 each
//    ⇒ worst product ≈ 1.03 — nominally unstable, therefore the in-loop soft clip is
//    NOT optional. It is BIBO-bounding, C1-continuous, and its knee is a Character.
//  ⚠️ The house softClip in DelayEngine.h:315-322 is DISCONTINUOUS (`if (x>1.4)
//  return tanh(x)` jumps 1.4 → 0.885 at the knee). Recycling it verbatim would have
//  imported a click generator into a loop that is designed to reach the knee. The
//  shape here is the same idea, made continuous and C1 at the knee. Reported.
//
//  ══ NOTHING FREE-RUNS ═══════════════════════════════════════════════════════
//  The feedback COEFFICIENT (never the output — gating the output clicks) is
//  multiplied by an input-presence gate, applied SQUARED (the grid-leak law), whose
//  release is the `Tail` knob. Input silent ⇒ the comb rings for Tail and then stops.
//
//  ══ NO DISK, NO ALLOC, NO LOCKS below prepare() ═════════════════════════════
//  Denormals are flushed on every recirculating state; assume ScopedNoDenormals is
//  NOT set for us.
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>

namespace tw {

class TerrainFlangerFx
{
public:
    // ── identity ─────────────────────────────────────────────────────────────
    static constexpr int kNumTypes = 6;
    static constexpr int kNumChars = 8;
    static_assert (kNumTypes > 0 && kNumChars == 8, "roster/table must move together");

    enum TypeId { TapeZero = 0, Jet = 1, Bbd = 2, Endless = 3, Envelope = 4, Step = 5 };

    static const char* const* typeNames() noexcept
    {
        static const char* const n[kNumTypes] =
            { "Tape Zero", "Jet", "BBD", "Endless", "Envelope", "Step" };
        return n;
    }

    static const char* const* charNames (int type) noexcept
    {
        static const char* const c[kNumTypes][kNumChars] =
        {
            // Tape Zero — the two-deck through-zero machine
            { "Sub", "Add", "Worn Deck", "Servo", "Wide Zero", "Deep Zero", "Drifting Zero", "Counter Reel" },
            // Jet — the pedal comb
            { "Silver", "Compact", "Deep Sweep", "Hollow", "Screamer", "Drop", "Thin Air", "Twin Jet" },
            // BBD — the bucket brigade
            { "Mistress", "Deluxe", "Dark Bucket", "Squash", "Matrix", "Short Bucket", "Long Bucket", "Grind" },
            // Endless — the DAFx-15 dual sawtooth comb
            { "Rise", "Fall", "Rise Deep", "Fall Deep", "Double Helix", "Stacked Rise", "Soft Rise", "Tight Rise" },
            // Envelope — the comb chases the playing
            { "Up", "Down", "Snap", "Slow Swell", "Duck Zero", "Hold", "Wide Touch", "Deep Touch" },
            // Step — the tempo-locked sample-and-hold comb
            { "Random", "Stair Up", "Stair Down", "Pendulum", "Ratchet", "Drunk", "Wide Steps", "Glide" }
        };
        const int t = (type < 0) ? 0 : (type >= kNumTypes ? kNumTypes - 1 : type);
        return c[t];
    }

    // ── tempo sync: the house 20-entry list, cloned WHOLE from
    //    PluginProcessor.cpp:3455-3459 (and FilterFxEngine.h:60) including "Free" at
    //    index 0. All three fx3 devices must publish this identical table.
    static constexpr int kNumDivs = 20;
    static float divBeats (int i) noexcept
    {
        static const float B[kNumDivs] = { 0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 3.0f, 1.3333f, 1.0f,
                                           1.5f, 0.6667f, 0.5f, 0.75f, 0.3333f, 0.25f, 0.375f,
                                           0.1667f, 0.125f, 0.0625f, 0.03125f, 0.015625f };
        return B[i < 0 ? 0 : (i >= kNumDivs ? kNumDivs - 1 : i)];
    }
    static const char* const* divNames() noexcept
    {
        static const char* const N[kNumDivs] =
            { "Free", "4 bar", "2 bar", "1 bar", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T",
              "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32", "1/64", "1/128", "1/256" };
        return N;
    }

    struct Params
    {
        int   type = 0, character = 0;

        // ── FRONT 3 + Mix ────────────────────────────────────────────────────
        float rate     = 0.35f;   // Rate      0..1 → 0.02..20 Hz log free, or a sync division
        float depth    = 0.55f;   // Depth     0..1 → sweep excursion (Endless: notch depth)
        // ⚠️ Feedback is BIPOLAR and 0.5 IS THE CENTRE (= no feedback). 0.0 = −97 %
        //    (hollow / odd series), 1.0 = +97 % (jet / harmonic series). Do NOT wire a
        //    unipolar 0-default param to this field — 0.0 is full NEGATIVE regeneration.
        float feedback = 0.5f;
        float mix      = 0.5f;    // 1.0 = FULLY WET, ZERO DRY (law 3)

        // ── BACK 8, all 0..1 ─────────────────────────────────────────────────
        float b1 = 0.5f;    // Manual   0.1–20 ms log · Tape Zero: Zero Bias −7.5…+7.5 ms
        float b2 = 0.35f;   // Spread   0–180° L/R sweep phase offset
        float b3 = 0.625f;  // Width    0–160 % wet M/S   (0.625 = 100 %, neutral)
        float b4 = 0.35f;   // Damping  in-loop LP 20 kHz → 700 Hz (0 = undamped)
        float b5 = 0.5f;    // Shape    LFO sine→tri→ramp · Step: 2–24 steps · Env: curve
        float b6 = 0.20f;   // Bounce   servo hunt + tape drift (the analog instability)
        float b7 = 0.35f;   // Tail     feedback gate release 60 ms – 3 s (Env: follower rel)
        float b8 = 0.12f;   // Low Cut  20 Hz – 1 kHz, in-loop AND wet

        bool   tempoSync = false;
        double bpm       = 120.0;
    };

    struct Viz
    {
        float lfo      = 0.0f;    // −1..+1 instantaneous sweep — THE needle
        float lvl      = 0.0f;    // wet level 0..1
        float notch[8] {};        // comb notch centres in Hz, 0 = unused
        float depthNow = 0.0f;    // effective excursion, ms
    };

    // ═════════════════════════════════════════════════════════════════════════
    void prepare (double sampleRate, int /*maxBlock*/) noexcept
    {
        fs_ = (sampleRate > 8000.0) ? (float) sampleRate : 48000.0f;

        // 60 ms of ring + Hermite guard, power of two. 42 ms max comb + 8 ms Tape Zero
        // reference deck + drift headroom fits inside with room to spare.
        int need = (int) std::ceil (0.062f * fs_) + 64;
        int sz = 64; while (sz < need) sz <<= 1;
        bufL_.assign ((size_t) sz, 0.0f);
        bufR_.assign ((size_t) sz, 0.0f);
        dryL_.assign ((size_t) sz, 0.0f);
        dryR_.assign ((size_t) sz, 0.0f);
        mask_ = sz - 1;

        kSm_   = 1.0f - std::exp (-1.0f / (0.015f * fs_));   // 15 ms generic glide
        kFast_ = 1.0f - std::exp (-1.0f / (0.003f * fs_));   // 3 ms gate attack
        dipDn_ = 1.0f - std::exp (-1.0f / (0.003f * fs_));
        dipUp_ = 1.0f - std::exp (-1.0f / (0.040f * fs_));

        driftL_.prepare (fs_, 0x9E3779B9u);
        driftR_.prepare (fs_, 0x85EBCA6Bu);
        biasDrift_.prepare (fs_, 0xC2B2AE35u);

        reset();
    }

    void reset() noexcept
    {
        std::fill (bufL_.begin(), bufL_.end(), 0.0f);
        std::fill (bufR_.begin(), bufR_.end(), 0.0f);
        std::fill (dryL_.begin(), dryL_.end(), 0.0f);
        std::fill (dryR_.begin(), dryR_.end(), 0.0f);
        wr_ = 0;

        ch_[0].clear(); ch_[1].clear();
        driftL_.reset(); driftR_.reset(); biasDrift_.reset();

        ph_ = 0.0f; sawPh_ = 0.0f;
        bs_ = 0.0f; bv_ = 0.0f;
        envIn_ = 0.0f; gate_ = 0.0f; lvlSm_ = 0.0f;
        envF_[0] = envF_[1] = 0.0f;
        holdPk_ = 0.0f;
        stepPh_ = 0.0f; stepCur_ = 0.0f; stepTgt_ = 0.0f; stepIdx_ = 0; stepK_ = 0; stepDir_ = 1;
        stepRng_ = 0x2545F491u; stepGl_ = 0.01f; stepCurR_ = 0.0f; stepTgtR_ = 0.0f;
        dip_ = 1.0f; pendType_ = -1; pendChr_ = -1;
        primed_ = false;
        viz_ = Viz{};
    }

    void setParams (const Params& p) noexcept { p_ = p; }

    // ═════════════════════════════════════════════════════════════════════════
    //  IN PLACE. Owns its own equal-power dry/wet from Params::mix.
    // ═════════════════════════════════════════════════════════════════════════
    void processStereo (float* L, float* R, int numSamples) noexcept
    {
        if (L == nullptr || R == nullptr || numSamples <= 0) return;

        // ── type / character swap: dip to 2 %, swap at the floor, recover. The ring
        //    is NOT cleared (that would guillotine the tail); the STATE is re-seated.
        const int wantT = clampi (p_.type, 0, kNumTypes - 1);
        const int wantC = clampi (p_.character, 0, kNumChars - 1);
        if ((wantT != type_ || wantC != chr_) && (pendType_ != wantT || pendChr_ != wantC))
        { pendType_ = wantT; pendChr_ = wantC; }
        if (! primed_) { type_ = wantT; chr_ = wantC; pendType_ = pendChr_ = -1; dip_ = 1.0f; }

        const CharSpec* cs = &spec (type_, chr_);
        cookBlock (*cs);
        if (! primed_) { snapSmoothers(); primed_ = true; }

        const float twoPi = 6.2831853071795864f;
        float peakWet = 0.0f;
        float lastComb = 0.0f, lastMod = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // ── 0. swap dip ──────────────────────────────────────────────────
            if (pendType_ >= 0)
            {
                dip_ += (0.02f - dip_) * dipDn_;
                if (dip_ < 0.05f)
                {
                    type_ = pendType_; chr_ = pendChr_;
                    pendType_ = pendChr_ = -1;
                    reseat();                       // followers / companders / filters
                    cs = &spec (type_, chr_);
                    cookBlock (*cs);                // new type's targets
                    snapDelays();                   // and land on them, silently
                }
            }
            else dip_ += (1.0f - dip_) * dipUp_;
            const CharSpec& c = *cs;

            // ── 1. per-sample smoothers (the no-clicks law) ──────────────────
            manS_  += kSm_ * (manT_  - manS_);
            biaS_  += kSm_ * (biaT_  - biaS_);
            depS_  += kSm_ * (depT_  - depS_);
            fbS_   += kSm_ * (fbT_   - fbS_);
            sprS_  += kSm_ * (sprT_  - sprS_);
            widS_  += kSm_ * (widT_  - widS_);
            dmpS_  += kSm_ * (dmpT_  - dmpS_);
            dLagS_ += kSm_ * (dLagT_ - dLagS_);
            lowS_  += kSm_ * (lowT_  - lowS_);
            bncS_  += kSm_ * (bncT_  - bncS_);
            shpS_  += kSm_ * (shpT_  - shpS_);
            mixS_  += kSm_ * (mixT_  - mixS_);
            combS_ += kSm_ * (combT_ - combS_);

            dryS_ += kSm_ * (dryT_ - dryS_);
            wetS_ += kSm_ * (wetT_ - wetS_);
            nrmS_ += kSm_ * (nrmT_ - nrmS_);
            const float dryG = dryS_, wetG = wetS_;

            const float inL = L[i], inR = R[i];

            // ── 2. input presence: the feedback gate + the Envelope Type source ──
            const float rect = std::max (std::fabs (inL), std::fabs (inR));
            envIn_ += ((rect > envIn_) ? kFast_ : gateRel_) * (rect - envIn_);
            envIn_ = flush (envIn_);
            lvlSm_ += ((rect > lvlSm_) ? 0.02f : 0.0015f) * (rect - lvlSm_);
            // gate applied SQUARED — a linear release latches audibly, the square dies.
            const float g1 = std::min (1.0f, envIn_ * 150.0f);
            gate_ = g1 * g1;

            // ── 3. THE MODULATOR — one master clock, offsets derived at read time ─
            ph_ += inc_; if (ph_ >= 1.0f) ph_ -= 1.0f;
            sawPh_ += sawInc_; if (sawPh_ >= 1.0f) sawPh_ -= 1.0f;

            float modL = 0.0f, modR = 0.0f;
            modulator (c, inL, inR, modL, modR);

            // ── 4. GEOMETRY, then the reads ─────────────────────────────────
            Geo gL, gR;
            geometry (c, modL, 0, gL);
            geometry (c, modR, 1, gR);

            const float wetL = deck (0, gL, c);
            const float wetR = deck (1, gR, c);

            // ── 5. the feedback write. The COEFFICIENT is gated, never the output.
            const float fbL = loop (0, gL, c);
            const float fbR = loop (1, gR, c);
            const float gFb = fbS_ * gate_;

            float nl = inL + gFb * fbL;
            float nr = inR + gFb * fbR;
            nl = softClip (nl, c.clipK);
            nr = softClip (nr, c.clipK);
            if (c.flags & F_BBD) { nl = ch_[0].compressIn (nl, c.pumpMul, cpAtk_, cpRel_);
                                   nr = ch_[1].compressIn (nr, c.pumpMul, cpAtk_, cpRel_); }
            bufL_[(size_t) wr_] = flush (nl);
            bufR_[(size_t) wr_] = flush (nr);
            dryL_[(size_t) wr_] = flush (inL);      // the CLEAN reference line
            dryR_[(size_t) wr_] = flush (inR);
            wr_ = (wr_ + 1) & mask_;

            // ── 6. wet post: M/S width (never in the loop), then the wet trim ──
            const float M = 0.5f * (wetL + wetR);
            const float S = 0.5f * (wetL - wetR) * widS_;
            float wl = (M + S) * kWetTrim_ * dip_;
            float wr2 = (M - S) * kWetTrim_ * dip_;

            peakWet = std::max (peakWet, std::fabs (wl));
            lastComb = gL.comb; lastMod = modL;

            L[i] = dryG * inL + wetG * wl;
            R[i] = dryG * inR + wetG * wr2;
        }

        // ── viz push, once per block ────────────────────────────────────────
        viz_.lfo = clampf (lastMod, -1.0f, 1.0f);
        viz_.lvl = std::min (1.0f, peakWet);
        const float dms = std::fabs (lastComb) * 1000.0f / fs_;
        viz_.depthNow = dms;
        const bool sub = (spec (type_, chr_).pol < 0.0f);
        for (int k = 0; k < 8; ++k)
        {
            // subtractive comb notches at k/Δ (k≥1), additive at (2k+1)/2Δ
            const float f = (dms > 1e-4f)
                ? (sub ? (float) (k + 1) * 1000.0f / dms
                       : (float) (2 * k + 1) * 500.0f / dms)
                : 0.0f;
            viz_.notch[k] = (f > 20.0f && f < 20000.0f) ? f : 0.0f;
        }
    }

    const Viz& viz() const noexcept { return viz_; }

    // Diagnostics for the harness only (never called from the audio thread).
    float dbgComb()  const noexcept { return viz_.depthNow; }
    float liveRateHz() const noexcept { return rateHz_; }   // fb413 — the cross-device sync gate
    bool  monoHostile() const noexcept { return (spec (type_, chr_).flags & F_MONO_RISK) != 0; }
    static bool monoHostile (int t, int c) noexcept { return (spec (t, c).flags & F_MONO_RISK) != 0; }
    // the voicings whose two channels deliberately counter-run. NOT the same thing as
    // mono-hostile: measured, all of them decorrelate hard and still survive a mono sum.
    static bool counterLR   (int t, int c) noexcept { return (spec (t, c).flags & F_COUNTER_LR) != 0; }

private:
    // ═════════════════════════════════════════════════════════════════════════
    //  CHARACTER TABLE — every field is a MECHANISM constant, never an EQ curve.
    // ═════════════════════════════════════════════════════════════════════════
    enum Flags : unsigned
    {
        F_BBD        = 1u << 0,   // compander + delay-tracking reconstruction filter
        F_COUNTER_LR = 1u << 1,   // the two channels counter-run (behaviour)
        F_MONO_RISK  = 1u << 9,   // and it MEASURABLY thins on a mono sum (warning tag)
        F_MATRIX     = 1u << 2,   // Mistress Filter Matrix: LFO nearly frozen
        F_DUCK_ZERO  = 1u << 3,   // envelope drives Δ toward ZERO (loud notes cancel)
        F_HOLD       = 1u << 4,   // peak-hold follower
        F_SPLIT_ENV  = 1u << 5,   // L/R followers with different attack
        F_COUNTER    = 1u << 6,   // Tape Zero: the reference deck counter-sweeps
        F_BIASDRIFT  = 1u << 7,   // Tape Zero: the zero point itself wanders
        F_TWINTAP    = 1u << 8    // a second swept tap at a fixed delay ratio
    };

    struct CharSpec
    {
        float pol;        // lag-deck polarity in the wet sum: +1 additive, −1 subtractive
        float spanMul;    // sweep excursion multiplier
        float dampMul;    // in-loop LP corner multiplier (the resonance's physics)
        float shapeAdd;   // added to the Shape morph position (LFO waveform)
        float dwell;      // Tape Zero zero-dwell exponent / Envelope response exponent
        float driftMul;   // tape drift stack multiplier
        float bounceZ;    // servo spring damping ratio (lower = rings longer)
        float clipK;      // in-loop soft-clip knee
        float reconMul;   // BBD reconstruction-filter corner multiplier
        float pumpMul;    // BBD compander mismatch
        float atkMul;     // envelope follower attack multiplier
        float baseMul;    // multiplies the Manual (base) delay
        float tapRatio;   // second swept tap's delay ratio (F_TWINTAP)
        float fbCap;      // per-Character ceiling on |feedback|
        signed char dir;  // +1 / −1 sweep direction
        unsigned  flags;
        unsigned char pat;// Step pattern id
    };

    static const CharSpec& spec (int type, int chr) noexcept
    {
        return kTable_[clampi (type, 0, kNumTypes - 1)][clampi (chr, 0, kNumChars - 1)];
    }

    // ⚠️ a constexpr CLASS member, not a function-local static. A function-local static
    //    carries a thread-safe-initialisation guard (an atomic acquire) paid on EVERY
    //    call, and spec() is on the per-sample path.
    static constexpr CharSpec kTable_[kNumTypes][kNumChars] =
        {   // pol  span  damp  shape dwell drift bncZ  clip  recon pump  atk   base  tapR  fbCap dir flags / pat
        {   // ---- Tape Zero ----
          { -1.0f, 1.00f, 1.00f,+0.00f, 1.35f, 1.0f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, 0               , 0 },  // Sub
          { +1.0f, 1.00f, 1.00f,+0.00f, 1.35f, 1.0f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, 0               , 0 },  // Add
          { -1.0f, 1.00f, 0.72f,+0.15f, 1.15f, 2.4f, 0.45f, 0.60f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.985f, +1, 0               , 0 },  // Worn Deck
          { -1.0f, 1.00f, 1.00f,+0.00f, 1.50f, 1.2f, 0.18f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, 0               , 0 },  // Servo
          { -1.0f, 1.00f, 1.00f,+0.00f, 1.35f, 1.0f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, F_COUNTER_LR    , 0 },  // Wide Zero
          { -1.0f, 1.18f, 1.00f,+0.00f, 2.00f, 1.0f, 0.55f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, 0               , 0 },  // Deep Zero
          { -1.0f, 1.00f, 0.90f,+0.05f, 1.35f, 1.8f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, F_BIASDRIFT     , 0 },  // Drifting Zero
          { -1.0f, 1.00f, 1.00f,+0.00f, 1.35f, 1.0f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, F_COUNTER       , 0 },  // Counter Reel
        },
        {   // ---- Jet ----
          { +1.0f, 1.00f, 1.00f,+0.00f, 1.00f, 0.6f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, 0               , 0 },  // Silver
          { +1.0f, 0.86f, 0.62f,-0.50f, 1.00f, 0.6f, 0.50f, 0.62f, 1.00f, 1.00f, 1.00f, 0.85f, 0.00f, 0.995f, +1, 0               , 0 },  // Compact
          { +1.0f, 1.50f, 0.80f,+0.10f, 1.00f, 0.7f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.20f, 0.00f, 0.995f, +1, 0               , 0 },  // Deep Sweep
          { -1.0f, 1.00f, 0.90f,+0.00f, 1.00f, 0.6f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, 0               , 0 },  // Hollow
          { +1.0f, 1.00f, 0.75f,+0.00f, 1.00f, 0.6f, 0.50f, 0.22f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, 0               , 0 },  // Screamer
          { +1.0f, 1.15f, 0.90f,+0.50f, 1.00f, 0.8f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, 0               , 0 },  // Drop
          { +1.0f, 1.00f, 1.35f,+0.00f, 1.00f, 0.6f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 0.55f, 0.00f, 0.995f, +1, 0               , 0 },  // Thin Air
          { +1.0f, 1.00f, 0.95f,+0.15f, 1.00f, 0.6f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 1.53f, 0.985f, +1, F_TWINTAP       , 0 },  // Twin Jet
        },
        {   // ---- BBD ----
          { +1.0f, 1.00f, 0.70f,+0.00f, 1.00f, 0.9f, 0.50f, 0.66f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, F_BBD           , 0 },  // Mistress
          { +1.0f, 1.00f, 0.90f,+0.00f, 1.00f, 0.8f, 0.50f, 0.70f, 1.60f, 0.45f, 1.60f, 1.00f, 0.00f, 0.995f, +1, F_BBD           , 0 },  // Deluxe
          { +1.0f, 1.00f, 0.40f,+0.00f, 1.00f, 1.0f, 0.50f, 0.62f, 0.40f, 1.10f, 1.00f, 1.10f, 0.00f, 0.995f, +1, F_BBD           , 0 },  // Dark Bucket
          { +1.0f, 1.00f, 0.70f,+0.00f, 1.00f, 1.0f, 0.50f, 0.60f, 0.95f, 2.10f, 0.25f, 1.00f, 0.00f, 0.995f, +1, F_BBD           , 0 },  // Squash
          { +1.0f, 1.00f, 0.70f,+0.00f, 1.00f, 0.6f, 0.50f, 0.66f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.995f, +1, F_BBD|F_MATRIX  , 0 },  // Matrix
          { +1.0f, 0.80f, 1.10f,+0.00f, 1.00f, 0.9f, 0.50f, 0.66f, 1.75f, 0.85f, 1.20f, 0.45f, 0.00f, 0.995f, +1, F_BBD           , 0 },  // Short Bucket
          { +1.0f, 1.10f, 0.55f,+0.00f, 1.00f, 1.1f, 0.50f, 0.66f, 0.62f, 1.20f, 0.90f, 2.00f, 0.00f, 0.995f, +1, F_BBD           , 0 },  // Long Bucket
          { -1.0f, 1.00f, 0.60f,+0.00f, 1.00f, 1.0f, 0.50f, 0.40f, 0.85f, 1.55f, 0.50f, 1.00f, 0.00f, 0.995f, +1, F_BBD           , 0 },  // Grind
        },
        {   // ---- Endless ----
          { +1.0f, 1.00f, 1.00f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.985f, +1, 0               , 0 },  // Rise
          { +1.0f, 1.00f, 1.00f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.985f, -1, 0               , 0 },  // Fall
          { +1.0f, 1.55f, 1.00f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.55f, 0.00f, 0.985f, +1, 0               , 0 },  // Rise Deep
          { +1.0f, 1.55f, 1.00f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.55f, 0.00f, 0.985f, -1, 0               , 0 },  // Fall Deep
          { +1.0f, 1.00f, 1.00f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.985f, +1, F_COUNTER_LR    , 0 },  // Double Helix
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 2.00f, 0.90f, +1, F_TWINTAP       , 0 },  // Stacked Rise
          { -1.0f, 1.00f, 1.00f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.985f, +1, 0               , 0 },  // Soft Rise
          { +1.0f, 0.75f, 1.15f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 0.60f, 0.00f, 0.985f, +1, 0               , 0 },  // Tight Rise
        },
        {   // ---- Envelope ----
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.985f, +1, 0               , 0 },  // Up
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.985f, -1, 0               , 0 },  // Down
          { +1.0f, 1.10f, 0.95f,+0.00f, 2.20f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 0.12f, 1.00f, 0.00f, 0.985f, +1, 0               , 0 },  // Snap
          { +1.0f, 1.00f, 0.95f,+0.00f, 0.55f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 6.00f, 1.00f, 0.00f, 0.985f, +1, 0               , 0 },  // Slow Swell
          { -1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.5f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.85f, +1, F_DUCK_ZERO     , 0 },  // Duck Zero
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 0.50f, 1.00f, 0.00f, 0.985f, +1, F_HOLD          , 0 },  // Hold
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.985f, +1, F_SPLIT_ENV     , 0 },  // Wide Touch
          { +1.0f, 1.70f, 0.95f,+0.00f, 1.00f, 0.4f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.20f, 0.00f, 0.985f, +1, 0               , 0 },  // Deep Touch
        },
        {   // ---- Step ----
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.3f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.98f, +1, 0               , 0 },  // Random
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.3f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.98f, +1, 0               , 1 },  // Stair Up
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.3f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.98f, +1, 0               , 2 },  // Stair Down
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.3f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.98f, +1, 0               , 3 },  // Pendulum
          { +1.0f, 1.15f, 0.85f,+0.00f, 1.00f, 0.3f, 0.50f, 0.66f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.98f, +1, 0               , 4 },  // Ratchet
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.3f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.98f, +1, 0               , 5 },  // Drunk
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.3f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.98f, +1, F_COUNTER_LR    , 6 },  // Wide Steps
          { +1.0f, 1.00f, 0.95f,+0.00f, 1.00f, 0.3f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.98f, +1, 0               , 7 },  // Glide
        }
        };

    // ═════════════════════════════════════════════════════════════════════════
    //  small parts
    // ═════════════════════════════════════════════════════════════════════════
    static inline int   clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static inline float flush (float x) noexcept { return (std::fabs (x) < 1.0e-20f) ? 0.0f : x; }

    // sin(2*pi*p) for p in [0,1). Parabola + one correction term, ~0.1 % error - far
    // inside an LFO's needs, and it is called up to 3x per sample.
    static inline float fsin (float p) noexcept
    {
        p -= std::floor (p);
        const float x = (p < 0.5f) ? 2.0f * p : 2.0f * p - 2.0f;      // sin(pi*x)
        const float y = 4.0f * x * (1.0f - std::fabs (x));
        return 0.225f * (y * std::fabs (y) - y) + y;
    }
    // 2^x and log2(x), ~1e-5 relative. The sweep map is exponential BY DESIGN (an
    // exponential delay sweep reads as a linear pitch dive), so exp2 is on the
    // per-sample path for four of the six Types and is worth doing cheaply.
    static inline float fexp2 (float x) noexcept
    {
        if (x < -60.0f) return 0.0f;
        if (x >  60.0f) return 1.0e18f;
        const float xf = std::floor (x);
        const float f  = x - xf;
        const float pl = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
        const int32_t bits = (int32_t) ((int) xf + 127) << 23;
        float sc; std::memcpy (&sc, &bits, sizeof sc);
        return pl * sc;
    }
    static inline float flog2 (float x) noexcept
    {
        if (x <= 0.0f) return -60.0f;
        int32_t i; std::memcpy (&i, &x, sizeof i);
        const int e = ((i >> 23) & 255) - 127;
        i = (i & 0x007FFFFF) | (127 << 23);
        float m; std::memcpy (&m, &i, sizeof m);
        const float pl = -1.7417939f + m * (2.8212026f + m * (-1.4699568f + m * (0.4478100f + m * (-0.0563887f))));
        return (float) e + pl;
    }
    // Pade tanh, clamped. |err| < 2e-4 over the useful range.
    static inline float ftanh (float x) noexcept
    {
        if (x >  3.2f) return 0.99668f;
        if (x < -3.2f) return -0.99668f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
    // sinh by series; the compander only ever feeds it |x| <= 3.
    static inline float fsinh (float x) noexcept
    {
        const float x2 = x * x;
        return x * (1.0f + x2 * (0.1666667f + x2 * (0.008333333f + x2 * 0.000198413f)));
    }

    // C1-continuous bounded soft clip. Linear to ±k, asymptotic to ±kCeil.
    // (Deliberately NOT DelayEngine.h:315's version, which jumps at the knee.)
    static inline float softClip (float x, float k) noexcept
    {
        const float C = 1.30f;
        const float a = std::fabs (x);
        if (a <= k) return x;
        const float s = (x < 0.0f) ? -1.0f : 1.0f;
        return s * (k + (C - k) * ftanh ((a - k) / (C - k)));
    }

    // Soft two-sided limiter that is EXACT below half-range — used on the sweep
    // argument so the clamp never hard-corners (the bible's "fold the clamp softly").
    static inline float softLim (float x, float L) noexcept
    {
        const float h = 0.5f * L;
        const float a = std::fabs (x);
        if (a <= h) return x;
        const float s = (x < 0.0f) ? -1.0f : 1.0f;
        return s * (h + h * std::tanh ((a - h) / h));
    }

    // The tape drift stack — the TapeMachines.h SmoothRandom triple (0.6/2.2/7 Hz).
    struct Drift
    {
        float st[3] {}, tg[3] {}, ph[3] {}, inc[3] {}, sm[3] {};
        uint32_t rng = 1u;
        void prepare (float fs, uint32_t seed) noexcept
        {
            const float r[3] = { 0.6f, 2.2f, 7.0f };
            const float s[3] = { 2.5f, 8.0f, 24.0f };
            rng = seed | 1u;
            for (int i = 0; i < 3; ++i)
            { inc[i] = r[i] / fs; sm[i] = std::min (0.12f, 1.0f - std::exp (-6.2831853f * s[i] / fs));
              ph[i] = 0.0f; st[i] = 0.0f; tg[i] = 0.0f; }
        }
        float held = 0.0f, out = 0.0f; int dec = 0;
        void reset() noexcept { for (int i = 0; i < 3; ++i) { st[i] = tg[i] = ph[i] = 0.0f; } held = out = 0.0f; dec = 0; }
        inline float next() noexcept
        {
            if (--dec <= 0)
            {
                dec = 8;                                  // 6 kHz refresh for a <= 7 Hz signal
                static const float w[3] = { 0.62f, 0.26f, 0.12f };
                float o = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    ph[i] += inc[i] * 8.0f;
                    if (ph[i] >= 1.0f)
                    { ph[i] -= 1.0f; rng = rng * 1664525u + 1013904223u;
                      tg[i] = (float) (int32_t) rng * (1.0f / 2147483648.0f); }
                    st[i] += sm[i] * 8.0f * (tg[i] - st[i]);
                    st[i] = flush (st[i]);
                    o += w[i] * st[i];
                }
                held = o;
            }
            out += 0.06f * (held - out);                  // interpolate, never step the delay
            return flush (out);
        }
    };

    // Per-channel state.
    struct Chan
    {
        float dampZ = 0.0f;      // in-loop one-pole LP state
        float lowZ  = 0.0f;      // in-loop one-pole HP state
        float dcX = 0.0f, dcY = 0.0f;
        float wLowZ = 0.0f;      // wet-path HP state
        float rec[4] {}, rc2[2] {};   // BBD reconstruction cascades
        float dLagZ = 0.0f, dRefZ = 0.0f;   // per-deck damping (the delay path's band limit)
        float envC = 0.0f, envE = 0.0f;   // compander followers (mismatched time constants)

        void clear() noexcept
        { dampZ = lowZ = dcX = dcY = wLowZ = envC = envE = dLagZ = dRefZ = 0.0f;
          rec[0]=rec[1]=rec[2]=rec[3]=0.0f; rc2[0]=rc2[1]=0.0f; }

        // NE570-flavoured compander. The compress/expand pair is unity at small
        // signal and mismatched in its TIME CONSTANTS — that mismatch is the pump.
        inline float compressIn (float x, float pumpMul, float aC, float rC) noexcept
        {
            const float a = std::fabs (x);
            envC += ((a > envC) ? aC : rC) * (a - envC); envC = flush (envC);
            const float gc = 1.0f / (1.0f + 13.0f * pumpMul * envC);
            const float k = 2.2f;
            return ftanh (k * x * gc) * (1.0f / k);
        }
        inline float expandOut (float y, float pumpMul, float aE, float rE) noexcept
        {
            const float a = std::fabs (y);
            envE += ((a > envE) ? aE : rE) * (a - envE); envE = flush (envE);
            const float ge = std::min (4.0f, 1.0f + 13.0f * pumpMul * envE);
            const float k = 2.2f;
            return fsinh (clampf (k * y, -3.0f, 3.0f)) * (1.0f / k) * ge;
        }
        // 4-pole reconstruction lowpass, corner supplied per sample (it tracks the
        // instantaneous delay — the bucket-brigade clock-droop law).
        inline float recon (float x, float c) noexcept
        {
            float v = x;
            for (int i = 0; i < 4; ++i) { rec[i] += c * (v - rec[i]); rec[i] = flush (rec[i]); v = rec[i]; }
            return v;
        }
        inline float recon2 (float x, float c) noexcept
        {
            float v = x;
            for (int i = 0; i < 2; ++i) { rc2[i] += c * (v - rc2[i]); rc2[i] = flush (rc2[i]); v = rc2[i]; }
            return v;
        }
    };

    struct Geo
    {
        float ref  = 2.0f;   // reference deck delay, samples
        float d1   = 2.0f;   // swept deck 1, samples (absolute)
        float w1   = 1.0f;
        float d2   = 2.0f;   // swept deck 2 (Endless / twin tap)
        float w2   = 0.0f;
        float fb   = 16.0f;  // feedback tap delay, samples
        float comb = 0.0f;   // Δ in samples (signed; only Tape Zero goes negative)
        float m    = 1.0f;   // comb mix depth
    };

    // ── 4-point cubic Hermite (Catmull-Rom). d ≥ 2 enforced by the caller.
    inline float readAt (const float* b, float d) const noexcept
    {
        const float lim = (float) (mask_ - 3);
        if (d < 2.0f) d = 2.0f; else if (d > lim) d = lim;
        const int   di = (int) d;
        const float fr = d - (float) di;
        const int   im1 = (wr_ - di + 1) & mask_;
        const int   i0  = (wr_ - di)     & mask_;
        const int   i1  = (wr_ - di - 1) & mask_;
        const int   i2  = (wr_ - di - 2) & mask_;
        const float ym1 = b[(size_t) im1], y0 = b[(size_t) i0], y1 = b[(size_t) i1], y2 = b[(size_t) i2];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * fr + c2) * fr + c1) * fr + y0;
    }

    // ── LFO shape: TRIANGLE -> SINE -> soft RAMP, continuously morphed.
    //    Order matters: a triangle's sweep speed is CONSTANT and a sine's is not, so
    //    sine->tri->ramp folds back on itself on every shape statistic - a plateau in
    //    the middle of the knob by construction (law 1). tri->sine->ramp is monotone
    //    in sweep-speed crest: 1.0 -> 1.4 -> ~11.
    //    The "ramp" returns over 8 % of the cycle rather than instantly: a hard saw
    //    reset on a delay READ POSITION is a click; an 8 % return is a tape drop.
    static inline float shapeMorph (float p, float s) noexcept
    {
        const float sn = fsin (p);
        const float tr = (p < 0.25f) ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        const float rp = (p < 0.92f) ? (2.0f * p / 0.92f - 1.0f) : (1.0f - 2.0f * (p - 0.92f) / 0.08f);
        if (s < 0.5f) { const float t = s * 2.0f;   return tr + t * (sn - tr); }
        const float t = (s - 0.5f) * 2.0f;          return sn + t * (rp - sn);
    }

    // ═════════════════════════════════════════════════════════════════════════
    void cookBlock (const CharSpec& c) noexcept
    {
        // Rate → Hz (free log 0.02–20 Hz, or a sync division; identical list in all
        // three fx3 devices). Sync skips index 0 ("Free" is what the pill is for).
        float hz;
        if (p_.tempoSync)
        {
            const int idx = 1 + (int) std::lround (clampf (p_.rate, 0.0f, 1.0f) * (float) (kNumDivs - 2));
            const float beats = divBeats (idx);
            const double bpm = (p_.bpm > 1.0) ? p_.bpm : 120.0;
            hz = (float) (bpm / 60.0) / std::max (1.0e-4f, beats);
        }
        else
        {
            // fb411 - NO PLAYING SAFE, SHAPED (the fb397 law). 20 Hz was a clean, safe ceiling
            // and the hard rule puts a knob's top where the sound stops being USEFUL, not where
            // the DSP stops being clean. Above 60 % of the travel the sweep enters audio rate:
            // modulating a 1-5 ms delay at 90 Hz is no longer a sweep, it is FM, and the comb
            // grows sidebands. That is what "crazy at 100" means on a flanger.
            // The shape, not a flat multiplier: a flat one moves the DEFAULT too and swamps the
            // geometry that tells the Types apart (fb397 measured 12 gates red doing exactly
            // that). 0..0.6 is EXACTLY the shipped taper, sample for sample.
            const float t = clampf (p_.rate, 0.0f, 1.0f);
            if (t <= 0.60f) hz = 0.02f * std::pow (1000.0f, t);              // 0.02 → 1.262 Hz
            else
            {
                const float h60 = 0.02f * std::pow (1000.0f, 0.60f);         // 1.2619 Hz
                hz = h60 * std::pow (90.0f / h60, (t - 0.60f) * 2.5f);       // → 90 Hz
            }
        }

        if (c.flags & F_MATRIX) hz *= 0.10f;                 // the Filter Matrix freeze
        rateHz_ = hz;
        inc_    = hz / fs_;
        sawInc_ = hz / fs_;

        manT_ = 0.1f * std::pow (200.0f, clampf (p_.b1, 0.0f, 1.0f)) * c.baseMul;  // ms
        // Tape Zero re-reads the SAME knob as a bipolar Zero Bias. It gets its own
        // smoother: reading p_.b1 raw in the geometry would zipper the null's position.
        biaT_ = (clampf (p_.b1, 0.0f, 1.0f) - 0.5f) * 15.0f;                      // ms
        depT_ = clampf (p_.depth, 0.0f, 1.0f);
        // bipolar feedback, t^1.5 each side, per-Character ceiling
        {
            const float t = (clampf (p_.feedback, 0.0f, 1.0f) - 0.5f) * 2.0f;   // −1..+1
            const float a = std::pow (std::fabs (t), 1.5f) * c.fbCap;
            fbT_ = (t < 0.0f) ? -a : a;
        }
        sprT_ = clampf (p_.b2, 0.0f, 1.0f) * 0.5f;                     // 0..180° as turns
        widT_ = clampf (p_.b3, 0.0f, 1.0f) * 1.6f;
        shpT_ = clampf (clampf (p_.b5, 0.0f, 1.0f) + c.shapeAdd, 0.0f, 1.0f);
        bncT_ = clampf (p_.b6, 0.0f, 1.0f);
        mixT_ = clampf (p_.mix, 0.0f, 1.0f);
        dryT_ = std::cos (mixT_ * 1.5707963f);
        wetT_ = std::sin (mixT_ * 1.5707963f);

        // Damping - TWO corners off one knob, because a flanger's HF loss happens in
        // two places: once per pass through the delay line (the delay-path corner) and
        // again on every recirculation (the in-loop corner, which therefore compounds).
        {
            const float t = clampf (p_.b4, 0.0f, 1.0f);
            const float hzD = 20000.0f * std::pow (0.025f, t) * c.dampMul;      // 20 k -> 500 Hz
            const float hzL = 20000.0f * std::pow (0.060f, t) * c.dampMul;      // 20 k -> 1.2 kHz
            dmpT_  = onePole (clampf (hzD, 200.0f, 0.45f * fs_));
            dLagT_ = onePole (clampf (hzL, 250.0f, 0.45f * fs_));
        }
        // Low Cut: 20 Hz → 1 kHz, the house 20·50^t mapping
        {
            const float hzL = 20.0f * std::pow (50.0f, clampf (p_.b8, 0.0f, 1.0f));
            lowT_ = onePole (clampf (hzL, 5.0f, 4000.0f));
        }
        // Tail: 60 ms → 3 s, the gate release (Envelope Type: also the follower release)
        tailSec_ = 0.060f * std::pow (50.0f, clampf (p_.b7, 0.0f, 1.0f));
        gateRel_ = 1.0f - std::exp (-1.0f / (tailSec_ * fs_));

        // Envelope-Type follower attack: 60 ms → 1 ms as Rate rises ("how fast the
        // comb chases the playing"). Rate is never a dead knob on this Type.
        {
            const float atk = 0.060f * std::pow (1.0f / 60.0f, clampf (p_.rate, 0.0f, 1.0f)) * c.atkMul;
            envAtk_ = 1.0f - std::exp (-1.0f / (clampf (atk, 0.0002f, 0.5f) * fs_));
        }

        // comb mix depth: 1 everywhere except Endless, where Depth IS the notch depth
        combT_ = (type_ == Endless) ? (0.05f + 0.95f * depT_) : 1.0f;
        nrmT_  = 1.0f / std::sqrt (1.0f + combT_ * combT_);
        msInv_ = 1000.0f / fs_;
        // compander time constants, cooked ONCE per block (they were four std::exp per
        // sample per channel per call site - the whole of the BBD Type's CPU overrun).
        { const float tc = std::max (0.05f, c.atkMul);
          cpAtk_ = 1.0f - std::exp (-1.0f / (0.004f * tc * fs_));
          cpRel_ = 1.0f - std::exp (-1.0f / (0.120f * tc * fs_));
          exAtk_ = 1.0f - std::exp (-1.0f / (0.025f * tc * fs_));
          exRel_ = 1.0f - std::exp (-1.0f / (0.400f * tc * fs_)); }

        // Step: how many distinct comb positions (Shape remap), and the glide floor
        stepN_ = 2 + (int) std::lround (clampf (p_.b5, 0.0f, 1.0f) * 22.0f);
        {
            const float period = 1.0f / std::max (0.02f, rateHz_);
            float gl = std::max (0.005f, 0.15f * period);
            if (spec (type_, chr_).pat == 7) gl = std::max (0.005f, 0.60f * period);
            stepGl_ = 1.0f - std::exp (-1.0f / (gl * fs_));
        }

        maxD_ = (float) (mask_ - 8);
    }

    inline float onePole (float hz) const noexcept
    {
        if (hz <= 0.0f) return 0.0f;
        if (hz >= fs_ * 0.49f) return 1.0f;
        return 1.0f - std::exp (-6.2831853071795864f * hz / fs_);
    }

    void snapSmoothers() noexcept
    {
        manS_ = manT_; biaS_ = biaT_; depS_ = depT_; fbS_ = fbT_; sprS_ = sprT_; widS_ = widT_;
        dmpS_ = dmpT_; dLagS_ = dLagT_; lowS_ = lowT_; bncS_ = bncT_; shpS_ = shpT_; mixS_ = mixT_;
        dryS_ = dryT_; wetS_ = wetT_; nrmS_ = nrmT_;
        combS_ = combT_;
    }
    void snapDelays() noexcept { manS_ = manT_; biaS_ = biaT_; depS_ = depT_; combS_ = combT_; }

    void reseat() noexcept
    {
        ch_[0].clear(); ch_[1].clear();
        bs_ = 0.0f; bv_ = 0.0f;
        envF_[0] = envF_[1] = 0.0f; holdPk_ = 0.0f;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  THE MODULATOR — what drives the sweep. One master clock; L/R offsets are
    //  DERIVED at read time, never integrated separately (two accumulators "at the
    //  same rate" integrate their glide skew forever).
    // ═════════════════════════════════════════════════════════════════════════
    void modulator (const CharSpec& c, float inL, float inR, float& mL, float& mR) noexcept
    {
        if (type_ == Envelope)
        {
            const float relC = 1.0f - std::exp (-1.0f / (tailSec_ * fs_));
            const float rl = std::fabs (inL), rr = std::fabs (inR);
            const float aL = (c.flags & F_SPLIT_ENV) ? envAtk_ : envAtk_;
            const float aR = (c.flags & F_SPLIT_ENV) ? envAtk_ * 0.22f : envAtk_;
            envF_[0] += ((rl > envF_[0]) ? aL : relC) * (rl - envF_[0]);
            envF_[1] += ((rr > envF_[1]) ? aR : relC) * (rr - envF_[1]);
            envF_[0] = flush (envF_[0]); envF_[1] = flush (envF_[1]);

            float e0 = envF_[0], e1 = envF_[1];
            if (c.flags & F_HOLD)
            {   // peak-hold: the comb stays where the loudest hit put it
                const float pk = std::max (e0, e1);
                holdPk_ = std::max (pk, holdPk_ - holdPk_ * relC * 0.25f);
                e0 = e1 = holdPk_;
            }
            // knee AT the measured bus: −38 dBFS floor, −14 dBFS full sweep.
            mL = envMap (e0, c);
            mR = envMap (e1, c);
            if (! (c.flags & F_SPLIT_ENV)) mR = mL;
            return;
        }

        if (type_ == Step)
        {
            stepPh_ += inc_ * stepRateMul_;
            if (stepPh_ >= 1.0f)
            {
                stepPh_ -= 1.0f;
                advanceStep (c);
            }
            stepCur_  += stepGl_ * (stepTgt_  - stepCur_);
            stepCurR_ += stepGl_ * (stepTgtR_ - stepCurR_);
            mL = stepCur_;
            mR = (c.flags & F_COUNTER_LR) ? stepCurR_ : stepCur_;
            return;
        }

        // LFO Types (Tape Zero / Jet / BBD / Endless)
        float pL = ph_;
        float pR = ph_ + sprS_; if (pR >= 1.0f) pR -= 1.0f;
        float a = shapeMorph (pL, shpS_);
        float b = shapeMorph (pR, shpS_);

        if (type_ == TapeZero)
        {
            // servo BOUNCE: a damped spring on the sweep target. The overshoot on every
            // reversal is the Eventide FL-201 capstan model; blended in by Bounce so
            // Bounce 0 is genuinely OFF, not "a filter with a small coefficient".
            const float wn = 6.2831853f * 1.8f;
            const float z  = c.bounceZ - 0.30f * bncS_;
            const float acc = wn * wn * (a - bs_) - 2.0f * z * wn * bv_;
            bv_ += acc / fs_; bs_ += bv_ / fs_;
            bs_ = flush (bs_); bv_ = flush (bv_);
            a = a + bncS_ * (bs_ - a);
            b = b + bncS_ * (shapeMorph (pR, shpS_) - b);   // R keeps the un-bounced shape
        }

        mL = a; mR = b;
        if (c.flags & F_COUNTER_LR) mR = -mR;     // Wide Zero / Double Helix counter-sweep
    }

    inline float envMap (float e, const CharSpec& c) const noexcept
    {
        // −38 dBFS (0.0126) → 0, −14 dBFS (0.20) → 1, log in between; knee −26 dBFS.
        const float lo = 0.0126f, hi = 0.20f;
        const float v = clampf (std::log (std::max (e, 1.0e-7f) / lo) / std::log (hi / lo), 0.0f, 1.0f);
        const float shaped = std::pow (v, 0.35f + 2.6f * shpS_ * c.dwell);
        if (c.flags & F_DUCK_ZERO) return 1.0f - shaped;     // loud ⇒ Δ → 0 ⇒ cancel
        // direction is applied ONCE, in geometry(). Applying c.dir here TOO made
        // Envelope/Up and Envelope/Down BIT-IDENTICAL (double negation) - the harness
        // measured their Character distance as exactly 0.0 dB, which is how it surfaced.
        return shaped;
    }

    void advanceStep (const CharSpec& c) noexcept
    {
        const int N = std::max (2, stepN_);
        stepRateMul_ = 1.0f;
        switch (c.pat)
        {
            case 1: stepIdx_ = (stepIdx_ + 1) % N; break;                              // Stair Up
            case 2: stepIdx_ = (stepIdx_ + N - 1) % N; break;                          // Stair Down
            case 3: { stepIdx_ += stepDir_;                                            // Pendulum
                      if (stepIdx_ >= N - 1) { stepIdx_ = N - 1; stepDir_ = -1; }
                      if (stepIdx_ <= 0)     { stepIdx_ = 0;     stepDir_ = +1; } } break;
            case 4: { stepRng_ = stepRng_ * 1664525u + 1013904223u;                    // Ratchet
                      stepIdx_ = (int) ((stepRng_ >> 9) % (unsigned) N);
                      static const float mul[4] = { 0.5f, 0.5f, 1.0f, 2.0f };
                      stepK_ = (stepK_ + 1) & 3; stepRateMul_ = 1.0f / mul[stepK_]; } break;
            case 5: { stepRng_ = stepRng_ * 1664525u + 1013904223u;                    // Drunk
                      const int d = ((stepRng_ >> 13) & 1u) ? 1 : -1;
                      stepIdx_ += d;
                      if (stepIdx_ < 0) stepIdx_ = 1; if (stepIdx_ > N - 1) stepIdx_ = N - 2;
                      if (stepIdx_ < 0) stepIdx_ = 0; } break;
            default: { stepRng_ = stepRng_ * 1664525u + 1013904223u;                   // Random / Wide / Glide
                       stepIdx_ = (int) ((stepRng_ >> 9) % (unsigned) N); } break;
        }
        const float u = (N > 1) ? ((float) stepIdx_ / (float) (N - 1)) : 0.5f;
        stepTgt_ = (u * 2.0f - 1.0f);              // direction lives in `pat` + geometry()
        const int ir = (stepIdx_ + N / 2) % N;
        const float ur = (N > 1) ? ((float) ir / (float) (N - 1)) : 0.5f;
        stepTgtR_ = (ur * 2.0f - 1.0f);
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  GEOMETRY — modulator value → the actual read positions, in samples.
    // ═════════════════════════════════════════════════════════════════════════
    void geometry (const CharSpec& c, float mod, int chan, Geo& g) noexcept
    {
        const float msToS = 0.001f * fs_;
        Drift& dr = (chan == 0) ? driftL_ : driftR_;
        const float drift = dr.next() * bncS_ * c.driftMul;

        g.m = combS_;

        if (type_ == TapeZero || (c.flags & F_DUCK_ZERO))
        {
            // ── the two-deck through-zero machine ────────────────────────────
            //  Δ sweeps LINEARLY around 0 (an exponential sweep can never reach it),
            //  with a "zero dwell" exponent that decelerates the crossing — which is
            //  what a thumb on a reel physically does, and what turns the null from a
            //  sample-wide glitch into an audible hole.
            const float span = 7.5f * depS_ * c.spanMul;                    // ms
            float bias = (type_ == TapeZero) ? biaS_ : 0.0f;                     // Zero Bias, ms
            if (c.flags & F_BIASDRIFT) bias += biasDrift_.next() * 4.5f * bncS_;
            const float sgn = (mod < 0.0f) ? -1.0f : 1.0f;
            const float am = std::fabs (mod);
            const float shaped = (am > 1.0e-6f) ? sgn * fexp2 (c.dwell * flog2 (am)) : 0.0f;
            float dMs = softLim (bias + span * shaped + drift * 1.20f, 15.6f);

            float refMs = 8.0f;
            if (c.flags & F_COUNTER) refMs = 8.0f - 0.35f * span * shaped;  // the reel counter-sweeps
            g.ref  = std::max (2.0f, refMs * msToS);
            g.d1   = std::max (2.0f, (refMs + dMs) * msToS);
            g.w1   = 1.0f; g.d2 = g.d1; g.w2 = 0.0f;
            g.comb = g.d1 - g.ref;
        }
        else if (type_ == Endless)
        {
            // ── DAFx-15 synchronized dual comb ──────────────────────────────
            //  Two sawtooth-swept combs 180° apart, each windowed by a raised cosine
            //  that is ZERO at its own saw reset — the crossfade hides every wrap.
            //  Dmin = 0.55·Dmax is the paper's load-bearing rule; Depth scales the
            //  PAIR, never the ratio, so the trajectory stays monotonic at every Depth.
            const float Dmax = manS_ * (1.0f + 0.6f * depS_) * c.spanMul * msToS;
            const float Dmin = 0.55f * Dmax;
            // notch frequency = k/D, so the notches RISE when the delay FALLS.
            float p1 = (c.dir > 0) ? (1.0f - sawPh_) : sawPh_;
            float p2 = p1 + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
            if (chan == 1)
            {
                float o = sprS_;
                if (c.flags & F_COUNTER_LR) { p1 = 1.0f - p1; p2 = 1.0f - p2; }  // Double Helix
                p1 += o; if (p1 >= 1.0f) p1 -= 1.0f;
                p2 += o; if (p2 >= 1.0f) p2 -= 1.0f;
            }
            const float D1 = (Dmin + (Dmax - Dmin) * p1) * (1.0f + 0.02f * drift);
            const float D2 = (Dmin + (Dmax - Dmin) * p2) * (1.0f + 0.02f * drift);
            g.w1 = 0.5f - 0.5f * std::cos (6.2831853f * p1);
            g.w2 = 1.0f - g.w1;
            g.ref = 2.0f;
            g.d1 = std::max (2.0f, 2.0f + D1);
            g.d2 = std::max (2.0f, 2.0f + D2);
            if (c.flags & F_TWINTAP) { g.d2 = std::max (2.0f, 2.0f + D1 * c.tapRatio); g.w1 = 0.6f; g.w2 = 0.4f; }
            g.comb = g.w1 * D1 + g.w2 * (g.d2 - 2.0f);
        }
        else
        {
            // ── the single-deck exponential comb (Jet / BBD / Envelope / Step) ──
            //  Delay is swept in OCTAVES, not milliseconds: an exponential sweep reads
            //  as a linear pitch dive (the A/DA sound); a linear-ms sweep bunches all
            //  the motion at the short end. 2.66 octaves at Depth 100 = 2^5.32 ≈ 40:1,
            //  the A/DA ratio, against an industry norm of 20:1.
            const float oct = softLim (2.66f * depS_ * c.spanMul * mod * (float) c.dir + drift * 0.30f, 6.4f);
            float tMs = manS_ * fexp2 (oct);
            tMs = clampf (tMs, 0.045f, 42.0f);
            float D = tMs * msToS;
            g.ref = 2.0f;
            g.d1 = std::max (2.0f, 2.0f + D);
            g.w1 = 1.0f;
            if (c.flags & F_TWINTAP) { g.d2 = std::max (2.0f, 2.0f + D * c.tapRatio); g.w1 = 0.62f; g.w2 = 0.38f; }
            else { g.d2 = g.d1; g.w2 = 0.0f; }
            g.comb = D;
        }

        // the feedback tap: the comb spacing itself, so the resonant peaks land
        // exactly ON the feedforward series (k/Δ) instead of drifting off it.
        g.fb = clampf (std::fabs (g.comb), 0.30f * msToS, std::min (42.0f * msToS, maxD_));
        g.ref = clampf (g.ref, 2.0f, maxD_);
        g.d1  = clampf (g.d1,  2.0f, maxD_);
        g.d2  = clampf (g.d2,  2.0f, maxD_);
    }

    // ── the wet sum. ref ± m·lag, constant-power normalised in m so Depth never
    //    changes the through level on Endless.
    inline float deck (int chan, const Geo& g, const CharSpec& c) noexcept
    {
        const float* fbb = (chan == 0) ? bufL_.data() : bufR_.data();   // recirculating
        const float* dry = (chan == 0) ? dryL_.data() : dryR_.data();   // clean input line
        Chan& s = ch_[(size_t) chan];

        // THE REFERENCE DECK READS THE CLEAN LINE, NOT THE LOOP.
        //   If both decks read the recirculating buffer the transfer function is
        //   (1 + pol*e)/(1 - g*e): its ZERO and its POLE land on the SAME frequency
        //   when the feedback sign is negative, and they cancel - so flipping the
        //   Feedback polarity measures as a comb that FLATTENS instead of one whose
        //   geography MOVES. Reading ref off the clean line restores the classic
        //   a + b*e/(1 - g*e) and the polarity flip becomes a 40 dB event.
        //   (Measured: the first build of this file had ref on the loop; the
        //   +/-Feedback gate read +5.6 dB where it now reads -15 dB.)
        float ref = readAt (dry, g.ref);
        float lag = readAt (fbb, g.d1) * g.w1;
        if (g.w2 > 0.0f) lag += readAt (fbb, g.d2) * g.w2;

        if (c.flags & F_BBD)
        {
            // the bucket brigade's reconstruction filter, corner TRACKING the delay -
            // the BBD clock rate goes as 1/delay, so a long delay is a slow clock is a
            // dark output. DelayEngine.h:124's droop law, rescaled to flange times.
            const float ms = std::fabs (g.comb) * msInv_;
            const float t01 = clampf (flog2 (clampf (ms, 0.5f, 20.0f) * 2.0f) * 0.187936f, 0.0f, 1.0f);
            const float hz = 11000.0f * fexp2 (t01 * -2.874f) * c.reconMul;
            const float x = 6.2831853f * clampf (hz, 300.0f, 0.45f * fs_) / fs_;
            reconC_ = x / (1.0f + x);
            lag = s.recon (lag, reconC_);
            lag = s.expandOut (lag, c.pumpMul, exAtk_, exRel_);
        }

        // DAMPING on the DELAY PATH. A real flanger's delay line is band-limited and
        // its dry leg is not - so the knob eats the comb's HIGH teeth, which is what
        // makes high regeneration SING instead of fizz.
        // Tape Zero is TWO MATCHED DECKS: filtering only one of them would leave a
        // high-passed residue at Delta = 0 and destroy the null. Both, or neither.
        const bool matched = (type_ == TapeZero) || ((c.flags & F_DUCK_ZERO) != 0u);
        s.dLagZ += dLagS_ * (lag - s.dLagZ); s.dLagZ = flush (s.dLagZ); lag = s.dLagZ;
        if (matched) { s.dRefZ += dLagS_ * (ref - s.dRefZ); s.dRefZ = flush (s.dRefZ); ref = s.dRefZ; }

        float w = (ref + c.pol * g.m * lag) * nrmS_;

        // the BBD's output reconstruction - a gentler 2-pole at the same tracking
        // corner across the WHOLE wet, so the Type darkens as the comb lengthens.
        if (c.flags & F_BBD) w = s.recon2 (w, reconC_ * 0.55f);

        // wet-path low cut (the same corner as the loop's — flanged bass wobbles the
        // mix floor; Eventide put Low Cut on the Instant Flanger for exactly this).
        s.wLowZ += lowS_ * (w - s.wLowZ); s.wLowZ = flush (s.wLowZ);
        return w - s.wLowZ;
    }

    // ── the recirculation path. Every stage in the loop-gain ledger lives here.
    inline float loop (int chan, const Geo& g, const CharSpec& c) noexcept
    {
        const float* b = (chan == 0) ? bufL_.data() : bufR_.data();
        Chan& s = ch_[(size_t) chan];
        float v = readAt (b, g.fb);

        if (c.flags & F_BBD) v = s.expandOut (v, c.pumpMul * 0.5f, exAtk_, exRel_);

        s.dampZ += dmpS_ * (v - s.dampZ); s.dampZ = flush (s.dampZ); v = s.dampZ;   // LP
        s.lowZ  += lowS_ * (v - s.lowZ);  s.lowZ  = flush (s.lowZ);  v -= s.lowZ;   // HP
        // in-loop DC blocker — asymmetric program + regeneration integrates a DC
        // pedestal, and the soft clip then rectifies it. One state per channel.
        const float r = 1.0f - (6.2831853f * 5.0f / fs_);
        const float y = v - s.dcX + r * s.dcY;
        s.dcX = v; s.dcY = flush (y);
        const float k = 0.70f / c.clipK;
        const float drv = k * k;                       // Silver 1.0 ... Screamer 4.8
        return softClip (y * drv, c.clipK) / drv;
    }

    // ═════════════════════════════════════════════════════════════════════════
    Params p_{};
    Viz    viz_{};

    float fs_ = 48000.0f;
    std::vector<float> bufL_, bufR_, dryL_, dryR_;
    int   mask_ = 0, wr_ = 0;

    Chan  ch_[2];
    Drift driftL_, driftR_, biasDrift_;

    int   type_ = 0, chr_ = 0, pendType_ = -1, pendChr_ = -1;
    bool  primed_ = false;
    float dip_ = 1.0f;

    // targets (block) / smoothed (per sample)
    float manT_ = 1.4f,  manS_ = 1.4f;
    float biaT_ = 0.0f,  biaS_ = 0.0f;
    float depT_ = 0.55f, depS_ = 0.55f;
    float fbT_  = 0.0f,  fbS_  = 0.0f;
    float sprT_ = 0.17f, sprS_ = 0.17f;
    float widT_ = 1.0f,  widS_ = 1.0f;
    float dmpT_ = 0.5f,  dmpS_ = 0.5f;
    float dLagT_ = 0.8f, dLagS_ = 0.8f;
    float reconC_ = 0.5f;
    float lowT_ = 0.01f, lowS_ = 0.01f;
    float bncT_ = 0.2f,  bncS_ = 0.2f;
    float shpT_ = 0.25f, shpS_ = 0.25f;
    float mixT_ = 0.5f,  mixS_ = 0.5f;
    float dryT_ = 0.707f, dryS_ = 0.707f, wetT_ = 0.707f, wetS_ = 0.707f;
    float nrmT_ = 0.707f, nrmS_ = 0.707f, msInv_ = 0.0208f;
    float cpAtk_ = 0.05f, cpRel_ = 0.002f, exAtk_ = 0.01f, exRel_ = 0.001f;
    float combT_= 1.0f,  combS_= 1.0f;

    float rateHz_ = 0.35f, inc_ = 0.0f, sawInc_ = 0.0f;
    float ph_ = 0.0f, sawPh_ = 0.0f;
    float bs_ = 0.0f, bv_ = 0.0f;
    float envIn_ = 0.0f, gate_ = 0.0f, lvlSm_ = 0.0f;
    float envF_[2] { 0.0f, 0.0f };
    float holdPk_ = 0.0f;
    float envAtk_ = 0.01f, gateRel_ = 0.001f, tailSec_ = 0.3f;
    float maxD_ = 1024.0f;

    int   stepN_ = 8, stepIdx_ = 0, stepK_ = 0, stepDir_ = 1;
    float stepPh_ = 0.0f, stepCur_ = 0.0f, stepTgt_ = 0.0f;
    float stepCurR_ = 0.0f, stepTgtR_ = 0.0f, stepGl_ = 0.01f, stepRateMul_ = 1.0f;
    uint32_t stepRng_ = 0x2545F491u;

    float kSm_ = 0.001f, kFast_ = 0.01f, dipDn_ = 0.01f, dipUp_ = 0.001f;

    // +2.5 dB. The two-deck sum averages −3 dB broadband against the input (the comb
    // is |cos(ωΔ/2)|, mean power 0.5); this puts unity-through back inside ±1 dB at
    // the default patch. Measured, not guessed — flanger_cert §A.
    static constexpr float kWetTrim_ = 1.0f;
};

} // namespace tw
