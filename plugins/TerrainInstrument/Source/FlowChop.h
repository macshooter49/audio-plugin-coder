#pragma once
// =============================================================================
//  FlowChop.h  —  Terrain Instrument · FLOW mode CHOP   (replaces the SEQ slot)
//  Waves Crate
//
//  Header-only, NO JUCE. RT-safe in process() (allocation only in prepare()).
//  Sibling of FlowArp.h — reuses its rate ladder, ArpRng, clamps, quantizers.
//
//  WHAT IT IS
//  CHOP is a performance mode (sibling to the ARP). It chops the synth's OWN
//  live output into grid-locked slices and re-grooves them in real time —
//  reordered, repitched IN KEY, reversed, rolled — like an MPC/Dilla flip that
//  plays itself. It is NOT a looper (the tape engine owns record/overdub) and
//  NOT a destructive FX (that's GLITCH). It rearranges; you ride the macros.
//
//  TWO SUB-MODES (right-click "glass" menu on the mode badge):
//    • ALWAYS-ON : continuously re-grooves the trailing output, like an instrument
//    • CATCH     : only flips while a key/trigger is held (punch-in performance)
//
//  ── THE NON-NEGOTIABLE: ZERO CLICKS ──────────────────────────────────────────
//  The "silent light-switch" principle, generalized. Every slice plays through a
//  voice in a small POOL; each voice has a COSINE (Hann-edged) envelope whose
//  attack starts at 0 and release ends at 0 with ZERO SLOPE at both ends (C1
//  continuous). Transitions OVERLAP: the outgoing voice releases while the
//  incoming voice attacks. Buffer reads are CONTINUOUS (read forward/back through
//  the ring — never loop a slice, so there is no loop seam to click). Out-of-range
//  reads clamp to the newest written sample (a flat hold, not a jump). Result:
//  a hard discontinuity is mathematically impossible. Proven offline by asserting
//  max |out[n]-out[n-1]| stays tiny under the most aggressive settings.
//
//  ── VARISPEED (the hard part, done right) ─────────────────────────────────────
//  Repitch = resampling: read the buffer at increment = pitch_ratio = 2^(semi/12).
//  Pitch and length couple (the tape/MPC sound). We DON'T fight that — we read
//  CONTINUOUSLY through the buffer for the whole gate (so up-pitched slices pull
//  in newer audio instead of looping/gapping, down-pitched slices stretch), and
//  the step grid is honored by gating each voice to the step with the cosine
//  release. Repitch is QUANTIZED to the held key so flips stay musical.
//  Interpolation: 4-point cubic Hermite (Catmull-Rom) — the sampler standard.
//
//  KNOBS (effective = base + LFO mod, summed upstream):
//    RATE → slice grid (ladder)   GATE → slice length / space (staccato↔legato)
//    VARY → flip intensity: déjà-vu re-roll + reverse/pitch probability
//    TRAJ → flip STYLE {Forward, Reverse, Shuffle, PingPong, StutterRoll, GrainSpray}
//    MORPH→ swing + pitch-spice (in-key repitch range)
//
//  Card depth (extension panel): slices, loop length, scale/key, déjà-vu lock,
//  per-style params, ratchet/roll, pitch range, mix, DIRT (future). Most controls
//  live there — front shows the 5 macros + the glass mode menu.
//
//  Build test: g++ -std=c++17 Source/FlowChop_test.cpp -o /tmp/fc && /tmp/fc
// =============================================================================

#include "FlowArp.h"
#include <vector>
#include <cmath>

namespace wc
{

enum class ChopMode  : int { AlwaysOn = 0, Catch };
enum class ChopStyle : int { Forward = 0, Reverse, Shuffle, PingPong, StutterRoll, GrainSpray };
static constexpr int kChopStyleN  = 6;
static constexpr int kChopVoices  = 8;     // slice-player pool (overlap headroom)
static constexpr int kChopMaxLoop = 32;    // max pattern length (slices in a loop)

inline ChopStyle chopStyle (float traj) noexcept { return (ChopStyle) arpQuantIdx (traj, kChopStyleN); }

class FlowChop
{
public:
    FlowChop() { rng_.seed (0xC40F5EEDu); }

    // ── lifecycle ───────────────────────────────────────────────────────────────
    void prepare (double sampleRate, double captureSeconds = 4.0) noexcept
    {
        sr_  = (sampleRate > 0.0 ? sampleRate : 44100.0);
        cap_ = (int) std::ceil (sr_ * (captureSeconds > 0.5 ? captureSeconds : 0.5));
        if (cap_ < 1024) cap_ = 1024;
        bufL_.assign ((size_t) cap_, 0.0f);
        bufR_.assign ((size_t) cap_, 0.0f);
        fade_ = (int) std::lround (sr_ * 0.0015);          // 1.5 ms cosine edge
        if (fade_ < 2) fade_ = 2;
        reset();
    }

    void reset() noexcept
    {
        wAbs_ = 0; written_ = 0;
        for (auto& v : voice_) v = Voice{};
        cur_ = -1;
        wetEnv_ = 0.f; catchHeld_ = false;
        haveClock_ = false; nextStep_ = 0; nextBoundary_ = 0.0; freePpq_ = 0.0;
        subRemain_ = 0; subTimer_ = 0; subPitchAccum_ = 0.f;
        for (int i = 0; i < kChopMaxLoop; ++i) { backValid_[i] = false; }
        rng_.seed (seed_ ? seed_ : 0xC40F5EEDu);
        regenPattern_ = true;
        // fb106 runtime (config ext_/extOn_ persists — it mirrors the card)
        revRunLeft_ = revCool_ = 0; subLvl_ = 1.f; pendDrop_ = false; subsFired_ = 0;
        colEnv_ = 0.f; spAcc_ = 0.0; lastRate_ = 1.0; wanderCur_ = 0.f;
        lpL_ = lpR_ = hpL_ = hpR_ = 0.f; wowPh_ = 0.f;
        fireCount_ = 0; vizStepF_ = 0.f; scanBackSamp_ = 0.0;
    }

