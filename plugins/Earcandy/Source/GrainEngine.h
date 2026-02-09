#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <random>

//==============================================================================
// GrainVoice: A single grain with envelope, position, and playback rate
//==============================================================================
struct GrainVoice
{
    bool active = false;
    double readPosition = 0.0;   // Fractional position in circular buffer
    double playbackRate = 1.0;   // Pitch shift: 2^(semitones/12)
    int remainingSamples = 0;    // Samples left in this grain
    int totalSamples = 0;        // Total grain length for envelope calc

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

    // Process one sample: write input, spawn grains, sum voices, apply feedback
    float processSample(float inputSample,
                        float grainSizeMs,
                        float density,
                        float sprayPercent,
                        float pitchSemitones,
                        float feedbackAmount,
                        float mixAmount)
    {
        // Write input + feedback into circular buffer
        circularBuffer[static_cast<size_t>(writeHead)] = inputSample + feedbackSample * feedbackAmount;

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
                spawnGrain(grainSizeMs, sprayPercent, pitchSemitones);
            }
        }

        // Sum all active grains
        float wetSample = 0.0f;
        for (auto& grain : grainPool)
        {
            if (!grain.active) continue;

            // Read from circular buffer with linear interpolation
            const float sample = readInterpolated(grain.readPosition);
            const float envelope = grain.getEnvelope();
            wetSample += sample * envelope;

            // Advance read position by playback rate
            grain.readPosition += grain.playbackRate;

            // Wrap read position
            while (grain.readPosition >= static_cast<double>(bufferLength))
                grain.readPosition -= static_cast<double>(bufferLength);
            while (grain.readPosition < 0.0)
                grain.readPosition += static_cast<double>(bufferLength);

            grain.remainingSamples--;
            if (grain.remainingSamples <= 0)
                grain.active = false;
        }

        // Soft clip the wet signal to prevent feedback runaway
        wetSample = softClip(wetSample);

        // Store for feedback path
        feedbackSample = wetSample;

        // Dry/wet mix
        const float mix = mixAmount * 0.01f;
        return inputSample * (1.0f - mix) + wetSample * mix;
    }

private:
    void spawnGrain(float grainSizeMs, float sprayPercent, float pitchSemitones)
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
            // Track oldest for stealing
            const int age = grainPool[i].totalSamples - grainPool[i].remainingSamples;
            if (age > maxAge)
            {
                maxAge = age;
                oldestIdx = i;
            }
        }

        // Steal oldest if pool is full
        if (voice == nullptr && oldestIdx >= 0)
            voice = &grainPool[oldestIdx];

        if (voice == nullptr) return;

        // Calculate grain length in samples
        const int grainLengthSamples = std::max(1, static_cast<int>(sr * grainSizeMs * 0.001));

        // Calculate read position based on spray
        const double spray = static_cast<double>(sprayPercent) * 0.01;
        const double maxOffset = spray * static_cast<double>(bufferLength);

        std::uniform_real_distribution<double> dist(0.0, maxOffset);
        const double offset = dist(rng);

        double readPos = static_cast<double>(writeHead) - static_cast<double>(grainLengthSamples) - offset;
        while (readPos < 0.0)
            readPos += static_cast<double>(bufferLength);

        // Playback rate from pitch
        const double rate = std::pow(2.0, static_cast<double>(pitchSemitones) / 12.0);

        voice->active = true;
        voice->readPosition = readPos;
        voice->playbackRate = rate;
        voice->totalSamples = grainLengthSamples;
        voice->remainingSamples = grainLengthSamples;
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
        // tanh saturation — smooth, musical limiting
        return std::tanh(x);
    }

    // State
    double sr = 44100.0;
    int bufferLength = 0;
    int writeHead = 0;
    double spawnCounter = 0.0;
    float feedbackSample = 0.0f;

    std::vector<float> circularBuffer;
    GrainVoice grainPool[MAX_GRAINS];
    std::mt19937 rng;
};
