#pragma once

#include <cmath>
#include <random>
#include <vector>

//==============================================================================
// TapeProcessor: Authentic cassette tape emulation for one channel
//
// Saturation modeled after transformer + tape hybrid topology:
//   - Integrator-Saturator-Differentiator (transformer core model)
//   - Asymmetric waveshaping for even harmonics (2nd = warmth)
//   - Airwindows-inspired dynamic application (saturates body, passes transients)
//   - Auto gain compensation (more drive = more harmonics, NOT just louder)
//   - Drive-dependent HF softening + cassette loss filter
//
// Signal chain: Pre-emphasis → Saturation → DC Block → HF Softening
//               → Wow/Flutter (modulated delay) → Hiss
//
// Based on ChowTapeModel, Airwindows, Neve/Studer research, DAFX literature.
//==============================================================================
class TapeProcessor
{
public:
    TapeProcessor() = default;

    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        sr = sampleRate;

        // Delay line for wow/flutter: needs ~30ms headroom for cassette-level wow
        delayBufferSize = static_cast<int>(sr * 0.06) + 4; // 60ms buffer
        delayBuffer.resize(static_cast<size_t>(delayBufferSize), 0.0f);
        std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
        delayWritePos = 0;

        // LFO phases
        wowPhase = 0.0;
        flutterPhase1 = 0.0;
        flutterPhase2 = 0.0;
        driftPhase = 0.0;

        // Saturation filter states
        integratorState = 0.0;
        prevSatInput = 0.0;
        prevSatOutput = 0.0;
        dcBlockState = 0.0;
        dcBlockPrev = 0.0;
        hfSoftenState = 0.0;

        // Pre-emphasis / de-emphasis filter states
        preEmphasisState = 0.0;
        deEmphasisState = 0.0;

        // Hiss filter states
        hissFilterState1 = 0.0f;
        hissFilterState2 = 0.0f;
        prevHissLP = 0.0f;

        // Pre-compute filter coefficients
        // Pre-emphasis: Type I cassette, 120us time constant (~1326 Hz corner)
        const double preEmphFreq = 1.0 / (2.0 * M_PI * 120.0e-6);
        preEmphCoeff = 1.0 / (1.0 + sr / (2.0 * M_PI * preEmphFreq));

        // Transformer integrator: leaky integrator at ~80 Hz
        // This makes bass hit the saturator harder (frequency-dependent saturation)
        const double integratorFreq = 80.0;
        integratorCoeff = 1.0 / (1.0 + sr / (2.0 * M_PI * integratorFreq));

        // DC blocker: HPF at ~10 Hz to remove bias-induced DC offset
        dcBlockCoeff = 1.0 - (2.0 * M_PI * 10.0 / sr);

        // HF softening: lowpass that tightens with drive (~8-14 kHz)
        hfSoftenBaseCoeff = 1.0 / (1.0 + sr / (2.0 * M_PI * 14000.0));
        hfSoftenDrivenCoeff = 1.0 / (1.0 + sr / (2.0 * M_PI * 8000.0));

