// ─────────────────────────────────────────────────────────────────────────────
// phaser_cert — the perceptual certification harness for the FX Phaser device.
//
//   clang++ -O2 -std=c++17 -I <repo>/plugins/TerrainInstrument/Tests/shim \
//           -I <repo>/plugins/TerrainInstrument/Source \
//           -I <this dir> phaser_cert.cpp -o /tmp/phaser_cert && /tmp/phaser_cert
//
// ⚠️ THE METRIC PROBLEM THIS HARNESS EXISTS TO SOLVE
// A phaser is ALL-PASS. Its wet magnitude spectrum is FLAT by construction, so the project's
// usual "magnitude-spectrum distance" dramaticism metric reports ~0 dB for changes that are
// enormous to the ear, and sample-difference RMS reports ~100 % for changes that are inaudible
// (fb282: an allpass change measured 102 % divergence, the real magnitude change was 0.02 dB,
// and Max heard NOTHING). Every gate below is therefore built on NOTCH GEOMETRY of the summed
// output and on the TRAJECTORY of that geometry through time:
//     notch count · notch centre frequencies · spacing ratios · notch depth · inter-notch peak
//     gain · sweep range in octaves · rise/fall asymmetry · monotonic spectral drift ·
//     step flatness · non-harmonic modulation lines · FM sidebands · pitch deviation in cents.
//
// ⚠️ AND THE LAW THIS HARNESS CANNOT ENFORCE (fb373): a green DSP harness proves the ENGINE
// works. It NEVER proves the plugin reaches it. The UI→param→DSP round trip is gated separately.
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainPhaserFx.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace {

constexpr float FS = 48000.0f;
using P  = tw::TerrainPhaserFx::Params;
using FX = tw::TerrainPhaserFx;

int gPass = 0, gFail = 0;

void section (const char* s) { std::printf ("\n[%s]\n", s); }
void gate (const char* what, bool ok, const std::string& detail)
{
    if (ok) { ++gPass; std::printf ("  ok    %-54s %s\n", what, detail.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-54s %s\n", what, detail.c_str()); }
}
std::string fmt (const char* f, double v) { char b[128]; std::snprintf (b, sizeof b, f, v); return b; }
std::string fmt2 (const char* f, double a, double b) { char c[160]; std::snprintf (c, sizeof c, f, a, b); return c; }
double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-14)); }

// ═════ FFT ═══════════════════════════════════════════════════════════════════
void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

// exact single-frequency DFT (used only to refine a deep null, where bin resolution matters)
double dftMag (const std::vector<float>& x, double hz, double fs = FS)
{
    const double w = 2.0 * M_PI * hz / fs;
    const double c = std::cos (w), s = std::sin (w);
    double cr = 1.0, ci = 0.0, ar = 0.0, ai = 0.0;
    for (float v : x) { ar += v * cr; ai -= v * ci; const double nc = cr * c - ci * s; ci = cr * s + ci * c; cr = nc; }
    return std::sqrt (ar * ar + ai * ai);
}

// ═════ signals ═══════════════════════════════════════════════════════════════
std::vector<float> noiseSig (int n, float rms = 0.05f, uint32_t seed = 22222u)
{
    std::vector<float> x ((size_t) n); uint32_t st = seed;
    for (int i = 0; i < n; ++i) { st = st * 1664525u + 1013904223u; x[(size_t) i] = ((float) (st >> 8) / 8388608.0f) - 1.0f; }
    double a = 0; for (float v : x) a += (double) v * v;
    const float g = rms / (float) std::sqrt (a / n); for (float& v : x) v *= g;
    return x;
}
std::vector<float> toneSig (int n, float hz, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = std::sin (6.2831853f * hz * i / FS);
    for (float& v : x) v *= rms * 1.41421f;
    return x;
}
// amplitude-modulated noise: the honest probe for anything envelope-driven (fb345 probe craft —
// a static sine has no envelope for a follower to follow).
std::vector<float> stabSig (int n, float rms = 0.05f, float perSec = 0.5f)
{
    auto x = noiseSig (n, rms);
    const int per = (int) (FS * perSec);
    for (int i = 0; i < n; ++i)
    { const float ph = (float) (i % per) / (float) per; x[(size_t) i] *= 0.03f + std::exp (-ph * 5.0f); }
    return x;
}

// ═════ engine drivers ════════════════════════════════════════════════════════
struct Out { std::vector<float> l, r; };

Out run (P p, const std::vector<float>& in, float fs = FS)
{
    FX e; e.prepare (fs, 128);
    Out o; o.l = in; o.r = in;
    const int n = (int) in.size();
    for (int i = 0; i < n; i += 128)
    { const int b = std::min (128, n - i); e.setParams (p); e.processStereo (&o.l[(size_t) i], &o.r[(size_t) i], b); }
    return o;
}

struct IR { std::vector<float> l, r; };

// Impulse response AFTER the engine has settled (smoothers, dip, seeding). With Depth 0 and a
// static sweep the device is LTI between coefficient updates, so this is the EXACT transfer
// function — the only honest way to read a notch that is 80 dB deep.
IR impulse (P p, int n = 32768, float amp = 1.0e-3f, float settleSec = 0.7f, float fs = FS)
{
    FX e; e.prepare (fs, 128);
    std::vector<float> z (128, 0.0f), z2 (128, 0.0f);
    const int s = (int) (fs * settleSec);
    for (int i = 0; i < s; i += 128)
    { std::fill (z.begin(), z.end(), 0.0f); std::fill (z2.begin(), z2.end(), 0.0f);
      e.setParams (p); e.processStereo (z.data(), z2.data(), 128); }
    IR ir; ir.l.assign ((size_t) n, 0.0f); ir.r.assign ((size_t) n, 0.0f);
    ir.l[0] = amp; ir.r[0] = amp;
    for (int i = 0; i < n; i += 128)
    { const int b = std::min (128, n - i); e.processStereo (&ir.l[(size_t) i], &ir.r[(size_t) i], b); }
    const float inv = 1.0f / amp;
    for (auto& v : ir.l) v *= inv;
    for (auto& v : ir.r) v *= inv;
    return ir;
}

// magnitude transfer function, dB, on a log frequency grid
struct TF { std::vector<double> f, g; };

TF tfOf (const std::vector<float>& ir, int pts = 600, double f0 = 30.0, double f1 = 18000.0,
         double fs = FS)
{
    size_t nf = 1; while (nf < ir.size()) nf <<= 1;
    std::vector<std::complex<double>> a (nf, std::complex<double> (0, 0));
    for (size_t i = 0; i < ir.size(); ++i) a[i] = ir[i];
    fft (a);
    std::vector<double> mag (nf / 2);
    for (size_t i = 0; i < nf / 2; ++i) mag[i] = std::abs (a[i]);
    TF t;
    const double binHz = fs / (double) nf;
    for (int i = 0; i < pts; ++i)
    {
        const double f = f0 * std::pow (f1 / f0, (double) i / (pts - 1));
        const double b = f / binHz;
        const size_t b0 = (size_t) b; const double fr = b - (double) b0;
        const double m = (b0 + 1 < mag.size()) ? (mag[b0] * (1 - fr) + mag[b0 + 1] * fr) : mag[mag.size() - 1];
        t.f.push_back (f); t.g.push_back (db (m));
    }
    return t;
}

// ═════ comb geometry ═════════════════════════════════════════════════════════
struct Comb
{
    int    count = 0;
    double lowOct = 0, meanSpacOct = 0, spacCV = 0;
    double peakDb = 0, depthDb = 0;
    std::vector<double> notchHz;
};

Comb combOf (const TF& t, double prom = 3.0, double lo = 60.0, double hi = 14000.0)
{
    Comb c; c.peakDb = -99; c.depthDb = 0;
    const int n = (int) t.f.size();
    for (int i = 3; i < n - 3; ++i)
    {
        if (t.f[(size_t) i] < lo || t.f[(size_t) i] > hi) continue;
        const double v = t.g[(size_t) i];
        bool minimum = true;
        for (int k = -3; k <= 3; ++k) if (k && t.g[(size_t) (i + k)] < v) { minimum = false; break; }
        if (! minimum) continue;
        // prominence: rise on both sides
        double lmax = v, rmax = v;
        for (int k = i; k > 0 && t.f[(size_t) k] > lo * 0.5; --k) { lmax = std::max (lmax, t.g[(size_t) k]); if (t.g[(size_t) k] < v) break; }
        for (int k = i; k < n && t.f[(size_t) k] < hi * 2.0; ++k) { rmax = std::max (rmax, t.g[(size_t) k]); if (t.g[(size_t) k] < v) break; }
        if (std::min (lmax, rmax) - v < prom) continue;
        c.notchHz.push_back (t.f[(size_t) i]);
        c.depthDb = std::min (c.depthDb, v);
    }
    for (int i = 0; i < n; ++i)
        if (t.f[(size_t) i] >= lo && t.f[(size_t) i] <= hi) c.peakDb = std::max (c.peakDb, t.g[(size_t) i]);
    c.count = (int) c.notchHz.size();
    if (c.count) c.lowOct = std::log2 (c.notchHz[0]);
    if (c.count >= 2)
    {
        std::vector<double> sp;
        for (int i = 1; i < c.count; ++i) sp.push_back (std::log2 (c.notchHz[(size_t) i] / c.notchHz[(size_t) i - 1]));
        double m = 0; for (double v : sp) m += v; m /= (double) sp.size();
        c.meanSpacOct = m;
        double var = 0; for (double v : sp) var += (v - m) * (v - m);
        c.spacCV = (m > 1e-6 && sp.size() > 1) ? std::sqrt (var / (double) sp.size()) / m : 0.0;
    }
    return c;
}

// the deepest null, refined below FFT bin resolution — this IS the Mix-1.0 dry-residual number
double deepestNullDb (const std::vector<float>& ir, double lo = 80.0, double hi = 12000.0)
{
    TF t = tfOf (ir, 2000, lo, hi);
    int bi = 0; double bv = 1e9;
    for (size_t i = 0; i < t.g.size(); ++i) if (t.g[i] < bv) { bv = t.g[i]; bi = (int) i; }
    double a = t.f[(size_t) std::max (0, bi - 2)], b = t.f[(size_t) std::min ((int) t.f.size() - 1, bi + 2)];
    for (int it = 0; it < 60; ++it)                       // golden-section on log f
    {
        const double m1 = a * std::pow (b / a, 0.382), m2 = a * std::pow (b / a, 0.618);
        if (dftMag (ir, m1) < dftMag (ir, m2)) b = m2; else a = m1;
    }
    return db (dftMag (ir, std::sqrt (a * b)));
}

