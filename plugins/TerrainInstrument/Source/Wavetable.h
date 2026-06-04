// Wavetable.h — Terrain Instrument synth section, Phase 2A (foundation)
// Frame-based wavetable storage + bilinear lookup.
//
// Layout (Phase 10a+): `mipData_` is a flat std::vector indexed as
// [mipLevel * numFrames_ * frameSize_ + frame * frameSize_ + sampleIndex].
// numMipLevels_ == 1 for legacy-constructed (factory) tables; == 8 for
// spec-built tables. lookup(framePos, phase) bilinearly interpolates across
// BOTH frame index (smooth morph between adjacent frames) AND phase index
// (smooth lookup within a single frame).
//
// All factory makers are static. The bank constructs all tables at startup;
// individual SynthVoices hold a pointer to whichever table is selected.
#pragma once

#include <vector>
#include <cmath>
#include <cstddef>
#include <array>
#include <juce_core/juce_core.h>

namespace tw
{
    /** Frequency-domain spec for a single wavetable frame.
     *  amplitudes[h-1] is the gain of the h-th harmonic (1-indexed mathematically,
     *  0-indexed in the array). phases[h-1] is its phase offset in radians.
     *  numHarmonics is the highest harmonic INDEX with potentially non-zero amp
     *  (i.e., buildFromSpec iterates h=1..numHarmonics inclusive). Skipped
     *  harmonics within that range must have amplitudes[h-1] == 0 — the
     *  reconstruction loop skips them via an amp==0 fast path. */
    struct FrameSpec
    {
        static constexpr int kMaxHarmonics = 256;
        std::array<float, kMaxHarmonics> amplitudes {};  // value-initialized to 0
        std::array<float, kMaxHarmonics> phases     {};  // value-initialized to 0
        int numHarmonics = 0;
    };

    /** A full wavetable spec: 16 frames of frequency-domain harmonic content.
     *  This is the source-of-truth representation; Wavetable::buildFromSpec()
     *  reconstructs 8 time-domain mip levels from it at construction. */
    struct WavetableSpec
    {
        static constexpr int kNumFrames = 16;
        std::array<FrameSpec, kNumFrames> frames {};
    };

    class Wavetable
    {
    public:
        static constexpr int kFrameSize    = 2048;  // power of 2 → cheap modulo via mask
        static constexpr int kNumMipLevels = 8;     // 256/128/64/32/16/8/4/2 harmonics

        // Maximum-harmonic count per mip level. Index 0 = full bandwidth.
        static constexpr std::array<int, kNumMipLevels> kMipMaxHarmonics
            { 256, 128, 64, 32, 16, 8, 4, 2 };

        Wavetable() = default;

        Wavetable (int numFrames, int frameSize = kFrameSize)
            : numFrames_     (numFrames > 0 ? numFrames : 1),
              frameSize_    (frameSize > 0 ? frameSize : kFrameSize),
              numMipLevels_ (1),
              mipData_      ((size_t) (1 * numFrames_ * frameSize_), 0.0f)
        {}

        int getNumFrames()    const noexcept { return numFrames_; }
        int getFrameSize()    const noexcept { return frameSize_; }
        int getNumMipLevels() const noexcept { return numMipLevels_; }

