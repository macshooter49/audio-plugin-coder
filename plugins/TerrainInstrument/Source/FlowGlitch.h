#pragma once
// =============================================================================
//  FlowGlitch.h  —  Terrain Instrument · FLOW mode GLITCH
//  Waves Crate
//
//  Header-only, NO JUCE. RT-safe in process() (allocation only in prepare()).
//  Sibling of FlowArp.h / FlowChop.h — shares the rate ladder, ArpRng, clamps.
//
//  WHAT IT IS
//  GLITCH is a performance mode (FLOW mode 3). It mangles the synth's OWN output
//  with a randomized, beat-synced chain of buffer effects — stutter repeats,
//  reverses, tape-stops, gates, pitch jumps, bitcrush, spectral-ish freezes, and
//  grain scatter. Where CHOP musically *re-grooves* slices, GLITCH *destroys*
//  them for chaos/fills. Driven by the same five macros + a glass mode menu.
//
//  ── THE NON-NEGOTIABLE: ZERO CLICKS ──────────────────────────────────────────
//  Most stutter/glitch effects click on every repeat — when a grain loops, the
//  read pointer jumps and the waveform steps discontinuously. We kill that the
//  same way CHOP does (the "silent light-switch"), on three fronts:
//    1) MASTER WET GATE — a cosine ramp (zero-slope ends) crossfades dry<->wet
//       on glitch enter and on natural release.
//    2) SEAM-CROSSFADE TAIL READER — INSIDE a glitch, whenever the effect's read
//       pointer jumps (grain wrap, scatter hop) a short cosine crossfade blends
//       the old grain's continuation into the new read. Length is clamped below
//       the effect's seam interval so it always finishes before the next seam.
//    3) DELAYED-COMMIT DUCK — when a NEW glitch fires while one is already wet
//       (a retrigger), the old effect fades to silence over ~2 ms, THEN the new
//       effect is committed and fades up. This covers every effect-type change
//       (read<->direct, direct<->direct) with one uniform mechanism, so no hard
//       wet jump is possible. A fresh glitch from silence commits immediately.
//  Proven offline by asserting max |out[n]-out[n-1]| stays tiny under every
//  effect at punishing settings, including back-to-back retriggers.
//
//  KNOBS (effective = base + LFO mod, summed upstream):
//    RATE -> glitch grid (ladder)        GATE -> slice length (tight<->long stutters)
//    VARY -> fire probability + deja-vu lock    TRAJ -> chaos (effect/pitch/reverse jumps)
//    MORPH-> swing
//
//  ── fb115: THE MONITOR CARD EXTENSION (GlitchExtParams, gated on extOn_) ─────
//  The Monitor card drives everything through setExt(); the legacy 5-macro path
//  is preserved BIT-EXACT when setExt() was never called (the original offline
//  proofs still pass against it). Ext mode:
//    · DICE DISCIPLINE (fb111 law): the ext path NEVER touches the mutable RNG.
//      Every decision is a stateless arpHash01(key, index, PURPOSE). Two
//      instances with the same params render bit-identical audio.
//    · Fire per grid slot: chance = VARY macro. Two dice streams — a LOCKED one
//      (keyed to Seed) and a FRESH one (keyed to Seed ^ loop index). Déjà Vu is
//      the amount of the pattern that reads the LOCKED stream: 0 = new pattern
//      every loop, 1 = the same glitch pattern forever. A locked slot replays
//      its ENTIRE internal dice (drift, scatter, spray) — true déjà vu.
//    · EXCLUSIVE READERS + STACKABLE OVERLAYS: Rep/Rev/Tape/Pit/Frz/Sct are
//      buffer READERS — one per fire, hash-picked from the enabled set. Gate
//      and Crush are OVERLAYS: enabled alongside a reader they process its wet
//      (REP+CRSH = crushed stutter, FRZ+GATE = gated freeze); enabled alone
//      they run as the main effect.
//    · Wet bus: Filter (Off/Low/Mid/High, chop's one-poles), Pan (L/C/R),
//      Bend (slow read-rate warble), master Decay (wet eases back to dry over
//      the hold). Clock Free/Sync: Free ignores the host bar position.
//    · ROLL: momentary forced fire (works at Chance 0), quantized to the Quant
//      grid (1/4..1/32) like a Gross Beat punch-in.
//
//  Build test: g++ -std=c++17 Source/FlowGlitch_test.cpp -o /tmp/fg && /tmp/fg
// =============================================================================

#include "FlowArp.h"
#include <vector>
#include <cmath>

namespace wc
{

enum class GlitchFx : int { Repeat = 0, Reverse, TapeStop, Gate, Pitch, Crush, Freeze, Scatter };
static constexpr int kGlitchFxN = 8;

// Monitor-card parameters (fb115). Defaults = the NEUTRAL DEFAULTS LAW: REP only,
// every depth knob at its transparent value — first touch teaches the knob.
struct GlitchExtParams
{
    bool  en[kGlitchFxN] = { true, false, false, false, false, false, false, false };
    float dejavu = 0.0f;          // 0 = fresh dice every loop, 1 = pattern locked forever
    float holdSteps = 1.0f;       // 1,2,3,4,6,8 grid steps
    int   loopLen = 8;            // pattern length in steps (2,4,8,12,16)
    float decay = 0.0f;           // master: wet eases back to DRY over the hold
    float drop  = 0.0f;           // fb143 — chance a fire lands as pure SILENCE (a hole in the loop)
    float burst = 0.0f;           // fb143 — chance a landed fire STREAKS onto the next step, same dice
    int   seed = 0;               // 0 = Free (re-rolls), 1..99 = locked dice
    int   quantIdx = 2;           // Roll punch-in grid: 1/4, 1/8, 1/16, 1/32
    bool  releaseNow = false;     // false = "End": Rep/Rev holds round UP to whole repeats
    float bend = 0.0f;            // slow read-rate warble on readers (tape drift)
    int   filter = 0;             // (legacy global bus — superseded by fxFlt/fxPan, fb125)
    int   pan = 1;
    int   fxFlt[kGlitchFxN] = { 0, 0, 0, 0, 0, 0, 0, 0 };   // fb125 — per-EFFECT Out: Filter Off/Low/Mid/High
    int   fxPan[kGlitchFxN] = { 1, 1, 1, 1, 1, 1, 1, 1 };   //         and Pan L/C/R ("pitch left, tape right")
    int   fxTrig[kGlitchFxN] = { 0, 0, 0, 0, 0, 0, 0, 0 };  // fb127 — per-EFFECT Trig: 0 Sync (the beat grid)
                                                            // 1 Free (its own drifting clock) 2 Roll-only (manual)
    int   fxGrid[kGlitchFxN] = { 0, 0, 0, 0, 0, 0, 0, 0 };  // fb127 — per-EFFECT Grid: 0 = Main (follow the
                                                            // master Grid), 1..19 = its own ladder division
                                                            // ("pitch at 1/1 while repeat runs 1/16") — the
                                                            // fire's slice/hold lengths use ITS division too
    bool  sync = true;            // false = Free-run clock (ignore host bar position)
    // per-effect (4 each) — names match the card. Research-confirmed inits (fb115):
    // Rep/Rev Size = the whole step (turning DOWN speeds the stutter), Pitch = -12
    // (the classic octave-down), Gate div 2, Crush 8-bit ÷4, Freeze 50 ms, Scatter 40 ms.
    float repSize = 1.0f, repSpeed = 0.0f, repFade = 0.0f, repVary = 0.0f;
    float revLen  = 1.0f, revFade  = 0.0f, revSprd  = 0.0f, revSnap  = 1.0f;
    float tapeCurve = 0.7f, tapeTime = 0.5f, tapeDepth = 1.0f, tapeSpin = 0.0f;
    float gateRate = 0.14f, gateShape = 0.5f, gateNudge = 0.0f, gateAmt = 1.0f;
    float pitShift = 0.0f, pitWalk = 0.0f, pitGlide = 0.0f, pitJump = 0.0f;
    float crshBits = 0.67f, crshRate = 0.31f, crshTone = 1.0f, crshAmt = 1.0f;
    float frzSize = 0.19f, frzSpray = 0.0f, frzShine = 0.0f, frzMelt = 0.3f;
    float sctSize = 0.26f, sctAmt = 1.0f, sctVary = 0.0f, sctWidth = 0.0f;
};

class FlowGlitch
{
public:
    FlowGlitch() { rng_.seed (0x6117C40Du); for (auto& s : bStep_) s = kBNever; }

    // -- lifecycle ----------------------------------------------------------------
    void prepare (double sampleRate, double captureSeconds = 4.0) noexcept
    {
        sr_  = (sampleRate > 0.0 ? sampleRate : 44100.0);
        kHole_ = 1.0f - std::exp (-1.0f / (0.0025f * (float) sr_));   // fb143 — 2.5ms hole glide
        cap_ = (int) std::ceil (sr_ * (captureSeconds > 0.5 ? captureSeconds : 0.5));
        if (cap_ < 1024) cap_ = 1024;
        bufL_.assign ((size_t) cap_, 0.0f);
        bufR_.assign ((size_t) cap_, 0.0f);
        fade_ = (int) std::lround (sr_ * 0.0020);            // 2 ms cosine seam/gate/duck edge
        if (fade_ < 2) fade_ = 2;
        reset();
    }

