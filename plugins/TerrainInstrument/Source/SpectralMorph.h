#pragma once
//
//  SpectralMorph.h — Terrain Instrument
//  ─────────────────────────────────────────────────────────────────────────
//  Frequency-domain "spectral morph" for the wavetable oscillator. This is the
//  REAL spectral slot (it replaces the old per-OSC FX rack that was mislabeled
//  "spectral"). Each mode reshapes a frame's harmonic spectrum, and the result
//  is handed to Wavetable::buildFromSpec — which band-limits across mips and
//  snaps any inharmonic partials onto the harmonic grid (the Batch-2 foundation).
//
//  DESIGN — built for the modulation future (north star: every knob modulated):
//    • apply() is a PURE, STATELESS, amount-parameterized transform.
//        morphed = SpectralMorph::apply(baseSpec, mode, amount)
//    • v1 ships PER-OSC: one morphed table per oscillator, rebuilt when the
//      shape / mode / amount changes, shared by all voices. That delivers 100%
//      of the knob-turn magic.
//    • PER-VOICE later (with the LFO/mod-matrix phase) is a drop-in: call the
//      SAME apply() per voice with that voice's modulated amount and cache the
//      result per voice. The transform never changes — only where `amount`
//      comes from. Nothing here bakes a knob value in as a constant.
//
//  HONEST single-cycle note: a looped single-cycle wavetable is harmonic by
//  construction, so "sliding a partial" = its energy redistributing across the
//  harmonic grid as amount changes (the snap splits a non-integer ratio across
//  adjacent bins, centroid = the true ratio). That continuous spectral sweep is
//  exactly how a single-cycle wavetable synth (incl. Vital) actually behaves —
//  a timbral morph, not a literal microtonal glide of each partial.
//
#include "Wavetable.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace tw
{
    // Curated set (our own order — not a copy of Vital's menu). Indices are stable;
    // the dropdown/param will map 1:1 to these. Part 1 ships None + the two stretches;
    // the rest are reserved and currently pass through unchanged until their batch.
    enum class SpectralMode
    {
        None = 0,
        HarmonicStretch,      // ◆ Part 1 — even partial spacing widens (fundamental fixed)
        InharmonicStretch,    // ◆ Part 1 — power-law spacing → bell / metallic
        Vocode,               //   Part 2 — formant/vowel envelope sweep
        Smear,                //   Part 2 — spectral blur → dense / airy
        RandomAmplitudes,     //   Part 2 — re-rolled partial gains (seeded, static per note)
        DataCompress,         //   Part 2 — spectral bin reduction / quantize (digital grit)
        SpectralPhaser,       //   Part 2 — sweeping notches across the harmonic axis
        Count
    };

    class SpectralMorph
    {
    public:
        /** Pure transform: base spectrum + mode + amount(0..1) → morphed WavetableSpec
         *  ready for Wavetable::buildFromSpec. amount<=0 or None returns the base
         *  untouched (and the morph is continuous from there — at amount→0 the stretch
         *  factor → 1, so partials land back on the integer grid losslessly). */
        static WavetableSpec apply (const WavetableSpec& base, SpectralMode mode, float amount)
        {
            amount = std::clamp (amount, 0.0f, 1.0f);
            if (mode == SpectralMode::None || amount <= 0.0f)
                return base;

            WavetableSpec out;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
                morphFrame (base.frames[(size_t) f], out.frames[(size_t) f], mode, amount);
            return out;
        }

    private:
        // Read a frame's spectrum as a unified (ratio, amp, phase) partial list, whether
        // it was authored as integer harmonics OR inharmonic partials. Every mode then
        // operates on this one representation, and we write it back as partials so
        // buildFromSpec's energy-preserving snap + band-limiting handles the rest.
        static int extract (const FrameSpec& f, FrameSpec::Partial* p) noexcept
        {
            int n = 0;
            if (f.numPartials > 0)
            {
                for (int i = 0; i < f.numPartials && n < FrameSpec::kMaxPartials; ++i)
                    if (f.partials[(size_t) i].amp != 0.0f)
                        p[n++] = f.partials[(size_t) i];
            }
            else
            {
                for (int h = 1; h <= f.numHarmonics && n < FrameSpec::kMaxPartials; ++h)
                {
                    const float a = f.amplitudes[(size_t) (h - 1)];
                    if (a != 0.0f)
                        p[n++] = { (float) h, a, f.phases[(size_t) (h - 1)] };
                }
            }
            return n;
        }

        static void writeBack (FrameSpec& out, const FrameSpec::Partial* p, int n) noexcept
        {
            out.numHarmonics = 0;                       // force the partial path in buildFromSpec
            out.numPartials   = std::min (n, FrameSpec::kMaxPartials);
            for (int i = 0; i < out.numPartials; ++i)
                out.partials[(size_t) i] = p[i];
        }

        static void morphFrame (const FrameSpec& in, FrameSpec& out, SpectralMode mode, float amount) noexcept
        {
            FrameSpec::Partial p[FrameSpec::kMaxPartials];
            const int n = extract (in, p);

            switch (mode)
            {
                case SpectralMode::HarmonicStretch:
                {
                    // Even spacing WIDENS, fundamental pinned. A partial at ratio r maps to
                    // 1 + (r−1)·s, so harmonic n slides n → 1+(n−1)·s. s: 1 (identity) → 2.5.
                    // At full stretch a harmonic series 1,2,3,4… becomes 1,3.5,6,8.5… — the
                    // energy fans up the spectrum as a continuous sweep.
                    const float s = 1.0f + 2.0f * amount;
                    for (int i = 0; i < n; ++i)
                        p[(size_t) i].ratio = 1.0f + (p[(size_t) i].ratio - 1.0f) * s;
                    break;
                }

                case SpectralMode::InharmonicStretch:
                {
                    // Power-law spacing → genuinely inharmonic (bell / metallic). ratio' =
                    // ratio^p, p: 1 (identity) → 1.7. The fundamental (ratio 1) is fixed;
                    // higher partials are pushed progressively further, so the spacing grows
                    // non-uniformly — the morph from a clean tone into a struck-metal timbre.
                    const float pw = 1.0f + 0.7f * amount;
                    for (int i = 0; i < n; ++i)
                        p[(size_t) i].ratio = std::pow (p[(size_t) i].ratio, pw);
                    break;
                }

                // ── Reserved (next batch) — pass through unchanged until implemented ──
                case SpectralMode::Vocode:
                case SpectralMode::Smear:
                case SpectralMode::RandomAmplitudes:
                case SpectralMode::DataCompress:
                case SpectralMode::SpectralPhaser:
                case SpectralMode::None:
                case SpectralMode::Count:
                default:
                    break;   // identity: partials unchanged
            }

            writeBack (out, p, n);
        }
    };
} // namespace tw
