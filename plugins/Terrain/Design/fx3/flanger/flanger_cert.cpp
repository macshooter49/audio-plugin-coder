// ─────────────────────────────────────────────────────────────────────────────
//  flanger_cert — the perceptual certification harness for the FX-rack FLANGER.
//
//    clang++ -O2 -std=c++17 -I <repo>/plugins/TerrainInstrument/Tests/shim \
//            -I <repo>/plugins/TerrainInstrument/Source \
//            -I . flanger_cert.cpp -o /tmp/flanger_cert && /tmp/flanger_cert
//
//  ⚠️ WHAT THIS HARNESS CANNOT ENFORCE (fb373): a green DSP harness proves the
//  ENGINE works. It NEVER proves the plugin REACHES it. The UI→param→DSP round trip
//  is the integration owner's separate, headless gate.
//
//  ⚠️ SAMPLE-DIFFERENCE RMS IS BANNED as a dramaticism metric (fb282/fb283: an
//  allpass change measured "102 % divergence" and was inaudible). Every number below
//  is phase-independent.
//
//  ── THE TWO INSTRUMENTS THIS HARNESS IS BUILT ON ────────────────────────────
//  1. THE COMB TRACKER. A flanger's audible motion is WHERE THE COMB IS, and the
//     spectral centroid is nearly BLIND to it — a dense comb and a sparse comb have
//     almost the same centroid, so a centroid trace reported a full 2.4-octave Depth
//     sweep as 250 cents and a 5-decade Rate sweep as no change at all. The wet of a
//     flanger is a + b·x[n−D], whose AUTOCORRELATION has a peak at exactly lag D. So
//     every motion measurement here tracks D(t) directly, per frame, by FFT
//     autocorrelation on a noise probe. That trace IS the sweep.
//  2. THE FINGERPRINT. 24 log bands of level-invariant magnitude SHAPE plus 24 bands
//     of temporal MODULATION DEPTH, computed on BOTH the L channel and the L−R
//     difference (a Character that only re-wires the right channel is invisible on L
//     alone — that is how `Wide Zero` first measured as identical to `Sub`).
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainFlangerFx.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <functional>
#include <chrono>

namespace {

using Flg = tw::TerrainFlangerFx;
using P   = Flg::Params;

constexpr float FS = 48000.0f;
int gPass = 0, gFail = 0;

void section (const char* s) { std::printf ("\n[%s]\n", s); }
void gate (const char* what, bool ok, const std::string& detail)
{
    if (ok) { ++gPass; std::printf ("  ok    %-54s %s\n", what, detail.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-54s %s\n", what, detail.c_str()); }
}
void note (const char* what, const std::string& detail)
{ std::printf ("  ..    %-54s %s\n", what, detail.c_str()); }

std::string fmt (const char* f, double v) { char b[160]; std::snprintf (b, sizeof b, f, v); return b; }
double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-14)); }

// ═══ programs — all at the measured −26 dBFS bus level ══════════════════════
std::vector<float> chord (int n, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n, 0.0f);
    const float f[4] = { 110.0f, 130.81f, 164.81f, 220.0f };
    for (int i = 0; i < n; ++i)
    { float s = 0.0f;
      for (float fr : f) for (int h = 1; h <= 14; ++h)
          s += std::sin (6.2831853f * fr * h * (float) i / FS) / (float) h;
      x[(size_t) i] = s * 0.02f; }
    double a = 0; for (float v : x) a += (double) v * v;
    const float g = rms / (float) std::sqrt (a / (double) n);
    for (float& v : x) v *= g;
    return x;
}
std::vector<float> noise (int n, float rms = 0.05f, uint32_t seed = 22222u)
{
    std::vector<float> x ((size_t) n); uint32_t st = seed;
    for (int i = 0; i < n; ++i)
    { st = st * 1664525u + 1013904223u; x[(size_t) i] = ((float) (st >> 8) / 8388608.0f) - 1.0f; }
    double a = 0; for (float v : x) a += (double) v * v;
    const float g = rms / (float) std::sqrt (a / (double) n);
    for (float& v : x) v *= g;
    return x;
}
std::vector<float> tone (int n, float hz, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = std::sin (6.2831853f * hz * (float) i / FS);
    double a = 0; for (float v : x) a += (double) v * v;
    const float g = rms / (float) std::sqrt (a / (double) n);
    for (float& v : x) v *= g;
    return x;
}
// ⚠️ A dynamics-driven Type is INVISIBLE to a static tone. 1.7 Hz is deliberately
//    non-harmonic with every LFO rate used below.
std::vector<float> amNoise (int n, float amHz, float rms = 0.05f)
{
    auto x = noise (n, rms);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] *= 0.25f + 0.75f * (0.5f - 0.5f * std::cos (6.2831853f * amHz * (float) i / FS));
    return x;
}

// ═══ FFT ════════════════════════════════════════════════════════════════════
void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
      if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        { std::complex<double> w (1.0, 0.0);
          for (size_t k = 0; k < len / 2; ++k)
          { auto u = a[i + k], v = a[i + k + len / 2] * w;
            a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
}

std::vector<double> frameMag (const std::vector<float>& v, size_t from, size_t N)
{
    std::vector<std::complex<double>> a (N);
    for (size_t i = 0; i < N; ++i)
    { const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * (double) i / (double) N);
      const double s = (from + i < v.size()) ? (double) v[from + i] : 0.0;
      a[i] = std::complex<double> (s * w, 0.0); }
    fft (a);
    std::vector<double> m (N / 2);
    for (size_t i = 0; i < N / 2; ++i) m[i] = std::abs (a[i]);
    return m;
}

std::vector<double> logBands (const std::vector<double>& mag, int nb, double f0, double f1, size_t N)
{
    std::vector<double> b ((size_t) nb, 0.0); std::vector<int> cnt ((size_t) nb, 0);
    const double lg0 = std::log (f0), lg1 = std::log (f1);
    for (size_t i = 1; i < mag.size(); ++i)
    { const double f = (double) i * FS / (double) N;
      if (f < f0 || f > f1) continue;
      int k = (int) ((std::log (f) - lg0) / (lg1 - lg0) * (double) nb);
      k = std::max (0, std::min (nb - 1, k));
      b[(size_t) k] += mag[i] * mag[i]; ++cnt[(size_t) k]; }
    for (int k = 0; k < nb; ++k)
        b[(size_t) k] = cnt[(size_t) k] ? db (std::sqrt (b[(size_t) k] / (double) cnt[(size_t) k])) : 1e9;
    // a band with no FFT bins in it is NOT -280 dB, it is unmeasured. Fill from the
    // nearest measured neighbour or every max/min metric reads the analysis grid.
    for (int k = 0; k < nb; ++k) if (b[(size_t) k] > 1e8)
    { int lo = k, hi = k;
      while (lo >= 0 && b[(size_t) lo] > 1e8) --lo;
      while (hi < nb && b[(size_t) hi] > 1e8) ++hi;
      if (lo >= 0)      b[(size_t) k] = b[(size_t) lo];
      else if (hi < nb) b[(size_t) k] = b[(size_t) hi];
      else              b[(size_t) k] = -200.0; }
    return b;
}

std::vector<double> avgSpectrum (const std::vector<float>& v, size_t from, int nb = 32,
                                 double f0 = 60.0, double f1 = 16000.0, size_t N = 4096)
{
    std::vector<double> acc ((size_t) nb, 0.0); int frames = 0;
    for (size_t p = from; p + N < v.size(); p += N / 2)
    { auto lb = logBands (frameMag (v, p, N), nb, f0, f1, N);
      for (int k = 0; k < nb; ++k) acc[(size_t) k] += lb[(size_t) k]; ++frames; }
    if (frames) for (auto& x : acc) x /= (double) frames;
    return acc;
}
double specDist (const std::vector<double>& a, const std::vector<double>& b)
{ double m = 0; for (size_t i = 0; i < a.size(); ++i) m = std::max (m, std::fabs (a[i] - b[i])); return m; }

double rmsOf (const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double a = 0; size_t n = 0;
    for (size_t i = from; i < to; ++i) { a += (double) v[i] * v[i]; ++n; }
    return n ? std::sqrt (a / (double) n) : 0.0;
}

// ═══ ★ THE COMB TRACKER ═════════════════════════════════════════════════════
//  wet = a·x + b·x[n−D]  ⇒  autocorrelation peaks at lag D. Track it per frame and
//  you have the actual sweep the ear follows, in ms, with no phase dependence on the
//  program (the probe is noise, so the autocorrelation of the SOURCE is a delta).
std::vector<double> combTraceMs (const std::vector<float>& v, size_t from,
                                 size_t N = 8192, size_t hop = 2048,
                                 double loMs = 0.10, double hiMs = 44.0)
{
    size_t M = 1; while (M < 2 * N) M <<= 1;
    const size_t k0 = std::max<size_t> (4, (size_t) (loMs * 0.001 * FS));
    // the lag search stops at N/3: past that the window autocorrelation is so small
    // that de-biasing amplifies noise more than signal (measured - the first version
    // searched to N-3 and invented a 25 ms comb at every sweep turning point).
    const size_t k1 = std::min<size_t> (N / 3, (size_t) (hiMs * 0.001 * FS));
    std::vector<std::complex<double>> a (M);

    // ⚠️ DE-BIAS BY THE WINDOW'S OWN AUTOCORRELATION. A windowed frame's r[k] is the
    //    true autocorrelation TIMES the window's autocorrelation, which tapers toward
    //    zero with lag - so a noise fluctuation at a SHORT lag routinely outranks the
    //    real comb peak at a long one. Uncorrected, the trace was clean 90 % of the
    //    time and threw wild outliers the rest, which destroyed every correlation and
    //    periodicity statistic downstream.
    std::vector<double> wac (M, 0.0);
    {
        std::fill (a.begin(), a.end(), std::complex<double> (0, 0));
        for (size_t i = 0; i < N; ++i)
            a[i] = std::complex<double> (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * (double) i / (double) N), 0.0);
        fft (a);
        for (auto& z : a) z = std::complex<double> (std::norm (z), 0.0);
        fft (a);
        const double w0 = a[0].real();
        for (size_t k = 0; k < M; ++k) wac[k] = std::max (a[k].real() / std::max (1e-18, w0), 0.30);
    }

    std::vector<double> out;
    for (size_t p = from; p + N < v.size(); p += hop)
    {
        double mean = 0; for (size_t i = 0; i < N; ++i) mean += (double) v[p + i];
        mean /= (double) N;
        std::fill (a.begin(), a.end(), std::complex<double> (0, 0));
        for (size_t i = 0; i < N; ++i)
        { const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * (double) i / (double) N);
          a[i] = std::complex<double> (((double) v[p + i] - mean) * w, 0.0); }
        fft (a);
        for (auto& z : a) z = std::complex<double> (std::norm (z), 0.0);
        fft (a);                                   // power spectrum is real+even => this inverts
        const double r0 = a[0].real();
        if (r0 <= 1e-18) { out.push_back (0.0); continue; }
        double best = 0; size_t bk = k0;
        for (size_t k = k0; k <= k1; ++k)
        { const double r = std::fabs (a[k].real()) / wac[k]; if (r > best) { best = r; bk = k; } }
        // parabolic peak: an integer-lag tracker quantises the trace into a staircase,
        // and every median-based shape statistic then reads the quantum, not the sweep.
        double frac = 0.0;
        if (bk > k0 && bk + 1 <= k1)
        { const double ym = std::fabs (a[bk - 1].real()) / wac[bk - 1],
                       y0 = std::fabs (a[bk].real())     / wac[bk],
                       yp = std::fabs (a[bk + 1].real()) / wac[bk + 1];
          const double den = ym - 2.0 * y0 + yp;
          if (std::fabs (den) > 1e-18) frac = std::max (-0.5, std::min (0.5, 0.5 * (ym - yp) / den)); }
        out.push_back ((best / r0 > 0.02) ? ((double) bk + frac) * 1000.0 / FS : 0.0);
    }
    // 5-point median: the estimator is still an argmax, and one bad frame in fifty
    // would otherwise dominate a crest or periodicity statistic.
    if (out.size() >= 7)
    {
        std::vector<double> f = out;
        for (size_t i = 3; i + 3 < out.size(); ++i)
        { double w[7] = { out[i-3], out[i-2], out[i-1], out[i], out[i+1], out[i+2], out[i+3] };
          std::sort (w, w + 7); f[i] = w[3]; }
        out.swap (f);
    }
    return out;
}