        /** Reconstruct 8 bandlimited mip levels from a frequency-domain spec.
         *  Resets internal storage to 16 frames × kFrameSize samples × 8 levels.
         *  Pure additive synthesis (one sin() per harmonic per sample) — no FFT
         *  in 10a. ~17M sin() calls per wavetable; budget ~150-200ms each.
         *  Normalizes each mip level so peak == 1.0. */
        void buildFromSpec (const WavetableSpec& spec)
        {
            numFrames_     = WavetableSpec::kNumFrames;
            frameSize_     = kFrameSize;
            numMipLevels_  = kNumMipLevels;
            mipData_.assign ((size_t) (numMipLevels_ * numFrames_ * frameSize_), 0.0f);

            constexpr double pi2 = 2.0 * 3.14159265358979323846;

            for (int level = 0; level < numMipLevels_; ++level)
            {
                const int hMax = kMipMaxHarmonics[(size_t) level];
                for (int frame = 0; frame < numFrames_; ++frame)
                {
                    const FrameSpec& fs = spec.frames[(size_t) frame];
                    const int hCount = std::min (hMax, fs.numHarmonics);
                    for (int sample = 0; sample < frameSize_; ++sample)
                    {
                        const double normPhase = (double) sample / (double) frameSize_;
                        double v = 0.0;
                        for (int h = 1; h <= hCount; ++h)
                        {
                            const double amp = (double) fs.amplitudes[(size_t) (h - 1)];
                            if (amp == 0.0) continue;
                            const double ph  = (double) fs.phases[(size_t) (h - 1)];
                            v += amp * std::sin (pi2 * (double) h * normPhase + ph);
                        }
                        mipData_[(size_t) ((level * numFrames_ + frame) * frameSize_ + sample)] = (float) v;
                    }
                }
            }

            normalizeMipLevels();
        }

        /** Canonical render-path lookup. mipLevel is clamped to
         *  [0, numMipLevels_-1] (so legacy single-tier tables, numMipLevels_=1,
         *  silently ignore the mipLevel argument). */
        float lookup (int mipLevel, float framePos, float phase) const noexcept
        {
            const int lvl = juce::jlimit (0, numMipLevels_ - 1, mipLevel);

            const float fIdx  = framePos * (float) (numFrames_ - 1);
            const int   f0    = (int) fIdx;
            const int   f1    = f0 < numFrames_ - 1 ? f0 + 1 : f0;
            const float fFrac = fIdx - (float) f0;

            const float p     = phase - std::floor (phase);
            const float pIdx  = p * (float) frameSize_;
            const int   p0    = (int) pIdx;
            const int   p1    = (p0 + 1) % frameSize_;
            const float pFrac = pIdx - (float) p0;

            const float a = sample (lvl, f0, p0);
            const float b = sample (lvl, f0, p1);
            const float c = sample (lvl, f1, p0);
            const float d = sample (lvl, f1, p1);

            const float fr0 = a + (b - a) * pFrac;
            const float fr1 = c + (d - c) * pFrac;
            return fr0 + (fr1 - fr0) * fFrac;
        }

        /** Backwards-compat alias used by legacy callers. Always reads mip 0. */
        float lookup (float framePos, float phase) const noexcept
        {
            return lookup (0, framePos, phase);
        }

        /** Direct mutable access — used by legacy factory methods only.
         *  Writes into mip slot 0 (the only slot legacy tables have). */
        float& sampleRef (int frame, int idx) noexcept
        {
            return mipData_[(size_t) ((0 * numFrames_ + frame) * frameSize_ + idx)];
        }

        /** Pick the mip level that bandlimits this phase increment. phaseInc =
         *  freq / sampleRate (cycles per sample, normalized). At phaseInc, the
         *  N-th harmonic sits at N*phaseInc; we need N*phaseInc < 0.5 (Nyquist).
         *  Picks the smallest level (most harmonics) whose harmonic count fits. */
        static int mipLevelForPhaseIncrement (double phaseInc) noexcept
        {
            const double safeInc = juce::jmax (phaseInc, 1.0e-9);
            const double maxSafeHarmonics = 0.5 / safeInc;
            for (int lvl = 0; lvl < kNumMipLevels; ++lvl)
                if ((double) kMipMaxHarmonics[(size_t) lvl] <= maxSafeHarmonics)
                    return lvl;
            return kNumMipLevels - 1;
        }

        /** Convenience: pick mip level by MIDI note + sample rate. Computes a
         *  reference phase increment at that note (assumes A4=440, 12-TET). */
        static int mipLevelForMidiNote (int midiNote, double sampleRate) noexcept
        {
            const double hz       = 440.0 * std::pow (2.0, (double) (midiNote - 69) / 12.0);
            const double phaseInc = hz / juce::jmax (sampleRate, 1.0);
            return mipLevelForPhaseIncrement (phaseInc);
        }

