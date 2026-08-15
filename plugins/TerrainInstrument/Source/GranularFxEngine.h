#pragma once
// ════════════════════════════════════════════════════════════════════════════
//  tw::GranularFxEngine — fb362. The front-page granular, moved to the BACK of
//  the FX rack.
//
//  Max, 2026-08-15: "you don't have to make a whole new granular engine. You can
//  take what's already on the front, the grain engine, and just put it in the
//  back… you could write new DSP if it suits the back of the effect rack."
//  That is exactly this file: tw::GranularEngine's kernel is recycled almost
//  line-for-line (spawner, windows, RMS match, norm glide, skew LUT, key tables,
//  equal-power pan, Hermite, compact active list, grain budget), and the ONE
//  thing the back of the rack actually needs is new — ring-relative addressing.
//
//  🔑 THE ONE REPRESENTATION CHANGE (GRANULAR-FX-BUILD-BIBLE §3.2)
//  The oscillator engine reads a STATIC sample: readPos is an absolute index into
//  a buffer whose length never moves, and its edge reads clamp. An FX granular
//  reads a LIVE ring that has no edges — it has a write head, and reading across
//  that head is the one true glitch. So the addressing is rewritten:
//
//      write:  ring[w & mask] = (in + fb·wet)·(1−freeze) + ring[w & mask]·freeze;  ++w
//      window: an AGE band [winLo, winHi] behind w — "the freshest N seconds"
//      grain:  an ABSOLUTE slice [lo, hi], converted from that band ONCE at spawn
//      step:   pos += ratio            (the writer's +1 makes the age move at 1 − ratio)
//      read:   hermite(pos)            (mask-wrapped, no clamps)
//
//  🔑 Why grains are ABSOLUTE and the window is an AGE. The window has to be an age
//  or "the last 2 seconds" would stop meaning that as the music plays. A GRAIN must
//  not be, and the harness proved it: under freeze the write head keeps advancing, so
//  a fixed age is a MOVING address — the whole cloud sweeps off the frozen material
//  within one window-span and ends up grazing silence. Converting once at spawn gives
//  both: a live window that tracks the present, and grains that hold what they caught.
//
//  Everything else about a grain — window, pan, key snap, birth jitter, budget — is
//  the osc engine's code with the position substituted.
//
//  🚨 THE THREE TRAPS THIS FILE EXISTS TO NOT FALL INTO
//  1. CATCH-UP. A pitched-UP forward grain (ratio r>1) closes on the write head at
//     (r−1) per sample. It must die before it arrives. Handled structurally: the
//     playable window is clamped into a SAFE BAND [kSafeMargin, N−kSafeMargin] and
//     grains REFLECT at their own latched bounds, so no read can reach the head at
//     any point in its life — stronger than a birth-time-only guard.
//  2. THE ±24 st CLAMP IS A NEW LINE, NOT INHERITED. The osc engine never clamps
//     `semis`: Pitch(±24) + randKeyOffset(up to +35) + spray(±24) legally reaches
//     ≈ +83 st, ratio ≈ 121×. It survives that only because it reads a static
//     buffer with clamped edges. On a ring, r=121 makes the guard unsatisfiable.
//     clampSemis() below is that missing line — do not remove it.
//  3. THE FREEZE SEAM. Freeze does NOT stop the addresses moving: a grain holding a
//     constant age reads w−age while w still advances, so the read sweeps through
//     the frozen ring and meets the seam at w₀ (newest sample butted against oldest)
//     — a full-scale jump mid-window where the envelope is ~1, i.e. a wideband click
//     roughly once per ring period. The age-bounds + reflection law is the fix; the
//     head FOLDS inside [ageLo,ageHi] and grains reflect, so every crossing is a
//     value-continuous slope corner instead of a value jump. Same reasoning that
//     killed the "fire crackle" on the osc side (GranularEngine.h:362-367).
//
//  Freeze is a write-BLEND, not a gate (GrainEngine.h:112-115 law) — an amount with
//  a glide, so partial freeze is regenerative smear and freeze=1 is a bit-exact
//  identity write. Feedback lives INSIDE the blend: writing `+ fb·wet` beside it
//  would make freeze=1 an unbounded accumulator.
//
//  Offline proof loop (Pattern A — NOT in CMakeLists):
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/GranularFxEngine_test.cpp -o /tmp/gfx && /tmp/gfx
// ════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>

namespace tw {

// ── Types (header pill). Each is a spawner + pitch + read policy on ONE kernel. ──
enum GrnType { GrnCloud = 0, GrnRise, GrnSwarm, GrnSuspend, GrnScatter, GrnRewind, GrnStretch, GrnPulverize };
// ── Character (back dropdown 1). Physics on the ring write/read path, never EQ. ──
enum GrnChar { GrnClean = 0, GrnTape, GrnCassette, GrnRadio, GrnWorn, GrnDrift };

struct GranularFxParams
{
    // front heroes
    float density  = 0.40f;  // 0..1 → 1..220 g/s log (Swarm ×2)
    float size     = 0.25f;  // 0..1 → 2..900 ms log
    float pitch    = 0.f;    // ±24 st base transpose
    // back panel, 4×2
    float scan     = 0.f;    // −1..+1 head rate through the window (0 = hold = live granulizer)
    float windowMs = 1000.f; // 50..16000 ms of the freshest past that is playable
    float spray    = 0.10f;  // 0..1 birth-age chaos (Scatter: skip/repeat probability)
    float detune   = 0.f;    // 0..1 → ±24 st per-grain scatter (key-snapped when Key on)
    float shape    = 0.5f;   // 0..1 Flat-top → Hann → Bell
    float width    = 0.5f;   // 0..1 per-grain equal-power pan spread
    float feedback = 0.f;    // 0..1.10 wet re-entry into the ring
    float freeze   = 0.f;    // 0..1 write-blend
    // dropdowns / pills
    int   type      = GrnCloud;
    int   character = GrnClean;
    int   key       = 0;     // 0=Off 1=Oct 2=5th 3=Chord 4=Maj 5=Min 6=Penta
    bool  freezeLatch = false;   // front pill — latched infinite hold (Reverb Freeze precedent)

