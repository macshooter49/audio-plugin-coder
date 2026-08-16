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
//  exposes (Wow / Saturate / Hiss — both machines, since fb367 retired the Sculptor). Type
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
//    · (fb366 retired Stop and Motor — the transport still advances at `speed`, which is
//      what makes the two points below true, but there is no user-facing spin-down.)
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

    // ═══ fb368 — THE TRANSPORT IS PART OF THE MACHINE ════════════════════════════
    //  Max: "studio is exactly the same as cassette and these need to be different
    //  effects."  Measured, and he is right: the RAW machines differ by 12.18 dB of
    //  spectral distance, but through the fb367 transport only 7.66 dB survives — and
    //  what survives is a ripple, not a character. The transport was the leveller: ONE
    //  playback-head lowpass at 4.2 vs 5.2 kHz (a third of an octave apart) sat after
    //  both machines and threw away exactly the harmonics that tell them apart, then a
    //  shared head bump, a shared dropout and a shared limiter put them back on the
    //  same shape.
    //
    //  A tape machine is not a saturator with a delay after it — the SPEED, the TRACK
    //  WIDTH and the HEAD are most of what you hear. So the transport is voiced per
    //  machine, and the two are voiced three octaves apart on purpose:
    //
    //    CASSETTE — a 1-7/8 ips shell. Bandwidth to 12 kHz (bright, present), bass
    //      down to 32 Hz, a TIGHT high head bump at 95 Hz, bright sibilant hiss, and
    //      it PUMPS (a cassette's own compression is a big part of the sound).
    //    STUDIO  — the vintage wire deck. A 2.8 kHz FOUR-POLE head and nothing above it,
    //      no bass below 130 Hz, a boxy 185 Hz honk, pink rumbling noise, micro-dropouts
    //      and a transport that wanders. Thin, telephone-ish, broken — the vintage one.
    //
    //  ⚠️ AND THE HEAD HAS TO BE STEEP TO WIN. Measured: the wire's own folder puts ~39 dB
    //  back into 1.5-6 kHz, so a 6 dB/oct head loses the argument — at 4.2 kHz one-pole the
    //  two machines came out within 3 dB there, and Studio actually measured BRIGHTER than
    //  Cassette. Real gap loss is steep, so the head is a 4-pole cascade at 2.8 kHz. That
    //  is the number where the machines separate without Studio turning to mud.
    //
    //  That is not a tweak, it is two different decades of machine, and it is what
    //  makes the type switch night and day instead of an EQ ripple.
    struct Voice
    {
        float hfHz;     // playback-head bandwidth — the single biggest tell
        float hpHz;     // how much bass the head can actually read back
        float bumpHz;   // head-bump centre
        float bumpQ;    // and how tight it is
        float bumpDb;   // its authority at Bump 100%
        float hissK;    // what the MACHINE's own hiss amount is driven to
        float hissTop;  // floor top-up (see noise() — only where the coupling forces it)
        float flSlow;   // flutter band, low edge (Hz)
        float flFast;   // flutter band, high edge (Hz)
        float flDepth;  // flutter authority
        float comp;     // transport-level compression (a shell pumps, a wire does not)
        int   lpN;      // playback-head pole count — gap loss is STEEP, 6 dB/oct is not
        float mgain;    // what level the MACHINE is driven at (see the note above)
    };

    static const Voice& voiceFor (int mch) noexcept
    {
        // ⚠️ THE BUMP MUST SIT ABOVE THE HEAD'S LOW LIMIT. First cut put Studio's bump at
        // 52 Hz behind a 150 Hz high-pass, so the resonance had nothing left to resonate on
        // and Bump measured +4.1 dB — Max's "Bump isn't doing anything", reproduced exactly.
        // Physically the head bump IS the top of the low-frequency rolloff, not something
        // under it: a wire deck honks BOXY at ~185 Hz, a shell thumps at ~90 Hz.
        // ⚠️ hissK is DELIBERATELY LOW ON THE WIRE and the floor is made up by hissTop —
        // see noise(). Its ceiling is set by where its own dropouts stop being tape.
        //                 hf     hp    bHz    bQ    bDb  hissK hissTop flS  flF  flDep comp
        static const Voice C { 13000.f, 32.f,  90.f, 1.70f, 12.f, 6.4f, 0.000f, 5.f, 17.f, 1.00f, 1.0f, 2, 2.2f };
        static const Voice W {  2800.f, 130.f, 185.f, 1.30f, 12.f, 0.95f, 0.075f, 3.f, 13.f, 1.55f, 0.25f, 4, 5.0f };
        return (mch == 1) ? C : W;
    }

    static const CharSpec& charSpec (int c) noexcept
    {
        static const CharSpec T[8] = {
            // fb368 — WIDENED. Max: "the character needs to also be amplified up to actually
            // make them sound night and day just like our reverbs." Measured at fb367 the
            // closest pair (Ferric vs Hot) was 3.29 dB apart, because the eight stocks only
            // really differed on two axes. Every stock now has its own identity on FIVE:
            // bandwidth, bass, saturation, noise and stability.
            /* Fresh   */ { 1.00f, 1.00f, 1.00f, 0.55f, 0.00f, 0.55f, 0.00f, 0.05f },
            /* Ferric  */ { 0.78f, 1.22f, 1.25f, 1.00f, 0.15f, 1.05f, 0.06f, 0.32f },
            /* Chrome  */ { 2.05f, 0.52f, 0.72f, 0.62f, 0.03f, 0.72f, 0.02f, 0.14f },
            /* Vintage */ { 0.66f, 1.38f, 1.45f, 1.25f, 0.30f, 1.50f, 0.18f, 0.66f },
            /* Worn    */ { 0.44f, 0.95f, 1.15f, 1.60f, 0.72f, 1.95f, 0.35f, 0.80f },
            /* Chewed  */ { 0.25f, 0.70f, 1.90f, 2.00f, 1.00f, 3.10f, 0.78f, 1.00f },
            /* Hot     */ { 1.18f, 1.45f, 2.30f, 0.72f, 0.10f, 0.85f, 0.04f, 1.00f },
            /* Cold    */ { 1.60f, 0.52f, 0.42f, 1.40f, 0.40f, 1.30f, 0.24f, 0.02f }
        };
        return T[(unsigned) c < 8u ? c : 0];
    }

    // ── the head pattern (back dropdown 2) ────────────────────────────────────
    //  Which of the four playback heads are down, how loud, and where they sit in
    //  the image. This is the RE-201's mode selector: the whole character of a
    //  multi-head echo is which heads you lower onto the tape.
    // 🔑🔑 fb368 — A HEAD STACK IS PHYSICAL, AND IT DOES NOT NEED THE ECHO TO EXIST.
    // Max: "the heads don't really sound anything different." Measured: with the echo OFF
    // — which is the DEFAULT — all eight patterns were BIT-IDENTICAL, 0.00 dB apart. The
    // pattern only ever gated the long echo taps, so with Delay off it gated nothing and
    // the control had never done anything at all for anyone who had not switched the echo
    // on. But a real deck's heads sit a CENTIMETRE apart on the tape path: the stack reads
    // the same tape at slightly different points, which is a short comb and a stereo image,
    // echo or no echo. spMs is that spacing, and it is what makes the eight patterns eight
    // different machines instead of eight identical ones.
    struct HeadSpec { float g[kHeads]; float pan[kHeads]; int fbHead; float spMs; };

    static const HeadSpec& headSpec (int h) noexcept
    {
        static const HeadSpec T[8] = {
            /* Single  */ { {0.00f,0.00f,0.00f,1.00f}, { 0.0f, 0.0f, 0.0f, 0.0f}, 3, 0.00f },
            /* Dual    */ { {0.82f,0.00f,0.00f,1.00f}, {-0.30f,0.0f, 0.0f, 0.30f}, 3, 2.20f },
            /* Triple  */ { {0.74f,0.80f,0.00f,1.00f}, {-0.42f,0.0f, 0.0f, 0.42f}, 3, 3.60f },
            /* Quad    */ { {0.68f,0.74f,0.80f,1.00f}, {-0.46f,-0.16f,0.16f,0.46f}, 3, 1.10f },
            /* Spread  */ { {1.00f,0.00f,0.00f,0.88f}, {-0.85f,0.0f, 0.0f, 0.85f}, 3, 9.00f },
            /* Swell   */ { {0.30f,0.52f,0.78f,1.00f}, {-0.24f,-0.08f,0.08f,0.24f}, 3, 5.00f },
            /* Ping    */ { {0.95f,0.85f,0.90f,1.00f}, {-0.95f,0.95f,-0.95f,0.95f}, 3, 0.62f },
            /* Cascade */ { {1.00f,0.70f,0.48f,0.32f}, {-0.55f,0.30f,-0.20f,0.10f}, 0, 7.50f }
        };
        return T[(unsigned) h < 8u ? h : 0];
    }

    struct Params
    {
        int   type      = 0;      // 0 Studio (= WireMachine) · 1 Cassette
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
        flW_ = 0; flLpF_ = flLpF2_ = flLpS_ = 0.0f;
        wr_ = 0;
        readPos_ = -(double) (sr_ * 0.38);
        speed_ = 1.0f; spinAcc_ = 0.0; packAcc_ = 0.0;
        wowA_ = wowB_ = 0.0f; wowWalk_ = 0.0f; jumpHold_ = 0;
        for (int k = 0; k <= kHeads; ++k)
        { lossT_[k][0]=lossT_[k][1]=0.0f; lossT2_[k][0]=lossT2_[k][1]=0.0f;
          lossT3_[k][0]=lossT3_[k][1]=0.0f; hpT_[k][0]=hpT_[k][1]=0.0f;
          bp1_[k][0]=bp1_[k][1]=0.0f; bp2_[k][0]=bp2_[k][1]=0.0f; }
        bumpCoefCtr_ = 0; tcEnv_ = 0.0f; dropHold_ = 0;
        ntA_ = ntB_ = ntR_ = 0.0f;
        hpL_ = hpR_ = 0.0f;
        aziL_ = 0.0f; aziD_ = 0.0f; flWan_ = 0.0f;
        for (int i = 0; i < 512; ++i) { hdL_[i] = 0.0f; hdR_[i] = 0.0f; }
        hdW_ = 0;
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
    //  🔑🔑 fb368 — AND IT CANNOT BE ONE NUMBER FOR BOTH. Max: "I should be able to lower
    //  the saturation... and it's at zero and I can't hear it anymore." Measured, Cassette at
    //  Saturate 0 was ALREADY clipping: x5 plus the machine's own 1.58x mid bell plus its
    //  1.5x floor pre-gain puts 0.92 into a cubic clip that breaks at 1.0, so THD was -32 dB
    //  before the knob was touched and the whole travel only moved it 0.63 dB. The knob was
    //  not weak, it had nowhere left to GO. Cassette therefore runs at x2.2 (clean at 0,
    //  clipped at 100 — the whole window inside the knob) while the wire, whose folder needs
    //  real level to fold, stays at x5. Output level is unaffected: the trim below divides by
    //  whatever gain went in.

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const CharSpec& C = charSpec (pr_.character);
        const HeadSpec& H = headSpec (pr_.heads);
        const int    mch = machineFor (pr_.type);
        const Voice& V   = voiceFor (mch);

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
        // ⚠️ CENTRED ON FRESH = 1.0. First cut was 0.55+0.62*sat, which put Fresh at 0.89x
        // and quietly detuned the x2.2/x5.0 machine levels everything else was calibrated
        // against — Cassette Saturate fell from 17.0 to 10.8 dB as a side effect.
        const float cDrv = 0.42f + 0.58f * C.sat;      // Cold 0.66x . FRESH 1.00x . Hot 1.75x
        const float dl = inL * driveLin_ * V.mgain * cDrv, dr = inR * driveLin_ * V.mgain * cDrv;
        float cL, cR;
        machine (dl, dr, cL, cR, C, gate_);
        cL *= trimLin_ * (1.0f / (V.mgain * cDrv));
        cR *= trimLin_ * (1.0f / (V.mgain * cDrv));

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
        // 🔑 THE BANDWIDTH IS THE MACHINE. 12 kHz vs 4.2 kHz — a shell you can hear the
        // cymbals through, and a wire deck you cannot. fb367 had these a third of an
        // octave apart and the two types measured as the same sound with a ripple.
        // Age closes it further, but only to 45% — the rest of Age is EVENTS now, not EQ.
        const float aLPbase = juceClampf (V.hfHz * C.hf * (1.0f - 0.55f * pr_.age), 700.0f, 19000.0f);
        // ⚠️ AND THE HF GOES WITH THE DROPOUT. When oxide lifts off the head it loses the
        // top FIRST and the level second — that is why a real dropout sounds like the tape
        // ducking underwater rather than someone pulling a fader. Coupling them is the
        // single thing that makes Age read as BROKEN instead of as a tone control.
        const float aLP = onePole (aLPbase * (0.20f + 0.80f * dropEnv_));
        lpN3_ = (V.lpN >= 3);
        // the head cannot read back what the track is too narrow to hold: a wire deck is
        // thin and boxy (150 Hz), a shell has real bass (32 Hz). The other half of the split.
        const float aHPh = onePole (juceClampf (V.hpHz / C.lf, 16.0f, 400.0f));
        const bool  doBump = pr_.bump > 0.0005f;
        // ── HEAD BUMP, and this time it is a RESONANCE ───────────────────────────────
        // Max: "Bump also isn't doing anything." Measured: 0 -> 100% moved 110 Hz by
        // 3.9 dB, because it was a one-pole lowpass added back — a gentle shelf, and a
        // shelf on a bass-light rack bus is nothing. A real head bump is a RESONANT peak
        // a few dB wide at the head/tape resonance, so it is now a TPT state-variable
        // bandpass with Q, up to +11 dB, and it still TRACKS THE TRANSPORT so it sags
        // with the pitch during a stop. Coefficients refresh every 32 samples — speed_
        // is a smoothed ramp, so that is inaudible and it keeps one tan() off the
        // per-sample path (the CPU hard rule).
        if (--bumpCoefCtr_ <= 0)
        {
            bumpCoefCtr_ = 32;
            const float fc = juceClampf (V.bumpHz * (0.25f + 0.75f * speed_), 18.0f, 260.0f);
            const float g  = std::tan (3.14159265f * fc / (float) sr_);
            const float k  = 1.0f / V.bumpQ;
            bpA1_ = 1.0f / (1.0f + g * (g + k));
            bpA2_ = g * bpA1_;
            bpA3_ = g * bpA2_;
            kFlF_ = onePole (V.flFast);      // the flutter band edges, per machine
            kFlS_ = onePole (V.flSlow);
            kFlWan_ = onePole (0.55f);       // wander is SLOW — this is wow, not flutter
        }
        // dB -> linear on the bandpass, so 100% is a real +11 dB shove, not a nudge
        const float bg = (std::pow (10.0f, (V.bumpDb * pr_.bump) * 0.05f) - 1.0f) * V.bumpQ;

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
            tapL[k] = playHead (readCubic (ringL_, d), k, 0, aLP, aHPh, doBump, bg);
            tapR[k] = playHead (readCubic (ringR_, d), k, 1, aLP, aHPh, doBump, bg);
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
        float dwL = playHead (cL, kHeads, 0, aLP, aHPh, doBump, bg);
        float dwR = playHead (cR, kHeads, 1, aLP, aHPh, doBump, bg);

        // ── THE HEAD STACK on the direct read (see HeadSpec). Single is one head, so it
        //    takes this path unchanged and the default is bit-identical to before.
        hdL_[(size_t) hdW_] = dwL; hdR_[(size_t) hdW_] = dwR;
        if (H.spMs > 0.001f)
        {
            const float sp = H.spMs * 0.001f * (float) sr_;
            float aL = 0.0f, aR = 0.0f, gs = 0.0f;
            for (int k = 0; k < kHeads; ++k)
            {
                if (H.g[k] <= 0.0f) continue;
                const float off = (float) (kHeads - 1 - k) * sp;
                float vL = dwL, vR = dwR;
                if (off >= 0.5f)
                {
                    const float rd = (float) hdW_ - off;
                    const int i1 = (int) std::floor (rd); const float fr = rd - (float) i1;
                    const float a0 = hdL_[(size_t) ((i1) & 511)], a1 = hdL_[(size_t) ((i1 + 1) & 511)];
                    const float b0 = hdR_[(size_t) ((i1) & 511)], b1 = hdR_[(size_t) ((i1 + 1) & 511)];
                    vL = a0 + (a1 - a0) * fr; vR = b0 + (b1 - b0) * fr;
                }
                const float pan = H.pan[k] * pr_.width;
                aL += vL * H.g[k] * std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f;
                aR += vR * H.g[k] * std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;
                gs += H.g[k] * H.g[k];
            }
            const float rms = std::sqrt (gs);
            const float nrm = 1.0f / (rms > 1.0f ? rms : 1.0f);
            dwL = aL * nrm; dwR = aR * nrm;
        }
        hdW_ = (hdW_ + 1) & 511;

        // ── AZIMUTH is a property of the HEAD, so it belongs on everything the head reads,
        //    not only on the repeats. It was loop-only, i.e. dead with the echo off — which
        //    is one of the three CharSpec fields that made Character feel weak.
        if (C.azi > 0.001f)
        {
            const float aA = onePole (juceClampf (16000.0f * (1.0f - 0.92f * C.azi), 700.0f, 18000.0f));
            aziD_ += aA * (dwL - aziD_);
            dwL = dwL + (aziD_ - dwL) * C.azi;
        }
        wetL += dwL; wetR += dwR;
        // ── THE FLOOR TOP-UP (and why it has to exist) ───────────────────────────────
        // Max: "the hiss is supposed to be more prominent." On CASSETTE that was a pure
        // trim and it is done — hissK carries it, hissTop is 0, the approved voicing is
        // untouched. On STUDIO it is arithmetically impossible through the machine alone:
        // WireMachine's hiss amount ALSO sets dropoutProb = 0.0004*amount^2 per sample, so
        // the +24.5 dB of floor he is asking for costs x91 the dropouts — 1273/sec, which
        // is not a noise floor, it is the fb367 gargle he already rejected, squared.
        //   LAW (fb367, applied structurally): when one knob drives two things, you cannot
        //   calibrate one of them by turning the knob. Split the jobs. The knob drives the
        //   machine at a DROPOUT-SAFE amount and drives the floor separately here.
        // This is a level trim voiced to the wire's own pink-plus-rumble colour — the
        // CHARACTER (wow, saturation, the dropouts themselves) is still the shipped machine,
        // untouched. Gated like everything else, so a silent tape is silent.
        if (V.hissTop > 0.0f)
        {
            const float amp = V.hissTop * C.noise
                            * std::pow (juceClampf (hissGlide_, 0.0f, 1.0f), 0.50f) * gate_;
            if (amp > 1.0e-7f)
            {
                const float wn = (float) (uni() * 2.0 - 1.0);
                ntA_ += 0.28f  * (wn   - ntA_);      // the top, ~2.3 kHz
                ntB_ += 0.045f * (ntA_ - ntB_);      // the body
                ntR_ += 0.004f * (wn   - ntR_);      // motor rumble
                const float n = ntA_ * 0.10f + ntB_ * 1.05f + ntR_ * 2.90f;
                wetL += n * amp;
                wetR += (n * 0.72f + (float) (uni() * 2.0 - 1.0) * 0.06f) * amp;
            }
        }

        // ── TRANSPORT COMPRESSION. A cassette PUMPS: its own level compression is a big
        // part of why a dubbed tape sounds the way it does, and a wire deck barely does it
        // at all. Voiced per machine, so it is one more thing telling the two apart.
        // ⚠️ C.comp was ALSO loop-only, so the stock's own pumping did nothing with the
        // echo off. It is the machine's compression AND the tape's, together, on everything.
        const float compAmt = V.comp + C.comp * 1.35f;
        if (compAmt > 0.01f)
        {
            const float mx = std::max (std::fabs (wetL), std::fabs (wetR));
            tcEnv_ += (mx > tcEnv_ ? 0.004f : 0.00035f) * (mx - tcEnv_);
            const float g = 1.0f / (1.0f + tcEnv_ * 9.0f * compAmt);
            wetL *= g; wetR *= g;
        }

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
            // 🔑🔑 fb368 — FLUTTER MUST NOT BE A SINE. Max: "the flutter sounds like Star Wars
            // lasers. It's supposed to sound like flutter, like wow and flutter on a tape."
            // Measured, and that is exactly what it was: fb367 modulated the delay with a
            // 61-78 Hz "scrape" sinusoid, and FM of a 220 Hz tone at 61 Hz puts a sideband
            // 29.7 dB under the carrier at 159 and 281 Hz — an inharmonic TONE beside every
            // partial. That is a ring modulator wearing a tape badge.
            //
            //   LAW: periodic modulation at an audio-adjacent rate makes DISCRETE SIDEBANDS,
            //   and discrete sidebands are a laser. Real flutter is irregular, so it has to
            //   come from BAND-LIMITED NOISE — noise smears the sidebands into a haze, which
            //   is precisely what a real transport does to a sustained note.
            //
            // The band is two one-poles (fast lowpass minus slow lowpass), voiced per machine:
            // a shell flutters fast and tight (5-22 Hz), a wire deck slow and wide (3-13 Hz).
            // TWO poles on the top edge, not one. A single 6 dB/oct lowpass at 22 Hz is
            // still only 11 dB down at 78 Hz, and that skirt is audible as hair on the
            // note — measured at −45 dB. Cascading it gives 12 dB/oct and puts the
            // modulation where flutter actually lives.
            const float w = (float) (uni() * 2.0 - 1.0);
            flLpF_ += kFlF_ * (w - flLpF_);
            flLpF2_ += kFlF_ * (flLpF_ - flLpF2_);
            flLpS_ += kFlS_ * (flLpF2_ - flLpS_);
            const float m = juceClampf ((flLpF2_ - flLpS_) * 11.5f, -1.0f, 1.0f);
            // taper: real at 25%, dramatic at 100% (the no-plateaus law), and EXACTLY zero
            // at 0 so the path stays bit-identical to not having it.
            const float amt = pr_.flutter * (0.35f + 0.65f * pr_.flutter);
            const float depth = amt * V.flDepth * (float) (sr_ * 0.00055);
            // ⚠️ AND THE STOCK'S OWN WANDER RIDES HERE TOO. C.wander used to scale only
            // wowGen(), which modulates readPos_ — i.e. the ECHO. With the echo off (the
            // default) a Chewed tape wandered exactly as much as a Fresh one: not at all.
            // A slow smoothed-noise term, so a worn stock genuinely will not hold pitch.
            flWan_ += kFlWan_ * ((float) (uni() * 2.0 - 1.0) - flWan_);
            const float wan = flWan_ * 7.0f * C.wander * (float) (sr_ * 0.00055);
            const float d = juceClampf (depth * (1.0f + m) + wan * 0.5f + std::fabs (wan) * 0.5f,
                                        0.0f, 900.0f);
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
    // Both machines read wow/sat/hiss from their OWN slots (TapeProcessor keeps two sets
    // so they can be calibrated independently). Hiss is gated by input presence, so a
    // powered, routed, silent Tape is silent.
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
        // fb368 — "the hiss is supposed to be more prominent." Raised on both machines, and
        // the knob taper pulled flatter so the bottom of the travel is a real noise floor
        // rather than silence. The two numbers stay SEPARATE for the one-knob-two-jobs law
        // above: the cassette's amount buys noise only, the wire's also buys dropouts, so
        // they cannot share a trim. Wire's ceiling is set by where its dropout rate stops
        // being tape and starts being a stutter — measured, not guessed.
        const float hk = juceClampf (hissGlide_, 0.0f, 1.0f);
        const float cassHiss = juceClampf (6.4f * C.noise * std::pow (hk, 0.50f) * gate, 0.0f, 8.0f);
        // ⚠️ CAPPED AT 0.75, and that cap is the DROPOUT budget, not a noise choice:
        // Chewed's noise field alone would take the amount past 2.0, which is 55
        // dropouts/sec. Level comes from hissTop; the cap keeps the gargle impossible.
        const float wireHiss = juceClampf (0.95f * C.noise * std::pow (hk, 0.62f) * gate, 0.0f, 0.75f);
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
        // fb370 sweep — machineFor() only ever returns 1 (Cassette) or 2 (Wire) since the
        // Harmonic Sculptor stopped being a tape type in fb367, so the old mch==0 branch
        // (Studio's Weave-driven wow) was unreachable. Removed rather than left to rot.
        const int   mch = machineFor (pr_.type);
        const float k = pr_.p1 * wander;                 // both machines: the Wow knob
        const float dt = (float) (1.0 / sr_);
        wowA_ += dt; if (wowA_ > 1.0e6f) wowA_ -= 1.0e6f;

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
    // 🔑🔑 fb368 — AGE HAS TO SOUND BROKEN, NOT FILTERED. Max, twice: "the age is supposed
    // to sound more broken up. It doesn't sound broken up. The age just sounds like an EQ."
    // Measured at fb367 and he is exactly right — Age 50% produced ZERO dropout events in
    // seven seconds and 3.19 dB of spectral change, so the entire bottom half of the knob
    // WAS a tone control and nothing else. Three changes, none of them EQ:
    //   1. the events start EARLY and get dense (~1/s at the 15% default, ~5/s at 100%,
    //      ~14/s on Chewed) instead of appearing only past 90%
    //   2. they are ABRUPT (1.5 ms in, 45 ms out) and DEEP (to −30 dB) and they take the
    //      TOP with them — that coupling lives on aLP in process(), and it is the thing
    //      that makes a dropout sound like the tape ducking under water
    //   3. ⚠️ NO CRACKLE. fb368 added impulsive oxide ticks here and Max heard them for
    //      exactly what they measure as: "snare noises... I didn't mean breakup like that."
    //      An 8 ms decay of broadband noise at the signal's own level IS a snare hit. Age
    //      breaks up by DIPPING, never by striking — a dropout falls, it does not hit.
    void agedropout (const CharSpec& C) noexcept
    {
        const float rate = juceClampf ((0.55f + C.drop) * std::pow (pr_.age, 0.75f), 0.0f, 1.6f);
        if (rate <= 0.0005f) { dropEnv_ += 0.004f * (1.0f - dropEnv_); return; }

        // ── the dropout events themselves
        if (dropHold_ > 0) { if (--dropHold_ == 0) dropTgt_ = 1.0f; }
        if (--dropTimer_ <= 0)
        {
            dropTimer_ = (int) (sr_ * (1.0 / (0.1 + 9.0 * (double) rate)) * (0.30 + 1.40 * uni()));
            // sqrt-weighted so most events are shallow scuffs and a few are real holes
            dropTgt_  = 1.0f - (float) std::sqrt (uni()) * 0.97f
                             * juceClampf (0.35f + rate, 0.0f, 1.0f);
            dropHold_ = (int) (sr_ * (0.004 + 0.060 * uni()));   // 4-64 ms of hole
        }
        // ABRUPT in (1.5 ms), lingering out (45 ms) — a shed patch, not a fader move
        dropEnv_ += (dropTgt_ < dropEnv_ ? 0.0139f : 0.00046f) * (dropTgt_ - dropEnv_);

    }

    // ONE playback head, five copies of its state (4 heads + the direct read).
    // Gap loss, head bump and the oxide dropout all live here — so they colour
    // everything read off the tape, and COMPOUND per repeat for free, because the
    // feedback is taken from an already-played tap and recorded again.
    float playHead (float x, int slot, int ch, float aLP, float aHP,
                    bool doBump, float bg) noexcept
    {
        float& lp = lossT_[slot][ch];
        lp += aLP * (x - lp); float y = lp;                 // gap loss / bandwidth
        float& lp2 = lossT2_[slot][ch];
        lp2 += aLP * (y - lp2); y = lp2;                    // ...and it is STEEP
        if (lpN3_) { float& lp3 = lossT3_[slot][ch]; lp3 += aLP * (y - lp3); y = lp3; }
        float& hp = hpT_[slot][ch];
        hp += aHP * (y - hp); y -= hp;                      // the track's low limit
        if (doBump)
        {   // TPT state-variable bandpass, added back — a real resonance with Q
            float& ic1 = bp1_[slot][ch]; float& ic2 = bp2_[slot][ch];
            const float v3 = y - ic2;
            const float v1 = bpA1_ * ic1 + bpA2_ * v3;
            const float v2 = ic2 + bpA2_ * ic1 + bpA3_ * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            y += v1 * bg;
        }
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

    TapeProcessor tapeL_, tapeR_;

    std::vector<float> ringL_, ringR_, flL_, flR_;
    int flW_ = 0;
    float flLpF_ = 0.0f, flLpF2_ = 0.0f, flLpS_ = 0.0f;     // the flutter noise band (fast LP − slow LP)
    float kFlF_ = 0.002f, kFlS_ = 0.0005f;
    int    mask_ = 0;
    long long wr_ = 0;
    double readPos_ = 0.0;
    float  baseDelay_ = 18000.0f;

    float  speed_ = 1.0f;
    double spinAcc_ = 0.0, packAcc_ = 0.0;
    float  driveLin_ = 1.0f, trimLin_ = 1.0f;

    float  wowA_ = 0.0f, wowB_ = 0.0f, wowWalk_ = 0.0f;
    int    jumpHold_ = 0;

    float  lossT_[kHeads + 1][2] = {}, lossT2_[kHeads + 1][2] = {}, lossT3_[kHeads + 1][2] = {};
    float  hpT_[kHeads + 1][2] = {};
    bool   lpN3_ = true;
    float  bp1_[kHeads + 1][2] = {}, bp2_[kHeads + 1][2] = {};   // head-bump SVF state
    float  bpA1_ = 0.0f, bpA2_ = 0.0f, bpA3_ = 0.0f;
    int    bumpCoefCtr_ = 0;
    float  hpL_ = 0.0f, hpR_ = 0.0f, aziL_ = 0.0f, aziD_ = 0.0f;
    float  hdL_[512] = {}, hdR_[512] = {};   // the head STACK's own short line
    int    hdW_ = 0;
    float  flWan_ = 0.0f, kFlWan_ = 0.00008f;
    float  dropEnv_ = 1.0f, dropTgt_ = 1.0f, compEnv_ = 0.0f, tcEnv_ = 0.0f;
    float  ntA_ = 0.0f, ntB_ = 0.0f, ntR_ = 0.0f;   // the floor top-up's colour
    int    dropTimer_ = 0, walkTimer_ = 0, dropHold_ = 0;
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
