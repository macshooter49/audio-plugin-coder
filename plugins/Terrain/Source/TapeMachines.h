#pragma once

#include <cmath>
#include <random>
#include <vector>
#include <algorithm>

//==============================================================================
// TapeMachines.h — Three distinct tape machine DSP algorithms for Terrain
//
// StudioMachine:   Warm reel-to-reel. Smooth tanh saturation, single gentle
//                  wow LFO, clean white hiss. Musical and predictable.
//
// CassetteMachine: Lo-fi degraded cassette. Cubic soft clip with asymmetric
//                  harmonics, triple-component wow/flutter, colored mid-range
//                  hiss with motor rumble. Complex and wobbly.
//
// WireMachine:     Experimental broken wire recorder. Asymmetric hard clip
//                  with wavefolder, chaotic multi-component flutter with random
//                  speed jumps, pink noise + crackle + micro-dropouts. Harsh
//                  and unpredictable.
//
// All machines share TapeMachineBase which provides:
//   - Virtual interface (prepare, reset, processSaturation, processWow, processHiss)
//   - DCBlocker, OnePoleLP, OnePoleHP, SmoothRandom utility structs
//   - Shared delay line with cubic Hermite interpolation
//   - onePoleCoeff() helper for filter coefficient calculation
//   - 2x oversampling support via prevOversampleInput
//==============================================================================

//==============================================================================
// Base class: shared utilities and virtual interface
//==============================================================================
class TapeMachineBase
{
public:
    virtual ~TapeMachineBase() = default;

    virtual void prepare(double sampleRate) = 0;
    virtual void reset() = 0;

    // processSaturation: nonlinear waveshaping with 2x oversampling
    // input: audio sample, amount: 0-1 normalized control
    // Returns: processed sample
    virtual double processSaturation(double input, double amount) = 0;

    // processWow: pitch modulation via modulated delay line
    // input: audio sample, amount: 0-1 normalized control
    // Returns: processed sample
    virtual double processWow(double input, double amount) = 0;

    // processHiss: additive noise generation + optional gain modulation
    // amount: 0-1 normalized control
    // audioGainMultiplier: set by reference (1.0 normally, <1.0 for dropouts)
    // Returns: additive noise sample to be summed with signal
    virtual double processHiss(double amount, float& audioGainMultiplier) = 0;

protected:
    double sr = 44100.0;

    //==========================================================================
    // Utility: one-pole lowpass coefficient from frequency
    static double onePoleCoeff(double freqHz, double sampleRate)
    {
        // Standard one-pole: alpha = 1 / (1 + fs / (2*pi*f))
        return 1.0 / (1.0 + sampleRate / (2.0 * M_PI * freqHz));
    }

    //==========================================================================
    // DC Blocker — highpass at ~10 Hz to remove bias-induced DC offset
    struct DCBlocker
    {
        double state = 0.0;
        double prev = 0.0;
        double coeff = 0.0;

        void prepare(double sampleRate)
        {
            coeff = 1.0 - (2.0 * M_PI * 10.0 / sampleRate);
            state = 0.0;
            prev = 0.0;
        }

        void reset()
        {
            state = 0.0;
            prev = 0.0;
        }

        double process(double input)
        {
            state = coeff * (state + input - prev);
            prev = input;
            return state;
        }
    };

    //==========================================================================
    // One-pole lowpass filter
    struct OnePoleLP
    {
        double state = 0.0;
        double alpha = 0.0;

        void setFreq(double freqHz, double sampleRate)
        {
            alpha = onePoleCoeff(freqHz, sampleRate);
        }

        void reset() { state = 0.0; }

        double process(double input)
        {
            state += alpha * (input - state);
            return state;
        }
    };

    //==========================================================================
    // One-pole highpass filter
    struct OnePoleHP
    {
        double prevInput = 0.0;
        double prevOutput = 0.0;
        double alpha = 0.0;

        void setFreq(double freqHz, double sampleRate)
        {
            // HPF alpha: closer to 1.0 = lower cutoff
            alpha = 1.0 / (1.0 + (2.0 * M_PI * freqHz) / sampleRate);
        }

        void reset()
        {
            prevInput = 0.0;
            prevOutput = 0.0;
        }

        double process(double input)
        {
            prevOutput = alpha * (prevOutput + input - prevInput);
            prevInput = input;
            return prevOutput;
        }
    };

    //==========================================================================
    // Smooth random generator — slowly wandering random value
    // Uses filtered white noise for smooth, continuous random modulation
    struct SmoothRandom
    {
        double state = 0.0;
        double target = 0.0;
        double phase = 0.0;
        double rate = 1.0;        // How many times per second to pick a new target
        double smoothing = 0.0;   // One-pole smoothing coefficient
        std::mt19937* rngPtr = nullptr;