// ═════ trajectory tracking (the motion discriminators live here) ═════════════
struct Traj
{
    std::vector<double> oct;        // comb PATTERN position, octaves relative to the run mean
    std::vector<double> absOct;     // deepest-notch log2(Hz) per frame (reported, not gated)
    std::vector<double> envDb;      // input frame level, dB
    std::vector<double> midDb;      // output mid-band level, dB
    std::vector<double> mean;       // the run's mean log-band gain curve
    std::vector<std::vector<double>> curve;
    double hopSec = 0;
    int    nb = 0;
    double bandOct = 0;
};

// normalised, mean-removed cross-correlation of two log-band gain curves, with parabolic refine.
// Returns how far the pattern in A sits ABOVE the pattern in B, in octaves.
// (The first version correlated RAW dB curves, so the large negative mean dominated and the
//  "best shift" tracked overlap length, not pattern alignment.)
double bestShiftOct (const std::vector<double>& A, const std::vector<double>& B,
                     int nb, double bandOct, double* peakCorr = nullptr,
                     int maxShift = 6, int minOverlap = 0)
{
    if (minOverlap <= 0) minOverlap = nb - 8;
    std::vector<double> rr;
    std::vector<int>    ss;
    for (int sft = -maxShift; sft <= maxShift; ++sft)
    {
        const int lo = std::max (0, -sft), hi = std::min (nb, nb - sft);
        if (hi - lo < minOverlap) { continue; }
        double ma = 0, mb = 0; int c = 0;
        for (int b = lo; b < hi; ++b) { ma += A[(size_t) (b + sft)]; mb += B[(size_t) b]; ++c; }
        ma /= c; mb /= c;
        double sab = 0, sa = 0, sb2 = 0;
        for (int b = lo; b < hi; ++b)
        { const double da = A[(size_t) (b + sft)] - ma, dbb = B[(size_t) b] - mb;
          sab += da * dbb; sa += da * da; sb2 += dbb * dbb; }
        rr.push_back ((sa > 1e-12 && sb2 > 1e-12) ? sab / std::sqrt (sa * sb2) : -2.0);
        ss.push_back (sft);
    }
    if (rr.empty()) { if (peakCorr) *peakCorr = 0; return 0; }
    size_t bi = 0; for (size_t i = 1; i < rr.size(); ++i) if (rr[i] > rr[bi]) bi = i;
    double d = 0;
    if (bi > 0 && bi + 1 < rr.size())
    { const double den = rr[bi - 1] - 2 * rr[bi] + rr[bi + 1];
      if (std::fabs (den) > 1e-9) d = 0.5 * (rr[bi - 1] - rr[bi + 1]) / den;
      if (d > 1.0) d = 1.0; if (d < -1.0) d = -1.0; }
    if (peakCorr) *peakCorr = rr[bi];
    return ((double) ss[bi] + d) * bandOct;
}

Traj trackTraj (const Out& o, const std::vector<float>& in,
                double flo = 120.0, double fhi = 14000.0, int nb = 64)
{
    Traj tr; tr.nb = nb;
    const int F = 3072, H = 512;
    tr.hopSec = (double) H / FS;
    tr.bandOct = std::log2 (fhi / flo) / (nb - 1);
    std::vector<double> win ((size_t) F);
    for (int i = 0; i < F; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (F - 1));
    const size_t nf = 4096;
    const double binHz = FS / (double) nf;
    for (size_t st = 0; st + F < in.size(); st += H)
    {
        std::vector<std::complex<double>> A (nf, 0.0), B (nf, 0.0);
        double e = 0;
        for (int i = 0; i < F; ++i)
        {
            const double xi = in[st + i] * win[(size_t) i];
            const double yi = 0.5 * (o.l[st + i] + o.r[st + i]) * win[(size_t) i];   // MONO SUM
            A[(size_t) i] = xi; B[(size_t) i] = yi; e += in[st + i] * in[st + i];
        }
        fft (A); fft (B);
        std::vector<double> g ((size_t) nb, 0.0);
        for (int b = 0; b < nb; ++b)
        {
            const double fc = flo * std::pow (fhi / flo, (double) b / (nb - 1));
            const double f1 = fc * std::pow (2.0, -tr.bandOct * 0.6), f2 = fc * std::pow (2.0, tr.bandOct * 0.6);
            size_t i1 = (size_t) std::max (1.0, f1 / binHz), i2 = (size_t) std::min ((double) nf / 2 - 1, f2 / binHz);
            if (i2 <= i1) i2 = i1 + 1;
            double sx = 0, sy = 0;
            for (size_t i = i1; i <= i2; ++i) { sx += std::norm (A[i]); sy += std::norm (B[i]); }
            g[(size_t) b] = 10.0 * std::log10 (std::max (sy, 1e-30) / std::max (sx, 1e-30));
        }
        std::vector<double> sm ((size_t) nb);
        for (int b = 0; b < nb; ++b)
        { int c = 0; double a2 = 0;
          for (int k = -1; k <= 1; ++k) if (b + k >= 0 && b + k < nb) { a2 += g[(size_t) (b + k)]; ++c; }
          sm[(size_t) b] = a2 / c; }
        int bi = 0; double bv = 1e9;
        for (int b = 1; b < nb - 1; ++b) if (sm[(size_t) b] < bv) { bv = sm[(size_t) b]; bi = b; }
        tr.absOct.push_back (std::log2 (flo) + bi * tr.bandOct);
        tr.curve.push_back (sm);
        tr.envDb.push_back (10.0 * std::log10 (std::max (e / F, 1e-20)));
        double mid = 0; for (int b = nb / 4; b < 3 * nb / 4; ++b) mid += sm[(size_t) b];
        tr.midDb.push_back (mid / (nb / 2));
    }
    // the run's mean curve is the reference the pattern position is measured against
    tr.mean.assign ((size_t) nb, 0.0);
    for (auto& c : tr.curve) for (int b = 0; b < nb; ++b) tr.mean[(size_t) b] += c[(size_t) b];
    if (! tr.curve.empty()) for (double& v : tr.mean) v /= (double) tr.curve.size();
    double emax = -1e9; for (double v : tr.envDb) emax = std::max (emax, v);
    double acc = 0; tr.oct.push_back (0.0);
    for (size_t i = 1; i < tr.curve.size(); ++i)
    { if (tr.envDb[i] > emax - 40.0 && tr.envDb[i - 1] > emax - 40.0)
          acc += bestShiftOct (tr.curve[i], tr.curve[i - 1], nb, tr.bandOct, nullptr, 32, 30);
      tr.oct.push_back (acc); }
    return tr;
}

double trajRange (const Traj& t, size_t skip = 6)
{
    if (t.oct.size() <= skip + 4) return 0;
    double lo = 1e9, hi = -1e9;
    for (size_t i = skip; i < t.oct.size(); ++i) { lo = std::min (lo, t.oct[i]); hi = std::max (hi, t.oct[i]); }
    return hi - lo;
}
// rise time / fall time — the Kraut discriminator (LDR attack/decay asymmetry)
double riseFall (const Traj& t, size_t skip = 6)
{
    int up = 0, dn = 0;
    for (size_t i = skip + 1; i < t.oct.size(); ++i)
    { const double d = t.oct[i] - t.oct[i - 1];
      if (d > 0.02) ++up; else if (d < -0.02) ++dn; }
    if (! up || ! dn) return 1.0;
    return (double) up / (double) dn;
}
// step flatness — the Steps discriminator (piecewise-constant trajectory)
double stepFrac (const Traj& t, size_t skip = 6)
{
    std::vector<double> d;
    for (size_t i = skip + 1; i < t.oct.size(); ++i) d.push_back (std::fabs (t.oct[i] - t.oct[i - 1]));
    if (d.size() < 8) return 0;
    double mx = 0; for (double v : d) mx = std::max (mx, v);
    if (mx < 0.6 * t.bandOct) return 0;                  // nothing moved at all: not "held"
    int c = 0; for (double v : d) if (v < 0.6 * t.bandOct) ++c;
    return (double) c / (double) d.size();
}
// monotone spectral drift — the Barber discriminator
double monoFrac (const Traj& t, size_t skip = 6, double* meanShiftOct = nullptr)
{
    int pos = 0, neg = 0; double acc = 0; int n = 0;
    for (size_t i = skip + 1; i < t.curve.size(); ++i)
    {
        const double sh = bestShiftOct (t.curve[i], t.curve[i - 1], t.nb, t.bandOct, nullptr, 24, 36);
        if (std::fabs (sh) > 0.01) { if (sh > 0) ++pos; else ++neg; acc += sh; ++n; }
    }
    if (meanShiftOct) *meanShiftOct = n ? acc / n : 0.0;
    const int tot = pos + neg;
    if (tot < 6) return 0.5;
    return std::max ((double) pos, (double) neg) / (double) tot;
}
// RIGIDITY — does the comb PATTERN translate rigidly, or does its shape change? A single sweep
// generator moves one comb bodily (rigid, r -> 1). Two generators sliding through each other
// change the shape itself, so even the best shift leaves residual. This is the Duo tell, and it
// needs no modulation-spectrum peak picking to work.
double rigidity (const Traj& t, int lag = 4, size_t skip = 6)
{
    double acc = 0; int n = 0;
    for (size_t i = skip + (size_t) lag; i < t.curve.size(); ++i)
    { double r = 0; bestShiftOct (t.curve[i], t.curve[i - (size_t) lag], t.nb, t.bandOct, &r, 24, 36);
      if (r > -1.5) { acc += r; ++n; } }
    return n ? acc / n : 0.0;
}
double derivCrest (const Traj& t, size_t skip = 6)
{
    std::vector<double> d;
    for (size_t i = skip + 1; i < t.oct.size(); ++i) d.push_back (std::fabs (t.oct[i] - t.oct[i - 1]));
    if (d.size() < 8) return 1.0;
    double m = 0, mx = 0; for (double v : d) { m += v; mx = std::max (mx, v); }
    m /= (double) d.size();
    return m > 1e-6 ? mx / m : 1.0;
}
double monoRipple (const Traj& t, size_t skip = 6)
{
    double acc = 0; int n = 0;
    for (size_t i = skip; i < t.curve.size(); ++i)
    { double lo = 1e9, hi = -1e9; for (double v : t.curve[i]) { lo = std::min (lo, v); hi = std::max (hi, v); }
      acc += hi - lo; ++n; }
    return n ? acc / n : 0.0;
}
// mean size of a trajectory step (separates a random walk from a jumping register)
double meanStepOct (const Traj& t, size_t skip = 6)
{
    double acc = 0; int n = 0;
    for (size_t i = skip + 1; i < t.oct.size(); ++i)
    { const double d = std::fabs (t.oct[i] - t.oct[i - 1]); if (d > 0.02) { acc += d; ++n; } }
    return n ? acc / n : 0.0;
}
// non-harmonic modulation line — the Duo discriminator (two sweep generators beating)
double nonHarmDb (const Traj& t, double* f0Hz = nullptr)
{
    const int n = (int) t.midDb.size(); if (n < 24) return -99;
    std::vector<double> x (t.midDb.begin(), t.midDb.end());
    double m = 0; for (double v : x) m += v; m /= n; for (double& v : x) v -= m;
    for (int i = 0; i < n; ++i) x[(size_t) i] *= 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (n - 1));
    const int K = 512; std::vector<double> mag ((size_t) K, 0.0);
    const double fsm = 1.0 / t.hopSec;
    for (int k = 1; k < K; ++k)
    {
        const double f = fsm * k / (2.0 * K);
        double ar = 0, ai = 0;
        for (int i = 0; i < n; ++i) { const double w = 2.0 * M_PI * f * i / fsm; ar += x[(size_t) i] * std::cos (w); ai -= x[(size_t) i] * std::sin (w); }
        mag[(size_t) k] = std::sqrt (ar * ar + ai * ai);
    }
    int pk = 1; for (int k = 2; k < K; ++k) if (mag[(size_t) k] > mag[(size_t) pk]) pk = k;
    const double fpk = fsm * pk / (2.0 * K);
    if (f0Hz) *f0Hz = fpk;
    double best = 0;
    for (int k = 2; k < K; ++k)
    {
        const double f = fsm * k / (2.0 * K);
        bool harm = false;
        for (int h = 1; h <= 8; ++h) if (std::fabs (f - h * fpk) < 0.10 * fpk) harm = true;
        if (! harm) best = std::max (best, mag[(size_t) k]);
    }
    return 20.0 * std::log10 (std::max (best, 1e-12) / std::max (mag[(size_t) pk], 1e-12));
}
double rankCorr (const std::vector<double>& a, const std::vector<double>& b, size_t skip = 6)
{
    const size_t n = std::min (a.size(), b.size()); if (n <= skip + 8) return 0;
    std::vector<double> x (a.begin() + (long) skip, a.begin() + (long) n);
    std::vector<double> y (b.begin() + (long) skip, b.begin() + (long) n);
    auto rank = [] (std::vector<double> v) {
        const size_t m = v.size();
        std::vector<size_t> idx (m); for (size_t i = 0; i < m; ++i) idx[i] = i;
        std::sort (idx.begin(), idx.end(), [&] (size_t p, size_t q) { return v[p] < v[q]; });
        std::vector<double> r (m); for (size_t i = 0; i < m; ++i) r[idx[i]] = (double) i;
        return r; };
    auto rx = rank (x), ry = rank (y);
    double mx = 0, my = 0; for (size_t i = 0; i < rx.size(); ++i) { mx += rx[i]; my += ry[i]; }
    mx /= rx.size(); my /= ry.size();
    double sab = 0, sa = 0, sb = 0;
    for (size_t i = 0; i < rx.size(); ++i)
    { const double da = rx[i] - mx, dbb = ry[i] - my; sab += da * dbb; sa += da * da; sb += dbb * dbb; }
    return (sa > 1e-12 && sb > 1e-12) ? sab / std::sqrt (sa * sb) : 0.0;
}
double corrOf (const std::vector<double>& a, const std::vector<double>& b, size_t skip = 6)
{
    const size_t n = std::min (a.size(), b.size()); if (n <= skip + 6) return 0;
    double ma = 0, mb = 0; size_t c = 0;
    for (size_t i = skip; i < n; ++i) { ma += a[i]; mb += b[i]; ++c; }
    ma /= c; mb /= c;
    double sab = 0, sa = 0, sb = 0;
    for (size_t i = skip; i < n; ++i) { const double da = a[i] - ma, dbb = b[i] - mb; sab += da * dbb; sa += da * da; sb += dbb * dbb; }
    return (sa > 1e-12 && sb > 1e-12) ? sab / std::sqrt (sa * sb) : 0.0;
}

