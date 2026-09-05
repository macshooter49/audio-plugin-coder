#pragma once
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  HarmTableSource.h — fb588. RESOLVING A WAVETABLE INTO THE ADDITIVE BANK'S PARTIALS.
// ══════════════════════════════════════════════════════════════════════════════════════════════
//
//  Max: "I want you to implement our wave table presets and the same menu for harmonics mode. I
//  want to be able to actually have additive synthesis with the wave tables."
//
//  The HARMONIC engine builds sound by stacking up to 512 sine partials, and until now the recipe
//  for that stack could only come from six procedural families. This turns any wavetable — factory
//  OR imported — into such a recipe, so the whole sculpt row (Keel · Splay · Cull · Tide · Terrace
//  · Clang, plus Lean, Shine, Wilt, Grit, Braid, Fan) can work on the tables you already have.
//
//  🚨 PHASE IS CARRIED, AND THAT WAS MEASURED, NOT ASSUMED. The first plan was magnitude only —
//     the additive bank never used phase as an input, and copying it back rebuilds the table's
//     exact waveform (measured: waveform distance 0.010 against the real table), which looked like
//     a duplicate of the wavetable engine. Three things killed that plan:
//       · makeSpectralDriftSpec writes ONE CONSTANT amplitude and morphs only PHASE across all 16
//         frames — under magnitude-only its WT Pos knob would have been completely dead.
//       · A memoryless nonlinearity is a function of the waveform, not the spectrum, so the same
//         table would fold into a different sound here than on the wavetable engine. That reads
//         as a bug: "why doesn't my table sound like my table?"
//       · Prior art is one-sided — Serum stores and edits per-partial phase, Vital's default
//         import style is literally PhaseStyle::kNone, Phase Plant gives every partial a phase
//         handle, Harmor drives phase from the source.
//     What stays random is the GLOBAL start phase, demoted from per-partial to one rotation per
//     unison sibling (see HarmonicEngine::noteOn) — which is what keeps the 16-voice headroom.
//
//  ⚠️ AND THE PARTIAL-LIST FRAMES ARE NOT SKIPPED. Bells, pianos and the Terra clouds store
//     arbitrary ratios rather than integer harmonics; they are snapped onto the harmonic grid with
//     the same energy-preserving phasor split Wavetable::buildFromSpec uses, because those are
//     exactly the tables an additive engine is most interesting on.
// ══════════════════════════════════════════════════════════════════════════════════════════════

#include "Wavetable.h"
#include <cmath>
#include <algorithm>

namespace tw
{
    struct HarmTableSource
    {
        static constexpr int kMaxN = 512;      // harm::kMaxPartials — the additive bank's width

        /** Resolve ONE frame onto the integer harmonic grid. Handles both frame kinds. */
        static void frameToGrid (const FrameSpec& f, float* amp, float* phase, int maxN, int& outN) noexcept
        {
            for (int j = 0; j < maxN; ++j) { amp[j] = 0.0f; phase[j] = 0.0f; }
            outN = 0;
            if (f.numPartials > 0)
            {
                // arbitrary ratios → the grid, energy-preserving linear phasor split
                for (int p = 0; p < f.numPartials; ++p)
                {
                    const FrameSpec::Partial& pt = f.partials[(size_t) p];
                    if (pt.amp == 0.0f || pt.ratio <= 0.0f) continue;
                    const double R = (double) pt.ratio;
                    if (R > (double) maxN) continue;
                    const int    h0 = (int) std::floor (R);
                    const double fr = R - (double) h0;
                    const double c = std::cos ((double) pt.phase) * pt.amp;
                    const double s = std::sin ((double) pt.phase) * pt.amp;
                    // accumulate as phasors in the amp/phase slots themselves
                    auto dep = [&] (int h, double w)
                    {
                        if (w <= 0.0 || h < 1 || h > maxN) return;
                        const int j = h - 1;
                        const double re = (double) amp[j] * std::cos ((double) phase[j]) + w * c;
                        const double im = (double) amp[j] * std::sin ((double) phase[j]) + w * s;
                        amp[j]   = (float) std::sqrt (re * re + im * im);
                        phase[j] = (float) ((amp[j] > 0.0f) ? std::atan2 (im, re) : 0.0);
                        if (h > outN) outN = h;
                    };
                    dep (h0, 1.0 - fr);
                    dep (h0 + 1, fr);
                }
            }
            else
            {
                const int n = std::min (f.numHarmonics, maxN);
                for (int j = 0; j < n; ++j)
                {
                    // 🚨 SIGNED AMPLITUDES. WavetableSpec stores amplitude as a SIGNED coefficient —
                    //    Square, Pulse and the other analytic tables write negative values, and a
                    //    negative amplitude is just a positive one half a turn out of phase. The
                    //    additive bank has no sign to give a partial (it renders amp * sin), so a
                    //    negative amp lands as silence. Measured before this line existed: on Square,
                    //    EVERY negative harmonic rendered at ~0.005 while every positive one matched
                    //    at ratio 1.00 — half the spectrum gone, 17.9 dB off the table.
                    //    The partial-list branch above never had this bug because it accumulates
                    //    phasors, so its magnitude is a sqrt and its sign lives in the atan2.
                    const float a = f.amplitudes[(size_t) j];
                    amp[j]   = std::fabs (a);
                    phase[j] = f.phases[(size_t) j] + (a < 0.0f ? 3.14159265358979f : 0.0f);
                }
                outN = n;
            }
        }