        void prepare(double rateHz, double smoothFreq, double sampleRate, std::mt19937& r)
        {
            rngPtr = &r;
            rate = rateHz;
            smoothing = onePoleCoeff(smoothFreq, sampleRate);
            phase = 0.0;
            state = 0.0;
            std::uniform_real_distribution<double> dist(-1.0, 1.0);
            target = dist(*rngPtr);
        }

        void reset()
        {
            state = 0.0;
            target = 0.0;
            phase = 0.0;
        }

        // Returns smoothly wandering value in [-1, 1]
        double next(double sampleRate)
        {
            phase += rate / sampleRate;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                if (rngPtr != nullptr)
                {
                    std::uniform_real_distribution<double> dist(-1.0, 1.0);
                    target = dist(*rngPtr);
                }
            }
            state += smoothing * (target - state);
            return state;
        }
    };

    //==========================================================================
    // Delay line with cubic Hermite interpolation
    // Same algorithm as existing TapeProcessor.h
    struct DelayLine
    {
        std::vector<float> buffer;
        int bufferSize = 0;
        int writePos = 0;

        void prepare(double maxDelaySec, double sampleRate)
        {
            bufferSize = static_cast<int>(sampleRate * maxDelaySec) + 4;
            buffer.resize(static_cast<size_t>(bufferSize), 0.0f);
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }

        void reset()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }

        void write(float sample)
        {
            buffer[static_cast<size_t>(writePos)] = sample;
            writePos = (writePos + 1) % bufferSize;
        }

        // Read with cubic Hermite interpolation at fractional delay
        float readCubic(double delaySamples) const
        {
            double pos = static_cast<double>(writePos) - delaySamples - 1.0;
            while (pos < 0.0)
                pos += static_cast<double>(bufferSize);

            const int idx1 = static_cast<int>(pos) % bufferSize;
            const int idx0 = (idx1 - 1 + bufferSize) % bufferSize;
            const int idx2 = (idx1 + 1) % bufferSize;
            const int idx3 = (idx1 + 2) % bufferSize;

            const float frac = static_cast<float>(pos - std::floor(pos));

            const float y0 = buffer[static_cast<size_t>(idx0)];
            const float y1 = buffer[static_cast<size_t>(idx1)];
            const float y2 = buffer[static_cast<size_t>(idx2)];
            const float y3 = buffer[static_cast<size_t>(idx3)];

            // Hermite interpolation coefficients
            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

            return ((c3 * frac + c2) * frac + c1) * frac + c0;
        }
    };

    //==========================================================================
    // RNG — each machine instance gets its own, seeded from pointer address
    std::mt19937 rng;

    void initRng()
    {
        rng.seed(static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)));
    }

    // Previous input sample for 2x oversampling midpoint interpolation
    double prevOversampleInput = 0.0;
};


//==============================================================================
//==============================================================================
//
//  StudioMachine — Warm reel-to-reel tape
//
//  Saturation: tanh waveshaper, DC bias for even harmonics, 2x oversampled
//  Wow: Single slow LFO with rate drift, gentle and musical
//  Hiss: White noise shaped with high-shelf boost, clean
//
//==============================================================================
//==============================================================================
class StudioMachine : public TapeMachineBase
{
public:
    StudioMachine() = default;

    void prepare(double sampleRate) override
    {
        sr = sampleRate;
        initRng();

        // Saturation filters
        dcBlocker.prepare(sr);
        preEmphasis.setFreq(3000.0, sr);   // HF boost at 3kHz
        deEmphasis.setFreq(8000.0, sr);    // Post rolloff at 8kHz

        // Wow delay line: 30ms buffer (generous for +-8 cents)
        delay.prepare(0.03, sr);

        // Wow LFO smooth random for rate drift
        rateDrift.prepare(0.1, 2.0, sr, rng);

        // Randomize LFO start phase
        std::uniform_real_distribution<double> phaseDist(0.0, 1.0);
        wowPhase = phaseDist(rng);

        // Hiss filters
        hissHiShelf.setFreq(4000.0, sr);    // High-shelf corner
        hissHP.setFreq(200.0, sr);           // HP to remove rumble

        prevOversampleInput = 0.0;
        hissFilterState = 0.0;
    }

    void reset() override
    {
        dcBlocker.reset();
        preEmphasis.reset();
        deEmphasis.reset();
        delay.reset();
        rateDrift.reset();
        wowPhase = 0.0;
        hissHiShelf.reset();
        hissHP.reset();
        prevOversampleInput = 0.0;
        hissFilterState = 0.0;
    }

