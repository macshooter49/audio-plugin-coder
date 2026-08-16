#pragma once

#include "TapeProcessor.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <random>

// ═══════════════════════════════════════════════════════════════════════════════
//  TapeFxEngine.h — fb365. ONE instance of the FX-rack TAPE device.
//
//  Max, 2026-08-15: "we already have DSP for cassette wire and studio so you can
//  just do the same thing… the three DSP that's on everyone just bring those up to
//  the front and make sure those are solid, same effect, same everything."
//
//  So the CHARACTER is not reimplemented. This engine owns a TapeProcessor per
//  channel — the identical StudioMachine / CassetteMachine / WireMachine that the
//  front page has always used, driven by the identical three controls each machine
//  exposes (Studio: Sculpt/Weave/Tilt · Cassette + Wire: Wow/Saturate/Hiss). Type
//  switching rides TapeProcessor's own 75 ms crossfade, so the fb345 char-switch
//  fade law is honoured by construction rather than re-derived.
//
//  What is NEW is the TRANSPORT around it — the thing that makes it a tape MACHINE
//  and not a saturator:
//
//    in → Drive → [ machine character ] ─┬─────────────────────────────→ wet direct
//                                        │
//                        ┌───────────────┴──── recorded to the loop ──────┐
//                        │  ring buffer, read by a head at readPos_       │
//                        │  4 heads at ¼ · ½ · ¾ · 1 of the loop          │
//                        │  feedback = last live head × Repeats           │
//                        │    → per-pass saturation                       │
//                        │    → per-pass gap loss (HF walks down)         │
//                        │    → head bump (compounds, like the real thing)│
//                        │    → dropouts / level walk (Age)               │
//                        └───────────────────────────────────────────────-┘
//
//  🔑 THE TRANSPORT IS A REAL TRANSPORT. readPos_ advances at `speed`, not at 1.
//  That single fact buys three things honestly instead of faking them:
//    · Stop is a real tape stop — the pitch falls as the capstan slows, because the
//      read head genuinely decelerates. Motor sets how long the flywheel takes.
//    · Wow and flutter are a modulation of the read RATE, so they accumulate over
//      repeats exactly as they do on tape (repeat 6 wobbles ~6× repeat 1).
//    · The visualizer is not decorated with a sine — the reels are handed the real
//      integrated transport angle, so they slow and stop when the tape does, and
//      the tape/wire wobble is the same number the DSP just used.
//
//  ⚠️ NOTHING FREE-RUNS (fb325). Hiss is gated by an input-presence envelope, so a
//  powered, routed, silent Tape is SILENT. The gate does not touch the echo tail —
//  it scales only the noise term the machine adds.
// ═══════════════════════════════════════════════════════════════════════════════

namespace tw {

class TapeFxEngine
{
public:
    static constexpr int kHeads = 4;
    static constexpr double kMaxDelaySec = 8.0;      // 4 bars at 120 BPM

    // ── the tape formulation (back dropdown 1) ────────────────────────────────
    //  Fresh · Ferric · Chrome · Vintage · Worn · Chewed · Hot · Cold
    //  Every field is a real physical consequence of the stock, not a preset of
    //  unrelated numbers: hf/lf are the play-EQ, sat is how early it compresses,
    //  noise is the floor, drop is oxide shedding, wander is transport slip,
    //  azi is azimuth error (an HF loss + a sub-sample skew on ONE channel, which
    //  is what actually makes a mis-aligned head sound wide and phasey), comp is
    //  the tape's own level pumping.
    struct CharSpec { float hf, lf, sat, noise, drop, wander, azi, comp; };

    // fb367 — TWO tape machines, and neither of them is the Harmonic Sculptor.
    // Max: "studio and wire and even the tube, these are literally just distortions.
    // They're not tape. The only tape that we even have is cassette... the only two tape
    // modes that we need is wire and cassette. I don't need studio anymore" — and then
    // "replace the wire with studio, the name at least."  So the device now offers
    // STUDIO (which IS WireMachine — real transport, real wow, real dropouts) and
    // CASSETTE. StudioMachine bypasses wow AND hiss by design (see TapeMachines.h: both
    // return the input untouched) and is purely the sculptor, i.e. a distortion. The rack
    // already has a Distortion device; it does not need a second one wearing a tape badge.
    //   type 0 = Studio  -> machine 2 (Wire)
    //   type 1 = Cassette-> machine 1 (Cassette)
    static int machineFor (int t) noexcept { return (t == 1) ? 1 : 2; }

    static const CharSpec& charSpec (int c) noexcept
    {
        static const CharSpec T[8] = {
            /* Fresh   */ { 1.00f, 1.00f, 1.00f, 0.55f, 0.00f, 0.85f, 0.00f, 0.10f },
            /* Ferric  */ { 0.74f, 1.16f, 1.35f, 1.00f, 0.10f, 1.00f, 0.06f, 0.45f },
            /* Chrome  */ { 1.28f, 0.86f, 0.78f, 0.70f, 0.04f, 0.90f, 0.03f, 0.20f },
            /* Vintage */ { 0.82f, 1.08f, 1.20f, 1.15f, 0.22f, 1.20f, 0.14f, 0.55f },
            /* Worn    */ { 0.58f, 0.94f, 1.10f, 1.45f, 0.60f, 1.55f, 0.26f, 0.70f },
            /* Chewed  */ { 0.40f, 0.80f, 1.45f, 1.80f, 1.00f, 2.40f, 0.62f, 0.95f },
            /* Hot     */ { 0.66f, 1.24f, 1.75f, 0.90f, 0.16f, 1.05f, 0.09f, 0.85f },
            /* Cold    */ { 1.10f, 0.70f, 0.62f, 1.35f, 0.34f, 1.30f, 0.18f, 0.05f }
        };
        return T[(unsigned) c < 8u ? c : 0];
    }

