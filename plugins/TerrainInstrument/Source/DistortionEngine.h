#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <atomic>   // fb328 — user-curve handoff flags (UI thread → audio-thread bake)
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
        // fb342 — the 64 odd taps baked REVERSED (hoU_[j] = hb_[2(kNH−1−j)+1]) into one contiguous
        // array: both polyphase dots then read an ascending-time window of the mirrored ring as a
        // straight a[j]·b[j], which is the form clang auto-vectorises (NEON FMA). SAME coefficients.
        for (int j = 0; j < kNH; ++j) hoU_[j] = hb_[2 * (kNH - 1 - j) + 1];

        // 10 Hz DC blocker, sample-rate aware (the in-tree TerrainFilters one hardcodes 0.995,
        // which is 38 Hz at 48k and 76 Hz at 96k — it changes character with sample rate).
        dcR_ = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 10.0 / fs_);
        dcR_ = juce::jlimit (0.90f, 0.99999f, dcR_);
        dcR2_ = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 10.0 / (2.0 * fs_));   // fb336 — Octave's blocker runs at the 2× rate
        dcR2_ = juce::jlimit (0.90f, 0.99999f, dcR2_);
        // fb337 — F of the fb325 quartic Soft-Clip anchor C = x/(1+0.62x⁴)^¼ (no elementary closed
        // form — the OLD cubic F in softClipA railed at 1.5 and no longer matched the curve, the
        // Shapers.h lockstep trap). Simpson on a 8/1024 grid + linear tail at C's asymptote
        // 0.62^(−¼) ≈ 1.12634. Interp error ≪ the 1e-5 ADAA divide gate.
        {
            double F = 0.0; fcLut_[0] = 0.0;
            auto C = [] (double x) { const double x2 = x * x; return x / std::sqrt (std::sqrt (1.0 + 0.62 * x2 * x2)); };
            for (int i = 1; i <= 1024; ++i)
            {
                const double x0 = (i - 1) / 128.0, x1 = i / 128.0;
                F += (C (x0) + 4.0 * C (0.5 * (x0 + x1)) + C (x1)) * (x1 - x0) / 6.0;
                fcLut_[i] = (float) F;
            }
        }

        // Auto-gain: ~300 ms. NOT 10 ms — a fast tracker level-compensates the sag duck away and
        // makes the stateful modes measure and sound like static curves.
        autoA_ = (float) std::exp (-1.0 / (0.300 * fs_));

        // Param glide ~15 ms (the DelayEngine idiom): a hard knob jump or an LFO square can never burst.
        smth_  = (float) (1.0 - std::exp (-1.0 / (0.015 * fs_)));

        xfEdK_ = onePole2 (4000.0f);   // fb324 — transformer core-loss pole (2× rate)

        // fb325 — sag attack (~4 ms fixed; release = Recovery) and the input-envelope gate that
        // keeps every feedback path silent without input (2 ms attack / 250 ms release).
        cvXfI_ = (float) (1.0 / (0.040 * fs_));   // fb326 — 40 ms curve crossfade
        aAtk4_ = 1.0f - (float) std::exp (-1.0 / (fs_ * 0.004));
        dnDip_ = (float) std::exp (-1.0 / (fs_ * 0.0005));   // fb345 — char fade-down: 1→0.02 in ~2 ms
        aTpDg_ = 1.0f - (float) std::exp (-1.0 / (fs_ * 2.0 * 0.040));   // fb345 — tape drive glide
                 // (~40 ms at the 2× rate): a Drive yank .6→0 instantly multiplied the J-A output
                 // normalisation ×0.6a by ~360 while M was still hot ⇒ measured +21.7 dBFS blast.
                 // A 100 ms swept target measured clean, so 40 ms kills the blast inaudibly.
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
            for (int n = 0; n < kRB; ++n) { se_[c][n] = dry_[c][n] = 0.0f; }
            for (int n = 0; n < 2 * kRB; ++n) { xr_[c][n] = so_[c][n] = 0.0f; }   // fb342 — mirrored rings are 2·kRB
            xi_[c] = si_[c] = di_[c] = 0;
            fbZ_[c] = punchZ_[c] = fbDc_[c] = 0.0f;
            toneZ_[c] = 0.0f;
            slewZ_[c] = holdV_[c] = holdPh_[c] = smZ_[c] = ringA_[c] = 0.0f;
            octBx_[c] = octBy_[c] = 0.0f;                              // fb336 — Octave's blocker
            for (int n = 0; n < 8; ++n) clnZ_[c][n] = 0.0f;            // fb336/337 — Clean's poles
            for (int n = 0; n < 3; ++n) smZb_[c][n] = 0.0f;            // fb337 — Smooth's extra poles
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
            tpDrvG_ = -1.0f; wornPh_ = wornTg_ = 0.0f; wornNxt_ = 0.05f;   // fb345 (scalar; set twice, harmless)
            xfPhi_[c] = xfPhiP_[c] = xfB_[c] = xfEd_[c] = 0.0f;
            sbLp_[c] = sbTn_[c] = 0.0f;
            for (int s = 0; s < 3; ++s) odSl_[s][c] = odLp_[s][c] = 0.0f;
            bqX1_[c] = bqX2_[c] = bqY1_[c] = bqY2_[c] = 0.0f;
            hiCut2_[c] = hiCut3_[c] = inEnv_[c] = 0.0f;      // fb325
            sbFlip_[c] = 1.0f; sbPrev_[c] = sbEnv_[c] = sbLp2_[c] = fbLp_[c] = kpZ_[c] = 0.0f;
            shEnv_[c] = shEnv2_[c] = shGrl_[c] = shStk_[c] = 0.0f;   // fb326
        }
        drNxt_ = 1.0f; drS_ = 0.0f; emphP_ = 9.0f; wRotP_ = -1.0f; fbLpP_ = -1.0f;
        chrPend_ = chr_; dipT_ = 1.0f;   // fb345 — a flush cancels any armed char fade
        drive01_ = drv01T_;              // fb345 — snap the glided drive (no boot glide)
        // fb342 — pow-hoist caches (dslC_ embeds fs_, so a sample-rate change MUST re-key) + the
        // dcOff reference cache + the silence gate all reset with the rest of the state.
        gapP_ = dslP_ = rDzP_ = scKp_ = -1.0f; gapC_ = dslC_ = rDzC_ = scKc_ = 0.0f;
        for (int c = 0; c < 2; ++c) { dcModeP_[c] = -1; dcKeyP_[c] = 1.0e9f; dcPKeyP_[c] = 1.0e9f; dcPKey2P_[c] = 1.0e9f; dcOffC_[c] = 0.0f; }
        asleep_ = false; silN_ = 0; sleptN_ = 0;
        // fb326 — boot-bake the curve banks so the front table is never silence: bake lands in the
        // back, flip immediately (no fade at init), and mark caches clean-at-current-settings.
        cvMode_ = mode_; cvChr_ = chr_; cvPill_ = pill2_; cvSm_ = pT_[4]; cvMor_ = kneeT_;
        cvForce_ = false; cvChk_ = 64;
        bakeCurves();
        cvFront_ ^= 1; cvXf_ = 1.0f; cvPend_ = false;
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
    /** fb345 — a char switch swaps physics tables mid-note. The mode-switch dip (dip_=0) is an
        INSTANT wet→dry jump — measured as the click itself (Tape ratio 6.2, Tube 3.0) — and hot
        state vs new tables rings through its recovery (Xfmr pk 50.4 raw / 2.2 dipped). So chars
        DEFER: fade the wet down ~2 ms with the OLD tables, swap + re-seat state at the bottom,
        recover over the normal ~15 ms. Mode switches keep their shipped fb320 path. */
    void setCharacter (int c) noexcept
    {
        const int nc = juce::jlimit (0, 7, c);
        if (nc == chrPend_) return;                 // block-rate resolve re-sends the same value
        chrPend_ = nc;
        if (nc != chr_) dipT_ = 0.0f;               // arm the fade-down; the swap lands at the bottom
    }
    void applyPendingChar() noexcept                // runs at the dip bottom (control head)
    {
        if (chrPend_ != chr_)
        {
            chr_ = chrPend_;
            for (int ch = 0; ch < 2; ++ch)
            {   // re-seat the stateful circuits: zeroed state re-charges from the input under
                // the dip's cover instead of clamping hot state against the new tables.
                tapeM_[ch] = tapeH1_[ch] = tapeHd_[ch] = tapeGl_[ch] = 0.0f;
                xfPhi_[ch] = xfPhiP_[ch] = xfB_[ch] = xfEd_[ch] = 0.0f;
                tubeBlk_[ch] = tubeIgS_[ch] = 0.0f;
                bqX1_[ch] = bqX2_[ch] = bqY1_[ch] = bqY2_[ch] = 0.0f;
                holdV_[ch] = hold2V_[ch] = holdPh_[ch] = hold2Ph_[ch] = errZ_[ch] = 0.0f;   // fb345 —
                fbLp_[ch] = fbZ_[ch] = fbDc_[ch] = 0.0f;   //   DIGITAL clocks + the feedback loop too
            }                                              //   (Melt's hot loop rang through the fade)
            bqMode_ = -1; tubeCk_ = -1;             // force coeff/op-point recompute vs the new tables
        }
    }
    void setQuality   (int q) noexcept { qual_ = juce::jlimit (0, 3, q); }
    // fb337 — NO dip on tier switches: dip_=0 jumps the output to FULL DRY in one sample (the dip
    // is a wet-mute, not a fade) — measured as the 0.10 click the probe caught. The tier swap's own
    // discontinuity is tiny (phase-B duplicate ↔ real, ADAA ↔ naive converge) — measured directly.
    void setAuto      (bool b) noexcept { autoOn_ = b; }
    void setPill2     (bool b) noexcept { pill2_  = b; }
    /** fb336 — FOLD `Track`'s pitch input. Last-played note (mono assumption, documented: this is a
        post-mix FX bus — polyphonic pitch does not exist here; the newest note wins, like glide). */
    void setKeyHz     (float hz) noexcept { keyT_ = juce::jlimit (20.0f, 4200.0f, hz); }

    /** 0..1 -> driveDb = D_max * t^0.8, then linear. NEVER a `1 + amt*k` multiplier.
        ⚠️ FOLD is the one family with a DIFFERENT taper (bible §2.5): fold count is a FLOOR function
        of gain, so a dB taper staircases the timbre — FOLD gets `g = 1 + t·62`, LINEAR, so the fold
        count itself is linear in the knob. Call setMode() before setDrive() (the processor does). */
    void setDrive (float t01) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, t01);
        drv01T_ = t;                                       // fb345 — now a GLIDE TARGET: raw per-sample
                                                           // consumers (Diode threshold, Growl morph,
                                                           // SlewClip makeup ×(0.75+0.5d)) stepped on a
                                                           // knob yank — measured 4.3× click floor. The
                                                           // control head glides drive01_ at smth_.
        // ⚠️🔑 fb522 LANE D — DO NOT "FIX" THE DRIVE FLOOR HERE. THE CONSTRAINT IS ALGEBRAIC.
        // A lane proposed lowering the floor so that Drive 0 is transparent (it is not: at a -12 dBFS
        // engine input Hard Clip measures 8.04% THD at Drive 0). That change was BUILT AND MEASURED on
        // this header (scratchpad/laneD/lvl0.cpp) and REVERTED, because the two goals are mutually
        // exclusive under a fixed output trim, and the proof is one line of algebra:
        //
        //     let G = pre-shaper gain at Drive 0, T = the post-trim (see `y *= (1.0f / kPreGain)`).
        //     The shaper is bounded to +-1, so:
        //        unity LEVEL at Drive 0  <=>  G * T == 1        (the fb321 unity-headroom law)
        //        TRANSPARENCY at Drive 0 <=>  busPeak * G < 1   <=>  busPeak < T
        //
        //     Both hold only when the bus sits BELOW the output ceiling T. Today T = 1/kPreGain =
        //     -12.04 dBFS, and the engine is transparent at Drive 0 at every level up to about
        //     -14 dBFS (measured: THD 0.0004% at -14, 0.0018% at -12, 8.04% by -12 with the AU's
        //     default Knee 0.65 in play). THE ENGINE IS CORRECTLY CALIBRATED FOR ITS OWN STATED BUS
        //     REFERENCE (refLevel_ = 0.05 = -26.02 dBFS), where Drive 0 measures 0.0001% THD.
        //
        // So the 8% is NOT a fault in this file: the FX bus is presenting -12 to -15 dBFS where this
        // engine's design reference is -26 dBFS. Lowering the floor here buys transparency and PAYS
        // -12.08 dB of level the moment the device is switched on — measured, and exactly the
        // regression fb321 was created to prevent. The real repair is the bus staging (outside this
        // header) or a deliberate, preset-visible move of BOTH T and the floor together. Whoever takes
        // that on: move `1.0f / kPreGain` at the post-trim and this floor in ONE commit, or the
        // level jumps. That pair is a lockstep just like Shapers.h:13's kFoldPre.
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
        // ── fb342 — SILENCE SLEEP with hysteresis. Measured: silence cost the SAME as playing (the
        // resampler + trackers run regardless), so 2048 consecutive samples with input AND wet
        // output below 1e-9 park the SIGNAL PATH and return exact zero. Wake = the FIRST input
        // sample above 1e-12 (more sensitive than the sleep side — that asymmetry IS the
        // hysteresis). The detector needs BOTH sides quiet: input-only would sleep under a ringing
        // feedback/scream tail. ⚠️ The asleep gate sits BELOW the control head, NOT here — see the
        // review fix at the gate: everything from the glides to the bake cadence must keep running
        // at idle or the core viz freezes against a knob drag (the curves-must-move law).
        const float aIn = std::fabs (inL) + std::fabs (inR);

        // Per-sample glide on everything (no-clicks hard rule).
        driveC_ += (driveT_ - driveC_) * smth_;
        drive01_ += (drv01T_ - drive01_) * smth_;          // fb345 — see setDrive
        kneeC_  += (kneeT_  - kneeC_ ) * smth_;
        biasC_  += (biasT_  - biasC_ ) * smth_;
        toneC_  += (toneT_  - toneC_ ) * smth_;
        emphC_  += (emphT_  - emphC_ ) * smth_;
        loC_    += (loT_    - loC_   ) * smth_;
        hiC_    += (hiT_    - hiC_   ) * smth_;
        mixC_   += (mixT_   - mixC_  ) * smth_;
        if (dipT_ < 0.5f)                                    // fb345 — char-swap fade-down (~2 ms):
        {                                                    //   old tables render while the wet fades,
            dip_ *= dnDip_;                                  //   the swap + state re-seat land at the
            if (dip_ < 0.02f) { applyPendingChar(); dipT_ = 1.0f; }   // bottom, then normal recovery
        }
        else
            dip_ += (1.0f - dip_) * smth_;                   // fb320 — mode-switch wet dip recovery
        slamC_  += (slamT_  - slamC_ ) * smth_;              // fb324 — ANALOG Slam pill (+20 dB), click-free
        wrapC_  += (wrapT_  - wrapC_ ) * smth_;              // fb336 — the four family pills glide like Slam:
        octC_   += (octT_   - octC_  ) * smth_;              //         a toggle is a ~20 ms crossfade, never a click
        trkC_   += (trkT_   - trkC_  ) * smth_;
        clnC_   += (clnT_   - clnC_  ) * smth_;
        keyC_   += (keyT_   - keyC_  ) * smth_;
        for (int k = 0; k < 8; ++k) pC_[k] += (pT_[k] - pC_[k]) * smth_;
        if (++occN_ >= 512) { occN_ = 0; for (int k = 0; k < 48; ++k) occ_[k] *= 0.85f; }   // fb328 — occupancy decay (~65 ms)

        // ── PER-FAMILY SLOT RESOLVE (bible §5.5) — the same 8 raw params mean different things ────
        //   emphasis: P3 everywhere EXCEPT DIGITAL (its P3 is the second destruction axis)
        //   knee/bias/asym: SIG is Knee only for CLIP; DIODE's knee is P5 and its SIG is Asym;
        //   FOLD's SIG is Symmetry, which IS the bias axis (DC pre-fold, removed after);
        //   SHAPER's bias is P6; ANALOG's SIG is Bias (voiced in Phase C).
        const Family fam = family();
        emphC_ = (fam == FAM_DIGITAL) ? 0.0f : (pC_[2] * 2.0f - 1.0f);
        switch (fam)
        {
            case FAM_CLIP:    kneeE_ = kneeC_;
                              // fb345 — GRID-LEAK BIAS (the DIODE law, CLIP edition): the absolute
                              // ±1 offset was ±dEff ≈ 5× the driven swing ⇒ DC-latch SILENCE past
                              // ~±20% travel — and FOUR factory presets (authored Bias 65–72 for
                              // warmth) measured −93 dBFS. Envelope-tracked: full knob = 0.9× the
                              // swing (ZS 1.1 — just past full duty) at ANY level. Never silent.
                              biasE_ = (pC_[4] * 2.0f - 1.0f)
                                     * juce::jmax (inEnv_[0], inEnv_[1]) * kPreGain
                                     * ((mode_ == ZeroSquare) ? 1.1f : 0.9f);
                              asymE_ = 0.0f;
                              wrapT_ = pill2_ ? 1.0f : 0.0f;                   /* fb336 — Wrap */   break;
            // fb322 — the ASYM MODE's offset goes PRE-DRIVE via the bias path (the fb315 lesson,
            // relearned: a post-drive offset is swamped by the driven signal and the knob reads dead).
            case FAM_DIODE:   kneeE_ = pC_[4];
                              asymE_ = kneeC_ * 2.0f - 1.0f;
                              // Asym = full bias injection. Diode 1/2 = partial (mismatched diodes
                              // shift the operating point too — and measured, rail-HEIGHT asymmetry
                              // alone gives only DC + odd harmonics; even harmonics need the DUTY
                              // shift, which the bias provides). Rectify: NONE — its SIG is the
                              // rectification amount, not an offset.
                              // fb345 — GRID-LEAK BIAS: the old absolute offsets (0.8/0.3, pre-dEff)
                              // were 1.5–4× the nominal driven swing ⇒ the wave went fully one-sided,
                              // the flat threshold clamped it to a CONSTANT, the DC blocker erased it:
                              // measured DEAD-SILENT at |SIG|>25% (Asym) / >85% (D1) at drive .5.
                              // A real circuit self-biases ∝ signal (grid-leak/auto-bias): the offset
                              // tracks the input envelope, so full knob = maximal duty-shift at ANY
                              // level and the quiet tip always keeps conducting.
                              biasE_ = ((mode_ == Asym)    ? asymE_ * 0.90f
                                      : (mode_ == Rectify) ? 0.0f
                                                           : asymE_ * 0.45f)
                                     * juce::jmax (inEnv_[0], inEnv_[1]) * kPreGain;
                              octT_  = pill2_ ? 1.0f : 0.0f;                   /* fb336 — Octave */ break;
            case FAM_FOLD:
            {
                kneeE_ = 0.5f;
                // fb345 — grid-leak law, FOLD edition: the static ×1.2 was 6× the pre-dEff swing
                // (preset 'Howl' measured −93 dBFS). Env-tracked: full = 1.1× swing — the fold
                // eats a full extra reflection at the top, never a latched constant.
                biasE_ = (kneeC_ * 2.0f - 1.0f)
                       * juce::jmax (inEnv_[0], inEnv_[1]) * kPreGain * 1.1f;
                asymE_ = 0.0f;
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
                trkT_ = pill2_ ? 1.0f : 0.0f;                                  /* fb336 — Track */
                break;
            }
            case FAM_SHAPER:  kneeE_ = 0.5f;    biasE_ = pC_[5] * 2.0f - 1.0f; asymE_ = 0.0f;
                              shaperCheckBake();                       // fb326 — dirty check @64-sample cadence
                              if (cvXf_ < 1.0f)
                              {
                                  cvXf_ = juce::jmin (1.0f, cvXf_ + cvXfI_);
                                  if (cvXf_ >= 1.0f && cvPend_) { cvFront_ ^= 1; cvPend_ = false; }
                              }
                              break;
            // fb324 — ANALOG: SIG = Bias (bipolar, consumed per-circuit in its own physical units);
            // Slam = Decapitator's Punish, +20 dB of pre-gain ON TOP of Drive (the ×10 target glides).
            case FAM_ANALOG:  kneeE_ = 0.5f;    biasE_ = kneeC_ * 2.0f - 1.0f; asymE_ = 0.0f;
                              slamT_ = pill2_ ? 10.0f : 1.0f;                                     break;
            default:          kneeE_ = 0.0f;    biasE_ = 0.0f;                 asymE_ = 0.0f;
                              clnT_  = pill2_ ? 1.0f : 0.0f;                   /* fb336 — Clean (default = DIGITAL) */ break;
        }

        // fb337 — QUALITY LIVE (§3.7/3.8): Off/Standard/High/Ultra. AUTO-PROMOTION (one tier, free —
        // latency never changes): razor knee · hot Snarl · Drive>80 · Octave lit · Beyond≥50. Never
        // FROM Off — Off is the deliberate lo-fi escape hatch and stays exactly what it says.
        {
            int eq = qual_;
            if (eq >= 1 && eq < 3)
            {
                const bool promo = drive01_ > 0.80f
                                || (fam == FAM_CLIP && kneeE_ < 0.15f)
                                || octC_ > 0.5f
                                || ((fam == FAM_CLIP || fam == FAM_DIODE) && pC_[7] > 0.30f)
                                || (fam == FAM_SHAPER && pC_[6] >= 0.50f);
                if (promo) ++eq;
            }
            eqEff_ = eq;
        }

        // fb325 — emphasis gain, dB-symmetric (see one(); cached: exp only when the knob moves)
        if (std::fabs (emphC_ - emphP_) > 1.0e-4f) { emphP_ = emphC_; emphG_ = std::exp (2.0723f * emphC_); }

        // fb342 — Rectify's dz taper hoisted to the knob edge (was a pow PER PHASE PER CHANNEL in
        // shapeM). The recompute lives HERE, not in shapeM: the c=−1 reference path must stay
        // write-free (the fb325 stateless-reference law — a cache write from sampleCurve's message-
        // thread call would race the audio thread). shapeM only reads rDzC_; dcOff and shapeS see
        // the same value within a sample, so this is bit-equivalent to the inline pow.
        if (mode_ == Rectify && std::fabs (pC_[5] - rDzP_) > 1.0e-6f)
        { rDzP_ = pC_[5]; rDzC_ = std::pow (pC_[5], 1.8f) * 2.6f; }

        // ── fb342 (review fix) — THE ASLEEP GATE SITS BELOW THE CONTROL HEAD. Everything above —
        // the glides, the family-slot resolve, quality promotion, the bake cadence, occ decay, the
        // cached knob-edge resolves — keeps running at idle, so sampleCurve()'s picture tracks a
        // knob drag and a family switch WHILE SILENT, and a curve sent from the card bakes at idle
        // (curves-must-move law: the first cut returned before the glides and froze the core viz
        // at its pre-sleep shape until a note woke the engine). Only the SIGNAL path below is
        // skipped: resampler, shapers, trackers, blockers. ~9 ns idle instead of ~4 — the price
        // of a live picture. wake() consequently advances ONLY the signal trackers: the glides and
        // bake state advanced themselves here, identically to a never-slept engine, BY CONSTRUCTION.
        if (asleep_)
        {
            if (aIn <= 1.0e-12f) { ++sleptN_; outL = 0.0f; outR = 0.0f; return; }
            wake();
        }

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

        // fb342 — the sleep detector (see the top of this function). Post-auto output on purpose:
        // whatever leaves the engine is what must be provably silent before the body is skipped.
        if (aIn < 1.0e-9f && std::fabs (outL) + std::fabs (outR) < 1.0e-9f)
        {
            if (++silN_ >= 2048) { asleep_ = true; silN_ = 0; sleptN_ = 0; }
        }
        else silN_ = 0;
    }