    //==========================================================================
    // Saturation: tanh waveshaper with DC bias offset for even harmonics
    // 2x oversampled to prevent aliasing
    double processSaturation(double input, double amount) override
    {
        if (amount < 0.001) { prevOversampleInput = input; return input; }

        // 2x oversampling: generate midpoint sample, process both, average
        const double midSample = (prevOversampleInput + input) * 0.5;
        prevOversampleInput = input;

        const double out1 = saturateSample(midSample, amount);
        const double out2 = saturateSample(input, amount);

        return (out1 + out2) * 0.5;
    }

    //==========================================================================
    // Wow: Single slow LFO (0.5-2.5Hz) with smooth random rate drift
    // Max +/-8 cents pitch deviation, 5ms center delay
    double processWow(double input, double amount) override
    {
        if (amount < 0.001) return input;

        delay.write(static_cast<float>(input));

        // Rate drift: smooth random modulation of LFO frequency
        const double drift = rateDrift.next(sr);
        const double wowFreq = 0.5 + 2.0 * amount + drift * 0.3 * amount;

        // Advance wow LFO (sine)
        wowPhase += std::max(wowFreq, 0.1) / sr;
        if (wowPhase >= 1.0) wowPhase -= 1.0;

        const double wowMod = std::sin(2.0 * M_PI * wowPhase);

        // +/-8 cents = +/-0.46% pitch shift
        // At 44.1kHz, 1 cent = ~0.000578 * sampleRate/frequency
        // Using delay modulation: 8 cents at 440Hz ~= 0.21ms
        // Scale by amount and convert to samples
        const double maxDeviationMs = 0.21 * amount;  // max 0.21ms = +/-8 cents
        const double deviationSamples = maxDeviationMs * 0.001 * sr * wowMod;

        // 5ms center delay
        const double centerDelay = sr * 0.005;
        const double totalDelay = centerDelay + deviationSamples;

        // Clamp to valid range
        const double clampedDelay = std::max(1.0, std::min(totalDelay,
            static_cast<double>(delay.bufferSize - 2)));

        return static_cast<double>(delay.readCubic(clampedDelay));
    }

    //==========================================================================
    // Hiss: White noise shaped with high-shelf +3dB at 4kHz, HP at 200Hz
    // Max level coefficient 0.15. No dropouts.
    double processHiss(double amount, float& audioGainMultiplier) override
    {
        audioGainMultiplier = 1.0f;  // Studio: never any dropouts

        if (amount < 0.001) return 0.0;

        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        double noise = dist(rng);

        // High-shelf at 4kHz: +3dB
        // Simple approach: LP the noise, subtract to get HP, boost HP, add back
        const double lpNoise = hissHiShelf.process(noise);
        const double hpNoise = noise - lpNoise;
        // +3dB boost on highs = multiply by ~1.41
        noise = lpNoise + hpNoise * 1.41;

        // Highpass at 200Hz to remove low rumble
        noise = hissHP.process(noise);

        // Level: max coefficient 0.15
        const double level = amount * 0.15;

        return noise * level;
    }

private:
    // Internal saturation function (called twice per sample for 2x oversampling)
    double saturateSample(double input, double amount)
    {
        const double drive = 1.0 + amount * 4.0;  // 1x to 5x drive

        // Pre-emphasis: HF boost at 3kHz
        const double lp = preEmphasis.process(input);
        const double hfContent = input - lp;
        double x = input + hfContent * amount * 0.6;

        // DC bias for even harmonics (asymmetry generates 2nd harmonic = warmth)
        const double bias = 0.05 * amount;
        x = x * drive + bias;

        // tanh waveshaper — smooth, warm compression
        x = std::tanh(x);

        // DC blocker (removes bias offset)
        x = dcBlocker.process(x);

        // Post de-emphasis: rolloff at 8kHz
        const double postLP = deEmphasis.process(x);
        const double postHF = x - postLP;
        x = postLP + postHF * (1.0 - amount * 0.5);

        // Gain compensation: prevent excessive volume increase at high drive
        const double gainComp = 1.0 / (1.0 + amount * 0.5);
        x *= gainComp;

        return x;
    }

    // Saturation filters
    DCBlocker dcBlocker;
    OnePoleLP preEmphasis;
    OnePoleLP deEmphasis;

    // Wow
    DelayLine delay;
    SmoothRandom rateDrift;
    double wowPhase = 0.0;

    // Hiss
    OnePoleLP hissHiShelf;
    OnePoleHP hissHP;
    double hissFilterState = 0.0;
};