    // ── the head pattern (back dropdown 2) ────────────────────────────────────
    //  Which of the four playback heads are down, how loud, and where they sit in
    //  the image. This is the RE-201's mode selector: the whole character of a
    //  multi-head echo is which heads you lower onto the tape.
    struct HeadSpec { float g[kHeads]; float pan[kHeads]; int fbHead; };

    static const HeadSpec& headSpec (int h) noexcept
    {
        static const HeadSpec T[8] = {
            /* Single  */ { {0.00f,0.00f,0.00f,1.00f}, { 0.0f, 0.0f, 0.0f, 0.0f}, 3 },
            /* Dual    */ { {0.82f,0.00f,0.00f,1.00f}, {-0.30f,0.0f, 0.0f, 0.30f}, 3 },
            /* Triple  */ { {0.74f,0.80f,0.00f,1.00f}, {-0.42f,0.0f, 0.0f, 0.42f}, 3 },
            /* Quad    */ { {0.68f,0.74f,0.80f,1.00f}, {-0.46f,-0.16f,0.16f,0.46f}, 3 },
            /* Spread  */ { {1.00f,0.00f,0.00f,0.88f}, {-0.85f,0.0f, 0.0f, 0.85f}, 3 },
            /* Swell   */ { {0.30f,0.52f,0.78f,1.00f}, {-0.24f,-0.08f,0.08f,0.24f}, 3 },
            /* Ping    */ { {0.95f,0.85f,0.90f,1.00f}, {-0.95f,0.95f,-0.95f,0.95f}, 3 },
            /* Cascade */ { {1.00f,0.70f,0.48f,0.32f}, {-0.55f,0.30f,-0.20f,0.10f}, 0 }
        };
        return T[(unsigned) h < 8u ? h : 0];
    }

    struct Params
    {
        int   type      = 0;      // 0 Studio · 1 Cassette · 2 Wire · 3 Tube
        int   character = 0;      // 0..7
        int   heads     = 0;      // 0..7
        float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;   // the machine's own three, 0..1
        float mix   = 0.35f;      // equal-power; 1.0 = FULLY wet, zero dry
        float timeSec = 0.38f;    // the LONGEST head — what the Time readout says
        float repeats = 0.30f;
        float drive   = 0.08f;
        float age     = 0.15f;
        float flutter = 0.25f;
        float bump    = 0.30f;
        float width   = 0.60f;
        float duck    = 0.00f;
        bool  delayOn = false;    // fb366 — the echo is OFF unless you ask for it
    };

    // What the card draws. Every field is a number the DSP actually just used.
    struct Viz
    {
        float spin  = 0.0f;   // integrated transport angle, TURNS (reels/hubs)
        float pack  = 0.5f;   // 0..1 supply→take-up transfer, at real tape speed
        float speed = 0.0f;   // 0..1 transport speed (0 = stopped)
        float wow   = 0.0f;   // the instantaneous rate deviation, ±1 normalised
        float lvl   = 0.0f;   // wet output level 0..1 (the VU needles ride THIS)
        float in    = 0.0f;   // input level 0..1
        float hiss  = 0.0f;   // the gated noise floor actually being added
        float head[kHeads] = { 0.0f, 0.0f, 0.0f, 0.0f };   // per-head tap level
    };

    void prepare (double sampleRate)
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        tapeL_.prepare (sr_, 0);
        tapeR_.prepare (sr_, 0);

        flL_.assign ((size_t) 1024, 0.0f); flR_.assign ((size_t) 1024, 0.0f); flW_ = 0;
        int need = (int) (sr_ * kMaxDelaySec) + 8;
        int sz = 1; while (sz < need) sz <<= 1;
        mask_ = sz - 1;
        ringL_.assign ((size_t) sz, 0.0f);
        ringR_.assign ((size_t) sz, 0.0f);

        rng_.seed (0x7A9E1u ^ (unsigned) (uintptr_t) this);
        reset();
    }

    void reset()
    {
        tapeL_.reset(); tapeR_.reset();
        std::fill (ringL_.begin(), ringL_.end(), 0.0f);
        std::fill (ringR_.begin(), ringR_.end(), 0.0f);
        std::fill (flL_.begin(), flL_.end(), 0.0f); std::fill (flR_.begin(), flR_.end(), 0.0f);
        flW_ = 0; flPh_ = 0.0f;
        wr_ = 0;
        readPos_ = -(double) (sr_ * 0.38);
        speed_ = 1.0f; spinAcc_ = 0.0; packAcc_ = 0.0;
        wowA_ = wowB_ = wowC_ = 0.0f; wowWalk_ = 0.0f; jumpHold_ = 0;
        for (int k = 0; k <= kHeads; ++k) { lossT_[k][0]=lossT_[k][1]=0.0f; bumpT_[k][0]=bumpT_[k][1]=0.0f; }
        hpL_ = hpR_ = 0.0f;
        aziL_ = 0.0f;
        dropEnv_ = 1.0f; dropTimer_ = 0; compEnv_ = 0.0f;
        walk_ = 0.0f; walkTgt_ = 0.0f; walkG_ = 1.0f; walkTimer_ = 0;
        inEnv_ = 0.0f; outEnv_ = 0.0f; duckEnv_ = 0.0f; gate_ = 0.0f; echoEnv_ = 0.0f;
        hissGlide_ = 0.0f;
        hissCoeff_ = (float) (1.0 - std::exp (-1.0 / (sr_ * 0.080)));
        heldType_ = -1; holdCtr_ = 0; reseat_ = 1.0f;
        for (auto& h : headEnv_) h = 0.0f;
        vizTick_ = 0;
        publish();
    }