    bool operator== (const GranularFxParams& o) const noexcept
    {
        return density == o.density && size == o.size && pitch == o.pitch && scan == o.scan
            && windowMs == o.windowMs && spray == o.spray && detune == o.detune && shape == o.shape
            && width == o.width && feedback == o.feedback && freeze == o.freeze
            && type == o.type && character == o.character && key == o.key
            && freezeLatch == o.freezeLatch;
    }
};

class GranularFxEngine
{
public:
    GranularFxEngine() = default;

    // ── setup ────────────────────────────────────────────────────────────────
    // ⚠️ ALLOCATES. Message thread only (the processor builds these lazily, exactly like the
    // fb352 reverb engines: the audio thread publishes a request and reads a pointer).
    void prepare (double sampleRate) noexcept
    {
        const double fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        // RollingCaptureBuffer.h:27-30 idiom — a DAW may call prepareToPlay repeatedly; do NOT
        // wipe captured audio when the rate is unchanged, or a re-prepare mid-session empties
        // the archive under a frozen cloud.
        const bool rateChanged = (fs != outRate_) || ringL_.empty();
        outRate_ = fs;

        normAlpha_ = 1.f - std::exp (-1.f / (0.003f * (float) outRate_));   // ~3 ms (osc parity)
        fastAlpha_ = 1.f - std::exp (-1.f / (0.010f * (float) outRate_));   // ~10 ms — freeze/feedback
        slowAlpha_ = 1.f - std::exp (-1.f / (0.015f * (float) outRate_));   // ~15 ms — scan/window
        envAtk_    = 1.f - std::exp (-1.f / (0.005f * (float) outRate_));
        envRel_    = 1.f - std::exp (-1.f / (0.250f * (float) outRate_));
        gateAtk_   = 1.f - std::exp (-1.f / (0.003f * (float) outRate_));
        gateRel_   = 1.f - std::exp (-1.f / (2.000f * (float) outRate_));   // the natural tail
        (void) windows();

        if (rateChanged)
        {
            size_t want = (size_t) std::ceil (kRingSeconds * outRate_);
            size_t n = 1; while (n < want) n <<= 1;          // mask addressing (DelayEngine idiom)
            ringL_.assign (n, 0.f);
            ringR_.assign (n, 0.f);
            mask_ = n - 1;
            w_ = 0;
            resetPool();
            ageHead_ = 0.0;
        }
        recomputeDerived();
        snapSmoothing();
    }

    bool isPrepared() const noexcept { return ! ringL_.empty(); }

    // Instance-wide live-grain budget, shared with every other granular surface (osc + FX).
    // Audio-thread-only mutation, no atomics — same contract as tw::GranularEngine.
    void setGrainBudget (int* used, int cap) noexcept { budgetUsed_ = used; budgetCap_ = cap; }

    // Scatter's tempo clock, resolved by the processor from the host BPM (Hz).
    void setSyncClockHz (float hz) noexcept { clockHz_ = hz > 0.01f ? hz : 0.01f; }

    void setParams (const GranularFxParams& p) noexcept { p_ = p; recomputeDerived(); }

    // Full stop — a type/character swap fades out and calls this, then re-seats (Phase G law).
    void reset() noexcept
    {
        if (! ringL_.empty()) { std::fill (ringL_.begin(), ringL_.end(), 0.f);
                                std::fill (ringR_.begin(), ringR_.end(), 0.f); }
        w_ = 0; ageHead_ = 0.0; countdown_ = 0.0; frozenOfs_ = 0.0; readOffset_ = 0.0;
        fbL_ = fbR_ = 0.f; dcL_ = dcR_ = 0.f; tailGate_ = 0.f;
        wowPh_ = flutPh_ = crackPh_ = 0.f;
        bpZ1L_ = bpZ2L_ = bpZ1R_ = bpZ2R_ = 0.f;
        hbL_ = hbR_ = 0.f;
        inEnv_ = 0.f;
        srHoldL_ = srHoldR_ = 0.f; srPhase_ = 0;
        resetPool();
        snapSmoothing();
    }