    void reset() noexcept
    {
        w_ = 0; written_ = 0;
        glitchActive_ = false; gCounter_ = 0; gDur_ = 0; gLen_ = 0; gStartW_ = 0;
        readPosF_ = 0.0; shCnt_ = 0; shHold_ = 0.f; shHoldR_ = 0.f; lastSubGrain_ = -1; scatterBase_ = 0; lastWrap_ = -1;
        wetLevel_ = 0.f; wetTarget_ = 0.f;
        xf_ = 0; xfLen_ = fade_; seamInterval_ = 1 << 30;
        lastIdx_ = 0.0; lastStep_ = 0.0; tailIdx_ = 0.0; tailStep_ = 0.0; haveLast_ = false;
        pendingFire_ = false;
        haveClock_ = false; nextStep_ = 0; nextBoundary_ = 0.0; freePpq_ = 0.0; lastBeats_ = -1.f;
        freePpqF_ = 0.0; nActiveG_ = 0;
        for (int i = 0; i <= kArpRateRichN; ++i) { trkS_[i] = GridTrk(); trkF_[i] = GridTrk(); }
        for (int i = 0; i < 64; ++i) lockFireValid_[i] = false;
        rng_.seed (seed_ ? seed_ : 0x6117C40Du);
        // ext render state
        phF_ = 0.0; lenCur_ = 0.0; lvlCur_ = 1.f; driftOff_ = 0.0; wrapN_ = 0;
        shimL_ = shimR_ = 0.f; shineG_ = 0.f;
        chunkOff_ = 0.0; prCur_ = 1.0; prTgt_ = 1.0; semisAcc_ = 0.0;
        ovCnt_ = 0; ovL_ = 0.f; ovR_ = 0.f; bendPh_ = 0.0;
        lpL_ = lpR_ = hpL_ = hpR_ = 0.f; tone1L_ = tone1R_ = 0.f;
        rollArmed_ = false; rollB_ = 0.0; rollN_ = 0; fireCount_ = 0;
        for (auto& s : bStep_) s = kBNever;                         // fb143 — no phantom streaks
        pendHole_ = false; holeFire_ = false; holeG_ = 1.f;
        fireNonce_ = 1u; fireStep16_ = 0.f; sctRate_ = 1.0; chanOffR_ = 0.0;
        gGain_ = 1.f; gGainPrev_ = 1.f; gGainTail_ = 1.f;
        lvlAcc_ = 0.0; lvlN_ = 0; for (int i = 0; i < 16; ++i) lvl16_[i] = 0.f;
        vizStepF_ = 0.f; vizLoopF_ = 0.f; outLvl_ = 0.f;
    }

    // -- configuration (card -> engine) -------------------------------------------
    void setEnabled (GlitchFx fx, bool on) noexcept { enabled_[(int) fx] = on; }
    void setWeight (GlitchFx fx, float wgt) noexcept { weight_[(int) fx] = wgt < 0.f ? 0.f : wgt; }
    void setMix (float wet) noexcept
    {
        mix_ = arpClamp01 (wet);
        // laneC audit (2026-08-26) — mixSm_ initialises to 1.0 and only glides inside the
        // active branch, so the FIRST fire after prepare leaked ~2.5 ms of wet even at mix 0
        // (FlowGlitch_test T7). While the engine is idle the glide has nothing to smooth:
        // snap the smoother to the target so the first fire opens at the real mix.
        if (! glitchActive_ && wetLevel_ <= 0.f)
            mixSm_ = mix_;
    }
    // fb142 — OUT MODE (Beat Repeat's most consequential switch): 0 Mix (wet replaces dry
    // during fires, dry between) · 1 Cut (dry mutes for the WHOLE fire window, even through
    // wet gaps) · 2 Gate (only the glitch ever sounds — silence between fires).
    void setOutMode (int m) noexcept { outMode_ = m < 0 ? 0 : (m > 2 ? 2 : m); }
    // fb142 — PING: each fire lands on the other side of the stereo field (equal-power,
    // alternating, deterministic — "predictability with random uniqueness", Max).
    void setPing (float a) noexcept { pingAmt_ = arpClamp01 (a); }
    void setHoldSteps (float steps) noexcept { holdSteps_ = steps < 0.25f ? 0.25f : (steps > 32.f ? 32.f : steps); }
    void setSeed (uint32_t s) noexcept { seed_ = s; }
    void setDejavu (float d) noexcept { dejavu_ = arpClamp01 (d); }
    void setLoopLen (int n) noexcept { loopLen_ = n < 1 ? 1 : (n > 64 ? 64 : n); }
    void setPitchRatio (float r) noexcept { pitchRatio_ = r < 0.25f ? 0.25f : (r > 4.f ? 4.f : r); }
    void setCrush (int bits, int downsample) noexcept { crushBits_ = bits < 1 ? 1 : (bits > 16 ? 16 : bits); crushDown_ = downsample < 1 ? 1 : (downsample > 64 ? 64 : downsample); }
    void setGateDiv (int div) noexcept { gateDiv_ = div < 1 ? 1 : (div > 32 ? 32 : div); }
    void setTapeCurve (float c) noexcept { tapeCurve_ = c < 0.f ? 0.f : (c > 8.f ? 8.f : c); } // 0 = linear (brake), ~3.5 = tape coast
    void setFreezeGrainMs (float ms) noexcept { freezeMs_ = ms < 1.f ? 1.f : (ms > 200.f ? 200.f : ms); }
    void setScatterMs (float grainMs, float windowMs) noexcept { scatterGrainMs_ = grainMs < 5.f ? 5.f : grainMs; scatterWinMs_ = windowMs < 20.f ? 20.f : windowMs; }

    // fb115 — the Monitor card takes over (legacy path stays bit-exact while unused)
    void setExt (const GlitchExtParams& p) noexcept
    {
        ext_ = p;
        if (ext_.loopLen < 1)  ext_.loopLen = 1;
        if (ext_.loopLen > 64) ext_.loopLen = 64;
        extOn_ = true;
    }
    void rollNow() noexcept { rollReq_ = true; }     // momentary punch-in (quantized)

    bool     isActive() const noexcept { return glitchActive_ || wetLevel_ > 0.001f; }
    GlitchFx currentFx() const noexcept { return gFx_; }

    // fb115 — Monitor feed accessors (message thread reads via processor atomics)
    float vizStepF16()   const noexcept { return vizStepF_; }
    float vizLoopSlotF() const noexcept { return vizLoopF_; }
    int   vizFx()        const noexcept { return glitchActive_ ? (int) gFx_ : -1; }
    float vizFireStep16()const noexcept { return fireStep16_; }
    float vizHoldSteps() const noexcept { return extOn_ ? ext_.holdSteps : holdSteps_; }
    long long vizFireCount() const noexcept { return fireCount_; }
    float wetLevelViz()  const noexcept { return wetLevel_; }
    float outLevel()     const noexcept { return outLvl_; }        // fb124 — speaker meter
    float stepLevel (int i) const noexcept { return lvl16_[i & 15]; }

