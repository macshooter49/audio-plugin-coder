// BeatsEngine.h
//
// Granular block-loop time-stretcher for Beats warp mode. This is the
// "choppy / stuttery" character mode — the Ableton Beats tell.
//
// Algorithm (block-level granular loop):
//   - The voice provides inputLen samples of source audio per process call,
//     where inputLen = numSamples / stretchRatio (so stretchRatio > 1 means
//     less input per output block — we stretch by REPEATING input).
//   - The block is divided into G grains of size grainIn = inputLen / G.
//     Output grain size grainOut = numSamples / G.
//   - For each output grain, the read position inside its source grain
//     advances at pitchRatio per output sample, modulo grainIn. That mod
//     is what produces the stutter: when grainOut > grainIn (stretchRatio
//     > 1), the read wraps multiple times within the source grain,
//     audibly repeating it.
//   - When grainOut < grainIn (stretchRatio < 1), the read only covers
//     part of the source grain before the grain ends — output plays
//     faster, skipping audio.
//
// Pitch shift is applied independently by scaling the read step (pitchRatio
// = 2^(semitones/12)). At pitchRatio = 1.0 with stretchRatio = 1.0, output
// is a pass-through.
//
// Latency: 0. Beats has no spectral pre-roll, so seek() is a no-op.
//
// RT-safety: process() is allocation-free, branch-light. prepare()/reset()
// may allocate but are only called outside the audio thread or at note-on.
//
#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <cstring>

namespace tw
{
    class BeatsEngine
    {
    public:
        BeatsEngine() = default;

        void prepare (double sampleRateHz, int numChannels, int /*blockSize*/)
        {
            sampleRate = sampleRateHz;
            channels   = juce::jlimit (1, 2, numChannels);
            ready      = true;
        }

        void reset() noexcept { /* stateless: no internal buffers to clear */ }

        bool isReady() const noexcept { return ready; }
        int  inputLatency() const noexcept { return 0; }

        void setStretchRatio (float r) noexcept
        {
            stretchRatio = juce::jlimit (0.1f, 15.0f, r);
        }

        void setPitchSemitones (float semis) noexcept
        {
            pitchSemitones = juce::jlimit (-24.0f, 24.0f, semis);
        }

        // No-op — Beats has no STFT to prime.
        void seek (const float* /*primeL*/, const float* /*primeR*/, int /*n*/) {}

        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;
            jassert (inL != outL && inR != outR && "Engine requires distinct buffers");

            const int inputLen = juce::jmax (1, (int) std::round (
                (double) numSamples / (double) stretchRatio));

            // Grain count: enough grains to give 4+ stutter cycles per
            // block at moderate stretch, but capped so each grain has at
            // least ~256 samples (otherwise the stutter becomes a sub-
            // audio-rate hum). 4 grains per block by default.
            int G = 4;
            const int minGrainIn = 128;
            while (G > 1 && inputLen / G < minGrainIn) G--;
            const int grainIn  = inputLen   / G;
            const int grainOut = numSamples / G;

            if (grainIn < 4 || grainOut < 4) {
                // Block too short for grain splitting — fall back to a
                // simple wrap-resample (still keeps stutter character for
                // tiny edge-case blocks).
                singleGrainProcess (inL, inR, outL, outR, numSamples, inputLen);
                return;
            }

            const double pitchRatio = std::pow (2.0, (double) pitchSemitones / 12.0);

            for (int g = 0; g < G; g++)
            {
                const int inGrainStart  = g * grainIn;
                const int outGrainStart = g * grainOut;

                for (int s = 0; s < grainOut; s++)
                {
                    // Read position within the source grain, scaled by
                    // pitch, wrapped (modulo). The wrap is the stutter.
                    double posInGrain = std::fmod ((double) s * pitchRatio,
                                                   (double) grainIn);
                    if (posInGrain < 0) posInGrain += grainIn;

                    int i0 = (int) posInGrain;
                    int i1 = (i0 + 1) % grainIn;
                    float frac = (float) (posInGrain - i0);

                    int idxL = inGrainStart + i0;
                    int idxR = inGrainStart + i1;
                    if (idxL >= inputLen) idxL = inputLen - 1;
                    if (idxR >= inputLen) idxR = inputLen - 1;

                    float sL = inL[idxL] + frac * (inL[idxR] - inL[idxL]);
                    float sR = inR[idxL] + frac * (inR[idxR] - inR[idxL]);

                    // Soft fade at the boundary of every grain repeat —
                    // 32-sample raised-cosine attack on each loop start to
                    // mask the click that would otherwise fire on every
                    // grain wrap. Without this the stutter sounds like
                    // a digital glitch instead of a musical loop.
                    if (i0 < kBoundaryFade)
                    {
                        const float f = 0.5f * (1.0f - std::cos (
                            juce::MathConstants<float>::pi
                            * (float) (i0 + 1) / (float) kBoundaryFade));
                        sL *= f;
                        sR *= f;
                    }

                    outL[outGrainStart + s] = sL;
                    outR[outGrainStart + s] = sR;
                }
            }

            // Any leftover samples (numSamples not perfectly divisible by G)
            // get filled by passthrough so we don't ship zeros at the tail.
            const int filled = G * grainOut;
            for (int i = filled; i < numSamples; i++)
            {
                const int src = juce::jlimit (0, inputLen - 1, i);
                outL[i] = inL[src];
                outR[i] = inR[src];
            }
        }

    private:
        // Fallback for tiny blocks (no room for grain split). Stretches
        // the whole input across the output via wrap-resample.
        void singleGrainProcess (const float* inL, const float* inR,
                                 float* outL, float* outR,
                                 int numSamples, int inputLen)
        {
            const double pitchRatio = std::pow (2.0, (double) pitchSemitones / 12.0);
            for (int i = 0; i < numSamples; i++)
            {
                double pos = std::fmod ((double) i * pitchRatio, (double) inputLen);
                if (pos < 0) pos += inputLen;
                int i0 = (int) pos;
                int i1 = (i0 + 1) % inputLen;
                float frac = (float) (pos - i0);
                outL[i] = inL[i0] + frac * (inL[i1] - inL[i0]);
                outR[i] = inR[i0] + frac * (inR[i1] - inR[i0]);
            }
        }

        static constexpr int kBoundaryFade = 32;  // raised-cosine grain-start fade

        double sampleRate     = 48000.0;
        int    channels       = 2;
        float  stretchRatio   = 1.0f;
        float  pitchSemitones = 0.0f;
        bool   ready          = false;
    };
}