        rng.seed(static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)));

        // Randomize initial drift offset per instance
        std::uniform_real_distribution<double> phaseDist(0.0, 1.0);
        driftPhase = phaseDist(rng);
    }

    void reset()
    {
        std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
        delayWritePos = 0;
        wowPhase = 0.0;
        flutterPhase1 = 0.0;
        flutterPhase2 = 0.0;
        integratorState = 0.0;
        prevSatInput = 0.0;
        prevSatOutput = 0.0;
        dcBlockState = 0.0;
        dcBlockPrev = 0.0;
        hfSoftenState = 0.0;
        preEmphasisState = 0.0;
        deEmphasisState = 0.0;
        hissFilterState1 = 0.0f;
        hissFilterState2 = 0.0f;
        prevHissLP = 0.0f;
    }

    // Process one sample with tape character
    // wowFlutter: 0-1, saturation: 0-1, hiss: 0-1
    float processSample(float input, float wowFlutter, float saturation, float hiss)
    {
        double output = static_cast<double>(input);

        // ── 1. Saturation engine: transformer + tape hybrid model ──
        if (saturation > 0.001f)
        {
            const double sat = static_cast<double>(saturation);
            const double drive = 1.0 + sat * 4.0;  // 1x to 5x drive

            // --- 1a. Pre-emphasis: boost HF into saturator (cassette record EQ) ---
            preEmphasisState += preEmphCoeff * (output - preEmphasisState);
            const double hfContent = output - preEmphasisState;
            output += hfContent * sat * 0.8;

            // --- 1b. Transformer-style frequency-dependent saturation ---
            // Leaky integrator models transformer primary coil:
            // bass frequencies charge the integrator higher → hit saturator harder
            // treble passes through relatively clean
            integratorState += integratorCoeff * (output * drive - integratorState);

            // Waveshape the integrator output (transformer core saturation)
            const double xformerSat = asymmetricSaturate(integratorState, sat);

            // Differentiator models transformer secondary coil
            const double xformerOut = xformerSat - prevSatOutput;
            prevSatOutput = xformerSat;

            // --- 1c. Direct path: Airwindows-inspired dynamic saturation ---
            // sin(x) waveshaper applied proportionally to signal density
            // This saturates sustained material but lets transients punch through
            const double driven = output * drive;
            const double clamped = std::max(-M_PI_2, std::min(driven, M_PI_2));
            const double sinSaturated = std::sin(clamped);

            // Dynamic application: use signal density to blend in saturation
            // (average of current + previous magnitude determines how much to apply)
            const double density = (std::abs(output) + std::abs(prevSatInput)) * 0.5;
            prevSatInput = output;
            const double applyAmount = std::min(density * drive * 1.5, 1.0);

            // Blend: clean signal where transient, saturated where sustained
            const double directSat = output * (1.0 - applyAmount) + sinSaturated * applyAmount;

            // --- 1d. Mix transformer path + direct path ---
            // Transformer path adds bass warmth + even harmonics from differentiator
            // Direct path adds overall body saturation
            const double xformerMix = 0.3 * sat;  // More transformer character with more drive
            output = directSat * (1.0 - xformerMix) + xformerOut * xformerMix * 3.0;

            // --- 1e. Add even harmonics via 2nd-order polynomial term ---
            // x^2 generates pure 2nd harmonic — the key to analog "warmth"
            const double evenHarmonicAmount = sat * 0.12;
            output += evenHarmonicAmount * output * std::abs(output);

            // --- 1f. Partial gain compensation ---
            // Keep some of the natural gain from saturation (warm volume boost)
            // but prevent it from getting out of control at high drive
            const double gainCompensation = 1.0 / (1.0 + sat * 0.4);
            output *= gainCompensation;

            // --- 1g. DC blocker (removes offset from asymmetric saturation) ---
            const double dcBlockInput = output;
            dcBlockState = dcBlockCoeff * (dcBlockState + dcBlockInput - dcBlockPrev);
            dcBlockPrev = dcBlockInput;
            output = dcBlockState;

            // --- 1h. De-emphasis: cut HF after saturation (cassette playback EQ) ---
            deEmphasisState += preEmphCoeff * (output - deEmphasisState);
            const double deEmphHF = output - deEmphasisState;
            output -= deEmphHF * sat * 0.6;

            // --- 1i. Drive-dependent HF softening ---
            // Real analog gear loses sparkle when pushed hard
            // Interpolate lowpass cutoff: 14kHz (clean) → 8kHz (driven)
            const double hfCoeff = hfSoftenBaseCoeff + sat * (hfSoftenDrivenCoeff - hfSoftenBaseCoeff);
            hfSoftenState += hfCoeff * (output - hfSoftenState);
            // Blend: mostly direct at low sat, more filtered at high sat
            output = output * (1.0 - sat * 0.5) + hfSoftenState * (sat * 0.5);
        }

        // ── 5. Wow/Flutter: modulated delay line with realistic cassette character ──
        if (wowFlutter > 0.001f)
        {
            // Write into delay buffer
            delayBuffer[static_cast<size_t>(delayWritePos)] = static_cast<float>(output);
            delayWritePos = (delayWritePos + 1) % delayBufferSize;

            const double wfAmount = static_cast<double>(wowFlutter);

            // --- Wow LFO: slow, ~0.5-3 Hz, triangle-ish shape ---
            const double wowFreq = 0.4 + 1.6 * wfAmount;
            wowPhase += wowFreq / sr;
            if (wowPhase >= 1.0) wowPhase -= 1.0;
            // Triangle wave for more natural cassette motor wow
            const double wowTriangle = 4.0 * std::abs(wowPhase - 0.5) - 1.0;
            // Add subtle 2nd harmonic for irregularity
            const double wowMod = wowTriangle * 0.8
                + std::sin(4.0 * M_PI * wowPhase) * 0.2;

            // --- Flutter LFO: two summed sines for complex pattern ---
            const double flutter1Freq = 6.0 + 5.0 * wfAmount;  // 6-11 Hz
            const double flutter2Freq = 9.5 + 4.0 * wfAmount;  // 9.5-13.5 Hz (non-harmonic)
            flutterPhase1 += flutter1Freq / sr;
            flutterPhase2 += flutter2Freq / sr;
            if (flutterPhase1 >= 1.0) flutterPhase1 -= 1.0;
            if (flutterPhase2 >= 1.0) flutterPhase2 -= 1.0;
            const double flutterMod = std::sin(2.0 * M_PI * flutterPhase1) * 0.65
                + std::sin(2.0 * M_PI * flutterPhase2) * 0.35;

            // --- Slow drift: very slow random-ish wander, ~0.05 Hz ---
            driftPhase += 0.05 / sr;
            if (driftPhase >= 1.0) driftPhase -= 1.0;
            const double drift = std::sin(2.0 * M_PI * driftPhase + 1.7);

            // --- Modulation depths (in samples) ---
            // Cassette wow: +/-0.2-0.4% speed variation
            // At 44.1kHz: 1ms = 44.1 samples
            // Wow depth: up to ~3ms (132 samples) at full amount
            const double wowDepthSamples = wfAmount * sr * 0.003;    // 0 to 3ms
            const double flutterDepthSamples = wfAmount * sr * 0.0004; // 0 to 0.4ms
            const double driftDepthSamples = wfAmount * sr * 0.0008;  // 0 to 0.8ms

            // Center delay: enough headroom for max modulation
            const double centerDelay = sr * 0.015; // 15ms center

            const double totalDelay = centerDelay
                + wowMod * wowDepthSamples
                + flutterMod * flutterDepthSamples
                + drift * driftDepthSamples;

            // Read from delay buffer with cubic interpolation
            const double readPos = static_cast<double>(delayWritePos) - totalDelay - 1.0;
            output = static_cast<double>(readDelayCubic(readPos));
        }

        // ── 6. Hiss: shaped noise for cassette character ──
        if (hiss > 0.001f)
        {
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            const float noise = dist(rng);

            // Two-pole bandpass for cassette hiss character (emphasis ~3-10 kHz)
            // First stage: lowpass at ~12kHz
            const float lpAlpha = 0.45f; // ~12kHz at 44.1kHz
            hissFilterState1 = lpAlpha * hissFilterState1 + (1.0f - lpAlpha) * noise;

            // Second stage: highpass at ~2kHz (subtract lowpassed version)
            const float hpAlpha = 0.92f; // ~2kHz at 44.1kHz
            hissFilterState2 = hpAlpha * (hissFilterState2 + hissFilterState1 - prevHissLP);
            prevHissLP = hissFilterState1;

            // Level: cassette hiss is typically -50 to -60 dB below signal
            // hiss param 0-1 maps linearly: 5%=subtle, 50%=moderate, 100%=full character
            const float hissLevel = hiss * 0.0008f;
            output += static_cast<double>(hissFilterState2 * hissLevel);
        }

        return static_cast<float>(output);
    }