    // -- main: process the audio buffer in place ----------------------------------
    void process (float rate, float gate, float vary, float traj, float morph,
                  double hostPpq, double bpm, double sampleRate,
                  float* L, float* R, int numSamples, bool playing) noexcept
    {
        if (cap_ <= 0 || numSamples <= 0) return;
        const double SR  = (sampleRate > 0.0 ? sampleRate : sr_);
        const double BP  = (bpm > 0.0 ? bpm : 120.0);
        const double pps = (BP / 60.0) / SR;
        const float  beats = arpBeatsPerStepRich (rate);
        const double stepSamp = (double) beats / pps;                 // FIX: samples per step (was beats*SR)
        const double sw  = (double) arpClamp01 (morph > 0.9f ? 0.9f : morph) * ((double) beats * 0.5);

        const bool useHost = playing && (! extOn_ || ext_.sync);      // ext Free-run ignores host pos
        double p = useHost ? hostPpq : freePpq_;
        const long long curStepP = (long long) std::floor (p / (double) beats);
        if (! haveClock_) { nextStep_ = curStepP; nextBoundary_ = boundaryTime (nextStep_, beats, sw); haveClock_ = true; lastBeats_ = beats; }
        else if (beats != lastBeats_ || nextStep_ > curStepP + 2 || nextStep_ < curStepP - 2)
        {
            // fb116/fb128 — THE STRANDED-CLOCK LAW (fb107 class, chop's fb109 window):
            // a latched boundary strands on ANY discontinuity — a ladder move (fb116) OR
            // a transport jump (fb128: a DAW LOOP WRAP throws ppq backwards every pass —
            // "the cards stop a couple seconds after I press play over my drum loop").
            // Re-anchor whenever the step counter drifts outside a sane window of the
            // ppq-derived truth, exactly like FlowChop has done since fb109.
            lastBeats_ = beats;
            nextStep_ = curStepP + 1;
            nextBoundary_ = boundaryTime (nextStep_, beats, sw);
            rollArmed_ = false;                       // a stale Roll boundary is meaningless now
        }

        // fb127 — FIRE GROUPS: every active (clock, division) pair runs its OWN boundary
        // tracker. Trig picks the clock (Sync = the host grid p; Free = a self-running
        // timeline, half a step out of phase so the grids interleave; Roll = manual only),
        // Grid picks the division (Main = the master Grid, or the effect's own ladder step
        // — "pitch at 1/1 while repeat runs 1/16"). Each group fires ONE pick among its
        // own effects; a fire's slice/hold lengths come from ITS division.
        nActiveG_ = 0;
        if (extOn_)
        {
            bool seenG[2][kArpRateRichN + 1] = {};
            for (int fi = 0; fi < kGlitchFxN; ++fi)
            {
                if (! ext_.en[fi] || ext_.fxTrig[fi] > 1) continue;      // Roll-only: no grid
                const int cls = ext_.fxTrig[fi];
                int dk = ext_.fxGrid[fi]; if (dk < 0) dk = 0; if (dk > kArpRateRichN) dk = kArpRateRichN;
                if (seenG[cls][dk]) continue;
                seenG[cls][dk] = true;
                GridTrk& t = (cls == 0 ? trkS_[dk] : trkF_[dk]);
                const float gb = dk == 0 ? beats : kArpRateRich[dk - 1];
                const double ref = cls == 0 ? p : freePpqF_;
                const double off = cls == 0 ? 0.0 : 0.5 * (double) gb;   // free interleaves
                const long long curG = (long long) std::floor ((ref - off) / (double) gb);
                if (! t.have || gb != t.beats || t.step > curG + 2 || t.step < curG - 2)
                {   // STRANDED-CLOCK LAW (fb107/116/122/128): (re)anchor on first use, any
                    // grid change, or any transport jump (DAW loop wraps, relocates)
                    t.beats = gb;
                    t.step  = curG + 1;
                    t.bnd   = (double) t.step * (double) gb + off;
                    t.have  = true;
                }
                activeG_[nActiveG_].trk = &t; activeG_[nActiveG_].cls = cls;
                activeG_[nActiveG_].divKey = dk; activeG_[nActiveG_].off = off;
                activeG_[nActiveG_].stepSamp = (double) gb / pps;
                ++nActiveG_;
            }
        }

        // ext: hoist per-block coefs (wet bus one-poles, bend LFO inc)
        if (extOn_)
        {
            const float lpHz = 900.f, hpHz = 300.f;
            lpC_ = 1.0f - std::exp (-2.0f * 3.14159265f * lpHz / (float) SR);
            hpC_ = 1.0f - std::exp (-2.0f * 3.14159265f * hpHz / (float) SR);
            const float toneHz = 500.f + ext_.crshTone * ext_.crshTone * 11000.f;
            toneC_ = ext_.crshTone > 0.985f ? 1.f : (1.0f - std::exp (-2.0f * 3.14159265f * toneHz / (float) SR));
            bendInc_ = 2.0 * 3.14159265358979 * 0.9 / SR;
            if (rollReq_) { rollReq_ = false; armRoll (p); }
        }

        for (int i = 0; i < numSamples; ++i, p += pps)
        {
            const float dryL = L[i], dryR = R[i];
            bufL_[(size_t) w_] = dryL;
            bufR_[(size_t) w_] = dryR;

            if (extOn_) { lvlAcc_ += 0.5 * ((double) dryL * dryL + (double) dryR * dryR); ++lvlN_; }

            int guard = 0;
            while (p >= nextBoundary_ && guard++ < 64)
            {
                if (extOn_)
                    publishLevel (nextStep_);          // fb127: fires live in the groups now
                else
                    onBoundary (nextStep_, gate, vary, traj, stepSamp);
                ++nextStep_;
                nextBoundary_ = boundaryTime (nextStep_, beats, sw);
            }
            if (extOn_)
            {   // fb127 — every fire group checks its own boundary (sync rides p, free
                // rides the self-running timeline; ≤8 compares per sample)
                freePpqF_ += pps;
                for (int gi = 0; gi < nActiveG_; ++gi)
                {
                    ActiveG& ag = activeG_[gi];
                    const double ref = ag.cls == 0 ? p : freePpqF_;
                    int guardG = 0;
                    while (ref >= ag.trk->bnd && guardG++ < 64)
                    {
                        onBoundaryExt (ag.trk->step, gate, vary, traj, ag.stepSamp, ag.cls, ag.divKey);
                        ++ag.trk->step;
                        ag.trk->bnd = (double) ag.trk->step * (double) ag.trk->beats + ag.off
                                      + ((ag.cls == 0 && ag.divKey == 0 && (ag.trk->step & 1LL)) ? sw : 0.0);
                    }
                }
            }

            // Roll punch-in: fires on its own quant grid, independent of Chance
            if (extOn_ && rollArmed_ && p >= rollB_)
            {
                rollArmed_ = false;
                fireRoll (gate, stepSamp);
            }

            // commit a pending glitch once the previous one has ducked to silence (or if idle)
            if (pendingFire_ && (! glitchActive_ || wetLevel_ <= 0.001f))
                commitPending();

            float outL = dryL, outR = dryR;
            if (glitchActive_)
            {
                // wet target: duck to 0 while a retrigger waits to commit OR during natural release; else open
                if (pendingFire_)                       wetTarget_ = 0.f;
                else if (gCounter_ >= gDur_ - fade_)     wetTarget_ = 0.f;
                else                                     wetTarget_ = 1.f;

                double idx = 0.0, step = 0.0; bool seam = false; float wl = dryL, wr = dryR;
                const bool isRead = extOn_ ? fxReadExt (gCounter_, dryL, dryR, idx, step, seam, wl, wr)
                                           : fxRead    (gCounter_, dryL, dryR, idx, step, seam, wl, wr);

                if (isRead)
                {
                    // seam is reported explicitly by the effect at a real grain wrap (never inferred).
                    // Per-grain gain (gGain_) rides THROUGH the crossfade: the outgoing tail keeps
                    // the OLD grain's gain — fades/mutes can never step mid-fade (fb115).
                    if (seam && haveLast_) { tailIdx_ = lastIdx_ + lastStep_; tailStep_ = lastStep_; xf_ = xfLen_; gGainTail_ = gGainPrev_; }

                    const float pL = interp (bufL_, idx) * gGain_;
                    const float pR = interp (bufR_, idx + chanOffR_) * gGain_;
                    if (xf_ > 0)
                    {
                        const float q   = (float) (xfLen_ - xf_) / (float) xfLen_;   // 0->1
                        // EQUAL-POWER seam fade: grain joins are UNCORRELATED (the new read vs
                        // the old grain's tail), so sin/cos (att^2 + rel^2 = 1) holds energy
                        // constant and removes the -3 dB dip a linear (att+rel=1) fade leaves at
                        // every wrap — that dip, repeated at grain rate, is audible as tremolo.
                        const float att = std::sin (1.57079633f * q);
                        const float rel = std::cos (1.57079633f * q);
                        wl = pL * att + interp (bufL_, tailIdx_) * gGainTail_ * rel;
                        wr = pR * att + interp (bufR_, tailIdx_ + chanOffR_) * gGainTail_ * rel;
                        tailIdx_ += tailStep_; --xf_;
                    }
                    else { wl = pL; wr = pR; }

                    lastIdx_ = idx; lastStep_ = step; haveLast_ = true;   // step from the effect, never stale
                }

                float aScale = 1.f;
                if (extOn_) aScale = extPost (isRead, wl, wr);            // overlays + bus + master decay
                if (extOn_)
                {
                    // fb143 DROP — a hole fire: the wet glides to silence (2.5ms one-pole,
                    // the slew law), so a hole can never click, even landing mid-crossfade.
                    holeG_ += ((holeFire_ ? 0.f : 1.f) - holeG_) * kHole_;
                    wl *= holeG_; wr *= holeG_;
                }

                // master wet gate (cosine, zero-slope)
                const float inc = 1.0f / (float) fade_;
                if (wetLevel_ < wetTarget_)      wetLevel_ = std::min (wetTarget_, wetLevel_ + inc);
                else if (wetLevel_ > wetTarget_) wetLevel_ = std::max (wetTarget_, wetLevel_ - inc);
                const float wenv = 0.5f - 0.5f * std::cos (3.14159265f * arpClamp01 (wetLevel_));
                mixSm_ += (mix_ - mixSm_) * kHole_;   // fb204 — glided mix (2.5ms, the hole coef)
                const float a = mixSm_ * wenv * aScale;

                wl *= pingL_; wr *= pingR_;                        // fb142 — per-fire stereo bounce
                if (outMode_ == 1)                                 // fb142 CUT: dry dies for the fire window
                { outL = dryL * (1.0f - wenv) + wl * a; outR = dryR * (1.0f - wenv) + wr * a; }
                else if (outMode_ == 2)                            // fb142 GATE: only the glitch sounds
                { outL = wl * a; outR = wr * a; }
                else
                { outL = dryL + (wl - dryL) * a; outR = dryR + (wr - dryR) * a; }

                ++gCounter_;
                if (! pendingFire_ && gCounter_ >= gDur_) { glitchActive_ = false; haveLast_ = false; }
            }
            else
            {
                if (wetLevel_ > 0.f) wetLevel_ = std::max (0.f, wetLevel_ - 1.0f / (float) fade_);
                if (outMode_ == 2)                                 // fb142 GATE: silence between fires
                {
                    const float g = 0.5f - 0.5f * std::cos (3.14159265f * arpClamp01 (wetLevel_));
                    outL *= g; outR *= g;
                }
            }

            if (extOn_) { bendPh_ += bendInc_; if (bendPh_ > 6.28318530718) bendPh_ -= 6.28318530718; }

            L[i] = flush (outL);
            R[i] = flush (outR);

            if (++w_ >= cap_) w_ = 0;
            if (written_ < cap_) ++written_;
        }

        freePpq_ = (useHost ? hostPpq : freePpq_) + pps * (double) numSamples;

        if (extOn_)
        {
            // fb124 — output level for the monitor's speaker meter (fast peak, ~160ms release)
            float pk = 0.f;
            for (int i = 0; i < numSamples; ++i)
            { const float aa = std::fabs (L[i]) + std::fabs (R[i]); if (aa > pk) pk = aa; }
            const float dec = std::exp (-(float) numSamples / ((float) SR * 0.16f));
            outLvl_ = std::max (pk * 0.5f, outLvl_ * dec);

            const double stepF = p / (double) beats;
            const double s16 = std::fmod (std::fmod (stepF, 16.0) + 16.0, 16.0);
            const int    ll  = ext_.loopLen;
            const double sl  = std::fmod (std::fmod (stepF, (double) ll) + (double) ll, (double) ll);
            vizStepF_ = (float) s16; vizLoopF_ = (float) sl;
        }
    }

private:
    static double boundaryTime (long long step, float beats, double sw) noexcept
    { return (double) step * (double) beats + ((step & 1LL) ? sw : 0.0); }