    // Audio thread. Cheap — no allocation, no branching on strings.
    void setParams (const Params& p) noexcept
    {
        pr_ = p;
        // NOTE: the machine is NOT switched here. process() re-seats it under a duck — see there.

        baseDelay_ = (float) juceClamp ((double) p.timeSec * sr_, 32.0, (double) mask_ - 64.0);

        // Timidity law (fb315): the FX bus sits near −26 dBFS, so a 1+k·amt trim is
        // inaudible. Drive is dB and it is BIG — 0 → +26 dB on a t^0.8 taper.
        // 0 -> +18 dB, not +26: the machine gain above already puts the tape at its knee, so
        // a 26 dB default of 25% was landing at x11 — past the window, where measured Cassette
        // Saturate authority is NEGATIVE. Drive is for pushing past it deliberately.
        driveLin_ = std::pow (10.0f, (18.0f * std::pow (p.drive, 0.8f)) * 0.05f);
        trimLin_  = std::pow (10.0f, (-9.0f * std::pow (p.drive, 0.8f)) * 0.05f);
    }

    // ═══ THE MACHINES WANT A LEVEL, AND THE RACK BUS DOES NOT HAVE IT ═══════════════
    //  Max, fb366: "the wow, the hiss and the saturation do not work... it seems like you
    //  didn't actually use the tape engine in the front."  The engine WAS the front one —
    //  the level going into it was not. These machines saturate on ABSOLUTE amplitude
    //  (Cassette's is `preGain = 1.5 + amount*3` into a cubic clip), and they were voiced
    //  for a near-unity front-page path. The rack FX bus sits at −26 dBFS (the fb315
    //  timidity law), where 4.5x of pre-gain is still dead centre of the linear region:
    //  measured, the Saturate knob moved THD by 2.3 dB there. Nothing. Inaudible.
    //
    //  Swept against input level, Cassette's knob has its MAXIMUM authority at x4:
    //      x1  −60.9 → −58.7  (+2.3 dB)   ← the rack bus. dead.
    //      x2  −48.8 → −35.5  (+13.3)
    //      x4  −36.5 → −19.9  (+16.6)     ← the sweet spot
    //      x8  −23.2 → −22.1  (+1.1)      ← already saturated at 0; the knob does nothing
    //      x16 −12.1 → −20.7  (−8.6)      ← past it, the post-LP eats harmonics
    //  So the tape runs at x4 and comes back down, and the number is measured rather
    //  than guessed. HISS is compensated for the same trim (see hissAmount below),
    //  because the machine adds noise at an absolute level INSIDE that boundary.
    //  x5, not x4: Cassette's knob moves preGain 1.5 -> 4.5, so for it to have any say the
    //  input must be quiet enough that 1.5x is clean and loud enough that 4.5x is not —
    //  a window around 0.28, i.e. x5.6 off the bus. Drive then pushes PAST it on purpose.
    static constexpr float kMachineGain = 5.0f;

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const CharSpec& C = charSpec (pr_.character);
        const HeadSpec& H = headSpec (pr_.heads);

        // ── THE TYPE RE-SEAT (the fb345 deferred-fade law) ──────────────────────────────
        // Measured: the SHIPPED TapeProcessor, on its own with none of this file involved,
        // puts a 14.6 dB envelope step through a Cassette->Studio switch against a 6.7 dB
        // quiescent floor. Its 75 ms crossfade brings the incoming machine up from STALE
        // internal state — that machine has not been processing, so its wow delay line still
        // holds whatever was in it when it last ran — and the fade is therefore between the
        // new signal and a stale one. Rewriting the three machines is off the table (Max:
        // "same effect, same everything"), so the WET DUCKS around the swap: fade out (~5 ms),
        // commit the type, stay down for the machine crossfade, come back. The echo loop is
        // never reset, so the tail survives, and at Mix < 100% the dry never drops out at all.
        //
        // WHY 20 ms AND NOT THE FULL 75 ms CROSSFADE: the stale window is bounded by the
        // incoming machine's own delay line — 1.2 ms on Studio, 3.2 ms on Cassette, 8.5 ms on
        // Wire — after which the crossfade is a legitimate blend of two LIVE machines and
        // needs no hiding. Measured across all six transitions, holding for 85 ms and holding
        // for 20 ms give the SAME audible-region envelope step (19.6 dB either way, against
        // the machines' own 13-21 dB quiescent motion — Wire's micro-dropouts are larger than
        // anything the switch does). So the longer duck bought nothing but a 87 ms hole.
        // fb367 — the Tube type is retired. Max: "tube is saturation, we already have tube
        // saturation." The rack's Distortion device owns valve character; this one owns tape.
        if (heldType_ < 0) { heldType_ = pr_.type; tapeL_.setMachine (machineFor (heldType_)); tapeR_.setMachine (machineFor (heldType_)); }
        else if (heldType_ != pr_.type)
        {
            reseat_ *= 0.985f;
            if (reseat_ < 0.02f)
            {
                heldType_ = pr_.type;
                tapeL_.setMachine (machineFor (heldType_)); tapeR_.setMachine (machineFor (heldType_));
                holdCtr_ = (int) (sr_ * 0.020);      // see the note below
            }
        }
        else if (holdCtr_ > 0) { --holdCtr_; reseat_ *= 0.985f; }
        else reseat_ += (1.0f - reseat_) * 0.0016f;

