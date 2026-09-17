#pragma once
// ═════════════════════════════════════════════════════════════════════════════════════════════════
//  tp22 — THE FILTER TABLE. A wavetable's harmonic content, turned into a filter's response curve.
//
//  Max: "use our 500 waves as filter types... open up our filter and select a wavetable... we should
//  hear something different." Kilohearts' Filter Table is the reference (NAMM 2025): the harmonic
//  content of ONE FRAME becomes a filter shape, scanning the table morphs that shape, CUTOFF places
//  harmonic 24 at the cutoff frequency, and RESONANCE heightens or flattens the peaks and troughs.
//  The anchoring and the resonance semantics here are deliberately theirs — the implementation is
//  not (they convolve a reconstructed kernel and offer four phase modes; that buys latency we cannot
//  pay, see below).
//
//  ── WHY A BAND CURVE AND NOT A CONVOLUTION KERNEL ───────────────────────────────────────────────
//  The faithful implementation builds a filter kernel per frame and convolves. Minimum-phase
//  reconstruction makes that latency-free, and it is the better-sounding device. It is NOT what this
//  file does, for one architectural reason: CUTOFF. A kernel's response is fixed at bake time, so
//  moving the cutoff means rebuilding it — and cutoff is the single most modulated parameter in the
//  instrument (every filter envelope, every LFO, key tracking, EROSION drift). A curve of BANDS
//  re-centres for free: the band ratios are constants and the cutoff just scales where they sit, so
//  the whole response slides under an envelope at no cost beyond the coefficient recompute every
//  other filter type already pays (and which fb441 already gates on a 1-cent change).
//  The trade is resolution: kBands bands across ~9 octaves instead of a full spectrum. That is the
//  honest limit of this design, written down rather than discovered later.
//
//  ── WHAT THIS FILE IS ───────────────────────────────────────────────────────────────────────────
//  PURE. No JUCE, no state, no allocation — so it is provable offline (Source/FilterTableSource_test.cpp)
//  and the same code runs in the plugin. It takes the harmonic grid HarmTableSource::bake() ALREADY
//  produces for the Harmonics engine (message thread, throttled, double-buffered) and collapses it
//  into per-frame band gains. Nothing new is analysed and the audio thread never sees a wavetable.
// ═════════════════════════════════════════════════════════════════════════════════════════════════
#include "HarmTableSource.h"
#include <cmath>

namespace tw
{

struct FilterTableSource
{
    /** Bands across the curve. 32 over ~9 octaves is ~3.5 per octave — enough for a table's formant
     *  structure to read as a shape, cheap enough to run per voice. The cost is linear in this. */
    static constexpr int kBands = 32;

    /** CUTOFF places THIS harmonic at the cutoff frequency (Kilohearts' choice, and a good one: the
     *  curve then spreads ~4.6 octaves below the knob and ~4.4 above, so the knob sits inside the
     *  shape instead of at its bottom edge). */
    static constexpr int kAnchorHarm = 24;

    static constexpr int kFrames = WavetableSpec::kNumFrames;
    static constexpr int kMaxN   = HarmTableSource::kMaxN;   // 512 harmonics per frame

    /** tp27 — WHAT RESONANCE 1.0 MEANS, in dB of peak deviation. The curve below is stored as a
     *  UNIT shape (peak magnitude exactly 1), so this is the only place the depth is decided and the
     *  drawing and the DSP cannot disagree about it. 20 dB is dramatic and still clear of the
     *  per-band clamp (kTblMaxDb = 24 in TerrainFilters), which matters: the previous version
     *  multiplied a raw 30-46 dB curve by 2.5 and pinned 70 % of its bands ON that clamp at full
     *  resonance, so the extremes of every table were the same saturated shape. */
    static constexpr float kDepthDb = 20.0f;

    /** The smallest peak deviation (dB) the normaliser will divide by — see bake(). A table whose
     *  spectrum is the library average has only noise left after the reference is removed, and
     *  dividing by noise would turn it into a full-depth random comb. */
    static constexpr float kMinPeakDb = 3.0f;