    // ── configuration (card → engine) ───────────────────────────────────────────
    void setMode (ChopMode m) noexcept { mode_ = m; }
    void setCatchHeld (bool held) noexcept { catchHeld_ = held; }          // CATCH trigger (key/footswitch)
    void setSlices (int n) noexcept { len_ = arpClampi (n, 2, kChopMaxLoop); regenPattern_ = true; }
    void setLoopLen (int n) noexcept { len_ = arpClampi (n, 2, kChopMaxLoop); regenPattern_ = true; }
    void setMix (float wet) noexcept { mix_ = arpClamp01 (wet); }
    void setScale (int rootMidi, uint16_t mask) noexcept { scaleRoot_ = arpClampi (rootMidi, 0, 127); scaleMask_ = mask; buildScale(); }
    void setSeed (uint32_t s) noexcept { seed_ = s; }
    void setDejavu (float d) noexcept { dejavu_ = arpClamp01 (d); }        // 0=re-roll every loop, 1=locked
    void setPitchRangeDeg (int deg) noexcept { pitchRangeDeg_ = arpClampi (deg, 0, 14); }
    void setReverseProb (float p) noexcept { revProb_ = arpClamp01 (p); }
    void setRatchet (int r) noexcept { ratchet_ = arpClampi (r, 1, 16); }   // sub-triggers per step (rolls)
    void setStyleOverride (int s) noexcept { styleOv_ = s; }                 // -1 = follow TRAJ
    void setHostTrack (bool on) noexcept { keyTrack_ = on; }
    void noteOnRoot (int midi) noexcept { lastRoot_ = arpClampi (midi, 0, 127); }

    bool  isActive() const noexcept { return wetEnv_ > 0.001f; }
    int   lastSliceIndex() const noexcept { return lastSlice_; }            // for UI / test
    ChopStyle activeStyle() const noexcept { return curStyle_; }

    // ── extension card (fb106): every Ribbon-card control in one struct. ────
    // setExt() flips the engine into card mode; until then every path below is
    // bit-exact legacy (the offline click-proofs run against that).
    struct ChopExtParams
    {
        int   slices = 8, loopCells = 8;   // window: loopCells memory cells cut into `slices` pieces
        float scan = 1.0f;                 // window position in the 16-cell memory (1 = Now)
        float wander = 0.0f;               // window drift per pattern
        float spread = 0.0f;               // per-slice source scatter
        float speed = 0.0f;                // memory stretch (decimated capture, pitch-compensated)
        bool  freeze = false, collect = false;
        int   rpts = 2;                    // window repeats before a free-seed re-roll
        int   modeOrder = 0;               // 0 Step · 1 Ping · 2 Rand · 3 Walk (style override)
        float oSpread=.5f, oBias=.5f, oLock=0.f, oSeed=.44f;                 // ORDER
        float pRange=.33f, pSteps=0.f, pGlide=.15f, pQuant=.6f;              // PITCH (odds off by default)
        float rvOdds=0.f, rvRun=.25f, rvSpread=0.f, rvSnap=.6f;              // REV (odds off by default)
        float tLen=.875f, tCurve=.3f, tRand=0.f, tGate=0.f;                  // TRIM (×1.0 neutral)
        float rCount=.33f, rDecay=.4f, rCurve=.5f, rOdds=0.f;                // REPEAT (odds off by default)
        float dAmt=0.f, dSize=.4f, dSpray=.1f, dTone=.5f;                    // DROP (off by default)
        float steps=0.f, detune=0.f, wow=0.f, smooth=.15f;                   // CHARACTER (neutral; smooth .15 = fade ×1.0)
        int   filter = 0;                  // 0 Off · 1 Low · 2 Mid · 3 High (wet bus)
        float grit=0.f, trim=.5f;          // BUS
    };
    void setExt (const ChopExtParams& x) noexcept
    { ext_ = x; extOn_ = true; len_ = arpClampi (x.slices, 2, kChopMaxLoop); }

    // instant memory clear — RT-safe: an empty written window makes every read
    // clamp to the newest sample (flat hold ≈ silence). No memset on the audio thread.
    void wipe() noexcept { written_ = 0; }

    // viz feed (audio thread writes, processor copies to atomics after process)
    float    vizStepF()     const noexcept { return vizStepF_; }   // slice index + frac in the loop
    uint32_t vizFireCount() const noexcept { return fireCount_; }
    float    wetLevel()     const noexcept { return wetEnv_; }

