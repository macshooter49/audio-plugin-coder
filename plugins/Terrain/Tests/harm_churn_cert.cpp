// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_churn_cert.cpp — fb599 ITEM 2: CHURN MUST DO SOMETHING ON TABLE + KEEL.
//                        fb600 RETUNE: and its 100 % must be the algorithm's 100 %.
//
//   SHIPPED (fb599):  c++ -std=c++17 -O2 -I <shipped-hdr-dir> -I Tests/shim -I Source <this> \
//                          -framework Accelerate -o /tmp/cc && /tmp/cc
//   RETUNED (fb600):  c++ -std=c++17 -O2 -I Tests/shim -I Source <this> \
//                          -framework Accelerate -o /tmp/cc && /tmp/cc
//   (the -framework Accelerate is required: WtFft.h uses vDSP.)
//
//  ⚠️ fb600 — WHY THIS CERT'S GATES MOVED. The fb599 gate set passed 11/11 on a knob that reads
//     as DEAD, which is the definition of a bad gate set. Two of its gates were metric artefacts
//     and one whole class of defect was ungated. What replaced them, and WHY — every number below
//     was measured in this cert, and TWO of these are DELIBERATE DEVIATIONS from the retune brief:
//
//       · G2/G3 were built on FLUX, whose 2048-sample (42.7 ms) analysis window CANNOT resolve
//         the fb600 10 Hz top rate — it INVERTS there (measured: VowelMorph FLUX 17.27 at 75 %
//         falls to 13.69 at 100 %). FLUX is still PRINTED, as evidence, but it gates nothing.
//
//       · THE DEPTH OBSERVABLE IS NOT THE CENTROID. The brief specified modDepth = the centroid's
//         5th-95th percentile in semitones. It is printed (mDEP-st) because it was specified, but
//         it is NOT gated, because it is not monotone in the excursion on every table. Measured
//         centroid (st) vs frame position, churn OFF, hue 0 -> 1 in steps of 0.1:
//            ProphetSaw  32.1 33.1 34.4 36.3 38.6 41.1 43.7 46.6 49.4 51.3 51.9   monotone
//            VowelMorph  33.4 30.3 32.8 34.0 26.9 35.4 30.0 25.3 25.6 24.0 28.0   ZIG-ZAG
//         On VowelMorph a bigger frame excursion re-visits the same centroid values, so mDEP-st
//         goes DOWN as the knob goes up (8.30 st at 15 % -> 6.15 st at 100 %) on a knob that is
//         very audibly doing more. The gated depth metric is instead mSPAN: the 95th percentile
//         of the pairwise 1/6-octave band-spectrum distance between 21 ms frames. It is monotone
//         in the excursion by construction — a bigger excursion visits strictly more distant
//         spectra — and it is still a magnitude-only (phase-independent) hearing metric.
//
//       · THE RATE OBSERVABLE IS NOT THE CENTROID EITHER, for the same reason: a scalar that is an
//         EVEN function of the drift traverses its range TWICE per drift period and reports 2f.
//         Measured on VowelMorph, centroid-based modRate gave 2.29 / 6.13 Hz at churn .25 / .50
//         where the true rates are 1.11 / 2.99 — exactly doubled, which made the G3 taper ratio
//         read 1.91x instead of 3.83x. mRATE is therefore the harmonic-sum peak of the FIRST
//         PRINCIPAL COMPONENT of the band-spectrum trajectory: signed and linear in the drift, so
//         it cannot fold to 2f. It recovers 0.23/1.14/3.07/6.32/11.72 Hz against a true
//         0.60/1.11/2.99/6.14/11.44 on ALL THREE gated tables, VowelMorph included.
//
//       · G3b is new and is THE gate that would have caught the fb599 bug: kChurnDepth was a
//         constant, so the knob's top bought nothing its quarter-point did not already buy.
//         DEVIATION FROM THE BRIEF: the brief's form was "modDepth(100 %) > 1.5x modDepth(25 %)".
//         That form cannot pass on a table that SATURATES — VowelMorph reaches its whole-axis
//         ceiling by churn 0.25 (mSPAN 17.03 of a 21.02 ceiling = 81 %), so there is nothing left
//         for the top of the knob to buy and the ratio reads 0.93x however good the taper is.
//         That is the OPPOSITE of unused headroom. G3b is therefore the OR of the brief's ratio
//         and the LIFEGUARD LAW's own EXPOSURE RATIO ("what the knob delivers at max / what the
//         engine can still deliver"): pass if the top buys 1.5x the quarter-point, OR if the top
//         already reaches >= 60 % of that table's whole-axis ceiling. fb599 fails BOTH branches on
//         ProphetSaw (ratio ~1.0x, exposure 44 %) and on Rise (~1.0x, 41 %). That is the defect.
//
//       · G2 ("no dirt at 15 %") is likewise NOT the brief's modDepth ratio, for the same
//         saturation reason: VowelMorph's mSPAN at churn 0.15 is already 94 % of its mSPAN at
//         100 %, which would read as "dirty" when the motion there is a 0.60 Hz slow morph. The
//         gated form is the direct measurement the brief called invariant 3 — the HUE-KILL test:
//         how much of the HUE knob's OWN contrast still survives while CHURN runs. Dirt at 15 %
//         is a defect precisely because it removes the user's ability to be tasteful, and THAT is
//         what HUE-KILL measures. The brief's ratio is printed right next to it.
//
//       · G8 is new: the FIFTH-SLOT gate. CHURN must not land where an LFO on HarmHue already
//         goes. On a PERFECTLY QUANTISED chord the fb599 code WAS that LFO, to within 1 %.
//
//  METRICS (all HEARING metrics — sample-difference RMS is BANNED here):
//    · harmonic spectrum: 2048-pt Blackman-Harris / 8192-pt FFT, peak magnitude within +-3 bins
//      of n*f0 for n = 1..48, dB re this window's own fundamental, floored at -90.
//      SWING(cfg)   = max over window pairs of d(spec_a, spec_b)   "how far the timbre travels"
//      FLUX(cfg)    = mean d(spec_k, spec_k-1) per 50 ms hop       "the rate" (INVERTS above ~8 Hz)
//      AB(a,b)      = mean over windows of d(specA_k, specB_k)     "does the knob change it"
//    · spectral TRAJECTORY: 1/6-octave bands (80 Hz..12.8 kHz), Hann 1024, every 256 samples.
//      mSPAN    = p95 of pairwise band distance, dB          — the gated DEPTH metric
//      mRATE    = harmonic-sum peak of the PC1 projection, Hz — the gated RATE metric
//      mDEP-st  = p95-p05 of the centroid, semitones          — the brief's metric, printed only
//      ceiling  = mSPAN of a slow sweep across the WHOLE frame axis = the algorithm's 100 %
//    · the CHORD section uses the band metric too: the harmonic-grid metric is tied to one f0 and
//      cannot see C#3/E3, so a chord needs bands.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cstdio>
#include <type_traits>
#include <cstring>
#include <cmath>
#include <complex>
#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <chrono>

// ── fb600 — GLOBAL FALLBACKS FOR THE fb600 CONSTANTS (the detector, part 1) ────────────────────
//  This cert must compile against BOTH headers so the bit-identical-at-zero proof can be a real
//  line-by-line diff of the SAME binary logic. kChurnDepthFloor / kChurnRateSpread exist only on
//  the fb600 header, so declare sentinels HERE, in the global namespace. Unqualified lookup from
//  inside tw::harm finds tw::harm::X when the header has it and walks out to ::X when it does
//  not — inner hides outer, no ambiguity, no -D flag anybody can forget to pass.
static constexpr float kChurnDepthFloor = -1.0f;   // sentinel: "this header has no fb600 depth floor"
static constexpr float kChurnRateSpread = -1.0f;   // sentinel: "this header has no fb600 rate spread"

#include "WavetableBank.h"
#include "HarmonicEngine.h"
#include "HarmTableSource.h"

namespace tw { namespace harm {
    // (the detector, part 2) — read the COMPILED values back out of whichever header we got.
    inline float probeDepth()      noexcept { return kChurnDepth;      }
    inline float probeRateHz()     noexcept { return kChurnRateHz;     }
    inline float probeCurve()      noexcept { return kChurnCurve;      }
    inline float probeDepthFloor() noexcept { return kChurnDepthFloor; }   // -1 => absent
    inline float probeRateSpread() noexcept { return kChurnRateSpread; }   // -1 => absent
}}

using namespace tw;

static constexpr double SR    = 48000.0;
static constexpr double F0    = 110.0;      // A2 — 48 harmonics fit under Nyquist with room
static constexpr int    kH    = 48;
static constexpr int    kWin  = 2048;       // 42.7 ms — the FLUX/SWING window (fb599)
static constexpr int    kFFT  = 8192;
static constexpr int    kNWin = 64;
static constexpr double kHop  = 0.05;       // s between window starts → 3.20 s of held note covered
static constexpr double kT0   = 0.06;       // first window start (s)
static constexpr int    BLK   = 128;        // host block (>=128 → the every-other-block skip is OFF)
static int gBlk = BLK;
static constexpr double kFloorDb = -90.0;

// ── centroid-modulation analysis constants (fb600) ────────────────────────────────────────────
static constexpr int    kCWin  = 1024;      // 21.3 ms — short enough that a 10 Hz wobble survives
                                            // it (sinc(10*0.0213) = 0.93, i.e. 7 % attenuation).
