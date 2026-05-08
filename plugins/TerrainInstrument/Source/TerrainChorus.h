// plugins/Terrain/Source/TerrainChorus.h
// Header-only Juno-60-inspired BBD chorus.
// See docs/superpowers/specs/2026-05-03-terrain-chorus-design.md.

#pragma once

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

class TerrainChorus
{
public:
    static constexpr int   BBD_BUFFER_SAMPLES   = 4096;     // ~92 ms at 44.1k
    static constexpr float BASE_DELAY_MS        = 8.5f;
    static constexpr float LFO_RATE_MIN_HZ      = 0.4f;
    static constexpr float LFO_RATE_MAX_HZ      = 1.5f;
    static constexpr float RIGHT_RATE_RATIO     = 1.07f;
    static constexpr float RIGHT_PHASE_OFFSET   = juce::MathConstants<float>::pi;
    static constexpr float WET_GAIN             = 2.5f;   // ~+8dB wet boost — chorus felt too subtle without it
    static constexpr float RECON_LP_CUTOFF_MIN  = 2000.0f;
    static constexpr float RECON_LP_CUTOFF_MAX  = 8000.0f;

    void prepare (double sr, int /*blockSize*/) noexcept
    {
        sampleRate = sr;
        bufferL.assign ((size_t) BBD_BUFFER_SAMPLES, 0.0f);
        bufferR.assign ((size_t) BBD_BUFFER_SAMPLES, 0.0f);
        reconLpL.prepare (sr);
        reconLpR.prepare (sr);
        reconLpL.setCutoff (sr, RECON_LP_CUTOFF_MAX);  // initial bright setting
        reconLpR.setCutoff (sr, RECON_LP_CUTOFF_MAX);
        reset();
    }

    void reset() noexcept
    {
        std::fill (bufferL.begin(), bufferL.end(), 0.0f);
        std::fill (bufferR.begin(), bufferR.end(), 0.0f);
        writePos = 0;
        phaseL   = 0.0f;
        phaseR   = RIGHT_PHASE_OFFSET;
        reconLpL.reset();
        reconLpR.reset();
        companderL.reset();
        companderR.reset();
    }

    void setParams (float amount_, float width_, float character_) noexcept
    {
        amount    = juce::jlimit (0.0f, 1.0f, amount_);
        width     = juce::jlimit (0.0f, 1.0f, width_);
        character = juce::jlimit (0.0f, 1.0f, character_);

        leftLfoRate  = juce::jmap (character, LFO_RATE_MAX_HZ, LFO_RATE_MIN_HZ);
        rightLfoRate = leftLfoRate * RIGHT_RATE_RATIO;

        const float reconCutoff = juce::jmap (character, RECON_LP_CUTOFF_MAX, RECON_LP_CUTOFF_MIN);
        reconLpL.setCutoff (sampleRate, reconCutoff);
        reconLpR.setCutoff (sampleRate, reconCutoff);

        drivePre = juce::jmap (character, 1.0f, 1.3f);
    }