    // ── main: process the synth output in place ───────────────────────────────────
    void process (float rate, float gate, float vary, float traj, float morph,
                  double hostPpq, double bpm, double sampleRate,
                  float* L, float* R, int numSamples, bool playing) noexcept
    {
        if (cap_ <= 0 || numSamples <= 0) return;
        const double SR  = (sampleRate > 0.0 ? sampleRate : sr_);
        const double BP  = (bpm > 0.0 ? bpm : 120.0);
        const double pps = (BP / 60.0) / SR;
        const float  cellBeats = arpBeatsPerStepRich (rate);
        // fb109 — TIME IS TRUTHFUL (Max: "1/16 must chop at 1/16"): the audible
        // slice grid is ALWAYS the Time division. Slices = pattern length (variety
        // before the flip repeats); Loop = how far apart source picks jump in the
        // memory (pad size = loop/slices cells); Scan = how deep. fb106's
        // grid-scaling made Time lie by loop/slices — the "can't speed it up" bug.
        const float  beats = cellBeats;
        const double stepSamp = (double) beats / pps;
        const double cellSamp = (double) cellBeats / pps;
        padSamp_ = extOn_ ? ((double) arpClampi (ext_.loopCells, 2, 16) / (double) len_) * cellSamp
                          : stepSamp;
        const double sw = (double) arpClamp01 (morph > 0.9f ? 0.9f : morph) * ((double) beats * 0.5);

        // window back-offset: Scan positions the loop window in the 16-cell memory
        // (1 = Now = legacy anchor); Wander re-rolls a drift each pattern regen.
        scanBackSamp_ = 0.0;
        if (extOn_)
        {
            const float cellsBack = arpClamp01 (1.0f - ext_.scan) * (float) (16 - arpClampi (ext_.loopCells, 2, 16));
            float drift = wanderCur_ * (float) (16 - arpClampi (ext_.loopCells, 2, 16)) * 0.5f;
            float total = cellsBack + drift;
            if (total < 0.f) total = 0.f;
            if (total > 15.f) total = 15.f;
            scanBackSamp_ = (double) total * cellSamp;
            // per-block hoisted wet-bus + character coefficients
            crushLv_  = (ext_.steps > 0.02f) ? std::pow (2.0f, 16.0f - ext_.steps * 11.0f) : 0.f;
            gritDrv_  = 1.0f + ext_.grit * 6.0f;
            gritNorm_ = 1.0f / std::tanh (gritDrv_);
            trimGain_ = 0.5f + arpClamp01 (ext_.trim);                       // .5x .. 1.5x, unity at .5
            fadeEff_  = (int) std::lround ((float) fade_ * (0.5f + arpClamp01 (ext_.smooth) * 3.5f));
            if (fadeEff_ < 2) fadeEff_ = 2;
            const float lpHz = 900.0f, hpHz = 300.0f;
            lpC_ = 1.0f - std::exp (-2.0f * 3.14159265f * lpHz / (float) SR);
            hpC_ = 1.0f - std::exp (-2.0f * 3.14159265f * hpHz / (float) SR);
            wowInc_ = 2.0f * 3.14159265f * 0.7f / (float) SR;                // 0.7 Hz tape wow
        }

        curStyle_ = (extOn_)
            ? ((traj > 0.004f) ? chopStyle (traj)                            // TRAJ macro engaged → its 6-style ladder
                               : kOrderStyle[arpClampi (ext_.modeOrder, 0, 3)])
            : ((styleOv_ >= 0 && styleOv_ < kChopStyleN) ? (ChopStyle) styleOv_ : chopStyle (traj));
        const float gFrac   = arpClamp01 (gate < 0.05f ? 0.05f : gate);
        const float pitchAmt = arpClamp01 (morph);                          // MORPH adds pitch spice
        const float flipAmt  = arpClamp01 (vary);

        double p = playing ? hostPpq : freePpq_;
        // Re-anchor the step clock to the transport whenever the grid changes (RATE knob) or the
        // step counter drifts out of a sane window (transport jump). A raw step counter is only
        // valid for the `beats` it was built with: moving RATE UP makes the next boundary fall far
        // BEHIND p (the boundary loop rapid-fires, guard-capped → glitch/silence at 1/256); moving
        // RATE DOWN makes it fall far AHEAD of p (no boundary ever fires → dead/silent, can't
        // recover). Re-anchoring keeps the grid locked to ppq across any rate move.
        {
            const double    curFloor = std::floor (p / (double) beats);
            const long long curStep  = (long long) curFloor;
            if (! haveClock_ || beats != lastBeats_ || nextStep_ > curStep + 2 || nextStep_ < curStep - 2)
            {
                nextStep_     = curStep;
                nextBoundary_ = boundaryTime (nextStep_, beats, sw);
                lastBeats_    = beats;
                haveClock_    = true;
            }
        }

        // target wet gate: ALWAYS-ON open whenever playing; CATCH open only while held
        const bool wantWet = (mode_ == ChopMode::AlwaysOn) ? true : catchHeld_;

        for (int i = 0; i < numSamples; ++i, p += pps)
        {
            const float dryL = L[i], dryR = R[i];
            // record dry into ring (absolute index) — fb106: Freeze stops the tape,
            // Collect only keeps audible moments, Speed stretches (decimated capture,
            // playback pitch-compensated via rateComp_). Skipped samples do NOT
            // advance wAbs_ so slice math stays anchored to what memory holds.
            bool wr = true;
            if (extOn_)
            {
                if (ext_.freeze) wr = false;
                if (wr && ext_.collect)
                {
                    const float a = std::fabs (dryL) + std::fabs (dryR);
                    colEnv_ = (a > colEnv_) ? a : colEnv_ * 0.9995f;
                    wr = colEnv_ > 0.02f;
                }
                if (wr && ext_.speed > 0.01f)
                {
                    spAcc_ += 1.0 / (1.0 + (double) ext_.speed * 3.0);
                    if (spAcc_ >= 1.0) spAcc_ -= 1.0; else wr = false;
                }
            }
            if (wr)
            {
                bufL_[(size_t) (wAbs_ % cap_)] = dryL;
                bufR_[(size_t) (wAbs_ % cap_)] = dryR;
            }

            // step boundaries (swung) → schedule slice triggers
            int guard = 0;
            while (p >= nextBoundary_ && guard++ < 64)
            {
                onBoundary (nextStep_, stepSamp, gFrac, flipAmt, pitchAmt);
                ++nextStep_;
                nextBoundary_ = boundaryTime (nextStep_, beats, sw);
            }

            // sub-triggers (ratchets / StutterRoll rolls) inside a step
            if (subRemain_ > 0)
            {
                if (--subTimer_ <= 0)
                {
                    subPitchAccum_ += subPitchStep_;
                    if (extOn_) subLvl_ *= 1.0f - 0.6f * arpClamp01 (ext_.rDecay);   // rolls fade out
                    triggerVoice (pendBack_, pendRev_, pendPitch_ + subPitchAccum_, pendGate_, stepSamp, subLvl_, pendDrop_);
                    --subRemain_;
                    ++subsFired_;
                    int iv = subInterval_;
                    if (extOn_)
                    {   // Roll Curve — accelerando / ritardando spacing
                        const float f = std::pow (2.0f, (arpClamp01 (ext_.rCurve) - 0.5f) * 1.2f);
                        iv = (int) std::lround ((double) subInterval_ * std::pow ((double) f, (double) subsFired_));
                        if (iv < 1) iv = 1;
                    }
                    subTimer_ = iv;
                }
            }

            // render voice pool (sum) — each voice cosine-enveloped, continuous read
            float wetL = 0.f, wetR = 0.f;
            float wowMul = 1.0f;
            if (extOn_ && ext_.wow > 0.01f)
            {
                wowPh_ += wowInc_; if (wowPh_ > 6.2831853f) wowPh_ -= 6.2831853f;
                wowMul = 1.0f + ext_.wow * 0.012f * std::sin (wowPh_);
            }
            for (int v = 0; v < kChopVoices; ++v) if (voice_[v].active) renderVoice (voice_[v], wetL, wetR, wowMul);

            // fb106 wet bus: Steps (bit-step) → Grit (drive) → Filter → Trim
            if (extOn_)
            {
                if (crushLv_ > 0.f)
                { wetL = std::round (wetL * crushLv_) / crushLv_; wetR = std::round (wetR * crushLv_) / crushLv_; }
                if (ext_.grit > 0.01f)
                { wetL = std::tanh (wetL * gritDrv_) * gritNorm_; wetR = std::tanh (wetR * gritDrv_) * gritNorm_; }
                switch (ext_.filter)
                {
                    case 1:  // Low — one-pole LP
                        lpL_ += lpC_ * (wetL - lpL_); wetL = lpL_;
                        lpR_ += lpC_ * (wetR - lpR_); wetR = lpR_;
                        break;
                    case 2:  // Mid — HP into LP (band focus)
                        hpL_ += hpC_ * (wetL - hpL_); wetL -= hpL_;
                        hpR_ += hpC_ * (wetR - hpR_); wetR -= hpR_;
                        lpL_ += lpC_ * (wetL - lpL_); wetL = lpL_;
                        lpR_ += lpC_ * (wetR - lpR_); wetR = lpR_;
                        break;
                    case 3:  // High — one-pole HP
                        hpL_ += hpC_ * (wetL - hpL_); wetL -= hpL_;
                        hpR_ += hpC_ * (wetR - hpR_); wetR -= hpR_;
                        break;
                    default: break;
                }
                wetL *= trimGain_; wetR *= trimGain_;
            }

            // wet gate (cosine) for mode enter/exit click-free
            const float target = wantWet ? 1.f : 0.f;
            const float stepInc = 1.f / (float) fade_;
            if (wetEnv_ < target)      wetEnv_ = std::min (target, wetEnv_ + stepInc);
            else if (wetEnv_ > target) wetEnv_ = std::max (target, wetEnv_ - stepInc);
            const float wenv = 0.5f - 0.5f * std::cos (3.14159265358979f * arpClamp01 (wetEnv_)); // smooth the gate

            const float a = mix_ * wenv;
            L[i] = flush (dryL + (wetL - dryL) * a);
            R[i] = flush (dryR + (wetR - dryR) * a);

            if (wr)
            {
                ++wAbs_;
                if (written_ < cap_) ++written_;
            }
        }

        freePpq_ = (playing ? hostPpq : freePpq_) + pps * (double) numSamples;

        // viz playhead: slice position within the loop (time-based, linear)
        {
            double m = std::fmod (p / (double) beats, (double) len_);
            if (m < 0) m += len_;
            vizStepF_ = (float) m;
        }
    }

private:
    struct Voice
    {
        bool   active = false;
        double srcStart = 0.0;     // absolute start index of the slice in the ring
        double sliceLen = 0.0;     // source samples spanned by the slice (for reverse origin)
        double readPos = 0.0;      // samples advanced (always >= 0)
        double rate = 1.0;         // pitch ratio
        bool   reverse = false;
        float  level = 1.0f;
        int    phase = 0;          // 0 attack, 1 sustain, 2 release, 3 done
        int    aLen = 1, rLen = 1, aPos = 0, rPos = 0;
        int    gate = 0;           // output samples until release
        float  relStart = 1.0f;    // envelope shape value when release began
        // fb106 extension
        double rateFrom = 1.0;     // glide origin (varispeed portamento)
        int    glideLen = 0, glidePos = 0;
        bool   lpOn = false;       // Drop Tone one-pole
        float  lpC = 1.f, lpZL = 0.f, lpZR = 0.f;
    };