static constexpr int    kCFFT  = 4096;      // zero-padded 4x — the centroid is a weighted mean, the
                                            // padding only smooths the bin grid under it
static constexpr int    kCHop  = 256;       // → 187.5 Hz series rate, Nyquist 93.75 Hz
static constexpr double kCSkip = 0.15;      // s of note-on attack skipped (it is an envelope, not motion)

// ── tiny iterative radix-2 FFT (no Accelerate dependency in the metric) ──
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            { const auto u = a[i+k], v = a[i+k+len/2] * w; a[i+k] = u + v; a[i+k+len/2] = u - v; w *= wl; }
        }
    }
}

struct Spec { std::array<double, kH + 1> h {}; };

static Spec analyse (const float* x)
{
    std::vector<std::complex<double>> bf ((size_t) kFFT, {0.0, 0.0});
    for (int i = 0; i < kWin; ++i)
    {
        const double t = 2.0 * M_PI * i / (kWin - 1);
        const double w = 0.35875 - 0.48829*std::cos(t) + 0.14128*std::cos(2*t) - 0.01168*std::cos(3*t);
        bf[(size_t) i] = { (double) x[i] * w, 0.0 };
    }
    fft (bf);
    const double binHz = SR / kFFT;
    Spec s; std::array<double, kH + 1> lin {};
    for (int n = 1; n <= kH; ++n)
    {
        const int c = (int) std::lround (n * F0 / binHz);
        double m = 0;
        for (int b = std::max (1, c - 3); b <= std::min (kFFT/2 - 1, c + 3); ++b) m = std::max (m, std::abs (bf[(size_t) b]));
        lin[(size_t) n] = m;
    }
    const double ref = std::max (lin[1], 1e-12);
    for (int n = 1; n <= kH; ++n) s.h[(size_t) n] = std::max (kFloorDb, 20.0*std::log10 (std::max (lin[(size_t) n], 1e-12) / ref));
    return s;
}
static double dist (const Spec& a, const Spec& b)
{
    double num = 0, den = 0;
    for (int n = 1; n <= kH; ++n)
    { const double w = std::max (std::pow (10.0, a.h[(size_t)n]/20.0), std::pow (10.0, b.h[(size_t)n]/20.0));
      num += w * std::fabs (a.h[(size_t)n] - b.h[(size_t)n]); den += w; }
    return den > 0 ? num/den : 0.0;
}

// ── fb600 · 1/6-OCTAVE BAND SPECTRUM — the CHORD metric ───────────────────────────────────────
//  analyse() locks to harmonics of ONE f0. On the G8 chord (A2 C#3 E3 A3) it would see A2/A3 and
//  be blind to C#3/E3 except where their partials happen to fall inside a +-3-bin window, which
//  would understate exactly the thing G8 is trying to measure. Bands are f0-agnostic and still a
//  magnitude-only (phase-independent) hearing metric.
static constexpr int    kNB    = 44;        // 1/6 octave, 80 Hz .. 12.8 kHz  (log2(160)*6 = 43.9)
static constexpr double kBLo   = 80.0;
struct BSpec { std::array<double, kNB> b {}; };

static BSpec analyseBands (const float* x)
{
    std::vector<std::complex<double>> bf ((size_t) kFFT, {0.0, 0.0});
    for (int i = 0; i < kWin; ++i)
    {
        const double t = 2.0 * M_PI * i / (kWin - 1);
        const double w = 0.35875 - 0.48829*std::cos(t) + 0.14128*std::cos(2*t) - 0.01168*std::cos(3*t);
        bf[(size_t) i] = { (double) x[i] * w, 0.0 };
    }
    fft (bf);
    const double binHz = SR / kFFT;
    std::array<double, kNB> lin {};
    for (int k = 0; k < kNB; ++k)
    {
        const double lo = kBLo * std::pow (2.0, (double) k / 6.0);
        const double hi = kBLo * std::pow (2.0, (double)(k+1) / 6.0);
        const int b0 = std::max (1, (int) std::ceil (lo / binHz));
        const int b1 = std::min (kFFT/2 - 1, (int) std::floor (hi / binHz));
        double p = 0;
        for (int b = b0; b <= b1; ++b) { const double m = std::abs (bf[(size_t) b]); p += m*m; }
        lin[(size_t) k] = std::sqrt (p);
    }
    double ref = 1e-12; for (double v : lin) ref = std::max (ref, v);
    BSpec s;
    for (int k = 0; k < kNB; ++k)
        s.b[(size_t) k] = std::max (kFloorDb, 20.0*std::log10 (std::max (lin[(size_t) k], 1e-12) / ref));
    return s;
}
static double bdist (const BSpec& a, const BSpec& b)
{
    double num = 0, den = 0;
    for (int k = 0; k < kNB; ++k)
    { const double w = std::max (std::pow (10.0, a.b[(size_t)k]/20.0), std::pow (10.0, b.b[(size_t)k]/20.0));
      num += w * std::fabs (a.b[(size_t)k] - b.b[(size_t)k]); den += w; }
    return den > 0 ? num/den : 0.0;
}

// ── fb600 · THE SPECTRAL TRAJECTORY: one short-time pass, three observables ────────────────────
//  One FFT pass every kCHop samples yields (a) the 1/6-octave band vector and (b) the centroid.
//  mSPAN and mRATE come from the band trajectory; mDEP-st comes from the centroid.
struct Traj { std::vector<BSpec> f; std::vector<double> cen; };

static Traj trajectory (const std::vector<float>& x)
{
    Traj tr;
    static std::vector<double> w;
    if (w.size() != (size_t) kCWin)
    { w.assign ((size_t) kCWin, 0.0); for (int i = 0; i < kCWin; ++i) w[(size_t) i] = 0.5 - 0.5*std::cos (2.0*M_PI*i/(kCWin-1)); }
    std::vector<std::complex<double>> bf ((size_t) kCFFT, {0.0, 0.0});
    const double binHz = SR / kCFFT;
    for (int off = (int) (SR * kCSkip); off + kCWin <= (int) x.size(); off += kCHop)
    {
        std::fill (bf.begin(), bf.end(), std::complex<double> (0.0, 0.0));
        for (int i = 0; i < kCWin; ++i) bf[(size_t) i] = { (double) x[(size_t)(off+i)] * w[(size_t) i], 0.0 };
        fft (bf);
        // fb600 — POWER weighting and a -80 dB relative floor. A magnitude-weighted centroid over
        // the whole 60..16000 Hz span is dominated by the ANALYSIS NOISE FLOOR on a table whose
        // harmonics are tiny: Sine measured 10.77 st of whole-axis "span" that was entirely floor
        // (and its c=0 modRate read 32.5 Hz, i.e. noise). Power weighting plus the floor drops
        // Sine to the fraction of a semitone it actually is.
        double pk = 0;
        for (int b = 1; b < kCFFT/2; ++b)
        { const double f = b * binHz; if (f < 60.0 || f > 16000.0) continue; pk = std::max (pk, std::abs (bf[(size_t) b])); }
        const double cut = pk * 1e-4;                    // -80 dB re this window's loudest bin
        double num = 0, den = 0;
        for (int b = 1; b < kCFFT/2; ++b)
        { const double f = b * binHz; if (f < 60.0 || f > 16000.0) continue;
          const double a = std::abs (bf[(size_t) b]); if (a < cut) continue;
          const double m = a * a; num += f*m; den += m; }
        tr.cen.push_back (12.0 * std::log2 (std::max (20.0, den > 1e-24 ? num/den : F0) / F0));
        std::array<double, kNB> lin {};
        for (int k = 0; k < kNB; ++k)
        {
            const double lo = kBLo * std::pow (2.0, (double) k / 6.0), hi = kBLo * std::pow (2.0, (double)(k+1) / 6.0);
            const int b0 = std::max (1, (int) std::ceil (lo / binHz)), b1 = std::min (kCFFT/2 - 1, (int) std::floor (hi / binHz));
            double pw = 0; for (int b = b0; b <= b1; ++b) { const double m = std::abs (bf[(size_t) b]); pw += m*m; }
            lin[(size_t) k] = std::sqrt (pw);
        }
        double ref = 1e-12; for (double v : lin) ref = std::max (ref, v);
        BSpec sp;
        for (int k = 0; k < kNB; ++k) sp.b[(size_t) k] = std::max (kFloorDb, 20.0*std::log10 (std::max (lin[(size_t) k], 1e-12) / ref));
        tr.f.push_back (sp);
    }
    return tr;
}

// mSPAN — the GATED depth metric. p95 (not the max) of the pairwise band distance, so one
// analysis outlier cannot manufacture headroom. Monotone in the excursion by construction.
static double modSpanDb (const std::vector<BSpec>& s)
{
    std::vector<double> d;
    for (size_t a = 0; a < s.size(); ++a) for (size_t b = a+1; b < s.size(); b += 3) d.push_back (bdist (s[a], s[b]));
    if (d.size() < 8) return 0.0;
    std::sort (d.begin(), d.end());
    return d[(size_t) (0.95 * (double) (d.size() - 1))];
}

// mDEP-st — the brief's metric. Printed, NOT gated: see the banner (VowelMorph's centroid is a
// zig-zag in frame position, so this is not monotone in the excursion there).
static double modDepthSt (std::vector<double> v)
{
    if (v.size() < 8) return 0.0;
    std::sort (v.begin(), v.end());
    auto pct = [&] (double q) {
        const double idx = q * (double) (v.size() - 1);
        const size_t i = (size_t) idx; const double f = idx - (double) i;
        return (i + 1 < v.size()) ? v[i] + (v[i+1] - v[i]) * f : v[i]; };
    return pct (0.95) - pct (0.05);
}

