#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <random>
#include <algorithm>

//==============================================================================
// GrainVoice: A single grain with envelope, position, playback rate, and
// per-grain wander randomization (reverse, stutter, saturation, dropout)
//==============================================================================
struct GrainVoice
{
    bool active = false;
    double readPosition = 0.0;   // Fractional position in circular buffer
    double playbackRate = 1.0;   // Combined: base pitch + wander speed
    int remainingSamples = 0;    // Samples left in this grain
    int totalSamples = 0;        // Total grain length for envelope calc

    // Wander: per-grain amplitude scale (held constant for grain lifetime)
    // 1.0 = normal, 0.0 = dropout
    float amplitudeScale = 1.0f;

    // Wander V3: per-grain saturation and stutter state
    float saturationAmount = 0.0f;   // 0-1 per-grain drive
    bool isStuttering = false;       // micro-loop mode
    double stutterAnchor = 0.0;      // loop start position
    int stutterLoopSize = 0;         // 30-120ms (sample-rate scaled)
    int stutterCounter = 0;          // samples since last loop reset

    float getEnvelope() const
    {
        if (totalSamples <= 0) return 0.0f;
        // Hanning window: 0.5 * (1 - cos(2*pi*t/N))
        const double t = static_cast<double>(totalSamples - remainingSamples);
        return static_cast<float>(0.5 * (1.0 - std::cos(2.0 * juce::MathConstants<double>::pi * t / totalSamples)));
    }
};

//==============================================================================
// GrainEngine: Circular buffer + grain pool + scheduler for one channel
//
// WANDER V3: Probability-based grain disruption. Each grain independently
// rolls dice for dramatic discrete behaviors. Higher wander = more grains
// affected, not all grains subtly affected. intensity = wander^2:
//   1. Grain size variation — 0.4x-1.6x length (always, scaled)
//   2. Reverse              — play backwards (prob: intensity * 0.5)
//   3. Half-speed           — octave down time-stretch (prob: intensity * 0.3)
//   4. Stutter              — buffer repeat 30-120ms (prob: intensity * 0.3)
//   5. Dropout              — silent grain, creates gaps (prob: intensity * 0.2)
//   6. Per-grain saturation — tanh drive 0.3-1.0 (prob: intensity * 0.35)
//   7. Timing scatter       — ±5-50ms offset (prob: intensity * 0.4)
//==============================================================================
class GrainEngine
{
public:
    static constexpr int MAX_GRAINS = 128;
    static constexpr double BUFFER_SECONDS = 5.0;

    GrainEngine() = default;

    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        sr = sampleRate;
        bufferLength = static_cast<int>(sr * BUFFER_SECONDS);
        circularBuffer.resize(static_cast<size_t>(bufferLength), 0.0f);
        std::fill(circularBuffer.begin(), circularBuffer.end(), 0.0f);
        writeHead = 0;
        spawnCounter = 0.0;

        for (auto& grain : grainPool)
            grain.active = false;

