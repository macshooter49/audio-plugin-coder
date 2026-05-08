// SamplerVoice.h
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SampleBuffer.h"
#include <atomic>
#include <cmath>

namespace tw
{
    class SamplerVoice : public juce::SynthesiserVoice
    {
    public:
        SamplerVoice (SampleBuffer& sb, std::atomic<int>& rootNoteRef) noexcept
            : sample (sb), rootNoteParam (rootNoteRef) {}

        bool canPlaySound (juce::SynthesiserSound*) override { return true; }

        void startNote (int midiNoteNumber, float velocity,
                        juce::SynthesiserSound*, int /*pitchWheelPos*/) override
        {
            currentNote     = midiNoteNumber;
            currentVelocity = velocity;
            playhead        = 0.0;
            updatePitchRatio();
            isActive = true;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (! allowTailOff)
            {
                clearCurrentNote();
                isActive = false;
                playhead = 0.0;
            }
            // Task 8 wires the AR release; stopNote without tail-off cuts immediately.
        }

        void pitchWheelMoved (int /*newPitchWheelValue*/) override
        {
            // Task 7 hooks pitch bend in here.
        }

        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                              int startSample, int numSamples) override
        {
            if (! isActive) return;

            auto buf = sample.load();
            if (! buf || buf->getNumSamples() == 0) return;

            const int    bufLen   = buf->getNumSamples();
            const int    bufChans = buf->getNumChannels();
            const double pitchInc = pitchRatio;

            auto* outL = outputBuffer.getWritePointer (0, startSample);
            auto* outR = outputBuffer.getNumChannels() > 1
                         ? outputBuffer.getWritePointer (1, startSample) : outL;

            const auto* inL = buf->getReadPointer (0);
            const auto* inR = bufChans > 1 ? buf->getReadPointer (1) : inL;

            for (int i = 0; i < numSamples; ++i)
            {
                if (playhead >= static_cast<double> (bufLen - 1))
                {
                    clearCurrentNote();
                    isActive = false;
                    return;
                }

                const auto i0 = static_cast<int> (playhead);
                const auto frac = static_cast<float> (playhead - i0);
                const auto sampleL = inL[i0] + frac * (inL[i0 + 1] - inL[i0]);
                const auto sampleR = inR[i0] + frac * (inR[i0 + 1] - inR[i0]);

                const auto gain = currentVelocity;
                outL[i] += sampleL * gain;
                outR[i] += sampleR * gain;

                playhead += pitchInc;
            }
        }

    private:
        void updatePitchRatio()
        {
            const int rootNote = rootNoteParam.load();
            const double semitones = static_cast<double> (currentNote - rootNote);
            pitchRatio = std::pow (2.0, semitones / 12.0);
        }

        SampleBuffer&     sample;
        std::atomic<int>& rootNoteParam;

        int    currentNote     = -1;
        float  currentVelocity = 0.0f;
        double playhead        = 0.0;
        double pitchRatio      = 1.0;
        bool   isActive        = false;
    };

    struct SamplerSound : public juce::SynthesiserSound
    {
        bool appliesToNote    (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };
}