//==============================================================================
//==============================================================================
//
//  CassetteMachine — Lo-fi degraded cassette
//
//  Saturation: Cubic soft clip with asymmetric 2nd harmonic injection,
//              pre/post filtering for cassette character, 2x oversampled
//  Wow: Three components (slow + medium + flutter), all with rate drift
//  Hiss: Mid-range colored noise + motor rumble
//
//==============================================================================
//==============================================================================
class CassetteMachine : public TapeMachineBase
{
public:
    CassetteMachine() = default;

    void prepare(double sampleRate) override
    {
        sr = sampleRate;
        initRng();

        // Saturation pre-filters
        dcBlocker.prepare(sr);
        preHP.setFreq(60.0, sr);          // HP at 60Hz
        preLP.setFreq(12000.0, sr);       // LP at 12kHz
        preBell.setFreq(2500.0, sr);      // Bell resonance at 2.5kHz
        postLP.setFreq(8000.0, sr);       // Post-LP starts at 8kHz
        postLPDriven.setFreq(4000.0, sr); // Post-LP darkens to 4kHz at full drive

        // Wow delay line: 40ms buffer for complex modulation
        delay.prepare(0.04, sr);

        // Rate drift generators for each LFO component
        slowDrift.prepare(0.08, 1.5, sr, rng);
        medDrift.prepare(0.12, 2.0, sr, rng);
        flutterDrift.prepare(0.2, 3.0, sr, rng);

        // Randomize LFO start phases
        std::uniform_real_distribution<double> phaseDist(0.0, 1.0);
        slowPhase = phaseDist(rng);
        medPhase = phaseDist(rng);
        flutterPhase = phaseDist(rng);

        // Hiss filters
        hissBell.setFreq(3500.0, sr);     // Sibilant emphasis at 3.5kHz
        hissLP.setFreq(10000.0, sr);      // LP at 10kHz
        hissHP.setFreq(150.0, sr);        // HP at 150Hz
        rumbleLP.setFreq(300.0, sr);      // Motor rumble LP at 300Hz

        prevOversampleInput = 0.0;
    }

    void reset() override
    {
        dcBlocker.reset();
        preHP.reset();
        preLP.reset();
        preBell.reset();
        postLP.reset();
        postLPDriven.reset();
        delay.reset();
        slowDrift.reset();
        medDrift.reset();
        flutterDrift.reset();
        slowPhase = 0.0;
        medPhase = 0.0;
        flutterPhase = 0.0;
        hissBell.reset();
        hissLP.reset();
        hissHP.reset();
        rumbleLP.reset();
        prevOversampleInput = 0.0;
        rumbleState = 0.0;
    }

    //==========================================================================
    // Saturation: Cubic soft clip with asymmetric 2nd harmonic injection
    // Pre: HP 60Hz, LP 12kHz, bell +2.5dB at 2.5kHz
    // Post: LP 8kHz->4kHz (darkens with drive)
    // 2x oversampled
    double processSaturation(double input, double amount) override
    {
        if (amount < 0.001) { prevOversampleInput = input; return input; }

        // 2x oversampling
        const double midSample = (prevOversampleInput + input) * 0.5;
        prevOversampleInput = input;

        const double out1 = saturateSample(midSample, amount);
        const double out2 = saturateSample(input, amount);

        return (out1 + out2) * 0.5;
    }

    //==========================================================================
    // Wow: Three components — slow wow (0.8Hz, 60%), medium wow (2.5Hz, 25%),
    // fast flutter (6.5Hz, 15%). All rates drift +/-10%. Max +/-25 cents.
    // 8ms center delay.
    double processWow(double input, double amount) override
    {
        if (amount < 0.001) return input;

        delay.write(static_cast<float>(input));

        // Rate drift for each component (+/-10% of base rate)
        const double slowDriftVal = slowDrift.next(sr);
        const double medDriftVal = medDrift.next(sr);
        const double flutterDriftVal = flutterDrift.next(sr);

        // Slow wow: 0.8Hz base, triangle wave for natural motor wow
        const double slowRate = 0.8 * (1.0 + slowDriftVal * 0.1);
        slowPhase += slowRate / sr;
        if (slowPhase >= 1.0) slowPhase -= 1.0;
        const double slowMod = 4.0 * std::abs(slowPhase - 0.5) - 1.0; // Triangle

        // Medium wow: 2.5Hz, sine
        const double medRate = 2.5 * (1.0 + medDriftVal * 0.1);
        medPhase += medRate / sr;
        if (medPhase >= 1.0) medPhase -= 1.0;
        const double medMod = std::sin(2.0 * M_PI * medPhase);

        // Fast flutter: 6.5Hz, sine
        const double flutRate = 6.5 * (1.0 + flutterDriftVal * 0.1);
        flutterPhase += flutRate / sr;
        if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        const double flutMod = std::sin(2.0 * M_PI * flutterPhase);

        // Combine: 60% slow, 25% medium, 15% flutter
        const double combined = slowMod * 0.60 + medMod * 0.25 + flutMod * 0.15;

        // +/-25 cents: 25 cents at 440Hz ~= 0.64ms deviation
        const double maxDeviationMs = 0.64 * amount;
        const double deviationSamples = maxDeviationMs * 0.001 * sr * combined;

        // 8ms center delay
        const double centerDelay = sr * 0.008;
        const double totalDelay = centerDelay + deviationSamples;

        const double clampedDelay = std::max(1.0, std::min(totalDelay,
            static_cast<double>(delay.bufferSize - 2)));

        return static_cast<double>(delay.readCubic(clampedDelay));
    }