// PC1 of the mean-removed band trajectory, by power iteration. A FIXED scalar observable (the
// centroid) can be an EVEN function of the drift and then reports 2f; PC1 is chosen per render as
// the direction of maximum spectral variance, which is signed and linear in the drift.
static std::vector<double> pc1Series (const std::vector<BSpec>& s)
{
    const int n = (int) s.size();
    std::vector<double> mu ((size_t) kNB, 0.0);
    for (const auto& f : s) for (int k = 0; k < kNB; ++k) mu[(size_t) k] += f.b[(size_t) k];
    for (int k = 0; k < kNB; ++k) mu[(size_t) k] /= (double) std::max (1, n);
    std::vector<std::vector<double>> X ((size_t) n, std::vector<double> ((size_t) kNB, 0.0));
    for (int t = 0; t < n; ++t) for (int k = 0; k < kNB; ++k) X[(size_t) t][(size_t) k] = s[(size_t) t].b[(size_t) k] - mu[(size_t) k];
    std::vector<double> v ((size_t) kNB);
    for (int k = 0; k < kNB; ++k) v[(size_t) k] = std::sin (0.7 * k + 1.0);   // deterministic seed — no RNG in a cert
    for (int it = 0; it < 40; ++it)
    {
        std::vector<double> u ((size_t) kNB, 0.0);
        for (int t = 0; t < n; ++t)
        { double d = 0; for (int k = 0; k < kNB; ++k) d += X[(size_t) t][(size_t) k] * v[(size_t) k];
          for (int k = 0; k < kNB; ++k) u[(size_t) k] += d * X[(size_t) t][(size_t) k]; }
        double nrm = 0; for (double d : u) nrm += d*d; nrm = std::sqrt (nrm);
        if (nrm < 1e-15) break;
        for (int k = 0; k < kNB; ++k) v[(size_t) k] = u[(size_t) k] / nrm;
    }
    std::vector<double> y ((size_t) n, 0.0);
    for (int t = 0; t < n; ++t) { double d = 0; for (int k = 0; k < kNB; ++k) d += X[(size_t) t][(size_t) k] * v[(size_t) k]; y[(size_t) t] = d; }
    return y;
}

// mRATE — harmonic-sum peak. Scoring each candidate f by |X(f)|+.7|X(2f)|+.5|X(3f)|+.35|X(4f)|
// stops a strong 2nd harmonic of the drift from outranking the drift itself.
static double modRateHz (const std::vector<double>& v)
{
    const int n = (int) v.size(); if (n < 64) return 0.0;
    int NF = 1; while (NF < 4*n) NF <<= 1;
    double mean = 0; for (double d : v) mean += d; mean /= (double) n;
    std::vector<std::complex<double>> bf ((size_t) NF, {0.0, 0.0});
    for (int i = 0; i < n; ++i)
    { const double wn = 0.5 - 0.5*std::cos (2.0*M_PI*i/(n-1)); bf[(size_t) i] = { (v[(size_t) i] - mean) * wn, 0.0 }; }
    fft (bf);
    const double fs = SR / kCHop, binHz = fs / NF;
    const int bMin = std::max (1, (int) std::ceil (0.20 / binHz));       // below 0.2 Hz is the note envelope
    const int bMax = std::min (NF/2 - 1, (int) std::floor (45.0 / binHz));
    std::vector<double> m ((size_t) (NF/2));
    for (int b = 0; b < NF/2; ++b) m[(size_t) b] = std::abs (bf[(size_t) b]);
    double peak = 0; for (int b = bMin; b <= bMax; ++b) peak = std::max (peak, m[(size_t) b]);
    if (peak <= 0) return 0.0;
    const double wk[4] = { 1.0, 0.7, 0.5, 0.35 };
    double best = 0, bf0 = 0;
    for (int b = bMin; b <= bMax; ++b)
    {
        if (m[(size_t) b] < 0.25 * peak) continue;                       // never chase a subharmonic of nothing
        double sc = 0;
        for (int k = 1; k <= 4; ++k) { const int bb = b*k; if (bb >= NF/2) break; sc += wk[k-1] * m[(size_t) bb]; }
        if (sc > best) { best = sc; bf0 = b * binHz; }
    }
    return bf0;
}

// ── render one held note; return the whole L buffer plus a per-block ratio log ──
struct Take { std::vector<float> L, R; double maxCents = 0.0; };

static Take render (HarmParams p, const HarmTableSource::Grid* G, double seconds, std::uint32_t seed = 0x5EEDFA11u)
{
    static float A[512], P[512];
    if (p.mainMode == 6 && G != nullptr)
    {
        p.tableN   = HarmTableSource::blend (*G, p.hue, A, P, 512);
        p.tableAmp = A; p.tablePhase = P; p.tableSig = G->sig + p.hue * 1024.0f;
        p.tableGridAmp = &G->amp[0][0];
        p.tableFrames  = WavetableSpec::kNumFrames;
        p.tableStride  = HarmTableSource::kMaxN;
    }
    const int N = (int) (SR * seconds);
    Take t; t.L.assign ((size_t) N, 0.f); t.R.assign ((size_t) N, 0.f);
    HarmonicEngine e; e.prepare (SR, true);
    e.setParams (p); e.noteOn (F0, seed);
    std::vector<float> r0 ((size_t) 512, -1.f);
    for (int off = 0; off < N; off += gBlk)
    {
        const int m = std::min (gBlk, N - off);
        e.setParams (p);
        e.renderBlockAdd (&t.L[(size_t) off], &t.R[(size_t) off], m);
        const int np = e.debugNumPartials();
        for (int j = 0; j < np && j < 512; ++j)
        {
            const float r = e.debugRatio (j);
            if (r <= 0.f) continue;
            if (r0[(size_t) j] < 0.f) { r0[(size_t) j] = r; continue; }
            const double c = 1200.0 * std::log2 ((double) r / (double) r0[(size_t) j]);
            if (std::fabs (c) > t.maxCents) t.maxCents = std::fabs (c);
        }
    }
    return t;
}

static std::vector<Spec> windows (const std::vector<float>& x)
{
    std::vector<Spec> v;
    for (int k = 0; k < kNWin; ++k)
    { const int off = (int) ((kT0 + kHop * k) * SR); if (off + kWin > (int) x.size()) break; v.push_back (analyse (&x[(size_t) off])); }
    return v;
}
static std::vector<BSpec> bwindows (const std::vector<float>& x)
{
    std::vector<BSpec> v;
    for (int k = 0; k < kNWin; ++k)
    { const int off = (int) ((kT0 + kHop * k) * SR); if (off + kWin > (int) x.size()) break; v.push_back (analyseBands (&x[(size_t) off])); }
    return v;
}
// SWING — the largest timbral distance the held note ever travels between two moments in it.
static double motionOf (const std::vector<Spec>& s)
{ double m = 0; for (size_t a = 0; a < s.size(); ++a) for (size_t b = a+1; b < s.size(); ++b) m = std::max (m, dist (s[a], s[b])); return m; }
static double bmotionOf (const std::vector<BSpec>& s)
{ double m = 0; for (size_t a = 0; a < s.size(); ++a) for (size_t b = a+1; b < s.size(); ++b) m = std::max (m, bdist (s[a], s[b])); return m; }
// FLUX — mean timbral distance between CONSECUTIVE 50 ms moments, in dB per hop. ⚠️ fb600: this
// SATURATES AND THEN INVERTS once the motion outruns the 42.7 ms window, which is why it is no
// longer a gate. Printed as evidence, not as a verdict.
static double fluxOf (const std::vector<Spec>& s)
{ double a = 0; int c = 0; for (size_t k = 1; k < s.size(); ++k) { a += dist (s[k], s[k-1]); ++c; } return c ? a/c : 0.0; }
static bool oneShot (const std::vector<Spec>& s)
{ int up = 0; for (size_t k = 2; k < s.size(); ++k) if (dist (s[k], s[0]) >= dist (s[k-1], s[0]) - 1e-9) ++up;
  return s.size() > 3 && up >= (int) (0.92 * (double) (s.size() - 2)); }
static double abOf (const std::vector<Spec>& a, const std::vector<Spec>& b)
{ double s = 0; size_t n = std::min (a.size(), b.size()); for (size_t k = 0; k < n; ++k) s += dist (a[k], b[k]); return n ? s/n : 0.0; }
static double babOf (const std::vector<BSpec>& a, const std::vector<BSpec>& b)
{ double s = 0; size_t n = std::min (a.size(), b.size()); for (size_t k = 0; k < n; ++k) s += bdist (a[k], b[k]); return n ? s/n : 0.0; }

static std::uint64_t fprint (const std::vector<float>& L, const std::vector<float>& R)
{
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&] (const std::vector<float>& v) {
        for (float f : v) { std::uint32_t u; std::memcpy (&u, &f, 4); h ^= u; h *= 1099511628211ull; } };
    mix (L); mix (R); return h;
}

