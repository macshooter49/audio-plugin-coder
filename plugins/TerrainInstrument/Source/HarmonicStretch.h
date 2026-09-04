// ══════════════════════════════════════════════════════════════════════════════════════════════
//  HarmonicStretch.h — fb583. STRETCH: the knob that moves the partials themselves.
// ══════════════════════════════════════════════════════════════════════════════════════════════
//
//  WT POS chooses WHICH of the sixteen authored frames you hear. WARP and FOLD reshape the cycle
//  in the time domain. SPECTRAL reweights the partials that are there. This knob MOVES them.
//
//  THE LAW — the stiff string. A real string resists being bent, so its nth partial does not sit
//  at n·f0 but a little sharp of it, and the error grows with n. That is why a piano is tuned
//  stretched and why a struck bar rings inharmonic:
//
//        ratio(n)  =  n · √(1 + B·n²) / √(1 + B)
//
//  The √(1+B) divisor is not decoration — it PINS THE FUNDAMENTAL. ratio(1) == 1 exactly for every
//  B, so this knob never moves the note's pitch. ratio is strictly increasing in n, so partials
//  fan apart and never re-order. The instrument already speaks this language: Wavetable::buildStiff
//  builds Terra Bell and Terra Bar from the same law, and the MODAL engine's own Stretch knob is
//  described in its tooltip as "pure harmonic left, piano-stiff to bell/gong right".
//
//  🚨 THE STIFFNESS IS NOT A CONSTANT, AND THAT IS THE WHOLE DESIGN.
//     The obvious build — one global B for every frame — was built first, measured, and thrown
//     away. It is also, for the record, what SpectralMode::InharmonicStretch does today with a raw
//     power law (ratio^p, p up to 3.3). A fixed amount of stretch means a frame whose energy
//     already sits high gets that energy pushed clean out of the band, and the frame goes QUIET.
//     Serum HD is the proof: its upper frames carry no fundamental at all, and a fixed stiffness
//     dragged the table's natural collapse three octaves down, from C6 into C2-C3.
//
//     So the knob does not set a stiffness. It sets a DESTINATION. Each frame's spectral edge —
//     its highest partial above -40 dBc — is carried from where it sits toward harmonic 181, and
//     the stiffness that does exactly that is solved for per frame. Because ratio(n)/n is largest
//     at the edge, no partial anywhere in the frame can move further than the edge does, so the
//     bandwidth is BOUNDED BY CONSTRUCTION. A dense frame is moved gently; a sparse bell is moved
//     hard; both arrive at the same musical place. Anti-aliasing is free on top of that: the bake
//     drops any partial past the mip's ceiling, so a stretched partial above Nyquist is never
//     rendered (MEASURED: -164 dBc worst, at C7).
//
//  ONE DIRECTION, ON PURPOSE. Compression (partials squeezed toward the fundamental) was built and
//  MEASURED and then cut: a single-cycle frame is periodic by construction, so compressed partials
//  land on integer bins that are ALREADY OCCUPIED and phasor-sum into each other. The spectrum
//  collapses toward a sine — duller and duller — and the closest WT Pos match came back at
//  1.48-2.52 dB on the bell tables, i.e. indistinguishable from just turning WT Pos down. Half a
//  knob that reproduces another knob is the exact failure this control replaced.
//
//  WHAT IT COSTS. Nothing on the audio thread. This is a spec → spec transform that runs on the
//  message thread inside rebuildMorphIfNeeded, next to SpectralMorph::apply, and the result is
//  published to the voices as a finished table (2.27 ms per bake, double-buffered, atomically
//  swapped). The transform itself is 0.10 ms on the bank's widest table.
//
//  🚨 THE ONE THING IT CANNOT DO. A frame holding a single partial has nothing to stretch: the
//     fundamental is pinned, and there is no second partial to move away from it. Stretch is
//     therefore provably the identity on a pure sine — which is Sine at WT Pos 0, the factory
//     default patch. This is arithmetic, not a bug; Tests/wt_stretch_cert.cpp asserts it as a law
//     rather than hiding it. Measured over all 46 factory tables at three WT Pos settings, 135 of
//     138 cells move and the 3 that do not are exactly the single-partial frames.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#pragma once

#include "Wavetable.h"
#include <cmath>
#include <array>