        rng.seed(42);
    }

    void reset()
    {
        std::fill(circularBuffer.begin(), circularBuffer.end(), 0.0f);
        writeHead = 0;
        spawnCounter = 0.0;
        for (auto& grain : grainPool)
            grain.active = false;
    }

    int getActiveGrainCount() const
    {
        int count = 0;
        for (const auto& grain : grainPool)
            if (grain.active) ++count;
        return count;
    }

    // Process one sample: write input, spawn grains, sum voices
    // wanderAmount: 0-1 macro randomization intensity
    float processSample(float inputSample,
                        float grainSizeMs,
                        float density,
                        float sprayPercent,
                        float pitchSemitones,
                        float wanderAmount,
                        float freezeAmount,
                        float mixAmount)
    {
        // Write input into circular buffer (freeze blends old+new)
        circularBuffer[static_cast<size_t>(writeHead)] =
            inputSample * (1.0f - freezeAmount)
            + circularBuffer[static_cast<size_t>(writeHead)] * freezeAmount;

        // Advance write head
        writeHead = (writeHead + 1) % bufferLength;

        // Grain scheduling
        if (density > 0.0f)
        {
            const double spawnInterval = sr / static_cast<double>(density);
            spawnCounter += 1.0;

            if (spawnCounter >= spawnInterval)
            {
                spawnCounter -= spawnInterval;
                spawnGrain(grainSizeMs, sprayPercent, pitchSemitones, wanderAmount);
            }
        }

        // Sum all active grains
        float wetSample = 0.0f;
        for (auto& grain : grainPool)
        {
            if (!grain.active) continue;

            // Read from circular buffer with linear interpolation
            float sample = readInterpolated(grain.readPosition);

            // Per-grain saturation (tanh waveshaping)
            if (grain.saturationAmount > 0.0f)
            {
                const float drive = 1.0f + grain.saturationAmount * 3.0f;
                sample = std::tanh(sample * drive) / std::tanh(drive);
            }

            // Stutter micro-crossfade: fade in/out at loop boundaries to kill clicks
            if (grain.isStuttering)
            {
                const int fadeLen = std::min(32, grain.stutterLoopSize / 4);
                if (fadeLen > 0)
                {
                    if (grain.stutterCounter < fadeLen)
                        sample *= static_cast<float>(grain.stutterCounter) / static_cast<float>(fadeLen);
                    else if (grain.stutterCounter > grain.stutterLoopSize - fadeLen)
                        sample *= static_cast<float>(grain.stutterLoopSize - grain.stutterCounter) / static_cast<float>(fadeLen);
                }
            }

            const float envelope = grain.getEnvelope();
            wetSample += sample * envelope * grain.amplitudeScale;

            // Advance read position: stutter micro-loop or normal
            if (grain.isStuttering)
            {
                grain.stutterCounter++;
                if (grain.stutterCounter >= grain.stutterLoopSize)
                {
                    grain.stutterCounter = 0;
                    grain.readPosition = grain.stutterAnchor; // jump back
                }
                else
                {
                    grain.readPosition += grain.playbackRate; // advance within loop
                }
            }
            else
            {
                grain.readPosition += grain.playbackRate; // normal advance
            }

            // Wrap read position
            while (grain.readPosition >= static_cast<double>(bufferLength))
                grain.readPosition -= static_cast<double>(bufferLength);
            while (grain.readPosition < 0.0)
                grain.readPosition += static_cast<double>(bufferLength);

            grain.remainingSamples--;
            if (grain.remainingSamples <= 0)
                grain.active = false;
        }

        // Soft clip the wet signal
        wetSample = softClip(wetSample);

        // Dry/wet mix
        const float mix = mixAmount * 0.01f;
        return inputSample * (1.0f - mix) + wetSample * mix;
    }