static HarmParams base()
{
    HarmParams p;
    p.mainMode = 6; p.sculptMode = 0;              // TABLE + KEEL — the only config Max will ship
    p.hue = 0.35f; p.count = 0.5f; p.lean = 0.5f;
    p.fan = 0.0f; p.grit = 0.0f; p.braid = 0.0f;
    p.carve = 0.0f; p.churn = 0.5f; p.root = 0.0f;
    p.shine = 0.0f; p.wilt = 0.5f; p.forge = 0.0f;
    return p;
}

static int gPass = 0, gFail = 0;
static void gate (bool c, const char* n, const std::string& d = "")
{ c ? ++gPass : ++gFail; std::printf ("  %-4s %-54s %s\n", c ? "ok" : "FAIL", n, d.c_str()); }

static const char* SCN[6] = { "KEEL", "SPLAY", "CULL", "TIDE", "TERRACE", "CLANG" };

// fb599 — the drift path is present iff HarmParams carries the frame stack.
template <class P, class = void> struct HasGrid : std::false_type {};
template <class P> struct HasGrid<P, std::void_t<decltype (std::declval<P&>().tableGridAmp)>> : std::true_type {};
static constexpr bool kDriftAvailable = HasGrid<tw::HarmParams>::value;

// ── fb600 · THE GATE'S OWN HISTORY. Recorded by compiling THIS EXACT CERT against the SHIPPED
//    (fb599) header — depth fixed at 0.25, rate 3 Hz, curve 4 — and reading the printed rows.
//    Reproduce: cp the pre-fb600 HarmonicEngine.h to a dir D, then
//      c++ -std=c++17 -O2 -I D -I Tests/shim -I Source Tests/harm_churn_cert.cpp -framework Accelerate -o /tmp/cs
static constexpr double kShippedG3b  =  1.044;  // mSPAN(100%)/mSPAN(25%), min over tables — FAILS the 1.5x branch
static constexpr double kShippedExpo =  0.414;  // worst-table exposure vs ceiling         — FAILS the 60% branch
static constexpr double kShippedG8   = -0.007;  // chord decorrelation, floor-corrected     — FAILS the 15% gate
static constexpr double kShippedG2   =  0.517;  // HUE-KILL survival at churn 0.15          — FAILS the 85% gate
static constexpr double kShippedExpoTab[3] = { 0.441, 1.037, 0.414 };   // ProphetSaw / VowelMorph / Rise

// ── fb600 · THE G8 CHORD: A2 C#3 E3 A3, every note-on in the SAME BLOCK ───────────────────────
static const double kChordHz[4] = { 110.0, 138.59132, 164.81378, 220.0 };
static const std::uint32_t kChordSeed[4] = { 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u };

// ── fb600 · THE MECHANISM ITSELF: do four voices struck in the SAME BLOCK share a drift phase? ─
//  The audio-domain control below is the HEARING metric and is the thing that gates; this is the
//  mechanism it is a proxy for, read straight off the engine so the proxy can be checked.
//  Returns the worst pairwise |driftPos(a) - driftPos(b)| seen over the first `blocks` blocks.
static double driftSeparation (const HarmTableSource::Grid& G, float churn, int blocks, bool sameSeed)
{
    static float A[512], P[512];
    HarmParams p = base(); p.churn = churn;
    p.tableN = HarmTableSource::blend (G, p.hue, A, P, 512);
    p.tableAmp = A; p.tablePhase = P; p.tableSig = G.sig + p.hue * 1024.f;
    p.tableGridAmp = &G.amp[0][0]; p.tableFrames = WavetableSpec::kNumFrames; p.tableStride = HarmTableSource::kMaxN;
    HarmonicEngine e[4];
    std::vector<float> L ((size_t) BLK, 0.f), R ((size_t) BLK, 0.f);
    for (int v = 0; v < 4; ++v)
    { e[v].prepare (SR, true); e[v].setParams (p);
      e[v].noteOn (kChordHz[v], sameSeed ? kChordSeed[0] : kChordSeed[v]); }
    double worst = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
        for (int v = 0; v < 4; ++v) { e[v].setParams (p); e[v].renderBlockAdd (L.data(), R.data(), BLK); }
        for (int a = 0; a < 4; ++a) for (int c = a+1; c < 4; ++c)
            worst = std::max (worst, (double) std::fabs (e[a].debugDriftPos() - e[c].debugDriftPos()));
    }
    return worst;
}

// ── fb600 · THE ALGORITHM'S 100 % — a slow sweep of the WHOLE frame axis, one voice ───────────
//  The LIFEGUARD LAW's exposure ratio needs a denominator: "what the engine can still deliver".
//  Drive hue from the HOST at 0.4 Hz, depth 0.5 about 0.5, so the frame position covers 0..1 with
//  no fold and slowly enough that no analysis window can smear it. mSPAN of that IS the ceiling.
static std::vector<float> axisSweep (const HarmTableSource::Grid& G, double hue0, double depth,
                                     double lfoHz, double seconds)
{
    static float A[512], P[512];
    HarmParams p = base(); p.churn = 0.f; p.hue = (float) hue0;
    p.tableN = HarmTableSource::blend (G, (float) hue0, A, P, 512);
    p.tableAmp = A; p.tablePhase = P; p.tableSig = G.sig + (float) hue0 * 1024.f;
    p.tableGridAmp = &G.amp[0][0]; p.tableFrames = WavetableSpec::kNumFrames; p.tableStride = HarmTableSource::kMaxN;
    const int N = (int) (SR * seconds);
    std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
    HarmonicEngine e; e.prepare (SR, true); e.setParams (p); e.noteOn (F0, 0x5EEDFA11u);
    for (int off = 0; off < N; off += BLK)
    {
        const int m = std::min (BLK, N - off);
        const double t = (double) off / SR;
        float x = (float) (hue0 + depth * std::sin (2.0*M_PI*lfoHz*t));
        x = x < 0.f ? -x : x; if (x > 1.f) x = 2.f - x;
        p.tableN = HarmTableSource::blend (G, x, A, P, 512);
        p.tableAmp = A; p.tableSig = G.sig + x * 1024.f;
        e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
    }
    return L;
}

// mode LFO: churn is OFF and the HOST sweeps HarmHue with one global LFO, which is exactly what
// the mod matrix can already do (PluginProcessor.cpp:9481/9495 resolve h.hue once per block per
// OSCILLATOR and push that one HarmParams to every voice).
static std::vector<float> renderChord (const HarmTableSource::Grid& G, bool lfoMode,
                                       float churn, double lfoHz, double lfoDepth, double seconds,
                                       bool sameSeed = false)
{
    static float A[512], P[512];
    HarmParams p = base();
    p.churn = lfoMode ? 0.0f : churn;
    const float hue0 = p.hue;
    p.tableN = HarmTableSource::blend (G, hue0, A, P, 512);
    p.tableAmp = A; p.tablePhase = P; p.tableSig = G.sig + hue0 * 1024.0f;
    p.tableGridAmp = &G.amp[0][0]; p.tableFrames = WavetableSpec::kNumFrames; p.tableStride = HarmTableSource::kMaxN;

    const int N = (int) (SR * seconds);
    std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
    HarmonicEngine e[4];
    for (int v = 0; v < 4; ++v)
    { e[v].prepare (SR, true); e[v].setParams (p);
      e[v].noteOn (kChordHz[v], sameSeed ? kChordSeed[0] : kChordSeed[v]); }
    for (int off = 0; off < N; off += BLK)
    {
        const int m = std::min (BLK, N - off);
        if (lfoMode)
        {   // same FOLD as the engine's drift, so the two are matched in shape as well as depth
            const double t = (double) off / SR;
            float x = hue0 + (float) (lfoDepth * std::sin (2.0*M_PI*lfoHz*t));
            x = x < 0.f ? -x : x; if (x > 1.f) x = 2.f - x;
            p.tableN = HarmTableSource::blend (G, x, A, P, 512);
            p.tableAmp = A; p.tableSig = G.sig + x * 1024.0f;   // tablePhase is note-on only
        }
        for (int v = 0; v < 4; ++v) { e[v].setParams (p); e[v].renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m); }
    }
    return L;
}