    static constexpr ChopStyle kOrderStyle[4] = {
        ChopStyle::Forward, ChopStyle::PingPong, ChopStyle::Shuffle, (ChopStyle) 6 };   // 6 = Walk (card-only)

    static double boundaryTime (long long step, float beats, double sw) noexcept
    { return (double) step * (double) beats + ((step & 1LL) ? sw : 0.0); }

    // schedule the slice(s) for a step
    void onBoundary (long long step, double stepSamp, float gFrac, float flipAmt, float pitchAmt) noexcept
    {
        // fb106: Rpts holds a flip for N windows before a free re-roll; a locked
        // Seed makes every regen identical (the flip repeats forever); Wander
        // re-rolls the window drift on each regen.
        const long long loopIdx = (long long) ((step >= 0 ? step : 0) / (long long) len_);
        const bool atLoopStart = (step % len_) == 0;
        if (regenPattern_ || (atLoopStart && (! extOn_ || ext_.rpts <= 1 || (loopIdx % (long long) arpClampi (ext_.rpts, 1, 4)) == 0)))
        {
            if (extOn_)
            {
                const int seedIdx = (int) std::lround (arpClamp01 (ext_.oSeed) * 16.0f);
                if (seedIdx > 0) rng_.seed (0xC40F5EEDu ^ (uint32_t) seedIdx * 2654435761u);   // locked flip
                wanderCur_ = (ext_.wander > 0.01f) ? (rng_.unit() - 0.5f) * ext_.wander * 2.0f : 0.0f;
            }
            regeneratePattern();
            regenPattern_ = false;
        }
        const int li = (int) (((step % len_) + len_) % len_);

        // back-offset to replay captured slice order_[li] live-anchored:
        //   Forward (slice==li) => back 0 (≈1-slice latency, feels live).
        //   A pattern that wants a not-yet-recorded slice (slice>li) borrows last loop's copy.
        const int slice = order_[li];
        int back = li - slice;
        if (back < 0) back += len_;
        lastSlice_ = slice;

        // DROP lane: the slice goes silent — Size 0 = a real hole, else a scattered grain
        bool dropped = false;
        if (extOn_ && ext_.dAmt > 0.01f && rng_.unit() < ext_.dAmt * 0.85f) dropped = true;
        if (dropped && ext_.dSize <= 0.03f)
        {
            if (cur_ >= 0 && voice_[cur_].active && voice_[cur_].phase < 2) startRelease (voice_[cur_]);
            subRemain_ = 0;
            return;
        }

        // REV — card: absolute Odds (+VARY spice), runs of Len, spaced by Sprd,
        // Snap flips THIS moment in place; legacy: probability × VARY.
        bool rev; bool snapSelf = false;
        if (extOn_)
        {
            if (revRunLeft_ > 0)      { rev = true;  --revRunLeft_; }
            else if (revCool_ > 0)    { rev = false; --revCool_; }
            else
            {
                rev = rng_.unit() < arpClamp01 (ext_.rvOdds + flipAmt * 0.4f);
                if (rev)
                {
                    revRunLeft_ = (int) std::lround (ext_.rvRun * 3.0f);
                    revCool_    = (int) std::lround (ext_.rvSpread * 4.0f);
                }
            }
            if (rev && rng_.unit() < ext_.rvSnap) snapSelf = true;
        }
        else rev = (rng_.unit() < revProb_ * flipAmt);
        if (snapSelf) back = 0;

        // PITCH — card: Steps = odds, Range = reach (degrees), Quant = in-key vs
        // chromatic, full depth; legacy: VARY odds × MORPH depth.
        int semi = 0;
        if (extOn_)
        {
            const int reach = (int) std::lround (arpClamp01 (ext_.pRange) * 12.0f);
            if (reach > 0 && rng_.unit() < arpClamp01 (ext_.pSteps * 0.9f + flipAmt * 0.3f))
            {
                const int deg = (int) rng_.below ((uint32_t) reach + 1) * (rng_.unit() < 0.5f ? 1 : -1);
                semi = (rng_.unit() < ext_.pQuant) ? scaleSemis (deg) : deg;
                semi = arpClampi (semi, -12, 12);   // fb109: repitch caps at one octave — degrees can reach ±20 semis (Max: "staticky bullshit")
            }
        }
        else if (pitchRangeDeg_ > 0 && rng_.unit() < flipAmt)
        {
            const int deg = (int) rng_.below (pitchRangeDeg_ + 1) * (rng_.unit() < 0.5f ? 1 : -1);
            semi = scaleSemis (deg);
            semi = (int) std::lround (semi * (double) pitchAmt);   // MORPH scales the spice depth
        }

        // TRIM — Len shapes the gate, Rand humanizes, Gate chokes staccato
        float gF = gFrac;
        if (extOn_)
        {
            gF *= 0.3f + arpClamp01 (ext_.tLen) * 0.8f;
            gF *= 1.0f + (rng_.unit() - 0.5f) * arpClamp01 (ext_.tRand) * 0.8f;
            if (ext_.tGate > 0.01f && rng_.unit() < ext_.tGate * 0.6f) gF *= 0.35f;
            if (gF < 0.05f) gF = 0.05f; if (gF > 1.0f) gF = 1.0f;
        }
        float gateFull = (float) (stepSamp * (double) gF);
        float lvl = 1.0f;
        if (dropped)
        {   // grain survivor: a short scattered blip where the slice used to be
            gateFull = (float) std::max ((double) (4 * fadeCur()), (double) gateFull * (double) arpClamp01 (ext_.dSize) * 0.5);
            lvl = 0.9f;
        }

        // REPEAT lane (Odds × Count) + StutterRoll / GrainSpray style minimums
        int rolls = ratchet_;
        float pitchStep = 0.f;
        if (extOn_)
        {
            const int rc = 1 + (int) std::lround (arpClamp01 (ext_.rCount) * 3.0f);
            if (rc > 1 && rng_.unit() < arpClamp01 (ext_.rOdds) * 0.8f) rolls = rc;
        }
        if (curStyle_ == ChopStyle::StutterRoll) { rolls = (rolls < 4) ? 4 : rolls; pitchStep = (float) scaleSemis (1); } // ascending in-key
        if (curStyle_ == ChopStyle::GrainSpray)  { rolls = (rolls < 6) ? 6 : rolls; }

        subInterval_ = (rolls > 1) ? (int) std::lround (stepSamp / (double) rolls) : 0;
        if (rolls > 1 && subInterval_ < 1) subInterval_ = 1;
        // per-hit gate: rolls last one sub-slot (so voices never pile past the pool); else fill the step
        const float perGate = (rolls > 1) ? (float) std::max (2 * fadeCur(), subInterval_) : gateFull;

        // fire the first hit now; queue the remaining as timed sub-triggers
        triggerVoice (back, rev, (float) semi, perGate, stepSamp, lvl, dropped);
        subRemain_ = rolls - 1;
        subsFired_ = 0;
        subLvl_    = lvl;
        if (subRemain_ > 0)
        {
            subTimer_      = subInterval_;
            subPitchAccum_ = 0.f;
            subPitchStep_  = pitchStep;
            pendBack_      = back;  pendRev_ = rev; pendPitch_ = (float) semi; pendGate_ = perGate; pendDrop_ = dropped;
            if (curStyle_ == ChopStyle::GrainSpray) { pendBack_ = -1; }     // -1 = random per sub (set in trigger)
        }
    }