        // ── input envelopes: the presence gate (nothing free-runs) and the ducker
        const float ax = std::max (std::fabs (inL), std::fabs (inR));
        inEnv_  += (ax > inEnv_  ? 0.010f : 0.0006f) * (ax - inEnv_);
        duckEnv_+= (ax > duckEnv_? 0.020f : 0.0012f) * (ax - duckEnv_);
        const float gTgt = (inEnv_ > 2.0e-4f) ? 1.0f : 0.0f;
        gate_ += (gTgt > gate_ ? 0.0025f : 0.00006f) * (gTgt - gate_);

        // ── THE TRANSPORT ────────────────────────────────────────────────────────
        speed_ += 0.0008f * (1.0f - speed_);          // spins up on load, then just runs

        const float wowDev = wowGen (C.wander);          // ± fractional rate error
        const float rate = speed_ * (1.0f + wowDev);

        // ── record: the machine colours what goes ON the tape, once
        const float dl = inL * driveLin_ * kMachineGain, dr = inR * driveLin_ * kMachineGain;
        float cL, cR;
        machine (dl, dr, cL, cR, C, gate_);
        cL *= trimLin_ * (1.0f / kMachineGain);
        cR *= trimLin_ * (1.0f / kMachineGain);

        // ── THE PLAYBACK HEAD. fb365 harness [B]: Age and Bump used to live only in
        // the feedback path, so at Repeats 0 both knobs were DEAD — 1.79 dB, which was
        // the measurement's own noise floor. That was also physically wrong: gap loss,
        // head bump and oxide dropouts are properties of the HEAD, so everything read
        // off the tape gets them, not just the repeats. One stage, five copies of its
        // state (4 heads + the direct read), and the compounding is automatic because
        // the feedback is taken from an already-played tap and re-recorded.
        agedropout (C);
        // the oxide has thinned unevenly: a slow level walk, +-4.5 dB at full Age
        if (--walkTimer_ <= 0)
        { walkTimer_ = (int) (sr_ * (0.35 + 1.3 * uni())); walkTgt_ = (float) (uni() * 2.0 - 1.0); }
        walk_ += 0.00004f * (walkTgt_ - walk_);
        walkG_ = std::pow (10.0f, (walk_ * 4.5f * pr_.age) * 0.05f);
        const int   mch = machineFor (pr_.type);
        const float hfBase = (mch == 1) ? 5200.0f : 4200.0f;   // shell vs reel
        const float aLP = onePole (juceClampf (hfBase * C.hf * (1.0f - 0.88f * pr_.age), 260.0f, 18000.0f));
        const bool  doBump = pr_.bump > 0.0005f;
        // HEAD BUMP — the low resonance the head puts back. Its frequency is a real
        // property of the machine (a 15 ips reel bumps low and broad, a 1-7/8 ips
        // shell bumps higher and tighter) and it TRACKS THE TRANSPORT, so it sags
        // with the pitch during a Stop. At 1.15x it measured 0.99 dB against the
        // gate's 1.5 — inaudible, i.e. exactly the playing-safe the house rule bans.
        const float bHz = (mch == 1) ? 92.0f : 52.0f;   // a shell bumps high and tight, a reel low
        const float aB = onePole (juceClampf (bHz * (0.25f + 0.75f * speed_), 18.0f, 240.0f));
        const float bg = 1.95f * pr_.bump;

        // feedback read happens BEFORE the write, so a tap can never see the sample
        // it is about to help record (the one-clock law, fb345).
        // ⚠️ THE ECHO USED TO BE UNSTOPPABLE. Repeats is FEEDBACK, so at Repeats 0 the first
        // tap still played at full gain — measured, a −29 dBFS burst came back as a −16.7 dBFS
        // slap, LOUDER than the input, with no control that removed it. Max: "I can't turn the
        // delay off for some reason... I don't want to hear that fucking delay bro." So the
        // echo is now a PILL, off by default, and when it is on the whole tap bus is trimmed
        // to 0.5 so it sits under the tape rather than on top of it. Rack order does the rest:
        // put the real Delay device before the Tape and it gets processed BY the tape.
        float tapL[kHeads], tapR[kHeads];
        const double gap = (double) wr_ - readPos_;
        float wetL = 0.0f, wetR = 0.0f;
        echoEnv_ += ((pr_.delayOn ? 1.0f : 0.0f) - echoEnv_) * 0.0009f;   // click-free on/off
        const float echoG = echoEnv_ * 0.50f;
        for (int k = 0; k < kHeads; ++k)
        {
            tapL[k] = tapR[k] = 0.0f;
            if (H.g[k] <= 0.0f || echoEnv_ < 1.0e-4f) { headEnv_[k] += 0.002f * (0.0f - headEnv_[k]); continue; }
            const double d = gap * (double) (k + 1) * 0.25;
            tapL[k] = playHead (readCubic (ringL_, d), k, 0, aLP, doBump, aB, bg);
            tapR[k] = playHead (readCubic (ringR_, d), k, 1, aLP, doBump, aB, bg);
            const float pan = H.pan[k] * pr_.width;
            const float gl = H.g[k] * std::sqrt (0.5f * (1.0f - pan));
            const float gr = H.g[k] * std::sqrt (0.5f * (1.0f + pan));
            wetL += tapL[k] * gl * 1.41421356f * echoG;
            wetR += tapR[k] * gr * 1.41421356f * echoG;
            const float m = 0.5f * (std::fabs (tapL[k]) + std::fabs (tapR[k])) * H.g[k];
            headEnv_[k] += (m > headEnv_[k] ? 0.25f : 0.004f) * (m - headEnv_[k]);
        }