    // ── the one hot call: one stereo frame in, the WET frame out ──────────────
    // The caller owns Mix (equal-power ramp in the processor, exactly like the reverb/delay),
    // so this returns pure wet and the dry path never routes through the ring.
    void processSample (float inL, float inR, float& outL, float& outR) noexcept
    {
        outL = outR = 0.f;
        if (ringL_.empty()) return;

        // ── 0) smoothed controls. Every one of these is a knob the user can yank, and a raw
        //      per-block step on any of them is audible on a sustained cloud (the fb204 lesson).
        freezeSm_ += fastAlpha_ * (freezeTgt_ - freezeSm_);
        fbSm_     += fastAlpha_ * (p_.feedback - fbSm_);
        scanSm_   += slowAlpha_ * (p_.scan - scanSm_);
        winLoSm_  += slowAlpha_ * (winLoTgt_ - winLoSm_);
        winHiSm_  += slowAlpha_ * (winHiTgt_ - winHiSm_);
        normSm_   += normAlpha_ * (norm_ - normSm_);
        shapeSm_  += normAlpha_ * (p_.shape - shapeSm_);

        // input envelope — law 6 ("nothing free-runs"): every regenerating path is gated by it.
        const float inAbs = 0.5f * (std::fabs (inL) + std::fabs (inR));
        inEnv_ += (inAbs > inEnv_ ? envAtk_ : envRel_) * (inAbs - inEnv_);

        // ── 1) WRITE the live bus into the ring ──────────────────────────────
        // Feedback INSIDE the blend (§3.2): `(in + fb·wet)·(1−f) + ring·f`. Writing `+ fb·wet`
        // beside the blend makes freeze=1 an accumulator with gain 1+fb and no bound at all.
        float wrL = inL + fbSm_ * fbL_;
        float wrR = inR + fbSm_ * fbR_;
        writeCharacter (wrL, wrR);                       // Tape/Cassette/Radio/Pulverize physics
        const size_t wi = (size_t) (w_ & mask_);
        const float  f  = freezeSm_;
        ringL_[wi] = wrL * (1.f - f) + ringL_[wi] * f;
        ringR_[wi] = wrR * (1.f - f) + ringR_[wi] * f;
        ++w_;

        // 🔑 THE FROZEN ANCHOR. Everything that converts an AGE into an address (the window, the
        // scan head, every new grain's birth) does it through this offset, so a freeze holds the
        // material instead of sliding off it: at freeze = 1 the offset grows exactly as fast as
        // the write head, which pins the effective head still. Live (freeze = 0) it does not move
        // and the behaviour is the ordinary "N seconds behind the present". Partial freeze
        // interpolates, which is the regenerative smear the write-blend is there for.
        // ⚠️ The anchor engages only near a FULL freeze, and that nonlinearity is the whole point.
        // Driving it linearly from f (the first version) made a PARTIAL freeze slide the read
        // window into the past at (1 − f) per sample — a time machine, not a smear — and the
        // harness caught it as a silent Suspend: within a couple of seconds the window had walked
        // back past the beginning of recorded time and was grazing untouched ring.
        // The physics: at f < 1 every ring position still takes new audio, so the material at a
        // given AGE stays current (a blend of new and old — exactly the regenerative smear the
        // write-blend exists for) and the window must NOT move. Only as f → 1 does content at a
        // fixed age stop being refreshed, and only then does the anchor need to hold it.
        const double anchorRate = (double) (f - 0.9f) * 10.0;   // 0 below f=0.9, ramps to 1 at f=1
        frozenOfs_ += anchorRate < 0.0 ? 0.0 : (anchorRate > 1.0 ? 1.0 : anchorRate);
        // A freeze held longer than the ring must LOOP the archive rather than walk into the seam.
        // Grains already in flight are untouched (their bounds are absolute); only new births move,
        // and every grain is windowed to zero at both ends, so the loop point cannot click.
        const double ofsMax = (double) (mask_ + 1) - 2.0 * kSafeMargin - winSpan() - kMaxGrainLen;
        if (frozenOfs_ > ofsMax || frozenOfs_ < 0.0) frozenOfs_ = 0.0;

        // ── 2) the scan head, in AGE space ───────────────────────────────────
        // scan = 0 → age constant: the head keeps pace with the writer, i.e. a live granulizer
        // reading a fixed distance behind the present. scan > 0 races toward the present,
        // scan < 0 dives into the past. This is the osc engine's Scan semantics exactly
        // (there, 0 = the head stops moving through the sample = the "living freeze").
        double headRate = -(double) scanSm_ * 2.0;                     // ±200 %, osc parity
        if (p_.type == GrnStretch) headRate /= (double) headDiv_;      // Paulstretch head slowdown
        ageHead_ += headRate;
        foldHead();

        // Tape / Cassette WOW + FLUTTER — a coherent wobble of the whole read position (every
        // grain shifts together, which is what a slipping transport actually does). Applied as a
        // read-age offset, never by moving the grains' own bounds: their reflection band must stay
        // exactly where it was latched or the safety invariant stops holding.
        if (p_.character == GrnTape || p_.character == GrnCassette)
        {
            const float wowMul = (p_.character == GrnCassette) ? 2.f : 1.f;   // cassette wows twice as hard
            wowPh_  += 0.4f  / (float) outRate_; if (wowPh_  >= 1.f) wowPh_  -= 1.f;   // 0.4 Hz
            flutPh_ += 6.0f  / (float) outRate_; if (flutPh_ >= 1.f) flutPh_ -= 1.f;   // 6 Hz
            const double wow  = std::sin (2.f * kPi * wowPh_)  * 0.0015 * wowMul;      // ±0.15 %
            const double flut = std::sin (2.f * kPi * flutPh_) * 0.0005;               // ±0.05 %
            // as a fraction of the window span, so the wobble is musical at every Window setting
            readOffset_ = (wow + flut) * winSpan() * 0.5;
        }
        else readOffset_ = 0.0;

        // ── 3) spawner ───────────────────────────────────────────────────────
        countdown_ -= 1.0;
        while (countdown_ <= 0.0)
        {
            spawnGrain();
            double interval;
            if (p_.type == GrnScatter)
            {
                // Tempo clock, not a Poisson cloud — onsets land ON the grid (the measurable tell).
                // Density picks the DIVISION of that clock rather than a free rate, so the knob
                // still evolves 0→100 (law 5) without ever leaving the grid.
                const int div = 1 << (int) (clamp01 (p_.density) * 4.99f);   // 1,2,4,8,16
                interval = (double) outRate_ / ((double) clockHz_ * (double) div);
                countdown_ += interval;                     // NO jitter: the grid is the identity
            }
            else
            {
                interval = (double) outRate_ / (double) densHz_;
                const double jit = 0.5 * interval * (double) (nextRand01() * 2.f - 1.f);
                countdown_ += interval + jit;               // async sowing (Clouds)
            }
            if (countdown_ < 1.0) countdown_ = 1.0;
        }

        // ── 4) grain mix + retire — compact active list (osc engine, verbatim) ─
        float accL = 0.f, accR = 0.f;
        for (int j = 0; j < numActive_; )
        {
            Grain& g = pool_[(size_t) activeIdx_[j]];
            const float ph = (float) g.age / (float) g.len;
            const float win = windowAt (shapeSm_, ph);
            float sl, sr; readRing (g.readPos, sl, sr);
            accL += win * g.panL * sl;
            accR += win * g.panR * sr;

            g.readPos += g.posInc;
            g.readPos += g.drift;                 // Drift character — a WALK, not per-sample noise
            ++g.age;
            reflectPos (g);                       // value-continuous; never a teleport
            if (g.age >= g.len)
            {
                g.active = false;
                if (budgetUsed_ != nullptr) --(*budgetUsed_);
                freeIdx_[numFree_++] = activeIdx_[j];
                activeIdx_[j] = activeIdx_[--numActive_];
                continue;
            }
            ++j;
        }

        // ── 5) 1/√overlap normalization, glided (the unity-gain law) ─────────
        float oL = accL * normSm_;
        float oR = accR * normSm_;

        readCharacter (oL, oR);                   // Worn dropouts / Cassette head-bump / Radio AM

        // ── per-type level trim (MEASURED, not guessed) ──────────────────────
        // 1/√overlap normalises for grain DENSITY, but not for what each Type does on top of it:
        // Rewind's long reversed grains re-read overlapping material coherently and sum loud,
        // while Suspend's blended archive and Scatter's gridded spawner sit well below. Untrimmed
        // the roster spanned 22 dB, which fails the "types are night-and-day, not louder-and-
        // quieter" bar — a level difference reads as better/worse, not as different. Values come
        // from the harness's own per-type RMS, targeting ≈ 0.075 across the board.
        {
            static const float trim[8] = { 1.00f, 0.95f, 0.72f, 1.85f, 1.55f, 0.42f, 0.70f, 1.25f };
            const float tr = trim[(p_.type >= 0 && p_.type < 8) ? p_.type : 0];
            oL *= tr; oR *= tr;
        }

        // ── 6) DC block. Every regenerating path in this plugin is AC-coupled (the Phase G
        //      DC-latch silence class): a grain cloud with feedback can otherwise walk a DC
        //      offset into the ring and latch the whole device silent.
        const float hpL = oL - dcL_; dcL_ += 0.0008f * hpL;
        const float hpR = oR - dcR_; dcR_ += 0.0008f * hpR;
        oL = hpL; oR = hpR;

        // ── 7) safety: NaN guard + soft clip (GrainEngine.h:198-199 law) ─────
        if (! std::isfinite (oL)) { oL = 0.f; dcL_ = 0.f; }
        if (! std::isfinite (oR)) { oR = 0.f; dcR_ = 0.f; }
        oL = softClip (oL); oR = softClip (oR);

        // ── LAW 6: NOTHING FREE-RUNS ────────────────────────────────────────
        // A granular archive will happily graze a buffer forever after the input stops — the
        // harness measured it still ringing at −42 dBFS eight seconds into silence. So the wet
        // output (and the feedback tap) ride a follower with a fast attack and a ~2 s release:
        // long enough that the cloud keeps its natural tail and never gates between notes, short
        // enough that silence really does mean silence. The front Freeze PILL is the one
        // exemption — the same latched-hold consent the Reverb's Freeze pill already has.
        const float tgt = (inEnv_ > 3.0e-4f) ? 1.f : 0.f;
        tailGate_ += ((tgt > tailGate_) ? gateAtk_ : gateRel_) * (tgt - tailGate_);
        const float gate = p_.freezeLatch ? 1.f : tailGate_;
        oL *= gate; oR *= gate;

        fbL_ = oL; fbR_ = oR;
        outL = oL; outR = oR;
    }

