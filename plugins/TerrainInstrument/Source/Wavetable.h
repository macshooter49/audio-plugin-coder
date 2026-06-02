// Wavetable.h — Terrain Instrument synth section, Phase 2A (foundation)
// Frame-based wavetable storage + bilinear lookup.
//
// Layout: `frames_` is a flat std::vector of size numFrames * frameSize, indexed
// as [frame * frameSize + sampleIndex]. lookup(framePos, phase) bilinearly
// interpolates across BOTH frame index (smooth morph between adjacent frames)
// AND phase index (smooth lookup within a single frame).
//
// All factory makers are static. The bank constructs all 6 tables at startup;
// individual SynthVoices hold a pointer to whichever table is selected.
#pragma once

#include <vector>
#include <cmath>
#include <cstddef>

namespace tw
{
    class Wavetable
    {
    public:
        static constexpr int kFrameSize = 2048;  // power of 2 → cheap modulo via mask

        Wavetable() = default;

        Wavetable (int numFrames, int frameSize = kFrameSize)
            : numFrames_ (numFrames > 0 ? numFrames : 1),
              frameSize_ (frameSize > 0 ? frameSize : kFrameSize),
              frames_ ((size_t)(numFrames_ * frameSize_), 0.0f)
        {}

        int getNumFrames() const noexcept { return numFrames_; }
        int getFrameSize() const noexcept { return frameSize_; }

        /** Bilinear interpolation lookup.
         *  framePos: 0..1 across the frame stack (0 = first, 1 = last)
         *  phase:    0..1 within a single frame (0 = start of cycle) */
        float lookup (float framePos, float phase) const noexcept
        {
            // Frame index (continuous) + integer/fractional split.
            const float fIdx  = framePos * (float)(numFrames_ - 1);
            const int   f0    = (int) fIdx;
            const int   f1    = f0 < numFrames_ - 1 ? f0 + 1 : f0;
            const float fFrac = fIdx - (float) f0;

            // Phase index (continuous) + integer/fractional split. Wrap to [0,1).
            const float p     = phase - std::floor (phase);
            const float pIdx  = p * (float) frameSize_;
            const int   p0    = (int) pIdx;
            const int   p1    = (p0 + 1) % frameSize_;
            const float pFrac = pIdx - (float) p0;

            const float a = sample (f0, p0);
            const float b = sample (f0, p1);
            const float c = sample (f1, p0);
            const float d = sample (f1, p1);

            // Bilinear: lerp on phase first, then on frame.
            const float fr0 = a + (b - a) * pFrac;
            const float fr1 = c + (d - c) * pFrac;
            return fr0 + (fr1 - fr0) * fFrac;
        }

        /** Direct mutable access — used by factory methods only. */
        float& sampleRef (int frame, int idx) noexcept
        {
            return frames_[(size_t)(frame * frameSize_ + idx)];
        }

        // ── Factory methods ─────────────────────────────────────────────────
        /** Plain sine (one frame). Reference for tests and the "boring baseline." */
        static Wavetable makeSine()
        {
            Wavetable wt (1);
            const double twoPi = 2.0 * 3.14159265358979323846;
            for (int i = 0; i < wt.frameSize_; ++i)
                wt.sampleRef (0, i) = (float) std::sin (twoPi * (double) i / (double) wt.frameSize_);
            return wt;
        }

    private:
        float sample (int frame, int idx) const noexcept
        {
            return frames_[(size_t)(frame * frameSize_ + idx)];
        }

        int                  numFrames_ = 1;
        int                  frameSize_ = kFrameSize;
        std::vector<float>   frames_;
    };
}