        /** ── THE 16-FRAME GRID ────────────────────────────────────────────────────────────────
            Every frame of a table, already converted onto the harmonic grid.

            🚨 WHY THIS IS SPLIT IN TWO. HUE picks the frame, and HUE is modulatable, so the frame
               position moves at block rate ON THE AUDIO THREAD. But frameToGrid on a partial-list
               table costs up to 512 x (cos, sin, sqrt, atan2) per frame — nowhere near affordable
               per block, per oscillator. So the expensive half (`bake`) runs on the message thread
               once per SOURCE change, and the audio thread runs only `blend`, which is a lerp over
               two already-converted frames. That is what lets HUE be fully modulated rather than
               frozen at its parameter value. */
        struct Grid
        {
            float amp   [WavetableSpec::kNumFrames][kMaxN];
            float phase [WavetableSpec::kNumFrames][kMaxN];
            int   n     [WavetableSpec::kNumFrames] = {};
            float sig   = 0.0f;     // moves whenever the SOURCE changes
        };

        /** MESSAGE THREAD. Convert every frame of the table onto the harmonic grid. */
        static void bake (const WavetableSpec& spec, Grid& g) noexcept
        {
            double acc = 0.0;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                frameToGrid (spec.frames[(size_t) f], g.amp[f], g.phase[f], kMaxN, g.n[f]);
                acc += (double) g.n[f] * (1.0 + 0.011 * f) + (double) signature (g.amp[f], g.phase[f], g.n[f]);
            }
            g.sig = (float) acc;
        }

        /** AUDIO THREAD. Blend the two frames the wavetable engine's WT Pos would blend, so the
            additive bank scans the table the same way the wavetable oscillator does.
            ⚠️ Amplitude interpolates linearly; PHASE interpolates the SHORT WAY round the circle,
               because a naive lerp from 0.1 to 6.2 radians would sweep backwards through the whole
               turn and audibly smear a frame that is barely moving. */
        static int blend (const Grid& g, float pos01, float* amp, float* phase, int maxN) noexcept
        {
            const int F  = WavetableSpec::kNumFrames;
            const double fp = (double) std::min (1.0f, std::max (0.0f, pos01)) * (F - 1);
            const int    f0 = (int) std::floor (fp);
            const int    f1 = std::min (f0 + 1, F - 1);
            const float  t  = (float) (fp - (double) f0);

            const int n = std::min (maxN, std::max (g.n[f0], g.n[f1]));
            for (int j = 0; j < n; ++j)
            {
                const float a0 = g.amp[f0][j],   a1 = g.amp[f1][j];
                const float p0 = g.phase[f0][j], p1 = g.phase[f1][j];
                amp[j] = a0 + (a1 - a0) * t;
                float d = p1 - p0;
                while (d >  3.14159265358979f) d -= 6.28318530717959f;   // the short way round
                while (d < -3.14159265358979f) d += 6.28318530717959f;
                phase[j] = p0 + d * t;
            }
            return n;
        }

        /** Resolve straight from a spec — bakes and blends in one go. Message thread / offline
            only (it bakes all 16 frames); the plugin uses bake + blend so HUE can be modulated. */
        static int resolve (const WavetableSpec& spec, float pos01,
                            float* amp, float* phase, int maxN) noexcept
        {
            static thread_local Grid g;
            bake (spec, g);
            return blend (g, pos01, amp, phase, maxN);
        }

        /** A scalar that moves whenever the resolved content moves — the additive bank only
            rebuilds when its parameters change, so without this a new table would never take. */
        static float signature (const float* amp, const float* phase, int n) noexcept
        {
            double acc = (double) n * 0.7548776662;
            for (int j = 0; j < n; ++j)
                acc += (double) amp[j] * (1.0 + 0.0137 * j) + (double) phase[j] * (0.0031 * (j + 3));
            return (float) acc;
        }
    };
}
