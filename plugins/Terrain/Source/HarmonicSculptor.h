#pragma once

#include <cmath>
#include <algorithm>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
// HarmonicSculptor.h — Terrain Studio Machine v2.0 saturation engine.
//
// Three-knob design (visible labels in JS: SCULPT / DRIVE / TIMBRE).
// Each knob is a fundamentally different kind of distortion living in a
// distinct frequency range, so the three never mask each other:
//
//   SCULPT (0..1)      — BASS knob. Low-shelf pre-emphasis (lows ×1..2.5)
//                        into asymmetric soft-clip generates warm even+odd
//                        harmonics from the input's bass content. Low
//                        resonance bell at 180 Hz, Q rises with sculpt
//                        for tighter punch. Lives in the lows + lower-
//                        mids — distinct frequency real estate from DRIVE.
//
//   DRIVE  (0..1)      — MID/HIGH cascaded distortion. HF pre-emphasis
//                        (≥2 kHz boosted) → 4×..34× tanh → sin wavefolder
//                        → hard-clip ceiling. Buchla/serge-style synth
//                        fold timbre. Mixed in sqrt(drive)·0.9.
//
//   TIMBRE (-1..+1)    — bipolar with TWO COMPLETELY DIFFERENT engines.
//                        Negative side: sin wavefolder ×1..9 + LP 8k→1.5k
//                        on the post-mix signal. "Buchla West Coast fold"
//                        — fat, weird, harmonically rich.
//                        Positive side: dual resonant SVF peak filters
//                        at 800 Hz (Q 0.5..6) and 2.4 kHz (Q 0.5..8),
//                        plus a mild 16→7-bit crusher. Rings like a
//                        wah/formant/pluck — metallic musical character
//                        with a touch of digital edge.
//                        Centre = balanced.
//
// History notes:
//   - v1-v5 were a frequency-band saturator (warm-fold / presence-crunch
//     / silk-clip). Suffered from crossover phase + gain staging issues.
//   - v6 introduced the harmonic-order split (even via asymmetric clip,
//     odd via symmetric tanh) and is what we keep here.
//   - 2026-05-01 cleanup: removed envelope-follower normalize/denormalize
//     (dominant click source — discontinuous gain factor when env decayed
//     past threshold) and stripped the explicit Chebyshev T2/T4 terms
//     from the even path (their constant terms produced a -0.2 DC step
//     at silence → audible thump). The asymmetric soft-clip alone
//     generates plenty of even harmonics and is DC-free for x = 0.
//   - 2026-05-01 redesign: replaced the WEAVE odd/even crossfade with the
//     parallel-fuzz DRIVE knob (more audibly dramatic). Replaced TILT
//     harmonic-order distribution with bipolar TIMBRE harmonic-emphasis
//     (the previous LP/HP-only TILT was too subtle).
//==============================================================================
class HarmonicSculptor
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;

        timbreLP        .prepare (sampleRate);
        timbreHP        .prepare (sampleRate);
        resonance       .prepare (sampleRate);
        dcBlocker       .prepare (sampleRate);
        driveHfLP       .prepare (sampleRate);
        driveHfLP       .setFreq (2000.0, sampleRate); // fixed crossover for DRIVE's HF pre-emphasis
        sculptLowShelfLP.prepare (sampleRate);
        sculptLowShelfLP.setFreq (200.0, sampleRate);  // fixed crossover for SCULPT's low-shelf pre-emphasis
        timbreRes1      .prepare (sampleRate);
        timbreRes2      .prepare (sampleRate);

        prevOversampleInput = 0.0;
        heldSample          = 0.0;
        sampleHoldCounter   = 0.0;
    }

    void reset()
    {
        timbreLP.reset();
        timbreHP.reset();
        resonance.reset();
        dcBlocker.reset();
        driveHfLP.reset();
        sculptLowShelfLP.reset();
        timbreRes1.reset();
        timbreRes2.reset();
        prevOversampleInput = 0.0;
        heldSample          = 0.0;
        sampleHoldCounter   = 0.0;
    }

    // Process one sample. Knob ranges (in normalised units):
    //   sculpt: 0..1   (bass character — low-shelf into asymmetric soft-clip)
    //   drive:  0..1   (cascaded distortion: tanh → wavefolder → hard-clip)
    //   timbre: -1..+1 (bipolar — wavefold warm side, dual resonant peaks bright side)
    double processSample (double input, double sculpt, double drive, double timbre)
    {
        // 2x oversample for the inner nonlinear stages (sculpt waveshapers,
        // drive cascade, warm-timbre wavefolder). Without it the high-
        // harmonic content from wavefolders aliases hard.
        const double mid = (prevOversampleInput + input) * 0.5;
        prevOversampleInput = input;

        const double out1 = sculptCore (mid,   sculpt, drive, timbre);
        const double out2 = sculptCore (input, sculpt, drive, timbre);
        double processed = (out1 + out2) * 0.5;

        // Bright-side TIMBRE: a SUBTLE bit-crusher for digital edge,
        // applied post-oversample at native SR. The previous version also
        // had a sample-and-hold SR-reducer here, which combined with the
        // aggressive HP that used to live in sculptCore would silence the
        // signal at +100% (HP killed lows → bit-crush rounded the small
        // residue to zero → S&H froze zero). Removed.
        // Now: 16 bits at bright=0 → 7 bits at bright=1. 7-bit gives
        // 64 visible amplitude steps — audible "digital character" but
        // never quantises a typical signal to silence. The bell-curve
        // resonant ring (handled inside sculptCore) is the dominant
        // character; this just adds a touch of grit on top.
        if (timbre > 0.005)
        {
            const double bright = timbre;
            const int    bits   = std::max (7, (int) std::round (16.0 - bright * 9.0));
            const double levels = std::pow (2.0, (double) (bits - 1));
            processed = std::round (processed * levels) / levels;
        }

        return processed;
    }