        // ── Factory methods ─────────────────────────────────────────────────

        // ── Basic category (Phase 10a — frequency-domain spec format) ────────
        // 4 fundamental waveforms, all 16 frames identical (no morph).
        // 256 harmonics; the mip system bandlimits per-pitch.

        // ── Phase 11k — universal frame amplifier ─────────────────────────────
        // Apply progressive per-frame timbral variation to make WT POS dramatic
        // on ANY legacy (mip-0-only) wavetable. Frame 0 stays untouched (preserves
        // the wavetable's vanilla character); frames 1-15 receive progressively
        // stronger spectral + harmonic modification.
        //
        // Mode determines the character of the variation:
        //   Warmth     — boost lows + gentle soft saturation (analogue warmth)
        //   Brightness — boost highs + slight saturation (air/brilliance)
        //   Drive      — aggressive soft clipping, adds harmonics
        //   Spectrum   — spectral tilt (cut lows, boost highs) + saturation
        enum class AmplifyMode { Warmth, Brightness, Drive, Spectrum };

        /** Apply progressive per-frame timbral variation in-place.
         *  Operates on mip level 0 only (legacy wavetables have numMipLevels_==1).
         *  Frame 0 is left untouched; frames 1-15 are transformed with strength ∝ t².
         *  Re-normalises peak to 1.0 across ALL frames after processing. */
        void amplifyFramesInPlace (AmplifyMode mode) noexcept
        {
            const int N = frameSize_;
            for (int f = 1; f < numFrames_; ++f)  // skip frame 0 (untouched)
            {
                const float t        = (float) f / (float) (numFrames_ - 1);
                const float strength = t * t;  // quadratic ramp — gentle low, dramatic high

                // 1-pole LP for band-split — cutoff ~1/10 of frame period
                const float alpha = 0.10f;
                float lpZ = 0.0f;

                for (int s = 0; s < N; ++s)
                {
                    float& sample = mipData_[(size_t) ((0 * numFrames_ + f) * frameSize_ + s)];

                    // Band-split via 1-pole LP
                    lpZ += alpha * (sample - lpZ);
                    const float lows  = lpZ;
                    const float highs = sample - lpZ;

                    switch (mode)
                    {
                        case AmplifyMode::Warmth:
                        {
                            // Boost lows, gentle saturation
                            const float lowGain = 1.0f + strength * 2.5f;
                            const float drive   = 1.0f + strength * 2.0f;
                            sample = std::tanh ((lows * lowGain + highs) * drive)
                                   / std::tanh (drive);
                            break;
                        }
                        case AmplifyMode::Brightness:
                        {
                            // Boost highs, slight saturation
                            const float highGain = 1.0f + strength * 3.0f;
                            const float drive    = 1.0f + strength * 1.5f;
                            sample = std::tanh ((lows + highs * highGain) * drive)
                                   / std::tanh (drive);
                            break;
                        }
                        case AmplifyMode::Drive:
                        {
                            // Aggressive soft clipping, harmonic generation
                            const float drive = 1.0f + strength * 6.0f;
                            sample = std::tanh (sample * drive) / std::tanh (drive);
                            break;
                        }
                        case AmplifyMode::Spectrum:
                        {
                            // Spectral tilt + light saturation
                            const float lowCut  = 1.0f - strength * 0.4f;
                            const float highBst = 1.0f + strength * 2.0f;
                            const float drive   = 1.0f + strength * 1.5f;
                            sample = std::tanh ((lows * lowCut + highs * highBst) * drive)
                                   / std::tanh (drive);
                            break;
                        }
                    }
                }
            }
            // Re-normalize peak to 1.0 across all frames in mip 0
            float peak = 0.0f;
            const int total = numFrames_ * frameSize_;
            for (int s = 0; s < total; ++s)
                peak = std::max (peak, std::abs (mipData_[(size_t) s]));
            if (peak > 0.0f)
            {
                const float inv = 1.0f / peak;
                for (int s = 0; s < total; ++s)
                    mipData_[(size_t) s] *= inv;
            }
        }