    // ── viz: what the card draws (all cheap reads, no allocation) ────────────
    float  scanAge01()   const noexcept { return winSpan() > 1.0 ? (float) ((ageHead_ - winLoSm_) / winSpan()) : 0.f; }
    int    liveGrains()  const noexcept { return numActive_; }
    float  inputEnv()    const noexcept { return inEnv_; }
    // One ring sample at a given age, for the capture-waveform display.
    float  peekAge (double age) const noexcept
    {
        if (ringL_.empty()) return 0.f;
        const int64_t idx = (int64_t) w_ - (int64_t) age;
        const size_t  i   = (size_t) ((uint64_t) idx & (uint64_t) mask_);
        return 0.5f * (ringL_[i] + ringR_[i]);
    }
    double windowLoAge() const noexcept { return winLoSm_; }
    double windowHiAge() const noexcept { return winHiSm_; }
    double ringSamples() const noexcept { return (double) (mask_ + 1); }

    // ── offline harness accessors ────────────────────────────────────────────
    int    activeForTesting()  const noexcept { return numActive_; }
    double headAgeForTesting() const noexcept { return ageHead_; }
    double safeMarginForTesting() const noexcept { return kSafeMargin; }
    // The invariant the whole design rests on: no live grain may read across the write head.
    bool   allGrainsSafeForTesting() const noexcept
    {
        for (int j = 0; j < numActive_; ++j)
        {
            const Grain& g = pool_[(size_t) activeIdx_[j]];
            const double age = (double) w_ - g.readPos;   // how far behind the writer this read is
            if (age < kSafeMargin) return false;                          // about to be overwritten
            if (age > (double) (mask_ + 1) - kSafeMargin) return false;   // wrapped past the seam
        }
        return true;
    }

private:
    static constexpr int    kPool          = 64;
    static constexpr int    kWin           = 2048;
    static constexpr float  kPi            = 3.14159265358979f;
    static constexpr double kRingSeconds   = 16.5;   // 4-bar parity with the Delay's 16 s ceiling
    // Hermite reads 4 taps; a block may advance the writer by its whole length before the next
    // grain update. 4096 covers the largest sane host block plus the taps plus slack.
    static constexpr double kSafeMargin    = 4096.0;
    // Grain length is BOUNDED so recomputeDerived can reserve headroom for it. A grain reads a
    // fixed absolute slice, and the writer advances under it for the grain's whole life — so the
    // window's oldest edge must sit at least one max-grain-length clear of the ring's far end.
    static constexpr double kMaxGrainLen   = 96000.0;   // 2 s @ 48 k