private:
    // Asymmetric saturation curve for transformer core model
    // Positive half uses softer erf() curve, negative uses harder tanh()
    // This asymmetry generates even harmonics (2nd, 4th) = analog warmth
    static double asymmetricSaturate(double x, double satAmount)
    {
        const double bias = satAmount * 0.06; // Slight DC bias shifts operating point
        const double biased = x + bias;

        if (biased >= 0.0)
        {
            // Positive half: gentler erf() curve (tube-like soft onset)
            return std::erf(biased * 0.8);
        }
        else
        {
            // Negative half: harder tanh() curve (slightly more compressed)
            return std::tanh(biased * 1.1);
        }
    }

    // Cubic (Hermite) interpolation for smoother pitch modulation
    float readDelayCubic(double position) const
    {
        double pos = position;
        while (pos < 0.0)
            pos += static_cast<double>(delayBufferSize);

        const int idx1 = static_cast<int>(pos) % delayBufferSize;
        const int idx0 = (idx1 - 1 + delayBufferSize) % delayBufferSize;
        const int idx2 = (idx1 + 1) % delayBufferSize;
        const int idx3 = (idx1 + 2) % delayBufferSize;

        const float frac = static_cast<float>(pos - std::floor(pos));

        const float y0 = delayBuffer[static_cast<size_t>(idx0)];
        const float y1 = delayBuffer[static_cast<size_t>(idx1)];
        const float y2 = delayBuffer[static_cast<size_t>(idx2)];
        const float y3 = delayBuffer[static_cast<size_t>(idx3)];

        // Hermite interpolation
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    double sr = 44100.0;

    // Delay line for wow/flutter
    std::vector<float> delayBuffer;
    int delayBufferSize = 0;
    int delayWritePos = 0;

    // LFO state
    double wowPhase = 0.0;
    double flutterPhase1 = 0.0;
    double flutterPhase2 = 0.0;
    double driftPhase = 0.0;

    // Saturation filter coefficients
    double preEmphCoeff = 0.0;
    double integratorCoeff = 0.0;
    double dcBlockCoeff = 0.0;
    double hfSoftenBaseCoeff = 0.0;
    double hfSoftenDrivenCoeff = 0.0;

    // Saturation filter states
    double integratorState = 0.0;
    double prevSatInput = 0.0;
    double prevSatOutput = 0.0;
    double dcBlockState = 0.0;
    double dcBlockPrev = 0.0;
    double hfSoftenState = 0.0;
    double preEmphasisState = 0.0;
    double deEmphasisState = 0.0;

    // Hiss state
    float hissFilterState1 = 0.0f;
    float hissFilterState2 = 0.0f;
    float prevHissLP = 0.0f;

    std::mt19937 rng;
};