        /** Pure sine at frame 0; by frame 15 has subtle warm odd harmonics (saturated
         *  sine character). Preserves pure sine identity throughout — timbral
         *  evolution is gentle, not harsh. */
        static WavetableSpec makeSineSpec()
        {
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                // Frame 0: pure sine. Later frames add gentle 1/h² odd harmonics
                // so it evolves toward a soft, warm tone — still sine-like but richer.
                fs.amplitudes[0] = 1.0f;
                fs.phases[0]     = 0.0f;
                if (t < 0.01f)
                {
                    fs.numHarmonics = 1;
                }
                else
                {
                    // Grow harmonics quadratically: frame 1→1, frame 7→5, frame 15→16
                    const int numH = juce::jlimit (1, 16,
                                                    1 + (int) std::round (15.0f * t * t));
                    fs.numHarmonics = numH;
                    for (int h = 3; h <= numH; h += 2)
                    {
                        // Odd harmonics with 1/h² decay × t² so they stay inaudible
                        // at early frames and bloom softly at later ones.
                        fs.amplitudes[(size_t)(h - 1)] = t * t * 0.35f / (float)(h * h);
                        fs.phases[(size_t)(h - 1)]     = 0.0f;
                    }
                }
            }
            return spec;
        }

        /** Triangle at frame 0; by frame 15 the 1/h² harmonic rolloff flattens
         *  toward 1/h (triangle → almost-square). Odd-only throughout. */
        static WavetableSpec makeTriangleSpec()
        {
            WavetableSpec spec;
            constexpr double pi = 3.14159265358979323846;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                int populated = 0;
                // decayPow: 2.0 (triangle/1/h²) → 1.0 (square/1/h) over the sweep
                const float decayPow = 2.0f - t * 1.0f;
                for (int n = 1; n <= FrameSpec::kMaxHarmonics; n += 2)
                {
                    const double sign = ((n - 1) / 2) % 2 == 0 ? 1.0 : -1.0;
                    // Use the continuously varying exponent instead of fixed 2
                    const double amp = sign * 8.0 / (pi * pi)
                                     / std::pow ((double) n, (double) decayPow);
                    fs.amplitudes[(size_t)(n - 1)] = (float) amp;
                    fs.phases[(size_t)(n - 1)]     = 0.0f;
                    populated = n;
                }
                fs.numHarmonics = populated;
            }
            return spec;
        }

        /** Square (50% duty) at frame 0; progressively narrows to ~10% pulse
         *  by frame 15 — classic pulse-width morph. All harmonics (not odd-only)
         *  because pulse waves have both odd + even harmonics. */
        static WavetableSpec makeSquareSpec()
        {
            WavetableSpec spec;
            constexpr double pi = 3.14159265358979323846;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                // Pulse width: 0.5 (square) → 0.10 (narrow pulse) over 15 frames
                const double pw = 0.5 - (double) t * 0.40;
                int populated = 0;
                for (int n = 1; n <= FrameSpec::kMaxHarmonics; ++n)
                {
                    const double amp = (4.0 / pi) * (std::sin (pi * (double) n * pw) / (double) n);
                    if (std::abs (amp) < 1e-6)
                    {
                        fs.amplitudes[(size_t)(n - 1)] = 0.0f;
                    }
                    else
                    {
                        fs.amplitudes[(size_t)(n - 1)] = (float) amp;
                        populated = n;
                    }
                    fs.phases[(size_t)(n - 1)] = 0.0f;
                }
                fs.numHarmonics = populated;
            }
            return spec;
        }

        /** Pulse — palindrome pulse-width sweep: starts narrow (5%), opens to
         *  square (50%) at frame 7-8, closes back to narrow (5%) at frame 15.
         *  Produces a distinctive, symmetric timbre journey. */
        static WavetableSpec makePulseSpec()
        {
            WavetableSpec spec;
            constexpr double pi = 3.14159265358979323846;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                // Palindrome: 0..1..0 mapped via (1 - |2t - 1|)
                const double tBent = 1.0 - std::abs (2.0 * (double) t - 1.0);  // 0..1..0
                // pw: 0.05 (narrow) → 0.50 (square) → 0.05 (narrow)
                const double pw = 0.05 + tBent * 0.45;
                int populated = 0;
                for (int n = 1; n <= FrameSpec::kMaxHarmonics; ++n)
                {
                    const double amp = (2.0 / (pi * (double) n))
                                     * std::sin (pi * (double) n * pw);
                    if (std::abs (amp) < 1e-6)
                    {
                        fs.amplitudes[(size_t)(n - 1)] = 0.0f;
                    }
                    else
                    {
                        fs.amplitudes[(size_t)(n - 1)] = (float) amp;
                        populated = n;
                    }
                    fs.phases[(size_t)(n - 1)] = (float)(pi * 0.5);  // cos phase
                }
                fs.numHarmonics = populated;
            }
            return spec;
        }

        // ── Analog category: 6 iconic tables (Phase 2A) ──────────────────────
        // All use 16 frames so user can scan/morph via the FRAME knob.
        // Generated additively from sine harmonics — clean-room implementations,
        // no sampled or copyrighted content. Character is in the harmonic
        // distribution + frame-to-frame morph curve, not in any specific sample.

        /** Prophet 5-style saw — Phase 11k: vintage warm (frame 0) → hyper-bright (frame 15).
         *  Frame 0: 24 harmonics, 1/h decay (vintage Prophet character).
         *  Frame 15: 96 harmonics, 1/h^0.65 decay (brutally bright modern saw).
         *  Decay power migrates 1.0 → 0.65 so upper harmonics get louder as WT POS rises. */
        static WavetableSpec makeProphetSawSpec()
        {
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                // Harmonic count: 24 (vintage warm) → 96 (modern bright), quadratic
                const int numH = juce::jlimit (24, 96,
                                                (int) std::round (24.0f + 72.0f * t * t));
                fs.numHarmonics = numH;
                // Decay power: 1.0 (classic 1/h) → 0.65 (bright, harmonics sustained)
                const float decayPow = 1.0f - t * 0.35f;
                for (int h = 1; h <= numH; ++h)
                {
                    fs.amplitudes[(size_t)(h - 1)] = 1.0f / std::pow ((float) h, decayPow);
                    fs.phases[(size_t)(h - 1)]     = 0.0f;
                }
            }
            return spec;
        }

        /** OB-X dual-saw chorus character — Phase 11k: dual-layer enhancement.
         *  Frame 0: tight vintage saw (30 harmonics, no scatter — clean and focused).
         *  Frame 15: massive chorus saw (80 harmonics, heavy phase scatter, brighter decay).
         *  The scatter models OB-X's dual-VCO detuning baked into the waveform shape. */
        static WavetableSpec makeOBXSawSpec()
        {
            WavetableSpec spec;
            constexpr double pi2 = 2.0 * 3.14159265358979323846;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const double t = (double) f / 15.0;
                FrameSpec& fs = spec.frames[(size_t) f];
                // Harmonic count: 30 (vintage, tight) → 80 (massive chorus) quadratic
                const int hMax = juce::jlimit (30, 80,
                                                (int) std::round (30.0 + 50.0 * t * t));
                // Phase scatter: 0 (clean frame 0) → 0.45 cycles (heavy chorus)
                const double scatterAmt = t * t * 0.45;
                // Decay power: 1.0 → 0.75 (upper harmonics get louder with scatter)
                const float decayPow = (float)(1.0 - t * 0.25);
                unsigned int rng = 0xCAFEF00Du + (unsigned) f;
                auto rand01 = [&]() {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return ((double) rng / (double) 0xFFFFFFFFu);
                };
                for (int h = 1; h <= hMax; ++h)
                {
                    fs.amplitudes[(size_t)(h - 1)] = 1.0f / std::pow ((float) h, decayPow);
                    fs.phases[(size_t)(h - 1)]     = (float)(pi2 * scatterAmt * (rand01() - 0.5));
                }
                fs.numHarmonics = hMax;
            }
            return spec;
        }

        /** Juno-style 3-saw ensemble — Phase 11k: dramatic string/pad evolution.
         *  Frame 0: clean Juno saw (30 harmonics, no scatter, flat even response).
         *  Frame 15: lush ensemble (60 harmonics, heavy scatter, 2.0× even boost,
         *  brighter decay — the thick chorus string the Juno-106 is famous for). */
        static WavetableSpec makeJunoStrSpec()
        {
            WavetableSpec spec;
            constexpr double pi2 = 2.0 * 3.14159265358979323846;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const double t = (double) f / 15.0;
                FrameSpec& fs = spec.frames[(size_t) f];
                // Harmonic count: 30 (tight) → 60 (lush), quadratic
                const int hMax = juce::jlimit (30, 60,
                                                (int) std::round (30.0 + 30.0 * t * t));
                // Phase scatter: 0 → 0.40 cycles (heavy Juno ensemble detuning)
                const double scatterAmt = t * t * 0.40;
                // Even harmonic boost: 1.0 (flat) → 2.0 (rich hollow string)
                const double evenBoost = 1.0 + t * t * 1.0;
                // Decay power: 1.0 → 0.80 (brighter upper harmonics at high frames)
                const float decayPow = (float)(1.0 - t * 0.20);
                unsigned int rng = 0xBEEFCAFEu + (unsigned) f;
                auto rand01 = [&]() {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return ((double) rng / (double) 0xFFFFFFFFu);
                };
                for (int h = 1; h <= hMax; ++h)
                {
                    const double boost = (h % 2 == 0) ? evenBoost : 1.0;
                    fs.amplitudes[(size_t)(h - 1)] = (float)(boost / std::pow ((double) h, (double) decayPow));
                    fs.phases[(size_t)(h - 1)]     = (float)(pi2 * scatterAmt * (rand01() - 0.5));
                }
                fs.numHarmonics = hMax;
            }
            return spec;
        }

        // ── Phase 11h — Morph category: dramatic WT POS sweep wavetables ──
        // Each varies harmonic content / phase / formant ACROSS its 16 frames,
        // so sweeping WT POS feels like a night-and-day timbral journey.

        // Phase 11j — dramatic landmark sweep through 5 recognizably different waveforms:
        // frame 0=sine, frame 3=triangle, frame 6=square, frame 9=saw,
        // frame 12=bright saw, frame 15=brutally bright (128 harmonics).
        // Each landmark interpolates smoothly to the next.
        static WavetableSpec makeHarmonicRiseSpec()
        {
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];

                // Cubic harmonic count curve: 1 → 128 with most growth in upper half
                const int numH = juce::jlimit (1, FrameSpec::kMaxHarmonics,
                                                (int) std::round (1.0f + 127.0f * t * t * t));
                fs.numHarmonics = numH;

                // Power decay parameter migrates from 1.0 (mellow 1/h) at f=0
                // through 1.0 (saw 1/h) at f=9 down to 0.4 (brutally bright) at f=15
                const float p = (t < 0.6f) ? 1.0f : (1.0f - (t - 0.6f) * 1.5f);
                const float decayPow = juce::jlimit (0.4f, 1.0f, p);

                // Odd-only flag interpolates: pure odd (sine/tri/square) at f<=6, all (saw+) above
                const float oddOnlyMix = juce::jlimit (0.0f, 1.0f, 1.0f - t * 2.5f);

                for (int h = 1; h <= numH; ++h)
                {
                    const float base = 1.0f / std::pow ((float) h, decayPow);
                    const bool  isEven = (h % 2 == 0);
                    // Triangle bias: at low t, also boost 1/h^2 falloff
                    const float triBias = (1.0f - t) * 0.7f;
                    const float baseTri = base + triBias * (1.0f / ((float) h * (float) h));
                    const float amp = isEven ? baseTri * (1.0f - oddOnlyMix)
                                              : baseTri;
                    fs.amplitudes[(size_t)(h - 1)] = amp;
                }
            }
            return spec;
        }

        // Phase 11j — dramatic clarinet→brass→vocal→supersaw morph.
        static WavetableSpec makeOddEvenSpec()
        {
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];

                // numHarmonics grows from 16 → 96 with quadratic curve
                const int numH = juce::jlimit (16, 96,
                                                (int) std::round (16.0f + 80.0f * t * t));
                fs.numHarmonics = numH;

                // Even harmonic blend: quadratic so they appear suddenly in upper half
                const float evenBlend = t * t;
                // High-harmonic boost: emphasizes h>16 at upper frames (formant-ish)
                const float midBoost = t * 0.8f;

                for (int h = 1; h <= numH; ++h)
                {
                    const float base = 1.0f / (float) h;
                    const bool  isEven = (h % 2 == 0);
                    // Formant-style emphasis at h=6-12 for "brass" character mid-sweep
                    const float formant = (h >= 4 && h <= 12)
                                          ? (1.0f + midBoost) : 1.0f;
                    fs.amplitudes[(size_t)(h - 1)] = isEven
                        ? base * evenBlend * formant
                        : base * formant;
                }
            }
            return spec;
        }

        // Phase 11j — dramatic phase + amplitude chaos sweep.
        static WavetableSpec makePhaseDriftSpec()
        {
            WavetableSpec spec;
            std::uint32_t rng = 0xDEADBEEFu;
            auto nextFloat = [&rng]() -> float
            {
                rng ^= rng << 13;
                rng ^= rng >> 17;
                rng ^= rng << 5;
                return (float) rng / (float) 0xFFFFFFFFu;
            };
            std::array<float, 64> targetPhases {};
            std::array<float, 64> ampNoise    {};
            for (int h = 0; h < 64; ++h)
            {
                targetPhases[(size_t) h] = nextFloat() * 6.28318530718f;
                ampNoise[(size_t) h]     = 0.3f + nextFloat() * 1.4f;  // 0.3..1.7 multiplier
            }

            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                fs.numHarmonics = 64;
                for (int h = 1; h <= 64; ++h)
                {
                    const float base = 1.0f / (float) h;
                    // Amplitude noise scales with t (frame 0=clean saw, frame 15=chaotic)
                    const float ampVar = 1.0f + (ampNoise[(size_t)(h - 1)] - 1.0f) * t;
                    fs.amplitudes[(size_t)(h - 1)] = base * ampVar;
                    fs.phases[(size_t)(h - 1)]     = t * targetPhases[(size_t)(h - 1)];
                }
            }
            return spec;
        }

        // Phase 11j — DRAMATIC spectral centroid sweep from sub to near-Nyquist.
        static WavetableSpec makeSpectralSweepSpec()
        {
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];

                // Center frequency: quadratic 1.5 → 90 (was 1.5 → 28)
                // Quadratic means most action in upper half
                const float center = 1.5f + 88.5f * t * t;
                // Sigma grows but stays narrow-ish for selectivity
                const float sigma = 1.5f + 6.0f * t;

                fs.numHarmonics = FrameSpec::kMaxHarmonics;  // up to 256

                for (int h = 1; h <= FrameSpec::kMaxHarmonics; ++h)
                {
                    const float d = (float) h - center;
                    // Boost factor at high frames so it isn't quiet
                    const float boost = 1.0f + t * t * 2.0f;
                    fs.amplitudes[(size_t)(h - 1)] = boost
                        * std::exp (-(d * d) / (2.0f * sigma * sigma));
                }
            }
            return spec;
        }

        // Phase 11j — DRAMATIC formant journey through 6 landmarks.
        static WavetableSpec makeFormantRiseSpec()
        {
            // Now 6 landmarks instead of 5, covering wider register range
            static const float landmarks[6][3] = {
                {  70.0f,  250.0f,  700.0f },  // Sub-bass / subwoofer pulse
                { 200.0f,  600.0f, 1400.0f },  // Tuba
                { 500.0f, 1200.0f, 2200.0f },  // Trumpet
                { 730.0f, 1090.0f, 2440.0f },  // Vowel /a/
                { 280.0f, 2400.0f, 3300.0f },  // Vowel /i/
                { 250.0f, 2700.0f, 4500.0f },  // Ultra-bright nasal
            };
            constexpr float fund = 220.0f;
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float vp = ((float) f / 15.0f) * 5.0f;   // 0..5 across 6 landmarks
                const int   v0 = juce::jlimit (0, 5, (int) vp);
                const int   v1 = juce::jlimit (0, 5, v0 + 1);
                const float vt = vp - (float) v0;
                const float F1 = landmarks[v0][0] + (landmarks[v1][0] - landmarks[v0][0]) * vt;
                const float F2 = landmarks[v0][1] + (landmarks[v1][1] - landmarks[v0][1]) * vt;
                const float F3 = landmarks[v0][2] + (landmarks[v1][2] - landmarks[v0][2]) * vt;
                FrameSpec& fs = spec.frames[(size_t) f];
                fs.numHarmonics = 96;  // up from 50
                for (int h = 1; h <= 96; ++h)
                {
                    const float freq = (float) h * fund;
                    auto gf = [] (float fr, float center, float bw) -> float
                    {
                        const float d = fr - center;
                        return std::exp (-(d * d) / (2.0f * bw * bw));
                    };
                    // Wider bandwidths for more "presence"
                    const float w = gf (freq, F1, 120.0f)
                                  + 0.85f * gf (freq, F2, 220.0f)
                                  + 0.55f * gf (freq, F3, 320.0f);
                    fs.amplitudes[(size_t)(h - 1)] = w / std::pow ((float) h, 0.7f);  // less rolloff
                }
            }
            return spec;
        }

        // Phase 11j — dramatically extended stack: 2..32 partials with progressive
        // upper-harmonic emphasis at later frames.
        static WavetableSpec makeHarmonicSeriesSpec()
        {
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const int   numH = juce::jlimit (2, 32, 2 + f * 2);  // 2..32 partials
                const float t    = (float) f / 15.0f;
                const float norm = 1.0f / std::sqrt ((float) numH);
                FrameSpec& fs = spec.frames[(size_t) f];
                fs.numHarmonics = numH;
                for (int h = 1; h <= numH; ++h)
                {
                    // Progressive emphasis on upper harmonics at later frames
                    const float upperBoost = (h > numH / 2) ? (1.0f + t * 1.5f) : 1.0f;
                    fs.amplitudes[(size_t)(h - 1)] = norm * upperBoost;
                }
            }
            return spec;
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
        float sample (int mipLevel, int frame, int idx) const noexcept
        {
            return mipData_[(size_t) ((mipLevel * numFrames_ + frame) * frameSize_ + idx)];
        }

        /** Normalize each mip level so its peak == 1.0 (prevents level imbalance
         *  where higher mip levels — with fewer harmonics — sound quieter). */
        void normalizeMipLevels() noexcept
        {
            for (int lvl = 0; lvl < numMipLevels_; ++lvl)
            {
                float peak = 0.0f;
                for (int frame = 0; frame < numFrames_; ++frame)
                    for (int s = 0; s < frameSize_; ++s)
                        peak = std::max (peak, std::abs (mipData_[(size_t) ((lvl * numFrames_ + frame) * frameSize_ + s)]));
                if (peak <= 0.0f) continue;
                const float scale = 1.0f / peak;
                for (int frame = 0; frame < numFrames_; ++frame)
                    for (int s = 0; s < frameSize_; ++s)
                        mipData_[(size_t) ((lvl * numFrames_ + frame) * frameSize_ + s)] *= scale;
            }
        }

        int                  numFrames_     = 1;
        int                  frameSize_     = kFrameSize;
        int                  numMipLevels_  = 1;
        std::vector<float>   mipData_;     // flat [mipLevel][frame][sample]
    };
}