    struct Grain
    {
        // 🔑 ABSOLUTE ring position, not an age. The first draft stored an age and the harness
        // caught why that is wrong: under freeze the write head KEEPS ADVANCING, so a fixed age
        // is a MOVING address — after one window-span the whole cloud has swept off the frozen
        // material and is grazing whatever lies ahead (silence on a cold ring, 16.5 s-old audio
        // on a warm one). A frozen grain must hold its SLICE, so the slice is what it stores.
        // Live behaviour is unchanged: a grain advancing at `ratio` while the writer advances at
        // 1 falls behind at exactly (1 − ratio) per sample, which is the age formulation written
        // the other way round.
        double readPos = 0.0;    // absolute (mask-wrapped on access)
        double posInc  = 0.0;    // = ratio, signed (negative = reverse)
        double lo = 0.0, hi = 0.0;   // absolute bounds, latched at spawn — reflect here
        double drift   = 0.0;    // Drift character: a slow per-grain walk
        int    age = 0, len = 0;
        float  panL = 0.f, panR = 0.f;
        bool   active = false;
    };

    // ── ring read: 4-point Hermite, mask-wrapped, NO clamps (a ring has no edges) ──
    void readRing (double absPos, float& l, float& r) const noexcept
    {
        // The wow/flutter offset is clamped to a quarter of the safety margin so a tape wobble
        // can never spend the headroom that keeps every read behind the write head.
        const double lim = kSafeMargin * 0.25;
        const double off = readOffset_ < -lim ? -lim : (readOffset_ > lim ? lim : readOffset_);
        const double pos = absPos - off;
        const int64_t i1 = (int64_t) std::floor (pos);
        const float   fr = (float) (pos - (double) i1);
        const uint64_t m = (uint64_t) mask_;
        const size_t a = (size_t) ((uint64_t) (i1 - 1) & m);
        const size_t b = (size_t) ((uint64_t) (i1    ) & m);
        const size_t c = (size_t) ((uint64_t) (i1 + 1) & m);
        const size_t d = (size_t) ((uint64_t) (i1 + 2) & m);
        l = hermite (ringL_[a], ringL_[b], ringL_[c], ringL_[d], fr);
        r = hermite (ringR_[a], ringR_[b], ringR_[c], ringR_[d], fr);
    }

    static inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c = (x1 - xm1) * 0.5f, v = x0 - x1, w = c + v, a = w + v + (x2 - x0) * 0.5f, bn = w + a;
        return ((((a * t) - bn) * t + c) * t + x0);
    }

    // Reflect in AGE space at the grain's own latched bounds — GranularEngine::reflectAtBounds
    // with `age` substituted for `readPos`. Value-continuous: the slope flips (a corner, inaudible
    // at a random phase inside a window) where a position JUMP would be a click.
    static inline void reflectPos (Grain& g) noexcept
    {
        if (g.readPos > g.hi)
        {
            g.readPos = g.hi - (g.readPos - g.hi);
            g.posInc  = -g.posInc;
            g.drift   = -g.drift;
            if (g.readPos < g.lo) g.readPos = g.lo;      // degenerate span: pin
        }
        else if (g.readPos < g.lo)
        {
            g.readPos = g.lo + (g.lo - g.readPos);
            g.posInc  = -g.posInc;
            g.drift   = -g.drift;
            if (g.readPos > g.hi) g.readPos = g.hi;
        }
    }

    double winSpan() const noexcept { return winHiSm_ - winLoSm_; }

    // The head folds INSIDE the window (the :496-499 one-step idiom) instead of clamping, so a
    // scanning head loops the window seamlessly rather than parking on an edge.
    void foldHead() noexcept
    {
        const double lo = winLoSm_, hi = winHiSm_, span = hi - lo;
        if (span < 1.0) { ageHead_ = lo; return; }
        while (ageHead_ >  hi) ageHead_ -= span;
        while (ageHead_ <  lo) ageHead_ += span;
    }

    // 🚨 THE CLAMP THAT DID NOT COME OVER FROM THE OSC ENGINE. See the header note.
    static inline double clampSemis (double s) noexcept { return s < -24.0 ? -24.0 : (s > 24.0 ? 24.0 : s); }