    // ── legacy boundary (macros + mutable RNG) — untouched, bit-exact ────────────
    void onBoundary (long long step, float gate, float vary, float traj, double stepSamp) noexcept
    {
        bool fire;
        if (vary >= 1.0f)      fire = true;
        else if (vary <= 0.0f) fire = false;
        else                   fire = rollFire ((int) (((step % loopLen_) + loopLen_) % loopLen_), vary);
        if (! fire) return;

        pendFx_      = pickFx();
        pendStartW_  = w_;
        double sliceFrac = (double) arpClamp01 (gate);
        if (traj > 0.f) sliceFrac *= (1.0 - (double) traj * 0.5 * rng_.unit());
        pendGLen_ = (int) std::lround (stepSamp * (sliceFrac < 0.03 ? 0.03 : sliceFrac));
        if (pendGLen_ < 2) pendGLen_ = 2;
        if (pendGLen_ > cap_ / 2) pendGLen_ = cap_ / 2;
        pendGDur_ = (int) std::lround (stepSamp * (double) holdSteps_);
        if (pendGDur_ < pendGLen_) pendGDur_ = pendGLen_;
        pendPitch_   = pitchRatio_;
        if (traj > 0.f && rng_.unit() < traj) pendPitch_ = (rng_.unit() < 0.5f) ? 2.0f : 0.5f;
        pendReverse_ = (traj > 0.f && rng_.unit() < traj * 0.5f);

        pendingFire_ = true;                            // process() ducks (if wet) then commits
    }

    // ── fb115 ext boundary — STATELESS HASH dice (fb111 law), never the RNG ──────
    //  Locked stream (Seed) vs fresh stream (Seed ^ loop). Déjà Vu picks per slot.
    uint32_t seedBase() const noexcept { return ext_.seed > 0 ? (uint32_t) ext_.seed * 2654435761u + 0x9E3779B9u : 0x6117C40Du; }

    void onBoundaryExt (long long step, float gate, float vary, float traj, double stepSamp,
                        int clockClass, int divKey) noexcept   // fb127: the fire group's identity
    {
        const int  ll    = ext_.loopLen;
        const int  slot  = (int) (((step % ll) + ll) % ll);
        const long long loopN = (long long) std::floor ((double) step / (double) ll);
        const uint32_t kLock  = seedBase() ^ (clockClass ? 0xF2EE0000u : 0u)
                              ^ ((uint32_t) divKey * 0x9E3779B9u);              // decorrelated per group
        const uint32_t kFresh = kLock ^ ((uint32_t) (loopN & 0xffffffff) * 2246822519u + 0x85EBCA6Bu);
        const bool useLock = arpHash01 (kLock, (uint32_t) slot, 0xDAu) < ext_.dejavu;
        const uint32_t kUse = useLock ? kLock : kFresh;

        const float chance = arpClamp01 (vary * (1.f + traj * 0.5f));  // Chaos boosts the odds
        bool fire;
        if (chance >= 1.0f)      fire = true;
        else if (chance <= 0.0f) fire = false;
        else                     fire = arpHash01 (kUse, (uint32_t) slot, 0xF1u) < chance;

        // fb143 BURST — a landed fire STREAKS: when this group's PREVIOUS boundary fired,
        // the next one can re-fire with the SAME dice (nonce + reader), so the exact same
        // glitch repeats — clusters, not confetti. Stateless hash stream (fb111 law).
        const int gk = burstKey (clockClass, divKey);
        bool cont = false;
        if (! fire && ext_.burst > 0.001f && bStep_[gk] == step - 1
            && arpHash01 (kFresh, (uint32_t) slot, 0xB7u) < arpClamp01 (ext_.burst))
        { fire = true; cont = true; }
        if (! fire) return;

        GlitchFx fx;
        if (cont && ext_.en[bFx_[gk] & 7])
            fx = (GlitchFx) bFx_[gk];                                  // the streak keeps its reader
        else
        {
            cont = false;
            fx = pickFxExt (kUse, (uint32_t) slot, clockClass, divKey);
            if ((int) fx < 0) return;                                  // nothing in this group
            // Chaos: sometimes substitute a different reader FROM THE SAME GROUP
            if (traj > 0.f && arpHash01 (kFresh, (uint32_t) slot, 0xC7u) < traj * 0.35f)
            {
                const GlitchFx alt = pickFxExt (kFresh ^ 0x27d4eb2fu, (uint32_t) slot, clockClass, divKey);
                if ((int) alt >= 0) fx = alt;
            }
        }

        latchFireExt (fx, kUse, (uint32_t) slot, gate, traj,
                      arpHash01 (kFresh, (uint32_t) slot, 0xC9u), stepSamp, step);
        if (cont) pendNonce_ = bNonce_[gk];                            // the SAME dice — the moment repeats
        // fb143 DROP — some fires land as pure SILENCE (a hole punched in the loop). A
        // streak keeps its hole-ness: a cluster is all-glitch or all-hole, never a coin toss.
        pendHole_ = cont ? (bHole_[gk] != 0)
                         : (ext_.drop > 0.001f
                            && arpHash01 (kUse, (uint32_t) slot, 0xD0Du) < arpClamp01 (ext_.drop));
        bStep_[gk] = step; bNonce_[gk] = pendNonce_; bFx_[gk] = (int) fx; bHole_[gk] = pendHole_ ? 1 : 0;
    }

    // shared ext fire latch (boundary fires + Roll punch-ins)
    void latchFireExt (GlitchFx fx, uint32_t kUse, uint32_t slot, float gate, float traj,
                       float chaosDie, double stepSamp, long long step) noexcept
    {
        pendFx_     = fx;
        pendStartW_ = w_;

        // slice length: per-effect Size is the AUTHORITY (fb110 law); the front GATE
        // macro rides ±20%. Tape/Gate/Crush use the full step (their Size lives elsewhere).
        double frac;
        switch (fx)
        {
            case GlitchFx::Repeat:  frac = 0.08 + 0.92 * (double) arpClamp01 (ext_.repSize); break;
            case GlitchFx::Reverse: frac = 0.10 + 0.90 * (double) arpClamp01 (ext_.revLen);  break;
            default:                frac = 1.0; break;
        }
        frac *= 1.0 + ((double) arpClamp01 (gate) - 0.55) * 0.4;       // macro = bounded rider
        if (traj > 0.f) frac *= (1.0 - (double) traj * 0.5 * (double) chaosDie);
        if (frac < 0.03) frac = 0.03;
        if (frac > 2.0)  frac = 2.0;
        pendGLen_ = (int) std::lround (stepSamp * frac);
        if (pendGLen_ < 2) pendGLen_ = 2;
        if (pendGLen_ > cap_ / 2) pendGLen_ = cap_ / 2;

        pendGDur_ = (int) std::lround (stepSamp * (double) ext_.holdSteps);
        if (! ext_.releaseNow && (fx == GlitchFx::Repeat || fx == GlitchFx::Reverse))
        {   // "End": round the hold UP to whole repeats so the last one completes
            const int n = (pendGDur_ + pendGLen_ - 1) / (pendGLen_ > 0 ? pendGLen_ : 1);
            pendGDur_ = n * pendGLen_;
        }
        if (pendGDur_ < pendGLen_ && (fx == GlitchFx::Repeat || fx == GlitchFx::Reverse)) pendGDur_ = pendGLen_;
        if (pendGDur_ < 2) pendGDur_ = 2;

        // pitch target in SEMITONES (quantized — a control named in pitch must BE pitch)
        const int semis = (int) std::lround (((double) arpClamp01 (ext_.pitShift) * 2.0 - 1.0) * 12.0);
        pendSemis_ = semis;
        pendPitch_ = (float) std::pow (2.0, (double) semis / 12.0);
        pendReverse_ = false;
        pendHole_    = false;                                          // fb143 — Roll punch-ins always sound

        // the fire nonce: a locked slot replays its ENTIRE internal dice (true déjà vu)
        pendNonce_ = kUse ^ (slot * 2246822519u + 0x165667B1u);
        pendStep16_ = (float) (((step % 16) + 16) % 16);

        pendingFire_ = true;
    }

