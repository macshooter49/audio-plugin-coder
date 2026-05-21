// SignalsmithEngine.h
//
// Wraps signalsmith::stretch::SignalsmithStretch<float> with a sampler-voice
// friendly interface. Configured for TONES character — formants preserved at
// all pitch shifts. One instance owned per voice via WarpProcessor; lazily
// constructed when the voice first dispatches a non-NONE warp mode.
//
// RT-safety: process() is allocation-free. prepare() and reset() may allocate
// — call only from the audio thread at note-on boundaries.
//
// Latency: spectral stretchers introduce inherent latency. Signalsmith reports
// it via inputLatency() / outputLatency(). At note-on we reset() so the engine
// starts cold for each trigger — small bit of "pre-roll" silence is acceptable
// for one-shot sampler use.
//
#pragma once

#include <juce_core/juce_core.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <cmath>
#include <cstring>

namespace tw
{
    class SignalsmithEngine
    {
    public:
        SignalsmithEngine() = default;

        /** Allocates internal buffers. Call once before audio thread touches the instance. */
        void prepare (double sampleRateHz, int numChannels, int /*blockSize*/)
        {
            sampleRate = sampleRateHz;
            channels   = juce::jlimit (1, 2, numChannels);

            // Configure stretcher for TONES character. The library's presetDefault
            // picks sensible windowing for the SR. setFormantFactor(1.0) keeps
            // formants where they belong even at non-unity pitch shifts.
            stretcher.presetDefault (channels, (float) sampleRate);
            stretcher.setFormantFactor (1.0f);

            ready = true;
        }

        /** Resets internal state to silence. Must be called at note-on to prevent voice bleed. */
        void reset()
        {
            if (! ready) return;
            stretcher.reset();
        }

        bool isReady() const noexcept { return ready; }

        void setStretchRatio (float r) noexcept
        {
            stretchRatio = juce::jlimit (0.25f, 4.0f, r);
        }

        void setPitchSemitones (float semis) noexcept
        {
            pitchSemitones = juce::jlimit (-24.0f, 24.0f, semis);
        }

        /** Process numSamples of audio.
         *
         *  Input + output may alias (signalsmith handles in-place internally via
         *  its STFT overlap-add buffer). Output is the stretched + pitched audio
         *  matching numSamples worth of OUTPUT time. Input length is derived
         *  from the stretch ratio: inputLen = outputLen * stretchRatio.
         */
        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;

            stretcher.setTransposeSemitones (pitchSemitones);

            // Pointer-of-channels arrays for signalsmith's templated API.
            const float* inputs [2]  = { inL, channels == 2 ? inR : inL };
            float*       outputs[2] = { outL, channels == 2 ? outR : outL };

            // Derive input length from desired output + ratio. The library expects
            // inputSamples to match the audio you've ADVANCED in the source over
            // this block; outputSamples is what gets written to outputs.
            const int inputLen = (int) std::round ((double) numSamples * (double) stretchRatio);

            stretcher.process (inputs, inputLen, outputs, numSamples);

            // Mono source → duplicate L to R for stereo voice path.
            if (channels == 1 && outR != outL)
                std::memcpy (outR, outL, sizeof (float) * (size_t) numSamples);
        }

    private:
        signalsmith::stretch::SignalsmithStretch<float> stretcher;
        double sampleRate     = 48000.0;
        int    channels       = 2;
        float  stretchRatio   = 1.0f;
        float  pitchSemitones = 0.0f;
        bool   ready          = false;
    };
}