namespace tw
{
    struct HarmonicStretch
    {
        // ── THE TWO CONSTANTS, AND THE MEASUREMENTS THAT CHOSE THEM ──────────────────────────
        //
        //  kEdgeCeil is where a frame's spectral EDGE is allowed to end up at full knob: harmonic
        //  181, which is the mip ceiling this instrument gives a C3. It is not a taste setting, it
        //  is the fix for a real defect. With a FIXED stiffness (the first build of this file, and
        //  what SpectralMode::InharmonicStretch still does) a frame whose energy already sits high
        //  gets that energy shoved clean out of the band, and the frame goes quiet. MEASURED on
        //  Serum HD, whose upper frames carry NO fundamental at all (h1 amplitude falls 0.88 -> 0.00
        //  across the table), as frame-to-frame level spread over the WT Pos axis:
        //
        //                        C1     C2     C3     C4     C5     C6
        //      neutral          4.0    4.0    4.0    4.0    3.5   42.8   dB
        //      fixed stiffness  8.7   34.2   56.5   75.1   90.4   99.6   dB     <- the defect
        //      this file        4.2    4.2    4.3    4.3    4.6   54.1   dB
        //
        //  The table already thins at C6 on its own; a fixed stiffness dragged that collapse THREE
        //  OCTAVES down into C2-C3, where people actually play. Sweeping WT Pos would have faded
        //  the sound to nothing. Anchoring the destination instead of the stiffness costs 18% of
        //  the distinctiveness (ratio 1.21 -> 0.99) and removes 71 dB of that defect.
        //
        //  kGMax caps how far a SPARSE frame may travel — a five-partial bell has room to spread
        //  its edge 22x before it reaches harmonic 181, and past about 12x the sound stops being a
        //  bell and starts being a cluster. MEASURED (mean of the eight most different factory
        //  tables, amplitude-weighted magnitude-spectrum distance in dB against the CLOSEST of 101
        //  WT Pos settings — i.e. the best imitation the other knob can manage):
        //
        //      kGMax          6      8     12     20
        //      un-imitable  29.65  31.75  33.46  33.46  dB
        //      ratio         0.90   0.95   0.99   0.99
        //      worst step    2.58   4.21   5.96   5.96  dB
        //
        //  🏊‍♂️🦈 THE LIFEGUARD LAW, satisfied and then verified: 20 measures IDENTICAL to 12, because
        //     every frame has already reached kEdgeCeil by then. 12 IS the mechanism's ceiling, so
        //     there is no headroom left unused — and the knob's own 100% is that ceiling, not half
        //     of it. `ratio 0.99` says the sound at full travel sits as far from EVERY WT Pos
        //     setting as the two most different WT Pos settings sit from each other.
        static constexpr double kEdgeCeil = 181.0;
        static constexpr double kGMax     = 12.0;
        // ── AND THE THIRD CONSTANT, WHICH IS THE PRICE OF THE SECOND ────────────────────────
        //  A frame that ALREADY fills the band has a target of 1.0 under the ceiling alone — i.e.
        //  the knob is DEAD on it. That is a third of this bank: every Terra table carries 1024
        //  harmonics. So a frame always travels at least kGMin, ceiling or no ceiling, and that
        //  constant is exactly where the two failures meet. MEASURED, sweeping it:
        //
        //      kGMin                 1.0    3.0    4.0    5.5    7.0
        //      cells that MOVE     103    129    133    134    135   of 138
        //      extra WT Pos level  +0.3   +0.3   +3.6  +14.1  +20.2  dB   (fixed stiffness: +71.1)
        //
        //  These two are the SAME trade-off seen from both ends: a band-filling frame either
        //  stretches and loses some of its top, or keeps its top and does nothing. No value wins
        //  both, so 4.0 is the knee — 96% of the bank moves, and the level unevenness it adds is
        //  3.6 dB against tables that already vary by 4 to 21 dB across their own WT Pos axis.
        static constexpr double kGMin     = 4.0;
        static constexpr double kEdgeFloorDb = 0.01;   // -40 dBc: what counts as the spectral edge

        /** The highest partial carrying real energy — the frame's spectral edge. This is the whole
            reason the control is safe: the destination is measured from the CONTENT, so a dense,
            fundamental-less frame is moved gently and a sparse bell is moved hard. */
        static double edgeOf (const FrameSpec& f) noexcept
        {
            double peak = 0.0, edge = 1.0;
            if (f.numPartials > 0)
            {
                for (int p = 0; p < f.numPartials; ++p) peak = std::max (peak, (double) f.partials[(size_t) p].amp);
                for (int p = 0; p < f.numPartials; ++p)
                    if ((double) f.partials[(size_t) p].amp >= peak * kEdgeFloorDb)
                        edge = std::max (edge, (double) f.partials[(size_t) p].ratio);
            }
            else
            {
                const int nH = std::min (f.numHarmonics, FrameSpec::kMaxHarmonics);
                for (int n = 0; n < nH; ++n) peak = std::max (peak, (double) f.amplitudes[(size_t) n]);
                for (int n = 0; n < nH; ++n)
                    if ((double) f.amplitudes[(size_t) n] >= peak * kEdgeFloorDb) edge = std::max (edge, (double) (n + 1));
            }
            return edge;
        }