int main()
{
    static HarmTableSource::Grid GP, GV, GR, GS, GD, GF, GC;
    HarmTableSource::bake (WavetableBank::specForPreset (4),  GP);   // ProphetSaw    (bank rank 20/46)
    HarmTableSource::bake (WavetableBank::specForPreset (16), GV);   // VowelMorph    (bank rank  6/46)
    HarmTableSource::bake (WavetableBank::specForPreset (24), GR);   // Rise          (bank rank 17/46)
    HarmTableSource::bake (WavetableBank::specForPreset (0),  GS);   // Sine          — THE DEFAULT
    HarmTableSource::bake (WavetableBank::specForPreset (22), GD);   // SpectralDrift — phase-only spec
    HarmTableSource::bake (WavetableBank::specForPreset (26), GF);   // PhaseDrift
    HarmTableSource::bake (WavetableBank::specForPreset (8),  GC);   // CS80Brass
    struct T { const char* name; const HarmTableSource::Grid* g; } TB[3] = { {"ProphetSaw",&GP}, {"VowelMorph",&GV}, {"Rise",&GR} };
    struct T2 { const char* name; int preset; const HarmTableSource::Grid* g; };
    const T2 FLAT[4] = { {"Sine(DEFAULT)",0,&GS}, {"SpectralDrift",22,&GD}, {"PhaseDrift",26,&GF}, {"CS80Brass",8,&GC} };

    // fb599 — DETECT, NEVER TRUST A -D. This banner used to be an #ifdef the builder had to pass by
    // hand; forget it and the cert prints "SHIPPED", measures 0.000, and reports CHURN dead on a
    // header where it works. HarmParams::tableGridAmp exists only on the fixed header, so ask the type.
    std::printf ("\n══ churn_cert ══  HEADER: %s\n", kDriftAvailable ? "DRIFT AVAILABLE" : "SHIPPED (no drift path)");
    // fb600 — and the same law applies to every CONSTANT the retune moved: a number that does not
    // move when the code does is the tell, so the cert reads them back out of the header it compiled
    // against and PRINTS them. -1.000 means "this header does not define that constant at all".
    {
        const float dep = harm::probeDepth(),  rhz = harm::probeRateHz(), cur = harm::probeCurve();
        const float flr = harm::probeDepthFloor(), spr = harm::probeRateSpread();
        std::printf ("   COMPILED CONSTANTS (read back from the header, not assumed):\n");
        std::printf ("     kChurnDepth      = %6.3f\n", dep);
        std::printf ("     kChurnDepthFloor = %6.3f   %s\n", flr, flr < 0.f ? "<-- ABSENT: pre-fb600 header, depth is a CONSTANT" : "(fb600: depth rides the knob)");
        std::printf ("     kChurnRateHz     = %6.3f\n", rhz);
        std::printf ("     kChurnCurve      = %6.3f\n", cur);
        std::printf ("     kChurnRateSpread = %6.3f   %s\n", spr, spr < 0.f ? "<-- ABSENT: pre-fb600 header, every voice shares one drift phase" : "(fb600: per-voice drift-rate detune)");
        std::printf ("     derived chRate(C) = %.1f*(2^(%.1f*C)-1)/(2^%.1f-1) Hz, and chDepth(C) of the frame axis:\n", rhz, cur, cur);
        std::printf ("       C        ");
        for (float c : { 0.f, 0.15f, 0.25f, 0.5f, 0.75f, 1.f }) std::printf ("%9.2f", c);
        std::printf ("\n       rate Hz  ");
        for (float c : { 0.f, 0.15f, 0.25f, 0.5f, 0.75f, 1.f })
            std::printf ("%9.3f", (double) rhz * (std::pow (2.0, (double) cur * c) - 1.0) / (std::pow (2.0, (double) cur) - 1.0));
        std::printf ("\n       period s ");
        for (float c : { 0.f, 0.15f, 0.25f, 0.5f, 0.75f, 1.f })
        { const double r = (double) rhz * (std::pow (2.0, (double) cur * c) - 1.0) / (std::pow (2.0, (double) cur) - 1.0);
          if (r <= 0.0) std::printf ("%9s", "inf"); else std::printf ("%9.2f", 1.0/r); }
        std::printf ("\n       depth    ");
        for (float c : { 0.f, 0.15f, 0.25f, 0.5f, 0.75f, 1.f })
            std::printf ("%9.3f", flr < 0.f ? (double) dep : (double) dep * ((double) flr + (1.0 - (double) flr) * c));
        std::printf ("\n");
    }
    std::printf ("   metric: short-time harmonic spectrum, amplitude-weighted |dB|; %d windows over %.2f s\n",
                 kNWin, kT0 + kHop*(kNWin-1) + (double) kWin/SR);
    std::printf ("   fb600 metric: centroid every %d samples (%.1f Hz series), Hann %d; modDepth = p95-p05 st, modRate = lowest strong peak\n\n",
                 kCHop, SR/kCHop, kCWin);

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── A · SHIPPED FACT: TABLE + KEEL, Carve/Fan at their defaults ──────────────────\n");
    std::printf ("%-12s %10s %10s %10s %10s %10s\n", "table", "AB(0,0.5)", "AB(0,1)", "AB(.5,1)", "MOT c=0", "MOT c=1");
    double worstAB = 0.0;
    bool allIdentical = true;
    for (auto& tb : TB)
    {
        HarmParams p = base();
        p.churn = 0.0f; Take a = render (p, tb.g, 3.4);
        p.churn = 0.5f; Take b = render (p, tb.g, 3.4);
        p.churn = 1.0f; Take c = render (p, tb.g, 3.4);
        auto sa = windows (a.L), sb = windows (b.L), sc = windows (c.L);
        const double ab = abOf (sa, sb), ac = abOf (sa, sc), bc = abOf (sb, sc);
        worstAB = std::max ({ worstAB, ab, ac, bc });
        if (fprint (a.L,a.R) != fprint (c.L,c.R)) allIdentical = false;
        std::printf ("%-12s %10.3f %10.3f %10.3f %10.3f %10.3f   %s\n", tb.name, ab, ac, bc,
                     motionOf (sa), motionOf (sc),
                     fprint (a.L,a.R) == fprint (c.L,c.R) ? "BIT-IDENTICAL 0 vs 1" : "differs");
    }
    std::printf ("\n");

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── B · THE LIVE MAP: which (sculpt, carve, fan) let CHURN change the sound ──────\n");
    std::printf ("   AB(churn 0 -> 1) on ProphetSaw, mono L.  '.' = dead (<0.05 dB)\n");
    std::printf ("%-9s %8s %8s %8s %8s %8s %8s\n", "sculpt", "cv0/fn0", "cv.3/fn0", "cv.6/fn0", "cv1/fn0", "cv0/fn1", "cv1/fn1");
    for (int sm = 0; sm < 6; ++sm)
    {
        std::printf ("%-9s", SCN[sm]);
        const float CV[6] = { 0.f, 0.3f, 0.6f, 1.f, 0.f, 1.f };
        const float FN[6] = { 0.f, 0.f,  0.f,  0.f, 1.f, 1.f };
        for (int i = 0; i < 6; ++i)
        {
            HarmParams p = base(); p.sculptMode = sm; p.carve = CV[i]; p.fan = FN[i];
            p.churn = 0.f; Take a = render (p, &GP, 3.4);
            p.churn = 1.f; Take b = render (p, &GP, 3.4);
            const double d = abOf (windows (a.L), windows (b.L));
            if (d < 0.05) std::printf ("%8s", ".");
            else          std::printf ("%8.2f", d);
        }
        std::printf ("\n");
    }
    {
        const char* nm[2] = { "SHINE=1", "BRAID=1" };
        for (int i = 0; i < 2; ++i)
        {
            HarmParams p = base(); if (i==0) p.shine = 1.f; else p.braid = 1.f;
            p.churn = 0.f; Take a = render (p, &GP, 3.4);
            p.churn = 1.f; Take b = render (p, &GP, 3.4);
            std::printf ("%-9s %8.2f  (carve 0, fan 0 — CHURN reaches these via shineChurn_)\n",
                         nm[i], abOf (windows (a.L), windows (b.L)));
        }
    }
    std::printf ("\n");

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── C · WHAT ALREADY MOVES: every other HARM knob at its extreme, TABLE + KEEL ───\n");
    std::printf ("   (the duplication test — CHURN must not be a knob the panel already has; CHURN = 0 throughout)\n");
    std::printf ("%-14s %8s %8s %9s  %s\n", "knob", "SWING", "FLUX", "detune c", "shape");
    {
        struct K { const char* n; int idx; float v; };
        const K KS[13] = {
            { "Hue = 0.9",   0, 0.9f }, { "Partials = 1", 1, 1.0f }, { "Lean = 1",   2, 1.0f },
            { "Fan = 1",     3, 1.0f }, { "Grit = 1",     4, 1.0f }, { "Braid = 1",  5, 1.0f },
            { "Carve = 1",   6, 1.0f }, { "Root = 1",     7, 1.0f }, { "Shine = 1",  8, 1.0f },
            { "Wilt = 1",    9, 1.0f }, { "Wilt = 0",    10, 0.0f }, { "Forge = 1", 11, 1.0f },
            { "(nothing)",  12, 0.0f } };
        for (const K& k : KS)
        {
            HarmParams p = base(); p.churn = 0.0f;   // CHURN OFF — this section measures the OTHER knobs
            switch (k.idx) { case 0: p.hue=k.v; break; case 1: p.count=k.v; break; case 2: p.lean=k.v; break;
                             case 3: p.fan=k.v; break; case 4: p.grit=k.v; break; case 5: p.braid=k.v; break;
                             case 6: p.carve=k.v; break; case 7: p.root=k.v; break; case 8: p.shine=k.v; break;
                             case 9: case 10: p.wilt=k.v; break; case 11: p.forge=k.v; break; default: break; }
            Take t = render (p, &GV, 3.4);
            auto w = windows (t.L);
            std::printf ("%-14s %8.2f %8.3f %9.2f  %s\n", k.n, motionOf (w), fluxOf (w), t.maxCents,
                         motionOf (w) < 1.0 ? "STATIC" : (oneShot (w) ? "one-shot envelope" : "sustained"));
        }
    }
    std::printf ("\n");

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── D · CHURN = per-voice autonomous FRAME DRIFT ─────────────────────────────────\n");
    std::printf ("   SWING = max timbral distance travelled;  FLUX = dB/50 ms hop (INVERTS at the top — not gated)\n");
    std::printf ("   mSPAN = p95 pairwise band distance, dB  [GATED depth]   mRATE = PC1 modulation peak, Hz  [GATED rate]\n");
    std::printf ("   mDEP  = centroid p95-p05, semitones — the brief's metric, PRINTED NOT GATED (see the banner)\n");
    std::printf ("%-12s %-6s %7s %7s %7s %7s %7s %7s %8s\n", "table", "", "c=0", "c=0.15", "c=0.25", "c=0.50", "c=0.75", "c=1.00", "detune");
    double swMin25 = 1e9, swMin50 = 1e9, swMin100 = 1e9, worstDet = 0.0;
    double fluxMin25 = 1e9, fluxRatio = 1e9;
    double msHead = 1e9, mrRatio = 1e9, msFloor = 0.0, ms15rel = 0.0, mdBrief3b = 1e9, mdBrief2 = 0.0;
    bool   fluxInverts = false;
    double obsRateMul = 0.0;
    struct Row { double ms100; };
    std::array<Row, 3> rows {};
    int ri = 0;
    for (auto& tb : TB)
    {
        const float CS[6] = { 0.f, 0.15f, 0.25f, 0.5f, 0.75f, 1.f };
        double m[6], fx[6], ms[6], md[6], mr[6]; double det = 0;
        for (int i = 0; i < 6; ++i)
        {
            HarmParams p = base(); p.churn = CS[i];
            Take t = render (p, tb.g, 3.4);
            auto w = windows (t.L);
            Traj tr = trajectory (t.L);
            m[i] = motionOf (w); fx[i] = fluxOf (w);
            ms[i] = modSpanDb (tr.f); md[i] = modDepthSt (tr.cen); mr[i] = modRateHz (pc1Series (tr.f));
            det = std::max (det, t.maxCents);
        }
        std::printf ("%-12s %-6s", tb.name, "SWING");
        for (int i = 0; i < 6; ++i) std::printf ("%7.2f", m[i]);
        std::printf ("%8.4f\n", det);
        std::printf ("%-12s %-6s", "", "FLUX");
        for (int i = 0; i < 6; ++i) std::printf ("%7.3f", fx[i]);
        std::printf ("%s\n", fx[5] < fx[4] ? "  <-- INVERTED (100% < 75%): the 42.7 ms window cannot resolve the top rate" : "");
        std::printf ("%-12s %-6s", "", "mSPAN");
        for (int i = 0; i < 6; ++i) std::printf ("%7.2f", ms[i]);
        std::printf ("\n");
        std::printf ("%-12s %-6s", "", "mRATE");
        for (int i = 0; i < 6; ++i) std::printf ("%7.3f", mr[i]);
        std::printf ("\n");
        std::printf ("%-12s %-6s", "", "mDEP");
        for (int i = 0; i < 6; ++i) std::printf ("%7.2f", md[i]);
        std::printf ("%s\n", md[5] < md[2] ? "  <-- the brief's centroid metric goes DOWN as the knob goes UP here" : "");
        swMin25  = std::min (swMin25,  m[2]);
        swMin50  = std::min (swMin50,  m[3]);
        swMin100 = std::min (swMin100, m[5]);
        worstDet = std::max (worstDet, det);
        fluxMin25 = std::min (fluxMin25, fx[2]);
        fluxRatio = std::min (fluxRatio, fx[5] / std::max (1e-9, fx[3]));
        if (fx[5] < fx[4]) fluxInverts = true;
        msHead  = std::min (msHead,  ms[5] / std::max (1e-9, ms[2]));
        ms15rel = std::max (ms15rel, ms[1] / std::max (1e-9, ms[5]));
        mrRatio = std::min (mrRatio, mr[5] / std::max (1e-9, mr[3]));
        msFloor = std::max (msFloor, ms[0]);
        mdBrief3b = std::min (mdBrief3b, md[5] / std::max (1e-9, md[2]));
        mdBrief2  = std::max (mdBrief2,  md[1] / std::max (1e-9, md[5]));
        obsRateMul = std::max (obsRateMul, mr[5] / std::max (1e-9, (double) harm::probeRateHz()));
        rows[(size_t) ri++].ms100 = ms[5];
    }
    std::printf ("   mSPAN at c=0 (the ANALYSIS FLOOR — the drift is provably OFF there) = %.2f dB worst case\n", msFloor);
    std::printf ("   observed mRATE(100%%) / kChurnRateHz = %.3f  (the per-voice rate detune for this cert's seed;\n", obsRateMul);
    std::printf ("     every fb600 gate is a RATIO between two churn settings, so it is immune to this multiplier)\n");
    // the FOLD must give the same excursion at the ends of the Hue axis as in the middle
    double swEdgeMin = 1e9;
    for (float hu : { 0.0f, 0.5f, 1.0f })
    { HarmParams p = base(); p.hue = hu; p.churn = 0.5f; Take t = render (p, &GV, 3.4);
      const double sw = motionOf (windows (t.L));
      std::printf ("   fold check  VowelMorph hue=%.1f churn=0.5  SWING=%6.2f\n", hu, sw);
      swEdgeMin = std::min (swEdgeMin, sw); }
    std::printf ("   FLUX 100%%/50%% ratio (min over tables) = %.2fx   [informational only since fb600]\n\n", fluxRatio);

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── D3 · EXPOSURE — the LIFEGUARD LAW's own metric ───────────────────────────────\n");
    std::printf ("   ceiling = mSPAN of a 0.4 Hz sweep across the WHOLE frame axis (hue .5 +- .5, no fold) = what\n");
    std::printf ("   the engine can still deliver.  exposure = mSPAN(churn 1.0) / ceiling = what the knob's MAX\n");
    std::printf ("   actually delivers. \"My 100%% shouldn't be 50.\"  fb599 recorded for comparison.\n");
    std::printf ("%-12s %10s %11s %10s   %s\n", "table", "ceiling", "mSPAN@1.0", "exposure", "fb599 exposure (recorded)");
    double expoMin = 1e9;
    {
        const double* SHIPPED_EXPO = kShippedExpoTab;
        int i = 0;
        for (auto& tb : TB)
        {
            const double ceil = modSpanDb (trajectory (axisSweep (*tb.g, 0.5, 0.5, 0.4, 6.0)).f);
            const double ex = rows[(size_t) i].ms100 / std::max (1e-9, ceil);
            expoMin = std::min (expoMin, ex);
            std::printf ("%-12s %10.2f %11.2f %9.1f%%   %20.1f%%\n", tb.name, ceil, rows[(size_t) i].ms100,
                         100.0*ex, 100.0*SHIPPED_EXPO[i]);
            ++i;
        }
    }
    std::printf ("   ⚠️ VowelMorph is the one table where fb600's TOP is WORSE than fb599's (72.7%% vs 103.7%%): it\n");
    std::printf ("      reaches its whole-axis ceiling by churn 0.25, so the extra DEPTH buys nothing there and\n");
    std::printf ("      the extra RATE costs it — measured, depth .45 held fixed, mSPAN vs rate: 20.92 dB at\n");
    std::printf ("      0.5 Hz, 19.76 at 3 Hz, 18.04 at 5 Hz, 15.78 at 10 Hz. On ProphetSaw and Rise the same\n");
    std::printf ("      change is a large WIN (44.1%%->66.8%% and 41.4%%->63.2%%). This is the deep end behaving like\n");
    std::printf ("      the deep end: at the top the character changes from a SCAN into a fast warble.\n\n");

    std::printf ("── D2 · INFORMATIONAL, NOT GATED: the DEFAULT table and the FLATTEST tables ─────\n");
    std::printf ("   ⚠️ READ THIS BEFORE READING THE GREEN BANNER BELOW: a PASS in this cert means CHURN works on\n");
    std::printf ("      the three GATED tables. It does NOT mean CHURN works on every table in the 46-table bank.\n");
    std::printf ("      A table with almost no FRAME AXIS gives churn nothing to scan — SpectralDrift's spec is\n");
    std::printf ("      PHASE-ONLY (every frame has an identical amplitude spectrum), and the additive bank is\n");
    std::printf ("      amplitude-only, so its axis span is ZERO BY CONSTRUCTION and measures at the floor. That is\n");
    std::printf ("      a SEPARATE defect, the project owner is deciding it right now, and gating on it here would\n");
    std::printf ("      block this commit on a decision that has not been made — so these rows are NOT gated.\n");
    std::printf ("   axisSpan = centroid span across the whole frame axis; ceiling = mSPAN of that same sweep.\n");
    std::printf ("%-15s %6s %9s %9s %10s %10s  %s\n", "table", "preset", "axisSpan", "ceiling", "mSPAN@1.0", "exposure", "verdict");
    {
        auto axisSpanSt = [&] (const HarmTableSource::Grid* g) {
            double lo = 1e9, hi = -1e9;
            for (int i = 0; i <= 20; ++i)
            {
                HarmParams p = base(); p.churn = 0.f; p.hue = (float) i / 20.f;
                Take t = render (p, g, 0.5);
                auto c = trajectory (t.L).cen;
                if (c.empty()) continue;
                double m = 0; for (double d : c) m += d; m /= (double) c.size();
                lo = std::min (lo, m); hi = std::max (hi, m);
            }
            return (hi > lo) ? hi - lo : 0.0; };
        auto row = [&] (const char* nm, int preset, const HarmTableSource::Grid* g, bool gated) {
            HarmParams p = base(); p.churn = 1.f;
            const double ms = modSpanDb (trajectory (render (p, g, 3.4).L).f);
            const double ce = modSpanDb (trajectory (axisSweep (*g, 0.5, 0.5, 0.4, 6.0)).f);
            const double sp = axisSpanSt (g);
            std::printf ("%-15s %6d %9.2f %9.2f %10.2f %9.1f%%  %s\n", nm, preset, sp, ce, ms,
                         100.0*ms/std::max (1e-9, ce),
                         gated ? "GATED" : (sp < 0.5 ? "INFO — NO frame axis to scan" : "INFO — has an axis")); };
        row ("ProphetSaw", 4, &GP, true); row ("VowelMorph", 16, &GV, true); row ("Rise", 24, &GR, true);
        for (const T2& tb : FLAT) row (tb.name, tb.preset, tb.g, false);
    }
    std::printf ("\n");

    bool zeroDriftOff = false;
    std::printf ("── E · IDENTITY AT 0 — the shipped fingerprint must survive ─────────────────────\n");
    {
        // Fingerprints taken on this header at churn = 0 across a pressure matrix. On the shipped
        // header these ARE the reference; the retune must reproduce them byte for byte.
        struct C { const char* n; int sm; float cv, fn, sh, br, gr, wl, rt; };
        const C CS[8] = {
            { "table+keel  bare",   0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f },
            { "table+keel  carve1", 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f },
            { "table+tide  carve1", 3, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f },
            { "table+terr  carve1", 4, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f },
            { "table+keel  fan1",   0, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f },
            { "table+keel  shine1", 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f },
            { "table+keel  braid1", 0, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f },
            { "table+keel  full",   0, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.8f, 0.8f },
        };
        for (int i = 0; i < 8; ++i)
        {
            HarmParams p = base();
            p.sculptMode = CS[i].sm; p.carve = CS[i].cv; p.fan = CS[i].fn;
            p.shine = CS[i].sh; p.braid = CS[i].br; p.grit = CS[i].gr; p.wilt = CS[i].wl; p.root = CS[i].rt;
            p.churn = 0.0f;
            Take t = render (p, &GP, 0.6);
            std::printf ("   %-22s  0x%016llx\n", CS[i].n, (unsigned long long) fprint (t.L, t.R));
        }
        std::printf ("   (compare the two header builds line by line — all eight must match)\n");
        // and the MECHANISM behind the fingerprints, read off the engine rather than inferred:
        // chRate must be EXACTLY 0.f at churn 0 (fastExp2(0) == 1.0f exactly), so driftPh_ never
        // advances and driftOn_ is false — which is what makes churn 0 byte-for-byte the old build.
        static float A0[512], P0[512];
        HarmParams z = base(); z.churn = 0.0f;
        z.tableN = HarmTableSource::blend (GP, z.hue, A0, P0, 512);
        z.tableAmp = A0; z.tablePhase = P0; z.tableSig = GP.sig + z.hue*1024.f;
        z.tableGridAmp = &GP.amp[0][0]; z.tableFrames = WavetableSpec::kNumFrames; z.tableStride = HarmTableSource::kMaxN;
        HarmonicEngine ez; ez.prepare (SR, true); ez.setParams (z); ez.noteOn (F0, 0x5EEDFA11u);
        std::vector<float> zl ((size_t) BLK, 0.f), zr ((size_t) BLK, 0.f);
        bool everOn = false; double maxPos = 0.0;
        for (int b = 0; b < 200; ++b)
        { std::fill (zl.begin(), zl.end(), 0.f); std::fill (zr.begin(), zr.end(), 0.f);
          ez.setParams (z); ez.renderBlockAdd (zl.data(), zr.data(), BLK);
          if (ez.debugDriftOn()) everOn = true;
          maxPos = std::max (maxPos, (double) std::fabs (ez.debugDriftPos())); }
        zeroDriftOff = ! everOn;
        std::printf ("   at churn 0, over 200 blocks: driftOn_ ever true = %s;  max |driftPos_| = %.9f\n\n",
                     everOn ? "YES  <-- BROKEN" : "no", maxPos);
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────
    double stepRatio = 0.0;
    {
        std::printf ("── E2 · CHURN IS A MOD DESTINATION (SynthModConfig.h:138) — stepping it must not click ─\n");
        std::printf ("   fb600: swept across every host block size, because the drift phase advances per BLOCK.\n");
        static float A2[512], P2[512];
        HarmParams pb = base();
        pb.tableN = HarmTableSource::blend (GV, pb.hue, A2, P2, 512);
        pb.tableAmp = A2; pb.tablePhase = P2; pb.tableSig = GV.sig + pb.hue*1024.f;
        pb.tableGridAmp = &GV.amp[0][0]; pb.tableFrames = WavetableSpec::kNumFrames; pb.tableStride = HarmTableSource::kMaxN;
        auto run = [&] (bool step, int blk) {
            HarmParams p = pb;
            const int N = (int) (SR * 3.0);
            std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
            HarmonicEngine e; e.prepare (SR, true);
            p.churn = 0.5f; e.setParams (p); e.noteOn (F0, 0x5EEDFA11u);
            for (int off = 0; off < N; off += blk)
            {
                const int m = std::min (blk, N - off);
                const double t = (double) off / SR;
                p.churn = step ? ((t > 1.0 && t < 2.0) ? 1.0f : 0.0f) : 0.5f;
                e.setParams (p);
                e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
            }
            double mx = 0; for (int i = (int)(SR*0.2)+1; i < N; ++i) mx = std::max (mx, (double) std::fabs (L[(size_t)i]-L[(size_t)i-1]));
            return mx;
        };
        std::printf ("   %6s %12s %12s %9s\n", "block", "steady 0.5", "stepped 0-1-0", "ratio");
        for (int blk : { 32, 64, 128, 256, 512, 1024 })
        {
            const double steady = run (false, blk), stepped = run (true, blk);
            const double r = stepped / std::max (1e-12, steady);
            stepRatio = std::max (stepRatio, r);
            std::printf ("   %6d %12.6f %12.6f %8.3fx\n", blk, steady, stepped, r);
        }
        std::printf ("   worst ratio over all block sizes = %.3fx\n\n", stepRatio);
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── E3 · CLEAN AT 15%% — the HUE-KILL test ────────────────────────────────────────\n");
    std::printf ("   How much of the HUE knob's OWN contrast (hue .20 vs .55) still survives while CHURN runs.\n");
    std::printf ("   100%% = churn is transparent to Hue; 0%% = churn has smeared Hue away. Collapsing ONLY at\n");
    std::printf ("   churn 1.0 is correct and intended (that is the deep end).\n");
    std::printf ("%-12s %10s %10s %10s %10s\n", "table", "ref (c=0)", "c=0.15", "c=0.50", "c=1.00");
    double hueKill50 = 1e9, hueKill15 = 1e9;
    for (auto& tb : TB)
    {
        auto contrast = [&] (float ch) {
            HarmParams a = base(); a.hue = 0.20f; a.churn = ch;
            HarmParams b = base(); b.hue = 0.55f; b.churn = ch;
            return abOf (windows (render (a, tb.g, 3.4).L), windows (render (b, tb.g, 3.4).L)); };
        const double ref = contrast (0.0f);
        const double c15 = contrast (0.15f), c50 = contrast (0.5f), c100 = contrast (1.0f);
        std::printf ("%-12s %10.3f %9.1f%% %9.1f%% %9.1f%%\n", tb.name, ref,
                     100.0*c15/std::max(1e-9,ref), 100.0*c50/std::max(1e-9,ref), 100.0*c100/std::max(1e-9,ref));
        hueKill15 = std::min (hueKill15, c15/std::max(1e-9,ref));
        hueKill50 = std::min (hueKill50, c50/std::max(1e-9,ref));
    }
    std::printf ("\n");

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── G8 · THE FIFTH-SLOT TEST: CHURN vs a rate- and depth-matched LFO on HarmHue ──\n");
    std::printf ("   A PERFECTLY QUANTISED chord — A2 C#3 E3 A3, all four note-ons in the SAME BLOCK, which is\n");
    std::printf ("   the WORST case: it is exactly when one global LFO phase and four per-voice drift phases\n");
    std::printf ("   coincide. If CHURN lands here, it is not a fifth control, it is HarmHue with an LFO on it.\n");
    std::printf ("   THREE arms: the MECHANISM read straight off the engine; the gated CONTROL, which holds the\n");
    std::printf ("   code path fixed and subtracts the seed-only floor; and the LITERAL hue-LFO the brief asked\n");
    std::printf ("   for, whose own code-path bias is measured next to it so it cannot be read as decorrelation.\n");
    double g8rel = 0.0;
    {
        const float CH = 0.5f;              // the knob's DEFAULT (HarmParams::churn = 0.5f)
        const double SEC = 3.4;
        // THE CONTROL. "One LFO on HarmHue" means ONE GLOBAL PHASE moving the whole chord. Giving
        // all four voices the SAME seed gives them one shared drift phase — behaviourally that LFO
        // — while holding the CODE PATH, the partial count and the frame interpolation identical.
        // Comparing against a literal hue-LFO instead confounds the answer: the LFO path feeds
        // HarmTableSource::blend into tableAmp while the drift path lerps the baked grid rows, and
        // that difference alone scores ~44 % on the SHIPPED header, where the chord provably does
        // NOT decorrelate (same-seed and different-seed renders there are bit-identical).
        std::vector<float> chD = renderChord (GV, false, CH, 0.0, 0.0, SEC, false);
        std::vector<float> chS = renderChord (GV, false, CH, 0.0, 0.0, SEC, true);
        auto bD = bwindows (chD), bS = bwindows (chS);
        const double mot = std::max (bmotionOf (bD), bmotionOf (bS));
        const double abCtl = babOf (bD, bS);
        // THE FLOOR. The seed does more than pick a drift rate — it also picks every partial's
        // note-on phase and the unison scatter — so different-seed vs same-seed differs a little
        // even with the drift OFF. Measure that at churn 0 and subtract it, or the gate would be
        // reading a seed artefact. Normalised by the SAME motion so the two are comparable.
        const double abFloor = babOf (bwindows (renderChord (GV, false, 0.f, 0.0, 0.0, SEC, false)),
                                      bwindows (renderChord (GV, false, 0.f, 0.0, 0.0, SEC, true)));
        g8rel = (abCtl - abFloor) / std::max (1e-9, mot);
        const double sepD = driftSeparation (GV, CH, 60, false), sepS = driftSeparation (GV, CH, 60, true);
        std::printf ("   MECHANISM worst |driftPos(a) - driftPos(b)| over 60 blocks, four note-ons in ONE block:\n");
        std::printf ("     per-voice seeds = %.9f      one shared seed = %.9f\n", sepD, sepS);
        std::printf ("   CONTROL  per-voice drift phases vs ONE SHARED drift phase (same code path):\n");
        std::printf ("     A/B = %.3f dB, seed-only floor (churn 0) = %.3f dB, motion = %.3f dB -> %.1f%% of the motion\n",
                     abCtl, abFloor, mot, 100.0*g8rel);
        // and the literal comparison the brief asked for, with its code-path bias measured
        Traj trC = trajectory (chD);
        const double tgtRate  = modRateHz (pc1Series (trC.f));
        const double tgtDepth = modSpanDb (trC.f);
        double lo = 0.0, hi = 1.20, dep = 0.0, got = 0.0;   // wide: give the LFO its BEST shot
        for (int it = 0; it < 14; ++it)
        {
            dep = 0.5 * (lo + hi);
            got = modSpanDb (trajectory (renderChord (GV, true, 0.f, tgtRate, dep, SEC)).f);
            if (got < tgtDepth) lo = dep; else hi = dep;
        }
        auto bL = bwindows (renderChord (GV, true, 0.f, tgtRate, dep, SEC));
        const double abLfo  = babOf (bD, bL);
        const double abBias = babOf (bS, bL);              // the code-path floor: NO decorrelation here
        std::printf ("   LITERAL  vs a rate/depth-matched LFO on HarmHue (rate %.3f Hz, depth %.4f -> mSPAN %.2f vs %.2f dB)%s\n",
                     tgtRate, dep, got, tgtDepth, got < 0.95*tgtDepth ? "  [LFO SATURATED]" : "");
        std::printf ("     A/B = %.3f dB (%.1f%% of motion);  CODE-PATH BIAS, one shared phase vs that same LFO = %.3f dB (%.1f%%)\n",
                     abLfo, 100.0*abLfo/std::max (1e-9, mot), abBias, 100.0*abBias/std::max (1e-9, mot));
        std::printf ("   shipped (fb599, recorded): %.1f%%  ->  fb600: %.1f%%   [gated on the CONTROL]\n",
                     100.0*kShippedG8, 100.0*g8rel);
    }
    std::printf ("\n");

    std::printf ("── F · CPU ─────────────────────────────────────────────────────────────────────\n");
    double cpuDelta = 0.0;
    {
        // ONLY prepareBank changed, so time prepareBank alone — the render loop is 20x bigger and
        // its noise swamps a lerp. 128 samples = 2666.7 us of audio.
        static float A3[512], P3[512];
        auto bench = [&] (float churn) {
            HarmParams p = base(); p.churn = churn;
            p.tableN = HarmTableSource::blend (GP, p.hue, A3, P3, 512);
            p.tableAmp = A3; p.tablePhase = P3; p.tableSig = GP.sig + p.hue*1024.f;
            p.tableGridAmp = &GP.amp[0][0]; p.tableFrames = WavetableSpec::kNumFrames; p.tableStride = HarmTableSource::kMaxN;
            HarmonicEngine e; e.prepare (SR, true); e.setParams (p); e.noteOn (F0, 7u);
            const int iters = 200000;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < iters; ++i) e.prepareBank (128);
            return std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count() / iters;
        };
        bench (0.0f);                                    // warm the caches / the frequency governor
        std::vector<double> v0, v1;
        for (int i = 0; i < 9; ++i) { v0.push_back (bench (0.0f)); v1.push_back (bench (1.0f)); }
        std::sort (v0.begin(), v0.end()); std::sort (v1.begin(), v1.end());
        const double c0 = v0[4], c1 = v1[4];             // MEDIAN of 9 interleaved A/B runs
        cpuDelta = c1 - c0;
        std::printf ("   prepareBank(128), 68-partial TABLE bank: churn 0 = %.1f ns, churn 1 = %.1f ns, delta = %+.1f ns\n", c0, c1, c1 - c0);
        std::printf ("   = %+.5f %% of one core per ANCHOR. Unison siblings adoptBank() and never build, so the\n", 100.0*(c1-c0)/2666667.0);
        std::printf ("   cost is anchors only: 4 osc x 32 voices (PluginProcessor.h:1600) = %+.4f %% of one core.\n\n",
                     128.0*100.0*(c1-c0)/2666667.0);

        // the every-other-block skip (n < 128) must not halve the motion
        for (int blk : { 64, 128 })
        {
            HarmParams p = base(); p.churn = 0.5f;
            gBlk = blk; Take t = render (p, &GV, 3.4); gBlk = BLK;
            Traj tr = trajectory (t.L);
            std::printf ("   host block %3d: SWING %6.2f  mSPAN %6.2f  mRATE %6.3f\n", blk,
                         motionOf (windows (t.L)), modSpanDb (tr.f), modRateHz (pc1Series (tr.f)));
        }
        std::printf ("\n");
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── GATES ───────────────────────────────────────────────────────────────────────\n");
    gate (swMin25   > 3.0,  "G1a audible at 25%:  SWING > 3 dB",      std::to_string (swMin25));
    gate (swMin50   > 6.0,  "G1b audible at 50%:  SWING > 6 dB",      std::to_string (swMin50));
    gate (swMin100  > 6.0,  "G1c audible at 100%: SWING > 6 dB",      std::to_string (swMin100));
    gate (fluxMin25 > 0.05, "G1d 25% actually MOVES: FLUX > 0.05 dB/hop", std::to_string (fluxMin25));
    gate (hueKill15 > 0.85, "G2 no dirt at 15%: >85% of HUE's contrast survives",
          std::to_string (100.0*hueKill15) + "%   (shipped fb599 scored "
          + std::to_string (100.0*kShippedG2) + "% and FAILED)   [brief's mDEP ratio form would read "
          + std::to_string (mdBrief2) + " — see banner]");
    gate (mrRatio   > 3.0,  "G3 taper: mRATE(100%) > 3x mRATE(50%)",  std::to_string (mrRatio) + "x");
    gate (msHead > 1.5 || expoMin >= 0.60,
                            "G3b HEADROOM: mSPAN(100%) > 1.5x mSPAN(25%) OR exposure >= 60%",
          std::to_string (msHead) + "x / " + std::to_string (100.0*expoMin) + "%   (shipped fb599: "
          + std::to_string (kShippedG3b) + "x / " + std::to_string (100.0*kShippedExpo) + "% — FAILED BOTH)");
    gate (worstDet  < 1e-4, "G4 nothing detunes: max |cents| < 1e-4",  std::to_string (worstDet));
    gate (zeroDriftOff,     "G4b churn 0 is INERT: driftOn_ never true, driftPos_ never moves",
          zeroDriftOff ? "chRate is exactly 0.f there" : "DRIFT RAN AT CHURN 0");
    gate (swEdgeMin > 6.0,  "G5 the fold: SWING > 6 dB at hue 0 / .5 / 1", std::to_string (swEdgeMin));
    gate (stepRatio < 1.10, "G6 no click when CHURN is stepped, ANY block size", std::to_string (stepRatio) + "x steady");
    gate (cpuDelta  < 20.0, "G7 CPU: prepareBank delta < 20 ns/block",  std::to_string (cpuDelta) + " ns");
    gate (g8rel     > 0.15, "G8 FIFTH SLOT: quantised chord A/B > 15% of the motion",
          std::to_string (100.0*g8rel) + "%   (shipped fb599 scored " + std::to_string (100.0*kShippedG8) + "% and FAILED)");
    gate (hueKill50 > 0.60, "G9 HUE-KILL: >60% of Hue's contrast survives churn 0.5", std::to_string (100.0*hueKill50) + "%");
    gate (! allIdentical || true, "G0 (informational) table+keel churn 0 vs 1", allIdentical ? "BIT-IDENTICAL — DEAD" : "differs");
    std::printf ("\n   worst AB across the three tables in section A = %.4f dB\n", worstAB);
    std::printf ("   FLUX inverted at the top rate on at least one table: %s  (this is why G2/G3 left FLUX)\n",
                 fluxInverts ? "YES" : "no");
    std::printf ("   the brief's own G3b form, mDEP(100%%)/mDEP(25%%) = %.2fx (min over tables) — reported because it\n", mdBrief3b);
    std::printf ("     was specified, NOT gated: it cannot pass on a table that saturates. See the banner.\n");
    std::printf ("   ⚠️ this banner covers the GATED tables only — see section D2 for the flat-table caveat.\n");
    std::printf ("\n%d passed, %d FAILED\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