    /** tp27 — THE REFERENCE SPECTRUM: the average band curve of the whole factory library
     *  (454 tables x 16 frames, scripts/gen_filter_ref.py).
     *
     *  🚨 THIS IS THE FIX FOR "ALL 500 SOUND THE SAME". Max: "these do not sound different
     *  whatsoever ... as a wavetable you're supposed to sound different."  He was right and the
     *  numbers say why: EVERY wavetable's spectrum falls with harmonic number, so every curve was
     *  dominated by the same +25 dB -> -12 dB slope. Measured across the library, any two tables'
     *  curves correlated 0.735 and 58 % of each curve WAS that shared slope — what you heard was a
     *  fixed lowpass, identical on all 500, with the table's own voice buried underneath it.
     *
     *  A falling slope is what a CUTOFF is for. The table's job is the structure ON TOP of it: the
     *  formants, the notches, the resonant shelves that make one wavetable sound unlike another.
     *  Subtracting this reference leaves exactly that. Same measurement after the change: mean
     *  correlation 0.006 — uncorrelated — and distinctiveness doubled.
     *
     *  Subtracting the LIBRARY AVERAGE rather than each curve's own straight-line fit is deliberate:
     *  the fit also flattens a genuinely dark or genuinely bright table into the same nothing
     *  (measured 0.268 correlation but a third less distinctiveness). Against a fixed reference, a
     *  dark table still reads dark — it is dark RELATIVE TO A TYPICAL TABLE, which is the useful
     *  comparison. */
    static const float* referenceDb() noexcept
    {
        static const float r[kBands] = {
            14.996f, 14.996f, 14.996f, 15.134f,  6.308f,  9.305f,  6.609f,  8.108f,
             6.580f,  5.838f,  5.308f,  4.124f,  2.869f,  2.205f,  1.068f,  0.122f,
            -0.543f, -2.000f, -2.954f, -4.013f, -5.258f, -6.071f, -7.074f, -7.838f,
            -8.467f, -9.234f, -9.819f,-10.372f,-10.680f,-11.164f,-11.430f,-11.652f };
        return r;
    }

    /** The curve, as a UNIT shape: the table's deviation from referenceDb(), centred on zero and
     *  scaled so its peak magnitude over the whole table is exactly 1. The read site multiplies by
     *  kDepthDb * resonance, so the shape is the content and the depth is one constant. */
    struct Curve
    {
        float db  [kFrames][kBands] {};   // per frame, per band: dB above/below that frame's mean
        float sig = 0.0f;                 // moves whenever the SOURCE table changes
    };

    /** Band k's centre as a MULTIPLE OF CUTOFF. Constant for the life of the program — the cutoff
     *  scales these, which is the whole reason this design can be modulated. Log-spaced from
     *  harmonic 1 to harmonic kMaxN, with harmonic kAnchorHarm landing exactly on 1.0.
     *  Built ONCE into a static table: the coefficient path asks for these every time the cutoff
     *  moves, and that is not a place to run 32 exp() calls. */
    static const float* bandRatios() noexcept
    {
        static const struct T {
            float r[kBands];
            T() { for (int k = 0; k < kBands; ++k)
                  { const float t = (kBands > 1) ? (float) k / (float) (kBands - 1) : 0.0f;
                    r[k] = std::exp (t * std::log ((float) kMaxN)) / (float) kAnchorHarm; } }
        } t;
        return t.r;
    }
    static float bandRatio (int k) noexcept
    { return bandRatios()[(k < 0) ? 0 : (k >= kBands ? kBands - 1 : k)]; }

    /** The harmonic index at band k's centre (>= 1). Used by the bake to know what to average. */
    static int bandHarm (int k) noexcept
    {
        const int h = (int) std::lround ((double) bandRatio (k) * (double) kAnchorHarm);
        return h < 1 ? 1 : (h > kMaxN ? kMaxN : h);
    }