    int effDiv (int fi) const noexcept
    { int d = ext_.fxGrid[fi]; return d < 0 ? 0 : (d > kArpRateRichN ? kArpRateRichN : d); }
    // clockClass: 0 sync · 1 free · 2 Roll (prefers Roll-assigned, falls back to any; divKey -1 = any)
    GlitchFx pickFxExt (uint32_t kUse, uint32_t slot, int clockClass, int divKey) const noexcept
    {
        int readers[6]; int nr = 0;
        const int rlist[6] = { (int) GlitchFx::Repeat, (int) GlitchFx::Reverse, (int) GlitchFx::TapeStop,
                               (int) GlitchFx::Pitch,  (int) GlitchFx::Freeze,  (int) GlitchFx::Scatter };
        for (int pass = 0; pass < 2; ++pass)
        {
            nr = 0;
            for (int i = 0; i < 6; ++i)
            {
                if (! ext_.en[rlist[i]]) continue;
                const int tg = ext_.fxTrig[rlist[i]];
                const bool want = (clockClass == 2) ? (pass == 0 ? tg == 2 : true)   // Roll: its own first
                                 : (tg == clockClass && effDiv (rlist[i]) == divKey);  // groups: exact match
                if (want) readers[nr++] = rlist[i];
            }
            if (nr > 0)
            {
                const int pick = (int) (arpHash01 (kUse, slot, 0xB1u) * (float) nr);
                return (GlitchFx) readers[pick >= nr ? nr - 1 : pick];
            }
            if (clockClass != 2) break;                    // grids never fall back across groups
        }
        // overlays alone = main (they follow their own Trig + Grid assignment too)
        const int gI = (int) GlitchFx::Gate, cI = (int) GlitchFx::Crush;
        const bool okG = (clockClass == 2) ? true : (ext_.fxTrig[gI] == clockClass && effDiv (gI) == divKey);
        const bool okC = (clockClass == 2) ? true : (ext_.fxTrig[cI] == clockClass && effDiv (cI) == divKey);
        if (ext_.en[gI] && okG) return GlitchFx::Gate;
        if (ext_.en[cI] && okC) return GlitchFx::Crush;
        return (GlitchFx) (-1);
    }

    // Roll: quantize the punch-in to the Quant grid (next boundary from now)
    void armRoll (double p) noexcept
    {
        static const double QB[4] = { 1.0, 0.5, 0.25, 0.125 };
        const double qb = QB[ext_.quantIdx < 0 ? 0 : (ext_.quantIdx > 3 ? 3 : ext_.quantIdx)];
        rollB_ = (std::floor (p / qb) + 1.0) * qb;
        if (rollB_ - p > qb * 0.98) rollB_ = std::floor (p / qb) * qb + qb;   // keep it snappy
        rollArmed_ = true;
    }
    void fireRoll (float gate, double stepSamp) noexcept
    {
        ++rollN_;
        const uint32_t k = 0x9011E4A7u ^ ((uint32_t) rollN_ * 2654435761u);
        GlitchFx fx = pickFxExt (k, 0x77u, 2, -1);
        if ((int) fx < 0) return;
        latchFireExt (fx, k, 0x77u, gate, 0.f, 0.f, stepSamp, nextStep_);
    }

    void publishLevel (long long step) noexcept
    {
        if (lvlN_ > 0)
        {
            const int s = (int) ((((step - 1) % 16) + 16) % 16);
            lvl16_[s] = (float) std::sqrt (lvlAcc_ / (double) lvlN_);
        }
        lvlAcc_ = 0.0; lvlN_ = 0;
    }

    void commitPending() noexcept
    {
        gFx_ = pendFx_; gStartW_ = pendStartW_;
        gLen_ = pendGLen_; gDur_ = pendGDur_;
        latchedPitch_ = pendPitch_; latchedReverse_ = pendReverse_;
        holeFire_ = extOn_ && pendHole_;                             // fb143 — this fire is a hole
        gCounter_ = 0; readPosF_ = 0.0; shCnt_ = 0; shHold_ = 0.f; shHoldR_ = 0.f; lastSubGrain_ = -1; lastWrap_ = -1;
        decayLatch_ = arpClamp01 (ext_.decay); gateLatchPer_ = 0;   // fb204 — Decay latches per FIRE; gate re-latches
        seamInterval_ = extOn_ ? computeSeamIntervalExt() : computeSeamInterval();
        xfLen_ = fade_;
        if (extOn_ && gFx_ == GlitchFx::Freeze)                       // Melt widens the smear
        {
            const double half = (double) seamInterval_ * 0.5 - (double) fade_;
            if (half > 0.0) xfLen_ = fade_ + (int) ((double) arpClamp01 (ext_.frzMelt) * half);
        }
        if (xfLen_ > seamInterval_ / 2) xfLen_ = seamInterval_ / 2;
        if (xfLen_ < 1) xfLen_ = 1;
        xf_ = 0; haveLast_ = false;
        if (extOn_)
        {
            fireNonce_ = pendNonce_; fireStep16_ = pendStep16_; ++fireCount_;
            curFlt_ = ext_.fxFlt[(int) gFx_ & 7];             // fb125 — the FIRE latches its own
            curPan_ = ext_.fxPan[(int) gFx_ & 7];             //         Out routing (no mid-fire jumps)
            phF_ = 0.0; lenCur_ = (double) gLen_; lvlCur_ = 1.f; driftOff_ = 0.0; wrapN_ = 0;
            chunkOff_ = 0.0; semisAcc_ = (double) pendSemis_;
            prTgt_ = latchedPitch_;
            prCur_ = (ext_.pitGlide > 0.01f && gFx_ == GlitchFx::Pitch) ? 1.0 : (double) prTgt_;
            ovCnt_ = 0; ovL_ = 0.f; ovR_ = 0.f;
            sctRate_ = 1.0; chanOffR_ = 0.0;
            gGain_ = 1.f; gGainPrev_ = 1.f; gGainTail_ = 1.f;
            if (gFx_ == GlitchFx::Scatter)
            {
                newScatterGrain (0);
                // Width: R reads a FIXED per-fire lag deeper into the past — decorrelated
                // stereo scatter with zero extra seams (the offset never moves mid-fire)
                if (ext_.sctWidth > 0.005f)
                    chanOffR_ = -(double) hashDie (0xA110u, 0x5C2u)
                                * (double) arpClamp01 (ext_.sctWidth) * (double) seamInterval_ * 2.0;
            }
        }
        // fb142 — PING latches per fire (click-safe: gains change only at the commit seam)
        pingFlip_ = ! pingFlip_;
        if (pingAmt_ > 0.001f)
        {
            const float off = (pingFlip_ ? 1.0f : -1.0f) * pingAmt_;          // -1..1 across fires
            const float ang = (0.5f + off * 0.45f) * 1.57079632679f;          // keep off full rails
            pingL_ = std::cos (ang) / 0.70710678f;                            // center = unity
            pingR_ = std::sin (ang) / 0.70710678f;
        }
        else { pingL_ = 1.0f; pingR_ = 1.0f; }
        glitchActive_ = true;
        pendingFire_ = false;
    }

    int computeSeamInterval() const noexcept
    {
        switch (gFx_)
        {
            case GlitchFx::Repeat:
            case GlitchFx::Reverse:  return gLen_ > 1 ? gLen_ : 1;
            case GlitchFx::Pitch:    { const double pr = latchedPitch_ > 0.01f ? latchedPitch_ : 1.0; int s = (int) (gLen_ / pr); return s > 1 ? s : 1; }
            case GlitchFx::Freeze:   { int g = (int) std::lround (sr_ * (double) freezeMs_ * 0.001); if (g < 2) g = 2; if (g > gLen_) g = gLen_; return g; }
            case GlitchFx::Scatter:  { int g = (int) std::lround (sr_ * (double) scatterGrainMs_ * 0.001); if (g < 2) g = 2; return g; }
            case GlitchFx::TapeStop:
            case GlitchFx::Gate:
            case GlitchFx::Crush:
            default:                 return 1 << 30;        // no internal read seam (direct / monotonic)
        }
    }

    int computeSeamIntervalExt() const noexcept
    {
        switch (gFx_)
        {
            case GlitchFx::Repeat:
            case GlitchFx::Reverse:  return gLen_ > 1 ? gLen_ : 1;
            case GlitchFx::Pitch:    { const double pr = latchedPitch_ > 0.01f ? (double) latchedPitch_ : 1.0;
                                       int s = (int) ((double) gLen_ / (pr > 1.0 ? pr : 1.0)); return s > 1 ? s : 1; }
            case GlitchFx::Freeze:   { int g = grainSampExt (15.0 + (double) arpClamp01 (ext_.frzSize) * 185.0);
                                       if (g > gLen_) g = gLen_; return g > 1 ? g : 1; }
            case GlitchFx::Scatter:  return grainSampExt (12.0 + (double) arpClamp01 (ext_.sctSize) * 108.0);
            default:                 return 1 << 30;
        }
    }
    int grainSampExt (double ms) const noexcept
    { int g = (int) std::lround (sr_ * ms * 0.001); return g < 2 ? 2 : g; }

    bool rollFire (int slot, float chance) noexcept
    {
        if (dejavu_ > 0.f && slot >= 0 && slot < 64)
        {
            if (lockFireValid_[slot] && rng_.unit() < dejavu_) return lockFire_[slot];
            const bool f = rng_.unit() < chance;
            lockFire_[slot] = f; lockFireValid_[slot] = true; return f;
        }
        return rng_.unit() < chance;
    }

    GlitchFx pickFx() noexcept
    {
        float total = 0.f; for (int i = 0; i < kGlitchFxN; ++i) if (enabled_[i]) total += (weight_[i] > 0.f ? weight_[i] : 1.f);
        if (total <= 0.f) return GlitchFx::Repeat;
        float r = rng_.unit() * total;
        for (int i = 0; i < kGlitchFxN; ++i) if (enabled_[i]) { r -= (weight_[i] > 0.f ? weight_[i] : 1.f); if (r <= 0.f) return (GlitchFx) i; }
        return GlitchFx::Repeat;
    }

    float interp (const std::vector<float>& b, double pos) const noexcept
    {
        double fp = std::floor (pos);
        const double frac = pos - fp;
        const int a = wrap ((long long) fp), c = wrap ((long long) fp + 1);
        return b[(size_t) a] + (float) frac * (b[(size_t) c] - b[(size_t) a]);
    }
    int wrap (long long i) const noexcept { long long m = i % cap_; if (m < 0) m += cap_; return (int) m; }