        // ── the feedback loop: this is where tape AGES ───────────────────────────
        const int fh = H.fbHead;
        float fbL = tapL[fh], fbR = tapR[fh];
        // Repeats: 0 → 1.12 loop gain. Past 1.0 it sings, which is the point.
        const float loop = 1.12f * std::pow (pr_.repeats, 0.9f) * echoEnv_;
        fbL *= loop; fbR *= loop;

        // per-pass saturation — the repeats get dirtier, the direct signal does not
        const float ps = 1.0f + 2.4f * C.sat * (0.25f + 0.75f * pr_.drive);
        fbL = std::tanh (fbL * ps) / ps;
        fbR = std::tanh (fbR * ps) / ps;

        // the loop needs a bass drain, or the compounding head bump walks it into mud
        // after ~6 passes. This one is loop-only on purpose: it is a stability measure,
        // not something the playback head does.
        const float aHP = onePole (juceClampf (52.0f / C.lf, 20.0f, 220.0f));
        hpL_ += aHP * (fbL - hpL_); fbL -= hpL_;
        hpR_ += aHP * (fbR - hpR_); fbR -= hpR_;

        if (C.comp > 0.001f)
        {
            const float m = std::max (std::fabs (fbL), std::fabs (fbR));
            compEnv_ += (m > compEnv_ ? 0.006f : 0.0004f) * (m - compEnv_);
            const float duckAmt = 1.0f / (1.0f + compEnv_ * 5.0f * C.comp * (0.3f + pr_.age));
            fbL *= duckAmt; fbR *= duckAmt;
        }

        // AZIMUTH: a mis-aligned head loses HF on one channel and skews it in time.
        // A real defect, so it reads as width rather than as an effect.
        if (C.azi > 0.001f)
        {
            const float aA = onePole (juceClampf (16000.0f * (1.0f - 0.92f * C.azi), 900.0f, 18000.0f));
            aziL_ += aA * (fbL - aziL_);
            fbL = fbL + (aziL_ - fbL) * C.azi;
        }

        fbL = flush (fbL); fbR = flush (fbR);

        ringL_[(size_t) (wr_ & mask_)] = cL + fbL;
        ringR_[(size_t) (wr_ & mask_)] = cR + fbR;
        ++wr_;

        // advance the read head at the TRANSPORT rate — this is the pitch
        readPos_ += (double) rate;
        double g2 = (double) wr_ - readPos_;
        if (g2 < 8.0)               { readPos_ = (double) wr_ - 8.0;               g2 = 8.0; }
        if (g2 > (double) mask_-32) { readPos_ = (double) wr_ - (double) (mask_-32); g2 = mask_-32; }
        // servo the loop back to the SET length, but only while the tape is at
        // speed — during a stop the gap is supposed to run away, that IS the effect.
        if (speed_ > 0.985f) readPos_ += (g2 - (double) baseDelay_) * 2.0e-5;

        // ── the wet bus: direct machine output + the heads ───────────────────────
        // the direct signal is read off the SAME head, so it gets the same gap loss,
        // the same head bump and the same dropouts. This is what makes Age and Bump
        // alive at Repeats 0 instead of dead knobs.
        wetL += playHead (cL, kHeads, 0, aLP, doBump, aB, bg);
        wetR += playHead (cR, kHeads, 1, aLP, doBump, aB, bg);

        // ── FLUTTER: the fast half of tape speed instability, on its own control ─────
        // Max: "they have different flutters." Wow is the machine's own slow wander (0.6-2 Hz,
        // inside TapeMachines.h and untouched); flutter is the fast scrape the capstan and the
        // guides put on top, so it lives here on the transport, per machine — 7.4 Hz on a reel,
        // 9.7 Hz plus a 34 Hz scrape on a shell, 13 Hz and wandering on a wire.
        // 🔑 At Flutter = 0 the delay is EXACTLY 0 samples, so the path is bit-identical to not
        // having it — no static offset, no comb with the dry, nothing to regress.
        flL_[(size_t) flW_] = wetL; flR_[(size_t) flW_] = wetR;
        if (pr_.flutter > 0.0005f)
        {
            const int mchF = machineFor (pr_.type);
            // 🔑 fb367 — THAT WAS WOW, NOT FLUTTER. Max: "flutter isn't doing what it's
            // supposed to do. It's not flutter."  Right: 1.1 ms at 9.7 Hz is 6.7% of pitch
            // deviation, which is a seasick warble, and against the dry it just reads as
            // chorus. Flutter is FAST and SHALLOW — the 10-20 Hz band plus the scrape the tape
            // makes against the guides well above it, and it is IRREGULAR, not a metronome.
            const float f1 = (mchF == 1 ? 17.5f : 13.5f);
            const float f2 = (mchF == 1 ? 78.0f : 61.0f);       // scrape flutter
            flPh_ += (float) (1.0 / sr_); if (flPh_ > 1.0e6f) flPh_ -= 1.0e6f;
            if (--flJit_ <= 0) { flJit_ = (int) (sr_ * (0.07 + 0.22 * uni())); flJitV_ = (float) (uni() * 2.0 - 1.0); }
            flJitS_ += 0.0006f * (flJitV_ - flJitS_);
            const float m = std::sin (6.2831853f * f1 * (1.0f + flJitS_ * 0.35f) * flPh_) * 0.62f
                          + std::sin (6.2831853f * f2 * flPh_ + 1.1f) * 0.24f
                          + flJitS_ * 0.14f;
            const float depth = pr_.flutter * pr_.flutter * (float) (sr_ * 0.00030);  // → ±0.30 ms
            const float d = depth * (1.0f + m) * 0.5f;
            const float rd = (float) flW_ - d;
            const int i1 = (int) std::floor (rd); const float fr = rd - (float) i1;
            auto rdl = [&] (const std::vector<float>& b)
            { const float a0 = b[(size_t) ((i1) & 1023)], a1 = b[(size_t) ((i1 + 1) & 1023)];
              return a0 + (a1 - a0) * fr; };
            wetL = rdl (flL_); wetR = rdl (flR_);
        }
        flW_ = (flW_ + 1) & 1023;

