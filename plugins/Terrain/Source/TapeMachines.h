#pragma once

#include <cmath>
#include <random>
#include <vector>
#include <algorithm>

//==============================================================================
// TapeMachines.h — Three distinct tape machine DSP algorithms for Terrain v2.0
//
// Each machine models a different tape speed (IPS) which affects bandwidth,
// noise floor, saturation onset, and wow/flutter severity.
//
// StudioMachine:   15 IPS reel-to-reel. Wide bandwidth, smooth tanh saturation
//                  with pre/post emphasis, single gentle wow LFO (±1.2ms),
//                  clean shaped white hiss.
//
// CassetteMachine: 1⅞ IPS consumer cassette. Narrow bandwidth, cubic soft clip
//                  with aggressive frequency-dependent darkening, triple-LFO
//                  wow/flutter (±3.2ms), colored mid-range hiss + motor rumble.
//
// WireMachine:     Experimental wire recorder. Extremely narrow bandwidth,
//                  asymmetric clip + wavefolder with smoothstep blend at low
//                  settings, chaotic 4-component wow with random speed jumps
//                  (±8.5ms), pink noise + crackle + micro-dropouts + wire twist
//                  phase artifact.
//
// Hiss uses exponential curve: pow(amount, 2.5) * machineGain
//   Studio max: 0.003, Cassette max: 0.006, Wire max: 0.01
//   At 5%: barely audible. At 50%: moderate. At 100%: heavy but never clipping.
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

    virtual double processSaturation(double input, double amount) = 0;
    virtual double processWow(double input, double amount) = 0;
    virtual double processHiss(double amount, float& audioGainMultiplier) = 0;

protected:
    double sr = 44100.0;

    static double onePoleCoeff(double freqHz, double sampleRate)
    {
        return 1.0 / (1.0 + sampleRate / (2.0 * M_PI * freqHz));
    }

    // Smoothstep helper for Wire blend curve
    static double smoothstep(double edge0, double edge1, double x)
    {
        double t = std::max(0.0, std::min(1.0, (x - edge0) / (edge1 - edge0)));
        return t * t * (3.0 - 2.0 * t);
    }

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

        void reset() { state = 0.0; prev = 0.0; }

        double process(double input)
        {
            state = coeff * (state + input - prev);
            prev = input;
            return state;
        }
    };

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

    struct OnePoleHP
    {
        double prevInput = 0.0;
        double prevOutput = 0.0;
        double alpha = 0.0;

        void setFreq(double freqHz, double sampleRate)
        {
            alpha = 1.0 / (1.0 + (2.0 * M_PI * freqHz) / sampleRate);
        }

        void reset() { prevInput = 0.0; prevOutput = 0.0; }

        double process(double input)
        {
            prevOutput = alpha * (prevOutput + input - prevInput);
            prevInput = input;
            return prevOutput;
        }
    };

    // One-pole allpass filter for Wire twist artifact
    struct OnePoleAP
    {
        double state = 0.0;
        double alpha = 0.0;

        void setFreq(double freqHz, double sampleRate)
        {
            double tanVal = std::tan(M_PI * freqHz / sampleRate);
            alpha = (tanVal - 1.0) / (tanVal + 1.0);
        }

        void reset() { state = 0.0; }

        double process(double input)
        {
            double output = alpha * input + state;
            state = input - alpha * output;
            return output;
        }
    };

    struct SmoothRandom
    {
        double state = 0.0;
        double target = 0.0;
        double phase = 0.0;
        double rate = 1.0;
        double smoothing = 0.0;
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

        void reset() { state = 0.0; target = 0.0; phase = 0.0; }

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

            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

            return ((c3 * frac + c2) * frac + c1) * frac + c0;
        }
    };

    std::mt19937 rng;

    void initRng()
    {
        rng.seed(static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)));
    }

    double prevOversampleInput = 0.0;
};