        /** Where this frame's edge ends up at FULL knob — its own destination, not a global amount. */
        static double targetGrowth (double edge) noexcept
        {
            double t = std::min (kEdgeCeil / edge, kGMax);
            t = std::max (t, kGMin);                 // a band-filling frame still travels
            t = std::min (t, edge * 0.8);            // a 2-partial frame may not be asked for 12x
            return t;
        }

        /** The stiffness that puts this frame's edge exactly `G` times higher. Inverting
                G = ratio(edge)/edge = sqrt((1 + B·edge²) / (1 + B))
            gives B = (G² − 1) / (edge² − G²). Because ratio(n)/n is increasing in n, G at the edge
            is the LARGEST factor any partial sees — so nothing in the frame can ever move further
            than the edge does. That single fact is what makes the bandwidth bounded by construction. */
        static double stiffness (const FrameSpec& f, float s) noexcept
        {
            if (s <= 0.0f) return 0.0;
            const double edge = edgeOf (f);
            const double tgt  = targetGrowth (edge);
            if (tgt <= 1.0) return 0.0;
            const double G = 1.0 + (double) juce::jlimit (0.0f, 1.0f, s) * (tgt - 1.0);   // the knob travels the WHOLE way
            if (G <= 1.0 || edge <= G + 0.25) return 0.0;
            return (G * G - 1.0) / (edge * edge - G * G);
        }

        /** The stiff-string law. ratio(1) == 1 exactly for every B — the fundamental is pinned. */
        static double ratio (double n, double B) noexcept
        { return (B <= 0.0) ? n : n * std::sqrt (1.0 + B * n * n) / std::sqrt (1.0 + B); }

        /** One frame at knob position s. s <= 0, or a frame with nothing to stretch, returns the
            input untouched. */
        static FrameSpec frame (const FrameSpec& in, float s) noexcept
        {
            const double B = stiffness (in, s);
            if (B <= 0.0) return in;
            constexpr int H = FrameSpec::kMaxHarmonics;

            // ── An ALREADY-INHARMONIC frame (a bell, a piano, a Terra partial cloud) keeps its
            //    partial list: we move each ratio and hand the list back to buildFromSpec, whose
            //    own resolver does the grid snap. One transform, one snapper, no second copy.
            if (in.numPartials > 0)
            {
                FrameSpec out = in;
                int n = 0;
                for (int p = 0; p < in.numPartials; ++p)
                {
                    const FrameSpec::Partial& pt = in.partials[(size_t) p];
                    if (pt.amp == 0.0f || pt.ratio <= 0.0f) continue;
                    const double R = ratio ((double) pt.ratio, B);
                    if (R > (double) H) continue;          // past the ladder — band-limited away
                    out.partials[(size_t) n] = pt;
                    out.partials[(size_t) n].ratio = (float) R;
                    ++n;
                }
                out.numPartials = n;
                rmsMatchPartials (in, out);
                return out;
            }

            // ── NOTHING TO STRETCH. One harmonic is harmonic 1, and harmonic 1 is pinned, so the
            //    transform is the identity — return the frame itself rather than round-tripping it
            //    through a phasor and back, which is only identical to within a few ULPs. This is
            //    the pure sine (and Sine at WT Pos 0 IS the factory default patch), and it is the
            //    one place this control provably cannot do anything. Stated, not hidden.
            if (in.numHarmonics < 2) return in;

            // ── A HARMONIC frame. Deposit each partial at its new, generally FRACTIONAL place on
            //    the ladder with the energy-preserving linear phasor split — the same arithmetic
            //    buildFromSpec uses for an inharmonic frame (Wavetable.h, the `dep` lambda), done
            //    here so the result is a plain integer-harmonic frame and the bake's fast path
            //    still runs. Two partials landing in one bin SUM AS PHASORS, which is physically
            //    what happens and is allowed to cancel.
            FrameSpec out {};
            // fb530's lesson, kept: do NOT value-initialise the full ladder on every frame. ratio()
            // is strictly increasing, so the highest bin any deposit can reach is fixed by the
            // TOP input harmonic — clear that span and nothing more.
            std::array<double, (size_t) H + 2> px, py;
            const int    nIn  = (in.numHarmonics < H) ? in.numHarmonics : H;
            const double rTop = ratio ((double) (nIn > 0 ? nIn : 1), B);
            const int maxOut = (rTop + 1.0 >= (double) H) ? H : ((int) rTop + 1);
            std::fill (px.begin(), px.begin() + (maxOut + 1), 0.0);
            std::fill (py.begin(), py.begin() + (maxOut + 1), 0.0);
            int top = 0;
            for (int n = 1; n <= in.numHarmonics && n <= H; ++n)
            {
                const double a = (double) in.amplitudes[(size_t) (n - 1)];
                if (a == 0.0) continue;
                const double R = ratio ((double) n, B);
                if (R > (double) H) continue;              // past the ladder — band-limited away
                const double ph = (double) in.phases[(size_t) (n - 1)];
                const int    h0 = (int) std::floor (R);
                const double fr = R - (double) h0;
                const double c = std::cos (ph) * a, sn = std::sin (ph) * a;
                // w == 0 is the exact-integer landing (R whole, so the upper bin gets nothing).
                // Skipping it matters: depositing zero would still raise `top`, handing the frame a
                // trailing silent harmonic and quietly inflating numHarmonics.
                auto dep = [&] (int h, double w)
                {
                    if (w > 0.0 && h >= 1 && h <= H)
                    { px[(size_t) h] += w * c; py[(size_t) h] += w * sn; if (h > top) top = h; }
                };
                dep (h0,     1.0 - fr);
                dep (h0 + 1, fr);
            }
            for (int h = 1; h <= top; ++h)
            {
                const double m = std::sqrt (px[(size_t) h] * px[(size_t) h] + py[(size_t) h] * py[(size_t) h]);
                out.amplitudes[(size_t) (h - 1)] = (float) m;
                out.phases[(size_t) (h - 1)]     = (float) ((m > 0.0) ? std::atan2 (py[(size_t) h], px[(size_t) h]) : 0.0);
            }
            out.numHarmonics = top;
            rmsMatchHarmonics (in, out);
            return out;
        }

