// BeatsEngine.h
//
// Granular stutter time-stretcher for Beats warp mode — v2 (persistent
// state). The "choppy unique artifacts" north star.
//
// Why v2: the original block-level design tied grain size to the audio
// block (grainSize = inputLen / G ≈ 5 ms at default settings). That put
// the wrap frequency at 200 Hz, audibly buzzy / "robotic". Persistent
// history with a fixed ~30 ms grain pushes the wrap rate down to ~33 Hz
// — below the audio band — so cycle boundaries don't ring.
//
// Algorithm:
//   - Maintain a circular history buffer of recent input (~4 s of audio).
//   - Each grain is a fixed-size chunk of history (grainSize samples,
//     30 ms by default).
//   - Per output sample: read from (grainAnchor + (phase % grainSize)),
//     where phase advances at pitchRatio per sample. The mod wraps —
//     that is the stutter at stretchRatio > 1 (grain repeats), the
//     partial-skip at stretchRatio < 1 (only part of grain read before
//     advancing).
//   - When the grain's output budget (outputsPerGrain = grainSize *
//     stretchRatio) is exhausted, advance grainAnchor forward by
//     grainSize in source space and start the next grain.
//   - Hann fades on the first and last ~6% of each wrap cycle mask the
//     boundary clicks without creating obvious tremolo dips.
//
// Latency: inputLatency() returns grainSize so the voice's seek()
// priming can pre-fill the history with the chop's opening samples —
// no silent bootstrap ramp at note-on.
//
// RT-safety: process() is allocation-free.
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

            // Circular history — ~4 s at SR, rounded up to a power of two
            // so reads can use a bitmask. 32 k samples minimum so even at
            // very low SR we have enough headroom.
            int target = 1;
            while (target < (int) (sampleRate * 4.0)) target *= 2;
            if (target < 32768) target = 32768;
            historyL.realloc ((size_t) target);
            historyR.realloc ((size_t) target);
            std::memset (historyL.getData(), 0, (size_t) target * sizeof (float));
            std::memset (historyR.getData(), 0, (size_t) target * sizeof (float));
            historyMask = target - 1;

            // 30 ms grain — wraps at ~33 Hz (sub-audio), Hann fades on
            // ~6% of each edge. Clamped so we don't go absurdly small at
            // low SR or absurdly big at high SR.
            targetGrainSize = juce::jlimit (512, 4096, (int) (sampleRate * 0.030));
            fadeLen         = juce::jmax (16, targetGrainSize / 16);

            ready = true;
            reset();
        }

        void reset() noexcept
        {
            historyWriteIdx   = 0;
            grainAnchor       = 0;
            outputsThisGrain  = 0;
            firstBlockPending = true;
        }

        bool isReady()      const noexcept { return ready; }
        int  inputLatency() const noexcept { return targetGrainSize; }

        void setStretchRatio (float r) noexcept
        {
            stretchRatio = juce::jlimit (0.1f, 15.0f, r);
        }

        void setPitchSemitones (float s) noexcept
        {
            pitchSemitones = juce::jlimit (-24.0f, 24.0f, s);
        }

        // Voice calls this once at note-on with the chop's opening
        // inputLatency() samples — fills history so the first process()
        // call has a full grain available immediately.
        void seek (const float* primeL, const float* primeR, int n)
        {
            if (! ready || n <= 0) return;
            for (int i = 0; i < n; i++)
            {
                const int writeAt = historyWriteIdx & historyMask;
                historyL[writeAt] = primeL ? primeL[i] : 0.0f;
                historyR[writeAt] = primeR ? primeR[i] : (primeL ? primeL[i] : 0.0f);
                historyWriteIdx++;
            }
        }

        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;
            jassert (inL != outL && inR != outR && "Engine requires distinct buffers");

            const int inputLen = juce::jmax (1, (int) std::round (
                (double) numSamples / (double) stretchRatio));
            const double pitchRatio = std::pow (2.0, (double) pitchSemitones / 12.0);

            // Append this block's input to history.
            for (int i = 0; i < inputLen; i++)
            {
                const int writeAt = historyWriteIdx & historyMask;
                historyL[writeAt] = inL[i];
                historyR[writeAt] = inR[i];
                historyWriteIdx++;
            }

            // Bootstrap — need a full grain of history before producing
            // output. seek() should have primed this; if not (short chop,
            // small prime), output silence until enough history.
            if (firstBlockPending)
            {
                if (historyWriteIdx >= targetGrainSize)
                {
                    grainAnchor = 0;
                    outputsThisGrain = 0;
                    firstBlockPending = false;
                }
                else
                {
                    std::memset (outL, 0, sizeof (float) * (size_t) numSamples);
                    std::memset (outR, 0, sizeof (float) * (size_t) numSamples);
                    return;
                }
            }

            const int outputsPerGrain = juce::jmax (1, (int) std::round (
                (double) targetGrainSize * (double) stretchRatio));

            for (int i = 0; i < numSamples; i++)
            {
                // Advance to next grain when current grain's output
                // budget runs out.
                if (outputsThisGrain >= outputsPerGrain)
                {
                    grainAnchor += targetGrainSize;
                    outputsThisGrain = 0;
                }

                // Phase position within the grain. Advances at pitchRatio
                // per output sample; modulo wraps for stutter when
                // stretchRatio > 1 (grain replays) or trims when
                // stretchRatio < 1 (grain partially read before advance).
                const double phase = (double) outputsThisGrain * pitchRatio;
                double phaseWrapped = std::fmod (phase, (double) targetGrainSize);
                if (phaseWrapped < 0) phaseWrapped += targetGrainSize;

                // Linear-interpolated history read.
                const int absIdx = grainAnchor + (int) phaseWrapped;
                const int idx0   = absIdx & historyMask;
                const int idx1   = (absIdx + 1) & historyMask;
                const float frac = (float) (phaseWrapped - (int) phaseWrapped);
                const float sL = historyL[idx0] + frac * (historyL[idx1] - historyL[idx0]);
                const float sR = historyR[idx0] + frac * (historyR[idx1] - historyR[idx0]);

                // Soft Hann edges — small enough to be inaudible at
                // stretchRatio=1 with pitchRatio=1 (just grain-boundary
                // rounding) but masks the wrap clicks at higher stretch.
                const int posInt = (int) phaseWrapped;
                float winGain = 1.0f;
                if (posInt < fadeLen)
                {
                    const float t = (float) (posInt + 1) / (float) fadeLen;
                    winGain = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * t));
                }
                else if (posInt >= targetGrainSize - fadeLen)
                {
                    const float t = (float) (targetGrainSize - posInt) / (float) fadeLen;
                    winGain = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::pi * t));
                }

                outL[i] = sL * winGain;
                outR[i] = sR * winGain;
                outputsThisGrain++;
            }
        }

    private:
        juce::HeapBlock<float> historyL, historyR;
        int historyMask       = 0;
        int historyWriteIdx   = 0;
        int grainAnchor       = 0;
        int outputsThisGrain  = 0;
        int targetGrainSize   = 1440;
        int fadeLen           = 90;
        bool firstBlockPending = true;

        double sampleRate     = 48000.0;
        int    channels       = 2;
        float  stretchRatio   = 1.0f;
        float  pitchSemitones = 0.0f;
        bool   ready          = false;
    };
}