    // allocate a voice, release the current, attack the new (overlap = click-free)
    void triggerVoice (int back, bool rev, float semi, float gateSamples, double stepSamp,
                       float lvl = 1.0f, bool toneGrain = false) noexcept
    {
        if (back < 0) back = (int) rng_.below (len_);                    // GrainSpray random pick
        const double sliceLen = stepSamp;
        // fb109: `back` counts PADS (loop/slices cells each) — the source jump
        // distance; playback still spans one Time step read behind the write head.
        double srcStart = (double) wAbs_ - (double) back * padSamp_ - sliceLen;
        if (extOn_)
        {
            srcStart -= scanBackSamp_;                                   // Scan/Wander window offset
            if (ext_.spread > 0.01f)                                     // per-slice source scatter
                srcStart -= (double) rng_.unit() * (double) ext_.spread * sliceLen * 2.0;
            if (toneGrain && ext_.dSpray > 0.01f)                        // dropped-grain scatter
                srcStart -= (double) rng_.unit() * (double) ext_.dSpray * sliceLen * 4.0;
        }

        // release whoever is current
        if (cur_ >= 0 && voice_[cur_].active && voice_[cur_].phase < 2) startRelease (voice_[cur_]);

        const int slot = allocVoice();
        Voice& v = voice_[slot];
        v.active = true; v.phase = 0; v.aPos = 0; v.rPos = 0;
        v.srcStart = srcStart; v.sliceLen = sliceLen; v.readPos = 0.0;
        v.reverse = rev;
        v.rate = std::pow (2.0, (double) semi / 12.0);
        v.level = lvl;
        v.rateFrom = v.rate; v.glideLen = 0; v.glidePos = 0;
        v.lpOn = false; v.lpZL = 0.f; v.lpZR = 0.f; v.lpC = 1.f;
        if (extOn_)
        {
            if (ext_.detune > 0.01f)                                     // per-slice tape detune
                v.rate *= std::pow (2.0, (double) (rng_.unit() - 0.5f) * (double) ext_.detune * 100.0 / 1200.0);
            if (ext_.speed > 0.01f)                                      // decimated memory → pitch-compensate
                v.rate *= 1.0 / (1.0 + (double) ext_.speed * 3.0);
            if (ext_.pGlide > 0.01f)                                     // varispeed portamento from the last slice
            {
                v.rateFrom = lastRate_;
                v.glideLen = (int) std::lround ((double) arpClamp01 (ext_.pGlide) * sliceLen * 0.8);
                const int gCap = (int) std::lround (sr_ * 0.08);         // fb109: ≤80 ms — fast grids stay snappy, no seasick warble
                if (v.glideLen > gCap) v.glideLen = gCap;
            }
            if (toneGrain)                                               // Drop Tone: per-grain one-pole color
            {
                const float hz = 400.0f * std::pow (2.0f, arpClamp01 (ext_.dTone) * 4.3f);
                v.lpOn = true;
                v.lpC  = 1.0f - std::exp (-2.0f * 3.14159265f * hz / (float) sr_);
            }
            lastRate_ = v.rate;
        }
        v.aLen = clampFade ((int) gateSamples);
        if (extOn_)
        {   // Trim Curve: soft swell ↔ snap attack (re-clamped inside the gate)
            v.aLen = (int) std::lround ((float) v.aLen * (0.4f + arpClamp01 (ext_.tCurve) * 2.1f));
            const int half = (int) gateSamples / 2;
            if (v.aLen > half) v.aLen = half;
            if (v.aLen < 1) v.aLen = 1;
        }
        v.rLen = v.aLen;
        v.gate = (int) gateSamples < 1 ? 1 : (int) gateSamples;
        v.relStart = 1.0f;
        cur_ = slot;
        ++fireCount_;
    }