        if (pr_.duck > 0.001f)
        {
            // 7x an envelope that peaks around 0.1 on the -26 dBFS bus is a 3 dB nudge, not a
            // duck. The bus level is the same reason Drive and Saturate needed calibrating.
            const float d = 1.0f - juceClampf (duckEnv_ * 34.0f * pr_.duck, 0.0f, 0.96f);
            wetL *= d; wetR *= d;
        }

        // Width on the WET only (the DelayEngine grammar) — 0 = mono, 1.6 = wide
        if (std::fabs (pr_.width - 0.5f) > 0.002f)
        {
            const float m = 0.5f * (wetL + wetR), s = 0.5f * (wetL - wetR) * (pr_.width * 1.6f);
            wetL = m + s; wetR = m - s;
        }

        // Repeats past 1.0 SINGS — that is the point, and harness [J] measured the
        // Cassette hitting +15 dBFS with every control at 100%. A real machine does
        // not do that: the tape runs out of magnetic headroom and the howl plateaus.
        // 1.8x ceiling, unity slope at small signals, so nothing below it is touched.
        wetL = 1.8f * std::tanh (wetL * 0.5555556f) * reseat_;   // the re-seat duck rides the WET only
        wetR = 1.8f * std::tanh (wetR * 0.5555556f) * reseat_;

        // MIX — equal power, and 100% is FULLY wet with zero dry (the house law)
        const float mx = juceClampf (pr_.mix, 0.0f, 1.0f);
        const float wg = std::sin (mx * 1.5707963f), dg = std::cos (mx * 1.5707963f);
        outL = flush (inL * dg + wetL * wg);
        outR = flush (inR * dg + wetR * wg);

        // ── viz: the real numbers, 750 Hz, no decoration ─────────────────────────
        spinAcc_ += (double) speed_ / sr_;               // turns, at the real speed
        if (spinAcc_ > 1.0e7) spinAcc_ -= 1.0e7;
        packAcc_ += (double) speed_ / (sr_ * 74.0);      // a 74 min reel, honestly
        if (packAcc_ > 1.0) packAcc_ -= 1.0;
        const float ay = std::max (std::fabs (outL), std::fabs (outR));
        outEnv_ += (ay > outEnv_ ? 0.05f : 0.0009f) * (ay - outEnv_);
        lastWow_ = wowDev;
        if (++vizTick_ >= 64) { vizTick_ = 0; publish(); }
    }

    Viz viz() const noexcept
    {
        Viz v;
        v.spin  = vSpin_ .load (std::memory_order_relaxed);
        v.pack  = vPack_ .load (std::memory_order_relaxed);
        v.speed = vSpeed_.load (std::memory_order_relaxed);
        v.wow   = vWow_  .load (std::memory_order_relaxed);
        v.lvl   = vLvl_  .load (std::memory_order_relaxed);
        v.in    = vIn_   .load (std::memory_order_relaxed);
        v.hiss  = vHiss_ .load (std::memory_order_relaxed);
        for (int k = 0; k < kHeads; ++k) v.head[k] = vHead_[k].load (std::memory_order_relaxed);
        return v;
    }