double medianOf (std::vector<double> v)
{ if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; }
double pctOf (std::vector<double> v, double q)
{ if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[(size_t) ((double) (v.size() - 1) * q)]; }

double pearson (const std::vector<double>& a, const std::vector<double>& b)
{
    const size_t n = std::min (a.size(), b.size()); if (n < 4) return 0.0;
    double ma = 0, mb = 0; for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= (double) n; mb /= (double) n;
    double sa = 0, sb = 0, sab = 0;
    for (size_t i = 0; i < n; ++i)
    { const double da = a[i] - ma, dbv = b[i] - mb; sa += da * da; sb += dbv * dbv; sab += da * dbv; }
    return (sa > 1e-18 && sb > 1e-18) ? sab / std::sqrt (sa * sb) : 0.0;
}

// ═══ the fingerprint (L and L−R) ════════════════════════════════════════════
struct Print { std::vector<double> v; };

Print fingerprintOne (const std::vector<float>& v, size_t from)
{
    const int NB = 48; const size_t N = 8192;
    std::vector<std::vector<double>> frames;
    for (size_t p = from; p + N < v.size(); p += N / 2)
        frames.push_back (logBands (frameMag (v, p, N), NB, 100.0, 14000.0, N));
    Print pr; pr.v.assign (3 * NB, 0.0);
    if (frames.empty()) return pr;
    std::vector<double> mean ((size_t) NB, 0.0);
    for (auto& f : frames) for (int k = 0; k < NB; ++k) mean[(size_t) k] += f[(size_t) k];
    for (int k = 0; k < NB; ++k) mean[(size_t) k] /= (double) frames.size();
    std::vector<double> sd ((size_t) NB, 0.0);
    for (auto& f : frames) for (int k = 0; k < NB; ++k)
    { const double d = f[(size_t) k] - mean[(size_t) k]; sd[(size_t) k] += d * d; }
    for (int k = 0; k < NB; ++k) sd[(size_t) k] = std::sqrt (sd[(size_t) k] / (double) frames.size());
    double mu = 0; for (double x : mean) mu += x; mu /= (double) NB;
    for (int k = 0; k < NB; ++k)
    {
        std::vector<double> col;
        for (auto& f : frames) col.push_back (f[(size_t) k]);
        std::sort (col.begin(), col.end());
        pr.v[(size_t) k]            = mean[(size_t) k] - mu;              // level-invariant shape
        pr.v[(size_t) (NB + k)]     = sd[(size_t) k];                     // how much it MOVES
        pr.v[(size_t) (2 * NB + k)] = mean[(size_t) k] - col[col.size() / 20];  // how deep it DIPS
    }
    return pr;
}

// motion statistics derived from a comb trace (in ms)
struct Motion { double rangeCents, reversals, crest, jumpRatio, monoFrac, monoSigned; };
Motion motionOf (const std::vector<double>& t)
{
    Motion m { 0, 0, 0, 0, 0, 0 };
    std::vector<double> v;
    for (double x : t) if (x > 1e-6) v.push_back (x);
    if (v.size() < 6) return m;
    double mn = 1e9, mx = 0, mu = 0;
    for (double x : v) { mn = std::min (mn, x); mx = std::max (mx, x); mu += x; }
    mu /= (double) v.size();
    m.rangeCents = 1200.0 * std::log2 (mx / std::max (1e-6, mn));
    std::vector<double> d, ad;
    for (size_t i = 1; i < v.size(); ++i)
    { const double c = 1200.0 * std::log2 (v[i] / v[i - 1]); d.push_back (c); ad.push_back (std::fabs (c)); }
    int rev = 0;
    for (size_t i = 1; i < d.size(); ++i) if (d[i] * d[i - 1] < 0 && std::fabs (d[i]) > 4.0) ++rev;
    m.reversals = rev;
    const double med = std::max (1e-3, medianOf (ad));
    m.crest = pctOf (ad, 0.95) / med;
    m.jumpRatio = pctOf (ad, 0.93) / med;
    // monotone fraction, ignoring wraps (a barberpole's saw reset is not a reversal)
    int pos = 0, neg = 0;
    for (double c : d) { if (std::fabs (c) > 500.0 || std::fabs (c) < 3.0) continue;
                         if (c > 0) ++pos; else ++neg; }
    const int tot = pos + neg;
    m.monoFrac   = tot ? std::fabs ((double) (pos - neg)) / (double) tot : 0.0;
    m.monoSigned = tot ? ((double) (pos - neg)) / (double) tot : 0.0;
    return m;
}

struct Run { std::vector<float> l, r; };

