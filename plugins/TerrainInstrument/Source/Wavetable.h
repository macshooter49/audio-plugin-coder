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

        // ── Basic category (Phase 9 polish) — 4 fundamental waveforms ───────
        // Constant across all frames (no morph — these are "basic" by design).
        // Bandlimited additive synthesis (32 harmonics) for Triangle/Square/Pulse.

        /** Bandlimited triangle: odd harmonics, 1/n² decay, alternating sign. */
        static Wavetable makeTriangle()
        {
            Wavetable wt (1);
            constexpr int numHarmonics = 32;
            constexpr double pi  = 3.14159265358979323846;
            constexpr double pi2 = 2.0 * pi;
            const int N = wt.frameSize_;
            for (int i = 0; i < N; ++i)
            {
                const double phase = (double) i / (double) N;
                double v = 0.0;
                for (int n = 1; n <= numHarmonics; n += 2)
                {
                    const double sign = ((n - 1) / 2) % 2 == 0 ? 1.0 : -1.0;
                    v += sign * std::sin (pi2 * (double) n * phase) / (double) (n * n);
                }
                v *= 8.0 / (pi * pi);
                wt.sampleRef (0, i) = (float) v;
            }
            return wt;
        }

        /** Bandlimited square: odd harmonics, 1/n decay. */
        static Wavetable makeSquare()
        {
            Wavetable wt (1);
            constexpr int numHarmonics = 32;
            constexpr double pi  = 3.14159265358979323846;
            constexpr double pi2 = 2.0 * pi;
            const int N = wt.frameSize_;
            for (int i = 0; i < N; ++i)
            {
                const double phase = (double) i / (double) N;
                double v = 0.0;
                for (int n = 1; n <= numHarmonics; n += 2)
                    v += std::sin (pi2 * (double) n * phase) / (double) n;
                v *= 4.0 / pi;
                wt.sampleRef (0, i) = (float) v;
            }
            return wt;
        }

        /** Bandlimited 25% duty pulse: all harmonics, amplitude = (2/πn)sin(πnd). */
        static Wavetable makePulse()
        {
            Wavetable wt (1);
            constexpr int numHarmonics = 32;
            constexpr double pi   = 3.14159265358979323846;
            constexpr double pi2  = 2.0 * pi;
            constexpr double duty = 0.25;
            const int N = wt.frameSize_;
            for (int i = 0; i < N; ++i)
            {
                const double phase = (double) i / (double) N;
                double v = 2.0 * duty - 1.0;  // DC offset for asymmetric pulse
                for (int n = 1; n <= numHarmonics; ++n)
                {
                    const double amp = (2.0 / (pi * (double) n)) * std::sin (pi * (double) n * duty);
                    v += amp * std::cos (pi2 * (double) n * phase);
                }
                wt.sampleRef (0, i) = (float) v;
            }
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

        // ── Digital category (Phase 2B) ──────────────────────────────────────
        // 4 iconic digital wavetable characters — additive bandlimited recipes
        // approximating Wave/FM/Sample-based classics. No copyrighted samples.

        /** PPG Wave: 8-bit-flavor wavetable. Mostly-saw harmonics + frame-
         *  dependent harmonic peak shift (low/warm → high/icy) + sample-level
         *  quantization to nearest 1/16 to suggest 8-bit DAC character. */
        static Wavetable makePPGWave()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                // Frame-dependent harmonic emphasis: lowest harmonic emphasis 1.0
                // → 5.0 across frames (icy = upper harmonics get amplified).
                const double peakHarm = 1.0 + 5.0 * ((double) f / 15.0);
                const double sigma = 4.0;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    for (int h = 1; h <= 64; ++h)
                    {
                        const double w = std::exp (-((double) h - peakHarm) * ((double) h - peakHarm)
                                                    / (2.0 * sigma * sigma));
                        s += w * std::sin (phase * (double) h) / (double) h;
                    }
                    // Bit-quantize to 16 steps (signed) for PPG character.
                    const double q = std::round (s * 8.0) / 8.0;
                    wt.sampleRef (f, i) = (float) q;
                }
            }
            return wt;
        }

        /** DX7 EP: electric-piano bell tone. Fundamental + 3.5x inharmonic
         *  partial + 7.0x partial. Bell-partial amplitude ramps 0 → 1
         *  across frames (frame 0 = pure tone, frame 15 = bell ring). */
        static Wavetable makeDX7EP()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                const double bellAmp = (double) f / 15.0;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = std::sin (phase)
                             + 0.5 * std::sin (phase * 2.0)
                             + bellAmp * 0.6 * std::sin (phase * 3.5)
                             + bellAmp * 0.3 * std::sin (phase * 7.0);
                    wt.sampleRef (f, i) = (float)(s * 0.5);
                }
            }
            return wt;
        }

        /** D-50 Bell: cluster of inharmonic partials at bell-mode ratios
         *  (1.0, 2.756, 5.404, 8.93). Frames morph cluster amplitudes. */
        static Wavetable makeD50Bell()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            const double partials[] = { 1.0, 2.756, 5.404, 8.93 };
            for (int f = 0; f < 16; ++f)
            {
                const double clusterAmp = 0.3 + 0.7 * ((double) f / 15.0);
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = std::sin (phase);
                    for (int p = 1; p < 4; ++p)
                        s += clusterAmp * (0.6 / (double) p) * std::sin (phase * partials[p]);
                    wt.sampleRef (f, i) = (float)(s * 0.4);
                }
            }
            return wt;
        }

        /** M1 Piano: percussive piano harmonics. Fundamental + 2nd + 3rd
         *  (saw-like) + slight 4.04x detune for body inharmonicity. */
        static Wavetable makeM1Piano()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                const double detuneAmt = (double) f / 15.0;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = std::sin (phase)
                             + 0.5 * std::sin (phase * 2.0)
                             + 0.33 * std::sin (phase * 3.0)
                             + 0.2 * std::sin (phase * (4.0 + 0.04 * detuneAmt))
                             + 0.15 * std::sin (phase * 5.0)
                             + 0.1 * std::sin (phase * 6.0);
                    wt.sampleRef (f, i) = (float)(s * 0.4);
                }
            }
            return wt;
        }

        // ── Vocal category (Phase 2B) ────────────────────────────────────────
        // Formant-based additive synthesis. Each harmonic's amplitude is
        // weighted by a sum of Gaussian peaks at the formant frequencies,
        // simulating the resonant cavities of the human vocal tract.

        /** Helper: formant-weighted amplitude for a given harmonic index given
         *  fundamental frequency assumption of 220 Hz (A3 — neutral male voice).
         *  Three Gaussian peaks at f1/f2/f3, each with sigma ~80 Hz. */
        static double formantAmp (double harmonicHz, double f1, double f2, double f3)
        {
            const double sigma = 80.0;
            const double w1 = std::exp (-(harmonicHz - f1) * (harmonicHz - f1) / (2.0 * sigma * sigma));
            const double w2 = std::exp (-(harmonicHz - f2) * (harmonicHz - f2) / (2.0 * sigma * sigma));
            const double w3 = std::exp (-(harmonicHz - f3) * (harmonicHz - f3) / (2.0 * sigma * sigma));
            return w1 + 0.7 * w2 + 0.5 * w3;
        }

        /** Choir A→O: vowel A (730/1090/2440) at frame 0 morphs to
         *  vowel O (360/750/2400) at frame 15. Fundamental assumed 220 Hz. */
        static Wavetable makeChoirAtoO()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            const double fund = 220.0;
            for (int f = 0; f < 16; ++f)
            {
                const double t = (double) f / 15.0;
                // Linear morph A → O.
                const double F1 = 730.0  + (360.0  - 730.0)  * t;
                const double F2 = 1090.0 + (750.0  - 1090.0) * t;
                const double F3 = 2440.0 + (2400.0 - 2440.0) * t;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    for (int h = 1; h <= 40; ++h)
                    {
                        const double w = formantAmp ((double) h * fund, F1, F2, F3);
                        s += w * std::sin (phase * (double) h) / (double) h;
                    }
                    wt.sampleRef (f, i) = (float)(s * 0.6);
                }
            }
            return wt;
        }

        /** Whisper: shaped noise instead of harmonics. Mid-band emphasis
         *  shifts from low (300-1500 Hz) to high (1500-6000 Hz) across frames. */
        static Wavetable makeWhisper()
        {
            Wavetable wt (16);
            const int N = wt.frameSize_;
            // Reproducible noise — different seed per frame for variety.
            for (int f = 0; f < 16; ++f)
            {
                unsigned int rng = 0xDEADBEEFu + (unsigned) f;
                auto next = [&]() {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return ((float) rng / (float) 0xFFFFFFFFu) * 2.0f - 1.0f;
                };
                // 1st-order bandpass approximation: HP cutoff rises, LP fixed.
                const float hpCoef = 0.7f + 0.25f * ((float) f / 15.0f);  // 0.7 → 0.95
                const float lpCoef = 0.3f;
                float hpState = 0.0f, lpState = 0.0f;
                for (int i = 0; i < N; ++i)
                {
                    const float w = next();
                    // HP via subtracting running average.
                    hpState = hpState * hpCoef + w * (1.0f - hpCoef);
                    const float hp = w - hpState;
                    // LP smoothing for body.
                    lpState = lpState * lpCoef + hp * (1.0f - lpCoef);
                    wt.sampleRef (f, i) = lpState * 1.5f;
                }
            }
            return wt;
        }

        /** Vowel morph: cycles A→E→I→O→U across the 16 frames.
         *  Same formant additive as Choir. */
        static Wavetable makeVowelMorph()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            const double fund = 220.0;
            // 5 vowels distributed across 16 frames: idx 0, 4, 8, 12, 15.
            const double vowels[][3] = {
                { 730.0, 1090.0, 2440.0 },  // A
                { 530.0, 1840.0, 2480.0 },  // E
                { 390.0, 1990.0, 2550.0 },  // I
                { 360.0,  750.0, 2400.0 },  // O
                { 300.0,  870.0, 2240.0 },  // U
            };
            for (int f = 0; f < 16; ++f)
            {
                // Map frame 0..15 to vowel position 0..4 (continuous).
                const double vp = ((double) f / 15.0) * 4.0;
                const int v0 = (int) vp;
                const int v1 = v0 < 4 ? v0 + 1 : v0;
                const double vt = vp - (double) v0;
                const double F1 = vowels[v0][0] + (vowels[v1][0] - vowels[v0][0]) * vt;
                const double F2 = vowels[v0][1] + (vowels[v1][1] - vowels[v0][1]) * vt;
                const double F3 = vowels[v0][2] + (vowels[v1][2] - vowels[v0][2]) * vt;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    for (int h = 1; h <= 40; ++h)
                    {
                        const double w = formantAmp ((double) h * fund, F1, F2, F3);
                        s += w * std::sin (phase * (double) h) / (double) h;
                    }
                    wt.sampleRef (f, i) = (float)(s * 0.6);
                }
            }
            return wt;
        }

        // ── Metallic category (Phase 2B) ─────────────────────────────────────

        /** Bowed metal / Tibetan bowl. Inharmonic partials at bowl-mode
         *  ratios. Frame morph adds subtle irregularity. */
        static Wavetable makeBowedMetal()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            const double partials[] = { 1.0, 2.71, 5.07, 8.93, 13.40 };
            for (int f = 0; f < 16; ++f)
            {
                const double irregularity = 0.01 * (double) f;  // up to 0.15 cents drift per frame
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    for (int p = 0; p < 5; ++p)
                    {
                        const double ratio = partials[p] * (1.0 + irregularity * ((double)(p % 2) - 0.5));
                        s += (0.8 / (double)(p + 1)) * std::sin (phase * ratio);
                    }
                    wt.sampleRef (f, i) = (float)(s * 0.5);
                }
            }
            return wt;
        }

        /** Glass harmonics: high inharmonic partials clustered around 4-12x. */
        static Wavetable makeGlassHarmonics()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                const double clusterCenter = 6.0 + 4.0 * ((double) f / 15.0);  // 6 → 10
                const double sigma = 2.0;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.3 * std::sin (phase);  // fundamental for body
                    for (int h = 3; h <= 20; ++h)
                    {
                        const double hd = (double) h + 0.05 * (double)(h % 3);  // slight inharmonicity
                        const double w = std::exp (-(hd - clusterCenter) * (hd - clusterCenter)
                                                    / (2.0 * sigma * sigma));
                        s += w * 0.8 * std::sin (phase * hd) / hd;
                    }
                    wt.sampleRef (f, i) = (float)(s * 0.5);
                }
            }
            return wt;
        }

        /** Railroad: low metallic clang. Detuned subharmonics + 2/3/5
         *  partials. Frame morph shifts toward more upper clang. */
        static Wavetable makeRailroad()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                const double clangAmt = (double) f / 15.0;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = std::sin (phase * 0.5)  // subharmonic for weight
                             + 0.8 * std::sin (phase)
                             + 0.6 * std::sin (phase * 2.0)
                             + (0.3 + 0.7 * clangAmt) * std::sin (phase * 3.0)
                             + (0.2 + 0.7 * clangAmt) * std::sin (phase * 5.13)
                             + clangAmt * 0.3 * std::sin (phase * 7.8);
                    wt.sampleRef (f, i) = (float)(s * 0.3);
                }
            }
            return wt;
        }

        // ── Experimental category (Phase 2B) ─────────────────────────────────

        /** Dustbowl: bandlimited saw + high-passed reproducible dust noise.
         *  Frames 0→15 increase noise blend (0 → 40%). */
        static Wavetable makeDustbowl()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            unsigned int rng = 0xFEEDFACEu;
            auto next = [&]() {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                return ((float) rng / (float) 0xFFFFFFFFu) * 2.0f - 1.0f;
            };
            std::vector<float> dust ((size_t) N, 0.0f);
            float acc = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                const float w = next();
                acc = acc * 0.85f + w * 0.15f;
                dust[(size_t) i] = w - acc;  // high-passed dust
            }
            for (int f = 0; f < 16; ++f)
            {
                const double noiseAmp = 0.4 * ((double) f / 15.0);
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double saw = 0.0;
                    for (int h = 1; h <= 50; ++h)
                        saw += std::sin (phase * (double) h) / (double) h;
                    saw *= (2.0 / 3.14159265358979323846);
                    wt.sampleRef (f, i) = (float) saw + (float) noiseAmp * dust[(size_t) i];
                }
            }
            return wt;
        }

        /** Static Evolve: pure static (frame 0) → clean tone with static
         *  texture (frame 15). Same RNG seed for reproducibility per frame. */
        static Wavetable makeStaticEvolve()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                const double cleanAmt = (double) f / 15.0;
                unsigned int rng = 0x12345678u + (unsigned) f;
                auto next = [&]() {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return ((float) rng / (float) 0xFFFFFFFFu) * 2.0f - 1.0f;
                };
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double tone = 0.0;
                    for (int h = 1; h <= 30; ++h)
                        tone += std::sin (phase * (double) h) / (double) h;
                    tone *= (2.0 / 3.14159265358979323846);
                    const float noise = next();
                    wt.sampleRef (f, i) = (float)(cleanAmt * tone + (1.0 - cleanAmt) * noise * 0.8);
                }
            }
            return wt;
        }

        /** Spectral Drift: same harmonic spectrum across all frames, but
         *  each harmonic's phase offset is randomized per frame (fixed seed).
         *  Spectrum stays identical; waveform shape evolves wildly. */
        static Wavetable makeSpectralDrift()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                // Phase offsets per harmonic — deterministic per frame.
                unsigned int rng = 0xAABBCCDDu + (unsigned) f * 0x9E3779B9u;
                auto nextPhase = [&]() {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return ((double) rng / (double) 0xFFFFFFFFu) * twoPi;
                };
                std::vector<double> phaseOffsets ((size_t) 32);
                for (size_t k = 0; k < 32; ++k) phaseOffsets[k] = nextPhase();
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    for (int h = 1; h <= 32; ++h)
                        s += std::sin (phase * (double) h + phaseOffsets[(size_t)(h - 1)]) / (double) h;
                    wt.sampleRef (f, i) = (float)(s * (1.5 / 3.14159265358979323846));
                }
            }
            return wt;
        }

        /** Serum HD: bright modern wavetable. Balanced harmonic ramp with
         *  slight even-harmonic emphasis. Frame center 0→1 shifts low → high. */
        static Wavetable makeSerumHD()
        {
            Wavetable wt (16);
            const double twoPi = 2.0 * 3.14159265358979323846;
            const int N = wt.frameSize_;
            for (int f = 0; f < 16; ++f)
            {
                const double center = 2.0 + 8.0 * ((double) f / 15.0);  // 2 → 10
                const double sigma = 4.0;
                for (int i = 0; i < N; ++i)
                {
                    const double phase = twoPi * (double) i / (double) N;
                    double s = 0.0;
                    for (int h = 1; h <= 50; ++h)
                    {
                        const double w = std::exp (-((double) h - center) * ((double) h - center)
                                                    / (2.0 * sigma * sigma));
                        const double evenBoost = (h % 2 == 0) ? 1.15 : 1.0;
                        s += w * evenBoost * std::sin (phase * (double) h) / (double) h;
                    }
                    wt.sampleRef (f, i) = (float)(s * 1.5);
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