private:
    double sculptCore (double x, double sculpt, double drive, double timbre)
    {
        // ── SCULPT path: BASS character knob ──
        // Pre-emphasises lows BEFORE the asymmetric soft-clip so the
        // generated harmonics are predominantly low-mid (the input's bass
        // content gets amplified, then waveshaped, producing 2nd/3rd/4th
        // harmonic content in the lower-mid range). Combined with a low
        // resonance bell at 180 Hz, SCULPT now occupies the lows + lower-
        // mids — distinct frequency real estate from DRIVE (which pre-
        // emphasises ≥2 kHz). User wanted "more boom, more bass" character
        // and the two knobs to stop fighting for the same mids.
        const double xLow      = sculptLowShelfLP.process (x);
        const double xHigh     = x - xLow;
        const double lowBoost  = 1.0 + sculpt * sculpt * 1.5;        // 1x..2.5x on lows
        const double sculptIn  = xLow * lowBoost + xHigh;            // dry-balanced low-shelf

        const double sculptGain   = 1.0 + sculpt * sculpt * 1.0;     // 1x..2x drive
        const double sculptDriven = sculptIn * sculptGain;
        const double evenH = generateEven (sculptDriven);
        const double oddH  = generateOdd  (sculptDriven);
        double combined = evenH + oddH;

        // Low resonance bell at 180 Hz — Q + gain track SCULPT for tighter
        // boom as you turn up. Moved from 2.5 kHz (which was stepping on
        // DRIVE's mid range) down to 180 Hz where it gives real bass
        // weight without conflicting.
        resonance.setParams (180.0, 0.5 + sculpt * 1.8, 1.0 + sculpt * 0.5, sr);
        combined = resonance.process (combined);

        // Mix SCULPT harmonics into the dry (additive, sculpt² curve).
        double output = x + combined * sculpt * sculpt * 1.5;

        // ── DRIVE path: cascaded aggressive distortion ──
        // Three stages in series produce real "distortion" character (not
        // just hotter saturation): heavy pre-emphasis pushes the upper
        // frequencies into a brutal tanh, then a sine wavefolder bends
        // the result to generate dense odd-harmonic content (Buchla/serge-
        // style synth fold), then a hard-clip ceiling adds square-wave
        // edges. Each stage compounds harmonics on harmonics — the result
        // sounds nothing like the smooth SCULPT warmth.
        if (drive > 0.001)
        {
            // Stage 0: HF pre-emphasis — drive's "edge" lives in the highs.
            const double lp  = driveHfLP.process (x);
            const double hf  = x - lp;
            const double pre = x + hf * (0.5 + drive * 2.5);          // up to ~+18 dB above 2 kHz

            // Stage 1: brutal tanh — 4× to 34× drive into the soft clipper.
            const double tanhDrive = 4.0 + drive * 30.0;
            double fuzzed = std::tanh (pre * tanhDrive);

            // Stage 2: sine wavefolder — sin(x · foldDrive). When foldDrive
            // is large the argument crosses multiple sine periods per
            // input sample → enormous odd-harmonic content. This is the
            // sound the user means by "really distorted."
            const double foldDrive = 1.0 + drive * 4.0;               // 1..5
            fuzzed = std::sin (fuzzed * foldDrive) * 0.85;

            // Stage 3: hard-clip ceiling — adds the final square-wave edge.
            fuzzed = std::max (-1.0, std::min (1.0, fuzzed * (1.0 + drive * 1.5)));

            // Mix in with sqrt curve so low-knob settings are still audible
            // (user complaint: "right now I got these up to 100 and there's
            // no real distortion" — the previous drive² curve buried the
            // low end of the sweep). sqrt also blooms gracefully.
            output += fuzzed * std::sqrt (drive) * 0.9;
        }

        // ── TIMBRE warm side: sine wavefolder on the WHOLE output ──
        // Negative TIMBRE folds the entire post-mix signal through a sine
        // wavefolder. At -1 the signal folds 8+ times → massive harmonic
        // content with a warm character (sine folds are smoother than
        // mirror folds). The lowpass tames the harshest peaks. This is a
        // genuinely DIFFERENT distortion type from SCULPT/DRIVE — it has
        // a "synthesised / Buchla West Coast" quality.
        // (Bright-side bit-crush + SR-reduce is handled in processSample
        //  at native SR — see the comment there for why.)
        if (timbre < -0.005)
        {
            const double warm     = -timbre;                          // 0..1
            const double foldDrv  = 1.0 + warm * 8.0;                 // 1..9 (lots of folds)
            output = std::sin (output * foldDrv) * 0.85;

            timbreLP.setFreq (8000.0 - warm * 6500.0, sr);            // 8k → 1.5k
            output = timbreLP.process (output);
        }
        else if (timbre > 0.005)
        {
            // BRIGHT side: dual resonant peak filters — "wah / formant /
            // pluck" character. Q sweeps from gentle (0.5 = bandwidth)
            // up to extreme (6 / 8 = razor-narrow ringing). At +100% the
            // peaks ring like a vocal formant on every sample, giving a
            // metallic/musical "bell curve" character — completely
            // different from the negative side's wavefolder.
            //
            // Replaced the previous HP @ 1700 Hz which was killing the low
            // end and combining with the (also-removed) sample-and-hold
            // to silence the signal at +100%.
            const double bright = timbre;                            // 0..1
            timbreRes1.setParams ( 800.0, 0.5 + bright * 6.0, 1.0 + bright * 1.0, sr);
            output = timbreRes1.process (output);
            timbreRes2.setParams (2400.0, 0.5 + bright * 8.0, 1.0 + bright * 0.8, sr);
            output = timbreRes2.process (output);
        }

        // DC blocker — insurance.
        output = dcBlocker.process (output);

        // Soft limiter — tanh keeps the now-much-hotter signal musical.
        output = std::tanh (output * 0.95);

        // Gain compensation — three knobs all add energy. Scale by total
        // contribution so cranking everything to 100 doesn't slam the
        // limiter into mush.
        const double outGain = 1.0 / (1.0 + sculpt * 0.3 + drive * 0.5 + std::abs (timbre) * 0.4);
        output *= outGain;

        // NaN/Inf guard — single non-finite sample = catastrophic mix click.
        if (! std::isfinite (output)) output = 0.0;
        return output;
    }

    //==========================================================================
    // ── Even harmonic generator ──
    // Asymmetric waveshaper only. Generates 2nd / 4th / 6th harmonic content
    // organically via the f(x) ≠ -f(-x) asymmetry. Warm/musical/tube-like.
    //
    // The previous version added explicit Chebyshev T2 = 2x² - 1 and
    // T4 = 8x⁴ - 8x² + 1 reinforcement. Those polynomials have CONSTANT
    // terms (T2(0) = -1, T4(0) = +1) → the even path produced a sustained
    // -0.2 DC output for x = 0. While playing AC content the constant
    // averages out, but at the moment input drops to silence the output
    // STEPS from "AC riding around 0" to "constant -0.2", and the 10 Hz
    // DC blocker takes ~16 ms to absorb the step → audible thump after
    // every release. Removed entirely. The asymmetric soft-clip alone
    // generates plenty of even-harmonic content and is DC-free at x = 0
    // (shaped(0) = 0, so the whole expression collapses to 0).
    //==========================================================================
    static double generateEven (double x)
    {
        // Asymmetric soft clip: positive and negative halves shaped differently.
        // The asymmetry between coefficients 0.33 and 0.17 is what creates
        // even-order harmonic content. f(x) ≠ -f(-x) → even harmonics.
        double shaped;
        if (x >= 0.0)
            shaped = x - (x * x * x * 0.33);
        else
            shaped = x - (x * x * x * 0.17);

        // Subtract a scaled copy of the input to leave (mostly) just the
        // generated harmonic content. The 0.7 factor cancels most of the
        // fundamental; what remains is dominated by even-order products.
        // Boost ~1.6× to roughly compensate for the removed Chebyshev
        // reinforcement so perceived harmonic richness stays similar.
        return (shaped - (x * 0.7)) * 1.6;
    }

    //==========================================================================
    // ── Odd harmonic generator ──
    // Symmetric waveshaper + Chebyshev T3/T5 reinforcement.
    // Generates 3rd, 5th, 7th harmonic content. Sounds edgy/aggressive/transistor-like.
    //==========================================================================
    static double generateOdd (double x)
    {
        // Symmetric tanh — both halves shaped identically. f(x) = -f(-x) → odd only.
        const double shaped = std::tanh (x * 3.0);

        const double oddContent = shaped - (x * 0.6);

        // Chebyshev T3 (3rd harmonic = octave + fifth) and T5 (5th = two oct + third).
        const double xn  = std::max (-1.0, std::min (x, 1.0));
        const double xn2 = xn * xn;
        const double xn3 = xn2 * xn;
        const double xn5 = xn3 * xn2;
        const double h3 = 4.0  * xn3 - 3.0  * xn;                        // T3
        const double h5 = 16.0 * xn5 - 20.0 * xn3 + 5.0 * xn;            // T5

        return oddContent * 0.5 + h3 * 0.35 + h5 * 0.15;
    }

    //==========================================================================
    // Helper DSP structs
    //==========================================================================

    // One-pole lowpass with runtime cutoff update.
    struct OnePoleLP
    {
        double state = 0.0;
        double alpha = 0.0;

        void prepare (double /*sampleRate*/) { state = 0.0; alpha = 0.0; }
        void reset() { state = 0.0; }

        void setFreq (double freqHz, double sampleRate)
        {
            alpha = 1.0 - std::exp (-2.0 * M_PI * freqHz / sampleRate);
        }

        double process (double input)
        {
            state += alpha * (input - state);
            return state;
        }
    };

    // One-pole highpass = input minus lowpass.
    struct OnePoleHP
    {
        double state = 0.0;
        double alpha = 0.0;

        void prepare (double /*sampleRate*/) { state = 0.0; alpha = 0.0; }
        void reset() { state = 0.0; }

        void setFreq (double freqHz, double sampleRate)
        {
            alpha = 1.0 - std::exp (-2.0 * M_PI * freqHz / sampleRate);
        }

        double process (double input)
        {
            state += alpha * (input - state);
            return input - state;
        }
    };

    // Andrew Simper's TPT State Variable Filter, configured as a
    // peak/bell EQ. Supports per-sample coefficient updates cheaply.
    struct SVFPeak
    {
        double ic1eq = 0.0, ic2eq = 0.0;
        double a1 = 0.0, a2 = 0.0, a3 = 0.0;
        double peakBoost = 0.0;

        void prepare (double /*sampleRate*/) { ic1eq = 0.0; ic2eq = 0.0; }
        void reset() { ic1eq = 0.0; ic2eq = 0.0; }

        void setParams (double cutoffHz, double Q, double peakGain, double sampleRate)
        {
            const double g = std::tan (M_PI * cutoffHz / sampleRate);
            const double k = 1.0 / Q;
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
            peakBoost = peakGain - 1.0; // additive boost over flat
        }

        double process (double input)
        {
            const double v3 = input - ic2eq;
            const double v1 = a1 * ic1eq + a2 * v3;
            const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
            ic1eq = 2.0 * v1 - ic1eq;
            ic2eq = 2.0 * v2 - ic2eq;
            // Peak EQ: dry + (peakGain-1) * bandpass(v1)
            return input + peakBoost * v1;
        }
    };

    // DC blocker (10Hz HP).
    struct DCBlocker
    {
        double state = 0.0, prev = 0.0, coeff = 0.0;

        void prepare (double sampleRate)
        {
            coeff = 1.0 - (2.0 * M_PI * 10.0 / sampleRate);
            state = 0.0;
            prev = 0.0;
        }

        void reset() { state = 0.0; prev = 0.0; }

        double process (double input)
        {
            state = coeff * (state + input - prev);
            prev = input;
            return state;
        }
    };

    OnePoleLP   timbreLP;
    OnePoleHP   timbreHP;
    OnePoleLP   driveHfLP;        // crossover for DRIVE's parallel-fuzz HF emphasis
    OnePoleLP   sculptLowShelfLP; // crossover for SCULPT's low-shelf pre-emphasis (the "bass knob")
    SVFPeak     resonance;        // SCULPT's low resonance bell (the "boom")
    SVFPeak     timbreRes1;       // bright-TIMBRE first resonant peak (~800 Hz)
    SVFPeak     timbreRes2;       // bright-TIMBRE second resonant peak (~2.4 kHz)
    DCBlocker   dcBlocker;

    double sr = 44100.0;
    double prevOversampleInput = 0.0;

    // Bright-TIMBRE sample-and-hold state (per-instance = per-channel).
    // Held outside the 2x-oversample loop so SR-reduction operates at
    // native rate.
    double heldSample        = 0.0;
    double sampleHoldCounter = 0.0;
};