    //==========================================================================
    // Hiss: White noise with bell +5dB at 3.5kHz, LP 10kHz, HP 150Hz.
    // Plus motor rumble: pink-ish noise LP'd at 300Hz at 0.1x level.
    // Max level coefficient 0.25 (louder than Studio). No dropouts.
    double processHiss(double amount, float& audioGainMultiplier) override
    {
        audioGainMultiplier = 1.0f;  // Cassette: no dropouts

        if (amount < 0.001) return 0.0;

        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        double noise = dist(rng);

        // Bell +5dB at 3.5kHz (sibilant emphasis)
        // LP the noise at bell frequency, extract HF, boost it
        const double bellLP = hissBell.process(noise);
        const double bellHF = noise - bellLP;
        // +5dB = ~1.78x
        noise = bellLP + bellHF * 1.78;

        // LP at 10kHz
        noise = hissLP.process(noise);

        // HP at 150Hz
        noise = hissHP.process(noise);

        // Motor rumble: white noise -> pink approximation -> LP at 300Hz
        const double rumbleNoise = dist(rng);
        // Simple pinking: integrate and leak (one-pole LP at low freq)
        rumbleState = rumbleState * 0.99 + rumbleNoise * 0.01;
        const double rumble = rumbleLP.process(rumbleState) * 0.1;

        // Total noise: main hiss + motor rumble
        const double total = noise + rumble;

        // Level: max coefficient 0.25
        const double level = amount * 0.25;

        return total * level;
    }

private:
    // Internal saturation function for cassette character
    double saturateSample(double input, double amount)
    {
        const double drive = 1.0 + amount * 6.0;  // 1x to 7x drive

        // Pre-filter: HP at 60Hz (remove rumble before saturation)
        double x = preHP.process(input);

        // Pre-filter: LP at 12kHz (cassette bandwidth limit)
        x = preLP.process(x);

        // Bell +2.5dB at 2.5kHz (head bump resonance)
        const double bellLP = preBell.process(x);
        const double bellHF = x - bellLP;
        // +2.5dB = ~1.33x
        x = bellLP + bellHF * 1.33;

        // Apply drive
        x *= drive;

        // Asymmetric 2nd harmonic injection
        // x^2 term creates pure 2nd harmonic, scaled by amount
        const double secondHarmonic = 0.12 * amount * x * std::abs(x);

        // Cubic soft clip: x - x^3/3 (smooth limiting)
        if (x > 1.0) x = 2.0 / 3.0;
        else if (x < -1.0) x = -2.0 / 3.0;
        else x = x - (x * x * x) / 3.0;

        // Add 2nd harmonic after clipping
        x += secondHarmonic;

        // DC blocker
        x = dcBlocker.process(x);

        // Post-LP: blend between 8kHz (clean) and 4kHz (driven)
        const double cleanLP = postLP.process(x);
        const double drivenLP = postLPDriven.process(x);
        x = cleanLP * (1.0 - amount) + drivenLP * amount;

        // Gain compensation
        const double gainComp = 1.0 / (1.0 + amount * 0.6);
        x *= gainComp;

        return x;
    }

    // Saturation filters
    DCBlocker dcBlocker;
    OnePoleHP preHP;
    OnePoleLP preLP;
    OnePoleLP preBell;
    OnePoleLP postLP;
    OnePoleLP postLPDriven;

    // Wow
    DelayLine delay;
    SmoothRandom slowDrift;
    SmoothRandom medDrift;
    SmoothRandom flutterDrift;
    double slowPhase = 0.0;
    double medPhase = 0.0;
    double flutterPhase = 0.0;

    // Hiss
    OnePoleLP hissBell;
    OnePoleLP hissLP;
    OnePoleHP hissHP;
    OnePoleLP rumbleLP;
    double rumbleState = 0.0;
};