    // per-effect read. true => buffer-read effect (sets idx, step, seam);
    // false => direct (Gate/Crush set wet). step = per-sample idx increment in normal
    // motion; seam = true exactly at a grain wrap (so the seam-crossfade fires once).
    bool fxRead (int t, float dryL, float dryR, double& idx, double& step, bool& seam, float& wetL, float& wetR) noexcept
    {
        step = 0.0; seam = false;
        switch (gFx_)
        {
            case GlitchFx::Repeat:
            {
                const bool rev = latchedReverse_;
                const int  ph  = (gLen_ > 0) ? (t % gLen_) : 0;
                seam = (t > 0 && ph == 0);
                idx  = rev ? ((double) gStartW_ - 1.0 - (double) ph)
                           : ((double) gStartW_ - (double) gLen_ + (double) ph);
                step = rev ? -1.0 : 1.0;
                return true;
            }
            case GlitchFx::Reverse:
            {
                const int ph = (gLen_ > 0) ? (t % gLen_) : 0;
                seam = (t > 0 && ph == 0);
                idx  = (double) gStartW_ - 1.0 - (double) ph;
                step = -1.0;
                return true;
            }
            case GlitchFx::TapeStop:
            {
                // Physically-correct deceleration. A coasting (power-cut) machine slows
                // EXPONENTIALLY (fast initial pitch drop + long low tail), not linearly.
                // tapeCurve_ blends linear (braked stop) -> exponential coast (Kilohearts "Curve").
                const double T = (double) (gDur_ > 0 ? gDur_ : 1);
                const double x = (double) t / T;                       // 0..1 progress
                double rate;
                if (tapeCurve_ < 0.01)                                  // ~linear: braked stop
                    rate = 1.0 - x;
                else                                                   // normalized exp coast: 1 at x=0, 0 at x=1
                {
                    const double k = (double) tapeCurve_;
                    const double ek = std::exp (-k);
                    rate = (std::exp (-k * x) - ek) / (1.0 - ek);
                }
                if (rate < 0.0) rate = 0.0;
                idx  = (double) gStartW_ - (double) gLen_ + readPosF_;
                readPosF_ += rate;
                step = rate; seam = false;            // monotonic, no wrap
                return true;
            }
            case GlitchFx::Gate:
            {
                const int period = (gLen_ > 1) ? (gLen_ / (gateDiv_ < 1 ? 1 : gateDiv_)) : 1;
                const int per = period < 1 ? 1 : period;
                const int ph  = t % per;
                const int half = per / 2 > 0 ? per / 2 : 1;
                int eg = per / 8 + 1;
                if (eg > half) eg = half;
                float g;
                if (ph < half)                      // open window, cosine edges BOTH sides
                {
                    if (ph < eg)              g = 0.5f - 0.5f * std::cos (3.14159265f * (float) ph / (float) eg);
                    else if (ph > half - eg)  g = 0.5f + 0.5f * std::cos (3.14159265f * (float) (ph - (half - eg)) / (float) eg);
                    else                      g = 1.f;
                }
                else g = 0.f;
                wetL = dryL * g; wetR = dryR * g;
                return false;
            }
            case GlitchFx::Pitch:
            {
                // continuous read within the grain, advancing by pitch, wrapping at gLen
                const double pr  = latchedPitch_ > 0.01f ? (double) latchedPitch_ : 1.0;
                const double pos = (double) t * pr;
                const int    wrapIx = (gLen_ > 0) ? (int) std::floor (pos / (double) gLen_) : 0;
                seam = (t > 0 && wrapIx != lastWrap_);
                lastWrap_ = wrapIx;
                const double within = pos - (double) wrapIx * (double) gLen_;
                idx  = (double) gStartW_ - (double) gLen_ + within;
                step = pr;
                return true;
            }
            case GlitchFx::Crush:
            {
                if (shCnt_ <= 0) { shHold_ = dryL; shHoldR_ = dryR; shCnt_ = crushDown_; }
                --shCnt_;
                const float levels = (float) ((1 << crushBits_) - 1);
                wetL = std::round (shHold_  * 0.5f * levels) / (0.5f * levels);
                wetR = std::round (shHoldR_ * 0.5f * levels) / (0.5f * levels);
                return false;
            }
            case GlitchFx::Freeze:
            {
                int grain = (int) std::lround (sr_ * (double) freezeMs_ * 0.001);
                if (grain < 2) grain = 2;
                if (grain > gLen_) grain = gLen_;
                const int ph = t % grain;
                seam = (t > 0 && ph == 0);
                idx  = (double) gStartW_ - (double) grain + (double) ph;
                step = 1.0;
                return true;
            }
            case GlitchFx::Scatter:
            {
                int grain = (int) std::lround (sr_ * (double) scatterGrainMs_ * 0.001);
                if (grain < 2) grain = 2;
                int win = (int) std::lround (sr_ * (double) scatterWinMs_ * 0.001);
                if (win < grain + 1) win = grain + 1;
                if (win > cap_ - 2) win = cap_ - 2;
                const int sub = t / grain;
                seam = (t > 0 && sub != lastSubGrain_);
                if (sub != lastSubGrain_) { lastSubGrain_ = sub; scatterBase_ = (int) (rng_.below (win - grain)); }
                const int ph = t % grain;
                idx  = (double) gStartW_ - (double) win + (double) scatterBase_ + (double) ph;
                step = 1.0;
                return true;
            }
        }
        return false;
    }

    // ── fb115 ext readers — double-phase accumulators (Bend warble, accel rolls),
    //    every die a hash of (fireNonce_, wrap/grain index, PURPOSE). ──────────────
    float hashDie (uint32_t idx, uint32_t purpose) const noexcept { return arpHash01 (fireNonce_, idx, purpose); }
    double bendMul() const noexcept
    { return ext_.bend > 0.001f ? 1.0 + (double) ext_.bend * 0.06 * std::sin (bendPh_) : 1.0; }

    void newScatterGrain (int sub) noexcept
    {
        const int grain = seamInterval_;
        int win = grainSampExt (250.0) * 2;             // 500 ms scatter window
        if (win < grain * 4) win = grain * 4;
        if (win > cap_ - 2)  win = cap_ - 2;
        scatterBase_ = (int) ((double) hashDie ((uint32_t) sub, 0x5C1u) * (double) (win - grain));
        // Amt = density: below the odds the grain is a HOLE (gain 0, seam-crossfaded)
        gGainPrev_ = gGain_;
        gGain_ = hashDie ((uint32_t) sub, 0x5C3u) >= arpClamp01 (ext_.sctAmt) ? 0.f : 1.f;
        sctRate_ = 1.0 + ((double) hashDie ((uint32_t) sub, 0x5C4u) - 0.5) * (double) arpClamp01 (ext_.sctVary) * 0.6;
        sctWin_  = win;
    }

