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
            stretcher.setFormantFactor (formantFactor);

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
            stretchRatio = juce::jlimit (0.1f, 15.0f, r);
        }

        // SAMPLE-ENGINE-FORMANT — formant shift factor (1.0 = neutral, 2.0 = +1 oct).
        void setFormantFactor (float f) noexcept
        {
            formantFactor = juce::jlimit (0.25f, 4.0f, f);
            if (ready) stretcher.setFormantFactor (formantFactor);
        }
        void setPitchSemitones (float semis) noexcept
        {
            pitchSemitones = juce::jlimit (-24.0f, 24.0f, semis);
        }

        /** Input latency in samples — caller should prime this many input
         *  samples via seek() at note-on so the first process() call has
         *  meaningful output. */
        int inputLatency() const noexcept
        {
            return const_cast<signalsmith::stretch::SignalsmithStretch<float>&>(stretcher).inputLatency();
        }

        /** Prime the engine with input samples after reset(). Call this once
         *  at note-on with the FIRST inputLatency() source samples so the
         *  engine's STFT buffer is populated before any process() call. Without
         *  this, the first outputLatency() samples of output() are silent
         *  ramp/garbage.
         *
         *  Buffers must be distinct from any future process() call buffers. */
        void seek (const float* primeL, const float* primeR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;
            const float* inputs[2] = { primeL, channels == 2 ? primeR : primeL };
            stretcher.seek (inputs, numSamples, 1.0f /*playbackRateHint*/);
        }

        /** Process numSamples of audio.
         *
         *  Convention: stretchRatio > 1.0 means the chop plays LONGER — output
         *  duration > input duration. So for a given number of output samples,
         *  we consume LESS input. Formula: inputLen = outputLen / stretchRatio.
         *
         *  Examples:
         *    stretchRatio = 2.0 → consume 0.5x input per output sample (slower)
         *    stretchRatio = 0.5 → consume 2.0x input per output sample (faster)
         *    stretchRatio = 1.0 → transparent (input == output rate)
         *
         *  IMPORTANT: input and output buffers MUST be distinct. Per Signalsmith
         *  API contract: "The input/output buffers cannot be the same." Earlier
         *  versions of this wrapper aliased input=output and produced silent
         *  output — that bug is fixed by ensuring SamplerVoice passes separate
         *  scratch buffer pairs.
         */
        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;
            jassert (inL != outL && inR != outR && "Signalsmith requires distinct input/output buffers");

            stretcher.setTransposeSemitones (pitchSemitones);

            // Pointer-of-channels arrays for signalsmith's templated API.
            const float* inputs [2]  = { inL, channels == 2 ? inR : inL };
            float*       outputs[2] = { outL, channels == 2 ? outR : outL };

            // inputLen < outputLen when stretchRatio > 1.0 → audio plays longer.
            const int inputLen = (int) std::round ((double) numSamples / (double) stretchRatio);

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
        float  formantFactor  = 1.0f;   // SAMPLE-ENGINE-FORMANT
        bool   ready          = false;
    };
}