    void spawnGrain() noexcept
    {
        if (numFree_ <= 0) return;                                          // pool full: skip-and-wait
        if (budgetUsed_ != nullptr && *budgetUsed_ >= budgetCap_) return;   // instance-wide budget full

        // Scatter's probability gate: Spray becomes skip/repeat chance, so the grid breathes.
        if (p_.type == GrnScatter && nextRand01() < p_.spray * 0.5f) return;

        const int slot = freeIdx_[--numFree_];
        Grain& g = pool_[(size_t) slot];

        // ── birth: pick an AGE (head + sprayed jitter, inside the window), then convert it ONCE
        //    into an absolute slice. frozenOfs_ is what makes a freeze hold: it pushes the whole
        //    band back in step with the writer, so "the window" keeps meaning the same audio.
        double sprayAmt = (p_.type == GrnScatter) ? 0.0 : (double) clamp01 (p_.spray);
        double birthAge = ageHead_ + sprayAmt * (double) (nextRand01() * 2.f - 1.f) * 0.5 * winSpan();
        double loAge = winLoSm_, hiAge = winHiSm_;
        if (hiAge < loAge + 2.0) hiAge = loAge + 2.0;
        if (birthAge < loAge) birthAge = loAge;
        if (birthAge > hiAge) birthAge = hiAge;
        loAge += frozenOfs_; hiAge += frozenOfs_; birthAge += frozenOfs_;

        g.readPos = (double) w_ - birthAge;
        // absolute bounds — OWNED for life. A later Window move (or a freeze) cannot teleport a
        // grain in flight; it simply windows out inside the slice it was born in.
        g.lo = (double) w_ - hiAge;
        g.hi = (double) w_ - loAge;

        // ── per-grain pitch ──
        double semis = (double) p_.pitch;
        switch (p_.type)
        {
            case GrnRise:
                // every generation climbs: force an in-key upward draw even at Detune 0
                semis += randUpOffset (p_.key > 0 ? p_.key : 1);
                break;
            case GrnSwarm:
                // UNIFORM ±4 st, deliberately NOT key-snapped — that spread is the swarm
                semis += (double) (nextRand01() * 2.f - 1.f) * 4.0;
                break;
            default:
                if (p_.key > 0) semis += randKeyOffset (p_.key);
                break;
        }
        semis += (double) p_.detune * (double) (nextRand01() * 2.f - 1.f) * 24.0;
        if (p_.key > 0 && p_.type != GrnSwarm) semis = snapToKey (semis, p_.key);
        semis = clampSemis (semis);                       // ← trap 2. Never remove.
        double ratio = std::pow (2.0, semis / 12.0);

        // ── direction ──
        bool rev = false;
        if (p_.type == GrnRewind)       rev = true;                       // all reversed
        else if (p_.type == GrnScatter) rev = (nextRand01() > 0.75f);     // coin-flip per hit
        if (rev) ratio = -ratio;
        g.posInc = ratio;         // absolute step; the writer's +1 makes the age move at (1 − ratio)

        // ── length ──
        double len = (double) grainLenSamp_;
        if (p_.type == GrnSwarm)   len = len < 96.0 ? 96.0 : (len > 0.060 * outRate_ ? 0.060 * outRate_ : len);
        if (p_.type == GrnRewind)  len = len < 0.100 * outRate_ ? 0.100 * outRate_ : len;
        if (p_.type == GrnStretch) len *= 2.0;            // long Bell grains, high overlap
        if (len < 8.0) len = 8.0;
        if (len > kMaxGrainLen) len = kMaxGrainLen;   // the bound recomputeDerived reserves for
        // A grain must not outlive its own span: cap length so reflection has room to work.
        const double maxLen = (g.hi - g.lo) * 4.0;
        if (len > maxLen && maxLen > 8.0) len = maxLen;
        g.len = (int) len;
        g.age = 0;

        // ── Drift character: a slow per-grain age WALK (Phase G Worn-walk law — a walk, not a
        //    per-sample noise smoother). ±30 ms over the grain's life.
        g.drift = (p_.character == GrnDrift)
                ? (double) (nextRand01() * 2.f - 1.f) * (0.030 * outRate_) / (double) g.len
                : 0.0;

        // ── equal-power pan from Width ──
        float wdt = clamp01 (p_.width);
        if (p_.type == GrnSwarm) wdt = 1.f;               // full scatter field
        const float pan = wdt * (nextRand01() * 2.f - 1.f);
        const float a   = (pan * 0.5f + 0.5f) * 1.5707963f;
        g.panL = std::cos (a);
        g.panR = std::sin (a);

        g.active = true;
        activeIdx_[numActive_++] = slot;
        if (budgetUsed_ != nullptr) ++(*budgetUsed_);
    }

    // ── Character: physics on the WRITE path ─────────────────────────────────
    void writeCharacter (float& l, float& r) noexcept
    {
        // 🚨 Pulverize is a TYPE, not a Character. Driving it from the character switch below was
        // a real bug the harness caught: GrnPulverize (=7 in GrnType) is not a legal GrnChar value
        // (those stop at 5), so the case was unreachable and Pulverize came out bit-identical to
        // Cloud. It stacks WITH whatever Character is selected, which is the point of two axes.
        if (p_.type == GrnPulverize)
        {
            l = muLaw8 (l); r = muLaw8 (r);
            if (++srPhase_ >= 2) { srPhase_ = 0; srHoldL_ = l; srHoldR_ = r; }   // ÷2 SR images
            l = srHoldL_; r = srHoldR_;
            const float k = 0.5f;
            l = std::tanh (l * (1.f / k)) * k;
            r = std::tanh (r * (1.f / k)) * k;
        }

        switch (p_.character)
        {
            case GrnTape:
            {
                const float k = 0.35f;                    // tanh at ≈ −8 dB rel. program
                l = std::tanh (l * (1.f / k)) * k;
                r = std::tanh (r * (1.f / k)) * k;
                break;
            }
            case GrnCassette:
                l = muLaw8 (l); r = muLaw8 (r);           // Clouds' 8-bit µ-law ring
                break;
            case GrnRadio:
            {
                // 2nd-order band-pass 300 Hz–3.4 kHz, written INTO the ring (not an output EQ)
                bpZ1L_ += bpA_ * (l - bpZ1L_);  const float hl = l - bpZ1L_;
                bpZ2L_ += bpB_ * (hl - bpZ2L_); l = bpZ2L_;
                bpZ1R_ += bpA_ * (r - bpZ1R_);  const float hr = r - bpZ1R_;
                bpZ2R_ += bpB_ * (hr - bpZ2R_); r = bpZ2R_;
                break;
            }
            default: break;                               // Clean / Worn / Drift touch the read side
        }
    }

    // ── Character: physics on the READ path ──────────────────────────────────
    void readCharacter (float& l, float& r) noexcept
    {
        switch (p_.character)
        {
            case GrnCassette:
            {
                // head bump: +2 dB shelf around 80 Hz
                hbL_ += hbA_ * (l - hbL_); l += 0.26f * hbL_;
                hbR_ += hbA_ * (r - hbR_); r += 0.26f * hbR_;
                break;
            }
            case GrnRadio:
            {
                // AM crackle, GATED by the input envelope (law 6 — it cannot free-run)
                crackPh_ += 1.f;
                if (crackPh_ > (float) outRate_ * 0.013f)
                {
                    crackPh_ = 0.f;
                    crackle_ = (nextRand01() < 0.30f) ? (nextRand01() * 0.8f) : 0.f;
                }
                const float g = 1.f - crackle_ * (inEnv_ > 0.002f ? 1.f : inEnv_ * 500.f);
                l *= g; r *= g;
                break;
            }
            case GrnWorn:
            {
                // GrainEngine's Wander V3 dice at a fixed i = 0.35 — dropouts + micro-stutter.
                if (--wornCount_ <= 0)
                {
                    wornCount_ = (int) (outRate_ * 0.020);
                    wornGain_  = (nextRand01() < 0.20f * 0.35f) ? 0.f : 1.f;
                }
                l *= wornGain_; r *= wornGain_;
                break;
            }
            default: break;
        }
    }