    void processStereo (float& inL, float& inR) noexcept
    {
        // Compand on input + apply pre-BBD drive
        const float compressedL = companderL.compressIn (inL * drivePre);
        const float compressedR = companderR.compressIn (inR * drivePre);

        bufferL[(size_t) writePos] = compressedL;
        bufferR[(size_t) writePos] = compressedR;

        // Per-sample LFO advance (sine, anti-phase L/R, slightly detuned rates)
        const float lfoSamL = advancePhase (phaseL, leftLfoRate,  sampleRate);
        const float lfoSamR = advancePhase (phaseR, rightLfoRate, sampleRate);

        // Depth in samples: BASE_DELAY_MS × 0.6 × AMOUNT × sampleRate / 1000
        const float depthSamples     = BASE_DELAY_MS * 0.6f * amount * (float) sampleRate / 1000.0f;
        const float baseDelaySamples = BASE_DELAY_MS * 0.001f * (float) sampleRate;

        const float delayL = baseDelaySamples + lfoSamL * depthSamples;
        const float delayR = baseDelaySamples + lfoSamR * depthSamples;

        // Invariant guard (added per T2 code-review carry-forward) — catches
        // LFO sweep that would push delay out of buffer range. Both channels checked.
        jassert (delayL >= 1.0f && delayL < (float) BBD_BUFFER_SAMPLES);
        jassert (delayR >= 1.0f && delayR < (float) BBD_BUFFER_SAMPLES);

        const float readPosFL = (float) writePos - delayL + (float) BBD_BUFFER_SAMPLES;
        const float readPosFR = (float) writePos - delayR + (float) BBD_BUFFER_SAMPLES;

        const int   idxL  = ((int) std::floor (readPosFL)) % BBD_BUFFER_SAMPLES;
        const float fracL = readPosFL - std::floor (readPosFL);
        const int   idxR  = ((int) std::floor (readPosFR)) % BBD_BUFFER_SAMPLES;
        const float fracR = readPosFR - std::floor (readPosFR);

        const float wetLraw = hermite4 (bufferL, idxL, fracL);
        const float wetRraw = hermite4 (bufferR, idxR, fracR);

        const float wetLfiltered = reconLpL.processSample (wetLraw);
        const float wetRfiltered = reconLpR.processSample (wetRraw);

        const float wetL = companderL.expandOut (wetLfiltered);
        const float wetR = companderR.expandOut (wetRfiltered);

        writePos = (writePos + 1) % BBD_BUFFER_SAMPLES;

        // M/S widening — boost the side channel by (1 + WIDTH × 1.0)
        const float mid  = (wetL + wetR) * 0.5f;
        const float side = (wetL - wetR) * 0.5f * (1.0f + width);

        const float wetLwide = mid + side;
        const float wetRwide = mid - side;

        // Wet gain boost (chorus felt too subtle at unity AMOUNT) + soft-clip
        // via tanh to prevent harshness at extreme settings.
        const float wetLgained = std::tanh (wetLwide * WET_GAIN);
        const float wetRgained = std::tanh (wetRwide * WET_GAIN);

        // AMOUNT crossfade
        inL = inL * (1.0f - amount) + wetLgained * amount;
        inR = inR * (1.0f - amount) + wetRgained * amount;
    }

private:
    // 4-point Hermite interpolation. Index convention: idx is the floor sample,
    // frac is in [0, 1). Reads samples at (idx-1, idx, idx+1, idx+2).
    static float hermite4 (const std::vector<float>& buf, int idx, float frac) noexcept
    {
        const int n  = (int) buf.size();
        const float ym1 = buf[(size_t) ((idx - 1 + n) % n)];
        const float y0  = buf[(size_t) ((idx     + n) % n)];
        const float y1  = buf[(size_t) ((idx + 1     ) % n)];
        const float y2  = buf[(size_t) ((idx + 2     ) % n)];

        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    // Advances an LFO phase by one sample and returns sin(phase).
    static float advancePhase (float& phase, float rateHz, double sr) noexcept
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        phase += (twoPi * rateHz) / (float) sr;
        if (phase >= twoPi)  phase -= twoPi;
        if (phase <  0.0f)   phase += twoPi;
        return std::sin (phase);
    }

    // NE570-style soft-knee compander. compressIn applies tanh-shape
    // compression and advances an envelope follower; expandOut applies a
    // sinh expansion. The compress/expand pair is intentionally NOT a
    // mathematical inverse — that mismatch creates the BBD "character".
    // env is currently tracked but not yet read back into the gain path
    // (TODO: wire env into level-dependent gain riding in a future polish task).
    struct Compander
    {
        float env = 0.0f;
        const float attackCoef = 0.001f;   // tau ≈ 23ms at 44.1k (one-pole 1/(sr·coef))
        const float releaseCoef = 0.0001f; // tau ≈ 227ms at 44.1k

        float compressIn (float x) noexcept
        {
            const float absX = std::abs (x);
            const float coef = absX > env ? attackCoef : releaseCoef;
            env += (absX - env) * coef;
            const float shaped = std::tanh (x * 1.5f) * 0.7f;
            return shaped;
        }

        float expandOut (float x) noexcept
        {
            // Approximate inverse of compressIn — boosts mid-low signals back up
            return std::sinh (x * 1.5f) / 1.5f * 1.4f;
        }

        void reset() noexcept { env = 0.0f; }
    };

    Compander companderL, companderR;
    float drivePre = 1.0f;  // CHARACTER-driven pre-BBD drive

    // 4th-order Butterworth LP via two cascaded biquad stages (RBJ Q values).
    struct ReconLP
    {
        juce::dsp::IIR::Filter<float> stage1, stage2;

        void prepare (double sr) noexcept
        {
            juce::dsp::ProcessSpec spec { sr, 1u, 1u };
            stage1.prepare (spec);
            stage2.prepare (spec);
            reset();
        }

        void reset() noexcept
        {
            stage1.reset();
            stage2.reset();
        }

        void setCutoff (double sr, float cutoffHz) noexcept
        {
            // Two cascaded biquads with Q values from Butterworth tables for 4th order:
            // Q1 = 0.541, Q2 = 1.307
            *stage1.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, cutoffHz, 0.541f);
            *stage2.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, cutoffHz, 1.307f);
        }

        float processSample (float x) noexcept
        {
            return stage2.processSample (stage1.processSample (x));
        }
    };

    ReconLP reconLpL, reconLpR;

    double sampleRate = 44100.0;
    std::vector<float> bufferL, bufferR;
    int   writePos  = 0;
    float amount    = 0.0f;
    float width     = 0.7f;
    float character = 0.3f;

    float phaseL       = 0.0f;
    float phaseR       = RIGHT_PHASE_OFFSET;
    float leftLfoRate  = LFO_RATE_MAX_HZ;
    float rightLfoRate = LFO_RATE_MAX_HZ * RIGHT_RATE_RATIO;
};