private:
    void spawnGrain(float grainSizeMs, float sprayPercent, float pitchSemitones, float wanderAmount)
    {
        // Find a free voice (or steal the oldest)
        GrainVoice* voice = nullptr;
        int oldestIdx = -1;
        int maxAge = 0;

        for (int i = 0; i < MAX_GRAINS; ++i)
        {
            if (!grainPool[i].active)
            {
                voice = &grainPool[i];
                break;
            }
            const int age = grainPool[i].totalSamples - grainPool[i].remainingSamples;
            if (age > maxAge)
            {
                maxAge = age;
                oldestIdx = i;
            }
        }

        if (voice == nullptr && oldestIdx >= 0)
            voice = &grainPool[oldestIdx];

        if (voice == nullptr) return;

        // Grain length in samples (non-const: wander size variation may modify)
        int grainLengthSamples = std::max(1, static_cast<int>(sr * grainSizeMs * 0.001));

        // Base read position from spray
        const double spray = static_cast<double>(sprayPercent) * 0.01;
        const double maxOffset = spray * static_cast<double>(bufferLength);

        std::uniform_real_distribution<double> sprayDist(0.0, std::max(0.001, maxOffset));
        const double sprayOffset = sprayDist(rng);

        double readPos = static_cast<double>(writeHead) - static_cast<double>(grainLengthSamples) - sprayOffset;

        // Base playback rate from pitch knob
        double rate = std::pow(2.0, static_cast<double>(pitchSemitones) / 12.0);

        // Default wander values (neutral)
        float ampScale = 1.0f;
        float satAmount = 0.0f;
        bool stutter = false;
        double stuttAnchor = 0.0;
        int stuttLoopSize = 0;

        // ── WANDER V3: probability-based grain disruption ──
        // Each grain independently rolls dice for dramatic discrete behaviors.
        // Higher wander = more grains affected, not all grains subtly affected.
        // Inspired by Chase Bliss Onward ERROR / Mutable Instruments Beads.
        if (wanderAmount > 0.001f)
        {
            const float intensity = wanderAmount * wanderAmount;

            std::uniform_real_distribution<float> probDist(0.0f, 1.0f);

            // 1. GRAIN SIZE VARIATION — always on (scaled by intensity)
            //    Multiply grain length by 0.4x-1.6x. Short=clicks, long=drones
            {
                const float sizeRange = 0.6f * intensity; // 0 at wander=0, 0.6 at max
                std::uniform_real_distribution<float> sizeDist(1.0f - sizeRange, 1.0f + sizeRange);
                const float sizeMult = sizeDist(rng);
                grainLengthSamples = std::max(1, static_cast<int>(static_cast<float>(grainLengthSamples) * sizeMult));
            }

            // 2. REVERSE — play grain backwards
            //    Probability: 0% at 0, up to 50% at max
            const float reverseProb = intensity * 0.5f;
            const bool isReversed = probDist(rng) < reverseProb;
            if (isReversed)
                rate = -rate;

            // 3. HALF-SPEED — octave down time-stretch
            //    Probability: 0% at 0, ~30% at max
            const float halfSpeedProb = intensity * 0.3f;
            if (probDist(rng) < halfSpeedProb)
                rate *= 0.5;

            // 4. STUTTER — buffer repeat, mutually exclusive with reverse
            //    Probability: 0% at 0, ~30% at max
            //    Loop must be 30-120ms to sound like a glitch/repeat, NOT a pitched tone.
            //    (Loops < 20ms create audible pitch at the repetition frequency.)
            //    Only activates on grains long enough for at least 2 repetitions.
            if (!isReversed)
            {
                const int minLoop = static_cast<int>(sr * 0.03);  // 30ms
                const int maxLoop = static_cast<int>(sr * 0.12);  // 120ms

                if (grainLengthSamples >= minLoop * 2) // need room for 2+ reps
                {
                    const float stutterProb = intensity * 0.3f;
                    if (probDist(rng) < stutterProb)
                    {
                        stutter = true;
                        // stuttAnchor set below after readPos is wrapped
                        const int clampedMax = std::min(maxLoop, grainLengthSamples / 2);
                        std::uniform_int_distribution<int> loopDist(minLoop, std::max(minLoop, clampedMax));
                        stuttLoopSize = loopDist(rng);
                    }
                }
            }

            // 5. DROPOUT — silent grain, creates gaps
            //    Probability: 0% at 0, ~20% at max
            const float dropoutProb = intensity * 0.2f;
            if (probDist(rng) < dropoutProb)
                ampScale = 0.0f;

            // 6. PER-GRAIN SATURATION — tanh drive applied in render loop
            //    Probability: 0% at 0, ~35% at max
            const float satProb = intensity * 0.35f;
            if (probDist(rng) < satProb)
            {
                std::uniform_real_distribution<float> satDist(0.3f, 1.0f);
                satAmount = satDist(rng);
            }

            // 7. TIMING SCATTER — random ±5-50ms offset on top of spray
            //    Probability: 0% at 0, ~40% at max
            const float scatterProb = intensity * 0.4f;
            if (probDist(rng) < scatterProb)
            {
                const float minMs = 5.0f;
                const float maxMs = 5.0f + intensity * 45.0f; // 5-50ms
                std::uniform_real_distribution<float> scatterDist(-maxMs, maxMs);
                float scatterMs = scatterDist(rng);
                // Ensure minimum magnitude
                if (std::abs(scatterMs) < minMs)
                    scatterMs = (scatterMs >= 0.0f ? minMs : -minMs);
                readPos += static_cast<double>(scatterMs) * sr * 0.001;
            }
        }

        // Wrap read position into buffer
        while (readPos < 0.0)
            readPos += static_cast<double>(bufferLength);
        while (readPos >= static_cast<double>(bufferLength))
            readPos -= static_cast<double>(bufferLength);

        // Set stutter anchor from final wrapped position
        if (stutter)
            stuttAnchor = readPos;

        voice->active = true;
        voice->readPosition = readPos;
        voice->playbackRate = rate;
        voice->totalSamples = grainLengthSamples;
        voice->remainingSamples = grainLengthSamples;
        voice->amplitudeScale = ampScale;
        voice->saturationAmount = satAmount;
        voice->isStuttering = stutter;
        voice->stutterAnchor = stuttAnchor;
        voice->stutterLoopSize = stuttLoopSize;
        voice->stutterCounter = 0;
    }

    float readInterpolated(double position) const
    {
        const int idx0 = static_cast<int>(position) % bufferLength;
        const int idx1 = (idx0 + 1) % bufferLength;
        const float frac = static_cast<float>(position - std::floor(position));

        return circularBuffer[static_cast<size_t>(idx0)] * (1.0f - frac)
             + circularBuffer[static_cast<size_t>(idx1)] * frac;
    }

    static float softClip(float x)
    {
        return std::tanh(x);
    }

    // State
    double sr = 44100.0;
    int bufferLength = 0;
    int writeHead = 0;
    double spawnCounter = 0.0;

    std::vector<float> circularBuffer;
    GrainVoice grainPool[MAX_GRAINS];
    std::mt19937 rng;
};