    /** MESSAGE THREAD. Collapse a baked harmonic grid into the band curve.
     *
     *  Each band takes the RMS of the harmonics inside it, because a band is a bundle of partials and
     *  RMS is what their combined energy actually is — a peak pick would make one loud partial speak
     *  for a whole octave, a mean would let a dense band of quiet ones outvote a sparse loud one.
     *  The result is converted to dB and the frame's MEAN dB is subtracted, so every frame's curve is
     *  centred on zero: scanning the table then changes the SHAPE without the level jumping, which is
     *  what makes a modulated frame usable as a sound rather than as a volume pedal. */
    static void bake (const HarmTableSource::Grid& g, Curve& c) noexcept
    {
        constexpr float kFloorDb = -48.0f;    // a band with nothing in it is a trough, not a hole
        double acc = 0.0;
        for (int f = 0; f < kFrames; ++f)
        {
            const int n = g.n[f] > kMaxN ? kMaxN : (g.n[f] < 0 ? 0 : g.n[f]);
            float mean = 0.0f;
            for (int k = 0; k < kBands; ++k)
            {
                // this band spans the harmonics between its neighbours' midpoints (log domain)
                const int hc = bandHarm (k);
                const int lo = (k == 0)          ? 1     : (int) std::lround (std::sqrt ((double) bandHarm (k - 1) * (double) hc));
                const int hi = (k == kBands - 1) ? kMaxN : (int) std::lround (std::sqrt ((double) hc * (double) bandHarm (k + 1)));
                double e = 0.0; int cnt = 0;
                for (int h = lo; h <= hi && h <= n; ++h)
                { const float a = g.amp[f][h - 1]; e += (double) a * a; ++cnt; }
                const float rms = (cnt > 0) ? (float) std::sqrt (e / (double) cnt) : 0.0f;
                float db = (rms > 1.0e-6f) ? 20.0f * std::log10 (rms) : kFloorDb;
                if (db < kFloorDb) db = kFloorDb;
                c.db[f][k] = db;
                mean += db;
            }
            mean /= (float) kBands;
            const float* ref = referenceDb();
            for (int k = 0; k < kBands; ++k)
            {
                // centre it, then take out what EVERY wavetable has in common — see referenceDb()
                c.db[f][k] = (c.db[f][k] - mean) - ref[k];
                acc += (double) c.db[f][k] * (1.0 + 0.001 * k);
            }
            float m2 = 0.0f;
            for (int k = 0; k < kBands; ++k) m2 += c.db[f][k];
            m2 /= (float) kBands;
            for (int k = 0; k < kBands; ++k) c.db[f][k] -= m2;   // re-centre: the reference has its own mean
        }
        // NORMALISE THE WHOLE TABLE to a unit peak, so resonance means the same depth on every table
        // and no table lives at the clamp. Across the table, not per frame, so scanning still changes
        // how STRONG the shaping is and not only its shape.
        float peak = 0.0f;
        for (int f = 0; f < kFrames; ++f)
            for (int k = 0; k < kBands; ++k)
                peak = std::max (peak, std::fabs (c.db[f][k]));
        // 🚨 THE DIVISOR HAS A FLOOR, and it is load-bearing. Normalising by the peak alone means a
        //    table that sits ON the reference — one whose spectrum IS the library average — has only
        //    rounding noise left, and dividing by that noise amplifies it to FULL DEPTH. Such a table
        //    would come out as a full-strength random comb. Measured: a table constructed to equal
        //    referenceDb() baked to a curve of +/-1.0000 before this floor existed.
        //    With the floor, a typical table (peak ~14 dB after subtraction) still normalises to
        //    unit, an extreme one likewise, and a table that really has nothing to say stays quiet
        //    instead of shouting noise.
        const float denom = std::max (peak, kMinPeakDb);
        if (denom > 0.0f)
        {
            const float inv = 1.0f / denom;
            for (int f = 0; f < kFrames; ++f)
                for (int k = 0; k < kBands; ++k) c.db[f][k] *= inv;
        }
        c.sig = (float) acc;
    }

    /** AUDIO THREAD. The two frames the wavetable engine would blend, lerped, then scaled by
     *  RESONANCE. res 0 = flat (the filter does nothing at all, which is the right answer for a
     *  resonance of zero); res 1 = the table's own shape; above that the peaks and troughs are
     *  exaggerated, which is Kilohearts' "prominence" and the reason this control is not a Q.
     *  `pos01` scans the table exactly as WT Pos does, so FRAME reads the same way everywhere. */
    static void blend (const Curve& c, float pos01, float resScale, float* outDb) noexcept
    {
        const float p  = (pos01 < 0.0f) ? 0.0f : (pos01 > 1.0f ? 1.0f : pos01);
        const float fp = p * (float) (kFrames - 1);
        const int   f0 = (int) fp;
        const int   f1 = (f0 + 1 < kFrames) ? f0 + 1 : f0;
        const float t  = fp - (float) f0;
        for (int k = 0; k < kBands; ++k)
            outDb[k] = resScale * (c.db[f0][k] + (c.db[f1][k] - c.db[f0][k]) * t);
    }
};

} // namespace tw