    // µ-law 8-bit companding — the Clouds "Fairlight" law. Signal-correlated ≈ −48 dB floor.
    static inline float muLaw8 (float x) noexcept
    {
        const float mu = 255.f;
        const float s  = x < 0.f ? -1.f : 1.f;
        const float a  = std::fabs (x) > 1.f ? 1.f : std::fabs (x);
        float y = s * std::log (1.f + mu * a) / std::log (1.f + mu);   // compress
        y = std::round (y * 128.f) / 128.f;                             // 8-bit quantize
        const float ya = std::fabs (y);
        return (y < 0.f ? -1.f : 1.f) * (std::pow (1.f + mu, ya) - 1.f) / mu;   // expand
    }

    static inline float softClip (float x) noexcept { return std::tanh (x); }

    void recomputeDerived() noexcept
    {
        const float d = clamp01 (p_.density);
        densHz_ = std::pow (220.f, d);
        if (p_.type == GrnSwarm) densHz_ *= 2.f;                   // up to 440 g/s
        headDiv_ = 1.f + clamp01 (p_.size) * 3.f;                  // Stretch head divisor

        const float s = clamp01 (p_.size);
        float glenSec = 0.002f * std::pow (450.f, s);              // 2..900 ms (engine truth)
        grainLenSamp_ = glenSec * (float) outRate_;

        const float ov = densHz_ * glenSec;
        norm_ = 1.f / std::sqrt (ov < 1.f ? 1.f : ov);

        // Window → a SAFE age band. Clamped so no grain can ever read across the write head:
        // this is what makes the catch-up guard structural rather than a birth-time test.
        const double ringN = (double) (mask_ + 1);
        double span = (double) p_.windowMs * 0.001 * outRate_;
        const double maxSpan = ringN - 2.0 * kSafeMargin - kMaxGrainLen;
        if (span > maxSpan) span = maxSpan;
        if (span < 64.0)    span = 64.0;
        winLoTgt_ = kSafeMargin;
        winHiTgt_ = kSafeMargin + span;

        // Suspend forces the write-blend up: it IS the type's identity.
        freezeTgt_ = clamp01 (p_.freeze);
        if (p_.freezeLatch)          freezeTgt_ = 1.f;
        else if (p_.type == GrnSuspend) freezeTgt_ = freezeTgt_ < 0.60f ? 0.60f : freezeTgt_;
        // ⚠️ 0.60, not the 0.85 this started at. Each ring position is only rewritten ONCE per ring
        // period (16.5 s), so the blend's fill rate is per-PERIOD, not per-sample: at 0.85 the
        // archive needed roughly a minute to reach full level and the type measured 22 dB below the
        // others. 0.60 fills in about a third of that while still reading as a held, smeared texture.

        // Radio / Cassette filter coefficients (block-rate, no per-sample transcendentals).
        bpA_ = 1.f - std::exp (-2.f * kPi * 300.f  / (float) outRate_);
        bpB_ = 1.f - std::exp (-2.f * kPi * 3400.f / (float) outRate_);
        hbA_ = 1.f - std::exp (-2.f * kPi * 80.f   / (float) outRate_);
    }

    void snapSmoothing() noexcept
    {
        normSm_ = norm_; shapeSm_ = p_.shape; scanSm_ = p_.scan;
        freezeSm_ = freezeTgt_; fbSm_ = p_.feedback;
        winLoSm_ = winLoTgt_; winHiSm_ = winHiTgt_;
        ageHead_ = winLoTgt_ + 0.5 * (winHiTgt_ - winLoTgt_);
    }

    void resetPool() noexcept
    {
        if (budgetUsed_ != nullptr)
        {
            *budgetUsed_ -= numActive_;
            if (*budgetUsed_ < 0) *budgetUsed_ = 0;
        }
        for (auto& g : pool_) g.active = false;
        numActive_ = 0;
        numFree_   = kPool;
        for (int i = 0; i < kPool; ++i) freeIdx_[i] = i;
    }

    // ── windows: identical to the osc engine (RMS-matched so Shape morphs TIMBRE, not level;
    //    zero at both ends so a grain birth/death physically cannot click). Shared statics.
    struct Windows
    {
        std::array<float, kWin> flat {}, hann {}, bell {};
        Windows() noexcept
        {
            double sf = 0, sh = 0, sb = 0;
            for (int i = 0; i < kWin; ++i)
            {
                const float x = (float) i / (float) (kWin - 1);
                const float h = 0.5f - 0.5f * std::cos (2.f * kPi * x);
                const float alpha = 0.12f;
                float fl;
                if (x < alpha * 0.5f)            fl = 0.5f * (1.f + std::cos (kPi * (2.f * x / alpha - 1.f)));
                else if (x > 1.f - alpha * 0.5f) fl = 0.5f * (1.f + std::cos (kPi * (2.f * x / alpha - 2.f / alpha + 1.f)));
                else                             fl = 1.f;
                const float dd = (x - 0.5f) / 0.16f;
                const float b  = std::exp (-0.5f * dd * dd) * h;
                flat[i] = fl; hann[i] = h; bell[i] = b;
                sf += (double) fl * fl; sh += (double) h * h; sb += (double) b * b;
            }
            const float rf = (sf > 1e-9) ? (float) std::sqrt (sh / sf) : 1.f;
            const float rb = (sb > 1e-9) ? (float) std::sqrt (sh / sb) : 1.f;
            for (int i = 0; i < kWin; ++i) { flat[i] *= rf; bell[i] *= rb; }
        }
    };
    static const Windows& windows() noexcept { static const Windows w; return w; }

