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
#include "BeatsEngine.h"
#include <memory>
#include <cstring>
#include <cmath>

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
            if (beatsEngine)
                beatsEngine->prepare (sampleRate, channels, blockMax);
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
            else if (mode == WarpMode::Beats)
            {
                if (! beatsEngine)
                {
                    beatsEngine = std::make_unique<BeatsEngine>();
                    if (prepared)
                        beatsEngine->prepare (sampleRate, channels, blockMax);
                }
                beatsEngine->reset();
                beatsEngine->setStretchRatio   (stretchRatio);
                beatsEngine->setPitchSemitones (pitchSemitones);
            }
            // Texture engine plugs in next. For now, Texture routes through
            // Signalsmith too as a placeholder so the dispatch is wired.
            else if (mode == WarpMode::Texture)
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
            stretchRatio = juce::jlimit (0.1f, 15.0f, r);
            if (signalsmithEngine) signalsmithEngine->setStretchRatio (stretchRatio);
            if (beatsEngine)       beatsEngine      ->setStretchRatio (stretchRatio);
        }

        void setPitchSemitones (float semis) noexcept
        {
            pitchSemitones = semis;
            if (signalsmithEngine) signalsmithEngine->setPitchSemitones (semis);
            if (beatsEngine)       beatsEngine      ->setPitchSemitones (semis);
        }

        /** Audio-thread reset; safe to call at every note-on. */
        void noteOnReset() noexcept
        {
            if (signalsmithEngine) signalsmithEngine->reset();
            if (beatsEngine)       beatsEngine      ->reset();
        }

        /** Returns the active engine's input latency in samples. Caller
         *  (SamplerVoice) uses this to determine how many source samples to
         *  feed via seek() at note-on. Beats has 0 latency; only Signalsmith
         *  needs priming. */
        int inputLatency() const noexcept
        {
            if (mode == WarpMode::Tones || mode == WarpMode::Texture)
                return signalsmithEngine ? signalsmithEngine->inputLatency() : 0;
            if (mode == WarpMode::Beats)
                return beatsEngine ? beatsEngine->inputLatency() : 0;
            return 0;
        }

        /** Prime the active engine after reset(). Only Signalsmith uses it
         *  meaningfully; Beats::seek is a no-op. */
        void seek (const float* primeL, const float* primeR, int numSamples)
        {
            if (mode == WarpMode::Tones || mode == WarpMode::Texture)
            {
                if (signalsmithEngine) signalsmithEngine->seek (primeL, primeR, numSamples);
            }
            else if (mode == WarpMode::Beats)
            {
                if (beatsEngine) beatsEngine->seek (primeL, primeR, numSamples);
            }
        }

        bool hasEngineAllocated() const noexcept
        {
            return signalsmithEngine != nullptr || beatsEngine != nullptr;
        }
        WarpMode getMode() const noexcept { return mode; }

        /** How many source samples the caller must fill into the engine's
         *  input buffer to produce numSamples of output.
         *
         *  For Signalsmith (Tones / Texture) pitch is handled spectrally,
         *  so we only divide by stretchRatio. For BeatsEngine the pitch
         *  advances cyclePos directly through source, so source-read per
         *  output sample = pitchRatio / stretchRatio. Feeding only
         *  numSamples/stretchRatio at pitchRatio>1 starved the history
         *  buffer (loopAnchor outran writeIdx after a few beats) and
         *  produced buzz on every chromatic note above the root.
         *
         *  WarpMode::None returns numSamples (unused — None bypasses warp
         *  entirely and reads source directly in SamplerVoice). */
        int sourceSamplesPerBlock (int numSamples) const noexcept
        {
            const double sr = juce::jmax (0.0001, (double) stretchRatio);
            if (mode == WarpMode::Beats)
            {
                const double pr = std::pow (2.0, (double) pitchSemitones / 12.0);
                return juce::jmax (1, (int) std::round ((double) numSamples * pr / sr));
            }
            return juce::jmax (1, (int) std::round ((double) numSamples / sr));
        }

        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples)
        {
            // Identity passthrough for None mode (or if the requested
            // engine isn't allocated yet for some reason — defensive).
            if (mode == WarpMode::None)
            {
                if (outL != inL) std::memcpy (outL, inL, sizeof (float) * (size_t) numSamples);
                if (outR != inR) std::memcpy (outR, inR, sizeof (float) * (size_t) numSamples);
                return;
            }

            if (mode == WarpMode::Beats && beatsEngine)
            {
                beatsEngine->process (inL, inR, outL, outR, numSamples);
                return;
            }
            if (signalsmithEngine)
            {
                signalsmithEngine->process (inL, inR, outL, outR, numSamples);
                return;
            }

            // No active engine — fall back to identity to avoid silence.
            if (outL != inL) std::memcpy (outL, inL, sizeof (float) * (size_t) numSamples);
            if (outR != inR) std::memcpy (outR, inR, sizeof (float) * (size_t) numSamples);
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
        std::unique_ptr<BeatsEngine>       beatsEngine;
    };
}