private:
    // ── fb342 — the WAKE path: closed-form advance ONLY for the SIGNAL trackers the asleep gate
    // skips. The control head (glides, family resolve, bake cadence, occ decay) ran on every idle
    // sample, so all of THAT state is bit-identical to a never-slept engine's by construction —
    // the review fix retired the old glide closed-form here, and with it the whole ulp-stall class
    // it needed guarding against. Each remaining closed form is the silent-input recursion solved
    // exactly (signal-independent at zero input ⇒ click-free by construction). Deep circuit state
    // (J-A magnetisation, transformer flux, grid-blocking, slew/od stages, drift walks) holds
    // frozen: J-A does not move at H≈0, and the rest re-converge under signal inside their own
    // time constants (measured, fb342 harness).
    void wake() noexcept
    {
        const double S = (double) sleptN_;
        asleep_ = false; silN_ = 0; sleptN_ = 0;
        if (S <= 0.0) return;
        // the two mandated long-memory trackers + the cheap exact release pair (pure decays at zero
        // input: mag/|in0| ≈ 0 ⇒ z *= (1−coef) per sample ⇒ one pow each)
        autoZ_ *= (float) std::pow ((double) autoA_, S);
        const float er = (float) std::pow ((double) (1.0f - envR_),     S);
        const float pr = (float) std::pow ((double) (1.0f - kPunchRel), S);
        for (int c = 0; c < 2; ++c) { inEnv_[c] *= er; punchZ_[c] *= pr; }
        // SAG: the silent-input fixed point is sagIn_, NOT zero — an ANALOG mode with standing bias
        // (Stomp's diode current, Tape's remanence) keeps the reservoir CHARGED at rest, and the
        // frozen sagIn_ is exactly what a never-slept engine would keep feeding (it is a pure
        // function of the silent signal + params). Releasing to 0 here measured a 1.5× wake spike
        // on Stomp (rail springs open, then re-sags in 4 ms) — the harness bug that almost shipped.
        for (int c = 0; c < 2; ++c)
        {
            const float k = (sagIn_[c] > sagV_[c]) ? aAtk4_ : aRecC_;   // monotone approach: one regime
            sagV_[c] = sagIn_[c] + (sagV_[c] - sagIn_[c]) * (float) std::pow ((double) (1.0f - k), S);
        }
    }

    struct AdaaState { float x1 = 0.0f, F1 = 0.0f, kf = -1.0f; bool have = false; };
    // fb345 — kf = the curve-param key F1 was computed under. A gliding Knee made
    // y=(F_new(x0)−F_old(x1))/dx divide a parameter-induced F-mismatch by a small dx —
    // measured 0.568 spikes (16.6× the steady floor) while turning Knee at an ADAA tier.

    // ── the per-channel chain ────────────────────────────────────────────────
    float one (float x, int c, float drv) noexcept
    {
        const float in0 = x;                 // untouched input, for the latency-aligned dry
        if (family() == FAM_DIGITAL) return digitalOne (x, c, drv);   // 1×, L/R, no resampler (fb320)
        // fb325 — NOTHING SOUNDS WITHOUT INPUT (hard rule, Max: Stomp Snarl was a free-running 808).
        // Every self-oscillating feedback path is gated by the input envelope (2 ms / 250 ms): the
        // scream rings and DIES with the note — it follows what you play, never runs on its own.
        inEnv_[c] += (std::fabs (in0) - inEnv_[c]) * ((std::fabs (in0) > inEnv_[c]) ? envA_ : envR_);
        const float fbGate0 = juce::jmin (1.0f, inEnv_[c] * 30.0f);
        const float fbGate  = fbGate0 * fbGate0;   // fb345 — SQUARED release: the AC-coupled loop
                                                   //   genuinely oscillates now (the old DC latch
                                                   //   fell silent by pathology) and rang ~0.9 s
                                                   //   post-note; squared = ~0.4 s (the fb325 law).
                                                   //   1 while playing — the live scream unchanged.
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
        // 🔑 CALIBRATION — READ kPreGain'S DEFINITION (bottom of this file), NOT THIS COMMENT, FOR THE
        // LIVE VALUE. It is 4.0f. An older revision of this block argued for 8.0f and was left behind
        // when fb321 lowered it 8.0 -> 4.0 on Max's field report ("I can't turn the drive up to 5-10%
        // without it sounding like 100%"). That stale text cost a later measurement lane a proposal to
        // set kPreGain to 0.25 — a -24 dB error derived from a baseline that had not existed for
        // months. The number lives at exactly one site; this comment explains it and never restates it.
        //
        // 🔑 WHERE THE PRE-SHAPER GAIN ACTUALLY COMES FROM (fb522 lane D, measured on this header via
        // scratchpad/laneD/probe1.cpp — the offline harness compiles THIS FILE verbatim, fb283 law):
        //     total pre-shaper gain (dB) = 20*log10(kPreGain) + driveDb(t)
        // With Knee = 0 (exact razor, kneePre inactive) the clipping onset is a closed form: the rail
        // is touched when inputPeak * kPreGain * driveGain = 1. At Drive 0 that predicts an onset at
        // -20*log10(kPreGain) = -12.04 dBFS, and the harness measures the engine transparent (gain
        // -0.04 dB, THD 0.002%) at -12.00 dBFS and clipping hard by -6.00 dBFS. Model and measurement
        // agree to 0.04 dB: THERE IS NO UNACCOUNTED GAIN STAGE IN THIS PATH. inEnv_ is NOT an input
        // normaliser — it scales biasE_ and gates feedback only (see the fb345 grid-leak notes above)
        // — and the real auto-gain (autoZ_/autoOn_) is OFF by default and sits AFTER the shaper.
        //
        // ⚠️ MEASURE AT THE ENGINE'S INPUT NODE, NOT AT THE PLUGIN'S OUTPUT. Between this engine and
        // the plugin output sits the master chain including Output Gain (-12..+12 dB). A lane that
        // level-matched on the plugin's BYPASSED OUTPUT peak with Output Gain at max reported the
        // engine as ~30 dB hotter than Serum's; measured, the engine's own input sat ~6.5 dB BELOW the
        // output peak it was matched on, and the remainder was the drive TAPER (see setDrive), not a
        // constant. Two different quantities, one number — do not fold them together again.
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
        // fb342 — VECTORIZABLE POLYPHASE DOT. The old form (Σ h[2j+1]·xr[(xi−j)&kRM], one
        // accumulator) is a modular gather + a serial reduction — clang cannot vectorise either.
        // The ring is MIRRORED (every sample written at p AND p+kRB) so ANY 64-tap window is one
        // contiguous ascending-time block; the odd taps are pre-REVERSED (hoU_, prepare()) so the
        // dot is a straight a[j]·b[j]; 4 partial accumulators break the reduction chain for NEON
        // FMA. Same 129 coefficients, same latency — only the summation ORDER differs (float-
        // reassociation class, gated < 0.1 dB by the fb342 equivalence run).
        float* xr = xr_[c]; int& xi = xi_[c];
        { const int p = xi & kRM; xr[p] = x; xr[p + kRB] = x; }
        const float pA = xr[((xi - kC / 2) & kRM) + kRB];               // even branch = PURE DELAY
        float pB;
        {
            const float* win = &xr[(xi & kRM) + kRB - (kNH - 1)];       // win[j] = x[n − (kNH−1−j)]
            float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
            for (int j = 0; j < kNH; j += 4)
            {
                s0 += hoU_[j]     * win[j];
                s1 += hoU_[j + 1] * win[j + 1];
                s2 += hoU_[j + 2] * win[j + 2];
                s3 += hoU_[j + 3] * win[j + 3];
            }
            pB = ((s0 + s1) + (s2 + s3)) * 2.0f;                        // zero-stuff gain
        }
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

        // ── fb336 — FOLD `Track` (bible §898): fold-depth KEY TRACKING — pre-gain × (110/f0)^0.7,
        // so the 32-fold patch that is glorious on a C1 bass folds gently instead of hashing on a
        // C6 lead. Also a free aliasing control: cuts the worst-case corner rate ~4× up top.
        // Pitch = the last-played note (setKeyHz; mono law — a post-mix bus has no polyphonic f0).
        if (fam == FAM_FOLD && trkC_ > 1.0e-4f)
        {
            if (std::fabs (keyC_ - keyP_) > 0.5f)
            { keyP_ = keyC_; trkG_ = juce::jlimit (0.08f, 2.5f, std::pow (110.0f / juce::jmax (20.0f, keyC_), 0.7f)); }
            dEff *= 1.0f + trkC_ * (trkG_ - 1.0f);
        }

        // ── GAP (CLIP P6) — class-B crossover dead zone BEFORE the shaper, input-referred: distorts
        // the QUIET parts; at max anything below ~−6 dBFS is silence and notes tear into fragments.
        // fb325 — power-1.6 taper (no-plateau law): linear reached total gating by ~60 % and the top
        // of the knob measured frozen. The curve now keeps eating deeper across the whole travel.
        // fb342 — taper pow hoisted to the knob edge (the emphG_/fbLpP_ idiom): pC_[5] glides, so
        // the 1e-6 gate recomputes only WHILE the knob moves; both channels share the one cache.
        float gap = 0.0f;
        if (fam == FAM_CLIP)
        {
            if (std::fabs (pC_[5] - gapP_) > 1.0e-6f) { gapP_ = pC_[5]; gapC_ = std::pow (pC_[5], 1.6f) * 0.45f; }
            gap = gapC_;
        }
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
        // ── fb336 — CLIP `Wrap` (bible §896): instead of HOLDING at the rail, the driven value
        // TELEPORTS to the opposite rail (y = 2·frac((u+1)/2) − 1). Identity for |v| ≤ 1 by
        // construction, so the glided blend only ever touches beyond-rail content — the toggle's
        // ~20 ms fade is silent on clean signal, and at Drive 100 the wrapped hard clip is a
        // modulo-sawtooth of the input: a full-band scream that still tracks pitch.
        // fb337 — the QUALITY tiers inside the (structurally constant) resampler skeleton:
        //   Off      = 1×: phase B duplicates A — same delays, same latency, raw aliasing on purpose.
        //   Standard = 2× naive (the shipped fb318 path).
        //   High/Ultra = 2× + ADAA-1 inside the OS region, WHITELIST-gated: only modes whose anti()
        //   provably matches today's curve (SoftClip w/ the LUT F · HardClip on the razor knee half,
        //   kneePre inactive there · ZeroSquare incl. pedestal+gate F). Wrap lit ⇒ ADAA off (F of a
        //   wrapped map is not F — the §1080 trap). Everything else: naive 2×, floors intact.
        const bool os2 = (eqEff_ >= 1);
        const float wrapA = (fam == FAM_CLIP) ? wrapC_ : 0.0f;
        auto wrapR = [wrapA] (float v) noexcept -> float
        {
            if (wrapA <= 1.0e-4f) return v;
            float w = (v + 1.0f) * 0.5f; w -= std::floor (w);
            return v + wrapA * ((w * 2.0f - 1.0f) - v);
        };
        // fb342 — the stateless DC reference is a pure function of its inputs, so it only moves when
        // one of them does: cached PER CHANNEL, keyed on EVERYTHING shapeM(c=−1) can read on ANY
        // mode/character path (the fb331 shared-default-audit discipline applied to a cache key):
        //   · mode_/chr_/cvFront_ (curve selection + the bake front flip)
        //   · biasE_·dEff — the argument itself. Also covers Width's per-channel dM/dS split and
        //     Punch/Track per-sample dEff modulation: when those move, the key moves per sample and
        //     the cache degrades to exactly today's per-sample recompute. Correct by construction.
        //   · kneeE_ (knees/pedestals) · kneeC_ (SHAPER Morph, DIODE asymE_, ShAsym 'Halves')
        //   · drive01_ (Diode1 falling threshold) · driveC_ (SineFold chr2 reads it RAW)
        //   · wrapA/dzU/gap (the domain pre-maps) · pC_[3..6] (foldIter ladder, SineFold spacing/
        //     stages/corner, Rectify dz, Diode2 gap, SHAPER Beyond)
        //   · cvXf_ (a 40 ms bake fade moves the key per sample, so the reference tracks the blend
        //     exactly; the completed flip re-keys via cvFront_ above).
        // zsGate_ earns no slot: at biasE_=0 the argument is 0 (gate outcome moot); otherwise it is
        // a pure function of dEff = key/biasE_ with biasE_ itself derived from pC_[4]. PER CHANNEL
        // is load-bearing: Width drives M and S at different dEff (a shared cache measured 100%
        // misses). ANALOG stays out (reference is identically 0 there). The float terms are hashed
        // as TWO checksums — flat and integer-weighted — compared under the same 1e-6 gate, because
        // a single flat sum aliases compensating knob pairs (see pKey2 below).
        float dcOff = 0.0f;
        if (fam != FAM_ANALOG)
        {
            const float aKey = ((fam == FAM_SHAPER) ? 0.0f : biasE_) * dEff
                             + ((fam == FAM_SHAPER) ? biasE_ : 0.0f);   // fb345 — SHAPER bias moved
                                                                        //   into shaperCore (domain
                                                                        //   units); key still tracks it
            const float pKey = kneeE_ + kneeC_ + drive01_ + driveC_ + wrapA + dzU + gap + cvXf_
                             + pC_[3] + pC_[4] + pC_[5] + pC_[6];
            // fb342 (review fix) — a SECOND, differently-weighted checksum over the SAME terms: a
            // flat sum aliases COMPENSATING pairs (FOLD Stages +δ with Spacing −δ verified to sit
            // inside the 1e-6 gate while the true reference moved — a standing DC error into the
            // 10 Hz blocker). A flat-sum cancel means Δa = −Δb, which shifts the weighted sum by
            // Δa·(wa − wb) ≠ 0 — distinct integer weights break every pairwise alias for one extra
            // fmadd chain.
            const float pKey2 = kneeE_ + 2.0f * kneeC_ + 3.0f * drive01_ + 4.0f * driveC_
                              + 5.0f * wrapA + 6.0f * dzU + 7.0f * gap + 8.0f * cvXf_
                              + 9.0f * pC_[3] + 10.0f * pC_[4] + 11.0f * pC_[5] + 12.0f * pC_[6];
            const int   iKey = mode_ + (chr_ << 5) + (cvFront_ << 8);
            if (dcModeP_[c] != iKey
                || std::fabs (aKey - dcKeyP_[c]) > 1.0e-6f || std::fabs (pKey - dcPKeyP_[c]) > 1.0e-6f
                || std::fabs (pKey2 - dcPKey2P_[c]) > 1.0e-6f)
            {
                dcModeP_[c] = iKey; dcKeyP_[c] = aKey; dcPKeyP_[c] = pKey; dcPKey2P_[c] = pKey2;
                dcOffC_[c] = shapeM (dzPost (wrapR (aKey)), -1);
            }
            dcOff = dcOffC_[c];
        }
        const float fbIn  = (fbA > 1.0e-5f) ? fbA * (fbZ_[c] - fbDc_[c]) : 0.0f;
        // fb345 — EVERY fbA loop is AC-COUPLED (the Fuzz Face coupling cap): the raw loop finds a
        // DC fixed point against any constant-capable curve — DIODE Snarl>0.2 measured −122 dB, and
        // presets 'Sludge' (OD Slew Kill + snarl) and 'Scorch' (Soft Growl + feedback) measured
        // SILENT. A DC-less loop can't hold the latch ⇒ it screams instead. Screams are AC ⇒ pass.
        // ── fb336 — DIODE `Octave` (bible §897): full-wave rectifier + its OWN DC blocker BEFORE
        // the mode's shaper, inside the 2× region (the blocker advances at 2fs, A then B
        // chronological). The mid-chain re-centre is exactly why Rectify+Octave lands at 4·f0 —
        // the second rectification is not idempotent. Diode 1 + Octave = an Octavia.
        float pA2 = pA, pB2 = pB;
        if (fam == FAM_DIODE && octC_ > 1.0e-4f)
        {
            float r  = std::fabs (pA2);
            float d2 = r - octBx_[c] + dcR2_ * octBy_[c]; octBx_[c] = r; octBy_[c] = d2;
            pA2 += octC_ * (2.0f * d2 - pA2);
            r  = std::fabs (pB2);
            d2 = r - octBx_[c] + dcR2_ * octBy_[c]; octBx_[c] = r; octBy_[c] = d2;
            pB2 += octC_ * (2.0f * d2 - pB2);
        }
        // A then B = chronological at the 2× rate, so stateful shapers (Slew Clip, DIODE slew) are exact.
        float yA, yB;
        if (fam == FAM_ANALOG)
        {
            // RAW into the circuit — no ×dEff, no shared bias/deadzone: each ANALOG mode applies
            // Drive and Bias in its OWN physical slot (grid volts, the drive→a pinning map, flux,
            // op-amp gain, cascade gain). The exact DC reference is impossible for a moving operating
            // point — the circuits self-centre (quiescent subtraction / differentiator / re-centre)
            // and the 10 Hz blocker takes the residue. Slam = +20 dB of pre-gain (Punish).
            const float vA = pA * kPreGain * slamC_ + fbIn;
            yA = shapeS (vA, c);
            yB = os2 ? shapeS (pB * kPreGain * slamC_ + fbIn, c) : yA;   // fb337 — Off = 1× (phase B duplicates A)
            if (c == 0) occAdd (vA);                     // fb328 — occupancy viz (driven axis)
        }
        else
        {
            adaaOn_ = eqEff_ >= 2 && wrapA <= 1.0e-4f
                   && chr_ == 0                          // fb345 — the F-tables are CHARACTER-BLIND: with
                                                         //   promo/High active, Rails/WrapTip/Modulo/Duty/…
                                                         //   all rendered as char 0 (measured 0.00 dB apart).
                                                         //   Chars ≠ 0 keep naive-2× — voicing beats ADAA-1.
                   && (mode_ == SoftClip || mode_ == ZeroSquare
                       || (mode_ == HardClip && kneeE_ * 2.0f - 1.0f < 1.0e-3f));   // fb337 — the lockstep whitelist
            const float bInj = (fam == FAM_SHAPER) ? 0.0f : biasE_;   // fb345 — SHAPER bias is domain-side (shaperCore)
            const float vA = wrapR ((deadzone (pA2 * kPreGain) + bInj + fbIn) * dEff);   // fb336 — Wrap folds the DRIVEN value
            yA = (adaaOn_ ? adaa (dzPost (vA), c) : shapeS (dzPost (vA), c)) - dcOff;
            if (! adaaOn_) { st_[c].x1 = dzPost (vA); st_[c].have = false; }   // fb345 — keep x1 fresh so a
                                                         //   promo/whitelist toggle re-enters ADAA on the REAL
                                                         //   previous sample (seamless), not a stale one
            if (os2)
            {
                const float vB = wrapR ((deadzone (pB2 * kPreGain) + bInj + fbIn) * dEff);
                yB = (adaaOn_ ? adaa (dzPost (vB), c) : shapeS (dzPost (vB), c)) - dcOff;
                if (! adaaOn_) { st_[c].x1 = dzPost (vB); st_[c].have = false; }
            }
            else yB = yA;                                // fb337 — Off = 1×
            if (c == 0) occAdd (vA);                     // fb328 — occupancy viz (driven axis)
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
            // fb342 — pow hoisted (moved-knob cache; the fs_ factor is baked in, flush() re-keys it)
            // ── fb522 lane D — THE EXPONENT IS SQUARED. Measured, the old `0.0028^p7` put the shared
            // 0.5 default at sU = 0.0185, which does NOT "graze the razor tips" (the line above): it
            // clamps the whole waveform. A slew limiter driven past its ceiling emits a fixed-slope
            // triangle whose shape is set by sU ALONE, so Drive stops mattering entirely. That is the
            // measured "Diode 2's knob runs BACKWARDS" AND the measured "Diode 2 is too dull" — one
            // bug, two symptoms (scratchpad/laneD/d2b.cpp, d2c.cpp, sine 250 Hz, -15 dBFS in):
            //     P7 0.00 : THD 18.0 -> 32.2 -> 44.7 -> 48.0 % across Drive, bw99 4250 Hz  (healthy)
            //     P7 0.50 : THD 18.0 -> 8.7 -> 8.3 -> 8.65 % — FALLS, then frozen; bw99  250 Hz
            // Squaring the exponent moves the middle of the knob and PINS BOTH ENDS: p7 = 0 still
            // gives 0.35 (off) and p7 = 1 still gives the identical 0.00098 sludge floor, so the
            // no-playing-safe ceiling is untouched and only the default's position changes. The
            // shared 0.5 default now lands at sU = 0.0805, i.e. where the fb325 comment always said
            // it should be — edges rounded, body intact.
            // ⚠️ KNOWN AND MEASURED, NOT FIXED HERE: above p7 ~ 0.8 the tone freezes (THD 10.67 %,
            // bw99 500 Hz at every point) and only the LEVEL keeps falling, to -17.7 dB at p7 = 1.
            // That is what a slew limiter does once it owns the waveform, and un-plateauing it means
            // giving the ceiling a signal-tracking term (the fb345 grid-leak treatment) — a bigger,
            // preset-visible change than this lane can certify without a build.
            if (std::fabs (pC_[6] - dslP_) > 1.0e-6f)
            { dslP_ = pC_[6]; dslC_ = 0.35f * std::pow (0.0028f, pC_[6] * pC_[6]) * (48000.0f / (float) fs_); }
            const float sU = dslC_;
            yA = slewZ_[c] + juce::jlimit (-sU, sU, yA - slewZ_[c]); slewZ_[c] = yA;
            if (os2) { yB = slewZ_[c] + juce::jlimit (-sU, sU, yB - slewZ_[c]); slewZ_[c] = yB; }
            else       yB = yA;                          // fb337 — Off = 1×: the slew state advances once per base sample
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
            fbDc_[c] = flushDenorm (fbDc_[c] + (fbZ_[c] - fbDc_[c]) * 0.0013f);   // fb345 — the
        }                                                    //   loop's coupling cap (~20 Hz), all fams

        float* se = se_[c]; float* so = so_[c]; int& si = si_[c];
        se[si & kRM] = yA;                                              // even ring: ONE delayed read — stays plain
        { const int p = si & kRM; so[p] = yB; so[p + kRB] = yB; }       // odd ring MIRRORED (fb342 — see the upsampler)
        float y = hb_[kC] * se[(si - kC / 2) & kRM];
        // ⚠️ the window base is (si − 1), NOT si: s[2n−2j−1] sits at an ODD index ⇒ sOdd[n−j−1] —
        // the fb318 one-early trap, carried into the contiguous form (win[j] = sOdd[n−1−(kNH−1−j)]).
        {
            const float* win = &so[((si - 1) & kRM) + kRB - (kNH - 1)];
            float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
            for (int j = 0; j < kNH; j += 4)
            {
                s0 += hoU_[j]     * win[j];
                s1 += hoU_[j + 1] * win[j + 1];
                s2 += hoU_[j + 2] * win[j + 2];
                s3 += hoU_[j + 3] * win[j + 3];
            }
            y += (s0 + s1) + (s2 + s3);
        }
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
        // fb337 — the old ">0.9 big-jump -> naive" escape is GONE: at working drive |dx| exceeds it
        // every cycle, so the kernel ping-ponged naive↔ADAA — measured as High LOSING 5 dB to
        // Standard (the escape was itself a modulator). Averaging across the big jump IS the job.
        else
        {
            const float kf = kneeE_ + zsGate_;               // fb345 — the F-relevant params this sample
            const float F = anti (u);
            const float Fp = (s.have && s.kf == kf) ? s.F1 : anti (s.x1);   // stale-curve F1 → recompute:
            y = (F - Fp) / dx;                               // both F's on ONE curve ⇒ y = f(ξ), bounded
            s.F1 = F; s.kf = kf; s.have = true;
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
            case Shaper: case ShaperAsym: case Harmonics: case Table:
                return shaperCore (u * kShDom, -1);        // fb326 — stateless DC reference

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
                const float dz = rDzC_;   // fb325 taper (full-travel) — fb342: baked in processSample
                                          // (this path must stay WRITE-free — the c=−1 reference law)
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
                // fb345 — Rebound was UNREAD by the closed-form sine fold (measured profD 0.00 at
                // Corner 0 — a dead knob whenever Corner sits low). It now PHASE-WARPS the fold
                // (skewed tip densities — a real folder morph); identity at knob 0, BIBO by sin().
                float yv = std::sin (1.5707963f * v + pC_[5] * 0.9f * std::sin (3.1415927f * v));
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

        case Shaper: case ShaperAsym: case Harmonics: case Table:
            return shaperF (u, c);                         // fb326 — PHASE D: the curve IS data

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
                case 1:                                                       // Duty — a THIN pulse: the flip point rides
                {                                                             //   the signal's own peak (~27% duty at any
                                                                              //   level), so it reads nasal-thin against
                                                                              //   the Comparator's square AT DEFAULT.
                                                                              //   (fb345 — the old th=-bias·drive·2 was
                                                                              //   the same crossing as chr 0: measured
                                                                              //   0.00 dB apart at every bias.)
                    if (c >= 0) zsEnv_[c] += (juce::jmin (2.5f, std::fabs (u)) - zsEnv_[c])
                                           * (std::fabs (u) > zsEnv_[c] ? 0.01f : 0.0004f);
                    const float th = 0.62f * ((c >= 0) ? zsEnv_[c] : 1.0f);   // c<0 = stateless DC ref
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
            // fb342 — pow hoisted (kneeE_ glides ⇒ recompute only while the Knee moves; shapeS is
            // audio-thread-only, so the cache write is race-free — sampleCurve never calls shapeS)
            if (std::fabs (kneeE_ - scKp_) > 1.0e-6f)
            { scKp_ = kneeE_; scKc_ = 0.0005f * std::pow (60.0f, kneeE_) * kPreGain * 0.5f; }
            float sU = scKc_;
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
                case 5: break;                                               // Ring — deficit-charged EXIT overshoot (below)
                case 6:                                                       // Bounce — the ceiling rides a 40 ms envelope:
                {                                                             //   loud passages slew FASTER (alive on hard playing)
                    env40_[c] += (juce::jmin (1.0f, std::fabs (u) * 0.5f) - env40_[c]) * 0.0005f;
                    sU *= 1.0f + 2.5f * env40_[c]; break;
                }
                case 7: sU *= (c == 1) ? 1.35f : 0.78f; break;               // Stereo — complementary ceiling skew.
                    // fb345 — the old "+15% on c==1 only" was a NO-OP on mono content: c1 is the
                    // SIDE under the family's M/S topology (silent for mono) — measured R−L 0.00 dB.
                    // Complementary: mono hears the darker M ceiling; Width>50 (L/R domain) hears
                    // the true left/right skew the bible describes.
            }
            float d2 = u - slewZ_[c];
            if (chr_ == 2 && d2 < 0.0f) d2 = juce::jmax (d2 * 3.0f, d2);     // Asym: fall ceiling 3× (evens, level-dependent)
            const bool wasLim = std::fabs (d2) > sU;
            float y = slewZ_[c] + juce::jlimit (-sU * (chr_ == 2 && d2 < 0.0f ? 3.0f : 1.0f), sU * (chr_ == 2 && d2 < 0.0f ? 3.0f : 1.0f), d2);
            if (chr_ == 5)
            {   // fb345 — the one-sample d2·0.6 zing measured 0.00 dB vs Charge (too timid to hear).
                // Real op-amp recovery OVERSHOOTS by the accumulated deficit: charge while limited,
                // dump on exit — spiky corners, a bright nasty ring that scales with how hard it hit.
                if (wasLim) ringA_[c] = juce::jlimit (-0.6f, 0.6f, ringA_[c] + d2 * 0.02f);
                else if (slewLim_[c] > 0.5f) { y += ringA_[c]; ringA_[c] = 0.0f; }
                else ringA_[c] = 0.0f;
            }
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
        // fb345 — the tape cook reads a GLIDED drive (shared, one machine): drive01_ is raw/unglided
        // and `a` ∝ 1/(0.01+6d) with output ∝ a — the yank blast (see prepare()'s aTpDg_ note).
        if (c == 0)
        {
            if (tpDrvG_ < 0.0f) tpDrvG_ = drive01_;             // first touch after reset: snap, no boot glide
            tpDrvG_ += (drive01_ - tpDrvG_) * aTpDg_;
        }
        const float dG = tpDrvG_;
        const float Ms = 0.5f + 1.5f * (1.0f - t.sat);
        float b  = biasE_ + drW_[c];
        float kJ = 0.47875f * t.km;
        if (chr_ == 7) { kJ *= 1.8f; b -= 0.25f; }  // Under-Biased: the deadzone is OPEN at Bias 0
        if (b < 0.0f)  kJ *= 1.0f + 6.0f * (-b);    // UNDER-BIAS: crossover spit — QUIET material distorts hardest (fb325 reach ×2)
        float dropo = 0.0f;
        if (chr_ == 6)                              // Worn: oxide dropouts — an aperiodic target-hold
        {                                           //   walk (fb325 grammar: shared motor, random hold,
                                                    //   double smoothing). fb345: the old per-sample
                                                    //   noise smoother had stdev ~0.003 — the flicker
                                                    //   mathematically never happened (Worn ≡ Under-Bias
                                                    //   twin, measured env stdev 0.000 dB over 6 s).
            if (c == 0)
            {
                wornPh_ += (float) (0.5 / fs_);
                if (wornPh_ >= wornNxt_)
                { wornPh_ = 0.0f; wornTg_ = rnd11 (0); wornNxt_ = 0.04f + rnd01 (0) * 0.22f; }
                wornW_[0] += (wornTg_ - wornW_[0]) * aTpDg_;    // ~40 ms double smoothing (reuse the coef)
                wornW_[1] += (wornW_[0] - wornW_[1]) * aTpDg_;
            }
            kJ *= 1.0f + 0.85f * wornW_[1];
            dropo = juce::jmax (0.0f, wornW_[1]) * 0.45f;
        }
        // Snarl = loop WIDTH, floored at 0.35 — harness-caught: mapping Snarl 0 straight to
        // c = 0.98 made the irreversible term ×0.02, i.e. THE HYSTERESIS LOOP WAS OFF at default
        // and tape degenerated into a static Langevin curve (the §2.6 failure, measured). Tape
        // always has a loop; Snarl widens it from real → maximally lossy and lagging.
        const float cJ = juce::jlimit (0.005f, 0.81f, std::sqrt (0.65f - 0.65f * pC_[7]) - 0.01f);
        // Sag on tape = program-coupled compression depth: sustained level collapses `a` (the loop
        // pins deeper), Recovery brings it back — tape's own slot on the family pair.
        const float a = (Ms * t.am / (0.01f + 6.0f * dG)) / (1.0f + pC_[3] * 1.5f * 1.5f * sagV_[c]);
        // fb325 — HARNESS/EAR-CAUGHT ("crazy tape noise at Drive 0"): the pinning k does NOT scale
        // with `a`, so at Drive 0 the loop was still fat relative to the signal and the ×(0.6a)
        // output normalisation amplified its residue ~60×. Below the working region the loop width
        // tracks the pinning scale ⇒ Drive 0 is genuinely CLEAN, the loop opens as drive rises.
        kJ *= juce::jmin (1.0f, 2.5f / a);
        // H grows with drive too (§2.4c: the pre-gain sits OUTSIDE the drive→a mapping, so the top
        // of Drive corners the loop instead of only re-pinning it — and Slam pushes far past it).
        float H = u * 5.0f * (1.0f + 2.0f * dG) * (1.0f - dropo);
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
        if (std::fabs (dG - tpDrvP_) > 1.0e-4f) { tpDrvP_ = dG; tpMk_ = std::pow (10.0f, 0.90f * dG); }
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
                              (pC_[3] * 1.5f + ((chr_ == 7) ? 1.2f : 0.0f)) * 1.25f * sagV_[c]));
                              // fb345 — was jmax(knob, 1.2 floor): Starved 9V masked the knob's
                              // first 80%. Additive = floor intact at 0, travel un-masked.
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
        const float rail = t.rail * (1.0f - juce::jmin (0.94f, (t.sg + pC_[3] * 1.5f) * 0.62f * sagV_[c]));
                         // fb345 — was jmax(t.sg, knob): Plexi's 0.9 floor MASKED the knob's first 60%
                         // (measured bit-identical 0→0.5) and Dying Battery's 1.3 masked 87%. Additive
                         // keeps the character floor at knob 0 and un-masks the whole travel.
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

    // ════════════════════════════════════════════════════════════════════════
    //  fb326 — PHASE D: THE SHAPER FAMILY (bible §6 + §9.5). The curve is DATA: drawn (Shaper /
    //  Shaper Asym), synthesised from a drawn SPECTRUM (Harmonics — Le Brun 1979 Chebyshev
    //  waveshaping: T_k maps a unit sine to EXACTLY its k-th harmonic), or read from a morphing
    //  wavetable frame (Table — 64 transfer functions on one knob). ONE core, four fronts.
    //  Laws encoded here: the 40 ms OUTPUT-crossfade declick (§6.5 — swapping f() mid-stream is
    //  textbook zipper; blending two memoryless maps of the same input is itself a valid transfer
    //  curve at every instant) · the Beyond rule blends RULE OUTPUTS (§6.4: clamp↔reflect↔wrap;
    //  reflect = a wavefolder built from your own curve) · per-bake slope normalisation (unity law:
    //  a staircase and a gentle S must both power on at ≈ 0 dB) · Chebyshev domain HARD-clamped
    //  (T16(1.5) ≈ 4.3e7 — BIBO, §9.5) · curves rebake on the audio thread, rate-limited, ~10 µs.
    //  ⏭️ The drag-editor + Send To Shaper (§6.6) land next with the UI mockup; setCurve()/
    //  setHarmonicBars() below are the ready-made receiving hooks.
    // ════════════════════════════════════════════════════════════════════════

    static constexpr int kCK = 2048;                  // curve cells over the bipolar domain
    static constexpr float kShDom = 0.35f;            // driven-signal → curve-domain scale (drive 0
                                                      // sits ~28% in — the Log voicing's home turf)