//==============================================================================
//==============================================================================
//
//  WireMachine — Experimental/broken wire recorder
//
//  Saturation: Asymmetric hard clipping + wavefolder, harsh/metallic,
//              extreme bandwidth limitation, 2x oversampled
//  Wow: Four chaotic components including random speed jumps,
//       massive +/-50 cents, unpredictable and broken
//  Hiss: Pink noise + crackle impulses + micro-dropouts,
//        hiss knob controls all three proportionally
//
//==============================================================================
//==============================================================================
class WireMachine : public TapeMachineBase
{
public:
    WireMachine() = default;

    void prepare(double sampleRate) override
    {
        sr = sampleRate;
        initRng();

        // Saturation filters
        dcBlocker.prepare(sr);
        // HP 200Hz (two cascaded one-poles for ~12dB/oct)
        preHP1.setFreq(200.0, sr);
        preHP2.setFreq(200.0, sr);
        // LP 6kHz (two cascaded for ~12dB/oct)
        preLP1.setFreq(6000.0, sr);
        preLP2.setFreq(6000.0, sr);
        // Bell +4dB at 1kHz
        preBell.setFreq(1000.0, sr);
        // Post-LP: 4kHz -> 2kHz
        postLP.setFreq(4000.0, sr);
        postLPDriven.setFreq(2000.0, sr);

        // Wow delay line: 80ms buffer for wild modulation
        delay.prepare(0.08, sr);

        // Chaotic smooth random generators
        slowRandom.prepare(0.15, 0.5, sr, rng);    // Very slow drift
        irregWowDrift.prepare(0.3, 1.5, sr, rng);   // Rate drift for irregular wow
        chaoticDrift.prepare(0.5, 2.0, sr, rng);    // Rate drift for chaotic flutter

        // Randomize phases
        std::uniform_real_distribution<double> phaseDist(0.0, 1.0);
        irregPhase = phaseDist(rng);
        chaoticPhase = phaseDist(rng);

        // Speed jump state
        speedJumpOffset = 0.0;

        // Hiss: pink noise state (Paul Kellet's algorithm)
        pinkB0 = 0.0; pinkB1 = 0.0; pinkB2 = 0.0;
        pinkB3 = 0.0; pinkB4 = 0.0; pinkB5 = 0.0; pinkB6 = 0.0;

        // Hiss filters
        hissLP.setFreq(6000.0, sr);
        hissBell.setFreq(800.0, sr);
        hissHP.setFreq(100.0, sr);

        // Crackle smoothing
        crackleState = 0.0;
        crackleSmooth.setFreq(2000.0, sr);  // Smoothing to avoid ultra-sharp clicks

        // Dropout state
        dropoutGain = 1.0;

        prevOversampleInput = 0.0;
    }

    void reset() override
    {
        dcBlocker.reset();
        preHP1.reset(); preHP2.reset();
        preLP1.reset(); preLP2.reset();
        preBell.reset();
        postLP.reset(); postLPDriven.reset();
        delay.reset();
        slowRandom.reset();
        irregWowDrift.reset();
        chaoticDrift.reset();
        irregPhase = 0.0;
        chaoticPhase = 0.0;
        speedJumpOffset = 0.0;
        pinkB0 = 0.0; pinkB1 = 0.0; pinkB2 = 0.0;
        pinkB3 = 0.0; pinkB4 = 0.0; pinkB5 = 0.0; pinkB6 = 0.0;
        hissLP.reset(); hissBell.reset(); hissHP.reset();
        crackleState = 0.0;
        crackleSmooth.reset();
        dropoutGain = 1.0;
        prevOversampleInput = 0.0;
    }

    //==========================================================================
    // Saturation: Asymmetric hard clipping + mild wavefolder
    // Positive: tanh(x * 1.5), Negative: tanh(x * 3.0) * 0.7
    // Wavefolder at high saturation, drive 1x-9x
    // Pre: HP 200Hz 12dB/oct, LP 6kHz 12dB/oct, bell +4dB at 1kHz Q=1.5
    // Post: LP 4kHz->2kHz (extremely dark)
    // 2x oversampled
    double processSaturation(double input, double amount) override
    {
        if (amount < 0.001) { prevOversampleInput = input; return input; }

        // 2x oversampling
        const double midSample = (prevOversampleInput + input) * 0.5;
        prevOversampleInput = input;

        const double out1 = saturateSample(midSample, amount);
        const double out2 = saturateSample(input, amount);

        return (out1 + out2) * 0.5;
    }

