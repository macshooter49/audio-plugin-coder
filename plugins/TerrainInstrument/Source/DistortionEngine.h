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
       * The other 21 modes (2 of 23 live here: Soft Clip, Hard Clip).
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
            rngC_[c] = 0x12345678u;
        }
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
        if (i == 1) { hiBypass_ = (v >= 0.999f); hiT_ = onePole (600.0f * std::pow (36.67f, v)); }
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
        hiT_ = onePole (600.0f * std::pow (36.67f, v));
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
            case FAM_FOLD:    kneeE_ = 0.5f;    biasE_ = (kneeC_ * 2.0f - 1.0f) * 1.2f; asymE_ = 0.0f;      break;
            case FAM_SHAPER:  kneeE_ = 0.5f;    biasE_ = pC_[5] * 2.0f - 1.0f; asymE_ = 0.0f;               break;
            case FAM_ANALOG:  kneeE_ = 0.5f;    biasE_ = kneeC_ * 2.0f - 1.0f; asymE_ = 0.0f;               break;
            default:          kneeE_ = 0.0f;    biasE_ = 0.0f;                 asymE_ = 0.0f;               break;
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
            // ── WIDTH = M/S DRIVE BALANCE. NOT a widener. 0 = only the MID is driven and the sides
            // pass clean · centre = matched · max = sides driven harder. The width SLOT is per family:
            // P4 for CLIP/DIODE/SHAPER, P8 for FOLD, none for ANALOG (always matched).
            const float w01 = (fam == FAM_FOLD) ? pC_[7] : (fam == FAM_ANALOG ? 0.5f : pC_[3]);
            const float wS  = w01 * 2.0f;
            const float dM  = driveC_;
            const float dS  = 1.0f + (driveC_ - 1.0f) * wS;   // linear-from-unity, no pow per sample

            const float m = 0.5f * (inL + inR), s = 0.5f * (inL - inR);
            const float ym = one (m, 0, dM);
            const float ys = one (s, 1, dS);
            outL = ym + ys;
            outR = ym - ys;
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
        // LOW CUT is PRE the shaper — it decides what is allowed to hog the curve at all.
        loCutZ_[c] += (x - loCutZ_[c]) * loC_;
        x -= loCutZ_[c];

        // EMPHASIS (pre). The inverse runs after the shaper; the pair does NOT cancel because a
        // nonlinearity sits between them — that non-cancellation IS the effect.
        if (std::fabs (emphC_) > 1.0e-4f)
        {
            emphZ_[c] += (x - emphZ_[c]) * kEmphCoef;
            x = emphZ_[c] + (x - emphZ_[c]) * (1.0f + emphC_ * 2.0f);   // tilt: lift/cut the highs
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
        const Family fam = family();

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
        const float gap = (fam == FAM_CLIP) ? pC_[5] * 0.5f : 0.0f;
        auto deadzone = [gap] (float v) noexcept -> float
        {
            if (gap <= 1.0e-5f) return v;
            const float a = std::fabs (v);
            return (a <= gap) ? 0.0f : ((v < 0.0f) ? (v + gap) : (v - gap));
        };

        // ── FEEDBACK (CLIP P8) / SNARL (DIODE P8) — one-sample NONLINEAR recursion, the same
        // physical mechanism in both families (Fuzz Face / minor loop). Exactly off at 0; above
        // ~0.75 it self-oscillates and screams. DIODE reaches 0.98 — the BIBO ceiling, deliberately.
        const float fbA = (fam == FAM_CLIP)  ? pC_[7] * 0.95f
                        : (fam == FAM_DIODE) ? pC_[7] * 0.98f : 0.0f;

        // ── DIODE DEAD ZONE (P6) — POST-drive (absolute threshold on the driven signal, so Drive
        // and Dead Zone genuinely FIGHT — that fight is the instrument). Diode 2's class-B gap is
        // its core identity, so it keeps a floor even at P6 = 0.
        float dzU = (fam == FAM_DIODE) ? pC_[5] * 3.0f : 0.0f;
        // fb322 — Diode 2's class-B floor rescaled for the unity law: 0.5 was sized for the old
        // kPreGain-8 scale and swallowed the whole 0.2-peak bus signal (near-silence at low drive).
        if (mode_ == Diode2) dzU = juce::jmax (0.15f, dzU);
        auto dzPost = [dzU] (float v) noexcept -> float
        {
            if (dzU <= 1.0e-5f) return v;
            return (v >= 0.0f) ? juce::jmax (0.0f, v - dzU) : juce::jmin (0.0f, v + dzU);
        };

        zsGate_ = 2.5e-4f * kPreGain * dEff;   // Zero-Square's −72 dBFS stability gate, input-referred

        const float dcOff = shapeM (dzPost (biasE_ * dEff));
        const float fbIn  = (fbA > 1.0e-5f) ? fbA * fbZ_[c] : 0.0f;
        // A then B = chronological at the 2× rate, so stateful shapers (Slew Clip, DIODE slew) are exact.
        float yA = shapeS (dzPost ((deadzone (pA * kPreGain) + biasE_ + fbIn) * dEff), c) - dcOff;
        float yB = shapeS (dzPost ((deadzone (pB * kPreGain) + biasE_ + fbIn) * dEff), c) - dcOff;

        // ── DIODE SLEW (P7) — junction-capacitance edge limiter, post-shaper, inside the OS region.
        // Only fast edges are touched — deliberately NOT a low-pass.
        if (fam == FAM_DIODE && pC_[6] > 1.0e-3f)
        {
            const float sU = 0.25f * pC_[6] * 1000.0f / (float) (fs_ * 2.0) * kPreGain;
            yA = slewZ_[c] + juce::jlimit (-sU, sU, yA - slewZ_[c]); slewZ_[c] = yA;
            yB = slewZ_[c] + juce::jlimit (-sU, sU, yB - slewZ_[c]); slewZ_[c] = yB;
        }
        if (fbA > 1.0e-5f) fbZ_[c] = flushDenorm (0.5f * (yA + yB));

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

        // DE-EMPHASIS (post) — the matching inverse.
        if (std::fabs (emphC_) > 1.0e-4f)
        {
            deEmphZ_[c] += (y - deEmphZ_[c]) * kEmphCoef;
            y = deEmphZ_[c] + (y - deEmphZ_[c]) / (1.0f + emphC_ * 2.0f);
        }

        // HI CUT is POST — it tames fizz WITHOUT changing what distorted.
        // 🔑 TRUE BYPASS AT MAX. A "fully open" 22 kHz one-pole still measured −1.10 dB at 20 kHz
        // (hf_probe.cpp). Max wants NOTHING taking the top end away, so at the top of the knob the
        // filter is skipped entirely rather than merely opened wide.
        if (hiBypass_) { hiCutZ_[c] = y; }
        else { hiCutZ_[c] += (y - hiCutZ_[c]) * hiC_; y = hiCutZ_[c]; }

        // TONE — post tilt around ~700 Hz. Its own state: the old version referenced hiCutZ_ AFTER
        // y had already been assigned from it, so the correction term was identically zero.
        if (std::fabs (toneC_) > 1.0e-4f)
        {
            toneZ_[c] += (y - toneZ_[c]) * kToneCoef;                 // lows
            const float hi = y - toneZ_[c];                            // highs
            y = toneZ_[c] * (1.0f - toneC_ * 0.8f) + hi * (1.0f + toneC_ * 0.8f);
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

    /** The memoryless transfer curve — also used for exact DC removal (dcOff = shapeM(f(bias))). */
    float shapeM (float u) noexcept
    {
        switch (mode_)
        {
            default:
            case SoftClip:  return softClipF (u, kneeE_);
            case HardClip:  return hardClipF (u, kneeWidth());

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
                // Shunt pair. 🔑 FALLING THRESHOLD (§2.5): V = V0·(1 − 0.85·d) — a fixed threshold
                // leaves the first third of Drive dead on normal material (the fb286 lesson).
                // Asym (SIG) = threshold imbalance between the two directions — the real mixed-pair.
                // fb322 MAKEUP (Max: "diode drives are way too quiet — drive should turn the volume
                // UP"): the falling threshold was shrinking the OUTPUT RAIL itself (ceiling ±T → ±0.1
                // at high drive → post-trim buried it below the dry bus). Normalise by the BASE
                // threshold so drive digs DEEPER at constant rail — and keep the Tp/Tn RATIO, so the
                // Asym imbalance stays audible as unequal rail heights (= the even harmonics).
                const float T  = juce::jmax (0.08f, 0.7f * (1.0f - 0.85f * drive01_));
                const float Tp = juce::jmax (0.03f, T * (1.0f + 0.6f * asymE_));
                const float Tn = juce::jmax (0.03f, T * (1.0f - 0.6f * asymE_));
                const float w  = 2.0f * kneeE_;
                return (u >= 0.0f) ? (Tp / T) * hardClipF (u / Tp, w)
                                   : (Tn / T) * hardClipF (u / Tn, w);
            }

            case Diode2:
            {
                // Series / class-B. Its dead-zone identity is applied FAMILY-WIDE in one() (P6, with
                // a floor for this mode) — what remains here is the post-gap clip. The gap makes it
                // the device's only EXPANDER: crest RISES with Dead Zone where Diode 1's falls.
                // fb322 — MAKEUP for the gap: the shards lost the gap's amplitude, so the mode buried
                // itself as Dead Zone grew ("way too quiet"). Capped ×4 (+12 dB) so Auto never chases
                // silence into a hiss wall (the spec's cap, applied at the source).
                const float mk = juce::jmin (4.0f, 1.0f + pC_[5] * 3.0f);
                return hardClipF (u, 2.0f * kneeE_) * mk;
            }

            case Asym:
                // Bias injection into a MATCHED pair — the curve stays symmetric, only where you SIT
                // on it moves: nearly clean when quiet, violently lopsided when loud (the tube-like
                // flavour). fb322: the injection rides the PRE-DRIVE bias path (biasE_, resolved per
                // family) so it scales with drive and stays audible at every setting — a post-drive
                // "+1.5" shift was swamped and the knob read dead (Max). dcOff removes the offset.
                return hardClipF (u, 2.0f * kneeE_);

            case Rectify:
            {
                // SIG = rectification amount a ∈ 0..2 (centre = full-wave). At a = 2 the negative
                // lobe is inverted AND TRIPLED (over-rectification) — half the drama lives between
                // 1 and 2, which is exactly where "the correct rectifier" stops. P6 gates the octave
                // (the Octavia trick: it only speaks above the threshold), then into a hot clip.
                const float a = asymE_ + 1.0f;
                float v = (1.0f - a) * u + a * std::fabs (u);
                const float dz = pC_[5] * 1.5f;
                if (dz > 1.0e-4f) v = (v >= 0.0f) ? juce::jmax (0.0f, v - dz)
                                                  : juce::jmin (0.0f, v + dz);
                return hardClipF (v, 2.0f * kneeE_);
            }

            case LinearFold: return foldIter (u);
            case WestCoast:
                // Buchla 259's parallel-stage idea: three detuned fold taps, summed. Each tap rides
                // the same Stages/Spacing/Rebound/Corner ladder, so the back-8 voices all of it.
                return 0.50f * foldIter (u)
                     + 0.35f * foldIter (u * 1.41421356f)
                     + 0.15f * foldIter (u * 2.0f);
            case SineFold:
            {
                // sin() of the driven signal — Bessel comb, C∞ (no corners ⇒ the gentlest aliaser in
                // the family). Stages (P4) clamps the reach; Corner (P7) blends the triangle cusp IN
                // (the 'Chime' trick — brighter without adding a single fold).
                const int   st = 1 + (int) (pC_[3] * 31.0f + 0.5f);
                const float v  = juce::jlimit (-(float) (2 * st), (float) (2 * st), u);
                float yv = std::sin (1.5707963f * v);
                const float crn = pC_[6];
                if (crn > 1.0e-3f) yv = (1.0f - crn) * yv + crn * foldIter (v);
                return yv;
            }
        }
    }

    /** The real shaping path — adds state where a mode needs it; everything else falls to shapeM. */
    float shapeS (float u, int c) noexcept
    {
        if (mode_ == SlewClip)
        {
            // The output cannot move faster than a fixed slope — distortion as a function of dx/dt,
            // not x. Knee = the slope ceiling (log taper, 0.0005 → 0.08 FS/sample input-referred;
            // the bottom decade is deliberately sludge). Drive asks for more slope than deliverable
            // ⇒ triangle. State clamped at the rails so direction changes stay immediate.
            const float sU = 0.0005f * std::pow (160.0f, kneeE_) * kPreGain * 0.5f;   // per 2×-sample
            float y = slewZ_[c] + juce::jlimit (-sU, sU, u - slewZ_[c]);
            y = juce::jlimit (-1.35f, 1.35f, y);
            slewZ_[c] = y;
            return y;
        }
        return shapeM (u);
    }

    /** Iterative wavefolder — the FOLD family's ladder, all four back knobs live:
        Stages (P4) = ladder length 1..32 · Spacing (P5) = argument power-warp 0.5..2.2 ·
        Rebound (P6) = reflection gain per fold 0.4..1.6 (>1 = no hardware does this) ·
        Corner (P7) = cusp rounding (sin-blend ≈ half the alias energy at the same fold count). */
    float foldIter (float u) noexcept
    {
        const int   stages = 1 + (int) (pC_[3] * 31.0f + 0.5f);
        const float spacP  = 0.5f + pC_[4] * 1.7f;
        const float reb    = 0.4f + pC_[5] * 1.2f;
        float v = u;
        const float a0 = std::fabs (v);
        if (a0 > 1.0e-9f && std::fabs (spacP - 1.0f) > 1.0e-3f)
            v = (v < 0.0f ? -1.0f : 1.0f) * std::pow (a0, spacP);
        float g = 1.0f; int k = 0;
        while (std::fabs (v) > 1.0f && k < stages)
        {
            v = (v > 0.0f) ? (2.0f - v) : (-2.0f - v);   // reflect at the rail
            g *= reb; ++k;
        }
        if (std::fabs (v) > 1.0f) v = (v > 0.0f ? 1.0f : -1.0f);   // ladder ended ⇒ rail
        v = juce::jlimit (-4.0f, 4.0f, v * g);                     // BIBO for Rebound > 1 runaway
        const float crn = pC_[6];
        if (crn > 1.0e-3f)                                         // sin of a triangle = the rounded triangle
            v = (1.0f - crn) * v + crn * std::sin (1.5707963f * juce::jlimit (-1.0f, 1.0f, v));
        return v;
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
        float u = x * kPreGain * drv * 0.25f;

        const float fbA = pC_[7] * 1.05f;                      // P8 — loop gain DELIBERATELY > 1;
        if (fbA > 1.0e-5f) u += fbA * fbZ_[c];                 //      tanh state is the BIBO bound

        const float sig    = kneeC_;                           // the front Crush axis, raw 0..1
        const float rate01 = (mode_ == Downsample) ? sig : pC_[2];
        const float bits01 = (mode_ == Bitcrush)   ? sig : (mode_ == Downsample ? pC_[2] : 0.0f);
        const float wrap01 = (mode_ == Overflow)   ? sig : 0.0f;

        if (wrap01 > 1.0e-3f)                                  // OVERFLOW — modulo-sawtooth wrap
        {
            const float thr = 1.0f - 0.97f * wrap01;           // 1 → 0.03 (§2.3: the most destructive)
            float v = std::fmod (u + thr, 2.0f * thr);
            if (v < 0.0f) v += 2.0f * thr;
            u = v - thr;
        }
        if (bits01 > 1.0e-3f)                                  // BITCRUSH — §2.5 taper, sub-bit floor
        {
            const float bits = 0.5f + 15.5f * std::pow (1.0f - bits01, 2.6f);
            const float D    = std::pow (2.0f, 1.0f - bits);
            if (pC_[4] > 1.0e-4f) u += (rnd01 (c) + rnd01 (c) - 1.0f) * D * pC_[4] * 2.0f;  // P5 TPDF — RNG untouched at 0
            u = juce::jlimit (-2.0f, 2.0f, D * std::round (u / D));   // clamp ±2.0, NEVER ±1 (sub-bit codes)
        }
        float fsr = (float) fs_;
        if (rate01 > 1.0e-3f)                                  // DOWNSAMPLE — exponential in Hz, fs → 20
        {
            // 🔑 POLARITY: 0 = clean (fs), up = destroy — like every other axis in the device. The
            // first version had it inverted, so the P3 default of 0 silently ran EVERYTHING through
            // a 20 Hz hold — Bitcrush measured 47% THD flat at every level and every bit depth
            // because a 20 Hz ZOH was doing the destroying, not the quantiser. Harness-caught.
            fsr = 20.0f * std::pow ((float) (fs_ / 20.0), 1.0f - rate01);
            float inc = fsr / (float) fs_;
            if (pC_[5] > 1.0e-4f) inc *= 1.0f + pC_[5] * rnd11 (c);          // P6 Jitter — grid instability
            if (c == 1 && pC_[6] > 1.0e-4f) inc *= 1.0f + pC_[6] * 0.13f;    // P7 Spread — clocks diverge; 0 = byte-identical
            holdPh_[c] += inc;
            if (holdPh_[c] >= 1.0f) { holdPh_[c] -= (float) (int) holdPh_[c]; holdV_[c] = u; }
            u = holdV_[c];
        }
        fbZ_[c] = std::tanh (u);
        if (pC_[3] > 1.0e-4f)                                  // P4 Smooth — reconstruction LP @0.45·fs_r
        {
            const float fc = juce::jmax (200.0f, 0.45f * fsr);
            const float kk = onePole (fc + (1.0f - pC_[3]) * (20000.0f - juce::jmin (20000.0f, fc)));
            smZ_[c] += (u - smZ_[c]) * kk;
            u = smZ_[c];
        }
        float y = u;                                           // fb321 — unity law: x·kPreGain·0.25 = x·1 at kPreGain 4
        const float d = y - dcX_[c] + dcR_ * dcY_[c];          // Overflow's asymmetric wraps park off zero
        dcX_[c] = y; dcY_[c] = d; y = d;
        if (! hiBypass_) { hiCutZ_[c] += (y - hiCutZ_[c]) * hiC_; y = hiCutZ_[c]; }
        if (std::fabs (toneC_) > 1.0e-4f)
        {
            toneZ_[c] += (y - toneZ_[c]) * kToneCoef;
            const float hi2 = y - toneZ_[c];
            y = toneZ_[c] * (1.0f - toneC_ * 0.8f) + hi2 * (1.0f + toneC_ * 0.8f);
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
        const float C = (std::fabs (x) <= 1.5f) ? (x - (4.0f / 27.0f) * x * x * x)
                                                : (x > 0.0f ? 1.0f : -1.0f);
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
    /** Hard Clip's knee in absolute units: w = 2*(Knee/100), so 0 is a razor and 2 is fully parabolic. */
    float kneeWidth() const noexcept { return 2.0f * kneeE_; }   // family-resolved knee (fb320)

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
