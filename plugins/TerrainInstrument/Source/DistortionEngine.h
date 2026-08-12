#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
// (no TerrainFilters.h include — the engine is self-contained, which also lets the offline harness
//  compile THIS EXACT HEADER against a 20-line juce shim: zero transcription drift, per the fb283 law)

/*  DistortionEngine.h — the FX-rack Distortion device (fb315)
    ================================================================================================
    ONE device, 23 modes in 6 families. Full spec: Design/DISTORTION-BUILD-BIBLE.md

    THIS FILE IS THE SHARED SHELL. Every mode plugs into exactly two functions:

        shape (u, mode, knee, chr)   ->  f(u)     the transfer curve, bounded
        anti  (u, mode, knee, chr)   ->  F1(u)    its antiderivative, for ADAA

    Add a mode = add one case to each. Nothing else changes. That is deliberate: 23 modes must not
    become 23 special cases, and every mode gets the shell's anti-aliasing, DC blocking, emphasis
    pair, auto-gain and click-free ramps for free.

    API mirrors DelayEngine.h exactly so it drops into the rack the same way:
      prepare(double) · clamped setters · processSample(inL,inR,outL&,outR&) · flush()
      processSample returns WET ONLY — the processor owns Mix.

    ── THE LAWS THIS FILE ENCODES ──────────────────────────────────────────────────────────────────
    1. NO PLAYING SAFE. driveDb = D_max * t^0.8 (near-linear in dB — the ear hears dB), and the clip
       threshold sits at -6 dBFS INTERNALLY so a synth bus peaking at -26 dBFS is already working at
       modest Drive. A linear `1 + amt*k` multiplier and a 0 dBFS threshold are what produced every
       dead first third in this plugin.
    2. THE CURVE MUST NOT SELF-NORMALISE. Writing y = tanh(g*x)/tanh(g) makes every Drive setting a
       rescaled copy of ONE shape; auto-gain then removes the only difference left and Drive feels
       dead. The knee lives in ABSOLUTE input units, so as Drive rises the knee shrinks relative to
       the signal and the curve genuinely morphs gentle -> razor.
    3. EXACT DC REMOVAL SUBTRACTS f(bias), NOT bias. y = (f(g*x + b) - f(b)) * makeup, then a
       sample-rate-aware 10 Hz high-pass as belt-and-braces. Subtracting `b` leaves an offset that is
       a guaranteed note-off click at the +-1.0 bias ranges this device uses.
    4. ADAA-1 with BOTH documented escapes: |dx| < 1e-5 -> midpoint (ill-conditioned divide);
       |dx| > 0.9 -> direct (a big jump must not drop out). Same pattern as the shipped
       TerrainFilters::WaveShaper and SynthVoice's fold ADAA.
    5. AUTO IS OFF BY DEFAULT and only ~70% when on, on a SLOW (~300 ms) RMS tracker. Full
       normalisation is the single biggest cause of a distortion feeling timid; a fast tracker also
       erases the ANALOG family's sag duck, which is the one behaviour worth its CPU.

    ⏭️ NOT YET IN THIS BUILD (each is additive, none changes the structure):
       * OVERSAMPLING. Modes declare a floor of ADAA-1 + 2x; today only the ADAA runs. Measured
         in-tree: ADAA-1 alone takes a hard clip from 61.0 -> 68.1 dB alias SNR at 220 Hz, so this is
         a real improvement over naive, just not the final number. Wire juce::dsp::Oversampling
         (filterHalfBandPolyphaseIIR, maxQuality, integerLatency) next, with the fixed-8-sample
         latency + dry-path compensation of bible 4.4.
       * The SHAPER family (4 modes + the transfer-curve editor). 19 of 23 modes live as of fb324:
         ANALOG (Phase C — Tube/Tape/Transformer/Stomp Box/Overdrive, the stateful family) + CLIP +
         DIODE + FOLD + DIGITAL, each with its 8 Character voicings.
*/

namespace tw
{

class DistortionEngine
{
public:
    // Mode indices — INDEX-ALIGNED with SYN_DST_TYPE and the UI optgroup list.
    enum Mode {
        Tube = 0, Tape, Transformer, StompBox, Overdrive,          //  0-4  ANALOG
        SoftClip, HardClip, ZeroSquare, SlewClip,                  //  5-8  CLIP
        Diode1, Diode2, Asym, Rectify,                             //  9-12 DIODE
        LinearFold, SineFold, WestCoast,                           // 13-15 FOLD
        Shaper, ShaperAsym, Harmonics, Table,                      // 16-19 SHAPER
        Downsample, Bitcrush, Overflow,                            // 20-22 DIGITAL
        kNumModes
    };

    // ── setup ───────────────────────────────────────────────────────────────
    // ── 2× OVERSAMPLER (fb318) — halfband FIR, DERIVED here and MEASURED before shipping ──────────
    // Why it exists: at 1× the nonlinearity's images fold straight back into the audible band, and the
    // ADAA that used to suppress them cost 11.8 dB of top octave (fb317). Oversampling fixes BOTH —
    // measured on scratchpad/os2x_verify.cpp: linear response FLAT to 21 kHz (−0.20 dB, identical to
    // no processing at all) AND alias SNR 24.5 → 38.1 dB on a hard-clipped 1245 Hz tone.
    //
    // 🔑 THE STRUCTURE IS SIMPLE because a halfband splits into two polyphase branches over ONE input
    // ring, and with an EVEN centre tap the even branch is a PURE DELAY:
    //     upsample :  v[2n]   = x[n−C/2]                 v[2n+1] = 2·Σ h[odd]·x[n−j]
    //     decimate :  w[n]    = h[C]·sEven[n−C/2] + Σ h[odd]·sOdd[n−j−1]
    // ⚠️ That `−j−1` is not a typo and it is the bug that cost a build: s[2n−2j−1] sits at an ODD
    // index, so it is sOdd[n−j−1], NOT sOdd[n−j]. Index it one early and the two branches sum
    // incoherently and comb — which measures as the exact cos(πf/fs) rolloff we were trying to remove.
    // fb321 — T 97 → 129. The 97-tap stopband was only −38 dB at 26 kHz, and at hot drive the folded
    // images sat ABOVE the analyzer floor: the "icicles between the harmonics" Max A/B'd against
    // Serum's clean comb. 129 taps ⇒ ~−67 dB at 26 kHz / −90 at 30 kHz, passband still 0.00 dB at
    // 20 kHz (measured in hb_design.cpp). Centre 64 stays EVEN so the pure-delay branch survives.
    // Cost: 32 more macs/sample of one polyphase branch — negligible.
    static constexpr int kT  = 129;         // taps (centre 64 = EVEN, so the even branch is a delay)
    static constexpr int kC  = 64;
    static constexpr int kNH = (kT - 1) / 2;// taps per polyphase branch
    static constexpr int kRB = 128, kRM = 127;   // ring size / mask (must exceed kNH)
    /** Latency of the up+down pair, in BASE-rate samples. Both stages delay by kC/2. */
    static constexpr int kOsLatency = kC;   // 32 + 32 = 64 (~1.3 ms, compensated internally)

    void prepare (double sampleRate) noexcept
    {
        fs_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

        // Halfband: windowed sinc at fc = 0.25 (normalised to the OVERSAMPLED rate), Blackman window,
        // normalised to unity DC. Measured: 0.00 dB at 20 kHz, −0.11 at 22 kHz, −38 dB at 26 kHz.
        double sum = 0.0;
        for (int n = 0; n < kT; ++n)
        {
            const int m = n - kC;
            const double s = (m == 0) ? 0.5
                                      : std::sin (juce::MathConstants<double>::pi * 0.5 * m)
                                        / (juce::MathConstants<double>::pi * m);
            const double w = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / (kT - 1))
                                  + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * n / (kT - 1));
            hb_[n] = (float) (s * w); sum += hb_[n];
        }
        for (int n = 0; n < kT; ++n) hb_[n] = (float) (hb_[n] / sum);

        // 10 Hz DC blocker, sample-rate aware (the in-tree TerrainFilters one hardcodes 0.995,
        // which is 38 Hz at 48k and 76 Hz at 96k — it changes character with sample rate).
        dcR_ = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 10.0 / fs_);
        dcR_ = juce::jlimit (0.90f, 0.99999f, dcR_);

        // Auto-gain: ~300 ms. NOT 10 ms — a fast tracker level-compensates the sag duck away and
        // makes the stateful modes measure and sound like static curves.
        autoA_ = (float) std::exp (-1.0 / (0.300 * fs_));

        // Param glide ~15 ms (the DelayEngine idiom): a hard knob jump or an LFO square can never burst.
        smth_  = (float) (1.0 - std::exp (-1.0 / (0.015 * fs_)));

        xfEdK_ = onePole2 (4000.0f);   // fb324 — transformer core-loss pole (2× rate)

        // fb325 — sag attack (~4 ms fixed; release = Recovery) and the input-envelope gate that
        // keeps every feedback path silent without input (2 ms attack / 250 ms release).
        aAtk4_ = 1.0f - (float) std::exp (-1.0 / (fs_ * 0.004));
        envA_  = 1.0f - (float) std::exp (-1.0 / (fs_ * 0.002));
        envR_  = 1.0f - (float) std::exp (-1.0 / (fs_ * 0.060));   // fb325: the free-run gate closes in
                                                                    // ~0.4 s — scream dies WITH the note

        flush();
    }

    void flush() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            st_[c] = AdaaState{};
            dcX_[c] = dcY_[c] = 0.0f;
            emphZ_[c] = deEmphZ_[c] = 0.0f;
            hiCutZ_[c] = loCutZ_[c] = 0.0f;
            for (int n = 0; n < kRB; ++n) { xr_[c][n] = se_[c][n] = so_[c][n] = dry_[c][n] = 0.0f; }
            xi_[c] = si_[c] = di_[c] = 0;
            fbZ_[c] = punchZ_[c] = 0.0f;
            toneZ_[c] = 0.0f;
            slewZ_[c] = holdV_[c] = holdPh_[c] = smZ_[c] = 0.0f;
            sqzZ_[c] = sagZ_[c] = zsSgn_[c] = zsEnv_[c] = zsLp_[c] = 0.0f;
            env40_[c] = jWalk_[c] = slewLim_[c] = prevU_[c] = 0.0f;
            hold2V_[c] = hold2Ph_[c] = errZ_[c] = telHp_[c] = wcLp_[c] = 0.0f;
            mirF_[c] = 1.0f;
            rngC_[c] = 0x12345678u;
            // fb324 — ANALOG family state
            sagV_[c] = sagIn_[c] = 0.0f;
            drPh_[c] = drTgt_[c] = drZ_[c] = drW_[c] = 0.0f;
            wp1_[c] = wp2_[c] = wp3_[c] = 0.0f; dwi_[c] = 0;
            for (int n = 0; n < kDr; ++n) drRing_[c][n] = 0.0f;
            tubeBlk_[c] = tubeIgS_[c] = 0.0f;
            tapeM_[c] = tapeH1_[c] = tapeHd_[c] = tapeGl_[c] = wornW_[c] = 0.0f;
            xfPhi_[c] = xfPhiP_[c] = xfB_[c] = xfEd_[c] = 0.0f;
            sbLp_[c] = sbTn_[c] = 0.0f;
            for (int s = 0; s < 3; ++s) odSl_[s][c] = odLp_[s][c] = 0.0f;
            bqX1_[c] = bqX2_[c] = bqY1_[c] = bqY2_[c] = 0.0f;
            hiCut2_[c] = hiCut3_[c] = inEnv_[c] = 0.0f;      // fb325
            sbFlip_[c] = 1.0f; sbPrev_[c] = sbEnv_[c] = sbLp2_[c] = fbLp_[c] = kpZ_[c] = 0.0f;
        }
        drNxt_ = 1.0f; drS_ = 0.0f; emphP_ = 9.0f; wRotP_ = -1.0f; fbLpP_ = -1.0f;
        // invalidate the analog caches so a flush (or sample-rate change) recomputes everything
        bqMode_ = -1; tubeCk_ = -1; sbChrP_ = -1; odChrP_ = -1;
        recovP_ = rateP_ = sbDrvP_ = odDrvP_ = tpDrvP_ = -1.0f;
        slamC_ = slamT_;
        dip_ = 1.0f;
        for (int k = 0; k < 8; ++k) pC_[k] = pT_[k];
        autoZ_ = 0.0f;
        // Snap every smoothed value to its target so a flush is silent, not a ramp from zero.
        driveC_ = driveT_; kneeC_ = kneeT_; biasC_ = biasT_; toneC_ = toneT_;
        emphC_  = emphT_;  loC_   = loT_;   hiC_   = hiT_;   mixC_  = mixT_;
    }

    // ── setters (all clamped; all glide per-sample) ──────────────────────────
    void setMode (int m) noexcept
    {
        const int nm = juce::jlimit (0, (int) kNumModes - 1, m);
        if (nm != mode_) dip_ = 0.0f;      // fb320 — type switch dips the wet through 0 (~15 ms), no click
        mode_ = nm;
    }
    void setCharacter (int c) noexcept { chr_  = juce::jlimit (0, 7, c); }
    void setQuality   (int q) noexcept { qual_ = juce::jlimit (0, 3, q); }
    void setAuto      (bool b) noexcept { autoOn_ = b; }
    void setPill2     (bool b) noexcept { pill2_  = b; }

    /** 0..1 -> driveDb = D_max * t^0.8, then linear. NEVER a `1 + amt*k` multiplier.
        ⚠️ FOLD is the one family with a DIFFERENT taper (bible §2.5): fold count is a FLOOR function
        of gain, so a dB taper staircases the timbre — FOLD gets `g = 1 + t·62`, LINEAR, so the fold
        count itself is linear in the knob. Call setMode() before setDrive() (the processor does). */
    void setDrive (float t01) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, t01);
        drive01_ = t;                                      // kept raw: the DIODE falling-threshold law needs it
        if (family() == FAM_FOLD) driveT_ = 1.0f + t * 62.0f;
        else                      driveT_ = std::pow (10.0f, maxDriveDb() * std::pow (t, 0.8f) * 0.05f);
    }
    /** Which of the SIX parameter-set families the current mode belongs to. The back-8 is keyed to
        the FAMILY, not the mode — that is what lets 6 param sets serve 23 modes without either a soup
        of near-identical labels or 23 separate relabel maps. */
    enum Family { FAM_ANALOG = 0, FAM_CLIP, FAM_DIODE, FAM_FOLD, FAM_SHAPER, FAM_DIGITAL };
    static Family familyOf (int mode) noexcept
    {
        if (mode <= Overdrive)  return FAM_ANALOG;
        if (mode <= SlewClip)   return FAM_CLIP;
        if (mode <= Rectify)    return FAM_DIODE;
        if (mode <= WestCoast)  return FAM_FOLD;
        if (mode <= Table)      return FAM_SHAPER;
        return FAM_DIGITAL;
    }
    Family family() const noexcept { return familyOf (mode_); }

    /** Back-panel slot i (0..7). ⚠️ THE MEANING IS PER FAMILY:
          ANALOG  Low Cut · Hi Cut · Emphasis · Sag   · Recovery · Drift  · Drift Rate · Snarl
          CLIP    Low Cut · Hi Cut · Emphasis · Width · Bias     · Gap    · Punch      · Feedback
          DIODE   Low Cut · Hi Cut · Emphasis · Width · Knee     · DeadZn · Slew       · Snarl
          FOLD    Low Cut · Hi Cut · Emphasis · Stages· Spacing  · Rebnd  · Corner     · Width
          SHAPER  Low Cut · Hi Cut · Emphasis · Width · Smooth   · Bias   · Beyond     · Squash
          DIGITAL Low Cut · Hi Cut · Bits/Rate· Smooth· Dither   · Jitter · Spread     · Feedback
        Slots 0 and 1 are Low Cut / Hi Cut in EVERY family, so their coefficients resolve here at
        block rate rather than per sample. */
    void setP (int i, float v01) noexcept
    {
        if (i < 0 || i >= 8) return;
        const float v = juce::jlimit (0.0f, 1.0f, v01);
        pT_[i] = v;
        if (i == 0) loT_ = onePole (20.0f * std::pow (60.0f, v));
        // fb325 — Hi Cut got "the power of Sag" (Max): 3-pole cascade (−18 dB/oct) + bottom at
        // 350 Hz (distorted-telephone territory). True bypass at max, as before.
        if (i == 1) { hiBypass_ = (v >= 0.999f); hiT_ = onePole (350.0f * std::pow (63.0f, v)); }
    }

    void setKnee  (float v01) noexcept { kneeT_ = juce::jlimit (0.0f, 1.0f, v01); }
    /** 0..1 UI -> -1..+1 internal. At +-1.0 the shaper sits a full threshold off zero. */
    void setBias  (float v01) noexcept { biasT_ = juce::jlimit (0.0f, 1.0f, v01) * 2.0f - 1.0f; }
    void setTone  (float v01) noexcept { toneT_ = juce::jlimit (0.0f, 1.0f, v01) * 2.0f - 1.0f; }
    /** The pre/de-emphasis PAIR: +-18 dB of tilt hinged ~1.2 kHz. +-6 dB would be the timid version. */
    void setEmphasis (float v01) noexcept { emphT_ = juce::jlimit (0.0f, 1.0f, v01) * 2.0f - 1.0f; }
    void setLowCut   (float v01) noexcept { loT_ = onePole (20.0f  * std::pow (60.0f,  juce::jlimit (0.0f, 1.0f, v01))); }
    void setHiCut    (float v01) noexcept
    {
        const float v = juce::jlimit (0.0f, 1.0f, v01);
        hiBypass_ = (v >= 0.999f);                     // top of the knob = OFF, not "open"
        hiT_ = onePole (350.0f * std::pow (63.0f, v));
    }

    /** 🔑 THE ENGINE OWNS MIX — a deliberate departure from DelayEngine's "processor owns Mix".
        The 2× resampler delays the wet by kOsLatency (48) samples. Only the engine holds BOTH the wet
        and the input, so only the engine can align them exactly; doing Mix outside would blend a
        delayed wet against an undelayed dry and comb at every Mix < 100%. The processor therefore does
        a simple env-gated replace (`+= env*(out − in)`), which is 0 when the device is off. */
    void setMix (float v01) noexcept { mixT_ = juce::jlimit (0.0f, 1.0f, v01); }

    /** Reported as 0 on purpose: for an INSTRUMENT the 1 ms has nothing to phase against, and
        reporting it would force an always-on host delay and break the byte-identical dry default.
        Internally it is fully compensated (the dry path runs through a matching delay). Bible §4.4. */
    int  getLatencySamples() const noexcept { return 0; }

    // ── the hot path. Returns WET ONLY; the processor owns Mix. ───────────────
    void processSample (float inL, float inR, float& outL, float& outR) noexcept
    {
        // Per-sample glide on everything (no-clicks hard rule).
        driveC_ += (driveT_ - driveC_) * smth_;
        kneeC_  += (kneeT_  - kneeC_ ) * smth_;
        biasC_  += (biasT_  - biasC_ ) * smth_;
        toneC_  += (toneT_  - toneC_ ) * smth_;
        emphC_  += (emphT_  - emphC_ ) * smth_;
        loC_    += (loT_    - loC_   ) * smth_;
        hiC_    += (hiT_    - hiC_   ) * smth_;
        mixC_   += (mixT_   - mixC_  ) * smth_;
        dip_    += (1.0f    - dip_   ) * smth_;              // fb320 — mode-switch wet dip recovery
        slamC_  += (slamT_  - slamC_ ) * smth_;              // fb324 — ANALOG Slam pill (+20 dB), click-free
        for (int k = 0; k < 8; ++k) pC_[k] += (pT_[k] - pC_[k]) * smth_;

        // ── PER-FAMILY SLOT RESOLVE (bible §5.5) — the same 8 raw params mean different things ────
        //   emphasis: P3 everywhere EXCEPT DIGITAL (its P3 is the second destruction axis)
        //   knee/bias/asym: SIG is Knee only for CLIP; DIODE's knee is P5 and its SIG is Asym;
        //   FOLD's SIG is Symmetry, which IS the bias axis (DC pre-fold, removed after);
        //   SHAPER's bias is P6; ANALOG's SIG is Bias (voiced in Phase C).
        const Family fam = family();
        emphC_ = (fam == FAM_DIGITAL) ? 0.0f : (pC_[2] * 2.0f - 1.0f);
        switch (fam)
        {
            case FAM_CLIP:    kneeE_ = kneeC_;  biasE_ = pC_[4] * 2.0f - 1.0f; asymE_ = 0.0f;               break;
            // fb322 — the ASYM MODE's offset goes PRE-DRIVE via the bias path (the fb315 lesson,
            // relearned: a post-drive offset is swamped by the driven signal and the knob reads dead).
            case FAM_DIODE:   kneeE_ = pC_[4];
                              asymE_ = kneeC_ * 2.0f - 1.0f;
                              // Asym = full bias injection. Diode 1/2 = partial (mismatched diodes
                              // shift the operating point too — and measured, rail-HEIGHT asymmetry
                              // alone gives only DC + odd harmonics; even harmonics need the DUTY
                              // shift, which the bias provides). Rectify: NONE — its SIG is the
                              // rectification amount, not an offset.
                              biasE_ = (mode_ == Asym)    ? asymE_ * 0.8f
                                     : (mode_ == Rectify) ? 0.0f
                                                          : asymE_ * 0.3f;      break;
            case FAM_FOLD:
            {
                kneeE_ = 0.5f;  biasE_ = (kneeC_ * 2.0f - 1.0f) * 1.2f;  asymE_ = 0.0f;
                // Character ladder biases (bible §9.4): Linear Fold = Serge/Ladder/Compressed/
                // Expanded/Lossy/Runaway/Rounded/Broken-Mirror · West Coast circuits set their own
                // stage caps (Buchla museum = 5 cells, uFold = 6). {stageMul, spacOff, rebMul, cornOff}
                static constexpr float LB[8][4] = {
                    {1,0,1,0},{2,0,1,0},{1,-0.45f,1,0},{1,0.9f,1,0},
                    {1,0,0.55f,0},{1,0,1.45f,0},{1,0,1,0.75f},{0.5f,0.3f,0.85f,0} };
                static constexpr float WB[8][4] = {
                    {0.16f,0,1,0},{1,0,1,0},{1,0,0.9f,0.1f},{1,0,0.9f,0.35f},
                    {1,0.15f,1.1f,-1.0f},{0.19f,0,1,0.2f},{1,0,1,0},{1,0,1,0} };
                const int ci = juce::jlimit (0, 7, chr_);
                if      (mode_ == LinearFold) { fStB_ = LB[ci][0]; fSpO_ = LB[ci][1]; fRbM_ = LB[ci][2]; fCnO_ = LB[ci][3]; }
                else if (mode_ == WestCoast)  { fStB_ = WB[ci][0]; fSpO_ = WB[ci][1]; fRbM_ = WB[ci][2]; fCnO_ = WB[ci][3]; }
                else                          { fStB_ = 1.0f; fSpO_ = 0.0f; fRbM_ = 1.0f; fCnO_ = 0.0f; }
                break;
            }
            case FAM_SHAPER:  kneeE_ = 0.5f;    biasE_ = pC_[5] * 2.0f - 1.0f; asymE_ = 0.0f;               break;
            // fb324 — ANALOG: SIG = Bias (bipolar, consumed per-circuit in its own physical units);
            // Slam = Decapitator's Punish, +20 dB of pre-gain ON TOP of Drive (the ×10 target glides).
            case FAM_ANALOG:  kneeE_ = 0.5f;    biasE_ = kneeC_ * 2.0f - 1.0f; asymE_ = 0.0f;
                              slamT_ = pill2_ ? 10.0f : 1.0f;                                     break;
            default:          kneeE_ = 0.0f;    biasE_ = 0.0f;                 asymE_ = 0.0f;               break;
        }

        // fb325 — emphasis gain, dB-symmetric (see one(); cached: exp only when the knob moves)
        if (std::fabs (emphC_ - emphP_) > 1.0e-4f) { emphP_ = emphC_; emphG_ = std::exp (2.0723f * emphC_); }

        if (fam == FAM_DIGITAL)
        {
            // ── DIGITAL runs in L/R, at 1×, with NO anti-aliasing — the artefacts ARE the product,
            // M/S would misdomain the destroyer, and `Spread` decorrelates the channel CLOCKS instead.
            outL = one (inL, 0, driveC_);
            outR = one (inR, 1, driveC_);
        }
        else
        {
            // ── WIDTH: 0 = only the MID is driven, sides pass clean · centre = matched M/S ·
            // fb325 — the TOP HALF LEAVES M/S: harness-caught, on a MONO source there IS no side,
            // so 50..100 was a structural no-op. The encode rotates from 45° (pure M/S — the maths
            // below is byte-identical to fb324 at centre) toward 90° (pure L/R): the two channels
            // then receive DIFFERENT drive, so the clipping itself decorrelates L from R — real
            // stereo out of a mono input (the FOLD threshold-skew law, made family-wide).
            const float w01 = (fam == FAM_FOLD) ? pC_[7] : (fam == FAM_ANALOG ? 0.5f : pC_[3]);
            const float wS  = juce::jmin (2.0f, w01 * 2.0f);
            const float dM  = driveC_;
            const float dS  = 1.0f + (driveC_ - 1.0f) * wS;   // linear-from-unity, no pow per sample
            if (std::fabs (w01 - wRotP_) > 1.0e-4f)
            {
                wRotP_ = w01;
                const float phi = 0.7853982f * (1.0f + juce::jmax (0.0f, w01 - 0.5f) * 2.0f);
                wCs_ = std::cos (phi) * 0.70710678f;  wSn_ = std::sin (phi) * 0.70710678f;
            }
            const float u1 = wCs_ * inL + wSn_ * inR;         // 45°: = 0.5(L+R), the old mid
            const float u2 = wSn_ * inL - wCs_ * inR;         // 45°: = 0.5(L−R), the old side
            const float y1 = one (u1, 0, dM);
            const float y2 = one (u2, 1, dS);
            outL = 2.0f * (wCs_ * y1 + wSn_ * y2);            // exact inverse rotation
            outR = 2.0f * (wSn_ * y1 - wCs_ * y2);
        }

        // Auto: ~70% compensation on a slow RMS tracker, so 100% Drive still gets LOUDER as well as
        // nastier. OFF by default — full normalisation is the timidity culprit.
        const float mag = 0.5f * (std::fabs (outL) + std::fabs (outR));
        autoZ_ += (mag - autoZ_) * (1.0f - autoA_);
        if (autoOn_)
        {
            const float g = (autoZ_ > 1.0e-5f) ? std::pow (juce::jlimit (0.02f, 8.0f, refLevel_ / autoZ_), 0.7f) : 1.0f;
            outL *= g; outR *= g;
        }
    }