    int allocVoice() noexcept
    {
        for (int v = 0; v < kChopVoices; ++v) if (! voice_[v].active) return v;
        // steal the one closest to done (smallest gate+release remaining)
        int best = 0; int bestRem = 1 << 30;
        for (int v = 0; v < kChopVoices; ++v) { const int rem = voice_[v].phase >= 2 ? (voice_[v].rLen - voice_[v].rPos) : (voice_[v].gate + voice_[v].rLen); if (rem < bestRem) { bestRem = rem; best = v; } }
        return best;
    }

    int fadeCur() const noexcept { return extOn_ ? fadeEff_ : fade_; }
    int clampFade (int gateSamples) const noexcept
    {
        int f = fadeCur();
        if (f > gateSamples / 2) f = gateSamples / 2;       // fade must fit inside the gate
        if (f < 1) f = 1;
        return f;
    }

    static void startRelease (Voice& v) noexcept
    {
        if (v.phase >= 2) return;
        // current envelope shape value -> release continues from it (continuous)
        float shape = (v.phase == 0) ? (0.5f - 0.5f * std::cos (3.14159265f * (float) v.aPos / (float) v.aLen)) : 1.0f;
        v.relStart = shape; v.phase = 2; v.rPos = 0;
    }

    void renderVoice (Voice& v, float& wetL, float& wetR, float wowMul = 1.0f) noexcept
    {
        // ---- envelope (cosine, zero-slope endpoints) ----
        float shape;
        if (v.phase == 0)            // attack
        {
            shape = 0.5f - 0.5f * std::cos (3.14159265f * (float) v.aPos / (float) v.aLen);
            if (++v.aPos >= v.aLen) v.phase = 1;
        }
        else if (v.phase == 1)       // sustain
        {
            shape = 1.0f;
        }
        else if (v.phase == 2)       // release
        {
            shape = v.relStart * (0.5f + 0.5f * std::cos (3.14159265f * (float) v.rPos / (float) v.rLen));
            if (++v.rPos >= v.rLen) { v.phase = 3; v.active = false; }
        }
        else { v.active = false; return; }

        const float g = shape * v.level;

        // ---- continuous read (forward, or backward for reverse) ----
        const double idx = v.reverse ? (v.srcStart + v.sliceLen - 1.0 - v.readPos)
                                     : (v.srcStart + v.readPos);
        float sL = sampleAt (bufL_, idx);
        float sR = sampleAt (bufR_, idx);
        if (v.lpOn)                                   // Drop Tone grain color
        {
            v.lpZL += v.lpC * (sL - v.lpZL); sL = v.lpZL;
            v.lpZR += v.lpC * (sR - v.lpZR); sR = v.lpZR;
        }
        wetL += g * sL; wetR += g * sR;

        // varispeed: glide from the previous slice's rate (tape portamento), wow wobble
        double eff = v.rate;
        if (v.glideLen > 0 && v.glidePos < v.glideLen)
        {
            const double t = (double) v.glidePos / (double) v.glideLen;
            eff = v.rateFrom + (v.rate - v.rateFrom) * t;
            ++v.glidePos;
        }
        v.readPos += eff * (double) wowMul;

        // gate countdown → release at step end
        if (v.phase < 2) { if (--v.gate <= 0) startRelease (v); }
    }