//==============================================================================
//==============================================================================
//
//  StudioMachine — 15 IPS professional reel-to-reel
//
//  Saturation: tanh with DC bias, pre-emphasis +3dB@3kHz, gentle post-rolloff
//              that preserves highs (16kHz - 4kHz*amount). 2x oversampled.
//  Wow: Single clean LFO at 0.8Hz, ±1.2ms max delay modulation + subtle
//       flutter at 5.5Hz ±0.15ms. Smooth, musical, predictable.
//  Hiss: Shaped white noise, pow(amount,2.5) * 0.003 max. Clean and subtle.
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

        dcBlocker.prepare(sr);
        preEmphasis.setFreq(3000.0, sr);
        // Post rolloff: starts at 16kHz, darkens to 12kHz at full drive
        postRolloff.setFreq(16000.0, sr);

        // Wow delay line: 50ms buffer for ±1.2ms modulation
        delay.prepare(0.05, sr);
        rateDrift.prepare(0.1, 2.0, sr, rng);

        std::uniform_real_distribution<double> phaseDist(0.0, 1.0);
        wowPhase = phaseDist(rng);
        flutterPhase = phaseDist(rng);

        hissHiShelf.setFreq(4000.0, sr);
        hissHP.setFreq(200.0, sr);

        prevOversampleInput = 0.0;
    }

    void reset() override
    {
        dcBlocker.reset();
        preEmphasis.reset();
        postRolloff.reset();
        delay.reset();
        rateDrift.reset();
        wowPhase = 0.0;
        flutterPhase = 0.0;
        hissHiShelf.reset();
        hissHP.reset();
        prevOversampleInput = 0.0;
    }

    //==========================================================================
    // Saturation: tanh with DC bias, pre-emphasis, gentle post-rolloff
    // 15 IPS = wide bandwidth, retains highs even when driven
    double processSaturation(double input, double amount) override
    {
        if (amount < 0.001) { prevOversampleInput = input; return input; }

        const double midSample = (prevOversampleInput + input) * 0.5;
        prevOversampleInput = input;

        const double out1 = saturateSample(midSample, amount);
        const double out2 = saturateSample(input, amount);

        return (out1 + out2) * 0.5;
    }

    //==========================================================================
    // Wow: Single primary LFO at 0.8Hz (±1.2ms) + subtle flutter at 5.5Hz (±0.15ms)
    // Smooth, gentle, "professional machine with slight speed issues"
    double processWow(double input, double amount) override
    {
        if (amount < 0.001) return input;

        delay.write(static_cast<float>(input));

        const double drift = rateDrift.next(sr);

        // Primary wow: 0.8Hz with subtle drift
        const double wowFreq = 0.8 + drift * 0.15 * amount;
        wowPhase += std::max(wowFreq, 0.1) / sr;
        if (wowPhase >= 1.0) wowPhase -= 1.0;
        const double wowMod = std::sin(2.0 * M_PI * wowPhase);

        // Subtle flutter: 5.5Hz
        flutterPhase += 5.5 / sr;
        if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        const double flutterMod = std::sin(2.0 * M_PI * flutterPhase);

        // ±1.2ms wow + ±0.15ms flutter
        const double wowDepthMs = amount * 1.2;
        const double flutterDepthMs = amount * 0.15;
        const double totalModMs = wowMod * wowDepthMs + flutterMod * flutterDepthMs;
        const double deviationSamples = totalModMs * 0.001 * sr;

        // 5ms center delay
        const double centerDelay = sr * 0.005;
        const double totalDelay = centerDelay + deviationSamples;

        const double clampedDelay = std::max(1.0, std::min(totalDelay,
            static_cast<double>(delay.bufferSize - 2)));

        return static_cast<double>(delay.readCubic(clampedDelay));
    }

    //==========================================================================
    // Hiss: Shaped white noise. Exponential curve, max 0.003 coefficient.
    // 15 IPS = lowest noise floor of all three machines.
    double processHiss(double amount, float& audioGainMultiplier) override
    {
        audioGainMultiplier = 1.0f;
        if (amount < 0.001) return 0.0;

        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        double noise = dist(rng);

        // High-shelf at 4kHz: +3dB on highs
        const double lpNoise = hissHiShelf.process(noise);
        const double hpNoise = noise - lpNoise;
        noise = lpNoise + hpNoise * 1.41;

        // HP at 200Hz
        noise = hissHP.process(noise);

        // Exponential curve: pow(amount, 2.5) * 0.003
        const double level = std::pow(amount, 2.5) * 0.003;

        return noise * level;
    }