// ═════ misc measurements ═════════════════════════════════════════════════════
double rmsOf (const std::vector<float>& v, size_t from = 0)
{ double a = 0; size_t n = 0; for (size_t i = from; i < v.size(); ++i) { a += (double) v[i] * v[i]; ++n; } return n ? std::sqrt (a / n) : 0; }

// peak level of everything above 6 kHz — a sine in, so ANY HF is a click / a switching artefact
double hfClickDbfs (const std::vector<float>& v, size_t from)
{
    float s1 = 0, s2 = 0, s3 = 0, s4 = 0; double pk = 0;
    const float a = 1.0f - std::exp (-6.2831853f * 6000.0f / FS);
    for (size_t i = 0; i < v.size(); ++i)
    {
        s1 += a * (v[i] - s1); const float h1 = v[i] - s1;
        s2 += a * (h1   - s2); const float h2 = h1   - s2;
        s3 += a * (h2   - s3); const float h3 = h2   - s3;
        s4 += a * (h3   - s4); const float h4 = h3   - s4;
        if (i > from) pk = std::max (pk, (double) std::fabs (h4));
    }
    return db (pk);
}
double thdDb (const std::vector<float>& y, double f0)
{
    const double fund = dftMag (y, f0);
    double h = 0; for (int k = 2; k <= 8; ++k) { const double m = dftMag (y, f0 * k); h += m * m; }
    return db (std::sqrt (h) / std::max (fund, 1e-12));
}
// instantaneous pitch deviation of a carrier, in cents (the vibrato tell)
double centsDev (const std::vector<float>& y, double f0)
{
    const int F = 1024, H = 128;
    std::vector<double> ph;
    for (size_t st = (size_t) (FS * 0.5f); st + F < y.size(); st += H)
    {
        double ar = 0, ai = 0;
        for (int i = 0; i < F; ++i)
        { const double w = 2.0 * M_PI * f0 * (st + i) / FS; const double win = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (F - 1));
          ar += y[st + i] * win * std::cos (w); ai -= y[st + i] * win * std::sin (w); }
        ph.push_back (std::atan2 (ai, ar));
    }
    if (ph.size() < 20) return 0;
    std::vector<double> d;
    for (size_t i = 1; i < ph.size(); ++i)
    { double dd = ph[i] - ph[i - 1]; while (dd > M_PI) dd -= 2 * M_PI; while (dd < -M_PI) dd += 2 * M_PI;
      d.push_back (dd * FS / (2.0 * M_PI * H)); }                 // Hz offset
    std::sort (d.begin(), d.end());
    const double loHz = d[(size_t) (d.size() * 0.02)], hiHz = d[(size_t) (d.size() * 0.98)];
    return 1200.0 * std::log2 ((f0 + hiHz) / std::max (1.0, f0 + loHz)) * 0.5;
}

