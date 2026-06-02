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

        // ── Analog category: 6 iconic tables (Phase 2A) ──────────────────────
        // All use 16 frames so user can scan/morph via the FRAME knob.
        // Generated additively from sine harmonics — clean-room implementations,
        // no sampled or copyrighted content. Character is in the harmonic
        // distribution + frame-to-frame morph curve, not in any specific sample.

        /** Prophet 5-style saw: frame 0 = full bright saw, frame 15 = warm
         *  (higher harmonics rolled off, evoking the SSM 2044 filter). */
        static Wavetable makeProphetSaw()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                // Frame-dependent harmonic rolloff. Frame 0 = ~100 harmonics
                // (bright); frame 15 = ~12 harmonics (warm).
                const int maxHarm = 100 - (int)((100.0 - 12.0) * (double) f / 15.0);
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    for (int h = 1; h <= maxHarm; ++h)
                        s += std::sin (phase * (double) h) / (double) h;
                    wt.sampleRef (f, i) = (float)(s * (2.0 / 3.14159265358979323846));
                }
            }
            return wt;
        }

        /** Jupiter-8 PWM: pulse wave morphing from 50% (square) at frame 0
         *  to ~5% (narrow pulse, hollow) at frame 15. Bandlimited via additive. */
        static Wavetable makeJupiterPWM()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                // Pulse width: 0.5 → 0.05 across frames.
                const double pw = 0.5 - 0.45 * ((double) f / 15.0);
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    // Pulse Fourier series.
                    for (int k = 1; k <= 64; ++k)
                        s += std::sin (3.14159265358979323846 * (double) k * pw) / (double) k
                             * std::cos ((double) k * phase);
                    wt.sampleRef (f, i) = (float)(s * (2.0 / 3.14159265358979323846));
                }
            }
            return wt;
        }

        /** Moog-style square with light odd-harmonic emphasis tracking across
         *  frames (suggests the ladder filter's resonance coloration). */
        static Wavetable makeMoogSqr()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                const double emphasis = 1.0 + 0.4 * ((double) f / 15.0);  // 1.0 → 1.4
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    // Odd harmonics only for square; emphasize 3rd as frame increases.
                    for (int k = 1; k <= 64; k += 2)
                    {
                        const double weight = (k == 3) ? emphasis : 1.0;
                        s += weight * std::sin ((double) k * phase) / (double) k;
                    }
                    wt.sampleRef (f, i) = (float)(s * (4.0 / 3.14159265358979323846));
                }
            }
            return wt;
        }

        /** OB-X dual-saw: one saw + a detuned saw offset by frame-dependent
         *  cents (0 → 12 cents across frames). Classic OB-X chorusing. */
        static Wavetable makeOBXSaw()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                // Detune: 0 → 12 cents. Equivalent frequency multiplier 2^(cents/1200).
                const double cents  = 12.0 * ((double) f / 15.0);
                const double ratio  = std::pow (2.0, cents / 1200.0);
                const int   maxH    = 80;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    for (int h = 1; h <= maxH; ++h)
                    {
                        s += std::sin (phase * (double) h) / (double) h;
                        s += std::sin (phase * (double) h * ratio) / (double) h;
                    }
                    wt.sampleRef (f, i) = (float)(s * (1.0 / 3.14159265358979323846));
                }
            }
            return wt;
        }

        /** CS-80 brass: saw blended with high-passed reproducible noise.
         *  Frame 0 = pure saw, frame 15 = saw + airy noise breath. */
        static Wavetable makeCS80Brass()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            // Reproducible pseudo-noise seeded once (xorshift32).
            unsigned int rng = 0xCAFEBABEu;
            auto next = [&]() {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                return ((float) rng / (float) 0xFFFFFFFFu) * 2.0f - 1.0f;
            };
            // Generate one shared high-passed noise frame: white noise minus its
            // running average (1st-order HPF approximation).
            std::vector<float> noise ((size_t) N, 0.0f);
            float acc = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                const float w = next();
                acc = acc * 0.95f + w * 0.05f;
                noise[(size_t) i] = w - acc;
            }
            for (int f = 0; f < 16; ++f)
            {
                const double noiseAmp = 0.3 * ((double) f / 15.0);
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double saw = 0.0;
                    for (int h = 1; h <= 60; ++h)
                        saw += std::sin (phase * (double) h) / (double) h;
                    saw *= (2.0 / 3.14159265358979323846);
                    wt.sampleRef (f, i) = (float) saw + (float) noiseAmp * noise[(size_t) i];
                }
            }
            return wt;
        }

        /** Juno-style 3-saw ensemble: 3 detuned saws, spread 0 → 8 cents
         *  across frames. Wide unison character in a single waveform. */
        static Wavetable makeJunoStr()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                const double cents  = 8.0 * ((double) f / 15.0);
                const double rUp    = std::pow (2.0,  cents / 1200.0);
                const double rDn    = std::pow (2.0, -cents / 1200.0);
                const int    maxH   = 60;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    for (int h = 1; h <= maxH; ++h)
                    {
                        s += std::sin (phase * (double) h) / (double) h;
                        s += std::sin (phase * (double) h * rUp) / (double) h;
                        s += std::sin (phase * (double) h * rDn) / (double) h;
                    }
                    wt.sampleRef (f, i) = (float)(s * (2.0 / (3.0 * 3.14159265358979323846)));
                }
            }
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