        /** The whole table, written into caller-owned storage. A WavetableSpec is ~229 KB, and the
            bake already carries one on the stack plus SpectralMorph::apply's return value — so the
            morph path uses THIS and keeps its frame where it was. `out` may not alias `in`. */
        static void applyInto (const WavetableSpec& in, float s, WavetableSpec& out) noexcept
        {
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
                out.frames[(size_t) f] = (s <= 0.0f) ? in.frames[(size_t) f]
                                                     : frame (in.frames[(size_t) f], s);
        }

        /** The whole table. s <= 0 returns the input BY VALUE, untouched — the identity that lets
            rebuildMorphIfNeeded publish nullptr at neutral and hand the voices the bank table.
            (Offline callers and the cert use this; the bake uses applyInto.) */
        static WavetableSpec apply (const WavetableSpec& in, float s) noexcept
        {
            if (s <= 0.0f) return in;
            WavetableSpec out;
            applyInto (in, s, out);
            return out;
        }

    private:
        // STRETCHING REDISTRIBUTES ENERGY; IT MUST NOT REMOVE IT. Partials that fly past the top
        // of the ladder are gone, and phasor sums in a shared bin can cancel — both of which would
        // read as the knob quietly turning the oscillator down. Rescale the survivors so the
        // frame's harmonic power (1/2 sum a^2) is what it was.
        static void rmsMatchHarmonics (const FrameSpec& in, FrameSpec& out) noexcept
        {
            double p0 = 0.0, p1 = 0.0;
            for (int n = 1; n <= in.numHarmonics && n <= FrameSpec::kMaxHarmonics; ++n)
            { const double a = (double) in.amplitudes[(size_t) (n - 1)]; p0 += a * a; }
            for (int h = 1; h <= out.numHarmonics; ++h)
            { const double a = (double) out.amplitudes[(size_t) (h - 1)]; p1 += a * a; }
            if (p1 <= 1.0e-20 || p0 <= 1.0e-20) return;
            const float g = (float) std::sqrt (p0 / p1);
            for (int h = 1; h <= out.numHarmonics; ++h) out.amplitudes[(size_t) (h - 1)] *= g;
        }

        static void rmsMatchPartials (const FrameSpec& in, FrameSpec& out) noexcept
        {
            double p0 = 0.0, p1 = 0.0;
            for (int p = 0; p < in.numPartials;  ++p) { const double a = (double) in.partials[(size_t) p].amp;  p0 += a * a; }
            for (int p = 0; p < out.numPartials; ++p) { const double a = (double) out.partials[(size_t) p].amp; p1 += a * a; }
            if (p1 <= 1.0e-20 || p0 <= 1.0e-20) return;
            const float g = (float) std::sqrt (p0 / p1);
            for (int p = 0; p < out.numPartials; ++p) out.partials[(size_t) p].amp *= g;
        }
    };
}