public:
    /** UI hooks (message thread ok — bakes are picked up at the 64-sample dirty cadence and always
        land through the 40 ms crossfade). The editor's dense-curve setter rides this same path
        next phase. */
    void setHarmonicBars (const float* bars16) noexcept
    {
        for (int i = 0; i < 16; ++i) hBars_[i] = juce::jlimit (-1.0f, 1.0f, bars16[i]);
        cvForce_ = true;
    }

    /** fb328 — the USER'S drawn banks (curve card / Send To Shaper). Transfer values over the
        bipolar domain, any density ≥ 8 (linear-resampled to kCK). Message thread; the audio-thread
        bake picks it up at the dirty cadence and lands through the 40 ms crossfade — the busy flag
        makes a mid-write bake skip one round instead of reading a torn table. */
    void setUserCurves (const float* a, const float* b, const float* c2, const float* d2, int n) noexcept
    {
        // fb330 — FOUR user banks (A→B→C→D on one Morph). A null bank keeps its generated default.
        if (a == nullptr || n < 8)
        {
            for (int k2 = 0; k2 < 4; ++k2) uCvHas_[k2].store (false, std::memory_order_release);
            uCvOn_.store (false, std::memory_order_release); cvForce_ = true; return;
        }
        const float* banks[4] = { a, b, c2, d2 };
        uCvBusy_.store (true, std::memory_order_release);
        for (int k2 = 0; k2 < 4; ++k2)
        {
            if (banks[k2] == nullptr) { uCvHas_[k2].store (false, std::memory_order_relaxed); continue; }
            for (int cell = 0; cell <= kCK; ++cell)
            {
                const float s = (float) cell / (float) kCK * (float) (n - 1);
                const int   k = juce::jmin (n - 2, (int) s);
                const float d = s - (float) k;
                uCv_[k2][cell] = juce::jlimit (-1.5f, 1.5f, banks[k2][k] + (banks[k2][k + 1] - banks[k2][k]) * d);
            }
            uCvHas_[k2].store (true, std::memory_order_relaxed);
        }
        uCvBusy_.store (false, std::memory_order_release);
        uCvOn_.store (true, std::memory_order_release);
        cvForce_ = true;
    }
    void clearUserCurves() noexcept
    {
        for (int k2 = 0; k2 < 4; ++k2) uCvHas_[k2].store (false, std::memory_order_release);
        uCvOn_.store (false, std::memory_order_release); cvForce_ = true;
    }
    bool userCurvesActive() const noexcept { return uCvOn_.load (std::memory_order_relaxed); }

    /** fb339 — the USER FRAME STACK for Table mode (bible §9.5: an osc's wavetable frames AS the
        transfer curves; Morph sweeps the frame index, neighbours blended AT BAKE TIME). Message
        thread; flat array = nFrames × frameLen, each frame bipolar ±1. Busy flag = the same
        torn-read guard as setUserCurves. */
    void setUserTable (const float* d, int nFrames, int frameLen) noexcept
    {
        if (d == nullptr || nFrames < 1 || frameLen < 8)
        { tblUOn_.store (false, std::memory_order_release); cvForce_ = true; return; }
        tblUBusy_.store (true, std::memory_order_release);
        const int nF = juce::jmin (kTF, nFrames);
        for (int f = 0; f < nF; ++f)
            for (int cell = 0; cell <= kTB; ++cell)
            {
                const float s2 = (float) cell / (float) kTB * (float) (frameLen - 1);
                const int   k  = juce::jmin (frameLen - 2, (int) s2);
                const float t  = s2 - (float) k;
                const float* fr = d + (size_t) f * (size_t) frameLen;
                tblU_[f][cell] = juce::jlimit (-1.5f, 1.5f, fr[k] + (fr[k + 1] - fr[k]) * t);
            }
        tblUN_ = nF;
        tblUBusy_.store (false, std::memory_order_release);
        tblUOn_.store (true, std::memory_order_release);
        cvForce_ = true;
    }
    void clearUserTable() noexcept { tblUOn_.store (false, std::memory_order_release); cvForce_ = true; }

    /** fb328 — the §5.8 core viz + Send To Shaper source: the CURRENT mode's stateless transfer on
        the driven axis (x spans ±occSpan()). Message-thread safe: memoryless families ride the fb325
        stateless reference (shapeM c = −1 advances no state); ANALOG/DIGITAL are static projections
        of the circuit's front end from the live params (their memory cannot be a curve). A concurrent
        bank swap mid-read tears one frame — viz-grade by design. */
    void sampleCurve (float* out, int n) noexcept
    {
        if (out == nullptr || n < 2) return;
        const Family fam  = family();
        const float  sp   = occSpan();
        const float  dEff = driveC_;
        const float  gapV = (fam == FAM_CLIP) ? std::pow (pC_[5], 1.6f) * 0.45f * juce::jmax (1.0f, dEff) : 0.0f;
        const float  dzU  = (fam == FAM_DIODE && mode_ != Diode2) ? pC_[5] * 3.0f : 0.0f;
        auto dz = [dzU] (float v) noexcept -> float { if (dzU <= 1.0e-5f) return v;
            return (v >= 0.0f) ? juce::jmax (0.0f, v - dzU) : juce::jmin (0.0f, v + dzU); };
        const float dcOff = (fam == FAM_ANALOG || fam == FAM_DIGITAL) ? 0.0f : shapeM (dz (biasE_ * dEff), -1);
        for (int i = 0; i < n; ++i)
        {
            const float v = ((float) i / (float) (n - 1) * 2.0f - 1.0f) * sp;
            float y;
            if (fam == FAM_ANALOG)
            {   // static front-end skeletons — real topology class per circuit, driven by the live knobs
                const float d1 = drive01_;
                switch (mode_)
                {
                    case Tube:        { const float g = 1.0f + 6.0f * d1, b = biasE_ * 0.6f;
                                        auto tb = [g] (float q) { return std::tanh (q * g * (q < 0.0f ? 1.3f : 0.9f)); };
                                        y = tb (v + b) - tb (b); } break;
                    case Tape:        { const float g = 0.8f + 5.0f * d1; y = std::tanh (v * g); } break;
                    case Transformer: { const float g = 0.6f + 4.0f * d1; const float u2 = v * g;
                                        y = u2 / std::pow (1.0f + std::pow (std::fabs (u2), 6.0f), 1.0f / 6.0f); } break;
                    case StompBox:    { const float g = 1.0f + 10.0f * d1;
                                        y = std::asinh (v * g) / std::asinh (juce::jmax (1.5f, g)); } break;
                    default:          { float u2 = v * (1.0f + 14.0f * d1);
                                        for (int s2 = 0; s2 < 3; ++s2) u2 = std::tanh (u2 * 1.4f); y = u2; } break;
                }
            }
            else if (fam == FAM_DIGITAL)
            {   // the grid's static transfer (Downsample's destruction is temporal — identity here)
                const float crush = kneeC_;
                if (mode_ == Bitcrush)      { const float steps = 2.0f + (1.0f - crush) * 60.0f;
                                              y = std::round (juce::jlimit (-1.2f, 1.2f, v) * steps) / steps; }
                else if (mode_ == Overflow) { const float thr = juce::jmax (0.03f, 1.0f - 0.97f * crush);
                                              float t2 = (v / thr + 1.0f) * 0.5f; t2 -= std::floor (t2);
                                              y = (t2 * 2.0f - 1.0f) * thr; }
                else                          y = juce::jlimit (-1.0f, 1.0f, v);
            }
            else
            {
                float u = v;
                // fb336 — the pills are VISIBLE on the curve (the everything-audible-shows law):
                // Octave = the even-symmetric rectifier silhouette; Wrap = the rail teleport.
                if (fam == FAM_DIODE && octC_ > 1.0e-4f)
                    u += octC_ * ((std::fabs (u) - sp * 0.5f) * 2.0f - u);
                if (gapV > 1.0e-5f) { const float a2 = std::fabs (u);
                                      u = (a2 <= gapV) ? 0.0f : ((u < 0.0f) ? u + gapV : u - gapV); }
                if (fam == FAM_CLIP && wrapC_ > 1.0e-4f)
                { float w2 = (u + 1.0f) * 0.5f; w2 -= std::floor (w2); u += wrapC_ * ((w2 * 2.0f - 1.0f) - u); }
                y = shapeM (dz (u), -1) - dcOff;
            }
            out[i] = juce::jlimit (-1.6f, 1.6f, y);
        }
    }

    /** fb328 — max-normalised occupancy snapshot (48 bins over the same driven axis as sampleCurve).
        Silence reads all-zero so the glow goes DARK at idle (the audio-reactive hard rule). */
    void copyOcc (float* out48) noexcept
    {
        float mx = 1.0e-6f;
        for (int i = 0; i < 48; ++i) mx = juce::jmax (mx, occ_[i]);
        const float g = (mx > 0.02f) ? 1.0f / mx : 0.0f;
        for (int i = 0; i < 48; ++i) out48[i] = occ_[i] * g;
    }