    float windowAt (float shape01, float phase01) const noexcept
    {
        const float ph = phase01 < 0.f ? 0.f : (phase01 > 1.f ? 1.f : phase01);
        const int idx = (int) (ph * (float) (kWin - 1));
        const Windows& W = windows();
        float s = shape01 < 0.f ? 0.f : (shape01 > 1.f ? 1.f : shape01);
        if (p_.type == GrnStretch) s = s < 0.75f ? 0.75f : s;   // long Bell grains
        if (s < 0.5f) { const float t = s * 2.f;          return W.flat[idx] + (W.hann[idx] - W.flat[idx]) * t; }
        else          { const float t = (s - 0.5f) * 2.f; return W.hann[idx] + (W.bell[idx] - W.hann[idx]) * t; }
    }

    inline float nextRand01() noexcept
    {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return (float) ((rng_ & 0x00FFFFFFu) / (double) 0x01000000);
    }
    static inline float clamp01 (float x) noexcept { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    // ── key quantize (osc engine, verbatim) ──
    static double snapToKey (double semis, int key) noexcept
    {
        if (key <= 0) return semis;
        const int* tbl; int n; keyTable (key, tbl, n);
        const double oc = std::floor (semis / 12.0);
        const double pc = semis - oc * 12.0;
        double best = tbl[0], bd = 1.0e9;
        for (int i = 0; i < n; ++i) { const double d = std::fabs (pc - (double) tbl[i]); if (d < bd) { bd = d; best = (double) tbl[i]; } }
        const double dWrap = std::fabs (pc - 12.0); if (dWrap < bd) best = 12.0;
        return oc * 12.0 + best;
    }
    static void keyTable (int key, const int*& tbl, int& n) noexcept
    {
        static const int oct[]   = { 0 };
        static const int fifth[] = { 0, 7 };
        static const int chord[] = { 0, 4, 7 };
        static const int maj[]   = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minr[]  = { 0, 2, 3, 5, 7, 8, 10 };
        static const int pent[]  = { 0, 2, 4, 7, 9 };
        switch (key)
        {
            case 1:  tbl = oct;   n = 1; break;
            case 2:  tbl = fifth; n = 2; break;
            case 3:  tbl = chord; n = 3; break;
            case 4:  tbl = maj;   n = 7; break;
            case 5:  tbl = minr;  n = 7; break;
            default: tbl = pent;  n = 5; break;
        }
    }
    double randKeyOffset (int key) noexcept
    {
        const int* tbl; int n; keyTable (key, tbl, n);
        const int deg = tbl[(int) (nextRand01() * (float) n) % (n > 0 ? n : 1)];
        static const int octWeighted[] = { -12, 0, 0, 0, 12, 12, 12, 24 };
        const int oc = octWeighted[(int) (nextRand01() * 8.f) & 7];
        return (double) (oc + deg);
    }
    // Rise: the same draw but never downward — that is what makes every feedback pass climb.
    double randUpOffset (int key) noexcept
    {
        const int* tbl; int n; keyTable (key, tbl, n);
        const int deg = tbl[(int) (nextRand01() * (float) n) % (n > 0 ? n : 1)];
        static const int octUp[] = { 0, 12, 12, 12, 12, 24, 7, 12 };
        const int oc = octUp[(int) (nextRand01() * 8.f) & 7];
        return (double) (oc + deg);
    }

    // ── state ────────────────────────────────────────────────────────────────
    std::vector<float> ringL_, ringR_;
    size_t   mask_ = 0;
    uint64_t w_    = 0;              // write head, monotonic (mask-wrapped on access)

    std::array<Grain, kPool> pool_ {};
    int activeIdx_[kPool] {};
    int freeIdx_[kPool] {};
    int numActive_ = 0, numFree_ = 0;

    double outRate_   = 48000.0;
    double ageHead_   = 0.0;
    double countdown_ = 0.0;
    uint32_t rng_ = 0x9E3779B9u;

    float densHz_ = 8.f, grainLenSamp_ = 480.f, norm_ = 1.f, headDiv_ = 1.f;
    float clockHz_ = 4.f;

    // smoothing
    float normAlpha_ = 0.007f, fastAlpha_ = 0.002f, slowAlpha_ = 0.0013f;
    float envAtk_ = 0.004f, envRel_ = 0.0001f;
    float normSm_ = 1.f, shapeSm_ = 0.5f, scanSm_ = 0.f, freezeSm_ = 0.f, fbSm_ = 0.f;
    double winLoSm_ = kSafeMargin, winHiSm_ = kSafeMargin + 48000.0;
    double winLoTgt_ = kSafeMargin, winHiTgt_ = kSafeMargin + 48000.0;
    float freezeTgt_ = 0.f;

    // character + safety state
    float fbL_ = 0.f, fbR_ = 0.f;
    float dcL_ = 0.f, dcR_ = 0.f;
    float inEnv_ = 0.f;
    float tailGate_ = 0.f, gateAtk_ = 0.006f, gateRel_ = 1.0e-5f;
    float wowPh_ = 0.f, flutPh_ = 0.f, crackPh_ = 0.f, crackle_ = 0.f;
    double readOffset_ = 0.0;    // Tape/Cassette wow+flutter, in samples (clamped in readRing)
    double frozenOfs_  = 0.0;    // the frozen anchor — see the note beside the ring write
    float bpZ1L_ = 0.f, bpZ2L_ = 0.f, bpZ1R_ = 0.f, bpZ2R_ = 0.f, bpA_ = 0.03f, bpB_ = 0.3f;
    float hbL_ = 0.f, hbR_ = 0.f, hbA_ = 0.01f;
    float srHoldL_ = 0.f, srHoldR_ = 0.f; int srPhase_ = 0;
    int   wornCount_ = 0; float wornGain_ = 1.f;

    int*  budgetUsed_ = nullptr;
    int   budgetCap_  = 0;

    GranularFxParams p_ {};
};

} // namespace tw