private:
    double saturateSample(double input, double amount)
    {
        const double drive = 1.0 + amount * 4.0;

        // Pre-emphasis: +3dB shelf at 3kHz (record head equalization)
        const double lp = preEmphasis.process(input);
        const double hfContent = input - lp;
        double x = input + hfContent * amount * 0.6;

        // DC bias for even harmonics
        const double bias = 0.05 * amount;
        x = x * drive + bias;

        // tanh waveshaper
        x = std::tanh(x);

        x = dcBlocker.process(x);

        // Post-rolloff: gentle, cutoff = 16kHz - 4kHz*amount
        // At 0%: 16kHz (nearly flat). At 100%: 12kHz (warm but still open)
        const double cutoff = 16000.0 - 4000.0 * amount;
        postRolloff.alpha = onePoleCoeff(std::max(cutoff, 4000.0), sr);
        const double postLP = postRolloff.process(x);
        const double postHF = x - postLP;
        // -6dB/oct slope: attenuate HF proportional to amount
        x = postLP + postHF * (1.0 - amount * 0.4);

        const double gainComp = 1.0 / (1.0 + amount * 0.5);
        x *= gainComp;

        return x;
    }

    DCBlocker dcBlocker;
    OnePoleLP preEmphasis;
    OnePoleLP postRolloff;

    DelayLine delay;
    SmoothRandom rateDrift;
    double wowPhase = 0.0;
    double flutterPhase = 0.0;

    OnePoleLP hissHiShelf;
    OnePoleHP hissHP;
};