private:
    // ── the machine: the SHIPPED character DSP, unchanged ────────────────────────
    // Studio reads sculpt/weave/tilt; Cassette and Wire read wow/sat/hiss from their
    // own slots. Hiss is scaled by the presence gate so a silent Tape is silent.
    void machine (float inL, float inR, float& oL, float& oR,
                  const CharSpec& C, float gate) noexcept
    {
        // 🔑 ALL NINE, EVERY SAMPLE, WHATEVER THE TYPE. The first cut fed the two
        // inactive machines zeros and only the live one real values — which meant that
        // the instant setMachine() started TapeProcessor's 75 ms crossfade, the machine
        // fading OUT had its whole control surface stepped to zero underneath it. The
        // fade was doing its job; the parameters were not. Harness [H] measured it as a
        // 24.5x jump on a type switch. Each machine reads only its own three, so
        // handing every machine sensible values costs nothing and makes the crossfade
        // an actual crossfade.
        // ⚠️ AND THE VALUE ITSELF MUST NOT STEP EITHER. Studio's third control is Tilt, not
        // Hiss, so its noise floor is a fixed 0.12 while Cassette's and Wire's is p3 — which
        // means the number JUMPS the moment the type crosses Studio, right in the middle of
        // the 75 ms crossfade. Harness [H] caught it as a 19.9x jump. Glided over 80 ms (longer
        // than the fade), so at no instant does a machine see a discontinuity.
        hissGlide_ += hissCoeff_ * (pr_.p3 - hissGlide_);
        // ⚠️ HISS WAS INAUDIBLE, and two things made it so. (1) The machine's own map is
        // pow(amount, 2.5) * 0.006 — at the default 18% that is 1.9e-5, i.e. −94 dBFS, and
        // the bottom HALF of the knob does nothing at all (the fb325 no-plateaus law).
        // (2) It is added INSIDE the x4 boundary above, so the trim divides it by 4 as well.
        // Fixed by pre-warping the knob (^0.6, so the travel is roughly linear in dB) and
        // compensating the trim. Fresh at 100% now lands ~−45 dBFS against a −26 dBFS
        // signal; the default 18% sits ~−67 dBFS, a floor you notice only in the gaps.
        // Capped at 3.0 so Worn/Chewed are filthy rather than louder than the music.
        // 4.2x, solved not guessed: the machine's noise RMS is 0.577*pow(A,2.5)*maxH, and my x5
        // trim divides it, so for Fresh (0.55) to land at -45 dBFS against a -26 dBFS signal,
        // A must reach 2.3 at knob 100% => 2.3/0.55 = 4.2. Capped at 4.5 so Chewed is filthy
        // (-30 dBFS, 4 dB under the music) instead of louder than it.
        // 🔑🔑 ONE KNOB, TWO JOBS — fb367, and it is the whole of "wire is still messed up".
        // In CassetteMachine the hiss amount buys noise and nothing else, so fb366's x4.2
        // compensation simply made the noise floor audible (Max: "cassette sounds really,
        // really, really fucking nice").  In WireMachine the SAME amount also sets the
        // micro-dropout rate: dropoutProb = 0.0004 * amount^2 PER SAMPLE.  Measured, x4.2
        // handed the wire 13 dropouts/sec at the 18% default and 102/sec at full, against
        // the 0.6 and 19 its own design intends — 22x. That is a gargling, stuttering mess
        // that the Saturate knob has no authority over, which is exactly what Max heard:
        // "I turned the saturation down and I still hear that crazy loud distorted
        // saturation."  LAW: a shared parameter can drive more than one thing, and
        // compensating one of them over-drives the other. So the two machines are
        // calibrated SEPARATELY — processSample takes both sets anyway.
        const float hk = juceClampf (hissGlide_, 0.0f, 1.0f);
        const float cassHiss = juceClampf (4.2f * C.noise * std::pow (hk, 0.60f) * gate, 0.0f, 4.5f);
        const float wireHiss = juceClampf (1.1f * C.noise * std::pow (hk, 0.75f) * gate, 0.0f, 1.3f);
        const float sat   = juceClampf (pr_.p2 * C.sat, 0.0f, 1.0f);
        const float mWow  = pr_.p1;
        const float tilt  = juceClampf ((pr_.p3 * 2.0f - 1.0f) + (C.hf - 1.0f) * 0.5f, -1.0f, 1.0f);
        lastHiss_ = (machineFor (pr_.type) == 1) ? cassHiss : wireHiss;
        oL = tapeL_.processSample (inL, mWow, sat, cassHiss, mWow, sat, wireHiss, pr_.p1, pr_.p2, tilt);
        oR = tapeR_.processSample (inR, mWow, sat, cassHiss, mWow, sat, wireHiss, pr_.p1, pr_.p2, tilt);
    }

    // ── wow + flutter, per machine, exactly the character each one is documented
    //    to have (TapeMachines.h header): Studio one gentle LFO, Cassette a triple,
    //    Wire chaotic with speed jumps. Returns a FRACTIONAL rate error.
    float wowGen (float wander) noexcept
    {
        const int   mch = machineFor (pr_.type);
        const float amt = (mch == 0) ? (0.18f + 0.35f * pr_.p2)   // Studio: Weave
                                          : pr_.p1;                    // Cassette/Wire: Wow
        const float k = amt * wander;
        const float dt = (float) (1.0 / sr_);
        wowA_ += dt; if (wowA_ > 1.0e6f) wowA_ -= 1.0e6f;

        if (mch == 0)
        {
            const float w = std::sin (6.2831853f * 0.8f * wowA_) * 0.0011f
                          + std::sin (6.2831853f * 5.5f * wowA_) * 0.00018f;
            return w * k;
        }
        if (mch == 1)
        {
            const float w = std::sin (6.2831853f * 0.62f * wowA_) * 0.0026f
                          + std::sin (6.2831853f * 2.90f * wowA_) * 0.0011f
                          + std::sin (6.2831853f * 7.30f * wowA_) * 0.00042f;
            return w * k;
        }
        // WIRE — a random walk plus occasional speed jumps. It is the broken one.
        if (--jumpHold_ <= 0)
        {
            jumpHold_ = (int) (sr_ * (0.18 + 0.9 * uni()));
            wowWalk_ = (float) (uni() * 2.0 - 1.0);
        }
        wowB_ += 0.00035f * (wowWalk_ - wowB_);
        const float w = std::sin (6.2831853f * 0.41f * wowA_) * 0.0055f
                      + std::sin (6.2831853f * 1.70f * wowA_) * 0.0022f
                      + wowB_ * 0.0085f;
        return w * k;
    }

    // 🔑 AGE HAD NO FLOOR OF ITS OWN. Max: "the age knob is also not doing what it's
    // supposed to do because whenever I turn it up to 100% it should sound really aged, it
    // doesn't sound like anything."  It was `C.drop * age`, and the DEFAULT character Fresh
    // has drop = 0.00 — so on a fresh tape the whole knob multiplied by ZERO. Age is the AGE
    // MASTER: it now brings its own dropouts, its own level walk and its own extra HF loss,
    // and the formulation only makes it worse. A fresh tape played a thousand times is worn.
    void agedropout (const CharSpec& C) noexcept
    {
        const float rate = (0.85f + C.drop) * pr_.age;   // Age 100% must DROP OUT, not dip
        if (rate <= 0.0005f) { dropEnv_ += 0.004f * (1.0f - dropEnv_); return; }
        if (--dropTimer_ <= 0)
        {
            dropTimer_ = (int) (sr_ * (0.04 + 1.9 * (1.0 - juceClampf (rate, 0.0f, 0.98f)) * (0.25 + uni())));
            dropTgt_ = 1.0f - (float) uni() * 0.92f * juceClampf (rate, 0.0f, 1.0f);
        }
        // recover faster than it drops — a shed patch is a dip, not a gate
        dropEnv_ += (dropTgt_ < dropEnv_ ? 0.0025f : 0.0006f) * (dropTgt_ - dropEnv_);
    }

    // ONE playback head, five copies of its state (4 heads + the direct read).
    // Gap loss, head bump and the oxide dropout all live here — so they colour
    // everything read off the tape, and COMPOUND per repeat for free, because the
    // feedback is taken from an already-played tap and recorded again.
    float playHead (float x, int slot, int ch, float aLP, bool doBump,
                    float aB, float bg) noexcept
    {
        float& lp = lossT_[slot][ch];
        lp += aLP * (x - lp); float y = lp;
        if (doBump) { float& bp = bumpT_[slot][ch]; bp += aB * (y - bp); y += bp * bg; }
        return y * dropEnv_ * walkG_;
    }

    float readCubic (const std::vector<float>& b, double delaySamples) const noexcept
    {
        const double pos = (double) wr_ - delaySamples;
        const int i1 = (int) std::floor (pos);
        const float f = (float) (pos - (double) i1);
        const float y0 = b[(size_t) ((i1 - 1) & mask_)], y1 = b[(size_t) (i1 & mask_)];
        const float y2 = b[(size_t) ((i1 + 1) & mask_)], y3 = b[(size_t) ((i1 + 2) & mask_)];
        const float c0 = y1, c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    float onePole (float hz) const noexcept
    {
        const float x = (float) (1.0 - std::exp (-6.2831853 * (double) hz / sr_));
        return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    }
    double uni() noexcept { return (double) (rng_() >> 8) * (1.0 / 16777216.0); }
    static float flush (float x) noexcept
    { return (std::fabs (x) < 1.0e-20f || ! std::isfinite (x)) ? 0.0f : x; }
    static float juceClampf (float v, float lo, float hi) noexcept
    { return v < lo ? lo : (v > hi ? hi : v); }
    static double juceClamp (double v, double lo, double hi) noexcept
    { return v < lo ? lo : (v > hi ? hi : v); }

    void publish() noexcept
    {
        vSpin_ .store ((float) (spinAcc_ - std::floor (spinAcc_ / 1000.0) * 1000.0), std::memory_order_relaxed);
        vPack_ .store ((float) packAcc_, std::memory_order_relaxed);
        vSpeed_.store (speed_,  std::memory_order_relaxed);
        vWow_  .store (juceClampf (lastWow_ * 180.0f, -1.0f, 1.0f), std::memory_order_relaxed);
        vLvl_  .store (juceClampf (outEnv_ * 5.2f, 0.0f, 1.0f), std::memory_order_relaxed);
        vIn_   .store (juceClampf (inEnv_  * 5.2f, 0.0f, 1.0f), std::memory_order_relaxed);
        vHiss_ .store (lastHiss_, std::memory_order_relaxed);
        for (int k = 0; k < kHeads; ++k)
            vHead_[k].store (juceClampf (headEnv_[k] * 6.0f, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    double sr_ = 48000.0;
    Params pr_;
    int lastType_ = -1;

    TapeProcessor tapeL_, tapeR_;

    std::vector<float> ringL_, ringR_, flL_, flR_;
    int flW_ = 0; float flPh_ = 0.0f;
    int flJit_ = 0; float flJitV_ = 0.0f, flJitS_ = 0.0f;
    int    mask_ = 0;
    long long wr_ = 0;
    double readPos_ = 0.0;
    float  baseDelay_ = 18000.0f;

    float  speed_ = 1.0f;
    double spinAcc_ = 0.0, packAcc_ = 0.0;
    float  driveLin_ = 1.0f, trimLin_ = 1.0f;

    float  wowA_ = 0.0f, wowB_ = 0.0f, wowC_ = 0.0f, wowWalk_ = 0.0f;
    int    jumpHold_ = 0;

    float  lossT_[kHeads + 1][2] = {}, bumpT_[kHeads + 1][2] = {};
    float  hpL_ = 0.0f, hpR_ = 0.0f, aziL_ = 0.0f;
    float  dropEnv_ = 1.0f, dropTgt_ = 1.0f, compEnv_ = 0.0f;
    int    dropTimer_ = 0, walkTimer_ = 0;
    float  walk_ = 0.0f, walkTgt_ = 0.0f, walkG_ = 1.0f;

    float  inEnv_ = 0.0f, outEnv_ = 0.0f, duckEnv_ = 0.0f, gate_ = 0.0f, echoEnv_ = 0.0f;
    float  headEnv_[kHeads] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float  lastWow_ = 0.0f, lastHiss_ = 0.0f;
    float  hissGlide_ = 0.0f, hissCoeff_ = 0.0003f;
    int    heldType_ = -1, holdCtr_ = 0;
    float  reseat_ = 1.0f;

    std::mt19937 rng_;
    int vizTick_ = 0;

    std::atomic<float> vSpin_ { 0.0f }, vPack_ { 0.5f }, vSpeed_ { 0.0f }, vWow_ { 0.0f };
    std::atomic<float> vLvl_ { 0.0f }, vIn_ { 0.0f }, vHiss_ { 0.0f };
    std::atomic<float> vHead_[kHeads] { {0.0f}, {0.0f}, {0.0f}, {0.0f} };
};

} // namespace tw