    bool fxReadExt (int t, float dryL, float dryR, double& idx, double& step, bool& seam, float& wetL, float& wetR) noexcept
    {
        step = 0.0; seam = false;
        switch (gFx_)
        {
            case GlitchFx::Repeat:
            {
                // accelerating roll: each wrap can shrink the loop (Speed), fade it
                // (Fade) and drift the start (Vary) — all hash dice off the fire nonce.
                const double r = bendMul();
                phF_ += r;
                if (phF_ >= lenCur_)
                {
                    phF_ = std::fmod (phF_, lenCur_ > 1.0 ? lenCur_ : 1.0);
                    ++wrapN_; seam = true; gGainPrev_ = gGain_;
                    if (ext_.repSpeed > 0.001f)
                    {
                        lenCur_ *= (1.0 - (double) arpClamp01 (ext_.repSpeed) * 0.22);
                        const double mn = std::max (96.0, (double) gLen_ * 0.01);
                        if (lenCur_ < mn) lenCur_ = mn;
                    }
                    if (ext_.repFade > 0.001f) { lvlCur_ *= (1.f - arpClamp01 (ext_.repFade) * 0.55f); gGain_ = lvlCur_; }
                    if (ext_.repVary > 0.001f)
                        driftOff_ = ((double) hashDie ((uint32_t) wrapN_, 0xD1u) - 0.5)
                                    * (double) arpClamp01 (ext_.repVary) * (double) gLen_;
                }
                idx  = (double) gStartW_ - (double) gLen_ + driftOff_ + phF_;
                step = r;
                return true;
            }
            case GlitchFx::Reverse:
            {
                const double r = bendMul();
                phF_ += r;
                if (phF_ >= lenCur_)
                {
                    phF_ = std::fmod (phF_, lenCur_ > 1.0 ? lenCur_ : 1.0);
                    ++wrapN_; seam = true; gGainPrev_ = gGain_;
                    if (ext_.revFade > 0.001f) { lvlCur_ *= (1.f - arpClamp01 (ext_.revFade) * 0.55f); gGain_ = lvlCur_; }
                    // Snap: odds the chunk re-anchors at the fire point; otherwise it
                    // walks BACKWARD through the memory (river reverse). Sprd scatters it.
                    if (hashDie ((uint32_t) wrapN_, 0xE1u) >= arpClamp01 (ext_.revSnap))
                        chunkOff_ += (double) gLen_;
                    if (ext_.revSprd > 0.001f)
                        chunkOff_ += (double) hashDie ((uint32_t) wrapN_, 0xE2u)
                                     * (double) arpClamp01 (ext_.revSprd) * (double) gLen_ * 2.0;
                    const double maxOff = (double) cap_ * 0.5 - (double) gLen_;
                    if (chunkOff_ > maxOff) chunkOff_ = 0.0;           // ran out of past — re-anchor
                }
                idx  = (double) gStartW_ - 1.0 - chunkOff_ - phF_;
                step = -r;
                return true;
            }
            case GlitchFx::TapeStop:
            {
                // Depth = how far it slows (1 = full stop). Time = when the stop lands
                // within the hold. Spin = wind back UP afterwards (tape restart).
                const double T  = (double) (gDur_ > 0 ? gDur_ : 1);
                const double tS = T * (0.25 + 0.75 * (double) arpClamp01 (ext_.tapeTime));
                const double sp = (double) arpClamp01 (ext_.tapeSpin);
                const double lo = 1.0 - (double) arpClamp01 (ext_.tapeDepth);
                const double k  = (double) ext_.tapeCurve * 6.0;
                double rate;
                if ((double) t < tS)
                {
                    const double x = (double) t / tS;
                    double c;
                    if (k < 0.06) c = 1.0 - x;
                    else { const double ek = std::exp (-k); c = (std::exp (-k * x) - ek) / (1.0 - ek); }
                    rate = lo + (1.0 - lo) * (c < 0.0 ? 0.0 : c);
                }
                else if (sp > 0.001)
                {
                    const double xr = ((double) t - tS) / std::max (1.0, (T - tS) * sp);
                    const double c  = xr >= 1.0 ? 1.0 : (0.5 - 0.5 * std::cos (3.14159265358979 * xr));
                    rate = lo + (1.0 - lo) * c;                        // spin back up to speed
                }
                else rate = lo;
                rate *= bendMul();
                if (rate < 0.0) rate = 0.0;
                idx  = (double) gStartW_ - (double) gLen_ + readPosF_;
                readPosF_ += rate;
                step = rate; seam = false;
                return true;
            }
            case GlitchFx::Gate:
            {
                gateApply (t, dryL, dryR, wetL, wetR);
                return false;
            }
            case GlitchFx::Pitch:
            {
                // Walk: every wrap steps the pitch FURTHER by the shift (stutter riser),
                // capped ±24 semis. Glide slews the ratio (80 ms perceptual cap, fb109).
                const double pos = phF_;
                const int wrapIx = (gLen_ > 0) ? (int) std::floor (pos / (double) gLen_) : 0;
                seam = (t > 0 && wrapIx != lastWrap_);
                if (wrapIx != lastWrap_ && t > 0)
                {
                    lastWrap_ = wrapIx;
                    if (ext_.pitWalk > 0.001f && pendSemis_ != 0)
                    {
                        semisAcc_ += (double) pendSemis_ * (double) arpClamp01 (ext_.pitWalk);
                        if (semisAcc_ >  24.0) semisAcc_ =  24.0;
                        if (semisAcc_ < -24.0) semisAcc_ = -24.0;
                        prTgt_ = (float) std::pow (2.0, std::round (semisAcc_) / 12.0);
                    }
                    // Jump: hash odds of an extra octave leap this wrap (explicit dice knob)
                    if (ext_.pitJump > 0.001f && hashDie ((uint32_t) wrapIx, 0xA7u) < arpClamp01 (ext_.pitJump))
                        prTgt_ = (float) ((double) prTgt_ * (hashDie ((uint32_t) wrapIx, 0xA8u) < 0.5f ? 2.0 : 0.5));
                }
                else lastWrap_ = wrapIx;
                if (ext_.pitGlide > 0.01f)
                {
                    const double tau = (double) arpClamp01 (ext_.pitGlide) * 0.080 * sr_;
                    const double a = tau > 1.0 ? 1.0 - std::exp (-1.0 / tau) : 1.0;
                    prCur_ += a * ((double) prTgt_ - prCur_);
                }
                else prCur_ = (double) prTgt_;
                const double r = prCur_ * bendMul();
                const double within = pos - (double) ((gLen_ > 0) ? std::floor (pos / (double) gLen_) : 0.0) * (double) gLen_;
                idx  = (double) gStartW_ - (double) gLen_ + within;
                phF_ += (r < 0.05 ? 0.05 : r);
                step = r;
                return true;
            }
            case GlitchFx::Crush:
                return false;                          // wet = dry passthrough; extPost crushes it
            case GlitchFx::Freeze:
            {
                const int grain = seamInterval_;
                const int ph = t % grain;
                seam = (t > 0 && ph == 0);
                if (seam)
                {
                    ++wrapN_;
                    if (ext_.frzSpray > 0.001f)
                        driftOff_ = -(double) hashDie ((uint32_t) wrapN_, 0xF5u)
                                    * (double) arpClamp01 (ext_.frzSpray) * sr_ * 0.5;   // up to 500 ms back
                }
                idx  = (double) gStartW_ - (double) grain + (double) ph + driftOff_;
                step = bendMul();
                // Shine: a windowed octave-up layer. The 2x read runs LINEARLY over the
                // last 2*grain of history so it never wraps mid-grain — the old fmod wrap
                // landed at the raised-cosine's PEAK, a full-level discontinuity every
                // grain (fb174). Both edges still sit at window zero, and the gain slews
                // (2.5ms one-pole, the slew law) so knob moves can't step either.
                const float shineTgt = ext_.frzShine > 0.005f ? arpClamp01 (ext_.frzShine) * 0.7f : 0.f;
                shineG_ += (shineTgt - shineG_) * kHole_;
                if (shineG_ > 1.0e-4f)
                {
                    const double wnd = 0.5 - 0.5 * std::cos (6.28318530718 * (double) ph / (double) grain);
                    const double p2  = (double) gStartW_ - 2.0 * (double) grain + 2.0 * (double) ph + driftOff_;
                    shimL_ = (float) (wnd * (double) interp (bufL_, p2)) * shineG_;
                    shimR_ = (float) (wnd * (double) interp (bufR_, p2)) * shineG_;
                }
                else { shimL_ = shimR_ = 0.f; }
                return true;
            }
            case GlitchFx::Scatter:
            {
                const int grain = seamInterval_;
                const int sub = t / grain;
                seam = (t > 0 && sub != lastSubGrain_);
                if (sub != lastSubGrain_) { lastSubGrain_ = sub; newScatterGrain (sub); phF_ = 0.0; }
                const double r = sctRate_ * bendMul();
                const double baseL = (double) scatterBase_;
                idx  = (double) gStartW_ - (double) sctWin_ + baseL + phF_;
                phF_ += r;
                step = r;
                return true;
            }
        }
        return false;
    }

    void gateApply (int t, float inL, float inR, float& outL, float& outR) noexcept
    {
        static const int DIVS[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };
        // fb204 — RATE + NUDGE latch at the period boundary (LADDER-adjacent): re-dividing
        // mid-period slammed the phase — a full-scale gate jump on every modulated block.
        if (gateLatchPer_ <= 1 || (t % gateLatchPer_) == 0)
        {
            const int div = DIVS[(int) std::lround ((double) arpClamp01 (ext_.gateRate) * 7.0)];
            const int per0 = (gLen_ > 1 ? gLen_ : 2) / div;
            gateLatchPer_   = per0 < 2 ? 2 : per0;
            gateLatchNudge_ = (int) ((double) arpClamp01 (ext_.gateNudge) * (double) gateLatchPer_);
        }
        const int per = gateLatchPer_;
        const int ph  = (t + gateLatchNudge_) % per;
        const int half = per / 2 > 0 ? per / 2 : 1;
        // Shape tops out at eg = half/2 — attack and release just meeting (a full
        // raised-cosine pulse). The old ceiling (eg = half) let the attack SWALLOW the
        // release above Shape ~0.5: the else-if handed 1.0 straight to the release's
        // tail — a one-sample 1 -> 0 slam every period (fb174).
        int eg = (int) ((double) per * (0.02 + (double) arpClamp01 (ext_.gateShape) * 0.23)) + 1;
        const int egFloor = (int) std::lround (sr_ * 0.0005);        // 0.5ms edge floor: at the
        if (eg < egFloor) eg = egFloor;                              // fastest divisions 2% of the
        if (eg > half / 2) eg = half / 2;                            // period is ~7 samp = a rasp,
        if (eg < 1) eg = 1;                                          // not a snap (fb174)
        float g;
        if (ph < half)
        {
            if (ph < eg)              g = 0.5f - 0.5f * std::cos (3.14159265f * (float) ph / (float) eg);
            else if (ph > half - eg)  g = 0.5f + 0.5f * std::cos (3.14159265f * (float) (ph - (half - eg)) / (float) eg);
            else                      g = 1.f;
        }
        else g = 0.f;
        gateAmtSm_ += (arpClamp01 (ext_.gateAmt) - gateAmtSm_) * kHole_;   // fb204 — glided depth
        g = 1.f - gateAmtSm_ + gateAmtSm_ * g;         // Amt = gate depth (floor level)
        outL = inL * g; outR = inR * g;
    }