//==============================================================================
//==============================================================================
//
//  CassetteMachine — 1⅞ IPS consumer cassette
//
//  Saturation: Cubic soft clip with pre-gain boost (distorts earlier than
//              Studio), mid-range resonance +4dB@2kHz, aggressive bandwidth
//              restriction that DRAMATICALLY darkens when driven.
//              Pre-LP: 13kHz - 7kHz*amount. Post-LP: 10kHz - 5kHz*amount.
//              2x oversampled.
//  Wow: Three interfering LFOs with rate drift:
//       Primary 0.6Hz ±2.0ms, Secondary 2.2Hz ±0.8ms, Flutter 7Hz ±0.4ms.
//       Total max ±3.2ms. Complex, wobbly, never repeats.
//  Hiss: Mid-colored noise + motor rumble, pow(amount,2.5) * 0.006 max.
//        Louder than Studio at same knob position (worse SNR at 1⅞ IPS).
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

        dcBlocker.prepare(sr);
        preHP.setFreq(60.0, sr);
        // Pre-bandwidth: starts at 13kHz, driven down to 6kHz (1⅞ IPS = narrow)
        preBandwidthLP.setFreq(13000.0, sr);
        // Mid-range resonance at 2kHz (cassette head bump)
        preBell.setFreq(2000.0, sr);
        // Post-LP: 10kHz clean, 5kHz driven (aggressive darkening)
        postLP.setFreq(10000.0, sr);
        postLPDriven.setFreq(5000.0, sr);

        // Wow delay line: 60ms buffer for complex modulation (±3.2ms)
        delay.prepare(0.06, sr);

        slowDrift.prepare(0.08, 1.5, sr, rng);
        medDrift.prepare(0.15, 2.5, sr, rng);
        flutterDrift.prepare(0.2, 3.0, sr, rng);

        std::uniform_real_distribution<double> phaseDist(0.0, 1.0);
        slowPhase = phaseDist(rng);
        medPhase = phaseDist(rng);
        flutterPhase = phaseDist(rng);

        hissBell.setFreq(3500.0, sr);
        hissLP.setFreq(10000.0, sr);
        hissHP.setFreq(150.0, sr);
        rumbleLP.setFreq(300.0, sr);

        prevOversampleInput = 0.0;
        rumbleState = 0.0;
    }

    void reset() override
    {
        dcBlocker.reset();
        preHP.reset();
        preBandwidthLP.reset();
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
    // Saturation: Cubic soft clip, pre-gain boost (cassette distorts earlier),
    // mid-bump, aggressive bandwidth restriction. Gets DARK when driven.
    double processSaturation(double input, double amount) override
    {
        if (amount < 0.001) { prevOversampleInput = input; return input; }

        const double midSample = (prevOversampleInput + input) * 0.5;
        prevOversampleInput = input;

        const double out1 = saturateSample(midSample, amount);
        const double out2 = saturateSample(input, amount);

        return (out1 + out2) * 0.5;
    }

    //==========================================================================
    // Wow: Three interfering LFOs — primary 0.6Hz, secondary 2.2Hz, flutter 7Hz
    // All with rate drift. Total ±3.2ms. Complex, wobbly, irregular.
    double processWow(double input, double amount) override
    {
        if (amount < 0.001) return input;

        delay.write(static_cast<float>(input));

        const double slowDriftVal = slowDrift.next(sr);
        const double medDriftVal = medDrift.next(sr);
        const double flutterDriftVal = flutterDrift.next(sr);

        // Primary wow: 0.6Hz (capstan eccentricity), triangle wave
        const double slowRate = 0.6 + slowDriftVal * 0.2;
        slowPhase += std::max(slowRate, 0.1) / sr;
        if (slowPhase >= 1.0) slowPhase -= 1.0;
        const double slowMod = 4.0 * std::abs(slowPhase - 0.5) - 1.0;

        // Secondary wow: 2.2Hz (take-up tension), sine
        const double medRate = 2.2 + medDriftVal * 0.5;
        medPhase += std::max(medRate, 0.1) / sr;
        if (medPhase >= 1.0) medPhase -= 1.0;
        const double medMod = std::sin(2.0 * M_PI * medPhase);

        // Flutter: 7Hz (motor vibration), sine
        const double flutRate = 7.0 + flutterDriftVal * 1.5;
        flutterPhase += std::max(flutRate, 0.5) / sr;
        if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        const double flutMod = std::sin(2.0 * M_PI * flutterPhase);

        // Depth: primary ±2.0ms, secondary ±0.8ms, flutter ±0.4ms
        const double totalModMs = slowMod * amount * 2.0
                                + medMod * amount * 0.8
                                + flutMod * amount * 0.4;
        const double deviationSamples = totalModMs * 0.001 * sr;

        // 8ms center delay
        const double centerDelay = sr * 0.008;
        const double totalDelay = centerDelay + deviationSamples;

        const double clampedDelay = std::max(1.0, std::min(totalDelay,
            static_cast<double>(delay.bufferSize - 2)));

        return static_cast<double>(delay.readCubic(clampedDelay));
    }

    //==========================================================================
    // Hiss: Mid-colored noise + motor rumble. pow(amount,2.5) * 0.006 max.
    // 1⅞ IPS = worse SNR than 15 IPS Studio.
    double processHiss(double amount, float& audioGainMultiplier) override
    {
        audioGainMultiplier = 1.0f;
        if (amount < 0.001) return 0.0;

        std::uniform_real_distribution<double> dist(-1.0, 1.0);
        double noise = dist(rng);

        // Bell +5dB at 3.5kHz (sibilant emphasis)
        const double bellLP = hissBell.process(noise);
        const double bellHF = noise - bellLP;
        noise = bellLP + bellHF * 1.78;

        // LP at 10kHz
        noise = hissLP.process(noise);

        // HP at 150Hz
        noise = hissHP.process(noise);

        // Motor rumble
        const double rumbleNoise = dist(rng);
        rumbleState = rumbleState * 0.99 + rumbleNoise * 0.01;
        const double rumble = rumbleLP.process(rumbleState) * 0.1;

        const double total = noise + rumble;

        // Exponential curve: pow(amount, 2.5) * 0.006
        const double level = std::pow(amount, 2.5) * 0.006;

        return total * level;
    }

private:
    double saturateSample(double input, double amount)
    {
        // Pre-gain: cassette has less headroom, distorts earlier than Studio
        // 1.5x base + 3x at full drive = up to 4.5x pre-gain before waveshaper
        const double preGain = 1.5 + amount * 3.0;
        const double drive = preGain;

        // HP at 60Hz
        double x = preHP.process(input);

        // Pre-bandwidth restriction: LP at 13kHz - 7kHz*amount
        // At 0%: 13kHz. At 100%: 6kHz (very muffled)
        const double bwCutoff = 13000.0 - 7000.0 * amount;
        preBandwidthLP.alpha = onePoleCoeff(std::max(bwCutoff, 2000.0), sr);
        x = preBandwidthLP.process(x);

        // Mid-range resonance: +4dB at 2kHz (cassette head bump)
        const double bellLP = preBell.process(x);
        const double bellHF = x - bellLP;
        // +4dB = ~1.58x on the low band
        x = bellLP * 1.58 + bellHF;

        // Apply drive
        x *= drive;

        // Asymmetric 2nd harmonic injection (messier than Studio)
        const double secondHarmonic = 0.15 * amount * x * std::abs(x);

        // Cubic soft clip
        if (x > 1.0) x = 2.0 / 3.0;
        else if (x < -1.0) x = -2.0 / 3.0;
        else x = x - (x * x * x) / 3.0;

        x += secondHarmonic;

        x = dcBlocker.process(x);

        // Post-LP: blend between 10kHz (clean) and 5kHz (driven)
        // This is where cassette gets DARK — much more aggressive than Studio
        const double postCutoff = 10000.0 - 5000.0 * amount;
        postLP.alpha = onePoleCoeff(std::max(postCutoff, 3000.0), sr);
        const double postDrivenCutoff = 5000.0 - 2000.0 * amount;
        postLPDriven.alpha = onePoleCoeff(std::max(postDrivenCutoff, 2000.0), sr);
        const double cleanOut = postLP.process(x);
        const double drivenOut = postLPDriven.process(x);
        x = cleanOut * (1.0 - amount) + drivenOut * amount;

        const double gainComp = 1.0 / (1.0 + amount * 0.7);
        x *= gainComp;

        return x;
    }

    DCBlocker dcBlocker;
    OnePoleHP preHP;
    OnePoleLP preBandwidthLP;
    OnePoleLP preBell;
    OnePoleLP postLP;
    OnePoleLP postLPDriven;

    DelayLine delay;
    SmoothRandom slowDrift;
    SmoothRandom medDrift;
    SmoothRandom flutterDrift;
    double slowPhase = 0.0;
    double medPhase = 0.0;
    double flutterPhase = 0.0;

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
//  Saturation: Asymmetric hard clip + wavefolder with smoothstep blend
//              (0-30% amount blends from clean to full Wire character).
//              Extremely narrow bandwidth (200Hz-6kHz). 2x oversampled.
//  Wow: 4 chaotic components: slow drift, irregular wow (wandering rate),
//       chaotic flutter (wildly wandering rate), random speed jumps.
//       Total max ±8.5ms. Broken, lurching, haunted.
//  Hiss: Pink noise + crackle + micro-dropouts + wire twist all-pass artifact.
//        pow(amount,2.5) * 0.01 max. Worst SNR of all three machines.
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

        dcBlocker.prepare(sr);
        preHP1.setFreq(200.0, sr);
        preHP2.setFreq(200.0, sr);
        preLP1.setFreq(6000.0, sr);
        preLP2.setFreq(6000.0, sr);
        preBell.setFreq(1000.0, sr);
        postLP.setFreq(4000.0, sr);
        postLPDriven.setFreq(2000.0, sr);

        // Wow delay line: 100ms buffer for massive chaotic modulation
        delay.prepare(0.1, sr);

        slowRandom.prepare(0.12, 0.5, sr, rng);
        irregWowDrift.prepare(0.3, 1.5, sr, rng);
        chaoticDrift.prepare(0.5, 2.0, sr, rng);

        std::uniform_real_distribution<double> phaseDist(0.0, 1.0);
        irregPhase = phaseDist(rng);
        chaoticPhase = phaseDist(rng);

        speedJumpOffset = 0.0;

        // Pink noise state
        pinkB0 = 0.0; pinkB1 = 0.0; pinkB2 = 0.0;
        pinkB3 = 0.0; pinkB4 = 0.0; pinkB5 = 0.0; pinkB6 = 0.0;

        hissLP.setFreq(6000.0, sr);
        hissBell.setFreq(800.0, sr);
        hissHP.setFreq(100.0, sr);

        crackleState = 0.0;
        crackleSmooth.setFreq(2000.0, sr);

        dropoutGain = 1.0;

        // Wire twist all-pass artifact
        wireTwistAP.setFreq(800.0, sr);
        wireTwistRandom.prepare(0.3, 1.0, sr, rng);

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
        wireTwistAP.reset();
        wireTwistRandom.reset();
        prevOversampleInput = 0.0;
    }

    //==========================================================================
    // Saturation: Asymmetric clip + wavefolder with smoothstep blend.
    // At 0-30%: gradually blend in Wire character (subtle at low settings).
    // At 30%+: full Wire algorithm.
    double processSaturation(double input, double amount) override
    {
        if (amount < 0.001) { prevOversampleInput = input; return input; }

        const double midSample = (prevOversampleInput + input) * 0.5;
        prevOversampleInput = input;

        const double out1 = saturateSample(midSample, amount);
        const double out2 = saturateSample(input, amount);

        return (out1 + out2) * 0.5;
    }

    //==========================================================================
    // Wow: 4 chaotic components
    //   1. Slow drift (smoothRandom, ±2.5ms) — wire tension wandering
    //   2. Irregular wow (wandering rate 0.5-2.5Hz, ±1.5ms) — spool inconsistency
    //   3. Chaotic flutter (wildly wandering rate 1-9Hz, ±0.5ms) — friction
    //   4. Random speed jumps (±4ms, slow decay ~300ms) — the signature Wire feature
    // Total max ±8.5ms. 12ms center delay.
    double processWow(double input, double amount) override
    {
        if (amount < 0.001)
        {
            speedJumpOffset *= 0.999;
            return input;
        }

        delay.write(static_cast<float>(input));

        // 1. Slow drift: wire tension changes unpredictably
        const double driftValue = slowRandom.next(sr) * amount * 2.5;

        // 2. Irregular wow: rate itself wanders 0.5-2.5Hz
        const double irregDriftVal = irregWowDrift.next(sr);
        const double irregRate = 1.0 + irregDriftVal * 1.5;
        irregPhase += std::max(irregRate, 0.1) / sr;
        if (irregPhase >= 1.0) irregPhase -= 1.0;
        const double irregMod = std::sin(2.0 * M_PI * irregPhase) * amount * 1.5;

        // 3. Chaotic flutter: rate wanders wildly 1-9Hz
        const double chaoticDriftVal = chaoticDrift.next(sr);
        const double chaoticRate = 4.0 + chaoticDriftVal * 5.0;
        chaoticPhase += std::max(chaoticRate, 0.5) / sr;
        if (chaoticPhase >= 1.0) chaoticPhase -= 1.0;
        const double chaoticMod = std::sin(2.0 * M_PI * chaoticPhase) * amount * 0.5;

        // 4. Random speed jumps — the signature Wire feature
        // Per-sample probability: ~0.0005 * amount
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        if (prob(rng) < 0.0005 * amount)
        {
            std::uniform_real_distribution<double> jumpDist(-1.0, 1.0);
            // Sudden ±4ms jump in delay time
            speedJumpOffset += jumpDist(rng) * amount * 4.0;
        }
        // Slow exponential decay (~300ms to half at 44.1kHz)
        // 0.9999^44100 ≈ 0.012 so about 100ms to reach 1%
        // Use 0.99993 for ~300ms half-life
        speedJumpOffset *= 0.99993;

        // Combine all (in ms)
        const double totalModMs = driftValue + irregMod + chaoticMod + speedJumpOffset;
        const double deviationSamples = totalModMs * 0.001 * sr;

        // 12ms center delay
        const double centerDelay = sr * 0.012;
        const double totalDelay = centerDelay + deviationSamples;

        const double clampedDelay = std::max(1.0, std::min(totalDelay,
            static_cast<double>(delay.bufferSize - 2)));

        return static_cast<double>(delay.readCubic(clampedDelay));
    }

    //==========================================================================
    // Hiss: Pink noise + crackle + micro-dropouts + wire twist all-pass.
    // pow(amount,2.5) * 0.01 max. Worst noise floor.
    double processHiss(double amount, float& audioGainMultiplier) override
    {
        // --- Micro-dropouts ---
        if (amount > 0.01)
        {
            std::uniform_real_distribution<double> prob(0.0, 1.0);
            if (prob(rng) < 0.001 * amount)
            {
                dropoutGain = 0.3;
            }
            dropoutGain = dropoutGain * 0.999 + 0.001;
            dropoutGain = std::min(dropoutGain, 1.0);
        }
        else
        {
            dropoutGain = 1.0;
        }
        audioGainMultiplier = static_cast<float>(dropoutGain);

        if (amount < 0.001) return 0.0;

        // --- Pink noise (Paul Kellet) ---
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

        pink *= 0.11;

        // LP at 6kHz
        pink = hissLP.process(pink);

        // Bell +3dB at 800Hz
        const double bellLP = hissBell.process(pink);
        const double bellHF = pink - bellLP;
        pink = bellLP * 1.41 + bellHF;

        // HP at 100Hz
        pink = hissHP.process(pink);

        // --- Wire twist phase artifact ---
        // All-pass modulated by slow random, unique to Wire
        const double twistVal = wireTwistRandom.next(sr);
        const double twistFreq = 800.0 + amount * 2000.0 * (0.5 + twistVal * 0.5);
        wireTwistAP.setFreq(std::max(200.0, std::min(twistFreq, 4000.0)), sr);
        pink = wireTwistAP.process(pink);

        // --- Crackle ---
        double crackle = 0.0;
        {
            std::uniform_real_distribution<double> cProb(0.0, 1.0);
            if (cProb(rng) < 0.03 * amount)
            {
                std::uniform_real_distribution<double> impDist(-0.4, 0.4);
                crackleState = impDist(rng);
            }
            crackle = crackleSmooth.process(crackleState);
            crackleState *= 0.8;
        }

        const double totalNoise = pink + crackle * amount;

        // Exponential curve: pow(amount, 2.5) * 0.01
        const double level = std::pow(amount, 2.5) * 0.01;

        return totalNoise * level;
    }