private:
    float occSpan() const noexcept    // fb328 — display span of the driven axis, per family
    {
        switch (family())
        {
            case FAM_SHAPER:  return 1.0f / kShDom;   // fb330 — EXACTLY the drawn domain: the core viz,
                                                      // the card and the ink share one x-axis (Max:
                                                      // "they don't align which they SHOULD")
            case FAM_FOLD:    return 4.5f;    // a few fold legs
            case FAM_DIGITAL: return 1.6f;    // the grid is defined for ±1 FS
            default:          return 3.0f;    // knee + rail run
        }
    }
    void occAdd (float v) noexcept
    {
        const int b = (int) ((juce::jlimit (-1.0f, 1.0f, v / occSpan()) + 1.0f) * 23.99f);
        occ_[b] += 0.01f;
    }
    float uCv_[4][kCK + 1] {};                          // fb328/330 — user banks A/B/C/D (resampled to the grid)
    std::atomic<bool> uCvOn_ { false }, uCvBusy_ { false };
    std::atomic<bool> uCvHas_[4] { {false}, {false}, {false}, {false} };   // fb330 — per-bank presence
    float occ_[48] {};                                  // fb328 — signal occupancy (driven axis, ch 0)
    int   occN_ = 0;

    /** The stateless curve core — domain map (Beyond) + bank morph. c < 0 = the DC reference. */
    float shaperCore (float v, int c) noexcept
    {
        // fb345 — BIAS lives HERE in DOMAIN units (±0.7 of the curve at full knob). The old family
        // vA injection was biasE_·dEff ≈ ±8 curve-domains ⇒ the read clamped to an edge CONSTANT
        // and the DC blocker erased it — measured SILENT at both Bias extremes on all four modes
        // (spec0-100 = 0.00). Inside the core, signal + DC-reference + viz stay consistent free.
        // (Table 'Phase' rotates the frame with biasE_ instead — shaperF, exempt here.)
        if (! (mode_ == Table && chr_ == 5))
            v += biasE_ * 0.7f;
        // BEYOND (P7) — blend the OUTPUTS of neighbouring rules (§6.4). Harmonics: clamp is a HARD
        // law (Chebyshev explodes outside [-1,1]) except its 'Beyond' voicing, which reflects.
        float bey = (mode_ == Harmonics) ? ((chr_ == 7) ? 0.5f : 0.0f) : pC_[6];
        float y;
        if (bey < 1.0e-3f)       y = cvMorph (domClamp (v), c);
        else if (bey < 0.5f)     { const float w = bey * 2.0f;
                                   y = (1.0f - w) * cvMorph (domClamp (v), c) + w * cvMorph (domFold (v), c); }
        else if (bey < 0.999f)   { const float w = bey * 2.0f - 1.0f;
                                   y = (1.0f - w) * cvMorph (domFold (v), c) + w * cvMorph (domWrap (v), c); }
        else                     y = cvMorph (domWrap (v), c);
        return y;
    }

    static float domClamp (float v) noexcept { return juce::jlimit (-1.0f, 1.0f, v); }
    static float domFold  (float v) noexcept
    {
        float t = (v + 1.0f) * 0.5f;
        t = std::fmod (t, 2.0f); if (t < 0.0f) t += 2.0f;
        return ((t <= 1.0f) ? t : 2.0f - t) * 2.0f - 1.0f;    // MIRROR: a folder from your own curve
    }
    static float domWrap  (float v) noexcept
    {
        float t = (v + 1.0f) * 0.5f;
        t -= std::floor (t);
        return t * 2.0f - 1.0f;                                // WRAP: the destruction rule
    }

    /** Morph (the family SIG) between banks A and B — gated to one read at the endpoints. */
    float cvMorph (float x, int c) noexcept
    {
        // Shaper Asym 'Halves': the sign selects the bank; Morph SWAPS the halves.
        if (mode_ == ShaperAsym && chr_ == 1)
        {
            const int bk = (x >= 0.0f) ? 0 : 1;
            const float m = kneeC_;
            if (m < 1.0e-3f) return cvEval (bk, x);
            return (1.0f - m) * cvEval (bk, x) + m * cvEval (bk ^ 1, x);
        }
        // fb330 — the DRAWN modes sweep ALL FOUR banks A→B→C→D on one Morph (Max: "it needs to be
        // ABCD so it morphs through all of them"). Adjacent-pair blend = piecewise-valid transfer.
        if (mode_ == Shaper || mode_ == ShaperAsym)
        {
            const float s = juce::jlimit (0.0f, 1.0f, kneeC_) * 3.0f;
            const int   k = juce::jmin (2, (int) s);
            const float f = s - (float) k;
            if (f < 1.0e-3f) return cvEval (k, x);
            if (f > 0.999f)  return cvEval (k + 1, x);
            return (1.0f - f) * cvEval (k, x) + f * cvEval (k + 1, x);
        }
        // Table sweeps FRAMES via rebake — Morph is not an A/B blend there (except Two-Table).
        const float m = (mode_ == Table && chr_ != 7) ? 0.0f : kneeC_;
        if (m < 1.0e-3f)  return cvEval (0, x);
        if (m > 0.999f)   return cvEval (1, x);
        return (1.0f - m) * cvEval (0, x) + m * cvEval (1, x);
    }

    float cvEval (int bank, float x) noexcept          // linear read + the 40 ms front/back blend
    {
        const float s = (x * 0.5f + 0.5f) * (float) kCK;
        const int   k = juce::jlimit (0, kCK - 1, (int) s);
        const float d = juce::jlimit (0.0f, 1.0f, s - (float) k);
        const float* tf = cvT_[bank][cvFront_];
        float y = (tf[k] + (tf[k + 1] - tf[k]) * d) * cvMk_[bank][cvFront_];
        if (cvXf_ < 1.0f)
        {
            const float* tb = cvT_[bank][cvFront_ ^ 1];
            const float yb = (tb[k] + (tb[k + 1] - tb[k]) * d) * cvMk_[bank][cvFront_ ^ 1];
            y = (1.0f - cvXf_) * y + cvXf_ * yb;       // §6.5 — blend of two maps IS a valid map
        }
        return y;
    }

    /** The stateful front — Squash, the per-voicing machines, then the core. */
    float shaperF (float u, int c) noexcept
    {
        // SQUASH (P8) — 3 ms/80 ms leveller INTO the curve: quiet passages traverse the full drawn
        // shape (the no-dead-first-third clause as a knob). Byte-identical bypass at 0.
        if (pC_[7] > 1.0e-4f)
        {
            const float a = std::fabs (u);
            shEnv_[c] += (a - shEnv_[c]) * ((a > shEnv_[c]) ? 0.0032f : 0.00013f);
            const float g = 0.9f / juce::jmax (0.045f, shEnv_[c]);
            u *= 1.0f + pC_[7] * (juce::jmin (20.0f, g) - 1.0f);   // up to 20:1 (~30 dB)
        }
        float v = u * kShDom;
        const bool sh = (mode_ == Shaper), as = (mode_ == ShaperAsym);
        // ── the voicing machines (bible §9.5 — each changes PHYSICS) ─────────
        if ((sh && chr_ == 1) || (as && chr_ == 2))               // Log / Log Both — traverse in dB:
        {                                                          //   quiet detail owns the graph
            const float s = (v < 0.0f) ? -1.0f : 1.0f;             //   (Trash 2's trick — what makes
            v = s * std::log1p (std::fabs (v) * 999.0f) * (1.0f / 6.9077553f);   // a drawn curve usable)
        }
        if (sh && chr_ == 7)                                       // Squeeze — the input is levelled
        {                                                          //   into the curve at ANY playing level
            const float a = std::fabs (u);
            shEnv2_[c] += (a - shEnv2_[c]) * ((a > shEnv2_[c]) ? 0.0032f : 0.00013f);
            v = u * kShDom * (0.85f / juce::jmax (0.06f, shEnv2_[c] * kShDom));
        }
        if (mode_ == Harmonics)
        {
            if (chr_ == 0)                                         // Pure — input hard-normalised: the
            {                                                      //   bars ARE the spectrum, guaranteed
                const float a = std::fabs (u);
                shEnv2_[c] += (a - shEnv2_[c]) * ((a > shEnv2_[c]) ? 0.008f : 0.0002f);
                v = u / juce::jmax (0.05f, shEnv2_[c] * 1.05f);
                v = juce::jlimit (-1.0f, 1.0f, v);
            }
            if (chr_ == 6)                                         // Stretch — warped domain: the ladder
            {                                                      //   detunes inharmonically (bell/gong)
                const float g2 = 0.4f + pC_[4] * 2.2f;
                const float a2 = std::fabs (v);
                if (a2 > 1.0e-9f) v = (v < 0.0f ? -1.0f : 1.0f) * std::pow (a2, g2);
            }
        }
        if (mode_ == Table && chr_ == 5)                           // Phase — Bias ROTATES the frame under
            v = domWrap (v + biasE_);                              //   the signal's zero crossing
        if ((sh && chr_ == 5) || (as && chr_ == 6))                // Growl — 0.35·y[n-1] into the input:
            v += 0.35f * shGrl_[c] * kShDom;                       //   the family's ONE memory voicing
        if (as && chr_ == 7)                                       // Bias Walk — the operating point
        {                                                          //   drifts with the program (grid bias)
            const float a = std::fabs (u);
            shEnv2_[c] += (a * a - shEnv2_[c]) * 0.0011f;          //   30 ms RMS-ish
            v += biasE_ * juce::jmin (1.0f, std::sqrt (shEnv2_[c]) * 2.5f) * 0.5f;
        }
        if (sh && chr_ == 6)                                       // Sticky — hysteresis on the read:
        {                                                          //   S&H grit that CLEANS UP when loud
            const float a = std::fabs (u);
            shEnv2_[c] += (a - shEnv2_[c]) * 0.004f;
            const float win = 0.05f * (1.0f - juce::jmin (1.0f, shEnv2_[c] * 2.0f));
            if (std::fabs (v - shStk_[c]) < win) v = shStk_[c]; else shStk_[c] = v;
        }
        float y = shaperCore (v, c);
        if ((sh && chr_ == 4) || (as && chr_ == 5))                // Twice — g(g(x)): harmonic order
            y = shaperCore (y, c);                                 //   SQUARES with nothing new drawn
        if ((sh && chr_ == 5) || (as && chr_ == 6)) shGrl_[c] = y;
        if ((sh && chr_ == 3) || (as && chr_ == 4))                // Glass — the curve becomes a COLOUR:
        {                                                          //   distortion folded into the mids
            wcLp_[c] += (y - wcLp_[c]) * 0.25f;
            y = wcLp_[c] * 1.15f;
        }
        return y;
    }

    // ── the bakes (audio thread, rate-limited, ~10 µs; always land through the 40 ms fade) ──────
    void shaperCheckBake() noexcept
    {
        if (--cvChk_ > 0) return;
        cvChk_ = 64;
        if (cvXf_ < 1.0f) return;                                  // one fade at a time
        const float mSnap = kneeC_;
        const bool dirty = cvForce_ || cvMode_ != mode_ || cvChr_ != chr_ || cvPill_ != pill2_
                        || std::fabs (cvSm_ - pC_[4]) > 0.01f
                        || (mode_ == Table && chr_ != 0 && chr_ != 7 && std::fabs (mSnap - cvMor_) > 0.008f);
        if (! dirty) return;
        cvForce_ = false; cvMode_ = mode_; cvChr_ = chr_; cvPill_ = pill2_; cvSm_ = pC_[4]; cvMor_ = mSnap;
        bakeCurves();
    }

    void bakeCurves() noexcept
    {
        const int bk = cvFront_ ^ 1;
        // Harmonics: build the (character-modified) bar sets once per bake
        float bA[17] {}, bB[17] {};
        if (mode_ == Harmonics)
        {
            for (int k = 1; k <= 16; ++k)
            {
                float a = hBars_[k - 1];
                if (chr_ == 2 && (k % 2) == 0) a = 0.0f;                       // Odd Only
                if (chr_ == 3 && k > 1 && (k % 2) == 1) a = 0.0f;              // Even Only (keep a1)
                if (chr_ == 4) a /= (float) k;                                 // Ramp — musical draw
                if (chr_ == 5 && (k % 2) == 0) a = -a;                         // Comb — flipped phases
                bA[k] = a;
                bB[k] = a / (float) k;                                         // bank B = tilted target
            }
        }
        // fb330 — the DRAWN modes carry FOUR morphable banks (A→B→C→D); the others keep their two.
        const int nBanks = (mode_ == Shaper || mode_ == ShaperAsym) ? 4 : 2;
        for (int bank = 0; bank < nBanks; ++bank)
        {
            float* t = cvT_[bank][bk];
            float pk = 1.0e-6f;
            for (int cell = 0; cell <= kCK; ++cell)
            {
                const float x = (float) cell * (2.0f / (float) kCK) - 1.0f;
                float y;
                // fb328 — the USER'S drawn curve (card / Send To Shaper) replaces the generated
                // banks on the two drawn modes; smooth / odd-enforce / slope-norm still apply, so
                // every engine law holds on user ink. Busy write ⇒ skip this round (cvForce_ holds).
                if (bank < 4 && uCvHas_[bank].load (std::memory_order_relaxed)
                    && ! uCvBusy_.load (std::memory_order_acquire)
                    && (mode_ == Shaper || mode_ == ShaperAsym))
                    y = uCv_[bank][cell];
                else switch (mode_)
                {
                    case ShaperAsym:                                     // fb330 — 4 generated banks
                        if      (bank == 0) y = (x > 0.0f) ? 1.35f * x - 0.35f * x * x * x : 0.05f * x;   // half-wave A: fat + even-rich
                        else if (bank == 1) y = 0.85f * std::sin (2.5f * 3.1415927f * x);                 // folder B
                        else if (bank == 2) y = (x >= 0.0f) ? std::tanh (2.6f * x) : std::tanh (1.1f * x) * 0.7f;   // asym drive C
                        else { float tw = (x + 1.0f) * 0.75f; tw -= std::floor (tw); y = tw * 2.0f - 1.0f; }        // wrap D
                        break;
                    case Harmonics:
                    {
                        // T0=1, T1=x, T_{k+1} = 2x·T_k − T_{k−1} (Le Brun) — bank-selected bars
                        const float* bb = (bank == 0) ? bA : bB;
                        float tm2 = 1.0f, tm1 = x, acc = bb[1] * x;
                        for (int k = 2; k <= 16; ++k)
                        {
                            const float tk = 2.0f * x * tm1 - tm2;
                            acc += bb[k] * tk;
                            tm2 = tm1; tm1 = tk;
                        }
                        y = acc;
                        break;
                    }
                    case Table:      y = (bank == 0 || chr_ == 7) ? ((bank == 1) ? 1.2f * x - 0.2f * x * x * x
                                                                                : tableFrame (x))
                                                                  : tableFrame (x);   // bank1 = hand curve (Two-Table)
                                     break;
                    default:                                             // Shaper — fb330: 4 generated banks
                        if      (bank == 0) y = 1.25f * x - 0.25f * x * x * x;         // gentle S (A)
                        else if (bank == 1) y = std::round (x * 2.5f) * 0.4f;          // razor staircase (B)
                        else if (bank == 2) y = std::sin (2.3561945f * x);             // sine fold (C)
                        else { float tw = (x + 1.0f) * 0.75f; tw -= std::floor (tw); y = tw * 2.0f - 1.0f; }   // wrap (D)
                        break;
                }
                t[cell] = y;
                pk = juce::jmax (pk, std::fabs (y));
            }
            // Harmonics/Table: peak-normalise (|f|≤Σ|a_k|≤16 — the §9.5 stability clamp; frame
            // switching must never step the level)
            if (mode_ == Harmonics || mode_ == Table)
                for (int cell = 0; cell <= kCK; ++cell) t[cell] /= pk;
            // SMOOTH (P5) — bake-time corner rounding: the most musical control AND the cheapest
            // anti-aliasing control (§5.5). Razor voicings deliberately skip it.
            const bool razor = ((mode_ == Shaper && chr_ == 2) || (mode_ == ShaperAsym && chr_ == 3)
                             || (mode_ == Table && chr_ == 6));
            int rad = razor ? 0 : (int) (pC_[4] * pC_[4] * 90.0f) + ((mode_ == Shaper && chr_ == 3) || (mode_ == ShaperAsym && chr_ == 4) ? 40 : 0)
                                 + ((mode_ == Table && chr_ == 3) ? 60 : 0);
            for (int pass = 0; pass < (rad > 0 ? 2 : 0); ++pass)
            {
                float acc = 0.0f; const int w = rad;
                for (int cell = 0; cell <= kCK; ++cell) cvS1_[cell] = t[cell];
                for (int cell = 0; cell <= kCK; ++cell)
                {
                    acc = 0.0f; int n = 0;
                    for (int j = juce::jmax (0, cell - w); j <= juce::jmin (kCK, cell + w); ++j) { acc += cvS1_[j]; ++n; }
                    t[cell] = acc / (float) n;
                }
            }
            if (mode_ == Table && chr_ == 2)                       // fb345 — 'Mip' had NO DSP (measured
            {   // byte-identical to 'Sweep'). Now the mip ladder, transfer edition: the frame's
                // resolution FALLS as Morph rises — chunky staircase frames by the top of the knob.
                const int hold = 2 + (int) (cvMor_ * 22.0f);
                for (int cell = 0; cell <= kCK; ++cell) t[cell] = t[(cell / hold) * hold];
            }
            // Table 'Half' + Shaper mode + the Sym pill: enforce ODD (zero DC, zero evens — §6.9)
            if (mode_ == Shaper || pill2_ || (mode_ == Table && chr_ == 4))
                for (int cell = 0; cell <= kCK / 2; ++cell)
                {
                    const float odd = 0.5f * (t[kCK - cell] - t[cell]);
                    t[kCK - cell] = odd; t[cell] = -odd;
                }
            // unity law: normalise the small-signal slope per bake (a staircase and a gentle S both
            // power on at ≈ 0 dB; Drive earns the rest)
            const float sl = std::fabs (t[kCK / 2 + 24] - t[kCK / 2 - 24]) / (48.0f / (float) kCK);
            cvMk_[bank][bk] = juce::jlimit (0.8f, 3.0f, 1.0f / (kShDom * juce::jmax (0.35f, sl)));
        }
        cvXf_ = 0.0f;                                              // 40 ms output crossfade begins
        cvPend_ = true;
    }

    /** The generated morph table (64+ distinct transfer functions on one knob) — sine → phase-
        distorted saw → PWM square → rising fold chaos. The user-wavetable hookup rides the same
        bake path next phase. */
    float tableFrame (float x) noexcept
    {
        // fb339 — USER FRAME STACK first (runs inside the BAKE — zero audio-thread cost; the
        // 40 ms table crossfade lands the swap). Falls back to the generated morph table.
        if (tblUOn_.load (std::memory_order_relaxed) && ! tblUBusy_.load (std::memory_order_acquire) && tblUN_ > 0)
        {
            const float fs2 = juce::jlimit (0.0f, 1.0f, cvMor_) * (float) (tblUN_ - 1);
            const int   f0  = juce::jmin (tblUN_ - 1, (int) fs2);
            const float ff  = fs2 - (float) f0;
            const float ph  = juce::jlimit (0.0f, 1.0f, (x + 1.0f) * 0.5f);
            const float s2  = ph * (float) kTB;
            const int   k   = juce::jmin (kTB - 1, (int) s2);
            const float t   = s2 - (float) k;
            const float a   = tblU_[f0][k] + (tblU_[f0][k + 1] - tblU_[f0][k]) * t;
            if (ff < 1.0e-3f || f0 + 1 >= tblUN_) return a;
            const float b   = tblU_[f0 + 1][k] + (tblU_[f0 + 1][k + 1] - tblU_[f0 + 1][k]) * t;
            return a + ff * (b - a);
        }
        const float m = cvMor_;
        const float ph = (x + 1.0f) * 0.5f;
        if (m < 0.34f)                                             // sine → PD saw
        {
            const float t = m * 2.94f;
            return std::sin (6.2831853f * (std::pow (ph, 1.0f + 3.0f * t) * (0.5f + 0.5f * t) + (1.0f - t) * 0.0f) - 3.1415927f * (1.0f - t) * 0.5f + 1.5707963f * t);
        }
        if (m < 0.67f)                                             // saw → PWM square
        {
            const float t = (m - 0.34f) * 3.03f;
            const float s = std::sin (6.2831853f * std::pow (ph, 2.5f) + 1.5707963f);
            return std::tanh (s * (1.0f + 14.0f * t)) * (1.0f - 0.25f * t)
                 + 0.25f * t * std::sin (12.566371f * ph);
        }
        const float t = (m - 0.67f) * 3.03f;                       // square → folded chaos
        return std::sin (6.2831853f * ph * (1.0f + 17.0f * t) + 3.0f * t * std::sin (6.2831853f * ph));
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
        if (c == 0) occAdd (u);                          // fb328 — occupancy viz (grid axis)

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
            // fb336 — DIGITAL `Clean` (bible §900): 4-pole pre band-limit at 0.45·fs_r BEFORE the
            // hold — true anti-aliasing, not post-hoc filtering (D16 Decimort's "Approximation
            // filter"; `Smooth` stays its "Images filter"). OFF = raw hash (§13 asserts the
            // inverse for DIGITAL); ON = band-limited lo-fi — telephone / AM radio.
            if (clnC_ > 1.0e-4f)
            {
                if (std::fabs (fsr - clnFsrP_) > 1.0f) { clnFsrP_ = fsr; clnK_ = onePole (juce::jmax (200.0f, 0.45f * fsr)); }
                float v5 = u;
                const int nCl = (eqEff_ >= 3) ? 8 : 4;         // fb337 — §3.8: Ultra doubles the Clean order
                for (int p5 = 0; p5 < nCl; ++p5) { clnZ_[c][p5] += (v5 - clnZ_[c][p5]) * clnK_; v5 = clnZ_[c][p5]; }
                u += clnC_ * (v5 - u);
            }
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
                if (c == 1 && pC_[6] <= 1.0e-4f)
                {   // fb345 — Spread 0 = ONE converter clock (real stereo ADC). The spread/jitter
                    // GLIDE (even just the boot default 0.5 gliding away) skewed c1's clock and the
                    // phase split persisted FOREVER — measured maxLR 0.51 on mono input at Spread 0.
                    // R follows L's tick decision exactly; values stay per-channel (stereo intact).
                    holdPh_[1] = holdPh_[0];
                    if (digTk_) holdV_[1] = u;
                }
                else
                {
                    holdPh_[c] += inc;
                    if (holdPh_[c] >= 1.0f) { holdPh_[c] -= (float) (int) holdPh_[c]; holdV_[c] = u; if (c == 0) digTk_ = true; }
                    else if (c == 0) digTk_ = false;
                }
                if (mode_ == Downsample && chr_ == 5) holdV_[c] *= 0.9896f;  // Track&Hold — the cap LEAKS (τ≈2 ms):
                u = holdV_[c];                                               //   every step becomes a tiny ramp, audibly analogue
                if (mode_ == Downsample && chr_ == 7)          // Shatter — a second clock at φ = 1.618:
                {                                              //   two inharmonic staircases beating
                    if (c == 1 && pC_[6] <= 1.0e-4f)
                    { hold2Ph_[1] = hold2Ph_[0]; if (digTk2_) hold2V_[1] = u; }   // fb345 — same one-clock law
                    else
                    {
                        hold2Ph_[c] += inc * 1.618f;
                        if (hold2Ph_[c] >= 1.0f) { hold2Ph_[c] -= (float) (int) hold2Ph_[c]; hold2V_[c] = u; if (c == 0) digTk2_ = true; }
                        else if (c == 0) digTk2_ = false;
                    }
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
            // fb337 — §3.8: for DIGITAL, Quality = reconstruction-filter ORDER, never oversampling
            // (oversampling a sample-domain destroyer deletes the effect). Smooth: 1/1/2/4 poles.
            const int nSm = (eqEff_ >= 3) ? 4 : (eqEff_ >= 2 ? 2 : 1);
            for (int p6 = 1; p6 < nSm; ++p6) { smZb_[c][p6 - 1] += (u - smZb_[c][p6 - 1]) * kk; u = smZb_[c][p6 - 1]; }
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

    float anti (float u) noexcept   // fb337 — LIVE for the whitelist. LOCKSTEP LAW (Shapers.h): every
    {                               // F here MUST match TODAY'S shapeM curve or ADAA ships wrong DSP.
        switch (mode_)
        {
            case HardClip: return hardClipA (u, kneeWidth());   // valid ONLY while kneePre is inactive — the whitelist gates on that
            case ZeroSquare:
            {   // F of: ped·sign(u)·[|u|>gate] + (1−p)·clip(u) — the pedestal AND its stability gate
                const float p  = 0.92f * (1.0f - kneeE_);
                const float a  = std::fabs (u);
                const float Fp = (a > zsGate_) ? p * (a - zsGate_) : 0.0f;
                const float Fc = (a <= 1.0f) ? a * a * 0.5f : (a - 0.5f);
                return Fp + (1.0f - p) * Fc;
            }
            case SoftClip:
            default:       return softClipA2 (u, kneeE_);       // fb337 — the QUARTIC anchor's F (LUT), not the stale cubic
        }
    }
    /** fb337 — softClipA with the C anchor's F from the LUT (the fb325 quartic has no elementary F;
        the old softClipA's cubic F silently stopped matching softClipF — kept below for reference). */
    float softClipA2 (float x, float k) const noexcept
    {
        const float a  = std::fabs (x);
        const float FA = a - std::log1p (a);
        const float FB = std::sqrt (1.0f + x * x) - 1.0f;
        const float FC = fcAnti (a);
        return (k <= 0.5f) ? ((1.0f - 2.0f * k) * FC + (2.0f * k) * FB)
                           : ((2.0f - 2.0f * k) * FB + (2.0f * k - 1.0f) * FA);
    }
    float fcAnti (float a) const noexcept
    {
        if (a >= 8.0f) return fcLut_[1024] + 1.12634f * (a - 8.0f);   // C's asymptote 0.62^(−¼)
        const float s = a * 128.0f; const int i = (int) s; const float t = s - (float) i;
        return fcLut_[i] + (fcLut_[i + 1] - fcLut_[i]) * t;
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
    // ── fb336 — the four family pills (bible §896-900), each glided like Slam (toggle = fade) ──
    float wrapT_ = 0.0f, wrapC_ = 0.0f;               // CLIP    Wrap   — rail teleport
    float octT_  = 0.0f, octC_  = 0.0f;               // DIODE   Octave — pre-rectifier + own blocker
    float trkT_  = 0.0f, trkC_  = 0.0f;               // FOLD    Track  — fold-depth key tracking
    float clnT_  = 0.0f, clnC_  = 0.0f;               // DIGITAL Clean  — 4-pole pre band-limit
    float keyT_  = 110.0f, keyC_ = 110.0f, keyP_ = -1.0f, trkG_ = 1.0f;
    float octBx_[2] {}, octBy_[2] {};                 // Octave's blocker state (advances at 2fs)
    float dcR2_  = 0.99935f;
    float clnZ_[2][8] {}; float clnFsrP_ = -1.0f, clnK_ = 1.0f;   // fb337 — 8 poles at Ultra
    // fb339 — Table's user frame stack (an osc's wavetable as 16 transfer curves)
    static constexpr int kTF = 16, kTB = 1024;
    float tblU_[kTF][kTB + 1] {};
    int   tblUN_ = 0;
    std::atomic<bool> tblUOn_ { false }, tblUBusy_ { false };
    float smZb_[2][3] {};                             // fb337 — Smooth's extra poles (High/Ultra)
    float fcLut_[1025] {};                            // fb337 — F of the quartic Soft-Clip anchor
    int   eqEff_ = 1;                                 // fb337 — effective quality (qual_ + promotion)
    bool  adaaOn_ = false;                            // fb337 — ADAA live this sample (whitelist)

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
    float fbDc_[2] {};       // fb345 — DIODE Snarl's coupling-cap state (AC-coupled loop)
    float punchZ_[2] {};     // CLIP Punch — transient follower state
    float slewZ_[2] {};      // Slew Clip / DIODE Slew — slope-limiter state
    float holdV_[2] {}, holdPh_[2] {};   // DIGITAL — ZOH held value + phase accumulator
    bool  digTk_ = false, digTk2_ = false;   // fb345 — L's tick decisions (Spread-0 one-clock lock)
    float smZ_[2] {};        // DIGITAL — reconstruction LP state
    uint32_t rngC_[2] { 0x12345678u, 0x12345678u };   // per-channel xorshift — SAME seed ⇒ Spread 0 is byte-identical L/R
    float drive01_ = 0.2f;   // GLIDED 0..1 knob (fb345; the DIODE falling-threshold law reads it)
    float drv01T_  = 0.2f;   // fb345 — its target (setDrive writes here)
    float dip_ = 1.0f;       // mode-switch wet dip (0 → 1 over ~15 ms)
    int   chrPend_ = 0;      // fb345 — deferred char swap (lands at the dip bottom)
    float dipT_ = 1.0f, dnDip_ = 0.98f;   // fb345 — fade-down arm + ~0.5 ms-τ coefficient
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
    float ringA_[2] {};      // fb345 — Ring's deficit accumulator (exit overshoot charge)
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
    float tpDrvG_ = -1.0f, aTpDg_ = 0.001f;           // fb345 — glided tape drive (-1 = snap on first use)
    float wornPh_ = 0.0f, wornNxt_ = 0.05f, wornTg_ = 0.0f;   // fb345 — Worn walk (shared motor)
    float xfPhi_[2] {}, xfPhiP_[2] {}, xfB_[2] {}, xfEd_[2] {};   // transformer flux/induction state
    float xfEdK_ = 0.1f;                              // 4 kHz core-loss pole (set in prepare)
    float sbLp_[2] {}, sbTn_[2] {};                   // stomp band split + tone LP
    int   sbChrP_ = -1; float sbHpK_ = 0.1f, sbLpK_ = 0.1f, sbDrvP_ = -1.0f, sbG_ = 1.0f;
    float sbHpHz_ = 720.0f, sbHpDrv_ = -1.0f, sbHpK2_ = 0.1f;   // fb325 — drive-tracking band split
    float sbFlip_[2] { 1.0f, 1.0f }, sbPrev_[2] {}, sbEnv_[2] {}, sbLp2_[2] {};   // fb325 — the SUB flip-flop
    float fbLp_[2] {}, fbLpP_ = -1.0f, fbLpK_ = 0.3f; // fb325 — feedback scream-color pole
    float kpZ_[2] {};                                 // fb325 — Knee edge-slew state (post-rail)
    // ── fb326 — SHAPER family state ──
    float cvT_[4][2][kCK + 1] {};                     // curve tables [bank A/B/C/D][front/back] (fb330)
    float cvMk_[4][2] { {1,1},{1,1},{1,1},{1,1} };    // per-bake slope normalisation (unity law) — 4 banks (fb330)
    float cvS1_[kCK + 1] {};                          // smooth-pass scratch
    int   cvFront_ = 0, cvChk_ = 1;
    float cvXf_ = 1.0f, cvXfI_ = 0.0005f;             // the 40 ms output crossfade (§6.5)
    bool  cvPend_ = false, cvForce_ = true, cvPill_ = false;
    int   cvMode_ = -1, cvChr_ = -1;
    float cvSm_ = -1.0f, cvMor_ = 0.35f;
    float hBars_[16] { 1.0f, 0.55f, 0.8f, 0.3f, 0.65f, 0.2f, 0.5f, 0.15f,
                       0.4f, 0.1f, 0.3f, 0.08f, 0.22f, 0.06f, 0.15f, 0.1f };   // default drawn spectrum
    float shEnv_[2] {}, shEnv2_[2] {}, shGrl_[2] {}, shStk_[2] {};
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
    float hoU_[kNH] {};                       // fb342 — the 64 odd taps REVERSED (contiguous-dot form)
    // fb342 — xr_/so_ are MIRRORED rings (each sample written at p and p+kRB, size 2·kRB) so the
    // polyphase windows are contiguous. They REPLACE the old kRB rings — keeping both copies alive
    // measured a +5-9 ns DIGITAL regression in the scratch build (pure cache pressure). se_ (one
    // delayed read) and dry_ stay plain.
    float xr_[2][2 * kRB] {}, so_[2][2 * kRB] {};
    float se_[2][kRB] {}, dry_[2][kRB] {};
    int   xi_[2] { 0, 0 }, si_[2] { 0, 0 }, di_[2] { 0, 0 };
    // fb342 — the measured-CPU-pass state: dcOff reference cache (see the key comment at the use
    // site), the four pow hoists, and the silence gate.
    int   dcModeP_[2] { -1, -1 };
    float dcKeyP_[2] { 1.0e9f, 1.0e9f }, dcPKeyP_[2] { 1.0e9f, 1.0e9f }, dcOffC_[2] {};
    float dcPKey2P_[2] { 1.0e9f, 1.0e9f };    // review fix — the weighted anti-aliasing checksum
    float gapP_ = -1.0f, gapC_ = 0.0f;        // CLIP Gap taper
    float dslP_ = -1.0f, dslC_ = 0.0f;        // DIODE junction-slew ceiling (embeds fs_ — flush re-keys)
    float rDzP_ = -1.0f, rDzC_ = 0.0f;        // Rectify dz taper (AUDIO thread writes; shapeM only reads)
    float scKp_ = -1.0f, scKc_ = 0.0f;        // SlewClip slope ceiling
    bool  asleep_ = false;
    int   silN_ = 0;
    long long sleptN_ = 0;
    float dcR_ = 0.9987f, autoA_ = 0.9999f, autoZ_ = 0.0f, smth_ = 0.002f;
    bool  hiBypass_ = true;    // Hi Cut at max = TRUE bypass (an "open" 22 kHz pole still cost -1.1 dB @20k)
    bool  useAdaa_  = false;   // fb337 — RETIRED: superseded by adaaOn_ (per-sample, whitelist-gated inside the 2× region). Kept so old notes still point somewhere.

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