    // read ring at absolute fractional index, clamped to the valid written window (Hermite)
    float sampleAt (const std::vector<float>& b, double absIdx) const noexcept
    {
        if (written_ <= 0) return 0.f;                  // wiped/empty memory = silence, not a stale DC hold
        const double newest = (double) (wAbs_ - 1);
        const double oldest = (double) (wAbs_ - (long long) (written_ < cap_ ? written_ : cap_));
        if (absIdx > newest) absIdx = newest;               // flat hold (no jump) if reading ahead
        if (absIdx < oldest) absIdx = oldest;
        const double fp = std::floor (absIdx);
        const float t = (float) (absIdx - fp);
        const long long i = (long long) fp;
        const float xm1 = ringAt (b, i - 1, oldest, newest);
        const float x0  = ringAt (b, i,     oldest, newest);
        const float x1  = ringAt (b, i + 1, oldest, newest);
        const float x2  = ringAt (b, i + 2, oldest, newest);
        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
    float ringAt (const std::vector<float>& b, long long i, double oldest, double newest) const noexcept
    {
        if ((double) i < oldest) i = (long long) oldest;
        if ((double) i > newest) i = (long long) newest;
        long long m = i % cap_; if (m < 0) m += cap_;
        return b[(size_t) m];
    }

    // ── flip-pattern generation (slice playback ORDER) + déjà-vu lock ─────────────
    void regeneratePattern() noexcept
    {
        const int L = len_;
        if (extOn_ && (int) curStyle_ == 6)            // Walk: wander to a neighbor each slice
        {
            int cur = arpClampi (order_[0], 0, L - 1);
            for (int i = 0; i < L; ++i)
            {
                order_[i] = cur;
                cur = arpClampi (cur + ((rng_.next() & 1u) ? 1 : -1), 0, L - 1);
            }
            return;
        }
        switch (curStyle_)
        {
            case ChopStyle::Forward:   for (int i = 0; i < L; ++i) order_[i] = i; break;             // recorded order (delayed passthrough)
            case ChopStyle::Reverse:   for (int i = 0; i < L; ++i) order_[i] = L - 1 - i; break;      // newest→oldest (time-reversed order)
            case ChopStyle::PingPong:  { const int P = (L > 1) ? 2 * L - 2 : 1; for (int i = 0; i < L; ++i) { int m = i % P; order_[i] = (m < L) ? L - 1 - m : L - 1 - (P - m); } } break;
            case ChopStyle::Shuffle:
            case ChopStyle::GrainSpray:
            case ChopStyle::StutterRoll:
            default:
            {
                if (extOn_)
                {   // ORDER lane: Lock keeps seats in place, Sprd bounds the reach, Bias leans old/new
                    const int span    = 1 + (int) std::lround (arpClamp01 (ext_.oSpread) * (float) (L - 1));
                    const int biasOff = (int) std::lround ((arpClamp01 (ext_.oBias) - 0.5f) * (float) L);
                    for (int i = 0; i < L; ++i)
                    {
                        if (rng_.unit() < arpClamp01 (ext_.oLock)) { order_[i] = i; continue; }
                        const int off = (int) rng_.below ((uint32_t) (2 * span + 1)) - span + biasOff;
                        order_[i] = arpClampi (i + off, 0, L - 1);
                    }
                    break;
                }
                for (int i = 0; i < L; ++i)
                {
                    if (! (backValid_[i] && rng_.unit() < dejavu_))          // déjà-vu: keep cached order when locked
                    { order_[i] = (int) rng_.below (L); backValid_[i] = true; }
                }
                break;
            }
        }
    }

    // scale tables: semitone offset for a scale-degree (in-key repitch)
    void buildScale() noexcept
    {
        scaleN_ = 0;
        for (int i = 0; i < 12; ++i) if (scaleMask_ & (1u << i)) { if (scaleN_ < 12) scaleSemi_[scaleN_++] = i; }
        if (scaleN_ == 0) { for (int i = 0; i < 12; ++i) scaleSemi_[i] = i; scaleN_ = 12; }
    }
    int scaleSemis (int degree) const noexcept
    {
        if (scaleN_ <= 0) return degree;
        const int oct = (degree >= 0) ? degree / scaleN_ : -(((-degree) + scaleN_ - 1) / scaleN_);
        const int idx = ((degree % scaleN_) + scaleN_) % scaleN_;
        return 12 * oct + scaleSemi_[idx];
    }

    static float flush (float x) noexcept
    {
        if (! (x == x)) return 0.f;
        if (x >  8.f) return 8.f;
        if (x < -8.f) return -8.f;
        if (x < 1.0e-20f && x > -1.0e-20f) return 0.f;
        return x;
    }

    // ── state ─────────────────────────────────────────────────────────────────────
    std::vector<float> bufL_, bufR_;
    int      cap_ = 0, fade_ = 2;
    long long wAbs_ = 0; int written_ = 0;
    double   sr_ = 44100.0;

    // config
    ChopMode mode_ = ChopMode::AlwaysOn;
    bool     catchHeld_ = false;
    int      len_ = 8;
    float    mix_ = 1.0f, dejavu_ = 0.0f, revProb_ = 0.35f;
    int      pitchRangeDeg_ = 4, ratchet_ = 1, styleOv_ = -1;
    bool     keyTrack_ = true;
    uint32_t seed_ = 0xC40F5EEDu;
    int      scaleRoot_ = 60, lastRoot_ = 60; uint16_t scaleMask_ = 0x0AB5; // major default
    int      scaleSemi_[12] = {0,2,4,5,7,9,11,0,0,0,0,0}; int scaleN_ = 7;

    // runtime
    Voice    voice_[kChopVoices];
    int      cur_ = -1, lastSlice_ = 0;
    ChopStyle curStyle_ = ChopStyle::Forward;
    float    wetEnv_ = 0.f;
    int      order_[kChopMaxLoop] = {0}; bool backValid_[kChopMaxLoop];
    bool     regenPattern_ = true;

    // sub-trigger (ratchet / roll) state
    int      subRemain_ = 0, subTimer_ = 0, subInterval_ = 1;
    float    subPitchAccum_ = 0.f, subPitchStep_ = 0.f;
    int      pendBack_ = 0; bool pendRev_ = false; float pendPitch_ = 0.f; float pendGate_ = 0.f;

    // clock
    bool     haveClock_ = false; long long nextStep_ = 0; double nextBoundary_ = 0.0, freePpq_ = 0.0, lastBeats_ = 0.0;

    // ── fb106 extension state ──────────────────────────────────────────────
    bool          extOn_ = false;
    ChopExtParams ext_;
    double        scanBackSamp_ = 0.0, spAcc_ = 0.0, lastRate_ = 1.0, padSamp_ = 0.0;
    float         wanderCur_ = 0.f, colEnv_ = 0.f;
    float         crushLv_ = 0.f, gritDrv_ = 1.f, gritNorm_ = 1.f, trimGain_ = 1.f;
    int           fadeEff_ = 2;
    float         lpC_ = 1.f, hpC_ = 1.f, lpL_ = 0.f, lpR_ = 0.f, hpL_ = 0.f, hpR_ = 0.f;
    float         wowPh_ = 0.f, wowInc_ = 0.f;
    int           revRunLeft_ = 0, revCool_ = 0;
    float         subLvl_ = 1.f; bool pendDrop_ = false; int subsFired_ = 0;
    uint32_t      fireCount_ = 0;
    float         vizStepF_ = 0.f;

    mutable ArpRng rng_;
};

} // namespace wc
