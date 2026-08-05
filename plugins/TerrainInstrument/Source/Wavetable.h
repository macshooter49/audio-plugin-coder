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
#include <complex>
#include <algorithm>
#include <atomic>
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

        // ── Batch 2 — arbitrary (inharmonic) partials ──────────────────────
        // If numPartials > 0, buildFromSpec uses THIS list for the frame instead
        // of the integer-harmonic arrays. Each partial sits at an arbitrary ratio
        // of the fundamental (e.g. a bell tierce at 1.2, a stretched piano partial
        // at 8.128). Band-limited per mip by ratio (treated as a fractional harmonic
        // number). NOTE: the frame is looped at the played pitch, so non-integer
        // partials become pseudo-harmonic on playback — this is the band-limited
        // wavetable rendition of an inharmonic tone, not a true tracking partial.
        struct Partial { float ratio = 0.0f; float amp = 0.0f; float phase = 0.0f; };
        static constexpr int kMaxPartials = 96;
        std::array<Partial, kMaxPartials> partials {};
        int numPartials = 0;
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
        static constexpr int kMaxFrames    = 256;   // hard ceiling on frame count (imported tables);
                                                    // also sizes renderBlend's stack weight array

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

            // iFFT synthesis buffer, reused per frame (far faster than summing sines).
            std::vector<std::complex<double>> specFFT ((size_t) frameSize_);

            for (int level = 0; level < numMipLevels_; ++level)
            {
                const int hMax = kMipMaxHarmonics[(size_t) level];
                for (int frame = 0; frame < numFrames_; ++frame)
                {
                    const FrameSpec& fs = spec.frames[(size_t) frame];

                    // Resolve this frame to per-harmonic (amp, phase) capped at hMax.
                    // Integer-harmonic frames pass through; inharmonic-partial frames are
                    // snapped onto the harmonic grid with an energy-preserving linear split
                    // (phasor sum), because a single-cycle frame looped at the played pitch
                    // is harmonic by construction — snapping keeps it seam-clean + band-limited
                    // while preserving the spectral SHAPE (the bell/piano rendition).
                    std::array<double, (size_t) (kMipMaxHarmonics[0] + 1)> rAmp {};
                    std::array<double, (size_t) (kMipMaxHarmonics[0] + 1)> rPh  {};
                    if (fs.numPartials > 0)
                    {
                        std::array<double, (size_t) (kMipMaxHarmonics[0] + 1)> px {}, py {};
                        for (int p = 0; p < fs.numPartials; ++p)
                        {
                            const FrameSpec::Partial& pt = fs.partials[(size_t) p];
                            if (pt.amp == 0.0f || pt.ratio <= 0.0f || (double) pt.ratio > (double) hMax)
                                continue;
                            const double R    = (double) pt.ratio;
                            const int    h0   = (int) std::floor (R);
                            const int    h1   = h0 + 1;
                            const double frac = R - (double) h0;
                            const double c = std::cos ((double) pt.phase), s = std::sin ((double) pt.phase);
                            auto dep = [&] (int h, double w)
                            {
                                if (h >= 1 && h <= hMax)
                                { px[(size_t) h] += (double) pt.amp * w * c;
                                  py[(size_t) h] += (double) pt.amp * w * s; }
                            };
                            dep (h0, 1.0 - frac);
                            dep (h1, frac);
                        }
                        for (int h = 1; h <= hMax; ++h)
                        {
                            const double a = std::sqrt (px[(size_t) h] * px[(size_t) h] + py[(size_t) h] * py[(size_t) h]);
                            rAmp[(size_t) h] = a;
                            rPh[(size_t) h]  = (a > 0.0) ? std::atan2 (py[(size_t) h], px[(size_t) h]) : 0.0;
                        }
                    }
                    else
                    {
                        const int hCount = std::min (hMax, fs.numHarmonics);
                        for (int h = 1; h <= hCount; ++h)
                        {
                            rAmp[(size_t) h] = (double) fs.amplitudes[(size_t) (h - 1)];
                            rPh[(size_t) h]  = (double) fs.phases[(size_t) (h - 1)];
                        }
                    }

                    // Synthesize the frame by INVERSE FFT of the resolved harmonics —
                    // mathematically identical to summing sines, ~75x faster (the morph
                    // engine + bank build depend on this speed). Bin convention:
                    // X[h] = (A_h/2)(sin φ − i cos φ), conjugate-mirrored at N−h, so the
                    // raw inverse transform yields Σ A_h·sin(2π h n/N + φ_h) exactly.
                    const int N = frameSize_;
                    std::fill (specFFT.begin(), specFFT.end(), std::complex<double> (0.0, 0.0));
                    for (int h = 1; h <= hMax && h < N - h; ++h)
                    {
                        const double a = rAmp[(size_t) h];
                        if (a == 0.0) continue;
                        const double ph = rPh[(size_t) h];
                        const std::complex<double> c (0.5 * a * std::sin (ph),
                                                     -0.5 * a * std::cos (ph));
                        specFFT[(size_t) h]       = c;
                        specFFT[(size_t) (N - h)] = std::conj (c);
                    }
                    inverseFFT (specFFT);   // raw Σ X[k]·e^{+i2πkn/N} (no normalization)

                    float* dst = &mipData_[(size_t) ((level * numFrames_ + frame) * frameSize_)];
                    for (int sample = 0; sample < N; ++sample)
                        dst[(size_t) sample] = (float) specFFT[(size_t) sample].real();
                }
            }

            normalizeMipLevels();

            // Content complete — bump the epoch LAST so a reader keying its cache on it
            // can only observe the new epoch together with the finished content.
            buildEpoch_.fetch_add (1, std::memory_order_release);
        }

        /** Build a band-limited wavetable from raw mono PCM — "turn anything into a wavetable."
         *  Slices the source into up to `maxFrames` frames, each a `kFrameSize` window taken
         *  evenly across the WHOLE file (frame 0 = start, last frame = end), so the FRAME/WT-POS
         *  axis scans the sound's timbre over time. Each window is reconstructed the SAME way
         *  buildFromSpec does — forward-FFT, keep harmonics 1..hMax per mip level, drop DC, then
         *  inverse-FFT — so every frame is perfectly loop-seamless (integer harmonics of the frame
         *  length) and alias-free per pitch (the 8 mip levels). Offline / message-thread only. */
        void buildFromPcm (const float* pcm, int totalSamples, int framesWanted = 40)
        {
            frameSize_    = kFrameSize;
            numMipLevels_ = kNumMipLevels;
            // Exactly framesWanted evenly-spaced windows across the file (the FRAME/WTPOS axis scans the
            // sound over time). The caller picks the count = the resolution mode; clamped to kMaxFrames.
            numFrames_    = (pcm != nullptr && totalSamples >= frameSize_)
                              ? juce::jlimit (1, kMaxFrames, framesWanted)
                              : 1;
            mipData_.assign ((size_t) (numMipLevels_ * numFrames_ * frameSize_), 0.0f);
            if (pcm == nullptr || totalSamples <= 0)
            { buildEpoch_.fetch_add (1, std::memory_order_release); return; }

            const int N    = frameSize_;
            const int last = juce::jmax (0, totalSamples - 1);
            const double span = (double) juce::jmax (0, totalSamples - N);   // window-start range
            std::vector<std::complex<double>> win  ((size_t) N);
            std::vector<std::complex<double>> spec ((size_t) N);

            for (int frame = 0; frame < numFrames_; ++frame)
            {
                // 1) grab a kFrameSize window (frame 0 = file start … last frame = file end).
                const int start = (numFrames_ > 1)
                                    ? (int) std::lround (span * frame / (numFrames_ - 1)) : 0;
                for (int n = 0; n < N; ++n)
                {
                    float v;
                    if (totalSamples >= N)
                        v = pcm[(size_t) juce::jlimit (0, last, start + n)];
                    else   // source shorter than one frame → stretch it to fill the cycle
                        v = pcm[(size_t) juce::jlimit (0, last, (int) ((double) n * totalSamples / N))];
                    win[(size_t) n] = std::complex<double> ((double) v, 0.0);
                }
                // 2) forward FFT → the window's harmonic spectrum (harmonic h == bin h).
                forwardFFT (win);
                // 3) per mip level, keep harmonics 1..hMax (+ conjugate mirror), drop DC & Nyquist,
                //    then inverse-FFT. spec[k] = win[k]/N so inverseFFT reconstructs the signal.
                const double invN = 1.0 / (double) N;
                for (int level = 0; level < numMipLevels_; ++level)
                {
                    const int hMax = kMipMaxHarmonics[(size_t) level];
                    std::fill (spec.begin(), spec.end(), std::complex<double> (0.0, 0.0));
                    for (int h = 1; h <= hMax && h < N - h; ++h)
                    {
                        spec[(size_t) h]       = win[(size_t) h]       * invN;
                        spec[(size_t) (N - h)] = win[(size_t) (N - h)] * invN;
                    }
                    inverseFFT (spec);
                    float* dst = &mipData_[(size_t) ((level * numFrames_ + frame) * frameSize_)];
                    for (int n = 0; n < N; ++n)
                        dst[(size_t) n] = (float) spec[(size_t) n].real();
                }
            }
            normalizeMipLevels();
            buildEpoch_.fetch_add (1, std::memory_order_release);
        }

        /** fb253 — analyze THIS table into a 16-frame WavetableSpec (per-frame harmonic amps+phases),
         *  so the SpectralMorph engine (which consumes a WavetableSpec) can act on ANY loaded table —
         *  imported / custom, not just factory presets. Exact inverse of buildFromSpec's synthesis
         *  convention (verified round-trip to machine precision): with c = X_fwd[h]/N,
         *  A_h = 2|c|, phi_h = atan2(Re c, -Im c). Reads mip 0 (full bandwidth). Message-thread use
         *  (16 FFTs); cache the result and re-derive only when the source table changes (buildEpoch). */
        WavetableSpec toSpec() const noexcept
        {
            WavetableSpec spec;
            const int N = frameSize_;
            if (numFrames_ < 1 || N < 4 || mipData_.empty()) return spec;
            std::vector<std::complex<double>> buf ((size_t) N);
            const int hMax = std::min (FrameSpec::kMaxHarmonics, N / 2 - 1);
            const double invN = 1.0 / (double) N;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const int srcFrame = (numFrames_ > 1)
                    ? (int) std::lround ((double) f / (double) (WavetableSpec::kNumFrames - 1) * (double) (numFrames_ - 1))
                    : 0;
                const float* src = &mipData_[(size_t) ((size_t) srcFrame * (size_t) frameSize_)];   // mip level 0
                for (int n = 0; n < N; ++n) buf[(size_t) n] = std::complex<double> ((double) src[(size_t) n], 0.0);
                forwardFFT (buf);
                FrameSpec& fs = spec.frames[(size_t) f];
                fs.numPartials  = 0;                 // harmonic representation
                fs.numHarmonics = hMax;
                for (int h = 1; h <= hMax; ++h)
                {
                    const std::complex<double> c = buf[(size_t) h] * invN;
                    fs.amplitudes[(size_t) (h - 1)] = (float) (2.0 * std::abs (c));
                    fs.phases[(size_t) (h - 1)]     = (float) std::atan2 (c.real(), -c.imag());
                }
            }
            return spec;
        }

        /** BUILD EPOCH — increments after every COMPLETED (re)build. The spectral-morph
         *  slots rebuild their two Wavetable objects IN PLACE forever (same addresses),
         *  so a pointer-keyed cache (the voice's blend composite) can never tell that
         *  the CONTENT changed — a composite rendered against a mid-build (zeroed)
         *  table stayed latched as SILENCE until an unrelated knob moved (the
         *  2026-07-05 scope-flatline root cause). Keying on (pointer, epoch) forces a
         *  fresh composite after every completed rebuild. Atomic: message-thread
         *  writes, audio-thread reads; NOT copied by the (deleted-by-atomic) implicit
         *  copy — tables are built in place, never copied. */
        int buildEpoch() const noexcept { return buildEpoch_.load (std::memory_order_acquire); }

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

        // ── WT BLUR (frame blend) ─────────────────────────────────────────────
        // Builds ONE blended single-cycle waveform at `mip`, centred on `framePos`,
        // width set by `blur` (0..1). It is a weighted sum of the wavetable's frames:
        //   • blur = 0  → exactly the bilinear two-frame read (bit-identical to lookup()).
        //   • blur > 0  → a Gaussian window over the frame axis blended in, smearing a
        //                 band of frames into a richer, creamier composite.
        // Alias-free by construction: EVERY blended frame is read from the SAME mip, so
        // the band edge is shared and a linear sum of band-limited frames stays
        // band-limited (no new partials, no imaging). Edge-clamped + weight-renormalised
        // so the ends don't thin out, and RMS-matched to the un-blurred frame so loudness
        // is constant across the whole blur range. Build once per block, then read every
        // sample with readCycle().  `out` must hold frameSize_ samples.
        void renderBlend (int mip, float framePos, float blur, float* out) const noexcept
        {
            const int lvl = juce::jlimit (0, numMipLevels_ - 1, mip);
            const int N   = numFrames_;
            framePos = juce::jlimit (0.0f, 1.0f, framePos);
            blur     = juce::jlimit (0.0f, 1.0f, blur);

            const float fIdx  = framePos * (float) (N - 1);   // centre in frame units
            const int   f0    = juce::jlimit (0, N - 1, (int) fIdx);
            const int   f1    = f0 < N - 1 ? f0 + 1 : f0;
            const float fFrac = fIdx - (float) f0;

            // ── FAST PATH — blur ≈ 0 (the common case): exactly the bilinear two-frame read.
            // O(frameSize), INDEPENDENT of frame count — so scanning/modulating WT-POS on a big
            // imported table (up to kMaxFrames frames) costs the SAME as a 16-frame factory table.
            // ⚡ THE CPU FIX: an LFO on WT-POS makes the caller rebuild this EVERY block, and the old
            // path was O(frameSize × N) with N exp() calls even at blur 0 → a massive spike on
            // 256-frame imports (fine on factory's 16). This short-circuits it.
            if (blur <= 1.0e-4f)
            {
                for (int n = 0; n < frameSize_; ++n)
                    out[n] = sample (lvl, f0, n) * (1.0f - fFrac) + sample (lvl, f1, n) * fFrac;
                return;   // the bilinear read IS the RMS reference → nothing to re-match
            }

            // ── BLUR PATH — Gaussian over a BOUNDED band around the centre. Frames past ~4σ carry
            // negligible weight (exp(-8) ≈ 3e-4), so only [lo..hi] is summed → O(frameSize × band)
            // instead of O(frameSize × N). Keeps blur cheap on big imported tables too.
            const float sigma = 0.0001f + blur * blur * 9.0f;  // frame-axis spread
            const int   band  = juce::jmin (N, (int) std::ceil (sigma * 4.0f) + 2);
            const int   lo    = juce::jmax (0,     f0 - band);
            const int   hi    = juce::jmin (N - 1, f1 + band);

            float w[kMaxFrames] = { 0.0f };   // sized to kMaxFrames; only [lo..hi] is written/read
            float gsum = 0.0f;
            for (int f = lo; f <= hi; ++f)
            {
                const float d = ((float) f - fIdx) / sigma;
                const float g = std::exp (-0.5f * d * d);
                w[(size_t) f] = g;
                gsum += g;
            }
            const float gInv = gsum > 1.0e-12f ? (blur / gsum) : 0.0f;
            for (int f = lo; f <= hi; ++f) w[(size_t) f] *= gInv;     // Gaussian part, scaled by blur
            w[(size_t) f0] += (1.0f - blur) * (1.0f - fFrac);         // + bilinear part, scaled by (1-blur)
            w[(size_t) f1] += (1.0f - blur) * fFrac;

            // Build the blended cycle over the band + accumulate RMS of it and of the bilinear ref.
            double accBlend = 0.0, accRef = 0.0;
            for (int n = 0; n < frameSize_; ++n)
            {
                float v = 0.0f;
                for (int f = lo; f <= hi; ++f)
                    v += w[(size_t) f] * sample (lvl, f, n);
                out[n] = v;
                accBlend += (double) v * (double) v;

                const float ref = sample (lvl, f0, n) * (1.0f - fFrac)
                                + sample (lvl, f1, n) * fFrac;
                accRef += (double) ref * (double) ref;
            }

            // RMS-match to the un-blurred frame → loudness independent of blur amount.
            const double g  = accBlend > 1.0e-20 ? std::sqrt (accRef / accBlend) : 1.0;
            const float  gf = (float) g;
            for (int n = 0; n < frameSize_; ++n) out[n] *= gf;
        }

        // Read a pre-built single-cycle buffer (from renderBlend) with the SAME phase
        // interpolation lookup() uses, so blur = 0 reproduces lookup() exactly.
        static float readCycle (const float* buf, float phase) noexcept
        {
            const float p     = phase - std::floor (phase);
            const float pIdx  = p * (float) kFrameSize;
            const int   p0    = (int) pIdx;
            const int   p1    = (p0 + 1) % kFrameSize;
            const float pFrac = pIdx - (float) p0;
            return buf[p0] + (buf[p1] - buf[p0]) * pFrac;
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
            // Batch 1.1 — BASIC, refined to the T.
            // Frame 0: a MATHEMATICALLY PURE sine (single harmonic, nothing else).
            // Morph: a gentle harmonic "bloom" — a 1/h^1.8 series (odd + even, with
            // even harmonics softened for a rounder tone) scaled by t, so WT POS
            // sweeps sine → warm horn/organ. All sine-phase → phase-coherent,
            // click-free morph. This gives the WT POS knob real, musical travel
            // while keeping frame 0 a textbook sine.
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                fs.amplitudes[0] = 1.0f;
                fs.phases[0]     = 0.0f;
                if (t < 1.0e-4f) { fs.numHarmonics = 1; continue; }
                const int numH = juce::jlimit (1, 24, 1 + (int) std::round (23.0f * t));
                fs.numHarmonics = numH;
                const float bloom = t;                       // linear bloom intensity
                for (int h = 2; h <= numH; ++h)
                {
                    const float evenSoften = (h % 2 == 0) ? 0.55f : 1.0f;
                    fs.amplitudes[(size_t)(h - 1)] = bloom * 0.5f * evenSoften
                                                   / std::pow ((float) h, 1.8f);
                    fs.phases[(size_t)(h - 1)]     = 0.0f;
                }
            }
            return spec;
        }

        /** Triangle at frame 0; by frame 15 the 1/h² harmonic rolloff flattens
         *  toward 1/h (triangle → almost-square). Odd-only throughout. */
        static WavetableSpec makeTriangleSpec()
        {
            // Batch 1.1 — exact band-limited triangle at frame 0: odd harmonics only,
            // amplitude 8/π²·1/h², ALTERNATING sign (the textbook triangle series).
            // Morph: the rolloff exponent eases 2.0 → 1.25, brightening it into a
            // hollow, reedy tone — it stays ODD-ONLY the whole way (never a saw),
            // so it's a distinct "bright triangle" journey, not a clone of saw.
            WavetableSpec spec;
            constexpr double pi = 3.14159265358979323846;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                const double decayPow = 2.0 - 0.75 * (double) t;   // 2.0 → 1.25
                int populated = 0;
                for (int n = 1; n <= FrameSpec::kMaxHarmonics; n += 2)
                {
                    const double sign = (((n - 1) / 2) % 2 == 0) ? 1.0 : -1.0;
                    const double amp  = sign * (8.0 / (pi * pi))
                                      / std::pow ((double) n, decayPow);
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
            // Batch 1.1 — exact band-limited square at frame 0 (duty 0.5 → the
            // sin(nπ·0.5) term auto-nulls even harmonics, leaving 1/h odd-only).
            // Morph: PWM, duty 0.5 → 0.15 (square → hollow pulse). COSINE phase
            // (π/2) on every harmonic so the pulse is centered/symmetric and the
            // whole duty sweep is phase-continuous (no morph clicks). The "warm PWM".
            WavetableSpec spec;
            constexpr double pi = 3.14159265358979323846;
            const float cosPhase = (float) (pi * 0.5);
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                const double pw = 0.5 - 0.35 * (double) t;      // 0.5 → 0.15
                int populated = 0;
                for (int n = 1; n <= FrameSpec::kMaxHarmonics; ++n)
                {
                    const double amp = (2.0 / (pi * (double) n)) * std::sin (pi * (double) n * pw);
                    if (std::abs (amp) < 1.0e-6)
                        fs.amplitudes[(size_t)(n - 1)] = 0.0f;
                    else { fs.amplitudes[(size_t)(n - 1)] = (float) amp; populated = n; }
                    fs.phases[(size_t)(n - 1)] = cosPhase;
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
            // Batch 1.1 (rev) — a DISTINCT pulse, never a 50% square (that's the Square
            // table's job). Frame 0 = 33% "third-less" pulse: duty 1/3 nulls the 3rd
            // harmonic, giving a hollow, woody, clarinet-ish tone that's audibly NOT a
            // square even at WT POS 0. Morph thins it 0.33 → 0.06 (bright, nasal, reedy
            // lead). Cosine phase, phase-continuous. So Square = wide hollow PWM, Pulse =
            // thin bright pulse — two different timbral zones, distinct at every position.
            WavetableSpec spec;
            constexpr double pi = 3.14159265358979323846;
            const float cosPhase = (float) (pi * 0.5);
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                const double pw = 0.33 - 0.27 * (double) t;     // 0.33 (hollow) → 0.06 (nasal)
                int populated = 0;
                for (int n = 1; n <= FrameSpec::kMaxHarmonics; ++n)
                {
                    const double amp = (2.0 / (pi * (double) n)) * std::sin (pi * (double) n * pw);
                    if (std::abs (amp) < 1.0e-6)
                        fs.amplitudes[(size_t)(n - 1)] = 0.0f;
                    else { fs.amplitudes[(size_t)(n - 1)] = (float) amp; populated = n; }
                    fs.phases[(size_t)(n - 1)] = cosPhase;
                }
                fs.numHarmonics = populated;
            }
            return spec;
        }

        // ── Analog category: 6 iconic tables (Phase 11l research-driven) ──────
        // Each factory is circuit-grounded — harmonic signatures derived from
        // published research on each synth's actual VCO/filter architecture.
        // See docs/research/2026-06-03-analog-oscillator-research.md for sources.

        /** Prophet 5 (SSM 2030) — Phase 11l research-driven.
         *  SSM 2030 sawtooth-core: mild even-harmonic boost (+12%) at vintage frames
         *  from transistor-pair mismatch, fading at high frames. Soft exponential
         *  taper above h=20 at frame 0 (SSM noise floor). Coherent phases (single VCO).
         *  Sweep: vintage warm (frame 0) → modern bright aggressive (frame 15). */
        static WavetableSpec makeProphetSawSpec()
        {
            // SSM 2030 sawtooth-core character:
            // - Mild even-harmonic boost at low frames (vintage warm)
            // - Standard 1/h sawtooth transitioning to 1/h^0.70 at high frames (modern bright)
            // - Even-harmonic boost fades out as upper harmonic count rises
            // - Coherent (zero) phases — Prophet uses single VCO per voice, no detuning baked in
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];

                // Harmonic count: 24 (vintage) → 80 (modern bright), quadratic
                const int numH = juce::jlimit (24, 80,
                                                (int) std::round (24.0f + 56.0f * t * t));
                fs.numHarmonics = numH;

                // Decay power: 1.0 (classic 1/h) → 0.70 (bright) linear
                const float decayPow = 1.0f - t * 0.30f;

                // Even-harmonic boost: +12% at frame 0 → 0% at frame 8 → 0% at frame 15
                // Models SSM 2030 transistor-pair mismatch in expo converter
                const float evenBoost = juce::jmax (0.0f, 0.12f - t * 0.17f);  // 0.12 → 0 by t=0.7

                // Soft taper above harmonic 20 at low frames (models SSM noise floor)
                // The taper fades away as we open up to modern bright mode
                const float taperStart = 20.0f + t * 60.0f;  // moves from 20 to 80 over sweep

                // SSM 2030 (Rev 1/2) instability is a core part of the early-Prophet
                // character — the chips drift. Model it as a SMALL harmonic phase
                // scatter that's largest at the vintage frame and fades to coherent by
                // the stable CEM 3340 (Rev 3) end. Subtle: single-VCO, not a detune.
                const float ssmDrift = juce::jmax (0.0f, 0.18f * (1.0f - t * 1.3f));
                unsigned int rng = 0x50554E4Bu + (unsigned) f;
                auto rand11 = [&]() -> float {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return ((float) rng / (float) 0xFFFFFFFFu) * 2.0f - 1.0f;
                };

                for (int h = 1; h <= numH; ++h)
                {
                    float amp = 1.0f / std::pow ((float) h, decayPow);

                    // Even-harmonic SSM boost
                    if (h % 2 == 0)
                        amp *= (1.0f + evenBoost);

                    // Soft exponential taper for high harmonics at vintage frames
                    if ((float) h > taperStart)
                    {
                        const float taperAmt = ((float) h - taperStart) / 20.0f;
                        amp *= std::exp (-taperAmt * (1.0f - t) * 1.5f);
                    }

                    fs.amplitudes[(size_t)(h - 1)] = amp;
                    // Mostly coherent; vintage frames get a touch of SSM phase drift.
                    fs.phases[(size_t)(h - 1)] = ssmDrift * rand11() * (float) h * 0.04f;
                }
            }
            return spec;
        }

        /** Jupiter-8 (Roland IR3R09) PWM — Phase 11l research-driven.
         *  Pure mathematical pulse-wave formula amp(h,pw) = (2/(pi*h)) * sin(pi*h*pw).
         *  Creates authentic harmonic NULLS at specific duty cycle positions:
         *  h=3 null at pw=33%, h=4 null at pw=25% — the "hollow animated" JP-8 quality.
         *  Sweep: 50% (square/odd-only/hollow) → 8% (narrow pulse/bright/all harmonics). */
        static WavetableSpec makeJupiterPWMSpec()
        {
            // Roland Jupiter-8 VCO1 square-to-narrow-pulse sweep.
            // Pure mathematical pulse-wave formula: amp(h,pw) = (2/(pi*h)) * sin(pi*h*pw)
            // This creates authentic harmonic nulls at specific duty cycle positions —
            // the "hollow" and "animated" quality of Jupiter-8 pads comes from these nulls.
            WavetableSpec spec;
            constexpr double pi = 3.14159265358979323846;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const double t = (double) f / 15.0;
                FrameSpec& fs = spec.frames[(size_t) f];

                // Pulse width: 0.50 (square) → 0.08 (narrow pulse), linear
                const double pw = 0.50 - t * 0.42;

                // Jupiter-8 harmonic count: up to 96 harmonics
                // Jupiter VCOs have a small reset-discharge transient (the "rounding
                // at the top" / notch) that adds a little high-harmonic edge — the
                // Roland brightness. Model it as a gentle HF emphasis (~+15% by h=96).
                const double glitch = 0.15;
                int populated = 0;
                for (int h = 1; h <= 96; ++h)
                {
                    // Standard pulse-wave formula — mathematically exact, no approximations
                    double amp = (2.0 / (pi * (double) h)) * std::sin (pi * (double) h * pw);
                    amp *= (1.0 + glitch * ((double) h / 96.0));   // reset-transient HF edge

                    if (std::abs (amp) < 1e-6)
                        fs.amplitudes[(size_t)(h - 1)] = 0.0f;
                    else
                    {
                        fs.amplitudes[(size_t)(h - 1)] = (float) amp;
                        populated = h;
                    }
                    fs.phases[(size_t)(h - 1)] = 0.0f;
                }
                fs.numHarmonics = populated;
            }
            return spec;
        }

        /** Minimoog (discrete transistor) square-to-saw — Phase 11l research-driven.
         *  Starts as near-square (odd harmonics dominant) with DC-offset leakage on h=2 (~3%).
         *  Even harmonics GROW quadratically as frames advance (mixer saturation model).
         *  Moog IIR soft taper above h=25 (capacitor integrator soft limit).
         *  Sweep: thick warm square (frame 0) → fat driven 3-oscillator sawtooth (frame 15). */
        static WavetableSpec makeMoogSqrSpec()
        {
            // Minimoog discrete-transistor "square" — built on the MEASURED ~48% duty.
            // Real Minimoogs sit at 48/52%, not a perfect 50%, and that asymmetry is
            // exactly what injects the weak EVEN harmonics behind the Moog square's
            // fat-yet-hollow overtone (owner scope measurements; SOS Synth Secrets).
            // Frame 0 = authentic 48% pulse. Morph: duty widens 0.48 → 0.40 and the
            // spectrum brightens/fattens (mixer + ladder push) — warm square → fat
            // driven lead, staying in the pulse family (cosine phase) so it keeps its
            // Moog identity and morphs click-free. Moog is fat AND bright, so the top
            // stays extended; only a gentle integrator rolloff at the extreme top.
            constexpr double pi = 3.14159265358979323846;
            const float cosPhase = (float) (pi * 0.5);
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                const double pw   = 0.48 - 0.08 * (double) t;     // 0.48 → 0.40 (more even content)
                const float  fatten = 1.0f + 0.25f * t;           // low-harmonic body lift when driven
                const int    numH = juce::jlimit (32, 72, (int) std::round (32.0f + 40.0f * t));
                fs.numHarmonics = numH;
                for (int h = 1; h <= numH; ++h)
                {
                    double amp = (2.0 / (pi * (double) h)) * std::sin (pi * (double) h * pw);
                    if (h <= 4) amp *= (double) fatten;           // fatten the body across the morph
                    // Gentle transistor/integrator rolloff only at the very top (stays bright)
                    if (h > 40)
                    {
                        const float excess = (float) (h - 40) / 32.0f;
                        amp *= std::exp (-excess * (1.2f - t) * 0.6f);
                    }
                    if (std::abs (amp) < 1.0e-6)
                        fs.amplitudes[(size_t)(h - 1)] = 0.0f;
                    else
                        fs.amplitudes[(size_t)(h - 1)] = (float) amp;
                    fs.phases[(size_t)(h - 1)] = cosPhase;
                }
            }
            return spec;
        }

        /** Oberheim OB-X (CEM 3340 + discrete SEM filter) — Phase 11l research-driven.
         *  Gaussian rolloff above h=22 (capacitor filter between oscillator and VCF).
         *  Even-harmonic boost from CEM 3340 triangle-core sawtooth derivation (+8%).
         *  Serial VCA distortion: h=2 +10-30%, h=3 +5-15% (the "ballsy" Oberheim character).
         *  Sweep: clean vintage tight (frame 0) → gritty serial-distorted lead (frame 15). */
        static WavetableSpec makeOBXSawSpec()
        {
            // Oberheim OB-X sawtooth character:
            // - 10kHz Gaussian rolloff above ~harmonic 22 (capacitor filter confirmed by Electric Druid)
            // - Even-harmonic boost from CEM 3340 triangle-core sawtooth derivation (+8%)
            // - Serial VCA distortion adds h=2,3 enhancement (the "ballsy" Oberheim character)
            // - WT POS sweep: clean vintage (rolloff tight, low distortion) → gritty lead
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];

                // Harmonic count: 22 (capacitor-limited) → 60 (aggressive, rolloff lifted)
                const int numH = juce::jlimit (22, 60,
                                                (int) std::round (22.0f + 38.0f * t * t));
                fs.numHarmonics = numH;

                // OB-X signature: a one-pole capacitor rolloff sits between the
                // oscillators and the filter (Electric Druid / GreatSynthesizers
                // teardown) — THE reason Oberheims read "dull but girthy/brassy".
                // Corner harmonic ~h=18 (vintage, strong mid-forward tilt) lifting to
                // ~h=42 (aggressive, brighter) as WT POS opens up.
                const float cornerH = 18.0f + t * 24.0f;   // ~10 kHz capacitor corner

                // Serial VCA distortion boost on h=2, h=3 — models OB-X VCA chain overdrive
                const float h2Boost = 1.10f + t * 0.20f;  // +10% at f0 → +30% at f15
                const float h3Boost = 1.05f + t * 0.10f;  // +5%  at f0 → +15% at f15

                // Even-harmonic CEM triangle-core boost (consistent — fixed by circuit)
                const float evenBoost = 0.08f;

                for (int h = 1; h <= numH; ++h)
                {
                    float amp = 1.0f / (float) h;  // base sawtooth

                    // CEM triangle-core even-harmonic boost
                    if (h % 2 == 0)
                        amp *= (1.0f + evenBoost);

                    // Serial VCA distortion boosts
                    if (h == 2) amp *= h2Boost;
                    if (h == 3) amp *= h3Boost;

                    // One-pole (−6 dB/oct) capacitor rolloff — gentler and more
                    // authentic than a Gaussian; affects all harmonics, more above corner.
                    {
                        const float r = (float) h / cornerH;
                        amp /= std::sqrt (1.0f + r * r);
                    }

                    fs.amplitudes[(size_t)(h - 1)] = amp;
                    fs.phases[(size_t)(h - 1)]     = 0.0f;  // single VCO, coherent
                }
            }
            return spec;
        }

        /** Yamaha CS-80 brass (dual VCO + dual HPF+LPF chain) — Phase 11l research-driven.
         *  Architecturally unique: bandpass formant structure from HPF+resonant-LPF combination.
         *  Sub-harmonic attenuation: h=1 at 70%, h=2 at 85% (HPF removes mud below ~190Hz).
         *  Formant bell curve centered h=4→7 across sweep (resonant LPF bump).
         *  Dual-VCO phase scatter grows (Vangelis-style detuning).
         *  Sweep: soft brass (mild formant) → aggressive bite (strong formant + scatter). */
        static WavetableSpec makeCS80BrassSpec()
        {
            // Yamaha CS-80 dual-VCO + dual-filter brass character.
            // Architecturally unique among the 6 oscillators:
            // - Bandpass formant structure (HPF + resonant LPF combination)
            // - Sub-harmonic attenuation (HPF removes h=1,2 partially)
            // - Formant peak centered near h=5 for mid-range brass character
            // - Dual-VCO phase scatter increases with WT POS (detuning model)
            constexpr double pi2 = 2.0 * 3.14159265358979323846;
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const double t = (double) f / 15.0;
                FrameSpec& fs = spec.frames[(size_t) f];

                // Harmonic count: 40 (soft) → 80 (bright aggressive)
                const int numH = juce::jlimit (40, 80,
                                                (int) std::round (40.0 + 40.0 * t));
                fs.numHarmonics = numH;

                // Formant parameters:
                // Center: h=4 (soft) → h=7 (aggressive) — formant shifts up with energy
                const double fCenter  = 4.0 + t * 3.0;
                // Width: 2.5 harmonics (focused) → 3.5 (broader)
                const double fWidth   = 2.5 + t * 1.0;
                // Strength: +40% (soft) → +120% (aggressive) relative to 1/h base
                const double fBoost   = 0.40 + t * 0.80;

                // Dual-VCO phase scatter (detuning between the two CS-80 layers)
                const double scatterAmt = t * t * 0.25;  // 0 → 0.25 cycles at h=1

                unsigned int rng = 0xC580FEEDu + (unsigned) f;  // deterministic per frame
                auto rand01 = [&]() -> double {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return (double) rng / (double) 0xFFFFFFFFu;
                };

                for (int h = 1; h <= numH; ++h)
                {
                    // Base sawtooth
                    double amp = 1.0 / (double) h;

                    // HPF attenuation (sub-harmonic reduction)
                    if (h == 1)      amp *= 0.70;  // 30% reduction on fundamental
                    else if (h == 2) amp *= 0.85;  // 15% reduction on 2nd harmonic
                    // h >= 3: no HPF effect (above HPF cutoff for typical CS-80 brass)

                    // Bandpass formant (resonant LPF creates peak)
                    const double dist = (double) h - fCenter;
                    const double bell = std::exp (-0.5 * (dist / fWidth) * (dist / fWidth));
                    amp *= (1.0 + fBoost * bell);

                    // Dual-VCO phase scatter: proportional to h (higher harmonics scatter more)
                    const double scatter = scatterAmt * (double) h;
                    const double phase   = pi2 * scatter * (rand01() - 0.5);

                    fs.amplitudes[(size_t)(h - 1)] = (float) amp;
                    fs.phases[(size_t)(h - 1)]     = (float) phase;
                }
            }
            return spec;
        }

        /** Roland Juno-60 DCO + BBD chorus — Phase 11l research-driven.
         *  DCO is rock-solid: ZERO phase scatter at frame 0 (no VCO drift).
         *  Sub-oscillator contribution grows: h=1 +30%, h=2 +15%, h=4 +8% at high frames.
         *  Chorus modeled as phase scatter 0 → 0.35 cycles (BBD FM sideband approximation).
         *  Sweep: thin solo DCO (clean, stable) → full Juno ensemble pad (sub+chorus). */
        static WavetableSpec makeJunoStrSpec()
        {
            // Roland Juno-60 DCO + chorus string ensemble character.
            // Unique features vs OBXSaw:
            // - Starts as CLEAN DCO (no phase scatter) — NOT rich from the start
            // - Sub-oscillator contribution grows: h=1 boosted +30%, h=2 +15% at high frames
            // - Chorus modeled as phase scatter growing from 0 → 0.35 cycles
            // - Even harmonic boost from sub-oscillator octave-below square wave
            // - WT POS: thin solo DCO → full Juno ensemble pad
            constexpr double pi2 = 2.0 * 3.14159265358979323846;
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const double t = (double) f / 15.0;
                FrameSpec& fs = spec.frames[(size_t) f];

                // Harmonic count: 30 (clean solo DCO) → 60 (full ensemble)
                const int hMax = juce::jlimit (30, 60,
                                                (int) std::round (30.0 + 30.0 * t));

                // Sub-oscillator blend: 0 (none) → 1 (full sub) — starts engaging at frame 3
                const double subBlend = juce::jmax (0.0, (t - 0.2) / 0.8);  // 0 until t=0.2, then ramps

                // Chorus phase scatter: 0 (no chorus) → 0.35 cycles (full chorus)
                const double scatterAmt = t * t * 0.35;  // quadratic — stays near 0 until mid-sweep

                unsigned int rng = 0xBBD60DCu + (unsigned) f;  // deterministic per frame (BBD=bucket brigade delay, DCO)
                auto rand01 = [&]() -> double {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return (double) rng / (double) 0xFFFFFFFFu;
                };

                for (int h = 1; h <= hMax; ++h)
                {
                    // Base DCO sawtooth: 1/h, all harmonics
                    double amp = 1.0 / (double) h;

                    // Sub-oscillator contribution:
                    // Sub-osc square at f0/2 reinforces h=1, partially boosts h=2,h=4
                    if (h == 1)
                        amp += subBlend * 0.30;  // fundamental reinforcement from sub-osc
                    if (h == 2)
                        amp += subBlend * 0.15 / (double) h;  // h=2 boost from sub-osc h=3 beat
                    if (h == 4)
                        amp += subBlend * 0.08 / (double) h;  // h=4 boost from sub-osc h=5 beat

                    // Chorus phase scatter (proportional to h — higher harmonics scatter more in FM)
                    const double scatter = scatterAmt * (double) h;
                    const double phase   = (scatter > 0.0) ? pi2 * scatter * (rand01() - 0.5) : 0.0;

                    fs.amplitudes[(size_t)(h - 1)] = (float) amp;
                    fs.phases[(size_t)(h - 1)]     = (float) phase;
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

        // Legacy factory methods removed — JupiterPWM, MoogSqr, CS80Brass
        // now use spec-based factories (makeJupiterPWMSpec, makeMoogSqrSpec,
        // makeCS80BrassSpec) above. Dead code kept here for reference only.
        // DEAD: makeJupiterPWM(), makeMoogSqr(), makeCS80Brass()

        // ── Digital category (Phase 11l research-driven) ─────────────────────
        // PPGWave migrated to buildFromSpec path (Gaussian peak migration, 8-bit grit).
        // DX7EP / D50Bell / M1Piano remain legacy time-domain (non-integer partials).
        // See docs/research/2026-06-03-digital-experimental-wavetable-research.md

        /** PPG Wave 2.2/2.3 — Batch 2, research-driven (spec-based, harmonic).
         *  A wavetable SCAN: a resonant "formant" harmonic peak migrates up the
         *  spectrum (h≈2 → h≈26) over a 1/h base, capped at 64 harmonics (the PPG's
         *  128-sample half-cycle limit), plus a deterministic high-harmonic GRIT
         *  floor that models the 8-bit DAC quantization buzz — the icy/glassy edge.
         *  Frame 0 = warm low-formant; frame 15 = bright digital brilliance. */
        static WavetableSpec makePPGWaveSpec()
        {
            WavetableSpec spec;
            constexpr int kMaxH = 64;          // PPG 128-sample cycle → ~64 harmonics
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t      = (float) f / 15.0f;
                const float center = 2.0f + 24.0f * t;     // formant peak migrates 2 → 26
                const float sigma  = 1.4f + 3.0f  * t;     // bump widens with the scan
                const float grit   = 0.006f + 0.018f * t;   // 8-bit quantization floor

                FrameSpec& fs = spec.frames[(size_t) f];
                fs.numHarmonics = kMaxH;

                // deterministic per-frame grit (xorshift) — stable across builds
                unsigned int rng = 0x50504721u + (unsigned) f;
                auto rnd = [&]() -> float {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return (float) rng / (float) 0xFFFFFFFFu;
                };

                for (int h = 1; h <= kMaxH; ++h)
                {
                    const float d    = (float) h - center;
                    const float bump = std::exp (-(d * d) / (2.0f * sigma * sigma));   // the formant peak
                    const float floorAmt = 0.10f / (float) h;                          // faint body under it
                    float amp = bump + floorAmt;
                    if (h >= 20) amp += grit * rnd();                                  // 8-bit DAC hash on top
                    fs.amplitudes[(size_t)(h - 1)] = amp;
                    // alternating phase adds the asymmetric "digital" waveshape look
                    fs.phases[(size_t)(h - 1)] = (h % 2 == 0) ? (float) (3.14159265358979323846 * 0.5) : 0.0f;
                }
            }
            return spec;
        }

        /** Bessel function J_n(x) of the first kind — portable ascending-series
         *  implementation (no <cmath> special-function dependency). Accurate for the
         *  range used by FM synthesis here (|x| <= ~4, n <= ~20). */
        static double besselJ (int n, double x) noexcept
        {
            if (n < 0) return ((-n) % 2 == 0 ? 1.0 : -1.0) * besselJ (-n, x);  // J_-n = (-1)^n J_n
            const double half = 0.5 * x;
            double term = 1.0;
            for (int i = 1; i <= n; ++i) term *= half / (double) i;            // (x/2)^n / n!
            double sum = term;
            const double h2 = half * half;
            for (int m = 1; m <= 40; ++m)
            {
                term *= -h2 / ((double) m * (double) (m + n));
                sum  += term;
                if (std::abs (term) < 1.0e-13) break;
            }
            return sum;
        }

        /** DX7 E.PIANO 1 — Batch 2, the classic Rhodes-y FM electric piano.
         *  REAL Algorithm 5: three parallel 2-op stacks SUMMED. Carriers all at 1.0;
         *  the signature "tine" is the OP1/OP2 stack with the modulator at the 14:1
         *  ratio; the other two stacks are 1:1 "body". Spectrum from Bessel sidebands
         *  J_k(I) at c±k·m (all integer ratios → harmonic → perfect wavetable fit).
         *  Morph = modulation index decay: frame 0 bright tine attack, frame 15 mellow
         *  body. THIS is why it finally sounds like a DX7 (real FM math, not a guess). */
        static WavetableSpec makeDX7EPSpec()
        {
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];

                // Modulation indices decay bright -> mellow across the morph.
                const double iTine  = 2.5 * std::pow (1.0 - (double) t, 1.5);   // 2.5 -> 0
                const double iBodyA = 0.30 + 1.20 * (1.0 - (double) t);          // 1.50 -> 0.30
                const double iBodyB = 0.30 + 0.90 * (1.0 - (double) t);          // 1.20 -> 0.30

                std::array<double, FrameSpec::kMaxHarmonics + 1> H {};           // harmonic accum (1-indexed)

                // One 2-op FM stack: carrier ratio c, modulator ratio m, index I, gain g.
                auto addStack = [&] (int c, int m, double I, double g)
                {
                    const int Kmax = 14;
                    for (int k = -Kmax; k <= Kmax; ++k)
                    {
                        double amp  = g * besselJ (k, I);
                        if (amp == 0.0) continue;
                        int    freq = c + k * m;
                        if (freq == 0) continue;                 // DC — skip
                        if (freq < 0) { freq = -freq; amp = -amp; }   // fold (sin(-x) = -sin x)
                        if (freq >= 1 && freq <= FrameSpec::kMaxHarmonics)
                            H[(size_t) freq] += amp;
                    }
                };

                addStack (1, 14, iTine,  0.85);   // OP1/OP2 — the tine (high-ratio modulator)
                addStack (1, 1,  iBodyA, 1.00);   // OP3/OP4 — body
                addStack (1, 1,  iBodyB, 0.90);   // OP5/OP6 — body

                int populated = 0;
                for (int h = 1; h <= FrameSpec::kMaxHarmonics; ++h)
                {
                    const float a = (float) H[(size_t) h];
                    fs.amplitudes[(size_t)(h - 1)] = a;
                    fs.phases[(size_t)(h - 1)]     = 0.0f;
                    if (std::abs (a) > 1.0e-5f) populated = h;
                }
                fs.numHarmonics = populated;
            }
            return spec;
        }

        /** D-50 Bell — Batch 2, inharmonic partial path (foundation showcase).
         *  Western tuned-bell partial ratios relative to the prime: hum 0.5, prime 1.0,
         *  tierce 1.2, quint 1.5, 1.7, nominal 2.0, 2.6, superquint 3.0, 3.3, octave
         *  nominal 4.0, + upper inharmonics. Nominal/superquint/octave-nominal (2/3/4)
         *  kept strong so the strike pitch reads correctly. Morph: frame 0 = full strike
         *  (metallic clang), frame 15 = sustain (hum/prime/tierce/nominal ring on);
         *  higher partials decay faster (∝ ratio^1.2).
         *  NOTE: single-cycle loop → this is the band-limited wavetable RENDITION of a
         *  bell (pseudo-harmonic on playback), not a true tracking-inharmonic bell. */
        static WavetableSpec makeD50BellSpec()
        {
            struct BP { float ratio, amp; };
            static const BP bells[] = {
                {0.50f, 0.45f}, {1.00f, 1.00f}, {1.20f, 0.60f}, {1.50f, 0.40f},
                {1.70f, 0.28f}, {2.00f, 0.70f}, {2.60f, 0.33f}, {3.00f, 0.50f},
                {3.30f, 0.22f}, {4.00f, 0.42f}, {5.00f, 0.20f}, {5.43f, 0.16f},
                {6.40f, 0.12f}, {8.10f, 0.09f},
            };
            const int nP = (int) (sizeof (bells) / sizeof (bells[0]));
            WavetableSpec spec;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                fs.numPartials = nP;
                for (int p = 0; p < nP; ++p)
                {
                    // higher partials decay faster across the morph
                    const float decay = std::exp (-t * 2.2f * std::pow (bells[(size_t) p].ratio, 1.2f));
                    fs.partials[(size_t) p] = { bells[(size_t) p].ratio,
                                                bells[(size_t) p].amp * decay,
                                                0.0f };
                }
            }
            return spec;
        }

        /** Korg M1 "Piano 16" — Batch 2, inharmonic partial path.
         *  Stiff-string inharmonicity: partial n sits at n·sqrt(1 + B·n²), B = 0.0005
         *  (mid register) — the slight upward stretch that gives the M1 its bright,
         *  faintly metallic 90s-house shimmer. Strong fundamental, ~ -9 dB/oct rolloff;
         *  higher partials decay faster across the morph (frame 0 = bright strike,
         *  frame 15 = mellow body). The small stretch is near-integer, so it renders
         *  cleanly as a wavetable while keeping the characteristic shimmer. */
        static WavetableSpec makeM1PianoSpec()
        {
            WavetableSpec spec;
            constexpr double B = 0.0005;
            constexpr int    nP = 28;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                fs.numPartials = nP;
                const float rollPow = 1.0f + 1.3f * t;     // brighter (1.0) -> mellow (2.3)
                for (int n = 1; n <= nP; ++n)
                {
                    const float ratio = (float) ((double) n * std::sqrt (1.0 + B * (double) n * (double) n));
                    float amp = 1.0f / std::pow ((float) n, rollPow);
                    // extra fast decay on the top octave across the morph
                    if (n > 8) amp *= std::exp (-t * 0.12f * (float) (n - 8));
                    fs.partials[(size_t)(n - 1)] = { ratio, amp, 0.0f };
                }
            }
            return spec;
        }

        // ── Vocal category (Phase 11l research-driven) ───────────────────────
        // Lorentzian formant synthesis from Peterson & Barney (1952) + Hillenbrand (1995).
        // All three spec-based (buildFromSpec path). Singer's formant ring at 3100Hz added.
        // See docs/research/2026-06-03-vowel-formant-metallic-research.md for sources.

        /** Lorentzian resonance helper: L(f,Fc,BW) = (BW/2)^2 / ((f-Fc)^2 + (BW/2)^2) */
        static float lorentzian (float f, float Fc, float BW) noexcept
        {
            const float r = BW * 0.5f;
            return (r * r) / ((f - Fc) * (f - Fc) + r * r);
        }

        /** Choir A→O — Phase 11l research-driven, spec-based.
         *  Male voice register. Frame 0 = /a/ (F1=730, F2=1090, F3=2440).
         *  Frame 15 = /o/ (F1=570, F2=840, F3=2410). Linear formant interpolation.
         *  Singer's formant ring at 3100Hz (trained male choir projection).
         *  Lorentzian resonance peaks (narrower than Gaussian → more authentic).
         *  Spectral slope -0.7 (voiced source roll-off). */
        static WavetableSpec makeChoirAtoOSpec()
        {
            // Formant targets (male voice register, Peterson & Barney 1952)
            // Frame 0 = /a/:  F1=730, F2=1090, F3=2440
            // Frame 15 = /o/: F1=570, F2=840,  F3=2410
            constexpr float F0 = 220.0f;  // canonical fundamental for spec generation
            constexpr int   numH = 64;    // 64 harmonics sufficient for this range

            // Hillenbrand et al. (1995) male formants. Bandwidths widened ~1.5x for the
            // lush massed-choir (multi-voice) blur — a soloist would use tighter BWs.
            // /a/ ("ah")
            const float a_F1=768, a_F2=1333, a_F3=2522, a_F4=3500;
            const float a_B1=120, a_B2=140,  a_B3=180,  a_B4=260;
            // /o/ ("oh")
            const float o_F1=497, o_F2=910,  o_F3=2459, o_F4=3400;
            const float o_B1=100, o_B2=120,  o_B3=160,  o_B4=260;

            WavetableSpec spec;
            for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
            {
                const float t = (float) frame / (float)(WavetableSpec::kNumFrames - 1);
                const float F1  = a_F1 + t * (o_F1 - a_F1);
                const float F2  = a_F2 + t * (o_F2 - a_F2);
                const float F3  = a_F3 + t * (o_F3 - a_F3);
                const float F4  = a_F4 + t * (o_F4 - a_F4);
                const float BW1 = a_B1 + t * (o_B1 - a_B1);
                const float BW2 = a_B2 + t * (o_B2 - a_B2);
                const float BW3 = a_B3 + t * (o_B3 - a_B3);
                const float BW4 = a_B4 + t * (o_B4 - a_B4);

                FrameSpec& fs = spec.frames[(size_t) frame];
                fs.numHarmonics = numH;
                for (int h = 1; h <= numH; ++h)
                {
                    const float freq = (float) h * F0;
                    if (freq > 8000.0f) break;

                    float w = lorentzian (freq, F1, BW1)
                            + 0.8f  * lorentzian (freq, F2, BW2)
                            + 0.5f  * lorentzian (freq, F3, BW3)
                            + 0.25f * lorentzian (freq, F4, BW4)
                            + 0.18f * lorentzian (freq, 2800.0f, 350.0f); // gentle massed-choir ring (Sundberg)
                    w *= std::pow ((float) h, -0.7f);  // spectral slope
                    fs.amplitudes[(size_t)(h - 1)] = w;
                    fs.phases[(size_t)(h - 1)] = 0.0f;  // cosine phases for stable morph
                }
            }
            return spec;
        }

        /** Whisper — Phase 11l research-driven, spec-based.
         *  Formant-shaped noise: randomized phases simulate aperiodic (noisy) source.
         *  Frame 0 = /u/-dark whisper (F1=300, F2=870). Frame 15 = /i/-bright whisper.
         *  Shallow spectral slope -0.3 (more high-frequency energy than voiced).
         *  Key: random phases = incoherent = sounds like bandpass noise (not tonal). */
        static WavetableSpec makeWhisperSpec()
        {
            // Whisper: formant-shaped noise. Uses /i/-like formant positions throughout
            // (whisper naturally produces a more neutral/bright tract shape).
            constexpr float F0 = 220.0f;
            constexpr int   numH = 96;  // more harmonics = more noise bandwidth

            struct WhisperFrame { float F1, F2, F3, BW1, BW2, BW3; };
            // Whispered formants shift slightly UP vs voiced and have much WIDER
            // bandwidths (turbulent source) — research: whispered F1/F2 raised, BW ~2-3x.
            const WhisperFrame start = { 350.0f,  950.0f, 2350.0f, 200.0f, 260.0f, 340.0f }; // /u/-ish dark
            const WhisperFrame end   = { 300.0f, 2400.0f, 3100.0f, 150.0f, 210.0f, 270.0f }; // /i/-ish bright

            WavetableSpec spec;
            std::uint32_t rng = 0xDEADBEEFu;  // fixed seed for deterministic noise shape
            auto nextPhase = [&rng]() -> float {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                return ((float) rng / (float) 0xFFFFFFFFu) * 6.28318530718f - 3.14159f;
            };

            for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
            {
                const float t = (float) frame / (float)(WavetableSpec::kNumFrames - 1);
                const float F1  = start.F1  + t * (end.F1  - start.F1);
                const float F2  = start.F2  + t * (end.F2  - start.F2);
                const float F3  = start.F3  + t * (end.F3  - start.F3);
                const float BW1 = start.BW1 + t * (end.BW1 - start.BW1);
                const float BW2 = start.BW2 + t * (end.BW2 - start.BW2);
                const float BW3 = start.BW3 + t * (end.BW3 - start.BW3);

                FrameSpec& fs = spec.frames[(size_t) frame];
                fs.numHarmonics = numH;
                for (int h = 1; h <= numH; ++h)
                {
                    const float freq = (float) h * F0;
                    if (freq > 10000.0f) break;

                    float w = 0.45f * lorentzian (freq, F1, BW1)   // F1 de-emphasized (airy, not chesty)
                            + 1.00f * lorentzian (freq, F2, BW2)   // upper formants carry the whisper
                            + 0.85f * lorentzian (freq, F3, BW3);
                    w *= std::pow ((float) h, -0.15f);  // very shallow slope = airy broadband whisper
                    fs.amplitudes[(size_t)(h - 1)] = w;
                    fs.phases[(size_t)(h - 1)] = nextPhase();  // KEY: random phases = incoherent = noise-like
                }
            }
            return spec;
        }

        /** VowelMorph A→E→I→O→U — Phase 11l research-driven, spec-based.
         *  All 5 vowels (male register, Peterson & Barney 1952) mapped across 16 frames.
         *  Key perceptual waypoints: /i/→/o/ is the most dramatic (F2 collapses 2290→840Hz).
         *  /a/→/e/ brightens strongly (F2 jumps 1090→1840Hz).
         *  Singer's formant ring omitted (VowelMorph is broader/less choral than ChoirAtoO). */
        static WavetableSpec makeVowelMorphSpec()
        {
            // 5 cardinal vowels — Hillenbrand et al. (1995) male register.
            struct VowelParams { float F1, F2, F3, F4, BW1, BW2, BW3; };
            static const VowelParams vowels[5] = {
                // /a/ "ah"   F1    F2    F3    F4    B1   B2   B3
                {              768, 1333, 2522, 3500,  90, 110, 150 },
                // /e/ "eh"
                {              580, 1799, 2605, 3500,  80, 100, 140 },
                // /i/ "ee"
                {              342, 2322, 3000, 3657,  60,  90, 150 },
                // /o/ "oh"
                {              497,  910, 2459, 3400,  80, 100, 140 },
                // /u/ "oo"
                {              378,  997, 2343, 3400,  70,  90, 130 },
            };

            constexpr float F0 = 220.0f;
            constexpr int   numH = 64;

            WavetableSpec spec;
            for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
            {
                const float t    = (float) frame / (float)(WavetableSpec::kNumFrames - 1);
                const float vIdx = t * 4.0f;  // 0..4 indexes into vowels[]
                const int   v0   = juce::jlimit (0, 4, (int) vIdx);
                const int   v1   = juce::jlimit (0, 4, v0 + 1);
                const float vFrac = vIdx - (float) v0;

                // Interpolate formant CENTERS in log space (perceptually even / Bark-like);
                // bandwidths linearly.
                auto logLerp = [] (float a, float b, float u) { return a * std::pow (b / a, u); };
                const float F1  = logLerp (vowels[v0].F1, vowels[v1].F1, vFrac);
                const float F2  = logLerp (vowels[v0].F2, vowels[v1].F2, vFrac);
                const float F3  = logLerp (vowels[v0].F3, vowels[v1].F3, vFrac);
                const float F4  = logLerp (vowels[v0].F4, vowels[v1].F4, vFrac);
                const float BW1 = vowels[v0].BW1 + vFrac * (vowels[v1].BW1 - vowels[v0].BW1);
                const float BW2 = vowels[v0].BW2 + vFrac * (vowels[v1].BW2 - vowels[v0].BW2);
                const float BW3 = vowels[v0].BW3 + vFrac * (vowels[v1].BW3 - vowels[v0].BW3);

                FrameSpec& fs = spec.frames[(size_t) frame];
                fs.numHarmonics = numH;
                for (int h = 1; h <= numH; ++h)
                {
                    const float freq = (float) h * F0;
                    if (freq > 8000.0f) break;

                    float w = lorentzian (freq, F1, BW1)
                            + 0.8f  * lorentzian (freq, F2, BW2)
                            + 0.5f  * lorentzian (freq, F3, BW3)
                            + 0.25f * lorentzian (freq, F4, 150.0f);
                    w *= std::pow ((float) h, -0.7f);
                    fs.amplitudes[(size_t)(h - 1)] = w;
                    fs.phases[(size_t)(h - 1)] = 0.0f;
                }
            }
            return spec;
        }

        // ── Metallic category (Batch 3) — TRUE inharmonic partials ───────────
        // The 537-cent error is FIXED. These place partials at their real
        // Euler-Bernoulli bar / thin-shell ring-mode ratios via the arbitrary-ratio
        // partial path (energy-preserving snap onto the harmonic grid), instead of
        // forcing 2.758 onto integer harmonic 3. Sources: Fletcher & Rossing "Physics
        // of Musical Instruments" (bar/shell modes), Apfel/Rossing (glass), CCRMA.

        /** BowedMetal — tuned vibraphone bar, bowed (pitched, mellow, glassy).
         *  Tuned bending modes 1 : 4 : 10 (a vibe bar is undercut to hit these), plus a
         *  faint untuned free-free edge at 2.758 and a high shimmer mode. Bowing SUSTAINS
         *  the tone: morph = bow attack (all modes + rosin texture) → sustained ring
         *  (fundamental + tuned 4th dominate, upper/edge modes gone). */
        static WavetableSpec makeBowedMetalSpec()
        {
            WavetableSpec spec;
            for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
            {
                const float t = (float) frame / (float) (WavetableSpec::kNumFrames - 1);
                FrameSpec& fs = spec.frames[(size_t) frame];
                const FrameSpec::Partial ps[] = {
                    {  1.000f, 1.00f,                         0.0f },   // fundamental — sustains
                    {  2.758f, 0.16f * (1.0f - t),           0.0f },   // free-free metal edge (attack)
                    {  4.000f, 0.55f * (1.0f - 0.45f * t),   0.0f },   // tuned 2nd mode — strong, mellow
                    {  6.800f, 0.12f * (1.0f - t),           0.0f },   // bow texture (inharmonic)
                    { 10.000f, 0.34f * (1.0f - 0.85f * t),   0.0f },   // tuned 3rd mode — fades
                    { 20.000f, 0.10f * (1.0f - t),           0.0f },   // glassy shimmer — fades first
                };
                const int n = (int) (sizeof (ps) / sizeof (ps[0]));
                fs.numPartials = n;
                for (int p = 0; p < n; ++p) fs.partials[(size_t) p] = ps[p];
            }
            return spec;
        }

        /** GlassHarmonics — wine glass / glass armonica (the 537-fix showcase).
         *  STRUCK (frame 0): true inharmonic shell ring-modes 1 : 2.40 : 4.30 : 6.50 : 9.0
         *  (measured wine-glass (2,0):(3,0) ≈ 1 : 2.4 — Apfel/Rossing) — bell-like.
         *  RUBBED (frame 15): collapses toward a near-pure 1 : 2 : 3 : 4 with steep falloff
         *  (rubbing excites mainly the (2,0) mode + stick-slip harmonics) — pure, sustained.
         *  Each partial MIGRATES from its inharmonic struck ratio to its harmonic rubbed
         *  ratio across the morph — turning WT POS literally rubs the glass. */
        static WavetableSpec makeGlassHarmonicsSpec()
        {
            struct GP { float rS, rR, aS, aR; };   // struck/rubbed ratio + amp endpoints
            static const GP g[] = {
                { 1.00f, 1.00f, 1.00f, 1.00f },
                { 2.40f, 2.00f, 0.50f, 0.16f },
                { 4.30f, 3.00f, 0.25f, 0.06f },
                { 6.50f, 4.00f, 0.12f, 0.02f },
                { 9.00f, 5.00f, 0.06f, 0.01f },
            };
            const int n = (int) (sizeof (g) / sizeof (g[0]));
            WavetableSpec spec;
            for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
            {
                const float t = (float) frame / (float) (WavetableSpec::kNumFrames - 1);
                FrameSpec& fs = spec.frames[(size_t) frame];
                fs.numPartials = n;
                for (int p = 0; p < n; ++p)
                {
                    const float r = g[(size_t) p].rS + t * (g[(size_t) p].rR - g[(size_t) p].rS);
                    const float a = g[(size_t) p].aS + t * (g[(size_t) p].aR - g[(size_t) p].aS);
                    fs.partials[(size_t) p] = { r, a, 0.0f };
                }
            }
            return spec;
        }

        /** Railroad — struck free-free steel bar / rail (clangorous, industrial).
         *  Full Euler-Bernoulli free-free bending series 1 : 2.758 : 5.406 : 8.936 :
         *  13.350 : 18.645 (Fletcher & Rossing) — genuinely inharmonic, NOT snapped to
         *  integers. Morph = decay after impact: frame 0 = strike (all modes, dense clang)
         *  → frame 15 = ring (fundamental + low modes; high modes radiate away fastest). */
        static WavetableSpec makeRailroadSpec()
        {
            struct RP { float ratio, amp, decayPow; };
            static const RP rr[] = {
                {  1.000f, 1.00f, 0.0f },   // fundamental — rings on
                {  2.758f, 0.70f, 1.5f },   // slow decay
                {  5.406f, 0.55f, 2.5f },
                {  8.936f, 0.45f, 4.0f },
                { 13.350f, 0.35f, 6.0f },
                { 18.645f, 0.22f, 8.0f },   // very fast decay
            };
            const int n = (int) (sizeof (rr) / sizeof (rr[0]));
            WavetableSpec spec;
            for (int frame = 0; frame < WavetableSpec::kNumFrames; ++frame)
            {
                const float t = (float) frame / (float) (WavetableSpec::kNumFrames - 1);
                FrameSpec& fs = spec.frames[(size_t) frame];
                fs.numPartials = n;
                for (int p = 0; p < n; ++p)
                {
                    const float decay = (rr[(size_t) p].decayPow <= 0.0f)
                                      ? 1.0f
                                      : std::pow (juce::jmax (0.0f, 1.0f - t), rr[(size_t) p].decayPow);
                    fs.partials[(size_t) p] = { rr[(size_t) p].ratio, rr[(size_t) p].amp * decay, 0.0f };
                }
            }
            return spec;
        }

        // ── Experimental category (Phase 11l research-driven) ────────────────
        // All 4 now use buildFromSpec path (mip anti-aliasing). Each is process-defined:
        // Dustbowl=78rpm degradation, StaticEvolve=static→choir narrative,
        // SpectralDrift=phase-only variation (flat spectrum), SerumHD=HD brilliance peak.
        // See docs/research/2026-06-03-digital-experimental-wavetable-research.md

        /** Dustbowl (78rpm shellac record degradation) — Phase 11l research-driven, spec-based.
         *  LP filter cutoff narrows saw to h=18 at frame 0 (4kHz at 220Hz ref).
         *  Mid-band surface noise (h=12-30) grows with frame (shellac grain noise).
         *  Varispeed phase jitter on upper harmonics grows (worn turntable instability).
         *  Sweep: clean vintage recording (frame 0) → heavily worn/degraded record (frame 15). */
        static WavetableSpec makeDustbowlSpec()
        {
            WavetableSpec spec;
            // Pre-generate deterministic random phases and noise amplitudes (varispeed + crackle)
            std::uint32_t rng = 0xFEEDFACEu;
            auto nextF = [&rng]() -> float {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                return (float) rng / (float) 0xFFFFFFFFu;
            };
            float randPhase[48] = {};
            float randNoise[48] = {};
            for (int i = 0; i < 48; ++i) {
                randPhase[i] = nextF() * 6.28318530718f;
                randNoise[i] = 0.02f + nextF() * 0.06f;  // 0.02..0.08
            }

            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t   = (float) f / 15.0f;
                const int cutH  = juce::jlimit (18, 48, (int) std::round (18.0f + 30.0f * t));
                FrameSpec& fs   = spec.frames[(size_t) f];
                fs.numHarmonics = 48;

                for (int h = 1; h <= 48; ++h)
                {
                    // LP-filtered saw (simulates 78rpm bandwidth limit)
                    float amp = 0.0f;
                    if (h <= cutH)
                    {
                        const float norm = (float) h / (float) cutH;
                        amp = (1.0f / (float) h) * (1.0f - 0.3f * norm * norm);
                    }
                    // Mid-band surface noise (harmonics 12-30): grows with frame
                    if (h >= 12 && h <= 30)
                        amp += t * randNoise[h - 1];

                    fs.amplitudes[(size_t)(h - 1)] = amp;

                    // Varispeed phase jitter on upper harmonics (starts at h=10)
                    if (h >= 10)
                        fs.phases[(size_t)(h - 1)] = t * randPhase[h - 1] * 0.8f;
                }
            }
            return spec;
        }

        /** StaticEvolve (radio static → choir formant) — Phase 11l research-driven, spec-based.
         *  Frame 0 = pure deterministic noise (max incoherence). Frame 15 = choir /a/ vowel.
         *  Noise decays as (1-t)^1.5; tone arrives as t^2 (late dramatic reveal).
         *  Target tone: Gaussian-formant choir "ah" at A3=220Hz (rewarding arrival).
         *  Phases collapse to zero as tone arrives (noise → coherent = static → signal). */
        static WavetableSpec makeStaticEvolveSpec()
        {
            WavetableSpec spec;
            constexpr int kNH = 64;

            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t       = (float) f / 15.0f;
                const float toneAmt = t * t;                         // quadratic arrival
                const float noiseAmt = std::pow (1.0f - t, 1.5f);   // faster noise decay

                // Deterministic per-frame random noise (different character each frame)
                std::uint32_t rng = 0x12345678u + (std::uint32_t) f * 0x9E3779B9u;
                auto nxt = [&rng]() -> float {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    return (float) rng / (float) 0xFFFFFFFFu;
                };

                FrameSpec& fs = spec.frames[(size_t) f];
                fs.numHarmonics = kNH;

                for (int h = 1; h <= kNH; ++h)
                {
                    // Noise contribution (deterministic random amplitude + phase)
                    const float noiseAmp = noiseAmt * (0.2f + nxt() * 0.8f);
                    const float noisePh  = nxt() * 6.28318530718f;

                    // Tone contribution: choir "ah" vowel formants at A3=220Hz
                    const float freq = (float) h * 220.0f;
                    const float sigma = 90.0f;
                    auto gf = [&](float fc) -> float {
                        const float d = freq - fc;
                        return std::exp (-(d * d) / (2.0f * sigma * sigma));
                    };
                    const float toneAmp = toneAmt * (gf (730.0f) + 0.7f * gf (1090.0f)
                                                    + 0.45f * gf (2440.0f)) / (float) h;
                    // Mix noise and tone; phases collapse as tone arrives
                    const float totalAmp = noiseAmp + toneAmp;
                    const float ph = noiseAmt * noisePh;   // phase randomizes when noisy, zeroes as tone arrives

                    fs.amplitudes[(size_t)(h - 1)] = totalAmp;
                    fs.phases[(size_t)(h - 1)]     = ph;
                }
            }
            return spec;
        }

        /** SpectralDrift (IDENTICAL amplitude spectrum, drifting phases) — Phase 11l, spec-based.
         *  Uses FLAT spectral envelope (equal amplitude h=1..32, not 1/h like PhaseDrift Morph).
         *  Frame 0 = all phases zero → buzzy aggressive additive tone.
         *  Frame 15 = fully randomized phases → sounds like musical noise (same spectrum, different ear).
         *  Psychoacoustic paradox: spectrum analyzer shows identical curve at every frame. */
        static WavetableSpec makeSpectralDriftSpec()
        {
            WavetableSpec spec;
            // Deterministic target phases for frame 15 (maximally randomized)
            std::uint32_t rng = 0xDEADC0DEu;
            auto nextPh = [&rng]() -> float {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                return ((float) rng / (float) 0xFFFFFFFFu) * 6.28318530718f;
            };
            float targetPhases[32] = {};
            for (int h = 0; h < 32; ++h) targetPhases[h] = nextPh();

            constexpr int kNH = 32;
            const float normAmp = 1.0f / std::sqrt ((float) kNH);  // equal RMS normalization

            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t = (float) f / 15.0f;
                FrameSpec& fs = spec.frames[(size_t) f];
                fs.numHarmonics = kNH;
                for (int h = 1; h <= kNH; ++h)
                {
                    fs.amplitudes[(size_t)(h - 1)] = normAmp;
                    fs.phases[(size_t)(h - 1)]     = t * targetPhases[h - 1];
                }
            }
            return spec;
        }

        /** SerumHD (modern wavetable synthesis — maximum brilliance) — Phase 11l, spec-based.
         *  Gaussian envelope center migrates h=3 → h=45 with t^1.5 acceleration.
         *  20% even-harmonic boost (Serum brand character — "buzzy" vs "hollow").
         *  Sigma grows 4→8 for increasing density. Zero phases = crystal-clear HD quality.
         *  Sweep: warm modern saw (frame 0) → overwhelming upper-harmonic brilliance (frame 15). */
        static WavetableSpec makeSerumHDSpec()
        {
            WavetableSpec spec;
            constexpr int kNH = 96;

            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const float t      = (float) f / 15.0f;
                const float tCube  = t * std::sqrt (t);               // t^1.5 for dramatic acceleration
                const float center = 3.0f + 42.0f * tCube;           // 3 → 45
                const float sigma  = 4.0f + 4.0f  * t;               // 4 → 8
                FrameSpec& fs      = spec.frames[(size_t) f];
                fs.numHarmonics    = kNH;

                for (int h = 1; h <= kNH; ++h)
                {
                    const float d         = (float) h - center;
                    const float evenBoost = (h % 2 == 0) ? 1.20f : 1.0f;  // Serum even-harmonic brightness
                    fs.amplitudes[(size_t)(h - 1)] = evenBoost
                                                   * std::exp (-(d * d) / (2.0f * sigma * sigma));
                    fs.phases[(size_t)(h - 1)]     = 0.0f;  // HD = phase-coherent, clean
                }
            }
            return spec;
        }

    private:
        // Forward DFT via the conjugation identity F = conj( inverseFFT( conj(x) ) ) — reuses the
        // proven radix-2 transform. a[k] ← Σ_n a[n]·e^{-i2πkn/N} (raw, UNnormalized). Build-time only.
        static void forwardFFT (std::vector<std::complex<double>>& a) noexcept
        {
            for (auto& z : a) z = std::conj (z);
            inverseFFT (a);
            for (auto& z : a) z = std::conj (z);
        }

        // In-place radix-2 inverse DFT (raw, UNnormalized): a[n] ← Σ_k a[k]·e^{+i2πkn/N}.
        // N (= frameSize_, 2048) is a power of two. Build-time only (not real-time).
        static void inverseFFT (std::vector<std::complex<double>>& a) noexcept
        {
            const int n = (int) a.size();
            for (int i = 1, j = 0; i < n; ++i)               // bit-reversal permutation
            {
                int bit = n >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap (a[(size_t) i], a[(size_t) j]);
            }
            for (int len = 2; len <= n; len <<= 1)
            {
                const double ang = 2.0 * 3.14159265358979323846 / (double) len;  // +sign = inverse
                const std::complex<double> wlen (std::cos (ang), std::sin (ang));
                for (int i = 0; i < n; i += len)
                {
                    std::complex<double> w (1.0, 0.0);
                    for (int k = 0; k < len / 2; ++k)
                    {
                        const std::complex<double> u = a[(size_t) (i + k)];
                        const std::complex<double> v = a[(size_t) (i + k + len / 2)] * w;
                        a[(size_t) (i + k)]               = u + v;
                        a[(size_t) (i + k + len / 2)]     = u - v;
                        w *= wlen;
                    }
                }
            }
        }

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

    public:
        // BUILD EPOCH storage — deliberately the LAST member (offsets of everything
        // above stay stable for tooling that links older objects). See buildEpoch().
        std::atomic<int>     buildEpoch_ { 0 };
    };
}