    // overlays (Gate/Crush on a reader's wet), crush tone/amt, Shine layer, wet bus
    // (filter + pan), master Decay. Returns a scale for the wet mix amount.
    float extPost (bool isRead, float& wl, float& wr) noexcept
    {
        if (gFx_ == GlitchFx::Freeze) { wl += shimL_; wr += shimR_; }   // shim slews to 0 on its own (fb174)

        // CRUSH — as main (solo) or overlay on any other effect's wet
        if (ext_.en[(int) GlitchFx::Crush] || gFx_ == GlitchFx::Crush)
        {
            const bool asMain = (gFx_ == GlitchFx::Crush);
            if (asMain || isRead || gFx_ == GlitchFx::Gate)
            {
                const int bits = 16 - (int) std::lround ((double) arpClamp01 (ext_.crshBits) * 12.0);
                const int down = 1 + (int) std::lround ((double) (arpClamp01 (ext_.crshRate) * arpClamp01 (ext_.crshRate)) * 31.0);
                if (ovCnt_ <= 0) { ovL_ = wl; ovR_ = wr; ovCnt_ = down; }
                --ovCnt_;
                const float levels = (float) ((1 << bits) - 1);
                float cl = std::round (ovL_ * 0.5f * levels) / (0.5f * levels);
                float cr = std::round (ovR_ * 0.5f * levels) / (0.5f * levels);
                if (bits < 6)                                   // loudness guard: low-bit
                {                                               // quantize gets LOUDER — trim it
                    const float trim = std::pow (10.f, -0.025f * (float) (6 - bits));
                    cl *= trim; cr *= trim;
                }
                if (toneC_ < 0.9995f)
                { tone1L_ += toneC_ * (cl - tone1L_); cl = tone1L_;
                  tone1R_ += toneC_ * (cr - tone1R_); cr = tone1R_; }
                crshAmtSm_ += (arpClamp01 (ext_.crshAmt) - crshAmtSm_) * kHole_;   // fb204 — glided
                wl += (cl - wl) * crshAmtSm_; wr += (cr - wr) * crshAmtSm_;
            }
        }

        // GATE — overlay on a reader (or on Crush-main). Gate-as-main already applied.
        if (ext_.en[(int) GlitchFx::Gate] && gFx_ != GlitchFx::Gate)
            gateApply (gCounter_, wl, wr, wl, wr);

        // per-effect Out (fb125): the active fire's own Filter then Pan
        switch (curFlt_)
        {
            case 1:  lpL_ += lpC_ * (wl - lpL_); wl = lpL_;
                     lpR_ += lpC_ * (wr - lpR_); wr = lpR_; break;
            case 2:  hpL_ += hpC_ * (wl - hpL_); wl -= hpL_;
                     hpR_ += hpC_ * (wr - hpR_); wr -= hpR_;
                     lpL_ += lpC_ * (wl - lpL_); wl = lpL_;
                     lpR_ += lpC_ * (wr - lpR_); wr = lpR_; break;
            case 3:  hpL_ += hpC_ * (wl - hpL_); wl -= hpL_;
                     hpR_ += hpC_ * (wr - hpR_); wr -= hpR_; break;
            default: break;
        }
        if (curPan_ == 0)      wr *= 0.25f;
        else if (curPan_ == 2) wl *= 0.25f;

        // master Decay: the glitch eases back to DRY over the hold (fb204: latched per fire —
        // a mid-fire modulated Decay re-bent the curve with a step)
        if (decayLatch_ > 0.001f && gDur_ > 0)
            return std::exp (-3.5f * decayLatch_ * (float) gCounter_ / (float) gDur_);
        return 1.f;
    }

    static float flush (float x) noexcept
    {
        if (! (x == x)) return 0.f;
        if (x >  8.f) return 8.f;
        if (x < -8.f) return -8.f;
        if (x < 1.0e-20f && x > -1.0e-20f) return 0.f;
        return x;
    }

    // -- state --------------------------------------------------------------------
    std::vector<float> bufL_, bufR_;
    int      cap_ = 0, w_ = 0, written_ = 0, fade_ = 2;
    double   sr_ = 44100.0;

    bool     enabled_ [kGlitchFxN] = { true, true, true, true, true, true, true, true };
    float    weight_  [kGlitchFxN] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    float    mix_ = 1.0f, holdSteps_ = 1.0f, dejavu_ = 0.0f;
    int      outMode_ = 0;                                          // fb142 — 0 Mix · 1 Cut · 2 Gate
    float    pingAmt_ = 0.0f, pingL_ = 1.0f, pingR_ = 1.0f;         // fb142 — per-fire stereo bounce
    bool     pingFlip_ = false;
    int      loopLen_ = 8;
    float    pitchRatio_ = 2.0f;
    float    tapeCurve_ = 3.5f;        // tape-stop deceleration curve: 0 linear -> ~3.5 exponential coast (default tape)
    int      crushBits_ = 8, crushDown_ = 4, gateDiv_ = 4;
    float    freezeMs_ = 50.f, scatterGrainMs_ = 40.f, scatterWinMs_ = 500.f;
    uint32_t seed_ = 0x6117C40Du;

    // fb115 ext
    GlitchExtParams ext_;
    bool     extOn_ = false;
    bool     rollReq_ = false, rollArmed_ = false;
    double   rollB_ = 0.0; long long rollN_ = 0;
    uint32_t fireNonce_ = 1u, pendNonce_ = 1u;
    int      pendSemis_ = 0;
    float    pendStep16_ = 0.f, fireStep16_ = 0.f;
    long long fireCount_ = 0;
    // fb143 BURST — per fire-group streak memory (the master + every fb127 group)
    static constexpr long long kBNever = -(1LL << 62);
    static int burstKey (int clockClass, int divKey) noexcept
    {
        const int c = clockClass < 0 ? 0 : (clockClass > 2 ? 2 : clockClass);
        const int d = divKey    < -1 ? -1 : (divKey    > 19 ? 19 : divKey);
        return (d + 1) * 3 + c;                                        // 0..62
    }
    long long bStep_[63]; uint32_t bNonce_[63] {}; int bFx_[63] {}; int bHole_[63] {};
    // fb143 DROP — hole-fire state + the 2.5ms wet glide (the slew law)
    bool  pendHole_ = false, holeFire_ = false;
    float holeG_ = 1.f, kHole_ = 0.02f;
    float mixSm_ = 1.f, gateAmtSm_ = 0.f, crshAmtSm_ = 0.f, decayLatch_ = 0.f;   // fb204 — glide/latch state
    int   gateLatchPer_ = 0, gateLatchNudge_ = 0;                                 // fb204 — gate period latch
    double   phF_ = 0.0, lenCur_ = 0.0, driftOff_ = 0.0, chunkOff_ = 0.0;
    float    lvlCur_ = 1.f;
    int      wrapN_ = 0;
    double   prCur_ = 1.0; float prTgt_ = 1.0f; double semisAcc_ = 0.0;
    float    shimL_ = 0.f, shimR_ = 0.f, shineG_ = 0.f;
    int      ovCnt_ = 0; float ovL_ = 0.f, ovR_ = 0.f;
    double   bendPh_ = 0.0, bendInc_ = 0.0;
    float    lpC_ = 1.f, hpC_ = 1.f, toneC_ = 1.f;
    float    lpL_ = 0.f, lpR_ = 0.f, hpL_ = 0.f, hpR_ = 0.f, tone1L_ = 0.f, tone1R_ = 0.f;
    double   sctRate_ = 1.0, chanOffR_ = 0.0; int sctWin_ = 1;
    int      curFlt_ = 0, curPan_ = 1;                       // fb125 — latched per fire
    float    gGain_ = 1.f, gGainPrev_ = 1.f, gGainTail_ = 1.f;
    double   lvlAcc_ = 0.0; int lvlN_ = 0; float lvl16_[16] = { 0.f };
    float    vizStepF_ = 0.f, vizLoopF_ = 0.f, outLvl_ = 0.f;

    // active glitch instance
    GlitchFx gFx_ = GlitchFx::Repeat;
    bool     glitchActive_ = false;
    int      gCounter_ = 0, gDur_ = 0, gLen_ = 0, gStartW_ = 0;
    double   readPosF_ = 0.0;
    float    shHold_ = 0.f, shHoldR_ = 0.f; int shCnt_ = 0;
    int      lastSubGrain_ = -1, scatterBase_ = 0, lastWrap_ = -1;
    float    latchedPitch_ = 2.0f; bool latchedReverse_ = false;

    // pending (latched at boundary, committed after the duck)
    bool     pendingFire_ = false;
    GlitchFx pendFx_ = GlitchFx::Repeat;
    int      pendStartW_ = 0, pendGLen_ = 0, pendGDur_ = 0;
    float    pendPitch_ = 2.0f; bool pendReverse_ = false;

    // click-free render spine
    float    wetLevel_ = 0.f, wetTarget_ = 0.f;
    int      xf_ = 0, xfLen_ = 2, seamInterval_ = 1 << 30;
    double   lastIdx_ = 0.0, lastStep_ = 0.0, tailIdx_ = 0.0, tailStep_ = 0.0; bool haveLast_ = false;

    // deja-vu fire lock
    bool     lockFire_[64] = { false }; bool lockFireValid_[64];

    // clock
    bool     haveClock_ = false; long long nextStep_ = 0; double nextBoundary_ = 0.0, freePpq_ = 0.0;
    float    lastBeats_ = -1.f;                    // fb116: re-anchor the step clock on any grid change
    // fb127 — fire groups: one boundary tracker per active (clock, division) pair
    struct GridTrk { bool have = false; long long step = 0; double bnd = 0.0; float beats = -1.f; };
    struct ActiveG { GridTrk* trk = nullptr; int cls = 0, divKey = 0; double off = 0.0, stepSamp = 1.0; };
    GridTrk  trkS_[kArpRateRichN + 1], trkF_[kArpRateRichN + 1];
    ActiveG  activeG_[kGlitchFxN]; int nActiveG_ = 0;
    double   freePpqF_ = 0.0;

    ArpRng   rng_;
};

} // namespace wc
