#pragma once

#include <cmath>
#include <vector>
#include <algorithm>

//==============================================================================
// SpaceReverb: Stereo Freeverb-style reverb (header-only)
//
// Architecture:
//   - 8 parallel comb filters per channel (with one-pole damping in feedback)
//   - 4 series allpass diffusers per channel
//   - Stereo spread via offset delay lengths between L and R
//
// Parameters (all 0-1 normalized):
//   - size:  scales comb filter delay times (room size)
//   - decay: feedback amount in comb filters (reverb tail length)
//   - tone:  damping lowpass in comb feedback (0=bright, 1=dark)
//   - mix:   wet/dry balance
//
// Based on Freeverb by Jezar at Dreampoint.
//==============================================================================
class SpaceReverb
{
public:
    SpaceReverb() = default;

    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        sr = sampleRate;

        // Scale reference delay lengths from 44100 Hz to current sample rate
        const double ratio = sr / 44100.0;

        for (int i = 0; i < NUM_COMBS; ++i)
        {
            int lenL = static_cast<int>(combTunings[i] * ratio);
            int lenR = static_cast<int>((combTunings[i] + STEREO_SPREAD) * ratio);
            combL[i].resize(lenL);
            combR[i].resize(lenR);
        }

        for (int i = 0; i < NUM_ALLPASSES; ++i)
        {
            int lenL = static_cast<int>(allpassTunings[i] * ratio);
            int lenR = static_cast<int>((allpassTunings[i] + STEREO_SPREAD) * ratio);
            allpassL[i].resize(lenL);
            allpassR[i].resize(lenR);
        }

        reset();
    }

    void reset()
    {
        for (int i = 0; i < NUM_COMBS; ++i)
        {
            combL[i].clear();
            combR[i].clear();
        }
        for (int i = 0; i < NUM_ALLPASSES; ++i)
        {
            allpassL[i].clear();
            allpassR[i].clear();
        }
    }

    // Process one stereo sample pair in-place
    // size: 0-1, decay: 0-1, tone: 0-1, mix: 0-1
    void processStereo(float& left, float& right,
                       float size, float decay, float tone, float mix)
    {
        // Input: mono sum scaled down
        const float input = (left + right) * 0.5f * 0.5f;

        // Map parameters
        const float feedback = 0.5f + decay * 0.47f;  // 0.5 to 0.97
        const float damp1 = tone * 0.4f;              // 0 to 0.4
        const float damp2 = 1.0f - damp1;
        const float roomSize = 0.3f + size * 0.7f;    // 0.3 to 1.0 (scales delay read position)

        float outL = 0.0f;
        float outR = 0.0f;

        // Parallel comb filters
        for (int i = 0; i < NUM_COMBS; ++i)
        {
            outL += combL[i].process(input, feedback, damp1, damp2, roomSize);
            outR += combR[i].process(input, feedback, damp1, damp2, roomSize);
        }

        // Series allpass diffusers
        for (int i = 0; i < NUM_ALLPASSES; ++i)
        {
            outL = allpassL[i].process(outL);
            outR = allpassR[i].process(outR);
        }

        // Wet/dry mix
        left  = left  * (1.0f - mix) + outL * mix;
        right = right * (1.0f - mix) + outR * mix;
    }

private:
    //==============================================================================
    // Comb filter with damping
    struct CombFilter
    {
        void resize(int newSize)
        {
            size = std::max(newSize, 1);
            buffer.resize(static_cast<size_t>(size), 0.0f);
            writePos = 0;
            filterState = 0.0f;
        }

        void clear()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            filterState = 0.0f;
            writePos = 0;
        }

        float process(float input, float feedback, float damp1, float damp2, float roomSize)
        {
            // Read from delay buffer with room-size scaling
            int readOffset = static_cast<int>(static_cast<float>(size) * roomSize);
            if (readOffset < 1) readOffset = 1;
            if (readOffset > size) readOffset = size;

            int readPos = writePos - readOffset;
            if (readPos < 0) readPos += size;

            float output = buffer[static_cast<size_t>(readPos)];

            // One-pole lowpass damping in feedback loop
            filterState = output * damp2 + filterState * damp1;

            // Write: input + filtered feedback
            buffer[static_cast<size_t>(writePos)] = input + filterState * feedback;

            writePos = (writePos + 1) % size;
            return output;
        }

        std::vector<float> buffer;
        int size = 1;
        int writePos = 0;
        float filterState = 0.0f;
    };

    //==============================================================================
    // Allpass diffuser
    struct AllpassFilter
    {
        void resize(int newSize)
        {
            size = std::max(newSize, 1);
            buffer.resize(static_cast<size_t>(size), 0.0f);
            writePos = 0;
        }

        void clear()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }

        float process(float input)
        {
            float delayed = buffer[static_cast<size_t>(writePos)];
            float output = delayed - input;
            buffer[static_cast<size_t>(writePos)] = input + delayed * ALLPASS_FEEDBACK;

            writePos = (writePos + 1) % size;
            return output;
        }

        static constexpr float ALLPASS_FEEDBACK = 0.5f;

        std::vector<float> buffer;
        int size = 1;
        int writePos = 0;
    };

    //==============================================================================
    double sr = 44100.0;

    static constexpr int NUM_COMBS = 8;
    static constexpr int NUM_ALLPASSES = 4;
    static constexpr int STEREO_SPREAD = 23;

    // Freeverb reference tunings at 44100 Hz
    static constexpr int combTunings[NUM_COMBS] = {
        1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617
    };
    static constexpr int allpassTunings[NUM_ALLPASSES] = {
        556, 441, 341, 225
    };

    CombFilter combL[NUM_COMBS];
    CombFilter combR[NUM_COMBS];
    AllpassFilter allpassL[NUM_ALLPASSES];
    AllpassFilter allpassR[NUM_ALLPASSES];
};