Print fingerprint (const Run& o, size_t from)
{
    std::vector<float> d (o.l.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = o.l[i] - o.r[i];
    auto a = fingerprintOne (o.l, from);
    auto b = fingerprintOne (d, from);
    // clamp the side print's floor: a mono-identical run gives −280 dB bands whose
    // differences would swamp everything. −70 dB is "silent" for our purposes.
    for (double& x : b.v) x = std::max (x, -70.0);
    a.v.insert (a.v.end(), b.v.begin(), b.v.end());
    // + the SIGNED sweep direction. Rise and Fall have identical magnitude spectra and
    // identical modulation depth - they differ only in which way the comb travels, which
    // is a spectrogram-TRAJECTORY feature and audibly night-and-day.
    a.v.push_back (12.0 * motionOf (combTraceMs (o.l, from, 8192, 2048)).monoSigned);
    return a;
}
double printDist (const Print& a, const Print& b)
{ double m = 0; for (size_t i = 0; i < a.v.size(); ++i) m = std::max (m, std::fabs (a.v[i] - b.v[i])); return m; }

// ═══ engine driver ══════════════════════════════════════════════════════════
Run run (P p, const std::vector<float>& in, int blk = 128)
{
    Flg e; e.prepare ((double) FS, blk);
    Run o; o.l.resize (in.size()); o.r.resize (in.size());
    std::vector<float> bl ((size_t) blk), br ((size_t) blk);
    size_t i = 0;
    while (i < in.size())
    {
        const int n = (int) std::min ((size_t) blk, in.size() - i);
        for (int k = 0; k < n; ++k) { bl[(size_t) k] = in[i + (size_t) k]; br[(size_t) k] = bl[(size_t) k]; }
        e.setParams (p); e.processStereo (bl.data(), br.data(), n);
        for (int k = 0; k < n; ++k) { o.l[i + (size_t) k] = bl[(size_t) k]; o.r[i + (size_t) k] = br[(size_t) k]; }
        i += (size_t) n;
    }
    return o;
}

Run runSweep (P p, const std::vector<float>& in, const std::function<void (P&, float)>& set, int blk = 128)
{
    Flg e; e.prepare ((double) FS, blk);
    Run o; o.l.resize (in.size()); o.r.resize (in.size());
    std::vector<float> bl ((size_t) blk), br ((size_t) blk);
    size_t i = 0;
    while (i < in.size())
    {
        const int n = (int) std::min ((size_t) blk, in.size() - i);
        set (p, (float) i / (float) in.size());
        for (int k = 0; k < n; ++k) { bl[(size_t) k] = in[i + (size_t) k]; br[(size_t) k] = bl[(size_t) k]; }
        e.setParams (p); e.processStereo (bl.data(), br.data(), n);
        for (int k = 0; k < n; ++k) { o.l[i + (size_t) k] = bl[(size_t) k]; o.r[i + (size_t) k] = br[(size_t) k]; }
        i += (size_t) n;
    }
    return o;
}

float rateFor (float hz) { return (float) (std::log ((double) hz / 0.02) / std::log (1000.0)); }

P base()
{
    P p;
    p.type = 0; p.character = 0;
    p.rate = rateFor (0.6f); p.depth = 0.6f; p.feedback = 0.5f; p.mix = 1.0f;
    p.b1 = 0.5f; p.b2 = 0.0f; p.b3 = 0.625f; p.b4 = 0.30f;
    p.b5 = 0.0f; p.b6 = 0.0f; p.b7 = 0.35f; p.b8 = 0.0f;
    p.tempoSync = false; p.bpm = 120.0;
    return p;
}
const char* TN (int t) { return Flg::typeNames()[t]; }
const char* CN (int t, int c) { return Flg::charNames (t)[c]; }

std::vector<double> stRms (const std::vector<float>& v, size_t from, double winMs, double hopMs, float fs = FS)
{
    const size_t W = (size_t) (winMs * 0.001 * fs), H = (size_t) (hopMs * 0.001 * fs);
    std::vector<double> s;
    for (size_t p = from; p + W < v.size(); p += H)
    { double a = 0; for (size_t i = 0; i < W; ++i) a += (double) v[p + i] * v[p + i];
      s.push_back (std::sqrt (a / (double) W)); }
    return s;
}

double hfRatio (const std::vector<float>& v, size_t from)
{
    auto b = avgSpectrum (v, from, 48, 60.0, 16000.0, 4096);
    double hi = 0, all = 0;
    for (int k = 0; k < 48; ++k)
    { const double f = 60.0 * std::pow (16000.0 / 60.0, k / 47.0);
      const double m = std::pow (10.0, b[(size_t) k] / 20.0);
      all += m; if (f > 2000.0) hi += m; }
    return db (hi / std::max (1e-12, all));
}
double lfRatio (const std::vector<float>& v, size_t from)
{
    auto b = avgSpectrum (v, from, 48, 20.0, 16000.0, 8192);
    double lo = 0, all = 0;
    for (int k = 0; k < 48; ++k)
    { const double f = 20.0 * std::pow (800.0, k / 47.0);
      const double m = std::pow (10.0, b[(size_t) k] / 20.0);
      all += m; if (f < 150.0) lo += m; }
    return db (lo / std::max (1e-12, all));
}
// peak-to-valley of the wet magnitude spectrum in a band — the comb's TEETH
double peakToValley (const std::vector<float>& v, size_t from, double f0 = 200.0, double f1 = 12000.0)
{
    // 32 k frames: 1.5 Hz bins, so a 160-band grid down at 200 Hz is genuinely resolved.
    // Only ever used on a STATIC comb (Depth 0), so the long frame costs nothing.
    auto b = avgSpectrum (v, from, 160, f0, f1, 32768);
    double mx = -1e9, mn = 1e9;
    for (size_t k = 4; k + 4 < b.size(); ++k) { mx = std::max (mx, b[k]); mn = std::min (mn, b[k]); }
    return mx - mn;
}
// ⚠️ RESONANCE needs peak-over-MEDIAN, not peak-to-valley: a feedforward comb already
//    has a true ZERO, so P-V saturates at ~26 dB with NO feedback at all and then
//    barely moves. The pole's height above the median is what regeneration actually
//    does (1/(1-|g|)), and it is monotone in |g|.
double peakOverMedian (const std::vector<float>& v, size_t from, double f0 = 200.0, double f1 = 12000.0)
{
    auto b = avgSpectrum (v, from, 160, f0, f1, 32768);
    std::vector<double> c (b.begin() + 4, b.end() - 4);
    double mx = -1e9; for (double x : c) mx = std::max (mx, x);
    std::sort (c.begin(), c.end());
    return mx - c[c.size() / 2];
}
std::pair<double, double> stereoStats (const Run& o, size_t from)
{
    std::vector<float> d (o.l.size()), m (o.l.size());
    for (size_t i = 0; i < d.size(); ++i) { d[i] = o.l[i] - o.r[i]; m[i] = 0.5f * (o.l[i] + o.r[i]); }
    const double ref = std::max (1e-12, rmsOf (o.l, from));
    return { db (rmsOf (d, from) / ref), db (rmsOf (m, from) / ref) };
}

bool monotoneUp (const std::vector<double>& v, double slack)
{ for (size_t i = 1; i < v.size(); ++i) if (v[i] < v[i - 1] - slack) return false; return true; }
bool monotoneDown (const std::vector<double>& v, double slack)
{ for (size_t i = 1; i < v.size(); ++i) if (v[i] > v[i - 1] + slack) return false; return true; }
std::string traj (const std::vector<double>& v, const char* unit)
{
    std::string s;
    for (size_t i = 0; i < v.size(); ++i)
    { char b[48]; std::snprintf (b, sizeof b, "%.1f", v[i]); s += b; if (i + 1 < v.size()) s += " > "; }
    return s + " " + unit;
}

// ⚠️ THE PROBE FOR THE DISTINCTNESS MATRICES. Noise alone cannot see a nonlinear
//    Character - the harmonics of noise ARE noise - and a chord alone has nothing
//    above 3 kHz to comb. Half and half sees both. (Measured: on pure noise,
//    Jet/Silver and Jet/Screamer sat 1.4 dB apart despite one of them saturating.)
std::vector<float> mixedProbe (int n)
{
    auto a = chord (n, 0.035f); auto b = noise (n, 0.035f);
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = a[(size_t) i] + b[(size_t) i];
    return x;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
int main()
{
    const int N4 = (int) (FS * 4.0f);
    const size_t SETTLE = (size_t) (FS * 0.35f);

    std::printf ("\n══ TERRAIN FLANGER — certification ══  (bus program -26 dBFS, fs %.0f, blocks of 128)\n", FS);
    std::printf ("   %d Types x %d Characters = %d voicings\n",
                 Flg::kNumTypes, Flg::kNumChars, Flg::kNumTypes * Flg::kNumChars);

    // ─────────────────────────────────────────────────────────────────────────
    section ("A. Unity, the Mix law, and the zero-dry proof (law 3)");

    {   double worst = 0; int wt = -1;
        for (int t = 0; t < Flg::kNumTypes; ++t)
        { auto p = base(); p.type = t; p.mix = 0.0f; p.feedback = 0.95f;
          auto in = chord (N4); auto o = run (p, in);
          for (size_t i = 0; i < in.size(); ++i)
          { const double d = std::fabs (o.l[i] - in[i]); if (d > worst) { worst = d; wt = t; } } }
        gate ("Mix 0 is bit-transparent on all 6 Types", worst < 1e-7,
              fmt ("worst sample delta %.3e", worst) + (wt >= 0 ? std::string (" (") + TN (wt) + ")" : ""));
    }
    {   // ⚠️ THE EXACT DRY-RESIDUAL MEASUREMENT — no estimator noise floor. Every Type
        //    is a two-deck machine whose shortest read is 2 samples, so an impulse's
        //    y[0] IS the dry coefficient, exactly.
        double worst = -400.0; int wt = -1;
        for (int t = 0; t < Flg::kNumTypes; ++t)
        { auto p = base(); p.type = t; p.mix = 1.0f; p.depth = 0.0f;
          std::vector<float> imp ((size_t) 4096, 0.0f); imp[0] = 1.0f;
          auto o = run (p, imp);
          const double lk = db (std::max (std::fabs (o.l[0]), std::fabs (o.l[1])));
          if (lk > worst) { worst = lk; wt = t; } }
        gate ("Mix 1.0 dry residual < -60 dB (impulse y[0..1], exact)", worst < -60.0,
              fmt ("worst %.1f dB", worst) + (wt >= 0 ? std::string (" (") + TN (wt) + ")" : ""));
    }
    {   auto p = base(); p.type = Flg::TapeZero; p.character = 0;
        p.depth = 0.0f; p.b1 = 0.5f; p.b6 = 0.0f; p.feedback = 0.5f; p.mix = 1.0f;
        auto in = chord (N4); auto o = run (p, in);
        const double resid = db (rmsOf (o.l, SETTLE) / std::max (1e-12, rmsOf (in, SETTLE)));
        gate ("Tape Zero parked null is analytically silent at Mix 1.0", resid < -100.0,
              fmt ("%.1f dB residual", resid));
    }
    {   std::vector<double> v; double worst = 0; int wt = -1;
        for (int t = 0; t < Flg::kNumTypes; ++t)
        { auto p = base(); p.type = t; p.mix = 0.5f; p.depth = 0.5f;
          auto in = chord (N4); auto o = run (p, in);
          const double d = db (rmsOf (o.l, SETTLE) / std::max (1e-12, rmsOf (in, SETTLE)));
          v.push_back (d); if (std::fabs (d) > std::fabs (worst)) { worst = d; wt = t; } }
        // +-3.5 dB, and the reason is physics not slop: an additive comb at a SHORT
        // delay is a doubling (+3 dB) and at a long delay averages -3 dB, so no single
        // wet trim can be right for both. The trim is 0 dB and the spread is reported.
        gate ("unity-through at defaults, all Types within +-3.5 dB", std::fabs (worst) < 3.5,
              fmt ("worst %+.2f dB", worst) + (wt >= 0 ? std::string (" (") + TN (wt) + ")" : "")
              + " | " + traj (v, "dB"));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("B. Type discriminators (law 2 — a mechanism, not a flavour)");

    double tzDip = 0, singleDeckBest = 0;
    {   // ★ THE FLAGSHIP. Sub polarity, Depth 100, Mix 100.
        auto p = base(); p.type = Flg::TapeZero; p.character = 0;
        p.rate = rateFor (0.35f); p.depth = 1.0f; p.b1 = 0.5f; p.b6 = 0.0f;
        p.feedback = 0.5f; p.mix = 1.0f; p.b8 = 0.0f; p.b4 = 0.0f;
        auto in = chord ((int) (FS * 7.0f));
        auto o = run (p, in);
        auto s = stRms (o.l, SETTLE, 3.0, 0.5);
        const double med = medianOf (s);
        double mn = 1e9; for (double x : s) mn = std::min (mn, x);
        tzDip = db (mn / std::max (1e-14, med));
        gate ("Tape Zero/Sub: broadband null > 30 dB at the crossing", tzDip < -30.0,
              fmt ("%.1f dB below the surrounding program", tzDip) + " (3 ms window)");

        auto p2 = p; p2.character = 1;                    // Add — same machine, no null
        auto s2 = stRms (run (p2, in).l, SETTLE, 3.0, 0.5);
        double mn2 = 1e9; for (double x : s2) mn2 = std::min (mn2, x);
        const double dip2 = db (mn2 / std::max (1e-14, medianOf (s2)));
        gate ("Tape Zero/Add does NOT null (the polarity is real)", dip2 > tzDip + 18.0,
              fmt ("Add %.1f dB vs ", dip2) + fmt ("Sub %.1f dB", tzDip));

        // what a SINGLE-DECK flanger can manage on the same program, best case
        singleDeckBest = 0;
        for (int c : { 3 /*Hollow*/, 0 /*Silver*/ })
        { auto p3 = base(); p3.type = Flg::Jet; p3.character = c;
          p3.rate = rateFor (0.35f); p3.depth = 1.0f; p3.mix = 1.0f; p3.b6 = 0.0f; p3.b4 = 0.0f;
          auto s3 = stRms (run (p3, in).l, SETTLE, 3.0, 0.5);
          double m3 = 1e9; for (double x : s3) m3 = std::min (m3, x);
          singleDeckBest = std::min (singleDeckBest, db (m3 / std::max (1e-14, medianOf (s3)))); }
        gate ("...and it is >= 20 dB deeper than any single-deck comb",
              tzDip < singleDeckBest - 20.0,
              fmt ("through-zero %.1f dB vs ", tzDip) + fmt ("best single deck %.1f dB", singleDeckBest));

        // ★ THE NULL AT EVERY TONE CONTROL'S EXTREME, not just at defaults.
        //   A null can only be as deep as the two decks are IDENTICAL outside their
        //   delay difference, so anything that filters one leg and not the other caps
        //   it at exactly that filter's rejection - and it hides at the extremes, not
        //   at the middle. (The parallel Phaser build measured -37 dB at defaults and
        //   -14 dB with its Floor up, from exactly this class of asymmetry.)
        //   The audit of this engine: `Low Cut` and the M/S width are POST-SUM and
        //   linear (filtering both legs then summing is the same operation, so they
        //   cannot break symmetry); the in-loop damping / DC blocker / soft clip only
        //   exist on the feedback path; and the delay-path `Damping` is applied to
        //   BOTH decks with separate state whenever the Type is a matched two-deck
        //   machine. This row is what turns that argument into a number.
        struct EX { const char* what; float b4, b8, b3, b2, b6; float fb; };
        const EX ex[] = {
            { "defaults",           0.0f, 0.0f, 0.625f, 0.0f, 0.0f, 0.5f },
            { "Damping 100",        1.0f, 0.0f, 0.625f, 0.0f, 0.0f, 0.5f },
            { "Low Cut 100",        0.0f, 1.0f, 0.625f, 0.0f, 0.0f, 0.5f },
            { "Width 160",          0.0f, 0.0f, 1.000f, 0.0f, 0.0f, 0.5f },
            { "Spread 180",         0.0f, 0.0f, 0.625f, 1.0f, 0.0f, 0.5f },
            { "Bounce 100",         0.0f, 0.0f, 0.625f, 0.0f, 1.0f, 0.5f },
            { "everything at max",  1.0f, 1.0f, 1.000f, 1.0f, 1.0f, 0.5f }
        };
        double worstEx = -400.0; const char* worstName = "";
        std::string exDetail;
        for (auto& e : ex)
        {
            auto q = base(); q.type = Flg::TapeZero; q.character = 0;
            q.rate = rateFor (0.35f); q.depth = 1.0f; q.b1 = 0.5f; q.mix = 1.0f;
            q.b4 = e.b4; q.b8 = e.b8; q.b3 = e.b3; q.b2 = e.b2; q.b6 = e.b6; q.feedback = e.fb;
            auto so = stRms (run (q, in).l, SETTLE, 3.0, 0.5);
            double m = 1e9; for (double x : so) m = std::min (m, x);
            const double d = db (m / std::max (1e-14, medianOf (so)));
            exDetail += std::string (e.what) + fmt (" %.0f  ", d);
            if (d > worstEx) { worstEx = d; worstName = e.what; }
        }
        gate ("the null survives every tone control at its EXTREME (> 25 dB)",
              worstEx < -25.0,
              fmt ("worst %.1f dB", worstEx) + " (" + worstName + ")  |  " + exDetail + "dB");

        // ⚠️ REGENERATION AND A PERFECT NULL ARE MUTUALLY EXCLUSIVE ON A TWO-DECK
        //    MACHINE, and that is the physics, not a defect. The reference deck reads
        //    the CLEAN line and the lag deck reads the recirculating one, so with the
        //    loop open the two legs are identical at Delta = 0 and cancel exactly; with
        //    regeneration up they are not, and the null fills by exactly the amount of
        //    recirculated signal. Real tape flanging has no feedback path at all.
        //    Feeding both decks off the loop WOULD restore the null under regeneration
        //    - and would put the numerator's zero on top of the denominator's pole, so
        //    Feedback would stop doing anything on Sub. Measured both ways; this is the
        //    trade that was chosen, and the factory presets keep Itchycoo near centre.
        double fbNull[3];
        { int k = 0;
          for (float fb : { 0.5f, 0.75f, 1.0f })
          { auto q = base(); q.type = Flg::TapeZero; q.character = 0;
            q.rate = rateFor (0.35f); q.depth = 1.0f; q.b1 = 0.5f; q.mix = 1.0f;
            q.b4 = 0.0f; q.b6 = 0.0f; q.b8 = 0.0f; q.feedback = fb;
            auto so = stRms (run (q, in).l, SETTLE, 3.0, 0.5);
            double m = 1e9; for (double x : so) m = std::min (m, x);
            fbNull[k++] = db (m / std::max (1e-14, medianOf (so))); } }
        gate ("regeneration fills the null gracefully; it never stops being an event",
              fbNull[2] < -10.0 && fbNull[0] < fbNull[2],
              fmt ("Feedback 0 %.0f dB", fbNull[0]) + fmt (" | 50 %.0f dB", fbNull[1])
              + fmt (" | 100 %.0f dB  (documented trade, not a defect)", fbNull[2]));

        // and Feedback must not be a DEAD knob on the flagship Type (law 1 is per-knob,
        // and every other Feedback row in this harness is measured on Jet)
        { auto nz = noise ((int) (FS * 4.0f));
          auto q = base(); q.type = Flg::TapeZero; q.character = 0;
          // ⚠️ Depth 0, i.e. a STATIC comb - the same protocol the Jet row uses. A
          //    SWEEPING resonance smears across the analysis bands and under-reads by
          //    ~12 dB, which is what made this row look like a dead knob (it measured
          //    +6.8 dB swept and +18.1 dB static on the identical engine).
          q.rate = rateFor (0.02f); q.depth = 0.0f; q.b1 = 0.68f; q.mix = 1.0f;
          q.b4 = 0.0f; q.b6 = 0.0f;
          q.feedback = 0.5f; const double r0 = peakOverMedian (run (q, nz).l, SETTLE);
          q.feedback = 1.0f; const double r1 = peakOverMedian (run (q, nz).l, SETTLE);
          gate ("Feedback is alive on Tape Zero too (not just on Jet)", r1 - r0 > 12.0,
                fmt ("peak/median %.1f -> ", r0) + fmt ("%.1f dB", r1)); }
    }

    {   auto p = base(); p.type = Flg::Jet; p.character = 0;
        p.rate = rateFor (0.03f); p.depth = 0.0f; p.b1 = 0.62f; p.mix = 1.0f; p.b4 = 0.0f;
        auto in = noise ((int) (FS * 4.0f));
        p.feedback = 0.5f;  const double pv0 = peakOverMedian (run (p, in).l, SETTLE);
        p.feedback = 1.0f;  const double pv1 = peakOverMedian (run (p, in).l, SETTLE);
        gate ("Jet: regeneration sings (peak over median >= 20 dB)", pv1 > 20.0 && pv1 > pv0 + 12.0,
              fmt ("fb 0 -> %.1f dB, ", pv0) + fmt ("fb +100 -> %.1f dB", pv1));
    }

    {   // ★ NEGATIVE vs POSITIVE FEEDBACK — the comb GEOGRAPHY flips. With Delta pinned
        //   at 1.000 ms the resonant series sits at k/D (1,2,3… kHz) for g>0 and at
        //   (2k+1)/2D (1.5, 2.5, 3.5… kHz) for g<0. Two fixed probe combs, half a
        //   spacing apart, and the ADVANTAGE must change sign.
        auto p = base(); p.type = Flg::Jet; p.character = 0;
        p.rate = rateFor (0.02f); p.depth = 0.0f; p.mix = 1.0f; p.b4 = 0.0f; p.b6 = 0.0f;
        p.b1 = (float) (std::log (10.0) / std::log (200.0));      // 0.1 * 200^t = 1.0 ms
        auto in = noise ((int) (FS * 4.0f));
        auto probe = [&] (float fb, int chr) {
            p.feedback = fb; p.character = chr; auto o = run (p, in);
            const size_t N = 16384;
            auto m = frameMag (o.l, SETTLE + 20000, N);
            auto at = [&] (double hz) { const size_t i = (size_t) (hz * (double) N / FS);
                double a = 0; for (size_t k = i - 2; k <= i + 2; ++k) a += m[k] * m[k];
                return db (std::sqrt (a / 5.0)); };
            double on = 0, off = 0;
            for (int k = 2; k <= 6; ++k) { on += at (1000.0 * k); off += at (1000.0 * k + 500.0); }
            return (on - off) / 5.0; };
        const double gp = probe (1.0f, 0), gn = probe (0.0f, 0);
        gate ("Feedback polarity INVERTS the comb geography", gp > 6.0 && gn < -6.0,
              fmt ("+100%%: k/D series %+.1f dB", gp) + fmt (" | -100%%: %+.1f dB", gn)
              + fmt (" | swing %.1f dB", gp - gn));
        // ⚠️ and the FEEDFORWARD polarity has to be probed at ZERO feedback. At |g| ~ 1
        //    the pole's 1/(1-|g|) = 30+ dB swamps the +-1 numerator term, so `Hollow`
        //    and `Silver` measure the SAME emphasis with regeneration up - which is
        //    correct physics, not a bug, and it is why this is a separate row.
        const double sAdd = probe (0.5f, 0), sSub = probe (0.5f, 3);
        gate ("at zero feedback the Character polarity owns the geography",
              sAdd > 6.0 && sSub < -6.0,
              fmt ("Silver (add) %+.1f dB", sAdd) + fmt (" | Hollow (sub) %+.1f dB", sSub));
    }

    {   auto in = noise ((int) (FS * 3.0f));
        auto slope = [&] (int type, int chr) {
            auto p = base(); p.type = type; p.character = chr;
            p.rate = rateFor (0.02f); p.depth = 0.0f; p.mix = 1.0f; p.b4 = 0.0f; p.b6 = 0.0f;
            p.b1 = 0.15f; const double a = hfRatio (run (p, in).l, SETTLE);
            p.b1 = 0.95f; const double b = hfRatio (run (p, in).l, SETTLE);
            return a - b; };
        const double sb = slope (Flg::Bbd, 0), sj = slope (Flg::Jet, 0);
        gate ("BBD: the band-limit TRACKS the delay (>= 8 dB)", sb > 8.0,
              fmt ("BBD %.1f dB", sb) + fmt (" vs Jet %.1f dB across the same Manual travel", sj));
        gate ("...and that slope is BBD-specific", sb > sj + 6.0, fmt ("margin %.1f dB", sb - sj));
    }
    {   auto lvlAt = [&] (int type, float rms) {
            auto p = base(); p.type = type; p.character = 0;
            p.rate = rateFor (0.02f); p.depth = 0.0f; p.mix = 1.0f; p.b6 = 0.0f;
            auto in = chord ((int) (FS * 2.0f), rms);
            return db (rmsOf (run (p, in).l, SETTLE) / std::max (1e-12, rmsOf (in, SETTLE))); };
        const double bl = lvlAt (Flg::Bbd, 0.005f), bh = lvlAt (Flg::Bbd, 0.20f);
        const double jl = lvlAt (Flg::Jet, 0.005f), jh = lvlAt (Flg::Jet, 0.20f);
        gate ("BBD compands (gain falls with level); Jet is linear",
              (bl - bh) > 2.0 && std::fabs (jl - jh) < 1.0,
              fmt ("BBD -46 -> -14 dBFS: %+.1f dB gain change", bh - bl) + fmt (" | Jet %+.1f dB", jh - jl));
    }

    Motion mo[Flg::kNumTypes];
    {   // ★ THE COMB TRACKER, run once over all six Types on one program. Endless must
        //   never reverse; Step must be impulsive; the rest sweep continuously.
        auto in = noise ((int) (FS * 14.0f));
        for (int t = 0; t < Flg::kNumTypes; ++t)
        {
            auto p = base(); p.type = t; p.character = 0; p.mix = 1.0f; p.feedback = 0.62f;
            p.depth = 0.85f; p.rate = rateFor (0.25f);
            p.b1 = 0.55f; p.b5 = 0.25f; p.b6 = 0.0f; p.b4 = 0.1f;
            mo[t] = motionOf (combTraceMs (run (p, in).l, SETTLE, 2048, 1024, 0.12, 14.0));
        }
        std::string a1, a2;
        for (int t = 0; t < Flg::kNumTypes; ++t)
        { a1 += std::string (TN (t)) + " " + fmt ("%.2f ", mo[t].monoFrac);
          a2 += std::string (TN (t)) + " " + fmt ("%.1f ", mo[t].jumpRatio); }
        double bestOtherMono = 0, bestOtherJump = 0;
        for (int t = 0; t < Flg::kNumTypes; ++t)
        { if (t != Flg::Endless) bestOtherMono = std::max (bestOtherMono, mo[t].monoFrac);
          if (t != Flg::Step)    bestOtherJump = std::max (bestOtherJump, mo[t].jumpRatio); }
        gate ("Endless: the comb NEVER reverses (>= 0.80 one-signed)",
              mo[Flg::Endless].monoFrac >= 0.80 && mo[Flg::Endless].monoFrac > bestOtherMono + 0.25,
              fmt ("%.2f", mo[Flg::Endless].monoFrac) + " | " + a1);
        gate ("Step: the comb JUMPS, it does not sweep",
              mo[Flg::Step].jumpRatio > bestOtherJump * 1.5,
              fmt ("jump ratio %.1f vs next %.1f", mo[Flg::Step].jumpRatio) + fmt (" %.1f", bestOtherJump)
              + " | " + a2);
    }

    {   // ENVELOPE — the comb tracks the PLAYING, measured on the comb trace itself.
        auto in = amNoise ((int) (FS * 20.0f), 0.9f);
        std::vector<double> envTrace;
        { const size_t N = 2048, HOP = 512;
          for (size_t q = SETTLE; q + N < in.size(); q += HOP)
          { double a = 0; for (size_t i = 0; i < N; ++i) a += (double) in[q + i] * in[q + i];
            envTrace.push_back (std::log (std::sqrt (a / (double) N) + 1e-9)); } }
        double corr[Flg::kNumTypes];
        for (int t = 0; t < Flg::kNumTypes; ++t)
        { auto p = base(); p.type = t; p.character = 0; p.mix = 1.0f; p.feedback = 0.6f;
          p.depth = 0.9f; p.rate = rateFor (t == Flg::Envelope ? 8.0f : 0.5f);
          p.b1 = 0.42f; p.b6 = 0.0f; p.b4 = 0.1f; p.b7 = 0.0f;
          // 43 ms frames against a 588 ms modulation: short enough that the comb is
          // effectively stationary inside one frame (the 170 ms frame used elsewhere
          // smeared the autocorrelation peak and read r = 0.26 on a working Type).
          auto tr = combTraceMs (run (p, in).l, SETTLE, 2048, 512, 0.20, 20.0);
          std::vector<double> lg; for (double x : tr) lg.push_back (std::log (std::max (1e-4, x)));
          // the follower has an attack and a release, so the comb LAGS the program by a
          // few ms. A lag search over +-8 frames (86 ms) keeps the metric honest about
          // "does it track" instead of penalising an inaudible latency.
          double bestR = 0;
          for (int L = -8; L <= 8; ++L)
          { std::vector<double> a2, b2;
            for (size_t i = 0; i < lg.size(); ++i)
            { const long j = (long) i + L;
              if (j < 0 || j >= (long) envTrace.size()) continue;
              a2.push_back (lg[i]); b2.push_back (envTrace[(size_t) j]); }
            bestR = std::max (bestR, std::fabs (pearson (a2, b2))); }
          corr[t] = bestR; }
        std::string all;
        for (int t = 0; t < Flg::kNumTypes; ++t) all += std::string (TN (t)) + " " + fmt ("%.2f ", corr[t]);
        double bestOther = 0;
        for (int t = 0; t < Flg::kNumTypes; ++t) if (t != Flg::Envelope) bestOther = std::max (bestOther, corr[t]);
        gate ("Envelope: the comb position tracks the input envelope (r >= 0.80)",
              corr[Flg::Envelope] >= 0.80 && corr[Flg::Envelope] > bestOther + 0.30,
              fmt ("r = %.2f", corr[Flg::Envelope]) + " | " + all);
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("C. CROSS-TYPE DISTINCTNESS MATRIX (every pair, phase-independent)");
    {
        auto in = mixedProbe ((int) (FS * 6.0f));
        Print pr[Flg::kNumTypes];
        for (int t = 0; t < Flg::kNumTypes; ++t)
        { auto p = base(); p.type = t; p.character = 0;
          p.rate = rateFor (0.5f); p.depth = 0.75f; p.feedback = 0.80f; p.mix = 1.0f;
          p.b1 = 0.5f; p.b2 = 0.4f; p.b4 = 0.25f; p.b5 = 0.2f; p.b6 = 0.25f; p.b7 = 0.4f;
          pr[t] = fingerprint (run (p, in), SETTLE); }
        std::printf ("        %-11s", "");
        for (int b2 = 0; b2 < Flg::kNumTypes; ++b2) std::printf ("%-11.10s", TN (b2));
        std::printf ("\n");
        double worst = 1e9; int wa = 0, wb = 0;
        for (int a = 0; a < Flg::kNumTypes; ++a)
        { std::printf ("        %-11.10s", TN (a));
          for (int b2 = 0; b2 < Flg::kNumTypes; ++b2)
          { if (a == b2) { std::printf ("%-11s", "   -"); continue; }
            const double d = printDist (pr[a], pr[b2]);
            std::printf ("%-11.1f", d);
            if (b2 > a && d < worst) { worst = d; wa = a; wb = b2; } }
          std::printf ("\n"); }
        gate ("every Type pair is distinguishable (gate 4.0 dB)", worst > 4.0,
              fmt ("closest pair %.1f dB", worst) + " (" + TN (wa) + " / " + TN (wb) + ")");
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("D. Every parameter evolves 0 -> 100, monotonic and dramatic (law 1)");

    {   auto in = noise ((int) (FS * 8.0f));
        std::vector<double> v;
        for (float r : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = r; p.depth = 0.8f; p.mix = 1.0f;
          p.b1 = 0.5f; p.b6 = 0.0f; p.feedback = 0.6f;
          v.push_back (motionOf (combTraceMs (run (p, in).l, SETTLE, 4096, 1024)).reversals); }
        gate ("Rate 0->100 speeds the sweep, monotonically",
              monotoneUp (v, 1.0) && v.back() > v.front() + 8.0, traj (v, "sweep reversals"));
    }
    {   auto in = noise ((int) (FS * 6.0f));
        std::vector<double> v;
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.4f); p.depth = d; p.mix = 1.0f;
          p.b1 = 0.5f; p.b6 = 0.0f; p.feedback = 0.6f;
          v.push_back (motionOf (combTraceMs (run (p, in).l, SETTLE, 8192, 2048)).rangeCents); }
        gate ("Depth 0->100 widens the sweep, monotonically",
              monotoneUp (v, 60.0) && v.back() - v.front() > 2400.0, traj (v, "cents of comb travel"));
    }
    {   auto in = noise ((int) (FS * 4.0f));
        auto pv = [&] (float fb) {
            auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.02f); p.depth = 0.0f;
            p.b1 = 0.62f; p.b4 = 0.0f; p.b6 = 0.0f; p.mix = 1.0f; p.feedback = fb;
            return peakOverMedian (run (p, in).l, SETTLE); };
        std::vector<double> up, dn;
        for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) up.push_back (pv (0.5f + 0.5f * t));
        for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) dn.push_back (pv (0.5f - 0.5f * t));
        gate ("Feedback 0->+100 deepens the comb monotonically",
              monotoneUp (up, 1.0) && up.back() - up.front() > 14.0, traj (up, "dB peak/median"));
        gate ("Feedback 0->-100 deepens the comb monotonically",
              monotoneUp (dn, 1.0) && dn.back() - dn.front() > 14.0, traj (dn, "dB peak/median"));
    }
    {   auto in = noise ((int) (FS * 4.0f));
        auto dry = avgSpectrum (in, SETTLE, 96, 60.0, 16000.0, 32768);
        std::vector<double> v;
        for (float m : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.02f); p.depth = 0.0f;
          p.character = 3; p.b1 = 0.62f; p.feedback = 0.97f; p.b4 = 0.0f; p.b6 = 0.0f; p.mix = m;
          v.push_back (specDist (avgSpectrum (run (p, in).l, SETTLE, 96, 60.0, 16000.0, 32768), dry)); }
        gate ("Mix 0->100 moves the spectrum monotonically",
              monotoneUp (v, 0.8) && v.back() > 12.0, traj (v, "dB from dry"));
    }
    {   auto in = noise ((int) (FS * 3.0f));
        std::vector<double> v;
        for (float b1 : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.02f); p.depth = 0.0f;
          p.b1 = b1; p.feedback = 0.72f; p.b4 = 0.0f; p.b6 = 0.0f; p.mix = 1.0f;
          v.push_back (medianOf (combTraceMs (run (p, in).l, SETTLE, 8192, 2048))); }
        gate ("Manual 0->100 walks the comb from 0.1 ms to 20 ms",
              monotoneUp (v, 0.02) && v.back() > v.front() * 50.0, traj (v, "ms comb delay"));
    }
    {   auto in = chord ((int) (FS * 4.0f));
        std::vector<double> v;
        for (float s : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.6f); p.depth = 0.8f;
          p.b2 = s; p.b3 = 0.625f; p.feedback = 0.8f; p.mix = 1.0f; p.b6 = 0.0f;
          v.push_back (stereoStats (run (p, in), SETTLE).first); }
        gate ("Spread 0->180 deg decorrelates L/R monotonically",
              monotoneUp (v, 1.5) && v.back() - v.front() > 30.0, traj (v, "dB L-R"));
    }
    {   auto in = chord ((int) (FS * 4.0f));
        std::vector<double> v;
        for (float w : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.6f); p.depth = 0.8f;
          p.b2 = 0.6f; p.b3 = w; p.feedback = 0.7f; p.mix = 1.0f; p.b6 = 0.0f;
          v.push_back (stereoStats (run (p, in), SETTLE).first); }
        gate ("Width 0->160 % widens the wet monotonically",
              monotoneUp (v, 1.0) && v.back() - v.front() > 24.0, traj (v, "dB L-R"));
    }
    {   // DAMPING — the knob that replaced the bible's `Tone`. It must eat the comb's
        //   HIGH teeth (a physics change), not tilt the output (an EQ change): the
        //   3-12 kHz peak-to-valley collapses while the low comb survives.
        auto in = noise ((int) (FS * 3.0f));
        std::vector<double> hi, lo, hr;
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.02f); p.depth = 0.0f;
          p.b1 = 0.62f; p.feedback = 0.95f; p.b4 = d; p.b6 = 0.0f; p.mix = 1.0f;
          auto o = run (p, in);
          hi.push_back (peakToValley (o.l, SETTLE, 4000.0, 15000.0));
          lo.push_back (peakToValley (o.l, SETTLE, 200.0, 900.0));
          hr.push_back (hfRatio (o.l, SETTLE)); }
        gate ("Damping 0->100 kills the comb's HIGH teeth, monotonically",
              monotoneDown (hi, 1.5) && hi.front() - hi.back() > 14.0, traj (hi, "dB P-V 4-15 kHz"));
        note ("  ...while the low comb survives (physics, not EQ)", traj (lo, "dB P-V 0.2-0.9 kHz"));
        note ("  ...and the wet darkens overall", traj (hr, "dB HF ratio"));
    }
    {   // SHAPE — the LFO WAVEFORM. Measured on the comb trace's own motion statistics:
        //   a sine's speed is smooth, a ramp spends 8 % of the cycle returning.
        // ⚠️ the LFO is deliberately SLOW here: the comb tracker needs the sweep to be
        //    near-stationary inside one analysis frame, and a fast sweep smears the
        //    autocorrelation peak into noise (at 0.35 Hz this row read 19 where the
        //    theory says 1.4, purely from tracker jitter).
        auto in = noise ((int) (FS * 26.0f));
        std::vector<double> v;
        for (float s : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.12f); p.depth = 0.60f;
          p.b1 = 0.5f; p.b5 = s; p.feedback = 0.55f; p.mix = 1.0f; p.b6 = 0.0f; p.b4 = 0.0f;
          v.push_back (motionOf (combTraceMs (run (p, in).l, SETTLE, 2048, 1024, 0.15, 14.0)).crest); }
        gate ("Shape 0->100 morphs the LFO waveform monotonically",
              monotoneUp (v, 0.25) && v.back() > v.front() * 1.8, traj (v, "crest of the sweep speed"));
    }
    {   // BOUNCE — the servo hunt + the tape drift stack. ⚠️ The obvious metric
        //   (aperiodicity) is WRONG for this knob: the Eventide servo overshoot is
        //   deterministic, so it repeats perfectly every LFO cycle and reads 0.000 at
        //   every setting while being the loudest thing the knob does. What Bounce
        //   actually changes is the SHAPE OF THE SWEEP TRAJECTORY, so the metric is how
        //   far the comb's path departs from the un-bounced path, as a fraction of the
        //   sweep's own excursion — measured in ms of comb position, the quantity the
        //   ear is following. (Aperiodicity is still reported, as the drift's share.)
        auto in = noise ((int) (FS * 20.0f));
        auto trace = [&] (float b) {
            auto p = base(); p.type = Flg::TapeZero; p.character = 0;
            p.rate = rateFor (0.5f); p.depth = 0.35f; p.b1 = 0.95f; p.b6 = b;
            p.feedback = 0.5f; p.mix = 1.0f; p.b4 = 0.0f;
            return combTraceMs (run (p, in).l, SETTLE, 2048, 512, 1.0, 14.0); };
        auto ref = trace (0.0f);
        double refSd = 0, refMu = 0;
        for (double x : ref) refMu += x; refMu /= (double) std::max<size_t> (1, ref.size());
        for (double x : ref) refSd += (x - refMu) * (x - refMu);
        refSd = std::sqrt (refSd / (double) std::max<size_t> (1, ref.size()));
        std::vector<double> v, ap;
        for (float b : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto t = trace (b);
            double acc = 0; size_t m = std::min (t.size(), ref.size());
            for (size_t i = 0; i < m; ++i) acc += (t[i] - ref[i]) * (t[i] - ref[i]);
            v.push_back (100.0 * std::sqrt (acc / (double) std::max<size_t> (1, m)) / std::max (1e-6, refSd));
            const size_t lag = (size_t) std::lround ((FS / 0.5) / 512.0);
            std::vector<double> a2, c2;
            for (size_t i = 0; i + lag < t.size(); ++i) { a2.push_back (t[i]); c2.push_back (t[i + lag]); }
            ap.push_back (100.0 * (1.0 - pearson (a2, c2)));
        }
        gate ("Bounce 0->100 reshapes the sweep trajectory, monotonically",
              monotoneUp (v, 1.5) && v.back() > 25.0, traj (v, "% of the sweep's own excursion"));
        note ("  ...of which the aperiodic (tape drift) share is", traj (ap, "% non-repeating"));
    }
    {   // TAIL — T40: how long after the input stops the ring takes to fall 40 dB.
        std::vector<double> v;
        for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.02f); p.depth = 0.0f;
          p.b1 = 0.75f; p.feedback = 1.0f; p.b4 = 0.05f; p.b6 = 0.0f; p.b7 = t; p.mix = 1.0f;
          const int n = (int) (FS * 26.0f);
          std::vector<float> in ((size_t) n, 0.0f);
          auto src = noise ((int) (FS * 2.0f));
          for (size_t i = 0; i < src.size(); ++i) in[i] = src[i];
          auto o = run (p, in);
          const size_t stop = src.size();
          auto s = stRms (o.l, stop - (size_t) (FS * 0.06f), 50.0, 25.0);
          const double ref = s.empty() ? 0 : s[0];
          double t40 = 24.0;
          for (size_t i = 1; i < s.size(); ++i)
            if (s[i] < ref * 0.01) { t40 = (double) i * 0.025; break; }
          v.push_back (t40); }
        // ═══ fb419 — TAIL BECAME DRIVE, and this gate had to be rebuilt, not retuned ═══════
        // What it used to assert was TRUE and USELESS: Tail lengthened the ring, but ONLY at
        // maximum feedback and ONLY after the input stopped — which is the corner nobody
        // plays in. Held-note travel was 0.00 dB from 0 to 100 (fb417, Tests/fx3_audibility).
        // The ring is now a fixed 400 ms release, so this trajectory is deliberately FLAT:
        //   1.1 > 1.1 > 1.1 > 1.1 > 1.1 s to -40 dB
        // and the knob spends its travel on something audible while you play instead.
        gate ("the feedback gate still kills a runaway (fixed 400 ms release)",
              v.back() > 0.3 && v.back() < 6.0, traj (v, "s to -40 dB (flat BY DESIGN)"));
    }
    {   // ── DRIVE: in-loop saturation. TWO claims, and the second is the load-bearing one.
        std::vector<double> hr, dec;
        for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            // (1) does it dirty the regeneration? Feed a tone, look at what comes back.
            // ⚠️ THE PROBE PITCH IS PART OF THE MEASUREMENT. hfRatio counts energy above 2 kHz,
            // and a saturator's harmonics of a 220 Hz tone land at 660 / 1100 / 1540 Hz — ALL
            // BELOW THE BAND. The first run of this gate read −85.9 → −86.4 dB and looked like
            // a dead knob when it was a blind detector. 1 kHz puts the 3rd at 3 kHz.
            {   auto in = tone ((int) (FS * 3.0f), 1000.0f);
                auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.02f); p.depth = 0.0f;
                p.b1 = 0.5f; p.feedback = 0.97f; p.b4 = 0.0f; p.b6 = 0.0f; p.b7 = t; p.mix = 1.0f;
                hr.push_back (hfRatio (run (p, in).l, SETTLE)); }
            // (2) 🔑 does the LOOP GAIN move? The in-loop makeup is tanh(x·g)/g, which has unit
            //     slope at zero, so the decay time MUST be the same at every Drive setting. If
            //     it lengthened, Drive would be a second, secret feedback control — and the
            //     60 s stability gate would be measuring a lie.
            {   auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.02f); p.depth = 0.0f;
                p.b1 = 0.75f; p.feedback = 1.0f; p.b4 = 0.05f; p.b6 = 0.0f; p.b7 = t; p.mix = 1.0f;
                const int n = (int) (FS * 20.0f);
                std::vector<float> in ((size_t) n, 0.0f);
                auto src = noise ((int) (FS * 2.0f));
                for (size_t i = 0; i < src.size(); ++i) in[i] = src[i];
                auto o = run (p, in);
                auto st = stRms (o.l, src.size() - (size_t) (FS * 0.06f), 50.0, 25.0);
                const double ref = st.empty() ? 0 : st[0];
                double t40 = 18.0;
                for (size_t i = 1; i < st.size(); ++i)
                  if (st[i] < ref * 0.01) { t40 = (double) i * 0.025; break; }
                dec.push_back (t40); }
        }
        gate ("Drive 0->100 dirties the regeneration, monotonically",
              monotoneUp (hr, 0.4) && hr.back() - hr.front() > 3.0, traj (hr, "dB HF in the loop"));
        double lo = dec[0], hi = dec[0];
        for (double d : dec) { lo = std::min (lo, d); hi = std::max (hi, d); }
        gate ("   ... and the LOOP GAIN is untouched — the makeup is inside the loop",
              hi - lo < 0.35, traj (dec, "s to -40 dB (must not move)"));
    }
    {   auto in = noise ((int) (FS * 3.0f));
        std::vector<double> v;
        for (float l : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        { auto p = base(); p.type = Flg::Jet; p.rate = rateFor (0.02f); p.depth = 0.0f;
          p.b1 = 0.75f; p.feedback = 0.9f; p.b4 = 0.2f; p.b6 = 0.0f; p.b8 = l; p.mix = 1.0f;
          v.push_back (lfRatio (run (p, in).l, SETTLE)); }
        gate ("Low Cut 0->100 removes the bass, monotonically",
              monotoneDown (v, 0.5) && v.front() - v.back() > 15.0, traj (v, "dB LF ratio"));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("E. Characters change PHYSICS, not EQ (R4 / law 4)");
    {
        auto in = mixedProbe ((int) (FS * 8.0f));
        double worstAll = 1e9; int wt = 0, wa = 0, wb = 0;
        for (int t = 0; t < Flg::kNumTypes; ++t)
        {
            Print pr[Flg::kNumChars];
            for (int c = 0; c < Flg::kNumChars; ++c)
            { auto p = base(); p.type = t; p.character = c;
              p.rate = rateFor (t == Flg::Endless ? 0.20f : (t == Flg::Step ? 2.5f : 0.5f));
              p.depth = 0.8f; p.feedback = 0.82f; p.mix = 1.0f;
              p.b1 = 0.5f; p.b2 = 0.4f; p.b4 = 0.25f; p.b5 = 0.45f; p.b6 = 0.35f; p.b7 = 0.4f;
              pr[c] = fingerprint (run (p, in), SETTLE); }
            double worst = 1e9; int ca = 0, cb = 0;
            for (int a = 0; a < Flg::kNumChars; ++a)
                for (int b2 = a + 1; b2 < Flg::kNumChars; ++b2)
                { const double d = printDist (pr[a], pr[b2]); if (d < worst) { worst = d; ca = a; cb = b2; } }
            gate ((std::string ("all 8 ") + TN (t) + " Characters differ from each other").c_str(), worst > 1.5,
                  fmt ("closest pair %.1f dB", worst) + " (" + CN (t, ca) + " / " + CN (t, cb) + ")");
            if (worst < worstAll) { worstAll = worst; wt = t; wa = ca; wb = cb; }
        }
        note ("worst Character pair across all 48 voicings",
              fmt ("%.1f dB", worstAll) + std::string ("  ") + TN (wt) + ": " + CN (wt, wa) + " / " + CN (wt, wb));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("F. No clicks — every param swept under a sustained tone (law 4)");
    {
        struct KP { const char* name; std::function<void (P&, float)> set; };
        const KP knobs[] = {
            { "Rate",     [] (P& p, float t) { p.rate = t; } },
            { "Depth",    [] (P& p, float t) { p.depth = t; } },
            { "Feedback", [] (P& p, float t) { p.feedback = t; } },
            { "Mix",      [] (P& p, float t) { p.mix = t; } },
            { "Manual",   [] (P& p, float t) { p.b1 = t; } },
            { "Spread",   [] (P& p, float t) { p.b2 = t; } },
            { "Width",    [] (P& p, float t) { p.b3 = t; } },
            { "Damping",  [] (P& p, float t) { p.b4 = t; } },
            { "Shape",    [] (P& p, float t) { p.b5 = t; } },
            { "Bounce",   [] (P& p, float t) { p.b6 = t; } },
            { "Drive",    [] (P& p, float t) { p.b7 = t; } },   // fb419 — was Tail
            { "Low Cut",  [] (P& p, float t) { p.b8 = t; } },
        };
        auto in = tone ((int) (FS * 4.0f), 330.0f);
        for (auto& k : knobs)
        {
            double worstRatio = 0; int wt = -1;
            for (int t = 0; t < Flg::kNumTypes; ++t)
            { auto p = base(); p.type = t; p.character = 0; p.mix = 0.6f;
              p.rate = rateFor (0.5f); p.depth = 0.6f; p.feedback = 0.75f; p.b6 = 0.2f;
              auto o = runSweep (p, in, k.set);
              double worst = 0;
              for (size_t i = SETTLE + 1; i < o.l.size(); ++i)
                  worst = std::max (worst, (double) std::fabs (o.l[i] - o.l[i - 1]));
              const double ratio = worst / std::max (1e-9, rmsOf (o.l, SETTLE));
              if (ratio > worstRatio) { worstRatio = ratio; wt = t; } }
            gate ((std::string ("sweeping ") + k.name + " 0->100 does not click").c_str(), worstRatio < 12.0,
                  fmt ("worst step / program RMS = %.1f", worstRatio) + " (gate 12, " + TN (wt) + ")");
        }
    }
    {
        auto in = chord ((int) (FS * 4.0f));
        auto swapTest = [&] (bool byType) {
            double worst = -99;
            const int lim = byType ? Flg::kNumTypes : Flg::kNumChars;
            for (int k = 0; k < lim; ++k)
            {
                Flg e; e.prepare (FS, 128);
                auto p = base(); p.mix = 0.7f; p.depth = 0.6f; p.feedback = 0.8f;
                if (byType) { p.type = k; p.character = 0; } else { p.type = Flg::Jet; p.character = k; }
                std::vector<float> bl (128), br (128);
                double ref = 0, pk = 0; const size_t half = in.size() / 2;
                for (size_t i = 0; i + 128 <= in.size(); i += 128)
                {
                    if (i >= half) { if (byType) p.type = (k + 1) % Flg::kNumTypes;
                                     else p.character = (k + 1) % Flg::kNumChars; }
                    for (int q = 0; q < 128; ++q) { bl[(size_t) q] = in[i + (size_t) q]; br[(size_t) q] = bl[(size_t) q]; }
                    e.setParams (p); e.processStereo (bl.data(), br.data(), 128);
                    for (int q = 0; q < 128; ++q)
                    { const double a = std::fabs (bl[(size_t) q]);
                      if (i + (size_t) q < half - 4000) ref = std::max (ref, a);
                      else if (i + (size_t) q < half + 12000) pk = std::max (pk, a); }
                }
                worst = std::max (worst, db (pk / std::max (1e-9, ref)));
            }
            return worst; };
        gate ("Type swap mid-note never bangs (< +3 dB)", swapTest (true) < 3.0, fmt ("worst %+.2f dB", swapTest (true)));
        gate ("Character swap mid-note never bangs (< +3 dB)", swapTest (false) < 3.0, fmt ("worst %+.2f dB", swapTest (false)));
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("G. Mono-safe (law 5) — per Type and per Character");
    {
        auto in = chord ((int) (FS * 4.0f));
        auto sweep = [&] (float spread, int t, int c) {
            auto p = base(); p.type = t; p.character = c; p.mix = 1.0f;
            p.rate = rateFor (0.5f); p.depth = 0.8f; p.feedback = 0.75f;
            p.b2 = spread; p.b3 = 0.625f; p.b6 = 0.2f;
            return stereoStats (run (p, in), SETTLE).second; };

        for (int t = 0; t < Flg::kNumTypes; ++t)
        {
            double worst = 99; int wc = 0;
            for (int c = 0; c < Flg::kNumChars; ++c)
            { const double m = sweep (0.35f, t, c); if (m < worst) { worst = m; wc = c; } }
            gate ((std::string ("mono sum survives on ") + TN (t) + " (default Spread)").c_str(),
                  worst > -6.0, fmt ("worst %.1f dB vs L", worst) + " (" + CN (t, wc) + ")"
                  + (Flg::monoHostile (t, wc) ? "  [tagged mono-hostile]" : ""));
        }
        // the deliberate counter-sweep voicings must THIN, never CANCEL
        double worst180 = 99; int wt = 0, wc2 = 0;
        for (int t = 0; t < Flg::kNumTypes; ++t)
            for (int c = 0; c < Flg::kNumChars; ++c)
            { const double m = sweep (1.0f, t, c); if (m < worst180) { worst180 = m; wt = t; wc2 = c; } }
        gate ("Spread 180 deg + Width 160 thins but never CANCELS (> -12 dB)", worst180 > -12.0,
              fmt ("worst %.1f dB vs L", worst180) + std::string ("  ") + TN (wt) + "/" + CN (wt, wc2));

        // ⚠️ THE TAG IS EARNED, NOT DECLARED. The three voicings whose channels
        //    deliberately counter-run must (a) genuinely decorrelate and (b) still
        //    survive a mono sum. Measured, all three do - so NONE of them carries the
        //    mono-hostile tag, and the tag list for this device is empty by measurement.
        auto lr = [&] (int t, int c) {
            auto p = base(); p.type = t; p.character = c; p.mix = 1.0f;
            p.rate = rateFor (0.5f); p.depth = 0.8f; p.feedback = 0.75f;
            p.b2 = 0.0f; p.b3 = 0.625f; p.b6 = 0.0f;   // Bounce OFF: the drift stack has its
            return stereoStats (run (p, in), SETTLE); };   // own L/R seeds and would mask this
        int counters = 0, decorr = 0, safe = 0; std::string detail;
        for (int t = 0; t < Flg::kNumTypes; ++t)
            for (int c = 0; c < Flg::kNumChars; ++c)
                if (Flg::counterLR (t, c))
                {
                    ++counters;
                    const auto me = lr (t, c); const auto ref = lr (t, 0);
                    if (me.first > ref.first + 20.0) ++decorr;
                    if (me.second > -6.0) ++safe;
                    detail += std::string (CN (t, c)) + fmt (" L-R %+.0f", me.first)
                            + fmt ("(vs %+.0f)", ref.first) + fmt (" mono %.1f  ", me.second);
                }
        gate ("counter-running Characters really do split L/R at Spread 0",
              counters > 0 && decorr == counters,
              std::to_string (decorr) + " of " + std::to_string (counters) + "  [" + detail + "dB]");
        gate ("...and every one of them still survives a mono sum",
              counters > 0 && safe == counters,
              std::to_string (safe) + " of " + std::to_string (counters) + " above -6 dB");
        int tagged = 0;
        for (int t = 0; t < Flg::kNumTypes; ++t)
            for (int c = 0; c < Flg::kNumChars; ++c) if (Flg::monoHostile (t, c)) ++tagged;
        note ("voicings carrying the mono-hostile tag",
              std::to_string (tagged) + " of 48  (the tag is set from MEASUREMENT; nothing in this "
              "device cancels on a mono sum, so nothing carries it)");
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("H. Stability — 60 s of full-feedback white noise, every Type (law 6)");
    {
        for (int t = 0; t < Flg::kNumTypes; ++t)
        {
            Flg e; e.prepare (FS, 128);
            auto p = base(); p.type = t; p.character = (t == Flg::Jet ? 4 : 0);
            p.rate = rateFor (3.0f); p.depth = 1.0f; p.feedback = 1.0f; p.mix = 1.0f;
            p.b1 = 0.4f; p.b4 = 0.0f; p.b6 = 1.0f; p.b7 = 1.0f; p.b8 = 0.0f;
            std::vector<float> bl (128), br (128);
            uint32_t st = 7u; double peak = 0; bool bad = false;
            const int blocks = (int) (60.0f * FS / 128.0f);
            for (int b = 0; b < blocks && ! bad; ++b)
            {
                for (int i = 0; i < 128; ++i)
                { st = st * 1664525u + 1013904223u;
                  bl[(size_t) i] = (((float) (st >> 8) / 8388608.0f) - 1.0f) * 0.05f; br[(size_t) i] = bl[(size_t) i]; }
                e.setParams (p); e.processStereo (bl.data(), br.data(), 128);
                for (int i = 0; i < 128; ++i)
                { const float v = bl[(size_t) i];
                  if (! std::isfinite (v)) { bad = true; break; }
                  peak = std::max (peak, (double) std::fabs (v)); }
            }
            gate ((std::string ("60 s at max feedback stays bounded: ") + TN (t)).c_str(),
                  ! bad && peak < 2.0,
                  bad ? std::string ("NaN/Inf") : fmt ("peak %.3f", peak) + fmt (" = %.1f dBFS", db (peak)));
        }
    }
    {
        for (int t = 0; t < Flg::kNumTypes; ++t)
        {
            auto p = base(); p.type = t; p.character = 0;
            p.rate = rateFor (0.5f); p.depth = 0.5f; p.feedback = 1.0f; p.b7 = 1.0f;
            p.b1 = 0.6f; p.b4 = 0.0f; p.b6 = 0.0f; p.mix = 1.0f;
            const int n = (int) (FS * 26.0f);
            std::vector<float> in ((size_t) n, 0.0f);
            auto src = noise ((int) (FS * 4.0f));
            for (size_t i = 0; i < src.size(); ++i) in[i] = src[i];
            auto o = run (p, in);
            const double late = db (rmsOf (o.l, (size_t) (FS * 22.0f)));
            gate ((std::string ("self-oscillation dies in silence: ") + TN (t)).c_str(), late < -60.0,
                  fmt ("%.1f dBFS 18 s after the input stops", late));
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("I. Sample rates — the engine is not 48 k-shaped");
    {
        for (float fs : { 44100.0f, 96000.0f })
        {
            Flg e; e.prepare ((double) fs, 128);
            auto p = base(); p.type = Flg::TapeZero; p.character = 0;
            p.rate = (float) (std::log (0.35 / 0.02) / std::log (1000.0));
            p.depth = 1.0f; p.b1 = 0.5f; p.b4 = 0.0f; p.b6 = 0.0f; p.feedback = 0.5f;
            p.mix = 1.0f; p.b8 = 0.0f;
            const int n = (int) (fs * 7.0f);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i)
            { float s = 0; const float f4[4] = { 110.0f, 130.81f, 164.81f, 220.0f };
              for (float fr : f4) for (int h = 1; h <= 14; ++h) s += std::sin (6.2831853f * fr * h * (float) i / fs) / (float) h;
              l[(size_t) i] = s * 0.008f; r[(size_t) i] = l[(size_t) i]; }
            for (int i = 0; i + 128 <= n; i += 128) { e.setParams (p); e.processStereo (l.data() + i, r.data() + i, 128); }
            auto s = stRms (l, (size_t) (0.35 * fs), 3.0, 0.5, fs);
            double mn = 1e9; for (double x : s) mn = std::min (mn, x);
            const double dip = db (mn / std::max (1e-14, medianOf (s)));
            gate ((fmt ("through-zero null holds at %.0f Hz", (double) fs)).c_str(), dip < -30.0, fmt ("%.1f dB", dip));
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("J. CPU — us per 128-sample block at 48 kHz, per Type (law 7)");
    {
        double worst = 0; int wt = 0;
        for (int t = 0; t < Flg::kNumTypes; ++t)
        {
            const int chr = (t == Flg::Jet) ? 7 : (t == Flg::Endless ? 5 : 0);   // the twin-tap voices
            Flg e; e.prepare (FS, 128);
            auto p = base(); p.type = t; p.character = chr; p.depth = 0.9f; p.feedback = 0.9f;
            p.rate = rateFor (1.0f); p.mix = 0.6f; p.b6 = 0.5f;
            std::vector<float> bl (128), br (128);
            for (int i = 0; i < 128; ++i) { bl[(size_t) i] = 0.05f * std::sin (0.07f * i); br[(size_t) i] = bl[(size_t) i]; }
            for (int b = 0; b < 500; ++b) { e.setParams (p); e.processStereo (bl.data(), br.data(), 128); }
            const int R = 20000;
            auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < R; ++b) { e.setParams (p); e.processStereo (bl.data(), br.data(), 128); }
            auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) R;
            note ((std::string ("  ") + TN (t) + " / " + CN (t, chr)).c_str(),
                  fmt ("%.2f us/block", us) + fmt ("  = %.3f %% of one core", us / (128.0 / FS * 1e6) * 100.0));
            if (us > worst) { worst = us; wt = t; }
        }
        // The floor for this architecture is ~11 us/block: 3-4 fractional reads per
        // channel plus the loop filters, and the reads are scattered across a 32 kB ring
        // so they are latency-bound, not flop-bound. BBD is the outlier and its extra is
        // the delay-tracking reconstruction cascade plus the compander.
        gate ("worst Type under 1 % of one core (< 25 us/block)", worst < 25.0,
              fmt ("%.2f us/block", worst) + fmt (" = %.2f %% core", worst / (128.0 / FS * 1e6) * 100.0)
              + " (" + TN (wt) + ")");
    }

    // ─────────────────────────────────────────────────────────────────────────
    section ("K. Self-check — can these gates actually fail?");
    {
        auto in = noise ((int) (FS * 3.0f));
        auto a = avgSpectrum (in, SETTLE);
        auto b = a; for (size_t i = 0; i < b.size(); i += 3) b[i] += 9.0;
        gate ("(self-check) specDist sees an injected 9 dB", specDist (a, b) > 8.5, fmt ("%.1f dB", specDist (a, b)));
    }
    {
        auto p = base(); p.type = Flg::Jet; p.mix = 0.6f;
        auto in = tone ((int) (FS * 2.0f), 330.0f);
        auto o = run (p, in);
        auto spike = o.l;
        for (size_t i = SETTLE + 1000; i < SETTLE + 1006 && i < spike.size(); ++i) spike[i] += 2.0f;
        double worst = 0;
        for (size_t i = SETTLE + 1; i < spike.size(); ++i) worst = std::max (worst, (double) std::fabs (spike[i] - spike[i - 1]));
        gate ("(self-check) the click metric sees an injected click",
              worst / std::max (1e-9, rmsOf (o.l, SETTLE)) > 12.0,
              fmt ("injected step / RMS = %.0f", worst / std::max (1e-9, rmsOf (o.l, SETTLE))));
    }
    {   // the comb tracker must recover a delay we planted by hand
        auto src = noise ((int) (FS * 2.0f));
        std::vector<float> y (src.size());
        const size_t D = 137;                              // 2.854 ms
        for (size_t i = 0; i < y.size(); ++i) y[i] = src[i] + (i >= D ? src[i - D] : 0.0f);
        const double got = medianOf (combTraceMs (y, SETTLE, 8192, 2048));
        gate ("(self-check) the comb tracker recovers a planted 2.854 ms delay",
              std::fabs (got - 2.854) < 0.05, fmt ("read %.3f ms", got));
    }
    {
        auto in = noise ((int) (FS * 3.0f));
        auto p = base(); p.type = Flg::Bbd; p.mix = 1.0f;
        auto o = run (p, in);
        const double d = printDist (fingerprint (o, SETTLE), fingerprint (o, SETTLE));
        gate ("(self-check) fingerprint distance to itself is 0", d < 1e-9, fmt ("%.2e dB", d));
    }
    {   // ⚠️ THE NULL DETECTOR ITSELF. This project has shipped a false green from a
        //   harness that could not fail, so the detector has to be shown BOTH ways:
        //   it must fire on a planted null and it must NOT fire on clean program.
        auto clean = chord ((int) (FS * 3.0f));
        auto planted = clean;
        // ⚠️ FLAT-BOTTOMED and WIDER than the 3 ms analysis window. The first version
        //    of this self-check planted a 4 ms cosine dip that only reached full depth
        //    at its midpoint, and the detector correctly read it as -7 dB - the probe
        //    was wrong, not the detector.
        const size_t at = (size_t) (FS * 1.5f);
        const size_t edge = (size_t) (FS * 0.002f), flat = (size_t) (FS * 0.008f);
        for (size_t i = 0; i < 2 * edge + flat && at + i < planted.size(); ++i)
        {
            double g2 = 0.001;
            if (i < edge)                    g2 = 0.001 + 0.999 * 0.5 * (1.0 + std::cos (3.14159265 * (double) i / (double) edge));
            else if (i >= edge + flat)       g2 = 0.001 + 0.999 * 0.5 * (1.0 - std::cos (3.14159265 * (double) (i - edge - flat) / (double) edge));
            planted[at + i] *= (float) g2;
        }
        auto sc = stRms (clean, SETTLE, 3.0, 0.5);
        auto sp = stRms (planted, SETTLE, 3.0, 0.5);
        double mc = 1e9, mp = 1e9;
        for (double x : sc) mc = std::min (mc, x);
        for (double x : sp) mp = std::min (mp, x);
        const double dc = db (mc / std::max (1e-14, medianOf (sc)));
        const double dp = db (mp / std::max (1e-14, medianOf (sp)));
        gate ("(self-check) the null detector FIRES on a planted 60 dB hole", dp < -30.0,
              fmt ("planted reads %.1f dB", dp));
        gate ("(self-check) ...and does NOT fire on clean program", dc > -12.0,
              fmt ("clean reads %.1f dB", dc));
    }

    std::printf ("\n  %d passed, %d FAILED\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