double lrDb (const Out& o, size_t from = 24000)
{
    std::vector<float> d (o.l.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = o.l[i] - o.r[i];
    return db (rmsOf (d, from) / std::max (1e-9, rmsOf (o.l, from)));
}
std::string vec (const std::vector<double>& v, const char* f = "%.2f")
{
    std::string s2 = "[";
    for (size_t i = 0; i < v.size(); ++i) { s2 += fmt (f, v[i]); if (i + 1 < v.size()) s2 += " "; }
    return s2 + "]";
}

P base()
{
    P p; p.type = 0; p.character = 0;
    p.rate = 0.35f; p.depth = 0.0f; p.feedback = 0.0f; p.mix = 1.0f;
    p.b1 = 0.5f; p.b2 = 0.5f; p.b3 = 0.5f; p.b4 = 0.0f; p.b5 = 0.5f;
    p.b6 = 0.05f; p.b7 = 0.0f; p.b8 = 0.0f;
    p.tempoSync = false; p.bpm = 120.0;
    return p;
}

const char* TN (int t) { return FX::typeNames()[t]; }
const char* CN (int t, int c) { return FX::charNames (t)[c]; }

// ═════ the cross-type feature vector ═════════════════════════════════════════
struct Feat
{
    static constexpr int N = 19;
    double v[N] {};
    static const char* name (int i)
    {
        static const char* M[N] = { "notch count", "lowest notch (oct)", "notch spacing (oct)",
            "spacing CV (inharmonicity)", "inter-notch peak (dB)", "notch depth (dB)",
            "sweep range (oct)", "rise/fall (log2)", "monotone drift", "step flatness",
            "comb rigidity", "mean step (oct)", "envelope correlation", "FM sideband (dBc)",
            "loop THD (dB)", "L/R decorrelation (dB)", "sweep centre (oct)",
            "LFO shape (deriv crest)", "mono-sum ripple (dB)" };
        return M[i];
    }
    static double jnd (int i)
    {
        static const double J[N] = { 1.0, 0.7, 0.35, 0.25, 4.0, 6.0,
                                     0.8, 0.6, 0.30, 0.28, 0.12, 0.30, 0.40, 12.0, 8.0, 6.0, 0.7,
                                     0.45, 4.0 };
        return J[i];
    }
};

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
int main()
{
    std::printf ("\n══ PHASER FX ENGINE — certification ══  (bus program -26 dBFS, fs %.0f)\n", FS);
    std::printf ("   %d Types x %d Characters. Metrics are NOTCH GEOMETRY, never wet magnitude.\n",
                 FX::kNumTypes, FX::kNumChars);

    // ─────────────────────────────────────────────────────────────────────────
    section ("A. The Mix law — and how you measure 'zero dry' on an ALL-PASS effect");
    std::printf ("   At Mix 1.0 the output is x + apBlend*(A{x}-x). At a notch A{x} = -x, so the\n"
                 "   output is EXACTLY the bypass-dry leak d. The measured null depth IS the residual.\n");
    {
        auto p = base(); p.mix = 0.0f; p.type = 0; p.character = 1; p.feedback = 0.8f; p.depth = 0.7f;
        auto in = noiseSig (48000); auto o = run (p, in);
        double worst = 0; for (size_t i = 2000; i < in.size(); ++i) worst = std::max (worst, (double) std::fabs (o.l[i] - in[i]));
        gate ("Mix 0 is bit-transparent", worst < 1e-6, fmt ("worst sample delta %.3e", worst));
    }
    {
        double worstResid = -999; int worstT = 0;
        for (int t = 0; t < FX::kNumTypes; ++t)
        {
            auto p = base(); p.type = t; p.character = (t == 5 ? 0 : 0); p.mix = 1.0f;
            p.depth = (t == 6 ? 1.0f : 0.0f); p.feedback = 0.0f; p.b1 = 0.45f; p.b3 = 0.4f;
            if (t == 6) p.rate = 0.0f;
            const double d = deepestNullDb (impulse (p, t == 6 ? 16384 : 32768).l, 100.0, 8000.0);
            if (d > worstResid) { worstResid = d; worstT = t; }
        }
        gate ("Mix 1.0 dry residual < -60 dB (all 9 Types)", worstResid < -60.0,
              fmt ("worst %.1f dB", worstResid) + std::string (" on ") + TN (worstT));
    }
    {   // and the vibrato voicing (pure all-pass, no internal sum) must be RIPPLE-FREE: any dry
        // leak d shows up as a comb ripple of 20log10((1+d)/(1-d)).
        auto p = base(); p.type = 5; p.character = 1; p.mix = 1.0f; p.depth = 0.0f; p.b1 = 0.45f;
        auto t = tfOf (impulse (p).l, 800, 80.0, 12000.0);
        double lo = 1e9, hi = -1e9; for (double v : t.g) { lo = std::min (lo, v); hi = std::max (hi, v); }
        const double r = std::pow (10.0, (hi - lo) / 20.0);
        const double leak = db ((r - 1.0) / (r + 1.0));
        gate ("Vibe/Vibrato all-pass leg is flat (implied leak)", leak < -60.0,
              fmt ("ripple %.4f dB", hi - lo) + fmt (" => leak %.1f dB", leak));
    }
    {   // unity through, at defaults
        auto p = base(); p.type = 0; p.character = 0; p.mix = 0.5f; p.depth = 0.45f; p.feedback = 0.3f;
        auto in = noiseSig (96000); auto o = run (p, in);
        const double lvl = db (rmsOf (o.l, 20000) / rmsOf (in, 20000));
        gate ("unity-through at defaults (+-2 dB)", std::fabs (lvl) < 2.0, fmt ("%.2f dB vs dry", lvl));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("B. Per-Type discriminators (law 2 — a MECHANISM, not an EQ flavour)");

    {   // 1 NINETY — N identical 1st-order stages put the notches at fb*tan((2m+1)pi/2N).
        //   For N = 4 that is tan(pi/8) and tan(3pi/8): a ratio of 5.83, which is what
        //   ElectroSmash measured on a real Phase 90 (58.5 Hz / 340.8 Hz).
        auto p = base(); p.type = 0; p.character = 0 /*Script 74*/; p.b2 = 0.25f; p.b3 = 0.0f;
        p.mix = 1.0f; p.b1 = 0.5f;
        auto c = combOf (tfOf (impulse (p).l));
        const double ratio = (c.count >= 2) ? c.notchHz[1] / c.notchHz[0] : 0.0;
        gate ("Ninety: exactly 2 notches from 4 identical stages", c.count == 2,
              std::to_string (c.count) + " notches");
        gate ("Ninety: notch ratio = the 5.83:1 phase law", std::fabs (ratio - 5.83) < 0.9,
              fmt ("%.2f : 1 (theory 5.83)", ratio));
    }
    {   // 2 STONE — the dedicated EXTRA all-pass in the feedback path changes the loop phase law,
        //   so the resonant peaks land where a 4-stage loop structurally cannot put them.
        auto ps = base(); ps.type = 1; ps.character = 1 /*Color On*/; ps.feedback = 1.0f; ps.b2 = 0.25f; ps.mix = 1.0f;
        auto cs = combOf (tfOf (impulse (ps, 65536).l));
        auto pn = base(); pn.type = 0; pn.character = 1 /*Block 78*/; pn.feedback = 1.0f; pn.b2 = 0.25f; pn.mix = 1.0f;
        auto cn = combOf (tfOf (impulse (pn, 65536).l));
        gate ("Stone: inter-notch peaks lift >= +6 dB", cs.peakDb > 6.0, fmt ("%.1f dB peak", cs.peakDb));
        // loop THD at bus level
        auto po = ps; po.b8 = 0.0f; po.depth = 0.0f;
        const double th = thdDb (run (po, toneSig (48000, 220.0f)).l, 220.0);
        gate ("Stone: OTA loop is nonlinear (THD in 0.05..8 %)", th > -66.0 && th < -22.0,
              fmt2 ("%.1f dB (= %.3f %%)", th, 100.0 * std::pow (10.0, th / 20.0)));
        const double sep = (cs.notchHz.size() && cn.notchHz.size())
                         ? std::fabs (std::log2 (cs.notchHz[0] / cn.notchHz[0])) : 0.0;
        gate ("Stone's comb is not Ninety-with-feedback", sep > 0.20,
              fmt ("lowest notch %.2f octaves apart", sep));
    }
    {   // 3 DUO — TWO sweep generators, ONE accumulator. A single generator translates its comb
        //   RIGIDLY; two generators sliding through each other change the comb's SHAPE, so even
        //   the best-shift correlation between frames drops. (The first harness tried to count
        //   modulation-spectrum lines and could not separate a harmonic of one LFO from a second
        //   LFO — it reported a non-harmonic line on single-LFO Ninety too. Replaced, not tuned.)
        auto in = noiseSig (48000 * 5);
        auto p = base(); p.type = 2; p.character = 0; p.depth = 0.65f; p.rate = 0.62f;
        p.mix = 1.0f; p.b1 = 0.5f; p.feedback = 0.3f;
        const double rg = rigidity (trackTraj (run (p, in), in));
        auto p1 = base(); p1.type = 0; p1.character = 0; p1.depth = 0.65f; p1.rate = 0.62f;
        p1.mix = 1.0f; p1.b1 = 0.5f; p1.feedback = 0.3f;
        const double rg1 = rigidity (trackTraj (run (p1, in), in));
        auto p2 = base(); p2.type = 3; p2.character = 0; p2.depth = 0.65f; p2.rate = 0.62f;
        p2.mix = 1.0f; p2.b1 = 0.5f; p2.feedback = 0.3f;
        const double rg2 = rigidity (trackTraj (run (p2, in), in));
        gate ("Duo: the comb does NOT translate rigidly (two clocks)", rg < rg1 - 0.10 && rg < rg2 - 0.10,
              fmt ("Duo %.3f vs ", rg) + fmt2 ("Ninety %.3f / Twelve %.3f", rg1, rg2));
    }
    {   // 4 TWELVE — 12 poles = 6 notches, and the ONLY Type whose Rate reaches audio rate.
        auto p = base(); p.type = 3; p.character = 0; p.b2 = 0.6f; p.mix = 1.0f; p.b1 = 0.5f; p.b3 = 0.35f;
        auto c = combOf (tfOf (impulse (p).l, 900, 22.0, 18000.0), 2.0, 24.0, 16000.0);
        gate ("Twelve: 6 notches from 12 poles", c.count >= 5 && c.count <= 7,
              std::to_string (c.count) + " notches");
        // FM sidebands: LFO at ~125 Hz, carrier 1 kHz, look at 1000 +- 125 (unreachable by any
        // other Type — their Rate tops out at 20 Hz).
        auto ph = base(); ph.type = 3; ph.character = 1 /*Hi Range*/; ph.rate = 1.0f; ph.depth = 0.5f;
        ph.mix = 1.0f; ph.b1 = 0.5f; ph.feedback = 0.4f;
        auto y = run (ph, toneSig (48000 * 2, 1000.0f)).l;
        std::vector<float> tail (y.begin() + 24000, y.end());
        const double car = dftMag (tail, 1000.0);
        double sb = 0; for (double off : { 250.0, 500.0 })
            sb = std::max (sb, std::max (dftMag (tail, 1000.0 + off), dftMag (tail, 1000.0 - off)));
        const double sbDbc = db (sb / std::max (car, 1e-12));
        auto pn = base(); pn.type = 0; pn.character = 0; pn.rate = 1.0f; pn.depth = 0.5f; pn.mix = 1.0f; pn.b1 = 0.5f;
        auto yn = run (pn, toneSig (48000 * 2, 1000.0f)).l;
        std::vector<float> tn (yn.begin() + 24000, yn.end());
        double sbn = 0; for (double off : { 250.0, 500.0 })
            sbn = std::max (sbn, std::max (dftMag (tn, 1000.0 + off), dftMag (tn, 1000.0 - off)));
        const double sbnDbc = db (sbn / std::max (dftMag (tn, 1000.0), 1e-12));
        gate ("Twelve/Hi Range: audio-rate FM sidebands >= -30 dBc", sbDbc > -30.0, fmt ("%.1f dBc", sbDbc));
        gate ("no other Type can produce them (Ninety @ Rate 100)", sbnDbc < -45.0, fmt ("%.1f dBc", sbnDbc));
    }
    {   // 5 KRAUT — the LDR warp makes the sweep RISE and FALL at different speeds.
        auto p = base(); p.type = 4; p.character = 2 /*Hard Skew*/; p.depth = 0.7f; p.rate = 0.6f;
        p.mix = 1.0f; p.b1 = 0.45f; p.b6 = 0.05f; p.feedback = 0.3f;
        auto in = noiseSig (48000 * 5);
        const double rf = riseFall (trackTraj (run (p, in), in));
        auto p2 = base(); p2.type = 0; p2.character = 0; p2.depth = 0.7f; p2.rate = 0.6f; p2.mix = 1.0f; p2.b1 = 0.45f;
        const double rf2 = riseFall (trackTraj (run (p2, in), in));
        gate ("Kraut: sweep rise/fall asymmetry >= 1.8:1", (rf > 1.8 || rf < 1.0 / 1.8),
              fmt ("%.2f : 1", rf));
        gate ("Ninety's triangle is symmetric (control)", std::fabs (std::log2 (rf2)) < 0.6,
              fmt ("%.2f : 1", rf2));
    }
    {   // 6 VIBE — the four measured Uni-Vibe capacitors. Notch spacing must NOT match the
        //   identical-stage law, and the wet-only voicing must wobble the PITCH.
        auto pv = base(); pv.type = 5; pv.character = 0; pv.b2 = 0.0f; pv.b3 = 0.5f; pv.mix = 1.0f; pv.b1 = 0.5f;
        auto cv = combOf (tfOf (impulse (pv).l));
        auto pn = base(); pn.type = 0; pn.character = 2; pn.b2 = 0.333f; pn.b3 = 0.0f; pn.mix = 1.0f; pn.b1 = 0.5f;
        auto cn = combOf (tfOf (impulse (pn).l));
        const double rv = (cv.count >= 2) ? cv.notchHz[1] / cv.notchHz[0] : 0;
        const double rn = (cn.count >= 2) ? cn.notchHz[1] / cn.notchHz[0] : 0;
        gate ("Vibe: notch ratio is inharmonic vs the identical-stage law",
              rv > 0 && rn > 0 && std::fabs (std::log2 (rv / rn)) > 0.20,
              fmt2 ("Vibe %.2f:1 vs Ninety %.2f:1", rv, rn));
        auto pw = base(); pw.type = 5; pw.character = 1 /*Vibrato Lamp*/; pw.mix = 1.0f;
        pw.depth = 0.8f; pw.rate = 0.62f; pw.b1 = 0.4f;
        const double cents = centsDev (run (pw, toneSig (48000 * 3, 440.0f)).l, 440.0);
        gate ("Vibe/Vibrato at Mix 100 wobbles pitch >= 8 cents", cents > 8.0, fmt ("%.1f cents", cents));
    }
    {   // 7 BARBER — the whole comb must translate in ONE direction, forever. And the cycle wrap
        //   is the hard part: the paper's own warning is that it clicks unless the wrapping
        //   section inherits its neighbour's state.
        auto p = base(); p.type = 6; p.character = 0; p.rate = 0.55f; p.depth = 0.8f; p.mix = 1.0f;
        p.b1 = 0.3f; p.b2 = 0.5f; p.b3 = 0.5f;
        auto in = noiseSig (48000 * 5);
        auto tr = trackTraj (run (p, in), in);
        double shU = 0; const double mfU = monoFrac (tr, 6, &shU);
        auto pd = p; pd.character = 2 /*Fall 8*/;
        auto trd = trackTraj (run (pd, in), in);
        double shD = 0; const double mfD = monoFrac (trd, 6, &shD);
        auto pl = base(); pl.type = 0; pl.character = 0; pl.rate = 0.55f; pl.depth = 0.8f; pl.mix = 1.0f; pl.b1 = 0.4f;
        auto trl = trackTraj (run (pl, in), in);
        double shL = 0; const double mfL = monoFrac (trl, 6, &shL);
        gate ("Barber/Rise: comb drifts one way >= 90 % of frames", mfU > 0.90,
              fmt ("%.0f %% monotone, ", 100 * mfU) + fmt ("mean shift %+.3f oct/frame", shU));
        gate ("Barber/Fall reverses it", mfD > 0.90 && shD < 0 && shU > 0,
              fmt ("%.0f %% monotone, ", 100 * mfD) + fmt ("mean shift %+.3f oct/frame", shD));
        gate ("an LFO Type turns around (control)", mfL < 0.85, fmt ("%.0f %% monotone", 100 * mfL));
        // wrap click: 220 Hz sine in, nothing above 6 kHz may come out but a click
        auto pc = p; pc.rate = 0.72f;                        // several wraps inside the window
        const double clk = hfClickDbfs (run (pc, toneSig (48000 * 4, 220.0f)).l, (size_t) (FS * 0.5f));
        gate ("Barber cycle-wrap click <= -60 dBFS", clk < -60.0, fmt ("%.1f dBFS above 6 kHz", clk));
    }
    {   // 8 ENVY — the motion source IS the circuit. It must track the program, and it must PARK
        //   on silence (nothing free-runs).
        auto p = base(); p.type = 7; p.character = 1 /*Slow Swell*/; p.depth = 0.0f; p.mix = 1.0f;
        p.b1 = 0.55f; p.b5 = 0.60f; p.b6 = 0.15f; p.feedback = 0.4f;
        auto in = stabSig (48000 * 8, 0.05f, 1.0f);
        auto tr = trackTraj (run (p, in), in);
        const double r = rankCorr (tr.envDb, tr.oct);
        gate ("Envy: notch trajectory tracks the program envelope", r > 0.80,
              fmt ("rank r = %.3f", r));
        std::vector<float> sil ((size_t) (48000 * 3), 0.0f);
        for (int i = 0; i < 24000; ++i) sil[(size_t) i] = 0.05f * std::sin (6.2831853f * 220.0f * i / FS);
        auto silO = run (p, sil);
        auto trs = trackTraj (silO, sil);
        double park = 0; const size_t half = trs.absOct.size() * 2 / 3;
        for (size_t i = half + 2; i < trs.absOct.size(); ++i) park = std::max (park, std::fabs (trs.absOct[i] - trs.absOct[half]));
        gate ("Envy PARKS on silence (nothing free-runs)", park < 0.25, fmt ("%.3f octaves of drift", park));
    }
    {   // 9 STEPS — the trajectory must be piecewise-constant.
        auto p = base(); p.type = 8; p.character = 0; p.depth = 0.8f; p.rate = 0.55f; p.mix = 1.0f;
        p.b1 = 0.45f; p.b6 = 0.0f; p.feedback = 0.35f;
        auto in = noiseSig (48000 * 5);
        const double sf = stepFrac (trackTraj (run (p, in), in));
        auto p2 = base(); p2.type = 0; p2.character = 0; p2.depth = 0.8f; p2.rate = 0.55f; p2.mix = 1.0f; p2.b1 = 0.45f;
        const double sf2 = stepFrac (trackTraj (run (p2, in), in));
        gate ("Steps: trajectory is piecewise-constant", sf > 0.30, fmt ("%.0f %% of frames held", 100 * sf));
        gate ("a continuous LFO is not (control)", sf2 < sf * 0.6 + 0.05, fmt ("Ninety %.0f %%", 100 * sf2));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("C. CROSS-TYPE DISTINCTNESS MATRIX (every pair, phase-independent)");
    std::printf ("   distance = max over %d features of |delta| / JND. A pair is distinct at > 1.00.\n", Feat::N);
    {
        Feat F[FX::kNumTypes];
        auto nz = noiseSig (48000 * 5);
        auto st = stabSig (48000 * 4);
        for (int t = 0; t < FX::kNumTypes; ++t)
        {
            // ── static geometry (LFO effectively frozen)
            auto ps = base(); ps.type = t; ps.character = 0; ps.mix = 1.0f; ps.feedback = 0.45f;
            ps.b1 = 0.5f; ps.b2 = 0.5f; ps.b3 = 0.5f; ps.depth = (t == 6 ? 0.6f : 0.0f); ps.rate = 0.0f;
            auto c = combOf (tfOf (impulse (ps, 32768).l), 2.5);
            F[t].v[0] = c.count; F[t].v[1] = c.count ? c.lowOct : 0;
            F[t].v[2] = c.meanSpacOct; F[t].v[3] = c.spacCV;
            F[t].v[4] = c.peakDb;
            F[t].v[5] = std::max (-60.0, c.depthDb);      // a true null is -inf; clamp or it dominates
            // ── motion
            auto pm = base(); pm.type = t; pm.character = 0; pm.mix = 1.0f; pm.depth = 0.7f;
            pm.rate = 0.62f; pm.b1 = 0.5f; pm.feedback = 0.35f; pm.b6 = 0.08f; pm.b4 = 0.5f;
            auto om = run (pm, nz);
            auto tr = trackTraj (om, nz);
            double sh = 0;
            F[t].v[6]  = trajRange (tr);
            F[t].v[7]  = std::log2 (std::max (0.05, riseFall (tr)));
            F[t].v[8]  = monoFrac (tr, 6, &sh);
            F[t].v[9]  = stepFrac (tr);
            F[t].v[10] = rigidity (tr);
            F[t].v[11] = meanStepOct (tr);
            F[t].v[15] = lrDb (om);
            F[t].v[17] = derivCrest (tr);
            F[t].v[18] = monoRipple (tr);
            double mo = 0; for (size_t i = 6; i < tr.oct.size(); ++i) mo += tr.oct[i];
            F[t].v[16] = tr.oct.size() > 6 ? mo / (double) (tr.oct.size() - 6) : 0.0;
            // ── envelope correlation on an AM probe
            auto tre = trackTraj (run (pm, st), st);
            F[t].v[12] = std::fabs (rankCorr (tre.envDb, tre.oct));
            // ── audio-rate sidebands at +-250 Hz (only Twelve/Hi can reach a 250 Hz LFO)
            auto pf = base(); pf.type = t; pf.character = (t == 3 ? 1 : 0); pf.rate = 1.0f;
            pf.depth = 0.5f; pf.mix = 1.0f; pf.b1 = 0.5f; pf.feedback = 0.4f;
            auto y = run (pf, toneSig (48000 * 2, 1000.0f)).l;
            std::vector<float> tl (y.begin() + 24000, y.end());
            double sb = 0; for (double off : { 250.0, 500.0 })
                sb = std::max (sb, std::max (dftMag (tl, 1000.0 + off), dftMag (tl, 1000.0 - off)));
            F[t].v[13] = db (sb / std::max (dftMag (tl, 1000.0), 1e-12));
            // ── loop nonlinearity, comb parked clear of the harmonics
            auto pt = base(); pt.type = t; pt.character = 0; pt.feedback = 0.9f; pt.mix = 1.0f; pt.b1 = 0.95f;
            F[t].v[14] = thdDb (run (pt, toneSig (48000, 220.0f)).l, 220.0);
        }
        std::printf ("        ");
        for (int b = 0; b < FX::kNumTypes; ++b) std::printf ("%7.6s", TN (b));
        std::printf ("\n");
        double worst = 1e9; int wa = 0, wb = 0, wf = 0;
        for (int a = 0; a < FX::kNumTypes; ++a)
        {
            std::printf ("  %-6.6s", TN (a));
            for (int b = 0; b < FX::kNumTypes; ++b)
            {
                if (a == b) { std::printf ("      ."); continue; }
                double best = 0; int bi = 0;
                for (int i = 0; i < Feat::N; ++i)
                { const double d = std::fabs (F[a].v[i] - F[b].v[i]) / Feat::jnd (i); if (d > best) { best = d; bi = i; } }
                std::printf ("%7.2f", best);
                if (b > a && best < worst) { worst = best; wa = a; wb = b; wf = bi; }
            }
            std::printf ("\n");
        }
        gate ("every Type pair is distinguishable (> 1.00 JND)", worst > 1.0,
              fmt ("closest pair %.2f", worst) + " = " + TN (wa) + "/" + TN (wb)
              + " (carried by " + Feat::name (wf) + ")");
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("D. Every param evolves 0 -> 100, monotonically and dramatically (law 1)");

    auto monoUp = [] (const std::vector<double>& v, double slack) {
        for (size_t i = 1; i < v.size(); ++i) if (v[i] < v[i - 1] - slack) return false; return true; };
    auto monoDn = [] (const std::vector<double>& v, double slack) {
        for (size_t i = 1; i < v.size(); ++i) if (v[i] > v[i - 1] + slack) return false; return true; };

    {   // RATE — measured LFO frequency out of the modulation spectrum
        auto in = noiseSig (48000 * 4);
        std::vector<double> hz;
        for (float r : { 0.15f, 0.35f, 0.55f, 0.75f })
        { auto p = base(); p.type = 0; p.depth = 0.7f; p.rate = r; p.mix = 1.0f; p.b1 = 0.45f;
          auto tr = trackTraj (run (p, in), in); double f0 = 0; nonHarmDb (tr, &f0); hz.push_back (f0); }
        gate ("Rate: monotonic and spans decades", monoUp (hz, 0.02) && hz.back() / std::max (1e-3, hz.front()) > 8.0,
              vec (hz, "%.2f") + " Hz");
    }
    {   // DEPTH — trajectory range, octaves
        auto in = noiseSig (48000 * 4);
        std::vector<double> oc;
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = 0; p.depth = d; p.rate = 0.45f; p.mix = 1.0f; p.b1 = 0.5f;
          oc.push_back (trajRange (trackTraj (run (p, in), in))); }
        gate ("Depth: monotonic, 0 -> multi-octave excursion", monoUp (oc, 0.15) && oc.back() > 3.0,
              vec (oc) + " octaves");
    }
    {   // FEEDBACK — inter-notch peak gain
        std::vector<double> pk;
        for (float f : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = 3; p.character = 0; p.feedback = f; p.mix = 1.0f; p.b1 = 0.45f; p.b2 = 0.6f; p.b3 = 0.4f;
          pk.push_back (combOf (tfOf (impulse (p, 65536).l), 2.0).peakDb); }
        gate ("Feedback: monotonic resonance lift", monoUp (pk, 0.4) && pk.back() - pk.front() > 10.0,
              vec (pk, "%.1f") + " dB inter-notch peak");
    }
    {   // fb412 — THE INVERT PILL. A pill that does nothing is against the house rule, so this
        //  measures that it does the ONE thing it claims: XOR the loop sign. Ninety/`Script 74`
        //  is positive geography and Ninety/`Negative` (Character 7) is its negative sibling, so
        //  Invert on the first must land on the second's geometry, and Invert on the second must
        //  come back. That is a stronger gate than "something changed".
        //  The definitive test is WHERE THE RESONANT PEAK SITS, not how tall it is. At k > 0 the
        //  loop phase reaches 0 midway BETWEEN the k = 0 notches (the block-Phase-90 mid-hump);
        //  at k < 0 the extra pi puts the peak exactly ON a k = 0 notch (the hollow honk). So:
        //  freeze the sweep, find the k = 0 notches, then measure how far the resonant peak lands
        //  from the nearest of them. No dB threshold to tune - it is a geometry question.
        auto peakHz = [] (P q) {
            auto t = tfOf (impulse (q, 65536).l);
            double bv = -1e9, bf = 0;
            for (size_t i = 0; i < t.f.size(); ++i)
                if (t.f[i] > 60.0 && t.f[i] < 14000.0 && t.g[i] > bv) { bv = t.g[i]; bf = t.f[i]; }
            return bf; };
        auto pos = base(); pos.type = 0; pos.character = 0; pos.feedback = 0.75f; pos.mix = 1.0f;
        pos.rate = 0.0f; pos.depth = 0.0f;                  // freeze it: a moving comb has no geometry
        auto neg = pos; neg.character = 7;                  // `Negative` — the sign flipped by hand
        auto posI = pos; posI.invert = true;                // the pill, on the positive Character
        auto negI = neg; negI.invert = true;                // and on the negative one
        auto zero = pos; zero.feedback = 0.0f;              // the k = 0 notch frequencies
        auto n0 = combOf (tfOf (impulse (zero, 65536).l), 2.0).notchHz;
        auto distOct = [&] (double hz) {
            double d = 9.0; for (double n : n0) d = std::min (d, std::fabs (std::log2 (hz / n)));
            return d; };
        const double dp = distOct (peakHz (pos)),  dpi = distOct (peakHz (posI));
        const double dn = distOct (peakHz (neg)),  dni = distOct (peakHz (negI));
        gate ("Invert pill moves the resonant peak ONTO the notch (the k<0 geography)",
              n0.size() >= 1 && dpi < dp * 0.5,
              fmt2 ("peak sits %.2f oct from a notch -> %.2f oct after Invert", dp, dpi));
        // ⚠️ the first draft of this gate asserted Invert LANDS ON `Negative`'s number, and it
        //  failed at 6.7 vs 12.7 — correctly. A Character re-wires more than the loop sign
        //  (stage count, LFO shape, depth), so two Characters never share a number just because
        //  they share a polarity. What a sign XOR actually predicts is DIRECTION: inverting a
        //  positive-geography Character must move the inter-notch peak the OPPOSITE way from
        //  inverting a negative one. That is the falsifiable claim, so that is the gate.
        // ⚠️ two weaker drafts of this gate were thrown away, and why matters. The first asserted
        //  Invert LANDS ON `Negative`'s inter-notch peak dB and failed at 6.7 vs 12.7 — correctly:
        //  a Character re-wires more than the loop sign, so two Characters never share a number
        //  just because they share a polarity. The second asserted a 3 dB move in opposite
        //  directions and read 2.5 dB — a gate I would have had to loosen to pass, which is worth
        //  nothing. Geometry answers it outright: XOR means the ALREADY-negative Character must
        //  travel the other way, off the notch and back into the gap.
        gate ("   ... and it is a SIGN XOR — the negative Character travels the OTHER way",
              dni > dn * 2.0,
              fmt2 ("`Negative` peak %.2f oct from a notch -> %.2f oct after Invert", dn, dni));
    }
    {   // MIX — notch depth (this is the reason the internal sum is not the Mix knob)
        std::vector<double> dp, resid;
        for (float m : { 0.05f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = 0; p.character = 0; p.mix = m; p.b1 = 0.5f; p.b2 = 0.25f; p.b3 = 0.2f;
          const double d = deepestNullDb (impulse (p).l, 100.0, 8000.0);
          dp.push_back (-d); resid.push_back (d); }
        gate ("Mix: notch depth is MONOTONIC 0 -> full", monoUp (dp, 0.5) && dp.back() > 55.0,
              vec (resid, "%.1f") + " dB null");
    }
    {   // b1 CENTER
        std::vector<double> lo;
        for (float c : { 0.10f, 0.32f, 0.55f, 0.78f, 1.0f })
        { auto p = base(); p.type = 0; p.character = 0; p.b1 = c; p.b2 = 0.25f; p.b3 = 0.2f; p.mix = 1.0f;
          auto cb = combOf (tfOf (impulse (p).l, 1000, 18.0, 18000.0), 2.5, 20.0, 17000.0);
          lo.push_back (cb.count ? std::log2 (cb.notchHz[0]) : 0.0); }
        std::vector<double> loHz; for (double v : lo) loHz.push_back (std::pow (2.0, v));
        gate ("Center: monotonic, spans the audio band", monoUp (lo, 0.1) && lo.back() - lo.front() > 5.0,
              vec (loHz, "%.0f") + " Hz lowest notch");
    }
    {   // b2 STAGES — notch count
        std::vector<double> nc;
        for (float s : { 0.0f, 0.35f, 0.7f, 1.0f })
        { auto p = base(); p.type = 3; p.character = 0; p.b2 = s; p.b3 = 0.4f; p.mix = 1.0f; p.b1 = 0.45f;
          nc.push_back (combOf (tfOf (impulse (p).l), 2.5).count); }
        gate ("Stages: notch count climbs monotonically", monoUp (nc, 0.01) && nc.back() - nc.front() >= 3,
              vec (nc, "%.0f") + " notches");
    }
    {   // b3 SPREAD — mean notch spacing
        std::vector<double> sp;
        for (float s : { 0.0f, 0.3f, 0.6f, 1.0f })
        { auto p = base(); p.type = 3; p.character = 0; p.b3 = s; p.b2 = 0.5f; p.mix = 1.0f; p.b1 = 0.45f;
          sp.push_back (combOf (tfOf (impulse (p).l), 2.0).meanSpacOct); }
        gate ("Spread: notch spacing widens monotonically", monoUp (sp, 0.03) && sp.back() - sp.front() > 0.4,
              vec (sp) + " octaves between notches");
    }
    {   // b4 STEREO — L/R decorrelation
        auto in = noiseSig (48000 * 2);
        std::vector<double> lr;
        for (float s : { 0.0f, 0.33f, 0.66f, 1.0f })
        { auto p = base(); p.type = 0; p.b4 = s; p.depth = 0.6f; p.mix = 1.0f; p.b1 = 0.45f;
          auto o = run (p, in); std::vector<float> d (o.l.size());
          for (size_t i = 0; i < d.size(); ++i) d[i] = o.l[i] - o.r[i];
          lr.push_back (std::max (-140.0, db (rmsOf (d, 20000) / std::max (1e-9, rmsOf (o.l, 20000))))); }
        gate ("Stereo: monotonic L/R decorrelation", monoUp (lr, 2.0) && lr.back() - lr.front() > 20.0,
              vec (lr, "%.1f") + " dB L-R");
    }
    {   // b5 TOUCH — bipolar env → sweep.
        // ⚠️ probe craft: a STEADY program (not stabs) so the follower sits at one level and the
        //    knob is the only thing moving; and the position is read as the shift of the whole
        //    comb pattern against the Touch-centred run, because at Mix 1.0 every notch is a true
        //    null and "the deepest notch" is decided by measurement noise.
        auto in = noiseSig (48000 * 3);
        auto runAt = [&] (float t5) {
            auto p = base(); p.type = 0; p.character = 0; p.b5 = t5; p.depth = 0.0f; p.mix = 1.0f;
            p.b1 = 0.62f; p.b6 = 0.2f;
            return trackTraj (run (p, in), in); };
        auto ref = runAt (0.5f);
        std::vector<double> sh;
        for (float t5 : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto tr = runAt (t5);
          sh.push_back (bestShiftOct (tr.mean, ref.mean, tr.nb, tr.bandOct, nullptr, 48, 20)); }
        gate ("Touch: bipolar and monotonic, multi-octave", monoUp (sh, 0.12) && sh.back() - sh.front() > 2.5,
              fmt ("%.2f octaves of travel ", sh.back() - sh.front()) + vec (sh) + " oct vs centre");
    }
    {   // b6 LAG — a one-pole on the motion source. At a fast rate more Lag = less excursion.
        auto in = noiseSig (48000 * 3);
        std::vector<double> rg;
        for (float l : { 0.0f, 0.3f, 0.6f, 1.0f })
        { auto p = base(); p.type = 0; p.depth = 0.8f; p.rate = 0.66f; p.b6 = l; p.mix = 1.0f; p.b1 = 0.5f;
          rg.push_back (trajRange (trackTraj (run (p, in), in))); }
        gate ("Lag: monotonically slugs the motion", monoDn (rg, 0.12) && rg.front() - rg.back() > 1.0,
              vec (rg) + " octaves of excursion");
    }
    {   // b7 FLOOR — the sweep's lower bound
        std::vector<double> lo;
        for (float f : { 0.10f, 0.40f, 0.70f, 1.0f })
        { auto p = base(); p.type = 0; p.character = 0; p.b1 = 0.0f; p.b7 = f; p.b2 = 0.25f; p.b3 = 0.2f; p.mix = 1.0f;
          auto cb = combOf (tfOf (impulse (p).l, 1000, 18.0, 18000.0), 2.0, 20.0, 17000.0);
          lo.push_back (cb.count ? std::log2 (cb.notchHz[0]) : 0.0); }
        std::vector<double> fHz; for (double v : lo) fHz.push_back (std::pow (2.0, v));
        gate ("Floor: IS the lowest notch (a clamp, never a filter)", monoUp (lo, 0.1) && lo.back() - lo.front() > 3.0,
              vec (fHz, "%.0f") + " Hz lowest notch vs Floor knob 10/40/70/100");
    }
    {   // b8 COLOR — two jobs, two gates. ⚠️ probe craft: the first version measured THD of a
        //   220 Hz tone with the comb parked at 350 Hz, so the notches ATE harmonics 2-8 and the
        //   metric read THD going DOWN as Color went up. Park the comb at 7 kHz and the harmonics
        //   live in the flat part of the response.
        std::vector<double> th, hf;
        for (float c : { 0.0f, 0.33f, 0.66f, 1.0f })
        {
            auto p = base(); p.type = 0; p.character = 1; p.feedback = 0.85f; p.b8 = c;
            p.mix = 1.0f; p.b1 = 0.95f;
            th.push_back (thdDb (run (p, toneSig (48000, 220.0f)).l, 220.0));
            auto q = base(); q.type = 0; q.character = 1; q.feedback = 0.92f; q.b8 = c;
            q.mix = 1.0f; q.b1 = 0.5f; q.b2 = 0.7f; q.b3 = 0.5f;
            auto tR = tfOf (impulse (q, 65536).l, 700, 100.0, 16000.0);
            auto q0 = q; q0.feedback = 0.0f;
            auto t0 = tfOf (impulse (q0, 16384).l, 700, 100.0, 16000.0);
            double num = 0, den = 0;
            for (size_t i = 0; i < tR.f.size(); ++i)
            { const double ex = std::max (0.0, tR.g[i] - t0.g[i]);   // what the loop ADDED
              num += std::log2 (tR.f[i]) * ex; den += ex; }
            hf.push_back (den > 1e-9 ? num / den : 0.0);
        }
        gate ("Color: monotonically dirties the resonance", monoUp (th, 1.0) && th.back() - th.front() > 12.0,
              vec (th, "%.1f") + " dB THD");
        std::vector<double> hz2; for (double v : hf) hz2.push_back (std::pow (2.0, v));
        gate ("Color: monotonically darkens the resonance (in-loop LP)",
              monoDn (hf, 0.10) && hf.front() - hf.back() > 1.0,
              vec (hz2, "%.0f") + " Hz centroid of the resonant excess");
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("E. Every Character re-wires PHYSICS, not tone (law R4 / fb345)");
    {
        auto in = stabSig (48000 * 5);          // AM probe: LFO Characters AND envelope Characters
        double globalWorst = 1e9; int gwT = 0, gwA = 0, gwB = 0;
        for (int t = 0; t < FX::kNumTypes; ++t)
        {
            double vs[FX::kNumChars][14];
            for (int c = 0; c < FX::kNumChars; ++c)
            {
                auto ps = base(); ps.type = t; ps.character = c; ps.mix = 1.0f; ps.feedback = 0.45f;
                ps.b1 = 0.5f; ps.b2 = 0.5f; ps.b3 = 0.5f; ps.rate = 0.0f;
                ps.depth = (t == 6 ? 0.6f : 0.0f);
                auto cb = combOf (tfOf (impulse (ps, 16384).l, 800, 25.0, 18000.0), 2.0, 28.0, 15000.0);
                vs[c][0] = cb.count; vs[c][1] = cb.count ? cb.lowOct : 0.0;
                vs[c][2] = cb.meanSpacOct; vs[c][3] = cb.peakDb;
                vs[c][4] = std::max (-60.0, cb.depthDb);
                auto pm = base(); pm.type = t; pm.character = c; pm.mix = 1.0f; pm.depth = 0.7f;
                pm.rate = 0.68f; pm.b1 = 0.5f; pm.feedback = 0.35f; pm.b6 = 0.08f; pm.b4 = 0.5f;
                auto om = run (pm, in);
                auto tr = trackTraj (om, in);
                vs[c][5]  = trajRange (tr);
                vs[c][6]  = std::log2 (std::max (0.05, riseFall (tr)));
                vs[c][7]  = stepFrac (tr);
                vs[c][8]  = monoFrac (tr);
                vs[c][9]  = meanStepOct (tr);
                vs[c][10] = lrDb (om);
                vs[c][12] = derivCrest (tr);
                vs[c][13] = monoRipple (tr);
                auto pt = base(); pt.type = t; pt.character = c; pt.feedback = 0.85f;
                pt.mix = 1.0f; pt.b1 = 0.95f;
                vs[c][11] = thdDb (run (pt, toneSig (48000, 220.0f)).l, 220.0);
            }
            static const double J[14] = { 1.0, 0.5, 0.30, 4.0, 6.0, 0.7, 0.55, 0.28, 0.30, 0.30,
                                          3.0, 8.0, 0.45, 4.0 };
            double worst = 1e9; int wa = 0, wb = 0; int wf = 0;
            for (int a = 0; a < FX::kNumChars; ++a) for (int b = a + 1; b < FX::kNumChars; ++b)
            {
                double best = 0; int bi = 0;
                for (int i = 0; i < 14; ++i)
                { const double d = std::fabs (vs[a][i] - vs[b][i]) / J[i]; if (d > best) { best = d; bi = i; } }
                if (best < worst) { worst = best; wa = a; wb = b; wf = bi; }
            }
            static const char* FN[14] = { "notch count", "lowest notch", "spacing", "peak", "depth",
                "sweep range", "rise/fall", "step flatness", "monotone drift", "step size",
                "L/R", "loop THD", "LFO shape", "mono ripple" };
            gate ((std::string (TN (t)) + ": all 8 Characters differ physically").c_str(), worst > 1.0,
                  fmt ("closest pair %.2f JND", worst) + " (" + CN (t, wa) + " / " + CN (t, wb)
                  + ", separated by " + FN[wf] + ")");
            if (worst < globalWorst) { globalWorst = worst; gwT = t; gwA = wa; gwB = wb; }
        }
        std::printf ("        weakest Character pair overall: %.2f JND - %s %s / %s\n",
                     globalWorst, TN (gwT), CN (gwT, gwA), CN (gwT, gwB));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("F. NO CLICKS — every param swept under a sustained tone (law 4)");
    {
        const char* nm[12] = { "Rate", "Depth", "Feedback", "Mix", "Center", "Stages",
                               "Spread", "Stereo", "Touch", "Lag", "Floor", "Color" };
        auto in = toneSig (48000 * 2, 220.0f);
        double worstAll = -999; const char* worstName = "";
        for (int k = 0; k < 12; ++k)
        {
            FX e; e.prepare (FS, 128);
            std::vector<float> l = in, r = in;
            const int n = (int) in.size();
            for (int i = 0; i < n; i += 64)
            {
                const float u = (float) i / (float) n;
                auto p = base(); p.type = 1; p.character = 0; p.depth = 0.5f; p.mix = 0.6f;
                p.feedback = 0.4f; p.b1 = 0.45f; p.rate = 0.4f;
                switch (k) { case 0: p.rate = u; break; case 1: p.depth = u; break;
                             case 2: p.feedback = u; break; case 3: p.mix = u; break;
                             case 4: p.b1 = u; break; case 5: p.b2 = u; break;
                             case 6: p.b3 = u; break; case 7: p.b4 = u; break;
                             case 8: p.b5 = u; break; case 9: p.b6 = u; break;
                             case 10: p.b7 = u; break; default: p.b8 = u; }
                e.setParams (p);
                e.processStereo (&l[(size_t) i], &r[(size_t) i], std::min (64, n - i));
            }
            const double clk = hfClickDbfs (l, (size_t) (FS * 0.2f));
            if (clk > worstAll) { worstAll = clk; worstName = nm[k]; }
        }
        gate ("full sweep of all 12 params: no click above -55 dBFS", worstAll < -55.0,
              fmt ("worst %.1f dBFS", worstAll) + std::string (" on ") + worstName);
    }
    {   // Type / Character hot-swap under a sustained tone must not bang.
        // ⚠️ probe craft: the first version swapped at Rate 0.35 = 0.13 Hz — a 7.5 s LFO period, so
        //    the two halves of a 2 s buffer sat at completely different points of the SWEEP and the
        //    "overshoot" it reported (+5.1 dB) was the comb being somewhere else, not a bang. Run
        //    the LFO at ~3 Hz so each half contains six full cycles and the two peaks are comparable,
        //    and gate the actual artefact — HF energy that a 220 Hz sine cannot produce — as well.
        auto in = toneSig (48000 * 2, 220.0f);
        double worst = -99; int wa = 0, wb = 0;
        double worstClk = -999; int ca = 0, cb2 = 0;
        for (int t = 0; t + 1 < FX::kNumTypes; ++t)
        {
            FX e; e.prepare (FS, 128);
            std::vector<float> l = in, r = in;
            const int n = (int) in.size();
            double refA = 0, refB = 0, pk = 0;
            for (int i = 0; i < n; i += 64)
            {
                auto p = base(); p.type = (i < n / 2 ? t : t + 1); p.character = (i < n / 2 ? 1 : 3);
                p.depth = 0.5f; p.mix = 0.6f; p.feedback = 0.5f; p.b1 = 0.5f; p.rate = 0.75f;
                e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], std::min (64, n - i));
            }
            for (int i = 12000;         i < n / 2 - 2000;  ++i) refA = std::max (refA, (double) std::fabs (l[(size_t) i]));
            for (int i = n / 2 + 20000; i < n - 1000;      ++i) refB = std::max (refB, (double) std::fabs (l[(size_t) i]));
            for (int i = n / 2;         i < n / 2 + 12000; ++i) pk   = std::max (pk,   (double) std::fabs (l[(size_t) i]));
            const double over = db (pk / std::max (1e-9, std::max (refA, refB)));
            if (over > worst) { worst = over; wa = t; wb = t + 1; }
            std::vector<float> around (l.begin() + n / 2 - 2000, l.begin() + n / 2 + 12000);
            const double clk = hfClickDbfs (around, 1000);
            if (clk > worstClk) { worstClk = clk; ca = t; cb2 = t + 1; }
        }
        gate ("Type swap mid-note never overshoots either steady state (< +3 dB)", worst < 3.0,
              fmt ("worst +%.2f dB ", worst) + TN (wa) + "->" + TN (wb));
        gate ("Type swap emits no click (HF residual <= -55 dBFS)", worstClk < -55.0,
              fmt ("worst %.1f dBFS ", worstClk) + TN (ca) + "->" + TN (cb2));
    }
    // ─────────────────────────────────────────────────────────────────────────
    section ("G. MONO-SAFE — the effect must survive a fold-down (law 5)");
    {
        auto monoRip = [] (P p) {
            auto ir = impulse (p, 32768);
            std::vector<float> m (ir.l.size());
            for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (ir.l[i] + ir.r[i]);
            auto t = tfOf (m, 800, 100.0, 13000.0);
            double lo = 1e9, hi = -1e9;
            for (double v : t.g) { lo = std::min (lo, v); hi = std::max (hi, v); }
            return hi - lo; };
        for (float stw : { 0.25f, 1.0f })
        {
            double worst = 1e9; int wt = 0; std::vector<double> all;
            for (int t = 0; t < FX::kNumTypes; ++t)
            {
                auto p = base(); p.type = t; p.character = 0; p.b4 = stw; p.rate = 0.0f;
                p.depth = 0.6f; p.mix = 1.0f; p.b1 = 0.5f; p.feedback = 0.4f;
                const double r = monoRip (p);
                all.push_back (r);
                if (r < worst) { worst = r; wt = t; }
            }
            gate ((fmt ("mono sum keeps a real comb at Stereo %.0f (>= 6 dB)", stw * 100.0)).c_str(),
                  worst > 6.0, fmt ("weakest %.1f dB on ", worst) + TN (wt) + "  " + vec (all, "%.1f"));
        }
        {   // and per Character on the widest Type
            double worst = 1e9; int wc = 0;
            for (int c = 0; c < FX::kNumChars; ++c)
            {
                auto p = base(); p.type = 2; p.character = c; p.b4 = 1.0f; p.rate = 0.0f;
                p.depth = 0.6f; p.mix = 1.0f; p.b1 = 0.5f; p.feedback = 0.4f;
                const double r = monoRip (p);
                if (r < worst) { worst = r; wc = c; }
            }
            gate ("Duo: every Character survives mono", worst > 6.0,
                  fmt ("weakest %.1f dB ", worst) + CN (2, wc));
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("H. Stability — 60 s of white noise at maximum everything, all 9 Types");
    {
        int bad = 0; std::string first; double peak = 0;
        auto blk = noiseSig (48000, 0.12f);
        for (int t = 0; t < FX::kNumTypes; ++t)
        {
            FX e; e.prepare (FS, 128);
            auto p = base(); p.type = t; p.character = (t == 1 ? 5 : 4);
            p.feedback = 1.0f; p.depth = 1.0f; p.mix = 1.0f; p.b8 = 1.0f; p.b3 = 1.0f;
            p.b2 = 1.0f; p.rate = 0.8f; p.b5 = 1.0f; p.b4 = 1.0f;
            bool ok = true;
            for (int s = 0; s < 60 && ok; ++s)
            {
                std::vector<float> l = blk, r = blk;
                for (int i = 0; i < 48000; i += 128)
                { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
                for (float v : l)
                { if (! std::isfinite (v) || std::fabs (v) > 40.0f) { ok = false; break; }
                  peak = std::max (peak, (double) std::fabs (v)); }
            }
            if (! ok) { ++bad; if (first.empty()) first = TN (t); }
        }
        gate ("60 s at max feedback: finite and bounded, all Types", bad == 0,
              bad ? (std::to_string (bad) + " unstable, first " + first)
                  : fmt ("0 unstable, worst peak %.3f", peak));
    }
    {   // DC latch — a 1st-order all-pass has H(DC) = +1, so without the in-loop 10 Hz HP the loop
        //   amplifies any offset by 1/(1−k) = ×20 at k = 0.95 and parks on a rail (Phase G class).
        //   ⚠️ measure the RATIO against Feedback 0 at the same settings: the flat ×1.414 the first
        //   run reported was the Mix trim (1/sqrt(0.5) at Mix 1.0), not a latch.
        double worstDc = 0; int wt = 0;
        std::vector<float> dcIn ((size_t) (48000 * 2), 0.08f);
        for (int t = 0; t < FX::kNumTypes; ++t)
        {
            auto mean = [&] (float fb) {
                auto p = base(); p.type = t; p.character = 0; p.feedback = fb; p.mix = 1.0f; p.b1 = 0.45f;
                auto o = run (p, dcIn);
                double m = 0; for (size_t i = 48000; i < o.l.size(); ++i) m += o.l[i];
                return m / (double) (o.l.size() - 48000); };
            const double r = std::fabs (mean (1.0f)) / std::max (1e-9, std::fabs (mean (0.0f)));
            if (r > worstDc) { worstDc = r; wt = t; }
        }
        gate ("in-loop AC coupling: feedback never amplifies DC", worstDc < 1.15,
              fmt ("worst DC gain x%.3f vs Feedback 0, ", worstDc) + std::string ("on ") + TN (wt));
    }
    {   // 44.1 / 48 / 96 kHz: not just "does not blow up" — the same NOTCHES in the same places.
        //  A tan-LUT that is not rebuilt per rate, or a coefficient that assumes 48 k, shows up
        //  here as the whole comb moving.
        auto notches = [] (float fs) {
            auto p = base(); p.type = 0; p.character = 0; p.b2 = 0.25f; p.b3 = 0.0f;
            p.mix = 1.0f; p.b1 = 0.5f;
            auto ir = impulse (p, 32768, 1.0e-3f, 0.7f, fs);
            return combOf (tfOf (ir.l, 900, 30.0, 16000.0, fs), 3.0, 60.0, 14000.0); };
        auto ref = notches (48000.0f);
        for (float fs : { 44100.0f, 96000.0f })
        {
            auto c = notches (fs);
            double worst = 0;
            const int nn = std::min ((int) c.notchHz.size(), (int) ref.notchHz.size());
            for (int i = 0; i < nn; ++i)
                worst = std::max (worst, std::fabs (c.notchHz[(size_t) i] / ref.notchHz[(size_t) i] - 1.0));
            gate ((fmt ("notch geometry identical at %.1f kHz", fs / 1000.0)).c_str(),
                  c.count == ref.count && nn > 0 && worst < 0.03,
                  std::to_string (c.count) + " notches, worst " + fmt ("%.2f %% off 48 k", 100.0 * worst));
        }
        for (float fs : { 44100.0f, 96000.0f })
        {   // and it must stay bounded at max settings there too
            FX e; e.prepare (fs, 128);
            auto p = base(); p.type = 3; p.character = 4; p.feedback = 1.0f; p.depth = 1.0f;
            p.mix = 1.0f; p.b8 = 1.0f; p.b2 = 1.0f;
            std::vector<float> l ((size_t) fs, 0.0f), r ((size_t) fs, 0.0f);
            for (int i = 0; i < (int) fs; ++i) l[(size_t) i] = r[(size_t) i] = 0.05f * std::sin (6.2831853f * 220.0f * i / fs);
            bool ok = true; double pk = 0;
            for (int i = 0; i + 128 <= (int) fs; i += 128)
            { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
            for (float v : l) { if (! std::isfinite (v)) ok = false; pk = std::max (pk, (double) std::fabs (v)); }
            gate ((fmt ("stable at max feedback at %.1f kHz", fs / 1000.0)).c_str(),
                  ok && pk > 1e-4 && pk < 4.0, fmt ("peak %.4f", pk));
        }
    }
    // ─────────────────────────────────────────────────────────────────────────
    section ("I. CPU — us per 128-sample block at 48 kHz, per Type");
    {
        auto in = noiseSig (48000, 0.05f);
        double worst = 0; int wt = 0;
        for (int t = 0; t < FX::kNumTypes; ++t)
        {
            FX e; e.prepare (FS, 128);
            auto p = base(); p.type = t; p.character = 0; p.depth = 0.7f; p.mix = 0.6f;
            p.feedback = 0.6f; p.b2 = 0.5f; p.b8 = 0.4f; p.b4 = 0.5f;
            std::vector<float> l = in, r = in;
            for (int i = 0; i < 12800; i += 128) { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
            const auto t0 = std::chrono::high_resolution_clock::now();
            const int reps = 12;
            for (int q = 0; q < reps; ++q)
                for (int i = 0; i < 48000 - 128; i += 128)
                { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
            const auto t1 = std::chrono::high_resolution_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count()
                            / (double) (reps * ((48000 - 128) / 128));
            std::printf ("        %-8s %6.2f us/block  (%.2f %% of one core)\n",
                         TN (t), us, 100.0 * us / (128.0 / FS * 1e6));
            if (us > worst) { worst = us; wt = t; }
        }
        // the stated worst case: 16 stages, 6 instances
        {
            FX e[6]; for (auto& x : e) x.prepare (FS, 128);
            auto p = base(); p.type = 3; p.character = 3 /*Sixteen Pole*/; p.b2 = 1.0f;
            p.depth = 0.8f; p.mix = 0.6f; p.feedback = 0.8f; p.b8 = 0.5f; p.b4 = 0.5f;
            std::vector<float> l = in, r = in;
            for (int i = 0; i < 12800; i += 128) for (auto& x : e) { x.setParams (p); x.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
            const auto t0 = std::chrono::high_resolution_clock::now();
            const int reps = 8;
            for (int q = 0; q < reps; ++q)
                for (int i = 0; i < 48000 - 128; i += 128)
                    for (auto& x : e) { x.setParams (p); x.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
            const auto t1 = std::chrono::high_resolution_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count()
                            / (double) (reps * ((48000 - 128) / 128));
            std::printf ("        %-8s %6.2f us/block  (%.2f %% of one core)  <- 16 stages x 6 INSTANCES\n",
                         "WORST", us, 100.0 * us / (128.0 / FS * 1e6));
            gate ("worst case (16 stages x 6 instances) under 12 % of a core",
                  100.0 * us / (128.0 / FS * 1e6) < 12.0,
                  fmt ("%.2f %%", 100.0 * us / (128.0 / FS * 1e6)));
        }
        gate ("no single Type exceeds 2 % of a core", 100.0 * worst / (128.0 / FS * 1e6) < 2.0,
              fmt ("worst %.2f us ", worst) + std::string ("on ") + TN (wt));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("J. Self-check — can these gates actually fail?");
    {
        auto p = base(); p.type = 0; p.character = 2; p.b2 = 0.333f; p.b3 = 0.0f; p.mix = 1.0f;
        auto ir = impulse (p);
        auto t = tfOf (ir.l);
        auto c = combOf (t);
        // inject a fake extra notch and confirm the detector sees it
        auto t2 = t; for (size_t i = 0; i < t2.g.size(); ++i)
        { const double d = std::log2 (t2.f[i] / 2500.0); t2.g[i] -= 18.0 * std::exp (-d * d / 0.02); }
        auto c2 = combOf (t2);
        gate ("(self-check) the notch detector sees an injected notch", c2.count == c.count + 1,
              std::to_string (c.count) + " -> " + std::to_string (c2.count));
    }
    {
        auto in = toneSig (48000, 220.0f);
        std::vector<float> spike = in;
        for (int i = 12000; i < 12004; ++i) spike[(size_t) i] += 0.50f;
        gate ("(self-check) the HF click detector sees an injected click",
              hfClickDbfs (spike, 4000) > -34.0, fmt ("%.1f dBFS", hfClickDbfs (spike, 4000)));
        gate ("(self-check) it does NOT fire on the clean tone",
              hfClickDbfs (in, 4000) < -80.0, fmt ("%.1f dBFS", hfClickDbfs (in, 4000)));
    }

    std::printf ("\n  %d passed, %d FAILED\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