private:
    struct AdaaState { float x1 = 0.0f, F1 = 0.0f; bool have = false; };

    // ── the per-channel chain ────────────────────────────────────────────────
    float one (float x, int c, float drv) noexcept
    {
        const float in0 = x;                 // untouched input, for the latency-aligned dry
        if (family() == FAM_DIGITAL) return digitalOne (x, c, drv);   // 1×, L/R, no resampler (fb320)
        // fb325 — NOTHING SOUNDS WITHOUT INPUT (hard rule, Max: Stomp Snarl was a free-running 808).
        // Every self-oscillating feedback path is gated by the input envelope (2 ms / 250 ms): the
        // scream rings and DIES with the note — it follows what you play, never runs on its own.
        inEnv_[c] += (std::fabs (in0) - inEnv_[c]) * ((std::fabs (in0) > inEnv_[c]) ? envA_ : envR_);
        const float fbGate = juce::jmin (1.0f, inEnv_[c] * 30.0f);
        // LOW CUT is PRE the shaper — it decides what is allowed to hog the curve at all.
        loCutZ_[c] += (x - loCutZ_[c]) * loC_;
        x -= loCutZ_[c];

        // EMPHASIS (pre). The inverse runs after the shaper; the pair does NOT cancel because a
        // nonlinearity sits between them — that non-cancellation IS the effect.
        // fb325 — dB-SYMMETRIC tilt (±18 dB): the old linear form (1 + e·2) hit EXACTLY ZERO at
        // 25 % of the knob — the de-emphasis divided by it (infinite gain = the click Max heard at
        // 25 %) and below 25 % the highs came back PHASE-FLIPPED. g = e^(2.07·e) is never ≤ 0.
        if (std::fabs (emphC_) > 1.0e-4f)
        {
            emphZ_[c] += (x - emphZ_[c]) * kEmphCoef;
            x = emphZ_[c] + (x - emphZ_[c]) * emphG_;
        }

        // ── the nonlinearity ─────────────────────────────────────────────────
        // 🔑 MEASURED CALIBRATION (harness dst_harness.cpp, driven at the REAL -26 dBFS bus level).
        // kPreGain was 4.0 and Hard Clip then measured THD = 0.00% for the FIRST 20% OF TRAVEL — the
        // textbook dead first third, because a hard clipper below threshold does literally nothing and
        // 0.05 * 4 * g only reaches the knee at g ≈ 4.6. At 8.0 the knee is reached by ~10% travel, so
        // every degree of the knob is alive on the razor mode too. Soft Clip is unaffected at the top
        // (bounded) and merely starts a touch warmer.
        //
        // BIAS IS APPLIED PRE-DRIVE, not post. Post-drive (`x*P*g + b`) the offset is swamped by the
        // driven signal — measured even/odd swing was only 0.000 -> 0.411 against a >4 gate. Pre-drive
        // it stays proportionally significant at EVERY drive setting, which is also the real pedal
        // topology (bias the junction, then drive it) and is what produces the documented one-sided
        // gated spit at extreme settings.
        // ── 2× OVERSAMPLED NONLINEARITY ──────────────────────────────────────
        // Only the SHAPER is oversampled; the tone/emphasis/DC filters stay at base rate (bible §9.1
        // chain order). Upsample → shape both phases → decimate.
        const Family fam = family();
        // fb324 — ANALOG (Phase C): the state ODEs run at BASE rate (bible §9.1 — they are sub-audio
        // integrators and generate no HF; 1× state is what makes Tube as cheap as Stomp Box), and the
        // Drift pair's wow/bias wander is applied to the input BEFORE the resampler.
        if (fam == FAM_ANALOG) x = analogTick (x, c);
        float* xr = xr_[c]; int& xi = xi_[c];
        xr[xi & kRM] = x;
        const float pA = xr[(xi - kC / 2) & kRM];                       // even branch = PURE DELAY
        float pB = 0.0f;
        for (int j = 0; j < kNH; ++j) pB += hb_[2 * j + 1] * xr[(xi - j) & kRM];
        pB *= 2.0f;                                                      // zero-stuff gain
        ++xi;
        // ⚠️🔑 ADAA IS DISABLED UNTIL OVERSAMPLING LANDS — and that is a DELIBERATE, MEASURED choice.
        // At 1x, first-order ADAA is y = (F(x0)-F(x1))/(x0-x1), which in the shaper's near-linear
        // region collapses to EXACTLY (x0+x1)/2 — a 2-tap FIR averager whose response is cos(pi*f/fs),
        // i.e. a HARD NULL AT NYQUIST. Measured on the shipped chain (scratchpad/hf_probe.cpp), against
        // theory in the last column:
        //     12k -3.15 (-3.01) · 16k -6.14 (-6.02) · 20k -11.84 (-11.74) · 22k -17.78 (-17.69)
        // That is the "our high end closes at the very top right, Serum's stays edge to edge" report —
        // the anti-aliasing was eating the top octave. The bible (§3.6) says it outright: ADAA must
        // NEVER run at 1x. Naive measures FLAT (-0.18 dB at every frequency tested).
        // TRADE, stated honestly: naive at 1x loses ~7 dB of alias suppression at low fundamentals and
        // much more on high notes. The REAL fix is oversampling, where the aperture null moves out of
        // the audio band entirely (4x ⇒ only -0.47 dB at 20 kHz) AND alias rejection improves past what
        // 1x ADAA ever gave (measured elsewhere: 4x naive 64.1 dB vs 1x naive 35.7 dB). Until then the
        // top end wins, because a 12 dB hole is far more audible than hash 35 dB down.
        // ⏭️ When oversampling lands: set useAdaa_ = true and run this inside the oversampled region.

        // ── PUNCH (CLIP P7) — transient-tracking DRIVE, bipolar. A drive modulator, not a compressor.
        // Negative: transients escape clean and only the sustain is destroyed. Positive: the attack
        // is vaporised. 3 ms attack / 80 ms release follower on the input.
        float dEff = drv;
        if (fam == FAM_CLIP)
        {
            const float pAmt = pC_[6] * 2.0f - 1.0f;
            if (std::fabs (pAmt) > 1.0e-3f)
            {
                const float mag = std::fabs (x);
                punchZ_[c] += (mag - punchZ_[c]) * (mag > punchZ_[c] ? kPunchAtk : kPunchRel);
                dEff *= 1.0f + pAmt * juce::jlimit (0.0f, 1.0f, punchZ_[c] * 8.0f) * 3.0f;   // ~∓24 dB swing
            }
        }

        // ── GAP (CLIP P6) — class-B crossover dead zone BEFORE the shaper, input-referred: distorts
        // the QUIET parts; at max anything below ~−6 dBFS is silence and notes tear into fragments.
        // fb325 — power-1.6 taper (no-plateau law): linear reached total gating by ~60 % and the top
        // of the knob measured frozen. The curve now keeps eating deeper across the whole travel.
        const float gap = (fam == FAM_CLIP) ? std::pow (pC_[5], 1.6f) * 0.45f : 0.0f;
        auto deadzone = [gap] (float v) noexcept -> float
        {
            if (gap <= 1.0e-5f) return v;
            const float a = std::fabs (v);
            return (a <= gap) ? 0.0f : ((v < 0.0f) ? (v + gap) : (v - gap));
        };

        // ── FEEDBACK (CLIP P8) / SNARL (DIODE P8) — one-sample NONLINEAR recursion, the same
        // physical mechanism in both families (Fuzz Face / minor loop). Exactly off at 0; above
        // ~0.75 it self-oscillates and screams. DIODE reaches 0.98 — the BIBO ceiling, deliberately.
        const float fbA = fbGate * (
                          (fam == FAM_CLIP)  ? pC_[7] * 0.95f
                        : (fam == FAM_DIODE) ? pC_[7] * 0.98f
                        // fb324 — ANALOG Snarl (P8): the real feedback path on the three AMP circuits.
                        // Stomp Box rides the 0.98 loop-gain BIBO edge and SCREAMS in the top 15 %
                        // (real TS parasitic-oscillation behaviour); Overdrive clamps at 0.85/N (the
                        // fs/2 limit-cycle guard, N up to 3). Tape and Transformer consume Snarl
                        // IN-MODE instead (J-A loop WIDTH / the minor loop) — bible §5.5.
                        // fb325 — Stomp's Snarl moved IN-MODE (the flip-flop sub); its old 0.98 loop
                        // sustained an LF oscillation through the CLEAN bass path = the free-running
                        // 808 Max reported. Overdrive raised 0.28 → 0.45 (measured inaudible before).
                        : (mode_ == Tube)      ? pC_[7] * 0.90f
                        : (mode_ == Overdrive) ? pC_[7] * 0.45f : 0.0f);

        // ── DIODE DEAD ZONE (P6) — POST-drive (absolute threshold on the driven signal, so Drive
        // and Dead Zone genuinely FIGHT — that fight is the instrument). Diode 2's class-B gap is
        // its core identity, so it keeps a floor even at P6 = 0.
        float dzU = (fam == FAM_DIODE) ? pC_[5] * 3.0f : 0.0f;
        // fb323 — Diode 2 owns its gap INSIDE its case now (Characters reshape the gap TOPOLOGY:
        // Class B/AB, Push-Pull's double notch, Sputter's breathing gap, Half Gate, Ratchet), so the
        // family-wide dz must NOT also apply or the mode double-gaps.
        if (mode_ == Diode2) dzU = 0.0f;
        auto dzPost = [dzU] (float v) noexcept -> float
        {
            if (dzU <= 1.0e-5f) return v;
            return (v >= 0.0f) ? juce::jmax (0.0f, v - dzU) : juce::jmin (0.0f, v + dzU);
        };

        zsGate_ = 2.5e-4f * kPreGain * dEff;   // Zero-Square's −72 dBFS stability gate, input-referred

        // fb325 — the DC reference is STATELESS (c = −1): it used to run through channel 0's
        // stateful character paths (West Coast's output pole, Diode 2's Sputter walk), corrupting
        // MID state and mismatching the SIDE reference — measured as 'West Coast only plays on the
        // right side'. A reference must never advance anyone's state.
        const float dcOff = (fam == FAM_ANALOG) ? 0.0f : shapeM (dzPost (biasE_ * dEff), -1);
        const float fbIn  = (fbA > 1.0e-5f) ? fbA * fbZ_[c] : 0.0f;
        // A then B = chronological at the 2× rate, so stateful shapers (Slew Clip, DIODE slew) are exact.
        float yA, yB;
        if (fam == FAM_ANALOG)
        {
            // RAW into the circuit — no ×dEff, no shared bias/deadzone: each ANALOG mode applies
            // Drive and Bias in its OWN physical slot (grid volts, the drive→a pinning map, flux,
            // op-amp gain, cascade gain). The exact DC reference is impossible for a moving operating
            // point — the circuits self-centre (quiescent subtraction / differentiator / re-centre)
            // and the 10 Hz blocker takes the residue. Slam = +20 dB of pre-gain (Punish).
            yA = shapeS (pA * kPreGain * slamC_ + fbIn, c);
            yB = shapeS (pB * kPreGain * slamC_ + fbIn, c);
        }
        else
        {
            yA = shapeS (dzPost ((deadzone (pA * kPreGain) + biasE_ + fbIn) * dEff), c) - dcOff;
            yB = shapeS (dzPost ((deadzone (pB * kPreGain) + biasE_ + fbIn) * dEff), c) - dcOff;
        }

        // ── DIODE SLEW (P7) — junction-capacitance edge limiter, post-shaper, inside the OS region.
        // Only fast edges are touched — deliberately NOT a low-pass.
        if (fam == FAM_DIODE && pC_[6] > 1.0e-3f)
        {
            // fb325 — THE DIODE-FAMILY KILLER, probe-caught: the fb320 mapping put the WHOLE knob
            // in crush-land (A_max 0.36 at the shared 0.5 default — every diode mode clamped to a
            // quiet triangle: threshold law, knee, makeup all erased = "Diode 1 and 2 are just
            // fucked up"). Correct law: knob up ⇒ the slope ceiling comes DOWN, exponentially:
            // 0 = off · default grazes only the razor tips · top = total sludge (no-playing-safe).
            const float sU = 0.35f * std::pow (0.0028f, pC_[6]) * (48000.0f / (float) fs_);
            yA = slewZ_[c] + juce::jlimit (-sU, sU, yA - slewZ_[c]); slewZ_[c] = yA;
            yB = slewZ_[c] + juce::jlimit (-sU, sU, yB - slewZ_[c]); slewZ_[c] = yB;
        }
        if (fbA > 1.0e-5f)
        {
            // fb325 no-plateau: past self-osc onset "more feedback = the same scream" measured
            // frozen. The reinjection is filtered by a pole that OPENS with P8 (1.2 → 15 kHz), so
            // the top of the travel keeps changing the scream's COLOR and pitch-center.
            // the pole CLOSES with P8 (14 kHz → 400 Hz): the top half of the knob drops the scream's
            // pitch-centre toward a motorboating growl — full-travel evolution, hugely audible.
            if (std::fabs (pC_[7] - fbLpP_) > 1.0e-4f)
            { fbLpP_ = pC_[7]; const float o = 1.0f - pC_[7]; fbLpK_ = onePole2 (400.0f + 14000.0f * o * o); }
            fbLp_[c] += (0.5f * (yA + yB) - fbLp_[c]) * fbLpK_;
            fbZ_[c] = flushDenorm (fbLp_[c]);
        }

        float* se = se_[c]; float* so = so_[c]; int& si = si_[c];
        se[si & kRM] = yA; so[si & kRM] = yB;
        float y = hb_[kC] * se[(si - kC / 2) & kRM];
        // ⚠️ (si - j - 1), NOT (si - j): s[2n−2j−1] is at an ODD index ⇒ sOdd[n−j−1].
        for (int j = 0; j < kNH; ++j) y += hb_[2 * j + 1] * so[(si - j - 1) & kRM];
        ++si;
        // 🔑 fb321 UNITY HEADROOM LAW (Max, from A/B'ing Serum): powering the device on must NOT jump
        // the level — Serum reads slightly QUIETER at low drive, and Drive buys the loudness INTO the
        // curve. Post-trim = 1/kPreGain ⇒ net UNITY in the linear region at Drive 0; the shaper's ±1
        // ceiling then lands ~+14 dB over the -26 dBFS bus at full drive. Loudness is EARNED by the
        // knob, not handed out at power-on (the old ×0.5 trim was a flat +12 dB the moment it engaged).
        y *= (1.0f / kPreGain);

        // DC blocker (10 Hz, sample-rate aware). Mandatory on every asymmetric setting.
        const float d = y - dcX_[c] + dcR_ * dcY_[c];
        dcX_[c] = y; dcY_[c] = d; y = d;

        // DE-EMPHASIS (post) — the matching inverse (divide by the SAME never-zero gain).
        if (std::fabs (emphC_) > 1.0e-4f)
        {
            deEmphZ_[c] += (y - deEmphZ_[c]) * kEmphCoef;
            y = deEmphZ_[c] + (y - deEmphZ_[c]) / emphG_;
        }

        // HI CUT is POST — it tames fizz WITHOUT changing what distorted.
        // 🔑 TRUE BYPASS AT MAX. A "fully open" 22 kHz one-pole still measured −1.10 dB at 20 kHz
        // (hf_probe.cpp). fb325 — now a 3-POLE cascade (−18 dB/oct): Max: "give the Hi Cut the
        // power of Sag" — one pole read as polite next to the family's own darkening.
        if (hiBypass_) { hiCutZ_[c] = hiCut2_[c] = hiCut3_[c] = y; }
        else
        {
            hiCutZ_[c] += (y          - hiCutZ_[c]) * hiC_;
            hiCut2_[c] += (hiCutZ_[c] - hiCut2_[c]) * hiC_;
            hiCut3_[c] += (hiCut2_[c] - hiCut3_[c]) * hiC_;
            y = hiCut3_[c];
        }

        // TONE — post tilt around ~700 Hz. Its own state: the old version referenced hiCutZ_ AFTER
        // y had already been assigned from it, so the correction term was identically zero.
        if (std::fabs (toneC_) > 1.0e-4f)
        {
            toneZ_[c] += (y - toneZ_[c]) * kToneCoef;                 // lows
            const float hi = y - toneZ_[c];                            // highs
            y = toneZ_[c] * (1.0f - toneC_ * 0.95f) + hi * (1.0f + toneC_ * 0.95f);   // fb325: full-kill reach
        }

        // ── MIX, with the dry aligned to the resampler's latency ─────────────
        // The wet is kOsLatency samples late. Delaying the dry by the SAME amount is what stops
        // Mix < 100% from combing; it is also why the engine owns Mix at all (see setMix).
        float* dr = dry_[c]; int& di = di_[c];
        dr[di & kRM] = in0;
        const float dryAligned = dr[(di - kOsLatency) & kRM];
        ++di;
        const float mw = mixC_ * dip_;         // fb320 — mode-switch dips the wet through 0 (no click)
        return flushDenorm (dryAligned * (1.0f - mw) + y * mw);
    }

    // ── ADAA-1, with BOTH documented escapes ─────────────────────────────────
    float adaa (float u, int c) noexcept
    {
        AdaaState& s = st_[c];
        const float dx = u - s.x1;
        float y;

        if (std::fabs (dx) < 1.0e-5f)          // ill-conditioned divide -> midpoint
        {
            y = shapeM (0.5f * (u + s.x1));
            s.have = false;
        }
        else if (std::fabs (dx) > 0.9f)        // big jump -> direct, so it cannot drop out
        {
            y = shapeM (u);
            s.have = false;
        }
        else
        {
            const float F = anti (u);
            const float Fp = s.have ? s.F1 : anti (s.x1);
            y = (F - Fp) / dx;
            s.F1 = F; s.have = true;
        }
        s.x1 = u;
        return y;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  THE CURVES (fb320 — Phase B: 12 memoryless/sample-domain modes live).
    //  Add a mode = one case in shapeM() (+ shapeS() only if it needs state).
    //  anti() stays dormant until ADAA returns inside 4× oversampling.
    // ════════════════════════════════════════════════════════════════════════

    /** The transfer curve — also the DC reference (dcOff = shapeM(f(bias), 0)). fb323: takes the
        channel so stateful CHARACTER voicings (Sputter, Ratchet, Timbre Sweep, the Buchla pole…)
        keep per-channel state; the DC reference always evaluates on channel 0's math. */
    float shapeM (float u, int c = 0) noexcept
    {
        switch (mode_)
        {
            default:
            case SoftClip:  return softClipF (u, kneeE_);
            case HardClip:  return hardClipF (kneePre (u), kneeWidth());

            case ZeroSquare:
            {
                // Comparator + scaled clean path. The ONLY true jump discontinuity in the device:
                // pedestal p from Knee (0.92 at razor). ⚠️ THE −72 dBFS GATE IS A STABILITY
                // REQUIREMENT, not taste — with p→0.92 and no gate, ANY nonzero input (denormals, a
                // released voice's noise floor) becomes a full-scale square.
                const float p    = 0.92f * (1.0f - kneeE_);
                const float core = juce::jlimit (-1.0f, 1.0f, u);
                const float ped  = (std::fabs (u) > zsGate_) ? (u > 0.0f ? p : -p) : 0.0f;
                return ped + (1.0f - p) * core;
            }

            case SlewClip:  return juce::jlimit (-1.35f, 1.35f, u);   // DC proxy; real path in shapeS

            case Diode1:
            {
                // Shunt pair. Falling threshold (§2.5) + fb322 rail makeup. fb323 CHARACTER = WHICH
                // DIODE (bible §9.3 — real Vf/ideality values): Si 1N4148 · Ge 1N34A · LED · Schottky
                // · 3-stacked · MOSFET square-law · asinh Log (never plateaus) · Leaky mismatched Ge.
                u = kneePre (u);                              // fb325 — Knee audible at ANY drive
                static constexpr float dVf[8] = { 0.70f, 0.30f, 1.80f, 0.25f, 2.10f, 0.70f, 0.70f, 0.55f };
                static constexpr float dKn[8] = { -1.f,  0.85f, 0.20f, 0.05f, 0.15f, -1.f,  -1.f,  0.90f };
                if (chr_ == 5)                                                // MOSFET — f = x − x³/3: H2+H3, nothing higher
                {
                    const float v = juce::jlimit (-1.0f, 1.0f, u);
                    return 1.5f * (v - v * v * v * (1.0f / 3.0f));
                }
                if (chr_ == 6)                                                // Log — asinh: compresses 60 dB into ~12, ever-growing
                    return std::asinh (1.6f * u) * (1.0f / 1.6f);
                const float w  = 2.0f * (dKn[chr_] >= 0.0f ? dKn[chr_] : kneeE_);
                // fb325 — Knee's MAIN life: the falling-threshold slope itself (0.85 → 0.40).
                // Corner width alone measured bit-identical at working drive (the rail erases it);
                // how hard the threshold COLLAPSES under drive is audible on every note.
                const float T  = juce::jmax (0.06f, dVf[chr_] * (1.0f - (0.85f - 0.45f * kneeE_) * drive01_));
                const float mm = (chr_ == 7) ? 0.15f : 0.0f;                  // Leaky: ±15% Vf mismatch
                const float Tp = juce::jmax (0.03f, T * (1.0f + 0.6f * asymE_ + mm));
                const float Tn = juce::jmax (0.03f, T * (1.0f - 0.6f * asymE_ - mm));
                const float kMk = 0.70f + 0.60f * kneeE_;     // fb325 — conduction efficiency: the
                return kMk * ((u >= 0.0f) ? (Tp / T) * hardClipF (u / Tp, w)      // knee is audible at ANY drive
                                          : (Tn / T) * hardClipF (u / Tn, w));
            }

            case Diode2:
            {
                // Series / class-B — the device's only EXPANDER. fb323 CHARACTER = the GAP TOPOLOGY
                // (bible §9.3): Class B razor · Class AB (survives a bus) · Gate · Ge Bridge ·
                // Push-Pull double notch · Sputter (breathing gap — silent at DZ 0, no-noise law) ·
                // Half Gate · Ratchet (gap re-closes during a decay — the flagged stateful exception).
                u = kneePre (u);                              // fb325 — Knee audible at ANY drive
                float g = juce::jmax (0.15f, pC_[5] * 3.0f);
                float w = 2.0f * kneeE_;
                switch (chr_)
                {
                    default: w = juce::jmin (w, 0.04f);            break;   // 0 Class B — razor corner, textbook buzz
                    case 1:  g *= 0.5f;  w = juce::jmax (w, 1.0f); break;   // Class AB — only the tail fizzes
                    case 2:  g *= 2.2f;  w = juce::jmin (w, 0.04f);break;   // Gate — only transients speak
                    case 3:  g *= 0.6f;  w = juce::jmax (w, 0.8f); break;   // Ge Bridge — buzzy but musical
                    case 4:                                                  // Push-Pull — TWO gaps offset around zero:
                    {                                                        //   hollow phasey rasp, different comb
                        const float d5 = g * 0.6f;
                        auto dz1 = [] (float v, float gg) noexcept { return (v >= 0.0f) ? juce::jmax (0.0f, v - gg) : juce::jmin (0.0f, v + gg); };
                        const float mk4 = juce::jmin (4.0f, 1.0f + g);
                        return 0.5f * (hardClipF (dz1 (u - d5, g * 0.5f), w) + hardClipF (dz1 (u + d5, g * 0.5f), w)) * mk4;
                    }
                    case 5:                                                  // Sputter — the gap BREATHES on slow noise;
                        if (c >= 0 && pC_[5] > 1.0e-4f)                      //   modulates the GAP, never the level
                        {                                                    //   (c<0 = the stateless DC reference)
                            jWalk_[c] += (rnd11 (c) - jWalk_[c]) * 0.002f;
                            g *= 1.0f + 0.35f * jWalk_[c];
                        }
                        break;
                    case 6:                                                  // Half Gate — one polarity clean, one gated
                    {
                        const float mk6 = juce::jmin (4.0f, 1.0f + g);
                        float v6 = (u < 0.0f) ? juce::jmin (0.0f, u + g) : u;
                        return hardClipF (v6, w) * ((u < 0.0f) ? mk6 : 1.0f);
                    }
                    case 7:                                                  // Ratchet — gap rides a FAST envelope, so a
                    {                                                        //   held note machine-guns as it decays
                        if (c >= 0)                                          //   (stateless when c<0 — the DC reference)
                        {
                            env40_[c] += (juce::jmin (1.0f, std::fabs (u)) - env40_[c]) * 0.003f;
                            g *= 0.4f + 1.8f * (1.0f - env40_[c]);
                        }
                        break;
                    }
                }
                // fb325 — Knee = the SHARD ATTACK: the hard max(0,·) hinge is blended with a
                // hyperbolic-smoothed hinge (s = knee·1.2), so the gap's re-entry softens audibly
                // at ANY drive (corner width alone was erased by the rail — measured).
                const float sK = kneeE_ * 1.2f;
                const float av = std::fabs (u) - g;
                const float hv = (sK > 1.0e-3f) ? 0.5f * (av + std::sqrt (av * av + sK * sK)) - sK * 0.5f
                                                : juce::jmax (0.0f, av);
                float v = (u >= 0.0f ? 1.0f : -1.0f) * juce::jmax (0.0f, hv);
                const float mk = juce::jmin (4.0f, 1.0f + g) * (0.75f + 0.50f * kneeE_);   // fb322 makeup + fb325 knee tilt
                return hardClipF (v, w) * mk;
            }

            case Asym:
                // Bias injection into a pair (pre-drive via biasE_ — fb322). fb323 CHARACTER = WHICH
                // PAIR (bible §9.3): Ge/Si · Si/LED · the TS-808 2:1 mod · pure Grid Bias · the
                // published Doidic Line 6 curve · asinh-vs-hard Half Hard · Screamer loop · Collapse.
                // fb325 (Max: "params not doing the params, not loud enough, Screamer wasn't
                // screaming"): every char's knees now SCALE with the Knee knob (kw — it was fixed on
                // 5 of 8, so P5 measured dead), output makeup ×1.35, and Screamer actually screams.
                {
                u = kneePre (u);                                        // fb325 — Knee at ANY drive
                const float kw = 0.30f + 1.70f * kneeE_;
                switch (chr_)
                {
                    default: return 1.35f * ((u >= 0.0f) ? hardClipF (u, 1.7f * kw)     // 0 Ge/Si — different knee AND threshold
                                                         : hardClipF (u, 0.9f * kw));
                    case 1:  return 1.35f * ((u >= 0.0f) ? hardClipF (u, 0.9f * kw)     // Si/LED — 2.6:1, the single-ended stage
                                                         : 1.35f * hardClipF (u / 1.35f, 0.4f * kw));
                    case 2:  return 1.35f * ((u >= 0.0f) ? hardClipF (u, 1.2f * kw)     // 2-Up-1-Down — the TS-808 asymmetric mod
                                                         : 1.20f * hardClipF (u / 1.20f, 1.2f * kw));
                    case 3:  return 1.35f * hardClipF (u, 1.2f * kw);                   // Grid Bias — MATCHED pair, pure bias shift
                    case 4:  { if (u >= 0.0f) return 1.35f * juce::jmin (u, 0.630035f); // Line 6 — Doidic et al., verbatim limits
                               const float x2 = u*u, x4 = x2*x2, x12 = x4*x4*x4;        //   hard cap one side, power-12 rolloff other
                               return 1.35f * u / std::pow (1.0f + x12, 1.0f/12.0f); }
                    case 5:  return 1.35f * ((u >= 0.0f) ? std::asinh ((0.8f + 2.0f * kneeE_) * u) / (0.8f + 2.0f * kneeE_)
                                                         : hardClipF (u, 0.2f * kw));   // Half Hard — two KINDS of nonlinearity
                    case 6:  { const float s = (u < 0.0f) ? -1.0f : 1.0f;               // Screamer — now it SCREAMS: the log
                               const float a2 = std::fabs (u);                          //   compression carries a howling
                               return 1.35f * s * (std::log1p (3.5f * a2) * 0.42f       //   overtone that grows with drive
                                                   + 0.28f * std::sin (5.5f * juce::jmin (2.0f, a2))); }
                    case 7:  return 1.35f * ((u >= 0.0f) ? hardClipF (u, 1.2f * kw)     // Collapse — negative half ≈ gone:
                                                         : 0.01f * hardClipF (u / 0.01f, 0.0f));
                }
                }

            case Rectify:
            {
                // SIG = rect amount a ∈ 0..2 (centre full-wave, 2 = over-rect: half the drama lives
                // past "correct"). fb323 CHARACTER = the TOPOLOGY (bible §9.3 — the real octave
                // pedals): Bridge · Half Wave · true Squarer · Octavia (Tycobrahe) · Green Ringer ·
                // Tone Machine (Foxx) · real mismatched Transformer · Over-Rect.
                const float a = asymE_ + 1.0f;
                float R;
                switch (chr_)
                {
                    default: R = std::fabs (u);                                          break; // 0 Bridge
                    case 1:  R = 2.0f * juce::jmax (0.0f, u);                            break; // Half Wave — fundamental survives −6 dB
                    case 2:  R = u * u;                                                  break; // Doubler — ONE harmonic, a pure octave
                    case 3:  R = hardClipF (std::fabs (u) * 2.5f, 0.40f);                break; // Octavia — rect into hot germanium
                    case 4:  R = 0.7f * (u + std::fabs (u));                             break; // Green Ringer — rect + original ⇒ ring
                    case 5:  R = std::fabs (hardClipF (std::fabs (u) * 2.0f, 0.20f));    break; // Tone Machine — rect→fuzz→rect
                    case 6:  R = (u >= 0.0f) ? std::fabs (u) * 1.12f                     // Transformer — ±12% mismatch: the octave
                                             : std::fabs (u) * 0.88f;                    break; //   is IMPERFECT, which is why it's alive
                    case 7:  R = 2.0f * std::fabs (u) - u;                               break; // Over-Rect — a locked at 2
                }
                float v = (1.0f - a) * u + a * R;
                const float dz = std::pow (pC_[5], 1.8f) * 2.6f;   // fb325 — full-travel (was frozen past 60%)
                if (dz > 1.0e-4f) v = (v >= 0.0f) ? juce::jmax (0.0f, v - dz)
                                                  : juce::jmin (0.0f, v + dz);
                return 1.4f * hardClipF (v, 2.0f * kneeE_);   // fb325 — makeup: the octave must SPEAK
            }

            case LinearFold: return foldIter (u);   // Characters bias the ladder itself (fStB_/fSpO_/fRbM_/fCnO_, resolved per block)

            case WestCoast:
            {
                // fb323 CHARACTER = WHICH FOLDER (bible §9.4, the DAFx-17 circuits). fb325 — this
                // case is now the STATELESS core only (the DC reference must never touch channel
                // state): the Timbre Sweep envelope and the output pole live in shapeS.
                float v = u;
                if (chr_ == 7) v += 0.13f;                               // Cold Solder — static offset
                if      (chr_ == 2) return foldIter (foldIter (v * 0.55f) * 1.4f) * 0.9f;
                else if (chr_ == 3) return 0.40f * foldIter (v) + 0.30f * foldIter (v * 1.3f) + 0.20f * foldIter (v * 1.8f);
                else if (chr_ == 4) return (0.50f * foldIter (v) + 0.35f * foldIter (v * 1.41421356f) + 0.20f * foldIter (v * 2.0f)) * 1.25f;
                return 0.50f * foldIter (v) + 0.35f * foldIter (v * 1.41421356f) + 0.15f * foldIter (v * 2.0f);
            }

            case SineFold:
            {
                // Bessel comb (C∞ — gentlest aliaser in the family). fb323 CHARACTER (bible §9.4):
                // Shaper (the cut Sine-Shaper mode lives HERE, a≤π) · Bell · Brass (Chowning's DC
                // offset) · Formant (spacing warp ⇒ a level-tracking resonance) · Cluster (the
                // 111-harmonic wall) · Hollow · Ripple · Chime (triangle cusp blended in).
                float v = u;
                switch (chr_)
                {
                    case 0: v = juce::jlimit (-2.0f, 2.0f, v); break;
                    default: break;
                    case 2: v += 0.35f * juce::jmin (4.0f, driveC_) * 0.25f; break;
                    case 3: { const float a2 = std::fabs (v); v = (v < 0.0f ? -1.0f : 1.0f) * std::pow (a2, 1.7f); } break;
                    case 4: v *= 3.0f; break;
                    case 6: { const float a2 = std::fabs (v); v = (v < 0.0f ? -1.0f : 1.0f) * std::pow (a2, 0.6f); } break;
                }
                const int st = 1 + (int) (pC_[3] * 31.0f + 0.5f);
                if (std::fabs (pC_[4] - 0.29f) > 0.02f)        // fb325 — Spacing warps the sine's
                {                                              //   argument too (was corner-only = dead)
                    const float sp2 = 0.5f + pC_[4] * 1.7f;
                    const float av = std::fabs (v);
                    if (av > 1.0e-9f) v = (v < 0.0f ? -1.0f : 1.0f) * std::pow (av, sp2);
                }
                v *= 0.30f + pC_[3] * 2.2f;    // fb325 — Stages drives the fold density too (its old
                                               // clamp-only role went inert above ~10% — matrix-caught)
                v = juce::jlimit (-(float) (2 * st), (float) (2 * st), v);
                float yv = std::sin (1.5707963f * v);
                if (chr_ == 5) yv *= 1.0f / (1.0f + 0.4f * std::fabs (v));
                float crn = pC_[6];
                if (chr_ == 7) crn = juce::jmax (crn, 0.45f);
                if (crn > 1.0e-3f) yv = (1.0f - crn) * yv + crn * foldIter (v);
                return yv;
            }
        }
    }

    /** fb323 — THE REAL SHAPING PATH, now carrying the CHARACTER VOICINGS (bible §9, each cited
        from the research: real circuits, real converters, real pedals — the Serum/Phase Plant bar).
        Character 0 on Soft/Hard Clip = the fb320 shipped sound (knobs stay fully live); the other
        seven are physics variants, not EQ. shapeM stays chr-agnostic for the DC reference — exotic
        voicings' residual offset is the 10 Hz blocker's job. */
    float shapeS (float u, int c) noexcept
    {
        switch (mode_)
        {
        // ── fb324 PHASE C: THE ANALOG FAMILY (bible §9.1) — dynamic, WITH MEMORY. u arrives RAW
        // (×kPreGain·Slam only): each circuit applies Drive in its own physical slot. The state
        // ODEs integrate at base rate in analogTick(); the family gate is that H3/H2 must MOVE
        // > 2× across a −30…0 dBFS input sweep — the measurement that proves the memory is real.
        case Tube:        return tubeF  (u, c);
        case Tape:        return tapeF  (u, c);
        case Transformer: return xfmrF  (u, c);
        case StompBox:    return stompF (u, c);
        case Overdrive:   return odF    (u, c);

        case WestCoast:   // fb325 — the STATEFUL wrapper (Timbre Sweep env + the per-channel output
        {                 //   pole) around the stateless core in shapeM (see the dcOff law).
            float v = u;
            if (chr_ == 6) { env40_[c] += (juce::jmin (1.0f, std::fabs (u)) - env40_[c]) * 0.0008f;
                             v *= 0.3f + 1.4f * env40_[c]; }
            float y = shapeM (v, -1);
            if      (chr_ == 0) { wcLp_[c] += (y - wcLp_[c]) * 0.083f; y = wcLp_[c]; }   // the real 1.33 kHz pole
            else if (chr_ != 4) { wcLp_[c] += (y - wcLp_[c]) * 0.408f; y = wcLp_[c]; }   // ~8 kHz elsewhere
            return y;
        }

        // ── fb325 CHARACTER ROSTER CULL (Max: "keep the ones that do the most, more of the creative
        // ones — Wrap Tip / Octave energy"). Dead-measuring voicings deleted; replacements are
        // event-machines, not curves (the research law: regime traversal beats gain).
        case SoftClip:
            switch (chr_)
            {
                default: return softClipF (u, kneeE_);                       // 0 Diff Pair — the anchor blend
                case 1:  return softClipF (u * 1.5f, 1.0f) * 0.85f;          // Glue — driven, dense, railless
                case 2:  return softClipF (u, 0.0f);                         // Dense — the tight quartic sigmoid
                case 3:                                                       // Growl — the PUBLISHED Serum-2 Diode-2
                {                                                             //   behavior: a sine transfer whose plateau
                    const float m = juce::jmin (1.0f, drive01_ * 1.4f);       //   HARD-CLIPS as drive rises (the dubstep
                    const float sv = std::sin (1.5707963f * juce::jlimit (-1.0f, 1.0f, u));   // growl morph — the curve
                    return (1.0f - m) * sv                                    //   ITSELF evolves with the knob)
                         + m * hardClipF (sv * (1.0f + drive01_ * 2.5f), 0.1f);
                }
            }

        case Diode1: case Diode2:
            return kneePost (shapeM (u, c), c);            // fb325 — Knee's second life at ANY drive
        case Asym:
        {
            float y = shapeM (u, c);
            // fb325 — every saturator CONVERGES at depth (measured bit-identical), so the knee's
            // guaranteed-audible half is a conduction-efficiency LEVEL tilt + the edge slew below.
            y *= 0.70f + 0.60f * kneeE_;
            return kneePost (y, c);
        }

        case HardClip:
        {
            u = kneePre (u);                                                 // fb325 — Knee's top half lives at ANY drive
            float hcY;
            switch (chr_)
            {
                default: hcY = hardClipF (u, kneeWidth());                 break;   // 0 Razor
                case 1:  hcY = (u >= 0.0f) ? hardClipF (u, kneeWidth())              // Rails — +1.00/−0.62
                                           : 0.62f * hardClipF (u / 0.62f, kneeWidth());   break;
                case 2:                                                       // Wrap Tip — beyond |u|=1.35 the output wraps
                {                                                             //   to the opposite rail (Max's favourite)
                    const float v = std::fabs (u);
                    if (v <= 1.35f) { hcY = hardClipF (u, kneeWidth()); break; }
                    const float ex = std::fmod (v - 1.35f, 2.0f);
                    hcY = (u > 0.0f ? 1.0f : -1.0f) * (1.0f - ex);            break;
                }
                case 3:                                                       // Modulo — Wrap Tip's manic sibling: the
                {                                                             //   signal wraps THROUGH the rail repeatedly
                    float w2 = u * 0.5f + 0.5f;
                    w2 -= std::floor (w2);
                    hcY = w2 * 2.0f - 1.0f;                                   break;
                }
            }
            return kneePost (hcY, c);
        }

        case ZeroSquare:
        {
            // fb325 REDO (Max: "Zero-Square gotta be way better — keep Octave, more creative ones").
            // The roster is now all EVENT MACHINES: comparator, PWM, octave-up, a CMOS flip-flop
            // octave-DOWN, and an envelope-swept PWM — new pitches and motion, not knee flavours.
            const float p0   = 0.92f * (1.0f - kneeE_);
            const float core = juce::jlimit (-1.0f, 1.0f, u);
            switch (chr_)
            {
                default:                                                      // 0 Comparator — the instantaneous edge
                {
                    const float ped = (std::fabs (u) > zsGate_) ? (u > 0.0f ? p0 : -p0) : 0.0f;
                    return ped + (1.0f - p0) * core;
                }
                case 1:                                                       // Duty — the flip point rides Bias: free PWM
                {                                                             //   + an enormous even-harmonic swing
                    const float th = -biasE_ * driveC_ * 2.0f;
                    const float ped = (std::fabs (u - th) > zsGate_) ? (u > th ? p0 : -p0) : 0.0f;
                    return ped + (1.0f - p0) * core;
                }
                case 2:                                                       // Octave — fires on the RECTIFIED signal:
                {                                                             //   a square at 2·f0 (Max's keeper)
                    const float T = 0.35f * juce::jmax (1.0f, driveC_ * 0.25f);
                    const float ped = p0 * ((std::fabs (u) > T) ? 1.0f : -1.0f);
                    return ped + (1.0f - p0) * core;
                }
                case 3:                                                       // Sub Oct — CMOS flip-flop divider (OC-2):
                {                                                             //   the square lands ONE OCTAVE DOWN and
                    const float th = zsGate_ * 4.0f + 0.01f;                  //   wears the note's envelope — trap sub
                    if (u > th && zsSgn_[c] <= 0.0f) zsLp_[c] = (zsLp_[c] >= 0.0f) ? -1.0f : 1.0f;
                    zsSgn_[c] = (u > th) ? 1.0f : -1.0f;
                    zsEnv_[c] += (juce::jmin (1.0f, std::fabs (u)) - zsEnv_[c]) * 0.004f;
                    return p0 * zsLp_[c] * zsEnv_[c] + (1.0f - p0) * core;
                }
                case 4:                                                       // PWM Env — the duty SWEEPS with the note's
                {                                                             //   decay: every hit is a pulse-width sweep
                    zsEnv_[c] += (std::fabs (u) - zsEnv_[c]) * 0.0006f;       //   (slow amplitude tracker)
                    const float th = zsEnv_[c] * (1.55f - 1.35f * juce::jmin (1.0f, zsEnv_[c] * 2.0f));
                    const float ped = (u > th) ? p0 : -p0;
                    const float gate = juce::jmin (1.0f, zsEnv_[c] * 30.0f);  //   silent without input
                    return ped * gate + (1.0f - p0) * core;
                }
            }
        }

        case SlewClip:
        {
            // Slope-ceiling family (dx/dt distortion). Character = WHICH op-amp is dying (bible §9.2):
            // fb325 — 160^k topped out ABOVE the signal's own slew by ~80% travel (SIG froze);
            // 60^k keeps the whole knob inside the limiting region.
            float sU = 0.0005f * std::pow (60.0f, kneeE_) * kPreGain * 0.5f;
            switch (chr_)
            {
                default: sU *= 3.0f;  break;                                 // 0 Fast — 5534-class: only the top octave
                case 1:  sU *= 0.25f; break;                                 // Slow — 741-class: everything sounds like 1974
                case 2:  break;                                              // Asym — handled below (3:1 rise/fall)
                case 3:                                                       // Charge — tanh soft ceiling, NO corner at all
                {
                    const float d2 = u - slewZ_[c];
                    float y = slewZ_[c] + sU * std::tanh (d2 / juce::jmax (1.0e-6f, sU));
                    y = juce::jlimit (-1.35f, 1.35f, y); slewZ_[c] = y; return y;
                }
                case 4:                                                       // Stick — a dead-band before slewing begins:
                {                                                             //   tiny signals FREEZE (granular decays)
                    float d2 = u - slewZ_[c];
                    const float db = 0.004f * kPreGain;
                    if (std::fabs (d2) < db) return slewZ_[c];
                    d2 -= (d2 > 0.0f ? db : -db);
                    float y = slewZ_[c] + juce::jlimit (-sU, sU, d2);
                    y = juce::jlimit (-1.35f, 1.35f, y); slewZ_[c] = y; return y;
                }
                case 5: sU *= 1.0f; break;                                   // Ring — overshoot on EXIT, added below
                case 6:                                                       // Bounce — the ceiling rides a 40 ms envelope:
                {                                                             //   loud passages slew FASTER (alive on hard playing)
                    env40_[c] += (juce::jmin (1.0f, std::fabs (u) * 0.5f) - env40_[c]) * 0.0005f;
                    sU *= 1.0f + 2.5f * env40_[c]; break;
                }
                case 7: if (c == 1) sU *= 1.15f; break;                      // Stereo — R ceiling +15%: opens on transients only
            }
            float d2 = u - slewZ_[c];
            if (chr_ == 2 && d2 < 0.0f) d2 = juce::jmax (d2 * 3.0f, d2);     // Asym: fall ceiling 3× (evens, level-dependent)
            const bool wasLim = std::fabs (d2) > sU;
            float y = slewZ_[c] + juce::jlimit (-sU * (chr_ == 2 && d2 < 0.0f ? 3.0f : 1.0f), sU * (chr_ == 2 && d2 < 0.0f ? 3.0f : 1.0f), d2);
            if (chr_ == 5 && slewLim_[c] > 0.5f && ! wasLim) y += juce::jlimit (-0.2f, 0.2f, d2 * 0.6f);   // Ring: exit zing
            slewLim_[c] = wasLim ? 1.0f : 0.0f;
            y = juce::jlimit (-1.35f, 1.35f, y);
            slewZ_[c] = y;
            // fb325 no-plateau law: at deep drive the triangle's amplitude is set by the slew rate
            // alone and Drive measured FROZEN (0.0 movement over the top half). The top now at least
            // earns level (+~4.5 dB), so the knob never dies.
            return y * (0.75f + 0.50f * drive01_);
        }

        default: return shapeM (u, c);
        }
    }

    /** Iterative wavefolder — the FOLD family's ladder, all four back knobs live:
        Stages (P4) = ladder length 1..32 · Spacing (P5) = argument power-warp 0.5..2.2 ·
        Rebound (P6) = reflection gain per fold 0.4..1.6 (>1 = no hardware does this) ·
        Corner (P7) = cusp rounding (sin-blend ≈ half the alias energy at the same fold count). */
    float foldIter (float u) noexcept
    {
        // fb323 — Character biases the LADDER (multipliers/offsets on the user's knobs, so the four
        // back knobs stay fully live on every voicing — dramaticism law).
        const int   stages = juce::jlimit (1, 32, (int) ((1.0f + pC_[3] * 31.0f) * fStB_ + 0.5f));
        const float spacP  = juce::jlimit (0.30f, 2.50f, 0.5f + pC_[4] * 1.7f + fSpO_);
        const float reb    = juce::jlimit (0.30f, 1.70f, (0.4f + pC_[5] * 1.2f) * fRbM_);
        float v = u;
        const float a0 = std::fabs (v);
        if (a0 > 1.0e-9f && std::fabs (spacP - 1.0f) > 1.0e-3f)
            v = (v < 0.0f ? -1.0f : 1.0f) * std::pow (a0, spacP);
        // fb325 — STAGES REACHES (no-plateau law): ladder length beyond the drive's reach was inert
        // (Max: "stages stops at 30% and stops evolving"). The knob now also scales the ladder
        // input, so more stages = audibly more folds across the WHOLE travel, on every fold mode.
        v *= 0.50f + (float) stages * 0.065f;
        float g = 1.0f; int k = 0;
        while (std::fabs (v) > 1.0f && k < stages)
        {
            v = (v > 0.0f) ? (2.0f - v) : (-2.0f - v);   // reflect at the rail
            g *= reb; ++k;
        }
        if (std::fabs (v) > 1.0f) v = (v > 0.0f ? 1.0f : -1.0f);   // ladder ended ⇒ rail
        v = juce::jlimit (-4.0f, 4.0f, v * g);                     // BIBO for Rebound > 1 runaway
        const float crn = (fCnO_ < -0.5f) ? 0.0f : juce::jlimit (0.0f, 1.0f, pC_[6] + fCnO_);
        if (crn > 1.0e-3f)                                         // sin of a triangle = the rounded triangle
            v = (1.0f - crn) * v + crn * std::sin (1.5707963f * juce::jlimit (-1.0f, 1.0f, v));
        return v;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  fb324 — PHASE C: THE ANALOG FAMILY (bible §9.1). Three real memory mechanisms, one per
    //  circuit group: MAGNETIC state (Tape's pinned domains, Transformer's core flux — the transfer
    //  relation is a LOOP, not a curve) · OPERATING-POINT reservoirs (Tube's cathode/grid-block
    //  caps, the pedals' supply rails — a loud stab changes how the NEXT note distorts) · RATE
    //  memory (the LM308's slew limiter — distortion as a function of dx/dt). Implementing any of
    //  these as its static curve is the documented failure (§2.6): the level-dependence metric
    //  reads flat and the mode collapses into the Soft Sat that was cut.
    // ════════════════════════════════════════════════════════════════════════

    /** Base-rate tick for the ANALOG family: the SAG/RECOVERY reservoir, Tube's grid-blocking cap,
        and the DRIFT pair (bias wander ±0.4 + a 0…±12 ms wow/flutter delay mod on the input).
        Returns the (possibly wow-delayed) input. Bit-identical bypass at Drift 0. */
    float analogTick (float x, int c) noexcept
    {
        // SAG (P4) / RECOVERY (P5) — one reservoir per channel; each mode FEEDS it its own rectified
        // quantity (cathode current, |M/Ms|, flux, rail peak) and READS it back in its own physical
        // slot. Recovery's bottom decade (0.05–0.5 ms) puts the sag corner INSIDE the audio band —
        // sputter, motorboat, ring-mod — deliberately (bible: that region is the point of the knob).
        if (std::fabs (pC_[4] - recovP_) > 1.0e-4f)
        {
            recovP_ = pC_[4];
            const float tauS = 0.05e-3f * std::pow (5000.0f, pC_[4]);       // 0.05 … 250 ms — fb325: the top
                                                                            // half was beyond any musical pump
            aRecC_ = 1.0f - (float) std::exp (-1.0 / (fs_ * (double) tauS));
        }
        // fb325 — fixed ~4 ms attack (release = Recovery): the reservoir now PUMPS audibly instead
        // of quasi-statically tracking the waveform (which read as "sag = a low-pass" — Max).
        sagV_[c] += (sagIn_[c] - sagV_[c]) * ((sagIn_[c] > sagV_[c]) ? aAtk4_ : aRecC_);
        // TUBE grid blocking — grid current charges the coupling cap (τ_c = 22 ms, the Fender
        // 0.1 µF/220 k): the cap pushes the grid negative and HOLDS it — the "farting out" artefact.
        if (mode_ == Tube)
            tubeBlk_[c] = juce::jlimit (0.0f, 8.0f, tubeBlk_[c] + tubeIgS_[c] * (40.0f / (float) fs_)
                                                    - tubeBlk_[c] * (45.5f / (float) fs_));
        // DRIFT (P6) / DRIFT RATE (P7) — the family's matched mod pair. Cassette floors it (its
        // triple-LFO wow IS the voicing — the fb323 Warble precedent).
        float drift = pC_[5];
        if (mode_ == Tape && chr_ == 3) drift = juce::jmax (drift, 0.20f);
        if (drift <= 1.0e-4f) { drW_[c] = 0.0f; return x; }
        if (std::fabs (pC_[6] - rateP_) > 1.0e-4f) { rateP_ = pC_[6]; rateC_ = 0.02f * std::pow (1500.0f, pC_[6]); }
        // fb325 — THE WANDER IS SHARED across channels (one motor, one supply). Per-channel wander
        // fed the M/S pair different operating points: the SIDE channel of a mono note became a
        // left-leaning wandering thump ("laser in the left ear" — Max). The walk is APERIODIC
        // (random interval + double smoothing — a fixed retarget period was the tempo'd zap), and
        // its excursion FALLS with rate: fast flutter is physically tiny.
        if (c == 0)
        {
            drPh_[0] += rateC_ / (float) fs_;
            if (drPh_[0] >= drNxt_) { drPh_[0] = 0.0f; drTgt_[0] = rnd11 (0); drNxt_ = 0.55f + rnd01 (0) * 0.9f; }
            drZ_[0]  += (drTgt_[0] - drZ_[0]) * juce::jmin (0.20f, rateC_ * 30.0f / (float) fs_);
            drS_     += (drZ_[0]  - drS_)     * juce::jmin (0.15f, rateC_ * 15.0f / (float) fs_);
            wp1_[0] += 0.6f / (float) fs_; if (wp1_[0] >= 1.0f) wp1_[0] -= 1.0f;
            wp2_[0] += 2.2f / (float) fs_; if (wp2_[0] >= 1.0f) wp2_[0] -= 1.0f;
            wp3_[0] += 7.0f / (float) fs_; if (wp3_[0] >= 1.0f) wp3_[0] -= 1.0f;
        }
        const float rTame = 1.0f / (1.0f + rateC_ * 0.10f);
        drW_[c] = drS_ * drift * 0.4f * rTame;                              // ±0.4 — modes scale into their units
        float wob = drS_;
        if (mode_ == Tape)
        {
            // wow/flutter — the CassetteMachine 0.6 / 2.2 / 7 Hz stack, blended with the random walk
            const float fl = 0.5f * std::sin (6.2831853f * wp1_[0]) + 0.3f * std::sin (6.2831853f * wp2_[0])
                           + 0.2f * std::sin (6.2831853f * wp3_[0]);
            wob = 0.5f * wob + 0.5f * fl * ((chr_ == 3) ? 2.0f : 1.0f);
        }
        drRing_[c][dwi_[c] & kDrM] = x;
        const float dS = juce::jlimit (0.0f, (float) kDrM - 4.0f,
                                       (0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, wob)) * 0.012f * (float) fs_ * drift * rTame);
        const int   i0 = dwi_[c] - (int) dS;
        const float fr = dS - (float) (int) dS;
        const float ym1 = drRing_[c][(i0 + 1) & kDrM], y0 = drRing_[c][i0 & kDrM],
                    y1  = drRing_[c][(i0 - 1) & kDrM], y2 = drRing_[c][(i0 - 2) & kDrM];
        ++dwi_[c];
        const float h1 = 0.5f * (y1 - ym1);
        const float h2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float h3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((h3 * fr + h2) * fr + h1) * fr + y0;                        // 4-pt Hermite (comb-click law)
    }

    /** TUBE — Dempwolf & Zölzer (DAFx-11 pp.257-262) 12AX7: Ik = softplus_C(V)^γ, continuously
        differentiable, fitted to REAL tubes (their Table 1 C/γ pairs are Characters 0–1 verbatim).
        Grid conduction charges the blocking cap; cathode current charges Rk‖Ck (sag): the ONLY mode
        whose operating point walks on two independent time constants at once. Even-dominant. */
    float tubeF (float u, int c) noexcept
    {
        //   {C knee, γ, headroom (grid-volt scale), sagMul, blockMul, sym (push-pull), biasOff}
        static constexpr struct { float C, g, hr, sg, bk, sym, bo; } TC[8] = {
            {3.40f, 1.26f, 1.0f,  1.0f,  1.0f, 0.0f,  0.0f },   // RSD 12AX7 — the DAFx-11 reference fit
            {4.56f, 1.35f, 1.0f,  1.0f,  1.0f, 0.0f,  0.0f },   // EHX 12AX7 — its real fit: audibly rounder
            {3.20f, 1.20f, 4.0f,  0.6f,  0.3f, 0.0f,  0.0f },   // 12AU7 Line — µ~20 headroom, the glue voice
            {3.40f, 1.26f, 1.3f,  1.7f,  0.6f, 0.85f, 0.0f },   // 6V6 Power — push-pull odd, hard cathode sag
            {8.50f, 1.10f, 0.45f, 0.15f, 0.5f, 0.0f,  0.0f },   // Starved Plate — no headroom, tail = transient
            {5.50f, 1.40f, 1.0f,  0.05f, 1.0f, 0.0f, -2.5f },   // Cold Clipper — −4 V bias, NO sag bloom
            {3.40f, 1.26f, 1.0f,  1.0f,  4.0f, 0.0f,  0.0f },   // Blocking Fender — max blocking: farts out
            {3.20f, 1.20f, 6.0f,  0.3f,  0.0f, 0.6f,  0.0f },   // Vari-Gain Hi-Fi — only peaks colour
        };
        const auto& t = TC[chr_];
        // Bias (SIG): + walks the grid INTO conduction (+0.5 V at max), − deep toward cutoff (−6 V).
        const float vb = ((biasE_ >= 0.0f) ? 2.0f * biasE_ : 4.5f * biasE_) + t.bo + drW_[c] * 2.0f;
        // Op-point cache: quiescent current, small-signal slope (⇒ unity at Drive 0 — the fb321 law),
        // rail makeup. Recomputed only when the Character or the glided bias actually moves.
        if (tubeCk_ != chr_ || std::fabs (tubeCb_ - vb) > 1.0e-3f)
        {
            tubeCk_ = chr_; tubeCb_ = vb;
            const float sp0 = spSoft (vb + 1.0f, t.C);
            const float i0  = std::pow (juce::jmax (1.0e-6f, sp0), t.g);
            const float sg0 = 1.0f / (1.0f + std::exp (-juce::jlimit (-30.0f, 30.0f, t.C * (vb + 1.0f))));
            const float df  = t.g * std::pow (juce::jmax (1.0e-4f, sp0), t.g - 1.0f) * sg0;
            const float ll  = kTubeIsat * kTubeIsat / ((kTubeIsat + i0) * (kTubeIsat + i0));
            tubeIL0_ = kTubeIsat * i0 / (kTubeIsat + i0);
            // fb324 harness: small-signal slope is EXACTLY unity (the fb321 law) — no rail-makeup
            // here (a rail normaliser measured −9.5 dB at Drive 0, the power-on cut Serum never
            // makes). The stage's output ceiling is the soft limit below instead.
            tubeG1_  = 1.0f / juce::jmax (0.03f, df * ll);
        }
        const float vg   = (u * driveC_) / t.hr + vb
                         - pC_[3] * 1.5f * 2.6f * sagV_[c]                  // cathode sag — the op point WALKS
                         - tubeBlk_[c];                                     // grid blocking — the next note ducks
        // Grid conduction (Dempwolf eq 11 shape) does TWO jobs, both real: (1) INSTANTANEOUS — grid
        // current through the source impedance clamps the positive half the moment Vg goes positive
        // (harness-caught: without this, load-line flattening vs cutoff nearly BALANCED and Bias
        // stopped making even harmonics); (2) SLOW — it charges the blocking cap (analogTick).
        const float igN = spSoft (vg - 0.2f, 9.9f);
        const float vgc = vg - 0.55f * igN;
        const float ik = std::pow (juce::jmax (1.0e-6f, spSoft (vgc + 1.0f, t.C)), t.g);
        const float il = kTubeIsat * ik / (kTubeIsat + ik);                 // the load line in one algebraic move
        tubeIgS_[c] = igN * t.bk;
        // fb325 matrix-caught TWICE: raw plate current averaged back to quiescent (sag dead), and
        // |il−IL0| read CUTOFF as loud AC — positive feedback that LATCHED the tube silent at
        // Sag 100. The cathode charges on CONDUCTION: only excursion ABOVE quiescent counts, so a
        // deep duck self-releases (negative feedback, like the real Rk‖Ck).
        sagIn_[c]   = juce::jmin (2.5f, juce::jmax (0.0f, il - tubeIL0_) * 3.0f * t.sg);
        float y = (il - tubeIL0_) * tubeG1_;
        if (t.sym > 1.0e-3f)      // push-pull voicings: mirror stage — odd-dominant, sag intact
        {
            const float ik2 = std::pow (juce::jmax (1.0e-6f, spSoft (2.0f * vb - vg + 1.0f, t.C)), t.g);
            const float il2 = kTubeIsat * ik2 / (kTubeIsat + ik2);
            y = (1.0f - t.sym) * y + t.sym * 0.5f * (il - il2) * tubeG1_;
        }
        // the following stage's supply rail — a soft C∞ ceiling, ASYMMETRIC like the real thing
        // (saturation side vs cutoff side differ): ~−0.5 dB on the small signal, +evens preserved.
        return (y >= 0.0f) ? y / (1.0f + 0.20f * y) : y / (1.0f - 0.34f * y);
    }

    /** TAPE — Jiles–Atherton hysteresis via Chowdhury's DAFx-19 audio-normalised cook: the transfer
        relation is a double-valued LOOP whose branch is selected by sign(dH/dt) — level-dependent
        lag that reads as SMEAR on transients, which no static curve can produce. The 55 kHz bias
        carrier is folded into the pinning k (the 4×-not-16× decision); the ODE runs at the 2× rate. */
    float tapeF (float u, int c) noexcept
    {
        //   {sat→M_s, kMul, bumpHz, bumpDb, gapLp, aMul (headroom — WHERE the machine breaks up)}
        static constexpr struct { float sat, km, bf, bg, gl, am; } TP[8] = {
            {0.35f, 1.00f, 45.0f, 3.0f, 18000.0f, 1.00f},   // Studer A80 15ips — wide loop, the reference
            {0.50f, 0.85f, 50.0f, 2.5f, 15000.0f, 0.75f},   // Ampex 350 — breaks up EARLIER, softer knee
            {0.30f, 1.00f, 90.0f, 1.5f, 20000.0f, 1.90f},   // Studer 30ips — saturates much LATER, clean
            {0.60f, 1.10f, 80.0f, 5.0f,  9000.0f, 0.70f},   // Cassette 1⅞ — band closes, triple-LFO wow
            {0.25f, 1.60f, 45.0f, 2.0f, 19000.0f, 1.35f},   // Chrome II — higher coercivity: LATER but HARDER
            {0.85f, 0.80f, 60.0f, 6.0f,  7000.0f, 0.45f},   // Dub Plate — over-biased AND driven, dark
            {0.55f, 1.00f, 45.0f, 3.0f, 14000.0f, 0.95f},   // Worn/Shedding — the loop width WANDERS (oxide)
            {0.55f, 1.00f, 45.0f, 3.0f, 13000.0f, 0.90f},   // Under-Biased — spitty at Bias 0 already
        };
        const auto& t = TP[chr_];
        const float Ms = 0.5f + 1.5f * (1.0f - t.sat);
        float b  = biasE_ + drW_[c];
        float kJ = 0.47875f * t.km;
        if (chr_ == 7) { kJ *= 1.8f; b -= 0.25f; }  // Under-Biased: the deadzone is OPEN at Bias 0
        if (b < 0.0f)  kJ *= 1.0f + 6.0f * (-b);    // UNDER-BIAS: crossover spit — QUIET material distorts hardest (fb325 reach ×2)
        float dropo = 0.0f;
        if (chr_ == 6)                              // Worn: oxide dropouts — the loop width AND the
        {                                           //   level flicker on a slow walk (~0.15 s)
            wornW_[c] += (rnd11 (c) - wornW_[c]) * 7.0e-5f;
            kJ *= 1.0f + 0.85f * wornW_[c];
            dropo = juce::jmax (0.0f, wornW_[c]) * 0.45f;
        }
        // Snarl = loop WIDTH, floored at 0.35 — harness-caught: mapping Snarl 0 straight to
        // c = 0.98 made the irreversible term ×0.02, i.e. THE HYSTERESIS LOOP WAS OFF at default
        // and tape degenerated into a static Langevin curve (the §2.6 failure, measured). Tape
        // always has a loop; Snarl widens it from real → maximally lossy and lagging.
        const float cJ = juce::jlimit (0.005f, 0.81f, std::sqrt (0.65f - 0.65f * pC_[7]) - 0.01f);
        // Sag on tape = program-coupled compression depth: sustained level collapses `a` (the loop
        // pins deeper), Recovery brings it back — tape's own slot on the family pair.
        const float a = (Ms * t.am / (0.01f + 6.0f * drive01_)) / (1.0f + pC_[3] * 1.5f * 1.5f * sagV_[c]);
        // fb325 — HARNESS/EAR-CAUGHT ("crazy tape noise at Drive 0"): the pinning k does NOT scale
        // with `a`, so at Drive 0 the loop was still fat relative to the signal and the ×(0.6a)
        // output normalisation amplified its residue ~60×. Below the working region the loop width
        // tracks the pinning scale ⇒ Drive 0 is genuinely CLEAN, the loop opens as drive rises.
        kJ *= juce::jmin (1.0f, 2.5f / a);
        // H grows with drive too (§2.4c: the pre-gain sits OUTSIDE the drive→a mapping, so the top
        // of Drive corners the loop instead of only re-pinning it — and Slam pushes far past it).
        float H = u * 5.0f * (1.0f + 2.0f * drive01_) * (1.0f - dropo);
        H = juce::jlimit (-8.0f * a, 8.0f * a, H);          // solver guard BEFORE the RK step (§2.3)
        const float T  = (float) (0.5 / fs_);
        const float Hd = (1.75f / T) * (H - tapeH1_[c]) - 0.75f * tapeHd_[c];   // α-transform, dα = 0.75
        const float k1 = T * jaDeriv (tapeM_[c], tapeH1_[c], tapeHd_[c], Ms, a, cJ, kJ);
        const float k2 = T * jaDeriv (tapeM_[c] + 0.5f * k1, 0.5f * (H + tapeH1_[c]),
                                      0.5f * (Hd + tapeHd_[c]), Ms, a, cJ, kJ);
        float M = tapeM_[c] + k2;
        if (! (M > -1.0e6f && M < 1.0e6f)) M = tapeM_[c];   // divergence: HOLD state, never zero (click)
        M = juce::jlimit (-1.2f * Ms, 1.2f * Ms, M);
        tapeM_[c] = M; tapeH1_[c] = H; tapeHd_[c] = Hd;
        sagIn_[c] = juce::jmin (2.5f, std::fabs (M / Ms) * 1.5f);
        // Parameter-only normalisation (NEVER program-dependent — that would flatten the exact
        // level-dependence being paid for): small-signal unity at every drive, then an EXPLICIT
        // dB-domain earned-loudness makeup (+13 dB at the top). Harness-caught: tape self-limits
        // (§2.4 says so outright), so a linear makeup measured FLAT from Drive 50 → 100.
        if (std::fabs (drive01_ - tpDrvP_) > 1.0e-4f) { tpDrvP_ = drive01_; tpMk_ = std::pow (10.0f, 0.90f * drive01_); }
        float y = M * (0.6f * a / Ms) * tpMk_;
        const float glHz = t.gl * ((b > 0.0f) ? (1.0f - 0.60f * b) : 1.0f);   // over-bias kills top end (fb325 reach)
        tapeGl_[c] += (y - tapeGl_[c]) * onePole2 (glHz);   // playhead gap loss
        y = tapeGl_[c];
        bqEnsure (t.bf, 1.2f, t.bg, true);                  // the LF head bump (45 Hz @15 ips / 90 @30)
        return bqRun (y, c);
    }

    static float jaDeriv (float M, float H, float Hd, float Ms, float a, float cc, float kk) noexcept
    {
        constexpr float al = 1.6e-3f;
        const float Q   = (H + al * M) / a;
        const float LQ  = langevin (Q), LD = langevinD (Q);
        const float Man = Ms * LQ;
        const float dM  = Man - M;
        const float dl  = (Hd >= 0.0f) ? 1.0f : -1.0f;                       // eq 7
        const float dlM = ((dl >= 0.0f) == (dM >= 0.0f)) ? 1.0f : 0.0f;      // eq 8
        float den1 = (1.0f - cc) * dl * kk - al * dM;
        if (std::fabs (den1) < 1.0e-4f) den1 = (den1 < 0.0f) ? -1.0e-4f : 1.0e-4f;
        const float MsaLD = (Ms / a) * LD;
        float den2 = 1.0f - cc * al * MsaLD;
        if (std::fabs (den2) < 1.0e-4f) den2 = (den2 < 0.0f) ? -1.0e-4f : 1.0e-4f;
        return ((1.0f - cc) * dlM * dM / den1 * Hd + cc * MsaLD * Hd) / den2;   // eq 18
    }

    /** TRANSFORMER — core flux is the TIME INTEGRAL of the applied voltage, so at fixed amplitude
        flux ∝ 1/f and LOW FREQUENCIES SATURATE FIRST. Integrate → saturate the FLUX (Tape's guarded
        Langevin, verbatim) → differentiate back: exactly unity, phase-flat, THD = 0 below the knee.
        The bible's trap — tanh-with-a-low-shelf — has no genuine frequency dependence; this does. */
    float xfmrF (float u, int c) noexcept
    {
        //   {τmMul, φkMul, hysMax, resHz, resQ, resDb, φDC bake, LangScale}
        static constexpr struct { float tm, fk, hy, rf, rq, rg, dc, ls; } XF[8] = {
            {1.00f, 1.0f, 0.15f, 42000.0f, 2.2f, 2.0f, 0.0f,  1.0f},   // Neve Marinair — round lower-mids
            {0.60f, 0.8f, 0.20f, 55000.0f, 4.0f, 4.0f, 0.0f,  1.0f},   // API 2503 — earlier, harder, SNAP
            {1.00f, 8.0f, 0.05f, 30000.0f, 1.2f, 1.0f, 0.0f,  1.0f},   // Jensen — iron colour only
            {1.50f, 1.2f, 0.18f, 25000.0f, 1.8f, 2.5f, 0.15f, 1.0f},   // Cinemag — LF-heavy, H2 bloom
            {0.20f, 0.5f, 0.30f, 35000.0f, 3.0f, 3.0f, 0.0f,  1.0f},   // Cheap Line — ruin from −20 dBFS
            {0.70f, 0.9f, 0.25f, 14000.0f, 5.0f, 6.0f, 0.0f,  1.0f},   // Germanium Radio — rings IN BAND
            {1.00f, 1.0f, 0.20f, 40000.0f, 2.0f, 2.0f, 0.80f, 1.0f},   // DC Magnetised — half-sat at rest
            {2.20f, 1.0f, 0.00f, 60000.0f, 1.5f, 0.5f, 0.0f,  2.5f},   // Toroid — pure sub-destroyer
        };
        const auto& t = XF[chr_];
        const float T2   = (float) (0.5 / fs_);
        const float tauM = 0.001f * t.tm;
        // Sag = core heat: sustained flux collapses φk, so the SAME level saturates deeper over time.
        const float fk = t.fk / (1.0f + pC_[3] * 1.5f * 1.5f * sagV_[c]);
        xfPhi_[c] = (1.0f - T2 * 0.5f) * xfPhi_[c] + (T2 / tauM) * (u * driveC_);   // leak τ_L = 2 s — BIBO MANDATORY
        // fb325 — Bias reach ×2.2 AND the STATIC magnetisation subtracted OUT of the differentiator:
        // bias used to sit inside ΔB, so turning the knob (or Drift wandering) was differentiated
        // ×2fs into loud crackle while the tonal shift stayed subtle (Max: "bias is barely doing
        // anything except adding noise"). Now the knob is silent to TURN and the audible change is
        // WHERE the flux sits on the curve — the even-harmonic asymmetry.
        const float qdc = biasE_ * 2.2f + t.dc + drW_[c];
        const float q0  = xfPhi_[c] / fk;
        float B = langevin (t.ls * (q0 + qdc)) / t.ls - langevin (t.ls * qdc) / t.ls;
        const float hys = t.hy * pC_[7];                     // Snarl = the minor loop (remanence)
        if (hys > 1.0e-5f)
            B += hys * ((xfPhi_[c] >= xfPhiP_[c]) ? 1.0f : -1.0f) * (1.0f - juce::jmin (1.0f, std::fabs (3.0f * B)));
        xfPhiP_[c] = xfPhi_[c];
        sagIn_[c]  = juce::jmin (2.5f, std::fabs (q0) * 0.6f);
        float y = (B - xfB_[c]) * (2.0f * (float) fs_) * tauM * 3.0f * fk;   // differentiate — exact inverse below knee
        xfB_[c] = B;
        xfEd_[c] += (y - xfEd_[c]) * xfEdK_;                 // core loss (magnetising-inductance LF droop)
        y -= 0.20f * xfEd_[c];
        // leakage-L × winding-C resonance — RINGS on transients (why iron adds SNAP, not just warmth)
        bqEnsure (juce::jmin (t.rf, 0.9f * (float) fs_), juce::jlimit (0.5f, 8.0f, t.rq), t.rg, false);
        return y + bqG_ * bqRun (y, c);
    }

    /** STOMP BOX — op-amp FEEDBACK diode clipper (TS-808/SD-1/Klon). Two structural identities,
        both audible blind: the pre-HP means BASS PASSES CLEAN by construction, and the feedback
        topology can NEVER square — residual log slope ⇒ crest ~2.5 where Overdrive collapses to 1.4. */
    float stompF (float u, int c) noexcept
    {
        //   {preHp, toneLp, Vf+, Vf−, shape(0 exp/1 LED/2 MOSFET), cleanBlend, rail, gainMul}
        static constexpr struct { float hp, lp, vp, vn; int shp; float cb, rail, gm; } SB[8] = {
            { 720.0f,  723.0f, 0.16f, 0.16f, 0, 0.0f, 1.00f, 1.0f},   // Green 808 — 1N4148 symmetric
            { 720.0f,  900.0f, 0.24f, 0.12f, 0, 0.0f, 1.00f, 1.0f},   // Yellow SD-1 — 2:1 asym, 1S2473 headroom
            { 500.0f, 1400.0f, 0.09f, 0.09f, 0, 0.5f, 1.00f, 1.9f},   // Klon — Ge + the parallel CLEAN blend
            { 120.0f, 2500.0f, 0.30f, 0.30f, 0, 0.0f, 1.00f, 0.6f},   // Timmy — whole band distorts, gently
            { 720.0f, 1200.0f, 0.45f, 0.45f, 1, 0.0f, 1.00f, 1.0f},   // LED — enormous headroom then a WALL
            { 720.0f, 1000.0f, 0.20f, 0.20f, 2, 0.0f, 1.00f, 1.0f},   // MOSFET — square-law knee curvature
            {1400.0f,  400.0f, 0.16f, 0.16f, 0, 0.0f, 1.00f, 1.2f},   // Mid Scoop — honky, cocked-wah
            { 720.0f,  800.0f, 0.16f, 0.16f, 0, 0.0f, 0.45f, 1.3f},   // Starved Battery — the dying 9 V
        };
        const auto& t = SB[chr_];
        if (sbChrP_ != chr_) { sbChrP_ = chr_; sbHpHz_ = t.hp; sbLpK_ = onePole2 (t.lp); sbDrvP_ = -1.0f; sbHpDrv_ = -1.0f; }
        // fb325 — Drive pulls the band-split DOWN (720 → ~180 Hz): the real pedal's split left an
        // 808 entirely in the clean band, so Drive/Bias/Emphasis measured dead on bass — exactly
        // Max's report. Cranking now pushes the drive INTO the body while low drive keeps the split.
        if (std::fabs (drive01_ - sbHpDrv_) > 1.0e-3f)
        { sbHpDrv_ = drive01_; sbHpK2_ = onePole2 (sbHpHz_ * (1.0f - 0.75f * drive01_)); }
        sbLp_[c] += (u - sbLp_[c]) * sbHpK2_;
        const float lo = sbLp_[c], hi = u - lo;              // 60 % of the identity: bass is NOT clipped
        if (std::fabs (drive01_ - sbDrvP_) > 1.0e-4f) { sbDrvP_ = drive01_; sbG_ = std::pow (55.0f, drive01_); }
        const float w = hi * sbG_ * t.gm + biasE_ * 0.35f + drW_[c] * 0.4f;
        float d;
        if      (t.shp == 1) d = (w >= 0.0f) ?  t.vp * hardClipF ( w / t.vp, 0.12f)
                                             : -t.vn * hardClipF (-w / t.vn, 0.12f);
        else if (t.shp == 2) d = w / std::sqrt (1.0f + (w * w) / (t.vp * t.vp * 6.0f));
        else                 d = (w >= 0.0f) ?  0.8f * t.vp * std::asinh ( w / (0.8f * t.vp))
                                             : -0.8f * t.vn * std::asinh (-w / (0.8f * t.vn));
        // the supply rail exists in every pedal (Sag/Recovery); Starved Battery is voiced to DIE.
        // fb325 matrix-caught: the rail was in absolute units (~1.0) while the diode clamp keeps the
        // signal near Vf (~0.2) — the rail NEVER touched the signal and Sag measured DEAD. The rail
        // now sits just above the diode ceiling, so sagging genuinely bites into the sound.
        const float rail = 2.4f * juce::jmax (t.vp, t.vn) * t.rail
                         * (1.0f - juce::jmin (0.93f,
                              juce::jmax (pC_[3] * 1.5f, (chr_ == 7) ? 1.2f : 0.0f) * 1.25f * sagV_[c]));
        d = rail * hardClipF (d / juce::jmax (0.03f, rail), 0.35f);
        d *= 1.0f + 2.2f * drive01_ + 1.6f * drive01_ * drive01_;   // fb325 — the clamped band earns level as
                                                             // the split eats the body (loudness must RISE)
        sagIn_[c] = juce::jmin (2.5f, std::fabs (d) * 3.4f);
        sbTn_[c] += (d - sbTn_[c]) * sbLpK_;                 // diode-cap + tone LP
        float y = lo + sbTn_[c] + t.cb * hi;                 // Klon: unclipped transient rides on top
        // fb325 — SNARL = THE SUB (Max: "Stomp Box is our 808 maker" + "Snarl must be key-tracked
        // and never free-run"). A CMOS flip-flop octave divider (BOSS OC-2 lineage) toggling on the
        // input's rising crossings: a square ONE OCTAVE DOWN that wears the input's own envelope —
        // key-tracked and input-gated BY CONSTRUCTION (it cannot exist without a note). Rounded by
        // a one-pole so it reads as SUB, not buzz.
        if (pC_[7] > 1.0e-4f)
        {
            if (u > 0.015f && sbPrev_[c] <= 0.015f) sbFlip_[c] = (sbFlip_[c] >= 0.0f) ? -1.0f : 1.0f;
            sbPrev_[c] = u;
            sbEnv_[c] += (juce::jmin (1.0f, std::fabs (u) * 5.0f) - sbEnv_[c]) * 0.004f;
            sbLp2_[c] += (sbFlip_[c] - sbLp2_[c]) * 0.026f;   // ~400 Hz round-off at the 2× rate
            y += pC_[7] * 0.55f * sbLp2_[c] * sbEnv_[c];
        }
        return y;
    }

    /** OVERDRIVE — a CASCADE of gain stages into SHUNT (to-ground) clipping with SLEW-RATE limiting
        (RAT/DS-1/Muff/Plexi). THE SLEW LIMITER IS THE MODE (bible: build it before the clipper —
        tanh-into-a-lowpass is the trap that collapses this into the cut Soft Sat). Shunt ⇒ it
        SQUARES (crest → 1.4); the limiter makes output amplitude FALL with frequency — the chirp
        fingerprint no memoryless waveshaper can fake. */
    float odF (float u, int c) noexcept
    {
        //   {N, slew@48k, rail, asymRail, interLp, shuntKnee, sagFloor, gainMul}
        static constexpr struct { int n; float sr, rail, ar, lp, kn, sg, gm; } ODT[8] = {
            {2,  1.39f, 1.00f, 1.00f, 2200.0f, 0.15f, 0.0f, 1.0f},   // LM308 Rat — 0.3 V/µs, the sound
            {2, 60.0f,  1.00f, 1.00f, 2200.0f, 0.15f, 0.0f, 1.0f},   // TL071 Rat — SAME circuit, 13 V/µs: the PROOF
            {2,  2.50f, 1.00f, 0.62f, 3200.0f, 0.10f, 0.0f, 1.1f},   // DS-1 — asymmetric shunt, tight
            {3,  3.00f, 0.90f, 0.90f, 2200.0f, 0.80f, 0.0f, 0.9f},   // Big Muff — soft shunt, violin sustain
            {3,  4.00f, 1.00f, 1.00f, 3500.0f, 0.60f, 0.9f, 1.0f},   // Plexi — NO diodes: the RAILS clip, heavy sag
            {3,  5.00f, 1.00f, 1.00f, 5000.0f, 0.12f, 0.0f, 1.0f},   // Modern High-Gain — percussive, tight
            {2,  0.02f, 1.00f, 1.00f, 2200.0f, 0.15f, 0.0f, 1.0f},   // Slew Kill — everything becomes a triangle
            {2,  1.00f, 0.67f, 0.85f, 2600.0f, 0.15f, 1.3f, 1.0f},   // Dying Battery — gated, motorboating
        };
        const auto& t = ODT[chr_];
        if (std::fabs (drive01_ - odDrvP_) > 1.0e-4f || odChrP_ != chr_)
        {
            odDrvP_ = drive01_; odChrP_ = chr_;
            odGs_  = std::pow (std::pow (8000.0f, drive01_) * t.gm, 1.0f / (float) t.n);  // total 1…8000 (+78 dB)
            // fb325 no-plateau: past the cascade's full-square point Drive measured frozen — the
            // inter-stage pole now narrows with drive (the Miller effect at high gain): the top of
            // the knob keeps getting DARKER and MEANER even after it can't get more square.
            odLpK_ = onePole2 (t.lp * (1.0f - 0.35f * drive01_));
        }
        const float dmax = juce::jmax (2.0e-4f, t.sr * 48000.0f / (2.0f * (float) fs_));  // BIBO floor 2e-4
        const float rail = t.rail * (1.0f - juce::jmin (0.94f, juce::jmax (t.sg, pC_[3] * 1.5f) * 0.62f * sagV_[c]));
        const float bdc  = ((biasE_ > 0.0f) ?  biasE_ * 0.5f  : 0.0f)   // Bias+ : DC at the shunt node
                         + juce::jmax (0.0f, drive01_ - 0.6f) * 0.30f;  // fb325: top of Drive keeps evolving (evens)
        const float bdz  = (biasE_ < 0.0f) ? -biasE_ * 0.25f : 0.0f;   // Bias− : class-B deadzone
        float v = u;
        float pk = 0.0f;
        for (int s = 0; s < t.n; ++s)
        {
            v *= odGs_;
            const float dd = v - odSl_[s][c];                          // THE SLEW LIMITER
            v = odSl_[s][c] + dmax * std::tanh (dd / dmax);            // soft: C1, ~12 dB/oct less alias
            odSl_[s][c] = juce::jlimit (-2.5f, 2.5f, v);               // state clamped: direction changes stay immediate
            v = odSl_[s][c];
            if (bdz > 1.0e-5f) v = (v >= 0.0f) ? juce::jmax (0.0f, v - bdz) : juce::jmin (0.0f, v + bdz);
            v += bdc;
            v = (v >= 0.0f) ? rail          * hardClipF (v / juce::jmax (0.05f, rail),          t.kn)
                            : rail * t.ar   * hardClipF (v / juce::jmax (0.05f, rail * t.ar),   t.kn);
            v -= bdc;                                                  // re-centre; the blocker takes the rest
            odLp_[s][c] += (v - odLp_[s][c]) * odLpK_;                 // inter-stage LP INSIDE the loop (free AA)
            v = odLp_[s][c];
            pk = juce::jmax (pk, std::fabs (v));
        }
        sagIn_[c] = juce::jmin (2.5f, pk * 1.1f);                      // the rail droops on program level
        return v;
    }

    // ── ANALOG shared math ───────────────────────────────────────────────────
    static float langevin (float x) noexcept                 // L(x) = coth x − 1/x, guards MANDATORY
    {
        const float ax = std::fabs (x);
        if (ax < 1.0e-4f) return x * (1.0f / 3.0f);
        if (ax > 12.0f)   return (x > 0.0f ? 1.0f : -1.0f) - 1.0f / x;
        return 1.0f / std::tanh (x) - 1.0f / x;
    }
    static float langevinD (float x) noexcept                // L'(x) = 1/x² − coth²x + 1
    {
        const float ax = std::fabs (x);
        if (ax < 1.0e-4f) return 1.0f / 3.0f;
        if (ax > 12.0f)   return 1.0f / (x * x);
        const float ct = 1.0f / std::tanh (x);
        return 1.0f / (x * x) - ct * ct + 1.0f;
    }
    static float spSoft (float v, float C) noexcept          // softplus_C, overflow-safe (Dempwolf eq 10)
    {
        const float cv = C * v;
        if (cv >  15.0f) return v;
        if (cv < -15.0f) return std::exp (juce::jmax (-60.0f, cv)) / C;
        return std::log1p (std::exp (cv)) / C;
    }
    float onePole2 (float hz) const noexcept                 // one-pole coefficient AT THE 2× RATE
    {
        const float f = juce::jlimit (5.0f, (float) (fs_ * 0.98), hz);
        return juce::jlimit (0.0f, 1.0f, (float) (1.0 - std::exp (-juce::MathConstants<double>::pi * f / fs_)));
    }
    /** Shared ANALOG biquad (Tape's head-bump peaking / Transformer's leakage-resonance bandpass) —
        the two modes are never active at once, so one state + one cached coef set serves both. */
    void bqEnsure (float f0, float q, float gDb, bool peak) noexcept
    {
        if (bqMode_ == mode_ && bqChr_ == chr_ && bqFs_ == (float) fs_) return;
        bqMode_ = mode_; bqChr_ = chr_; bqFs_ = (float) fs_;
        const float fs2 = 2.0f * (float) fs_;
        const float w0 = 6.2831853f * juce::jlimit (10.0f, 0.45f * fs2, f0) / fs2;
        const float sn = std::sin (w0), cs = std::cos (w0);
        const float al = sn / (2.0f * juce::jmax (0.3f, q));
        if (peak)
        {
            const float A  = std::pow (10.0f, gDb / 40.0f);
            const float a0 = 1.0f + al / A;
            bqB0_ = (1.0f + al * A) / a0; bqB1_ = (-2.0f * cs) / a0; bqB2_ = (1.0f - al * A) / a0;
            bqA1_ = (-2.0f * cs) / a0;    bqA2_ = (1.0f - al / A) / a0;
            bqG_  = 1.0f;
        }
        else
        {
            const float a0 = 1.0f + al;
            bqB0_ = al / a0; bqB1_ = 0.0f; bqB2_ = -al / a0;
            bqA1_ = (-2.0f * cs) / a0; bqA2_ = (1.0f - al) / a0;
            bqG_  = std::pow (10.0f, gDb / 20.0f) - 1.0f;
        }
        for (int ch = 0; ch < 2; ++ch) bqX1_[ch] = bqX2_[ch] = bqY1_[ch] = bqY2_[ch] = 0.0f;
    }
    float bqRun (float xin, int c) noexcept
    {
        const float y = bqB0_ * xin + bqB1_ * bqX1_[c] + bqB2_ * bqX2_[c] - bqA1_ * bqY1_[c] - bqA2_ * bqY2_[c];
        bqX2_[c] = bqX1_[c]; bqX1_[c] = xin; bqY2_[c] = bqY1_[c]; bqY1_[c] = flushDenorm (y);
        return bqY1_[c];
    }

    // ── DIGITAL — sample-domain destruction, 1×, L/R, NO anti-aliasing (the artefacts ARE the
    // product). Recycles VintageReverb's proven formulas: phase-accumulator ZOH, mid-tread crush
    // with TPDF dither, reconstruction LP at 0.45·fs_r. Two axes per mode: SIG is the front axis,
    // P3 the second, ordered per mode so you can stack rate + bits in ONE instance.
    float digitalOne (float x, int c, float drv) noexcept
    {
        const float in0 = x;
        loCutZ_[c] += (x - loCutZ_[c]) * loC_;                 // P1 — keeps sub-bass out of the grid
        x -= loCutZ_[c];
        // 🔑 MEASURED SCALING FIX: at ×kPreGain the driven signal peaked at ~3.7 — past the ±2.0
        // quantiser rail — so the "bitcrush" was actually the CLAMP hard-clipping (THD pinned at 47%
        // at EVERY level and EVERY bit depth; the 6 dB/bit law never appeared). The destroyer's grid
        // is defined for signal ≈ ±1 FS, so the digital path runs at ×0.25 of the analog drive scale
        // (bus level lands ≈ 0.9 at default Drive) and makes it back up on the way out. Drive still
        // pushes INTO the clamp at the top — deliberately: that is "drive into the converter".
        // fb323 GRID NORMALISATION: a real converter's input is normalised to its own full scale —
        // ours saw the raw −26 dBFS bus, so at working settings the wrap threshold NEVER engaged
        // (0.47 vs a 0.15-peak signal) and "12-bit" had 250 steps of headroom (inaudible). ×8 into
        // the grid, ÷8 out: unity preserved, the destroyer operates like the machines it models.
        float u = x * kPreGain * drv * 0.25f * 8.0f;

        // fb325 — Feedback amplified ×2.2 (Max: "supposed to be a crazy powerful knob") and gated
        // by the input envelope (nothing-sounds-without-input hard rule — Melt self-osc included).
        inEnv_[c] += (std::fabs (in0) - inEnv_[c]) * ((std::fabs (in0) > inEnv_[c]) ? envA_ : envR_);
        float fbA = pC_[7] * 2.2f;                             // P8 — loop gain DELIBERATELY > 1
        if (mode_ == Overflow && chr_ == 7) fbA = juce::jmax (fbA, 0.66f);   // Melt — preloaded self-oscillation
        fbA *= juce::jmin (1.0f, inEnv_[c] * 30.0f);
        if (fbA > 1.0e-5f) u += fbA * fbZ_[c];                 //      tanh state is the BIBO bound
        const float uIn = u;                                   // clean input for all crossing detectors

        const float sig = kneeC_;                              // the front Crush axis, raw 0..1
        float rate01 = (mode_ == Downsample) ? sig : pC_[2];
        float bits01 = (mode_ == Bitcrush)   ? sig : (mode_ == Downsample ? pC_[2] : 0.0f);
        const float wrap01 = (mode_ == Overflow) ? sig : 0.0f;

        // fb323 CHARACTER = WHICH CONVERTER (bible §9.6 — the real machines): SP-1200 (12-bit /
        // gentle image filter), Fairlight CMI (8-bit, filter OFF), G.711 Telephone (µ-law + band),
        // Warble (clock random-WALK), Track&Hold (leaking cap), Peak Hold (signal-locked clock),
        // Shatter (two clocks φ=1.618) · Bitcrush: mid-riser / Sputter / µ-law / A-law / noise-Shaped
        // / 12-Bit / Overs-wrap · Overflow: Two's Comp / Bounce / Mirror sub-octave / XOR / Rotate /
        // Soft Wrap / Melt.
        if (mode_ == Downsample)
        {
            if (chr_ == 1) { bits01 = juce::jmax (bits01, 0.109f); }             // SP-1200 — 12-bit second axis
            if (chr_ == 2) { bits01 = juce::jmax (bits01, 0.280f); }             // Fairlight — 8-bit, no image filter
        }
        if (mode_ == Bitcrush && chr_ == 6) bits01 = juce::jmax (bits01, 0.109f); // 12-Bit floor

        if (wrap01 > 1.0e-3f)                                  // OVERFLOW — the container breaks
        {
            float thr = 1.0f - 0.97f * wrap01;                 // 1 → 0.03 (§2.3: the most destructive)
            // fb325 matrix-caught: Dither (P5) was guarded behind the QUANTISER, which Overflow
            // never runs — the knob was structurally DEAD here. TPDF now jitters the wrap itself.
            if (pC_[4] > 1.0e-4f) u += (rnd01 (c) + rnd01 (c) - 1.0f) * pC_[4] * thr * 0.8f
                                        * juce::jmin (1.0f, inEnv_[c] * 30.0f);   // input-gated (no hiss law)
            if (chr_ == 3)                                     // Mirror — period halves on alternate cycles:
            {                                                  //   a sub-octave appears UNDER the destruction
                if (prevU_[c] < 0.0f && uIn >= 0.0f) mirF_[c] = (mirF_[c] > 0.75f) ? 0.5f : 1.0f;
                thr *= mirF_[c];
            }
            if (chr_ == 7) thr *= 1.0f + 0.35f * fbZ_[c];      // Melt — the threshold itself is modulated
            const float thN = (chr_ == 1) ? thr * 1.06f : thr; // Two's Comp — the asymmetric −32768 code
            if (chr_ == 2)                                     // Bounce — wraps positive, CLIPS negative
            {
                if (u > thr)      { float v = std::fmod (u + thr, 2.0f * thr); if (v < 0.0f) v += 2.0f * thr; u = v - thr; }
                else if (u < -thr) u = -thr;
            }
            else
            {
                const float span = thr + thN;
                float v = std::fmod (u + thN, span * 2.0f);    // wrap into [−thN, thr] (asym when Two's Comp)
                if (v < 0.0f) v += span * 2.0f;
                u = v - thN;
            }
            if (chr_ == 4)                                     // Corrupt — integer XOR: the damaged file
            {
                const int i5 = (int) (juce::jlimit (-1.0f, 1.0f, u / juce::jmax (0.03f, thr)) * 8192.0f);
                u = (float) (i5 ^ 0x0AA) / 8192.0f * thr;
            }
            if (chr_ == 5)                                     // Rotate — MSBs become LSBs: loud collapses
            {                                                  //   to quiet, quiet explodes. Total inversion
                const uint16_t i6 = (uint16_t) (int) (juce::jlimit (-1.0f, 1.0f, u * 0.5f) * 16384.0f);
                u = (float) (int16_t) (uint16_t) ((i6 << 2) | (i6 >> 14)) / 16384.0f * 2.0f;
            }
            if (chr_ == 6) { zsLp_[c] += (u - zsLp_[c]) * 0.5f; u = zsLp_[c]; }   // Soft Wrap — half the fizz
        }
        if (bits01 > 1.0e-3f)                                  // BITCRUSH — §2.5 taper, sub-bit floor
        {
            const float bits = 0.5f + 15.5f * std::pow (1.0f - bits01, 2.6f);
            const float D    = std::pow (2.0f, 1.0f - bits);
            const bool isBC  = (mode_ == Bitcrush);
            if (pC_[4] > 1.0e-4f) u += (rnd01 (c) + rnd01 (c) - 1.0f) * D * pC_[4] * 2.0f
                                        * juce::jmin (1.0f, inEnv_[c] * 30.0f);  // P5 TPDF — input-gated (no hiss law)
            if (isBC && chr_ == 5) u += errZ_[c];              // Shaped — error feedback: crush reads as hiss, not grit
            float q;
            if (isBC && (chr_ == 3 || chr_ == 4))              // Telephone (µ-law 255) / A-Law (87.6) — G.711:
            {                                                  //   companded quantisation, a genuinely other voice
                const float s5 = (u < 0.0f) ? -1.0f : 1.0f, ax = juce::jmin (1.0f, std::fabs (u) * 0.5f);
                float comp;
                if (chr_ == 3) comp = std::log1p (255.0f * ax) / std::log (256.0f);
                else { const float A5 = 87.6f, dn = 1.0f + std::log (A5);
                       comp = (ax < 1.0f / A5) ? (A5 * ax / dn) : ((1.0f + std::log (A5 * ax)) / dn); }
                comp = D * std::round (comp / D);
                const float ex = (chr_ == 3) ? (std::pow (256.0f, comp) - 1.0f) / 255.0f
                                             : ((comp * (1.0f + std::log (87.6f)) <= 1.0f) ? comp * (1.0f + std::log (87.6f)) / 87.6f
                                                                                           : std::exp (comp * (1.0f + std::log (87.6f)) - 1.0f) / 87.6f);
                q = s5 * ex * 2.0f;
            }
            else if (isBC && chr_ == 1) q = (std::floor (u / D) + 0.5f) * D;      // Zero-Sq — mid-RISER: 1-bit is a solid square
            else if (isBC && chr_ == 2) q = (std::fabs (u) < 1.25f * D) ? 0.0f    // Sputter — dead zone 2.5×: the broken sampler
                                                                        : D * std::round (u / D);
            else                        q = D * std::round (u / D);
            if (isBC && chr_ == 7 && std::fabs (q) > 2.0f)     // Overs — the top code WRAPS: real converter overflow
            {   float v = std::fmod (q + 2.0f, 4.0f); if (v < 0.0f) v += 4.0f; q = v - 2.0f; }
            else q = juce::jlimit (-2.0f, 2.0f, q);            // clamp ±2.0, NEVER ±1 (sub-bit codes)
            if (isBC && chr_ == 5) errZ_[c] = u - q;
            u = q;
        }
        float fsr = (float) fs_;
        if (rate01 > 1.0e-3f)                                  // DOWNSAMPLE — exponential in Hz, fs → 20
        {
            // 🔑 POLARITY: 0 = clean, up = destroy (harness-caught inversion, fb320).
            fsr = 20.0f * std::pow ((float) (fs_ / 20.0), 1.0f - rate01);
            if (mode_ == Downsample && chr_ == 6)              // Peak Hold — the clock locks to the MATERIAL:
            {                                                  //   track the cycle's peak, latch it at each rising
                hold2V_[c] = juce::jmax (hold2V_[c] * 0.9999f, std::fabs (u));   // zero crossing ⇒ the staircase
                if (prevU_[c] < 0.0f && uIn >= 0.0f) { holdV_[c] = hold2V_[c]; hold2V_[c] = 0.0f; }
                u = (uIn >= 0.0f ? 1.0f : -1.0f) * holdV_[c];  //   rings at the NOTE's pitch, not the clock's
            }
            else
            {
                float inc = fsr / (float) fs_;
                float jit = pC_[5];
                if (mode_ == Downsample && chr_ == 4)          // Warble — jitter floored, reshaped as a random
                {                                              //   WALK: the clock DRIFTS instead of fizzing
                    jit = juce::jmax (jit, 0.25f);
                    jWalk_[c] += (rnd11 (c) - jWalk_[c]) * 0.002f;
                    inc *= 1.0f + jit * jWalk_[c] * 3.0f;
                }
                else if (jit > 1.0e-4f) inc *= 1.0f + jit * rnd11 (c);       // P6 Jitter — per-sample grid instability
                if (c == 1 && pC_[6] > 1.0e-4f) inc *= 1.0f + pC_[6] * 0.13f;// P7 Spread — 0 = byte-identical
                holdPh_[c] += inc;
                if (holdPh_[c] >= 1.0f) { holdPh_[c] -= (float) (int) holdPh_[c]; holdV_[c] = u; }
                if (mode_ == Downsample && chr_ == 5) holdV_[c] *= 0.9896f;  // Track&Hold — the cap LEAKS (τ≈2 ms):
                u = holdV_[c];                                               //   every step becomes a tiny ramp, audibly analogue
                if (mode_ == Downsample && chr_ == 7)          // Shatter — a second clock at φ = 1.618:
                {                                              //   two inharmonic staircases beating
                    hold2Ph_[c] += inc * 1.618f;
                    if (hold2Ph_[c] >= 1.0f) { hold2Ph_[c] -= (float) (int) hold2Ph_[c]; hold2V_[c] = u; }
                    u = 0.5f * (u + hold2V_[c]);
                }
            }
        }
        prevU_[c] = uIn;
        if (mode_ == Downsample && chr_ == 3)                  // Telephone — the G.711 band: 300 Hz – 3.4 kHz
        {
            telHp_[c] += (u - telHp_[c]) * 0.039f;  u -= telHp_[c];
            zsLp_[c]  += (u - zsLp_[c])  * 0.363f;  u  = zsLp_[c];
        }
        // fb325 — the feedback color pole (see one()): P8's top half retunes the chaos instead of
        // freezing at "maximum scream".
        if (std::fabs (pC_[7] - fbLpP_) > 1.0e-4f)
        { fbLpP_ = pC_[7]; const float o = 1.0f - pC_[7]; fbLpK_ = onePole2 (400.0f + 14000.0f * o * o); }
        fbLp_[c] += (std::tanh (u) - fbLp_[c]) * fbLpK_;
        fbZ_[c] = flushDenorm (fbLp_[c]);
        if (pC_[3] > 1.0e-4f)                                  // P4 Smooth — reconstruction LP @0.45·fs_r
        {
            const float fc = juce::jmax (200.0f, 0.45f * fsr);
            const float kk = onePole (fc + (1.0f - pC_[3]) * (20000.0f - juce::jmin (20000.0f, fc)));
            smZ_[c] += (u - smZ_[c]) * kk;
            u = smZ_[c];
        }
        float y = u * 0.125f;                                  // ÷8 grid denormalisation (net unity at Drive 0)
        const float d = y - dcX_[c] + dcR_ * dcY_[c];          // Overflow's asymmetric wraps park off zero
        dcX_[c] = y; dcY_[c] = d; y = d;
        if (hiBypass_) { hiCutZ_[c] = hiCut2_[c] = hiCut3_[c] = y; }
        else
        {
            hiCutZ_[c] += (y          - hiCutZ_[c]) * hiC_;    // fb325 — 3-pole, matching the main path
            hiCut2_[c] += (hiCutZ_[c] - hiCut2_[c]) * hiC_;
            hiCut3_[c] += (hiCut2_[c] - hiCut3_[c]) * hiC_;
            y = hiCut3_[c];
        }
        if (std::fabs (toneC_) > 1.0e-4f)
        {
            toneZ_[c] += (y - toneZ_[c]) * kToneCoef;
            const float hi2 = y - toneZ_[c];
            y = toneZ_[c] * (1.0f - toneC_ * 0.95f) + hi2 * (1.0f + toneC_ * 0.95f);
        }
        const float mw = mixC_ * dip_;                         // 1× family ⇒ dry already aligned
        return flushDenorm (in0 * (1.0f - mw) + y * mw);
    }

    float anti (float u) noexcept   // dormant until ADAA returns inside 4× oversampling
    {
        switch (mode_)
        {
            case HardClip: return hardClipA (u, kneeWidth());
            case SoftClip:
            default:       return softClipA (u, kneeE_);
        }
    }

    // ── SOFT CLIP — the BJT long-tailed pair. tanh is the LITERAL I-V law here, not an
    //    approximation, and the thermal voltage sets the knee width in ABSOLUTE volts — which is
    //    exactly why the knee must never be normalised by Drive.
    //    Three anchors, all f(0)=0, f'(0)=1, |f|->1, all with closed-form antiderivatives.
    //    ADAA is LINEAR in f, so blending the antiderivatives with the SAME weights keeps a
    //    continuously-morphing knee exactly ADAA-able. Nothing else in the device gets that.
    static float softClipF (float x, float k) noexcept
    {
        const float A = x / (1.0f + std::fabs (x));
        const float B = x / std::sqrt (1.0f + x * x);
        // fb325 — the tight anchor is now a QUARTIC ALGEBRAIC SIGMOID, not the old cubic: the cubic
        // hit a hard rail at |x| = 1.5, so at low Knee "Soft" measured ≈ Hard Clip (Max: "soft and
        // hard are damn near the same thing"). Soft Clip now NEVER rails — compression at every
        // knee, C∞ everywhere — the structural night-and-day against Hard's corner-and-rail.
        const float x2 = x * x;
        const float C = x / std::sqrt (std::sqrt (1.0f + 0.62f * x2 * x2));
        return (k <= 0.5f) ? ((1.0f - 2.0f * k) * C + (2.0f * k) * B)
                           : ((2.0f - 2.0f * k) * B + (2.0f * k - 1.0f) * A);
    }
    static float softClipA (float x, float k) noexcept
    {
        const float a  = std::fabs (x);
        const float FA = a - std::log1p (a);
        const float FB = std::sqrt (1.0f + x * x) - 1.0f;
        const float FC = (a <= 1.5f) ? (x * x * 0.5f - (x * x * x * x) / 27.0f)
                                     : (0.9375f + (a - 1.5f));
        return (k <= 0.5f) ? ((1.0f - 2.0f * k) * FC + (2.0f * k) * FB)
                           : ((2.0f - 2.0f * k) * FB + (2.0f * k - 1.0f) * FA);
    }

    // ── HARD CLIP — an op-amp or diode pair into its rails. What makes it a design rather than a
    //    clamp() call is the KNEE: a real corner has finite width fixed in absolute volts, so it
    //    narrows relative to the signal as you drive. Soft-knee quadratic => f, F1 and F2 are all
    //    piecewise POLYNOMIALS (no transcendentals), which is why ADAA-2 will be nearly free here.
    static float hardClipF (float u, float w) noexcept
    {
        const float v = std::fabs (u);
        const float s = (u < 0.0f) ? -1.0f : 1.0f;
        if (w < 1.0e-4f) return s * (v < 1.0f ? v : 1.0f);        // exact razor (avoids the /w blow-up)
        const float a = 1.0f - w * 0.5f, b = 1.0f + w * 0.5f;
        if (v <= a) return s * v;
        if (v >= b) return s;
        const float t = v - a;
        return s * (v - t * t / (2.0f * w));                       // C1: f(a)=a, f'(a)=1, f(b)=1, f'(b)=0
    }
    static float hardClipA (float u, float w) noexcept             // F1 is EVEN
    {
        const float v = std::fabs (u);
        if (w < 1.0e-4f) return (v <= 1.0f) ? (v * v * 0.5f) : (v - 0.5f);
        const float a = 1.0f - w * 0.5f, b = 1.0f + w * 0.5f;
        if (v <= a) return v * v * 0.5f;
        if (v >= b) return b * b * 0.5f - w * w / 6.0f + (v - b);
        const float t = v - a;
        return v * v * 0.5f - (t * t * t) / (6.0f * w);
    }

    // ── helpers ──────────────────────────────────────────────────────────────
    /** Per-family drive ceiling. CLIP/DIODE/SHAPER +48 dB; ANALOG +30 (its modes self-limit, and
        `Slam` buys the nuclear option without destroying resolution in the useful 0..+12 dB region);
        FOLD +36 (tapered so FOLD COUNT, not gain, is linear in the knob). */
    float maxDriveDb() const noexcept
    {
        switch (mode_)
        {
            case Tube: case Tape: case Transformer: case StompBox: case Overdrive: return 30.0f;
            case LinearFold: case SineFold: case WestCoast:                        return 36.0f;
            default:                                                               return 48.0f;
        }
    }
    /** Hard Clip's knee in absolute units. fb325 — capped at 0.9 (was 2.0): a fully-parabolic knee
        IS a soft clipper, which made Hard at high Knee collapse into Soft's territory. Hard always
        rails now; the knee only decides how sharp the corner is. */
    float kneeWidth() const noexcept { return 0.9f * juce::jmin (1.0f, kneeE_ * 2.0f); }
    /** fb325 — the knee's SECOND life (matrix-caught: at working drive the signal sits 10-50× past
        every corner, so corner width measured bit-identical across the whole knob). The top half of
        Knee now PRE-ROUNDS the wave through the quartic sigmoid BEFORE the rail/threshold — audible
        as duller, thicker shoulders at ANY drive. The mode still rails: identity intact. */
    /** fb325 — the knee that survives DEEP drive: a rail erases everything a pre-shaper did
        (measured bit-identical squares across the whole knob at working drive). The top half of
        Knee slew-limits the OUTPUT edges instead: razor square → trapezoid, level-dependent (a real
        slew, not a filter), audible at any drive. */
    float kneePost (float y, int c) noexcept
    {
        const float pre = juce::jmax (0.0f, kneeE_ * 2.0f - 1.0f);
        if (pre < 1.0e-3f) { kpZ_[c] = y; return y; }
        const float sU = 0.55f * std::pow (0.035f, pre);
        kpZ_[c] += juce::jlimit (-sU, sU, y - kpZ_[c]);
        return kpZ_[c];
    }
    float kneePre (float u) const noexcept
    {
        const float pre = juce::jmax (0.0f, kneeE_ * 2.0f - 1.0f);
        if (pre < 1.0e-3f) return u;
        const float x2 = u * u;
        const float q  = u / std::sqrt (std::sqrt (1.0f + 0.10f * x2 * x2));
        return (1.0f - pre) * u + pre * q * 1.8f;
    }

    float onePole (float hz) const noexcept
    {
        const float f = juce::jlimit (5.0f, (float) (fs_ * 0.49), hz);
        return juce::jlimit (0.0f, 1.0f, (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * f / fs_)));
    }
    static float flushDenorm (float v) noexcept { return (std::fabs (v) < 1.0e-20f) ? 0.0f : v; }

    // ── state ────────────────────────────────────────────────────────────────
    double fs_ = 48000.0;
    int    mode_ = SoftClip, chr_ = 0, qual_ = 1;
    bool   autoOn_ = false, pill2_ = false;

    float driveT_ = 1.0f, driveC_ = 1.0f;
    float kneeT_  = 0.65f, kneeC_ = 0.65f;
    float biasT_  = 0.0f,  biasC_ = 0.0f;
    float toneT_  = 0.0f,  toneC_ = 0.0f;
    float emphT_  = 0.0f,  emphC_ = 0.0f;
    float loT_    = 0.0f,  loC_   = 0.0f;
    float hiT_    = 1.0f,  hiC_   = 1.0f;

    float mixT_ = 1.0f, mixC_ = 1.0f;
    // The back-8, raw. Interpreted per FAMILY at the point of use (see setP).
    float pT_[8] { 0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f };
    float pC_[8] { 0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f };
    float fbZ_[2] {};        // CLIP Feedback / DIODE Snarl / DIGITAL Feedback — recursion state
    float punchZ_[2] {};     // CLIP Punch — transient follower state
    float slewZ_[2] {};      // Slew Clip / DIODE Slew — slope-limiter state
    float holdV_[2] {}, holdPh_[2] {};   // DIGITAL — ZOH held value + phase accumulator
    float smZ_[2] {};        // DIGITAL — reconstruction LP state
    uint32_t rngC_[2] { 0x12345678u, 0x12345678u };   // per-channel xorshift — SAME seed ⇒ Spread 0 is byte-identical L/R
    float drive01_ = 0.2f;   // raw knob (the DIODE falling-threshold law reads it)
    float dip_ = 1.0f;       // mode-switch wet dip (0 → 1 over ~15 ms)
    float kneeE_ = 0.65f, biasE_ = 0.0f, asymE_ = 0.0f, zsGate_ = 0.0f;   // per-family resolved
    // fb323 — Character voicing state (per channel). Shared across modes where the modes can never
    // be active simultaneously (env40_ serves SlewClip Bounce, Diode2 Ratchet AND WestCoast Timbre
    // Sweep; zsLp_ serves Zero-Square Slew, Overflow SoftWrap and the Telephone LP).
    float sqzZ_[2] {};       // Soft Clip 'Squeeze' — 30 ms knee-hardening follower
    float sagZ_[2] {};       // Hard Clip 'Sag' — 120 ms rail-droop envelope
    float zsSgn_[2] {};      // Zero-Square 'Schmitt' — hysteresis sign state
    float zsEnv_[2] {};      // Zero-Square 'Ring' — 5 ms pedestal envelope
    float zsLp_[2] {};       // shared one-pole (ZS Slew / Overflow SoftWrap / Telephone LP)
    float env40_[2] {};      // shared slow envelope (Slew Bounce / D2 Ratchet / WC Timbre Sweep)
    float jWalk_[2] {};      // random-walk state (Warble clock / D2 Sputter gap)
    float slewLim_[2] {};    // Slew Clip 'Ring' — was-limiting flag
    float prevU_[2] {};      // crossing detector (Mirror / Peak Hold)
    float hold2V_[2] {}, hold2Ph_[2] {};   // second clock (Shatter) / peak accumulator (Peak Hold)
    float errZ_[2] {};       // Bitcrush 'Shaped' — noise-shaping error feedback
    float mirF_[2] { 1.0f, 1.0f };         // Overflow 'Mirror' — period toggle
    float telHp_[2] {};      // Telephone band HP
    float wcLp_[2] {};       // West Coast output pole (1.33 kHz museum / ~8 kHz extended)
    float fStB_ = 1.0f, fSpO_ = 0.0f, fRbM_ = 1.0f, fCnO_ = 0.0f;   // FOLD ladder Character biases

    // ── fb324 — ANALOG family state (Phase C) ────────────────────────────────
    static constexpr int   kDr = 4096, kDrM = 4095;   // drift/wow ring: ±12 ms up to 192k
    static constexpr float kTubeIsat = 2.2f;          // the load line's soft plate ceiling
    float sagV_[2] {}, sagIn_[2] {};                  // the SAG/RECOVERY reservoir (per mode-defined units)
    float recovP_ = -1.0f, aRecC_ = 0.01f;            // Recovery coef cache (exp gated on knob move)
    float rateP_  = -1.0f, rateC_ = 1.0f;             // Drift Rate cache
    float drPh_[2] {}, drTgt_[2] {}, drZ_[2] {}, drW_[2] {};   // band-limited random wander (±0.4)
    float drNxt_ = 1.0f, drS_ = 0.0f;                 // fb325 — aperiodic interval + 2nd smoothing pole
    float hiCut2_[2] {}, hiCut3_[2] {};               // fb325 — Hi Cut poles 2+3 (−18 dB/oct)
    float inEnv_[2] {};                               // fb325 — input env (feedback never free-runs)
    float envA_ = 0.01f, envR_ = 0.0001f, aAtk4_ = 0.005f;
    float emphG_ = 1.0f, emphP_ = 9.0f;               // fb325 — dB-symmetric emphasis gain cache
    float wRotP_ = -1.0f, wCs_ = 0.5f, wSn_ = 0.5f;   // fb325 — Width M/S→L/R rotation cache
    float wp1_[2] {}, wp2_[2] {}, wp3_[2] {};         // tape wow/flutter LFO stack (0.6/2.2/7 Hz)
    int   dwi_[2] {};
    float drRing_[2][kDr] {};                         // wow delay (cubic-read — comb-click law)
    float tubeBlk_[2] {}, tubeIgS_[2] {};             // grid-blocking cap volts + last grid current
    int   tubeCk_ = -1; float tubeCb_ = 1.0e9f;       // tube op-point cache key
    float tubeIL0_ = 0.0f, tubeG1_ = 1.0f;
    float tapeM_[2] {}, tapeH1_[2] {}, tapeHd_[2] {}, tapeGl_[2] {}, wornW_[2] {};   // J-A state
    float tpDrvP_ = -1.0f, tpMk_ = 1.0f;              // tape earned-loudness makeup cache
    float xfPhi_[2] {}, xfPhiP_[2] {}, xfB_[2] {}, xfEd_[2] {};   // transformer flux/induction state
    float xfEdK_ = 0.1f;                              // 4 kHz core-loss pole (set in prepare)
    float sbLp_[2] {}, sbTn_[2] {};                   // stomp band split + tone LP
    int   sbChrP_ = -1; float sbHpK_ = 0.1f, sbLpK_ = 0.1f, sbDrvP_ = -1.0f, sbG_ = 1.0f;
    float sbHpHz_ = 720.0f, sbHpDrv_ = -1.0f, sbHpK2_ = 0.1f;   // fb325 — drive-tracking band split
    float sbFlip_[2] { 1.0f, 1.0f }, sbPrev_[2] {}, sbEnv_[2] {}, sbLp2_[2] {};   // fb325 — the SUB flip-flop
    float fbLp_[2] {}, fbLpP_ = -1.0f, fbLpK_ = 0.3f; // fb325 — feedback scream-color pole
    float kpZ_[2] {};                                 // fb325 — Knee edge-slew state (post-rail)
    float odSl_[3][2] {}, odLp_[3][2] {};             // overdrive per-stage slew + inter-stage LP
    int   odChrP_ = -1; float odDrvP_ = -1.0f, odGs_ = 1.0f, odLpK_ = 0.1f;
    float slamT_ = 1.0f, slamC_ = 1.0f;               // the Slam pill's glided ×10
    float bqB0_ = 0, bqB1_ = 0, bqB2_ = 0, bqA1_ = 0, bqA2_ = 0, bqG_ = 0;   // shared analog biquad
    int   bqMode_ = -1, bqChr_ = -1; float bqFs_ = 0;
    float bqX1_[2] {}, bqX2_[2] {}, bqY1_[2] {}, bqY2_[2] {};

    float rnd01 (int c) noexcept { uint32_t& r = rngC_[c]; r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                                   return (float) (r & 0xFFFFFFu) / 16777216.0f; }
    float rnd11 (int c) noexcept { return rnd01 (c) * 2.0f - 1.0f; }

    AdaaState st_[2];
    float dcX_[2] {}, dcY_[2] {};
    float emphZ_[2] {}, deEmphZ_[2] {};
    float hiCutZ_[2] {}, loCutZ_[2] {}, toneZ_[2] {};
    // 2× oversampler state (per channel): input ring, the two decimator phase rings, and the
    // latency-matched dry delay.
    float hb_[kT] {};
    float xr_[2][kRB] {}, se_[2][kRB] {}, so_[2][kRB] {}, dry_[2][kRB] {};
    int   xi_[2] { 0, 0 }, si_[2] { 0, 0 }, di_[2] { 0, 0 };
    float dcR_ = 0.9987f, autoA_ = 0.9999f, autoZ_ = 0.0f, smth_ = 0.002f;
    bool  hiBypass_ = true;    // Hi Cut at max = TRUE bypass (an "open" 22 kHz pole still cost -1.1 dB @20k)
    bool  useAdaa_  = false;   // ⚠️ OFF at 1x — its cos(pi*f/fs) aperture nulls Nyquist. ON once oversampled.

    static constexpr float kEmphCoef  = 0.14f;   // ~1.2 kHz hinge at 48k
    static constexpr float kToneCoef  = 0.085f;  // ~700 Hz tilt hinge at 48k
    static constexpr float kPunchAtk  = 0.0064f; // ~3 ms  attack  @48k
    static constexpr float kPunchRel  = 0.00026f;// ~80 ms release @48k
    static constexpr float refLevel_  = 0.05f;   // Auto's target = the bus operating level (unity law)
    // Input pre-gain into the shaper. fb321 RE-CALIBRATED FROM MAX'S FIELD REPORT: at 8.0 the whole
    // Drive travel collapsed into its first ~10% ("I can't turn the drive up to 5-10% without it
    // sounding like 100%") because the -26 dBFS bus hit the threshold at ~+4 dB of drive. At 4.0 the
    // saturation onset lands ~15-20% travel on a single note (~10% on a chord, which is hotter), and
    // the top half of the knob is genuine destruction — the Serum-like arc. The earlier "dead first
    // 20%" measurement was a single -26 dBFS sine; real material is hotter, and the field report is
    // the better calibration source.
    static constexpr float kPreGain   = 4.0f;
};

} // namespace tw