    //==========================================================================
    // Wow: Four chaotic components
    //   1. Slow random drift (smoothRandom at 0.15Hz)
    //   2. Irregular wow (1.2Hz +/-0.8Hz drift)
    //   3. Chaotic flutter (5Hz +/-4Hz drift)
    //   4. Random speed jumps (1% * amount probability, +/-50 cents, exp decay)
    // Max +/-50 cents total. 12ms center delay. 80ms delay buffer.
    double processWow(double input, double amount) override
    {
        if (amount < 0.001)
        {
            // Even when off, decay any lingering speed jump
            speedJumpOffset *= 0.999;
            return input;
        }

        delay.write(static_cast<float>(input));

        // 1. Slow random drift
        const double slowDriftVal = slowRandom.next(sr);

        // 2. Irregular wow: 1.2Hz +/-0.8Hz drift
        const double irregDriftVal = irregWowDrift.next(sr);
        const double irregRate = 1.2 + irregDriftVal * 0.8;
        irregPhase += std::max(irregRate, 0.1) / sr;
        if (irregPhase >= 1.0) irregPhase -= 1.0;
        const double irregMod = std::sin(2.0 * M_PI * irregPhase);

        // 3. Chaotic flutter: 5Hz +/-4Hz drift
        const double chaoticDriftVal = chaoticDrift.next(sr);
        const double chaoticRate = 5.0 + chaoticDriftVal * 4.0;
        chaoticPhase += std::max(chaoticRate, 0.5) / sr;
        if (chaoticPhase >= 1.0) chaoticPhase -= 1.0;
        const double chaoticMod = std::sin(2.0 * M_PI * chaoticPhase);

        // 4. Random speed jumps
        // 1% probability * amount per sample
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        if (prob(rng) < 0.01 * amount)
        {
            // Random jump: +/-50 cents worth of delay offset
            std::uniform_real_distribution<double> jumpDist(-1.0, 1.0);
            const double jumpCents = jumpDist(rng) * 50.0;
            // Convert cents to delay offset in samples
            // 50 cents at center delay ~= significant pitch jump
            const double jumpMs = jumpCents * 0.0128 * 0.001;  // rough cents-to-ms
            speedJumpOffset += jumpMs * sr;
        }
        // Exponential decay of jump offset (0.95 per sample = very fast at audio rate,
        // but we want it audible, so use a slower decay)
        speedJumpOffset *= (1.0 - (1.0 - 0.9999));  // ~= 0.9999 decay

        // Combine all components, scale to +/-50 cents deviation
        // 50 cents at 440Hz ~= 1.28ms max deviation
        const double maxDeviationMs = 1.28 * amount;
        const double combinedMod = slowDriftVal * 0.25
                                 + irregMod * 0.30
                                 + chaoticMod * 0.20
                                 + 0.25; // DC offset: always slightly detuned

        const double deviationSamples = maxDeviationMs * 0.001 * sr * combinedMod
                                      + speedJumpOffset * amount;

        // 12ms center delay
        const double centerDelay = sr * 0.012;
        const double totalDelay = centerDelay + deviationSamples;

        const double clampedDelay = std::max(1.0, std::min(totalDelay,
            static_cast<double>(delay.bufferSize - 2)));

        return static_cast<double>(delay.readCubic(clampedDelay));
    }

