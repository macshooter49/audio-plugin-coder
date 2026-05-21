// WarpProcessor.h
//
// Per-voice warp dispatcher. Owns engine instances and routes process()
// calls based on the current WarpMode. NONE mode is a memcpy fast path that
// never touches an engine — voices that never opt into warp stay zero-cost.
//
// Lazy engine allocation: SignalsmithEngine is only constructed on the
// first non-NONE setMode() call. Voices that never warp keep
// hasEngineAllocated() == false for their entire lifetime, paying no
// memory cost.
//
// RT-safety: process() is allocation-free. setMode() may allocate the first
// time it transitions out of NONE; subsequent transitions are allocation-
// free. Audio thread should call setMode() only at note-on boundaries.
//
// Phase 1 ships TONES. Phase 2 plugs in BEATS + TEXTURE engines.
//
#pragma once

#include <juce_core/juce_core.h>
#include "../Slice.h"
#include "SignalsmithEngine.h"
#include <memory>
#include <cstring>

namespace tw
{
    class WarpProcessor
    {
    public:
        WarpProcessor() = default;

        void prepare (double sampleRateHz, int numChannels, int blockSize)
        {
            sampleRate = sampleRateHz;
            channels   = juce::jlimit (1, 2, numChannels);
            blockMax   = juce::jmax (1, blockSize);
            prepared   = true;

            if (signalsmithEngine)
                signalsmithEngine->prepare (sampleRate, channels, blockMax);
        }

        void setMode (WarpMode m)
        {
            if (m == mode) return;
            mode = m;
            if (mode == WarpMode::Tones)
            {
                if (! signalsmithEngine)
                {
                    signalsmithEngine = std::make_unique<SignalsmithEngine>();
                    if (prepared)
                        signalsmithEngine->prepare (sampleRate, channels, blockMax);
                }
                signalsmithEngine->reset();
                signalsmithEngine->setStretchRatio   (stretchRatio);
                signalsmithEngine->setPitchSemitones (pitchSemitones);
            }
            // Beats / Texture engines plug in here in Phase 2. For now, those
            // modes route through SignalsmithEngine too as a temporary fallback
            // so the UI works end-to-end before the real engines exist. This
            // keeps audio thread behavior safe.
            else if (mode == WarpMode::Beats || mode == WarpMode::Texture)
            {
                if (! signalsmithEngine)
                {
                    signalsmithEngine = std::make_unique<SignalsmithEngine>();
                    if (prepared)
                        signalsmithEngine->prepare (sampleRate, channels, blockMax);
                }
                signalsmithEngine->reset();
                signalsmithEngine->setStretchRatio   (stretchRatio);
                signalsmithEngine->setPitchSemitones (pitchSemitones);
            }
        }

        void setStretchRatio (float r) noexcept
        {
            stretchRatio = juce::jlimit (0.25f, 4.0f, r);
            if (signalsmithEngine) signalsmithEngine->setStretchRatio (stretchRatio);
        }

        void setPitchSemitones (float semis) noexcept
        {
            pitchSemitones = semis;
            if (signalsmithEngine) signalsmithEngine->setPitchSemitones (semis);
        }

        /** Audio-thread reset; safe to call at every note-on. */
        void noteOnReset() noexcept
        {
            if (signalsmithEngine) signalsmithEngine->reset();
        }

        bool hasEngineAllocated() const noexcept { return signalsmithEngine != nullptr; }
        WarpMode getMode() const noexcept { return mode; }

        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples)
        {
            if (mode == WarpMode::None || ! signalsmithEngine)
            {
                // Identity passthrough. memcpy explicitly; output may differ
                // from input pointer-wise even if values are identical.
                if (outL != inL) std::memcpy (outL, inL, sizeof (float) * (size_t) numSamples);
                if (outR != inR) std::memcpy (outR, inR, sizeof (float) * (size_t) numSamples);
                return;
            }
            signalsmithEngine->process (inL, inR, outL, outR, numSamples);
        }

    private:
        WarpMode mode           = WarpMode::None;
        float    stretchRatio   = 1.0f;
        float    pitchSemitones = 0.0f;

        double sampleRate = 48000.0;
        int    channels   = 2;
        int    blockMax   = 512;
        bool   prepared   = false;

        std::unique_ptr<SignalsmithEngine> signalsmithEngine;
    };
}