private:
    double saturateSample(double input, double amount)
    {
        const double drive = 1.0 + amount * 8.0;

        // Pre-filter: HP 200Hz ~12dB/oct
        double x = preHP1.process(input);
        x = preHP2.process(x);

        // Pre-filter: LP 6kHz ~12dB/oct (extremely narrow bandwidth)
        x = preLP1.process(x);
        x = preLP2.process(x);

        // Bell +4dB at 1kHz
        const double bellLP = preBell.process(x);
        const double bellHF = x - bellLP;
        x = bellLP * 1.58 + bellHF;

        x *= drive;

        // Asymmetric hard clipping + wavefolder
        double clipped;
        if (x >= 0.0)
            clipped = std::tanh(x * 1.5);
        else
            clipped = std::tanh(x * 3.0) * 0.7;

        const double foldDepth = 0.3 * amount;
        if (foldDepth > 0.01)
        {
            const double threshold = 1.0 - foldDepth * 0.5;
            if (clipped > threshold)
                clipped = threshold - (clipped - threshold);
            else if (clipped < -threshold)
                clipped = -threshold - (clipped + threshold);
        }

        // Smoothstep blend: 0-30% amount blends from clean to full Wire
        const double blendCurve = smoothstep(0.0, 0.3, amount);
        x = input * (1.0 - blendCurve) + clipped * blendCurve;

        x = dcBlocker.process(x);

        // Post-LP: 4kHz->2kHz
        const double cleanLP = postLP.process(x);
        const double drivenLP = postLPDriven.process(x);
        x = cleanLP * (1.0 - amount) + drivenLP * amount;

        const double gainComp = 1.0 / (1.0 + amount * 0.8);
        x *= gainComp;

        return x;
    }

    DCBlocker dcBlocker;
    OnePoleHP preHP1, preHP2;
    OnePoleLP preLP1, preLP2;
    OnePoleLP preBell;
    OnePoleLP postLP;
    OnePoleLP postLPDriven;

    DelayLine delay;
    SmoothRandom slowRandom;
    SmoothRandom irregWowDrift;
    SmoothRandom chaoticDrift;
    double irregPhase = 0.0;
    double chaoticPhase = 0.0;
    double speedJumpOffset = 0.0;

    double pinkB0 = 0.0, pinkB1 = 0.0, pinkB2 = 0.0;
    double pinkB3 = 0.0, pinkB4 = 0.0, pinkB5 = 0.0, pinkB6 = 0.0;

    OnePoleLP hissLP;
    OnePoleLP hissBell;
    OnePoleHP hissHP;
    OnePoleLP crackleSmooth;
    double crackleState = 0.0;

    double dropoutGain = 1.0;

    // Wire twist artifact
    OnePoleAP wireTwistAP;
    SmoothRandom wireTwistRandom;
};