    //==========================================================================
    // Hiss: Pink noise (Paul Kellet) + crackle impulses + micro-dropouts
    // Hiss knob controls ALL three: noise level, crackle, AND dropouts
    double processHiss(double amount, float& audioGainMultiplier) override
    {
        // --- Micro-dropouts ---
        // Rare: 0.1% chance * amount per sample, dips gain to 0.3, slow recovery
        if (amount > 0.01)
        {
            std::uniform_real_distribution<double> prob(0.0, 1.0);
            if (prob(rng) < 0.001 * amount)
            {
                dropoutGain = 0.3;
            }
            // Slow recovery toward 1.0
            dropoutGain = dropoutGain * 0.999 + 0.001;
            dropoutGain = std::min(dropoutGain, 1.0);
        }
        else
        {
            dropoutGain = 1.0;
        }
        audioGainMultiplier = static_cast<float>(dropoutGain);

        if (amount < 0.001) return 0.0;

        // --- Pink noise (Paul Kellet's algorithm) ---
        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        const double white = dist(rng);

        pinkB0 = 0.99886 * pinkB0 + white * 0.0555179;
        pinkB1 = 0.99332 * pinkB1 + white * 0.0750759;
        pinkB2 = 0.96900 * pinkB2 + white * 0.1538520;
        pinkB3 = 0.86650 * pinkB3 + white * 0.3104856;
        pinkB4 = 0.55000 * pinkB4 + white * 0.5329522;
        pinkB5 = -0.7616 * pinkB5 - white * 0.0168980;

        double pink = pinkB0 + pinkB1 + pinkB2 + pinkB3 + pinkB4 + pinkB5 + pinkB6 + white * 0.5362;
        pinkB6 = white * 0.115926;

        // Normalize pink noise (Paul Kellet output is roughly +/-4-5)
        pink *= 0.11;

        // LP at 6kHz
        pink = hissLP.process(pink);

        // Bell +3dB at 800Hz
        const double bellLP = hissBell.process(pink);
        const double bellHF = pink - bellLP;
        // +3dB = ~1.41x
        pink = bellLP * 1.41 + bellHF;

        // HP at 100Hz
        pink = hissHP.process(pink);

        // --- Crackle: random impulse events ---
        // 3% chance/sample at max amount, +/-0.4 amplitude, smoothed
        double crackle = 0.0;
        {
            std::uniform_real_distribution<double> prob(0.0, 1.0);
            if (prob(rng) < 0.03 * amount)
            {
                std::uniform_real_distribution<double> impDist(-0.4, 0.4);
                crackleState = impDist(rng);
            }
            // Smooth the crackle impulse with a one-pole to avoid ultra-sharp clicks
            crackle = crackleSmooth.process(crackleState);
            // Decay the crackle state
            crackleState *= 0.8;
        }

        // Combine: pink noise + crackle
        const double totalNoise = pink + crackle * amount;

        // Level: max coefficient matches approximate combined loudness
        // Pink is already quieter than white, so scale appropriately
        const double level = amount * 0.25;

        return totalNoise * level;
    }

private:
    // Internal saturation function for wire character
    double saturateSample(double input, double amount)
    {
        const double drive = 1.0 + amount * 8.0;  // 1x to 9x drive

        // Pre-filter: HP 200Hz, ~12dB/oct (two cascaded one-poles)
        double x = preHP1.process(input);
        x = preHP2.process(x);

        // Pre-filter: LP 6kHz, ~12dB/oct (two cascaded one-poles)
        x = preLP1.process(x);
        x = preLP2.process(x);

        // Bell +4dB at 1kHz (wire recorder head resonance)
        const double bellLP = preBell.process(x);
        const double bellHF = x - bellLP;
        // +4dB = ~1.58x on the low band (since bell boosts center, not HF)
        x = bellLP * 1.58 + bellHF;

        // Apply drive
        x *= drive;

        // Asymmetric hard clipping
        // Positive side: tanh(x * 1.5) — moderate saturation
        // Negative side: tanh(x * 3.0) * 0.7 — harder clip, quieter
        double clipped;
        if (x >= 0.0)
        {
            clipped = std::tanh(x * 1.5);
        }
        else
        {
            clipped = std::tanh(x * 3.0) * 0.7;
        }

        // Mild wavefolder at high saturation
        // fold depth scales with amount (0.3 * amount)
        const double foldDepth = 0.3 * amount;
        if (foldDepth > 0.01)
        {
            // Simple wavefolder: if |x| exceeds threshold, fold back
            const double threshold = 1.0 - foldDepth * 0.5;
            if (clipped > threshold)
            {
                clipped = threshold - (clipped - threshold);
            }
            else if (clipped < -threshold)
            {
                clipped = -threshold - (clipped + threshold);
            }
        }

        x = clipped;

        // DC blocker
        x = dcBlocker.process(x);

        // Post-LP: blend 4kHz (clean) to 2kHz (driven) — extremely dark
        const double cleanLP = postLP.process(x);
        const double drivenLP = postLPDriven.process(x);
        x = cleanLP * (1.0 - amount) + drivenLP * amount;

        // Gain compensation
        const double gainComp = 1.0 / (1.0 + amount * 0.8);
        x *= gainComp;

        return x;
    }

    // Saturation filters
    DCBlocker dcBlocker;
    OnePoleHP preHP1, preHP2;         // Cascaded for ~12dB/oct
    OnePoleLP preLP1, preLP2;         // Cascaded for ~12dB/oct
    OnePoleLP preBell;
    OnePoleLP postLP;
    OnePoleLP postLPDriven;

    // Wow
    DelayLine delay;
    SmoothRandom slowRandom;
    SmoothRandom irregWowDrift;
    SmoothRandom chaoticDrift;
    double irregPhase = 0.0;
    double chaoticPhase = 0.0;
    double speedJumpOffset = 0.0;

    // Hiss: Paul Kellet pink noise state
    double pinkB0 = 0.0, pinkB1 = 0.0, pinkB2 = 0.0;
    double pinkB3 = 0.0, pinkB4 = 0.0, pinkB5 = 0.0, pinkB6 = 0.0;

    // Hiss filters
    OnePoleLP hissLP;
    OnePoleLP hissBell;
    OnePoleHP hissHP;
    OnePoleLP crackleSmooth;
    double crackleState = 0.0;

    // Dropout state
    double dropoutGain = 1.0;
};
