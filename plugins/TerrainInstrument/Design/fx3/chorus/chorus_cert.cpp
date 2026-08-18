// ─────────────────────────────────────────────────────────────────────────────
// chorus_cert — the perceptual certification harness for the FX Chorus device (fb395).
//
//   clang++ -O2 -std=c++17 -I <repo>/plugins/TerrainInstrument/Tests/shim \
//           -I <repo>/plugins/TerrainInstrument/Source \
//           -I <this dir> chorus_cert.cpp -o /tmp/chorus_cert && /tmp/chorus_cert
//
// ⚠️ THE LAW THIS HARNESS CANNOT ENFORCE (fb373 — read it before trusting a green run):
// a green DSP harness proves the ENGINE works. It NEVER proves the plugin REACHES it.
// Selecting Cassette silently gave you Studio through four rounds of green measurement.
// The UI -> param -> DSP round trip is gated separately and headlessly.
//
// ⚠️ SAMPLE-DIFFERENCE RMS IS BANNED as a dramaticism metric (fb282/fb283): an allpass
// change measured "102 % divergence" and Max heard NOTHING; the real magnitude-spectrum
// change was 0.02 dB. Everything below is phase-INDEPENDENT: magnitude spectrum, spectral
// centroid, HF ratio, spectral flux, stereo correlation, and the modulation spectrum of
// the delay trace d(t) — the family tell that says what KIND of chorus this is.
//
// ── PROBE CRAFT, learned in this file (every one of these started as a wrong number) ──
//  * A chorus's wet IS a delayed copy of its dry, so no filter trick isolates them for the
//    Mix law. The dry path has ZERO delay and the wet has at least 1 ms, so the honest
//    probe is the PRE-WET WINDOW — and even that needs +in/-in cancellation, because the
//    engine's own BBD hiss lives in that window too and is not dry.
//  * Correlation frames must be LONGER than the LFO period. At 0.25 s frames, June's
//    within-cycle correlation swing read as 0.39 of "drift" and buried Vintage's real
//    clock-skew rotation (0.09 over a 6 s probe). 3 s frames over a 30 s probe separate them.
//  * A single 16384-sample magnitude spectrum of a MOVING comb is a snapshot at one LFO
//    phase, not a spectrum. Average |X| over a whole LFO cycle or the number is a dice roll.
//  * Peak-picking a pitch is wrong when the carrier has FM sidebands — the peak jumps to a
//    sideband and reads -25 cents on a Type that does not shift at all. The energy-weighted
//    CENTROID of the band is the estimator that survives symmetric modulation.
//  * "Comb depth" cannot be read off a 48-band log spectrum: the bands are wider than the
//    comb teeth. Sweep finely across ONE comb period and take max/min.
//  * The delay-time knob is measured by AUTOCORRELATION LAG, not by hunting for nulls.
//  * Feedback cannot be measured from a decay tail here: the loop is env-gated, so the tail
//    dies in ~150 ms at every setting BY LAW. Measure the sustained comb depth instead.
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainChorusFx.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <complex>
#include <chrono>
#include <algorithm>

namespace {

using CH = tw::TerrainChorusFx;
float FS = 48000.0f;
int gPass = 0, gFail = 0;
std::vector<std::string> gFails;

void section (const char* s) { std::printf ("\n[%s]\n", s); }
void gate (const char* what, bool ok, const std::string& detail)
{
    if (ok) { ++gPass; std::printf ("  ok    %-56s %s\n", what, detail.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-56s %s\n", what, detail.c_str());
              gFails.push_back (std::string (what) + "  [" + detail + "]"); }
}
std::string fmt (const char* f, double v) { char b[128]; std::snprintf (b, sizeof b, f, v); return b; }
std::string fmt2 (const char* f, double a, double b) { char x[192]; std::snprintf (x, sizeof x, f, a, b); return x; }

// ── probes, all normalised to the measured -26 dBFS bus (0.05 linear) ────────
std::vector<float> chord (int n, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n, 0.0f);
    const float f[4] = { 110.0f, 130.81f, 164.81f, 220.0f };
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (float fr : f) for (int h = 1; h <= 12; ++h)
            s += std::sin (6.2831853f * fr * (float) h * (float) i / FS) / (float) h;
        x[(size_t) i] = s * 0.02f;
    }
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
std::vector<float> pink (int n, float rms = 0.05f)
{
    auto w = noise (n, 1.0f, 7777u);
    std::vector<float> x ((size_t) n); float b0 = 0, b1 = 0, b2 = 0;
    for (int i = 0; i < n; ++i)
    {
        b0 = 0.99765f * b0 + w[(size_t) i] * 0.0990460f;
        b1 = 0.96300f * b1 + w[(size_t) i] * 0.2965164f;
        b2 = 0.57000f * b2 + w[(size_t) i] * 1.0526913f;
        x[(size_t) i] = b0 + b1 + b2 + w[(size_t) i] * 0.1848f;
    }
    double a = 0; for (float v : x) a += (double) v * v;
    const float g = rms / (float) std::sqrt (a / (double) n);
    for (float& v : x) v *= g;
    return x;
}

struct Run { std::vector<float> l, r; };

Run run (CH::Params p, const std::vector<float>& in, int block = 128)
{
    CH e; e.prepare ((double) FS, 512); e.setParams (p);
    Run o; o.l = in; o.r = in;
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - i);
        e.setParams (p);                                   // realistic: per block
        e.processStereo (&o.l[i], &o.r[i], n);
    }
    return o;
}

double rmsOf (const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double a = 0; size_t n = 0;
    for (size_t i = from; i < to; ++i) { a += (double) v[i] * v[i]; ++n; }
    return n ? std::sqrt (a / (double) n) : 0.0;
}
double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-13)); }

double binMag (const std::vector<float>& v, double hz, size_t from, size_t len)
{
    std::complex<double> acc (0, 0);
    for (size_t i = 0; i < len && from + i < v.size(); ++i)
    {
        const double t = 6.2831853 * hz * (double) i / (double) FS;
        acc += std::complex<double> (v[from + i] * std::cos (t), -v[from + i] * std::sin (t));
    }
    return std::abs (acc) / (double) len;
}

constexpr int NB = 48;
double bandHz (int b) { return 30.0 * std::pow (500.0, b / (double) (NB - 1)); }

std::vector<double> spectrum (const std::vector<float>& v, size_t from, size_t len = 16384)
{
    std::vector<double> s; s.reserve (NB);
    len = std::min (len, v.size() - from);
    for (int b = 0; b < NB; ++b) s.push_back (db (binMag (v, bandHz (b), from, len)));
    return s;
}
// ⚠️ the honest spectrum of a MOVING comb: average |X| over a whole modulation cycle.
std::vector<double> avgSpectrum (const std::vector<float>& v, size_t from, int win = 6, double hop = 0.35)
{
    std::vector<double> s ((size_t) NB, 0.0); int used = 0;
    for (int w = 0; w < win; ++w)
    {
        const size_t f = from + (size_t) ((double) w * hop * FS);
        if (f + 16384 >= v.size()) break;
        for (int b = 0; b < NB; ++b) s[(size_t) b] += binMag (v, bandHz (b), f, 16384);
        ++used;
    }
    if (used == 0) return spectrum (v, from);
    for (int b = 0; b < NB; ++b) s[(size_t) b] = db (s[(size_t) b] / used);
    return s;
}
double specDist (const std::vector<double>& a, const std::vector<double>& b)
{ double m = 0; for (size_t i = 0; i < a.size(); ++i) m = std::max (m, std::fabs (a[i] - b[i])); return m; }

double centroid (const std::vector<float>& v, size_t from, size_t len)
{
    len = std::min (len, v.size() - from);
    double num = 0, den = 0;
    for (int b = 0; b < 32; ++b)
    { const double f = 60.0 * std::pow (250.0, b / 31.0);
      const double m = binMag (v, f, from, len); num += f * m; den += m; }
    return den > 1e-13 ? num / den : 0.0;
}
double hfRatio (const std::vector<float>& v, size_t from, double split = 2000.0)
{
    const size_t len = std::min<size_t> (v.size() - from, 16384);
    double hi = 0, all = 0;
    for (int b = 0; b < NB; ++b)
    { const double f = bandHz (b); const double m = binMag (v, f, from, len); all += m; if (f > split) hi += m; }
    return db (hi / std::max (1e-13, all));
}
std::vector<float> mid (const Run& o)
{ std::vector<float> m (o.l.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (o.l[i] + o.r[i]); return m; }
std::vector<float> sideOf (const Run& o)
{ std::vector<float> s (o.l.size()); for (size_t i = 0; i < s.size(); ++i) s[i] = 0.5f * (o.l[i] - o.r[i]); return s; }

// ⚠️ frames must be LONGER than the LFO period or within-cycle swing masquerades as drift
std::vector<double> corrSeries (const Run& o, size_t from, double frameSec)
{
    std::vector<double> c;
    const size_t fl = (size_t) (frameSec * FS);
    for (size_t s = from; s + fl < o.l.size(); s += fl)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = s; i < s + fl; ++i)
        { xy += (double) o.l[i] * o.r[i]; xx += (double) o.l[i] * o.l[i]; yy += (double) o.r[i] * o.r[i]; }
        if (xx > 1e-16 && yy > 1e-16) c.push_back (xy / std::sqrt (xx * yy));
    }
    return c;
}
void meanStd (const std::vector<double>& v, double& m, double& s)
{
    m = 0; s = 0; if (v.empty()) return;
    for (double x : v) m += x; m /= (double) v.size();
    for (double x : v) s += (x - m) * (x - m);
    s = std::sqrt (s / (double) v.size());
}

double centroidMod (const Run& o, size_t from, int frames = 40, double hop = 0.043)
{
    double mn = 1e18, mx = 0;
    for (int f = 0; f < frames; ++f)
    {
        const size_t s = from + (size_t) ((double) f * hop * FS);
        if (s + 4096 >= o.l.size()) break;
        std::vector<float> w (o.l.begin() + (long) s, o.l.begin() + (long) s + 4096);
        const double c = centroid (w, 0, 4096);
        if (c > 1.0) { mn = std::min (mn, c); mx = std::max (mx, c); }
    }
    return (mn < 1e17 && mn > 1.0) ? (mx / mn) : 1.0;
}

// short-time HF-ratio excursion in dB. Centroid CANNOT see a lowpass whose corner sits
// above the program (flt_cert's own lesson); HF ratio can, and on noise it has headroom.
// ⚠️ REFERENCED TO THE INPUT FRAME BY FRAME. A noise probe's own HF ratio wanders +-4 dB
// between 8192-sample frames purely from its statistics, which put a 4.4 dB floor under
// this metric and hid everything smaller. Subtracting the SAME frame of the dry input
// cancels the source and leaves only what the engine did.
double hfRatioMod (const Run& o, const std::vector<float>& in, size_t from,
                   int frames = 46, double hop = 0.150)
{
    double mn = 1e18, mx = -1e18;
    for (int f = 0; f < frames; ++f)
    {
        const size_t s = from + (size_t) ((double) f * hop * FS);
        if (s + 8192 >= o.l.size()) break;
        std::vector<float> w (o.l.begin() + (long) s, o.l.begin() + (long) s + 8192);
        std::vector<float> d (in.begin() + (long) s, in.begin() + (long) s + 8192);
        const double h = hfRatio (w, 0, 2000.0) - hfRatio (d, 0, 2000.0);
        mn = std::min (mn, h); mx = std::max (mx, h);
    }
    return (mx > -1e17) ? (mx - mn) : 0.0;
}

// spectral flux — bands above 150 Hz only (a 4096 window cannot resolve 30 Hz, and the
// resulting bin noise dominated the metric on a perfectly static signal)
double specFlux (const Run& o, size_t from, int frames = 24, double hop = 0.043)
{
    std::vector<std::vector<double>> S;
    int b0 = 0; while (b0 < NB && bandHz (b0) < 150.0) ++b0;
    for (int f = 0; f < frames; ++f)
    {
        const size_t s = from + (size_t) ((double) f * hop * FS);
        if (s + 4096 >= o.l.size()) break;
        std::vector<float> w (o.l.begin() + (long) s, o.l.begin() + (long) s + 4096);
        S.push_back (spectrum (w, 0, 4096));
    }
    if (S.size() < 2) return 0.0;
    // ⚠️ only bands that CARRY ENERGY. A 48-band log spectrum of a harmonic chord has bands
    // sitting between partials whose dB value is pure DFT noise; counting them measured
    // 4.45 dB/frame of "movement" on a completely static signal and buried the real thing.
    std::vector<double> mean ((size_t) NB, 0.0);
    for (const auto& f : S) for (int b = 0; b < NB; ++b) mean[(size_t) b] += f[(size_t) b];
    double top = -1e18;
    for (int b = b0; b < NB; ++b) { mean[(size_t) b] /= (double) S.size(); top = std::max (top, mean[(size_t) b]); }
    double acc = 0; int n = 0;
    for (size_t i = 1; i < S.size(); ++i)
        for (int b = b0; b < NB; ++b)
        { if (mean[(size_t) b] < top - 35.0) continue;
          acc += std::fabs (S[i][(size_t) b] - S[i - 1][(size_t) b]); ++n; }
    return n ? acc / n : 0.0;
}

// ── d(t), sampled FROM THE ENGINE ───────────────────────────────────────────
struct DTrace { std::vector<double> l, r; double hz; };
DTrace dTrace (CH::Params p, double sec = 12.0, int dec = 32)
{
    CH e; e.prepare ((double) FS, 512); e.setParams (p);
    auto in = chord ((int) (sec * FS));
    DTrace t; t.hz = (double) FS / dec;
    t.l.reserve (in.size() / (size_t) dec);
    for (size_t i = 0; i + (size_t) dec <= in.size(); i += (size_t) dec)
    {
        float L[64], R[64];
        for (int k = 0; k < dec; ++k) { L[k] = in[i + (size_t) k]; R[k] = in[i + (size_t) k]; }
        e.processStereo (L, R, dec);
        t.l.push_back (e.liveDelayMs (0));
        t.r.push_back (e.liveDelayMs (1));
    }
    return t;
}
// properly normalised autocorrelation peak of d(t): 1.0 = perfectly periodic, 0 = noise
double dPeriodicity (const DTrace& t)
{
    const size_t n = t.l.size(); if (n < 400) return 0.0;
    double m = 0; for (double v : t.l) m += v; m /= (double) n;
    std::vector<double> x (n); for (size_t i = 0; i < n; ++i) x[i] = t.l[i] - m;
    double best = 0;
    const size_t loLag = (size_t) (0.10 * t.hz), hiLag = std::min (n / 2, (size_t) (8.0 * t.hz));
    for (size_t lag = loLag; lag < hiLag; ++lag)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = 0; i + lag < n; ++i) { xy += x[i] * x[i + lag]; xx += x[i] * x[i]; yy += x[i + lag] * x[i + lag]; }
        if (xx > 1e-14 && yy > 1e-14) best = std::max (best, xy / std::sqrt (xx * yy));
    }
    return std::max (0.0, std::min (1.0, best));
}
// ⚠️ THE TOPOLOGY TELL: the correlation between the LEFT and RIGHT delay traces. +1 =
// the two reads move TOGETHER (Pedal: one line read twice, co-phase). -1 = they move in
// OPPOSITION (June: the Juno antiphase). Around 0 = decorrelated (random walks). This is
// the discriminator broadband L/R audio correlation cannot give you, because at a 1.2 ms
// read offset two co-phase channels are already audio-decorrelated.
double dCorrLR (const DTrace& t)
{
    const size_t n = std::min (t.l.size(), t.r.size()); if (n < 100) return 0.0;
    double ml = 0, mr = 0; for (size_t i = 0; i < n; ++i) { ml += t.l[i]; mr += t.r[i]; }
    ml /= (double) n; mr /= (double) n;
    double xy = 0, xx = 0, yy = 0;
    for (size_t i = 0; i < n; ++i)
    { const double a = t.l[i] - ml, b = t.r[i] - mr; xy += a * b; xx += a * a; yy += b * b; }
    return (xx > 1e-14 && yy > 1e-14) ? xy / std::sqrt (xx * yy) : 0.0;
}

// skewness of the delay distribution — the asymmetric-glide tell (sag fast, recover slow)
double dSkew (const DTrace& t)
{
    const size_t n = t.l.size(); if (n < 100) return 0.0;
    double m = 0; for (double v : t.l) m += v; m /= (double) n;
    double s2 = 0, s3 = 0;
    for (double v : t.l) { const double d = v - m; s2 += d * d; s3 += d * d * d; }
    s2 /= (double) n; s3 /= (double) n;
    return (s2 > 1e-12) ? s3 / std::pow (s2, 1.5) : 0.0;
}
// modulation spectrum of d(t): the strongest line, and the strongest INDEPENDENT second line
double dLines (const DTrace& t, double& f1, double& f2, double loHz = 0.15, double hiHz = 30.0)
{
    const size_t n = t.l.size(); if (n < 256) { f1 = f2 = 0; return 0.0; }
    double m = 0; for (double v : t.l) m += v; m /= (double) n;
    std::vector<double> x (n); for (size_t i = 0; i < n; ++i) x[i] = t.l[i] - m;
    const int K = 320; std::vector<double> mag ((size_t) K), fr ((size_t) K);
    for (int k = 0; k < K; ++k)
    {
        const double f = loHz * std::pow (hiHz / loHz, k / (double) (K - 1));
        fr[(size_t) k] = f;
        std::complex<double> acc (0, 0);
        for (size_t i = 0; i < n; ++i)
        { const double a = 6.2831853 * f * (double) i / t.hz;
          acc += std::complex<double> (x[i] * std::cos (a), -x[i] * std::sin (a)); }
        mag[(size_t) k] = std::abs (acc) / (double) n;
    }
    int p1 = 0; for (int k = 0; k < K; ++k) if (mag[(size_t) k] > mag[(size_t) p1]) p1 = k;
    f1 = fr[(size_t) p1];
    int p2 = -1;
    for (int k = 0; k < K; ++k)
    {
        const double ratio = fr[(size_t) k] / f1;
        if (ratio < 2.2 && ratio > 1.0 / 2.2) continue;      // too close to f1 to be independent
        // ⚠️ and NOT a harmonic of f1: a triangle LFO puts 1/9 of its energy at 3*f, which
        // read as a "second rate" of 0.11-0.15 on every triangle Type and nearly buried
        // Ensemble's real 6.25 Hz bank.
        bool harmonic = false;
        for (int nh = 2; nh <= 24; ++nh)
        { if (std::fabs (ratio - nh) < 0.12 || std::fabs (1.0 / ratio - nh) < 0.12) { harmonic = true; break; } }
        if (harmonic) continue;
        if (p2 < 0 || mag[(size_t) k] > mag[(size_t) p2]) p2 = k;
    }
    if (p2 < 0) { f2 = 0; return 0.0; }
    f2 = fr[(size_t) p2];
    return mag[(size_t) p2] / std::max (1e-12, mag[(size_t) p1]);
}
// fraction of d(t) modulation energy above a split frequency (the Flutter probe)
double dHiFraction (const DTrace& t, double split = 3.0)
{
    const size_t n = t.l.size(); if (n < 256) return 0.0;
    double m = 0; for (double v : t.l) m += v; m /= (double) n;
    double hi = 0, all = 0;
    for (int k = 0; k < 120; ++k)
    {
        const double f = 0.2 * std::pow (150.0, k / 119.0);
        std::complex<double> acc (0, 0);
        for (size_t i = 0; i < n; ++i)
        { const double a = 6.2831853 * f * (double) i / t.hz;
          acc += std::complex<double> ((t.l[i] - m) * std::cos (a), -(t.l[i] - m) * std::sin (a)); }
        const double mg = std::abs (acc) / (double) n;
        all += mg; if (f > split) hi += mg;
    }
    return hi / std::max (1e-12, all);
}

// ⚠️ pitch by ENERGY-WEIGHTED CENTROID of the 425-455 Hz band, not by peak-picking. A
// carrier with symmetric FM sidebands has its PEAK jump to a sideband (Vintage read -25
// cents when it does not shift at all); the centroid of a symmetric pair stays on the
// carrier. Signature is (params, channel) -> cents.
double centsOffset (CH::Params p, int ch = 0)
{
    auto in = tone ((int) (5.0f * FS), 440.0f);
    auto o = run (p, in);
    const auto& v = (ch == 0) ? o.l : o.r;
    const size_t from = (size_t) (1.5f * FS), len = (size_t) (3.0f * FS);
    double num = 0, den = 0;
    for (double f = 424.0; f <= 456.0; f += 0.10)
    { const double m = binMag (v, f, from, len); const double w = m * m; num += f * w; den += w; }
    return den > 1e-20 ? 1200.0 * std::log2 ((num / den) / 440.0) : 0.0;
}

// base delay by AUTOCORRELATION LAG of the output (dry + delayed wet), in ms
double delayLagMs (CH::Params p)
{
    auto in = noise ((int) (3.0f * FS));
    auto o = run (p, in);
    const size_t from = (size_t) (0.6f * FS), n = (size_t) (1.6f * FS);
    double m = 0; for (size_t i = from; i < from + n; ++i) m += o.l[i]; m /= (double) n;
    double best = -1e18; size_t bl = 0;
    for (size_t lag = (size_t) (0.0004f * FS); lag < (size_t) (0.055f * FS); ++lag)
    {
        double a = 0;
        for (size_t i = from; i < from + n; ++i) a += (o.l[i] - m) * (o.l[i + lag] - m);
        if (a > best) { best = a; bl = lag; }
    }
    return (double) bl * 1000.0 / (double) FS;
}

CH::Params base (int type = 0, int chr = 0)
{
    CH::Params p;
    p.type = type; p.character = chr;
    p.rate = 0.35f; p.depth = 0.60f; p.feedback = 0.0f; p.mix = 1.0f;
    p.b1 = 0.5f; p.b2 = 0.0f; p.b3 = 0.7f; p.b4 = 0.25f;
    p.b5 = 0.0f; p.b6 = 0.5f; p.b7 = 0.0f; p.b8 = 1.0f;
    p.tempoSync = false; p.bpm = 120.0;
    return p;
}

// ═══ the cross-type descriptor ═══════════════════════════════════════════════
struct Feat
{
    std::vector<double> mono, sideS;
    double corrMean = 0, corrStd = 0;
    double centMod = 1, flux = 0;
    double dAuto = 0, dDual = 0, dSk = 0, f1 = 0, f2 = 0, dLR = 0;
    double cents = 0;
    double monoSurvive = 0;
};

Feat measure (CH::Params p, const std::vector<float>& probe, const std::vector<double>& dryMonoSpec)
{
    Feat f;
    auto o = run (p, probe);
    const size_t S = (size_t) (0.5f * FS);
    auto M = mid (o), Sd = sideOf (o);
    f.mono  = avgSpectrum (M, S);
    f.sideS = avgSpectrum (Sd, S);
    double m, s; meanStd (corrSeries (o, S, 3.0), m, s);
    f.corrMean = m; f.corrStd = s;
    f.centMod = centroidMod (o, S);
    f.flux    = specFlux (o, S);
    f.monoSurvive = db (rmsOf (M, S) / std::max (1e-12, rmsOf (o.l, S)));
    (void) dryMonoSpec;
    auto t = dTrace (p, 16.0);
    f.dAuto = dPeriodicity (t); f.dDual = dLines (t, f.f1, f.f2); f.dSk = dSkew (t);
    f.dLR = dCorrLR (t);
    f.cents = centsOffset (p);
    return f;
}

struct AxisRes { double val; const char* name; };
AxisRes distance (const Feat& a, const Feat& b)
{
    struct A { double d; const char* n; };
    const A ax[] = {
        { specDist (a.mono, b.mono)              / 3.00, "mono spec"    },
        { specDist (a.sideS, b.sideS)            / 4.00, "side spec"    },
        { std::fabs (a.corrMean - b.corrMean)    / 0.15, "L/R corr"     },
        { std::fabs (a.corrStd  - b.corrStd)     / 0.07, "corr drift"   },
        { std::fabs (a.centMod  - b.centMod)     / 0.10, "centroid mod" },
        { std::fabs (a.flux     - b.flux)        / 0.60, "spec flux"    },
        { std::fabs (a.dAuto    - b.dAuto)       / 0.15, "d(t) period"  },
        { std::fabs (a.dDual    - b.dDual)       / 0.15, "d(t) 2-rate"  },
        { std::fabs (a.cents    - b.cents)       / 2.00, "pitch offset" },
        { std::fabs (a.dLR      - b.dLR)         / 0.20, "d(t) L/R topology" }
    };
    AxisRes best { 0.0, "-" };
    for (const A& x : ax) if (x.d > best.val) { best.val = x.d; best.name = x.n; }
    return best;
}

double worstStep (const std::vector<float>& v, size_t from)
{
    double w = 0;
    for (size_t i = from + 1; i < v.size(); ++i) w = std::max (w, (double) std::fabs (v[i] - v[i - 1]));
    return w;
}

// max/min of |X(f)| swept finely across ONE comb period — the honest comb-depth probe
double combDepthDb (const Run& o, size_t from, double dMs, int k = 4)
{
    const double f0 = (double) k / (dMs * 0.001);
    const double f1 = (double) (k + 1) / (dMs * 0.001);
    double mx = 0, mn = 1e18;
    for (int i = 0; i <= 60; ++i)
    {
        const double f = f0 + (f1 - f0) * i / 60.0;
        const double m = binMag (o.l, f, from, 65536);
        mx = std::max (mx, m); mn = std::min (mn, m);
    }
    return db (mx) - db (std::max (mn, 1e-13));
}

} // namespace

int main()
{
    const int N2 = (int) (FS * 2.0f);
    const size_t S = (size_t) (0.5f * FS);

    std::printf ("\n══ TERRAIN CHORUS FX — certification ══   bus program -26 dBFS, fs %.0f Hz\n", (double) FS);
    std::printf ("   %d Types x %d Characters, 12 params (Rate Depth Feedback Mix + 8 back)\n",
                 CH::kNumTypes, CH::kNumChars);

    // ═════════════════════════════════════════════════════════════════════════
    section ("A. Unity, the Mix law, and zero latency");
    {
        auto in = chord (N2);
        auto p = base (1); p.mix = 0.0f;
        auto o = run (p, in);
        double worst = 0; for (size_t i = 0; i < in.size(); ++i) worst = std::max (worst, (double) std::fabs (o.l[i] - in[i]));
        gate ("Mix 0 is bit-transparent", worst < 1e-9, fmt ("worst sample delta %.3e", worst));
    }
    {
        auto in = chord (N2);
        auto p = base (1); p.mix = 0.0f; p.b7 = 0.8f;         // Low Keep engaged
        auto o = run (p, in);
        double worst = 0; for (size_t i = 0; i < in.size(); ++i) worst = std::max (worst, (double) std::fabs (o.l[i] - in[i]));
        gate ("Mix 0 stays transparent with Low Keep engaged", worst < 1e-7, fmt ("worst sample delta %.3e", worst));
    }
    {
        // ⚠️ THE DRY-LEAK PROBE. The dry path has ZERO delay by construction; the wet cannot
        // arrive before the minimum base delay. Measure the output BEFORE the first wet
        // sample can exist — and isolate the LINEAR (dry) part by running +in and -in
        // through fresh engines and halving the difference, because the engine's own BBD
        // hiss lives in that window too and is not dry.
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            auto p = base (t); p.mix = 1.0f; p.b1 = 0.5f;
            auto in = noise ((int) (0.02f * FS));
            std::vector<float> Lp (in), Rp (in), Lm (in.size()), Rm (in.size());
            for (size_t i = 0; i < in.size(); ++i) { Lm[i] = -in[i]; Rm[i] = -in[i]; }
            { CH e; e.prepare ((double) FS, 512); e.setParams (p); e.processStereo (Lp.data(), Rp.data(), (int) Lp.size()); }
            { CH e; e.prepare ((double) FS, 512); e.setParams (p); e.processStereo (Lm.data(), Rm.data(), (int) Lm.size()); }
            std::vector<float> lin (in.size());
            for (size_t i = 0; i < in.size(); ++i) lin[i] = 0.5f * (Lp[i] - Lm[i]);
            const size_t win = (size_t) (0.0008f * FS);       // 0.8 ms: below every base delay
            const double resid = db (rmsOf (lin, 0, win) / std::max (1e-12, rmsOf (in, 0, win)));
            gate ((std::string ("Mix 100% leaks no dry: ") + CH::typeNames()[t]).c_str(),
                  resid < -60.0, fmt ("%.1f dB in the pre-wet window", resid));
        }
    }
    {
        auto in = chord ((int) (FS * 4.0f));
        double worstDb = 0; std::string who;
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            auto p = base (t); p.mix = 1.0f;
            auto o = run (p, in);
            const double d = db (std::sqrt (0.5 * (rmsOf (o.l, S) * rmsOf (o.l, S) + rmsOf (o.r, S) * rmsOf (o.r, S)))
                                 / std::max (1e-12, rmsOf (in, S)));
            if (std::fabs (d) > std::fabs (worstDb)) { worstDb = d; who = CH::typeNames()[t]; }
        }
        gate ("unity-through at Mix 100% on every Type (+-1.0 dB)", std::fabs (worstDb) < 1.0,
              fmt ("worst %+.2f dB", worstDb) + " (" + who + ")");
    }
    {
        auto in = chord ((int) (FS * 4.0f));
        auto p = base (1); p.mix = 0.35f;
        auto o = run (p, in);
        const double d = db (rmsOf (o.l, S) / std::max (1e-12, rmsOf (in, S)));
        gate ("default Mix 35% sits within +-1.0 dB of bypass", std::fabs (d) < 1.0, fmt ("%+.2f dB", d));
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("B. R7 — the legacy antiphase claim, verified rather than quoted");
    {
        // Source/TerrainChorus.h:18/19/42/56 — RIGHT_PHASE_OFFSET = pi AND
        // RIGHT_RATE_RATIO = 1.07. Reproduce the legacy topology EXACTLY and ask when the
        // channels are first IN PHASE. Analytic: dphi = pi + 2pi(0.07 f)t, in phase when
        // 0.07 f t = 0.5  =>  t = 1/(0.14 f).
        const float pi = 3.14159265f;
        for (float fL : { 0.4f, 1.13f, 1.5f })
        {
            float pL = 0.0f, pR = pi; double tIn = -1;
            for (int i = 0; i < (int) (60.0f * FS); ++i)
            {
                pL += 6.2831853f * fL / FS;         if (pL >= 6.2831853f) pL -= 6.2831853f;
                pR += 6.2831853f * fL * 1.07f / FS; if (pR >= 6.2831853f) pR -= 6.2831853f;
                float d = std::fabs (pR - pL); if (d > pi) d = 6.2831853f - d;
                if (tIn < 0 && d < 0.05f) tIn = (double) i / FS;
            }
            const double predicted = 1.0 / (0.14 * fL);
            gate ((std::string ("legacy pi offset washes out at ") + fmt ("%.2f Hz", fL)).c_str(),
                  tIn > 0 && std::fabs (tIn - predicted) < 0.5,
                  fmt2 ("in phase after %.2f s (predicted %.2f s)", tIn, predicted));
        }
    }
    {
        auto in = chord ((int) (FS * 40.0f));
        auto pv = base (0, 0);  auto ov = run (pv, in);
        auto pk = base (0, 5);  auto ok = run (pk, in);       // Locked
        double m1, s1, m2, s2;
        meanStd (corrSeries (ov, S, 3.0), m1, s1);
        meanStd (corrSeries (ok, S, 3.0), m2, s2);
        gate ("Vintage's stereo image ROTATES (the legacy 1.07 skew)", s1 > 0.15,
              fmt2 ("corr mean %+.2f, drift sigma %.3f", m1, s1));
        gate ("Vintage/Locked holds still (the honest antiphase)", s2 < s1 * 0.55,
              fmt2 ("corr mean %+.2f, drift sigma %.3f", m2, s2));
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("C. Every Type's own discriminator (law 2)");

    auto probe = chord ((int) (FS * 30.0f));
    std::vector<double> dryMono;
    { Run d; d.l = probe; d.r = probe; dryMono = avgSpectrum (mid (d), S); }
    std::vector<Feat> F ((size_t) CH::kNumTypes);
    for (int t = 0; t < CH::kNumTypes; ++t) F[(size_t) t] = measure (base (t), probe, dryMono);

    {
        const Feat& f = F[0];
        double worstOther = 0; for (int t = 1; t < CH::kNumTypes; ++t) worstOther = std::max (worstOther, F[(size_t) t].corrStd);
        gate ("Vintage: the stereo image ROTATES (no other Type's does)",
              f.corrStd > 0.12 && f.corrStd > worstOther * 1.3,
              fmt2 ("corr drift sigma %.3f vs next best %.3f", f.corrStd, worstOther));
    }
    {
        const Feat& f = F[1];
        gate ("June: antiphase pair (audio L/R correlation collapses)", f.corrMean < 0.35,
              fmt2 ("audio corr %+.3f, d(t) topology %+.3f", f.corrMean, f.dLR));
    }
    {
        const Feat& f = F[2];
        // ⚠️ NOT broadband L/R audio correlation: Pedal's two reads sit 1.2 ms apart at
        // Phase 180, which already decorrelates the AUDIO (measured -0.06) even though the
        // two reads move perfectly together. The topology tell is the correlation of the
        // two DELAY TRACES, which is exactly what "co-phase" means.
        gate ("Pedal: CO-PHASE — the two reads move TOGETHER", f.dLR > 0.90,
              fmt ("d(t) L/R correlation %+.3f (gate > +0.90)", f.dLR));
        gate ("   ... where June's move in OPPOSITION", F[1].dLR < -0.90,
              fmt ("June d(t) L/R correlation %+.3f (gate < -0.90)", F[1].dLR));
    }
    {
        const Feat& f = F[3];
        gate ("Trio: centre-anchored — best mono survival of the LFO Types",
              f.monoSurvive > F[1].monoSurvive && f.monoSurvive > F[0].monoSurvive,
              fmt2 ("mono sum %.2f dB vs June %.2f dB", f.monoSurvive, F[1].monoSurvive));
    }
    {
        const Feat& f = F[4];
        // compared against the other PERIODIC Types only: Micro and Wow are random walks,
        // whose broadband d(t) trivially has "energy elsewhere" and is not a second RATE.
        double worstOther = 0;
        for (int t : { 0, 1, 2, 3, 7 }) worstOther = std::max (worstOther, F[(size_t) t].dDual);
        gate ("Ensemble: TWO simultaneous modulation rates in d(t)", f.dDual > 0.10,
              fmt2 ("lines at %.2f Hz and %.2f Hz, ", f.f1, f.f2) + fmt ("2nd/1st = %.3f", f.dDual));
        gate ("   ... strongest second rate of every periodic Type", f.dDual > worstOther * 1.4,
              fmt ("next best periodic Type %.3f", worstOther));
    }
    {
        const Feat& f = F[5];
        gate ("Micro: a CONSTANT steady-state pitch offset", std::fabs (f.cents) > 3.0,
              fmt ("L shifted %+.2f cents", f.cents));
        double worstOther = 0;
        for (int t = 0; t < CH::kNumTypes; ++t) if (t != 5) worstOther = std::max (worstOther, std::fabs (F[(size_t) t].cents));
        gate ("   ... and every other Type stays on pitch (< 3 c)", worstOther < 3.0,
              fmt ("worst other %.2f cents", worstOther) + " — Dark's tracked LP couples brightness to the sweep");
    }
    {
        const Feat& f = F[6];
        double bestPeriodic = 0; for (int t : { 0, 1, 2, 3, 4, 7 }) bestPeriodic = std::max (bestPeriodic, F[(size_t) t].dAuto);
        gate ("Wow: d(t) is the LEAST periodic of the roster", f.dAuto < bestPeriodic * 0.85,
              fmt2 ("d(t) periodicity %.3f vs best LFO Type %.3f", f.dAuto, bestPeriodic));
        double worstSkew = 0; for (int t : { 0, 1, 2, 3, 4, 7 }) worstSkew = std::max (worstSkew, std::fabs (F[(size_t) t].dSk));
        gate ("   ... and its glide is ASYMMETRIC (sag fast, recover slow)",
              std::fabs (f.dSk) > std::max (0.10, worstSkew * 1.3),
              fmt2 ("d(t) skew %+.3f vs worst symmetric Type %.3f", f.dSk, worstSkew));
    }
    {
        // the ONE thing about Dark no knob on June can imitate: the recon cutoff tracks the
        // INSTANTANEOUS delay, so the brightness breathes at the LFO rate.
        // 🛑 THE BIBLE'S DARK DISCRIMINATOR DOES NOT SURVIVE MEASUREMENT — both the failed
        //    claim and its replacement are recorded here rather than quietly swapped.
        //
        //    §2.7 says Dark's tell is that its recon cutoff tracks d(t) per sample, so its
        //    brightness "breathes" where June's does not, and gates it at >= 6 semitones of
        //    centroid swing against June's < 0.5. MEASURED at MATCHED geometry (both 12 ms
        //    base, 1.5 ms excursion, Colour 15, Width 0, Mix 100, deterministic harmonic
        //    probe so the source contributes nothing): June 7.48 dB of brightness excursion,
        //    Dark 7.33 dB. Dark is a hair LOWER. EVERY Type's brightness already breathes
        //    under a sweep — the fractional-delay interpolator's response depends on the frac
        //    part, which cycles at a rate set by d'(t) — and that shared effect is larger
        //    than a pole moving 0.85 of an octave. Raising the tracking exponent to 2.2 and
        //    the filter to three poles did not change the verdict. THE CLAIM IS RED. The
        //    tracking stays in the engine (the physics is right, and it is audible on its
        //    own) but it is not what separates Dark from June.
        //
        //    What IS unique to Dark, and what no knob on any Type can reach: it runs the full
        //    BBD RECONSTRUCTION CHAIN — three poles, not one. Colour moves the corner on
        //    every Type; nothing moves the SLOPE.
        //    ⚠️ probe craft: a noise probe's per-bin variance is +-5 dB and reported June at
        //    16.8 dB/oct, which is impossible from a one-pole. A deterministic 140-harmonic
        //    source with band-summed energy is the honest slope probe.
        auto slope = [&] (int t) {
            auto p = base (t); p.mix = 1.0f; p.depth = 0.0f; p.b4 = 0.0f; p.b3 = 0.0f; p.b6 = 0.15f;
            std::vector<float> in ((size_t) (FS * 3.0f), 0.0f);
            for (size_t i = 0; i < in.size(); ++i)
            { float v = 0; for (int h = 1; h <= 140; ++h)
                v += std::sin (6.2831853f * 100.0f * (float) h * (float) i / FS) / std::sqrt ((float) h);
              in[i] = v; }
            double a = 0; for (float v : in) a += (double) v * v;
            const float g = 0.05f / (float) std::sqrt (a / (double) in.size());
            for (float& v : in) v *= g;
            auto o = run (p, in);
            double lo = 0, hi = 0;
            for (int h = 1; h <= 140; ++h)
            { const double f = 100.0 * h; const double m = binMag (o.l, f, S, 32768);
              if (f > 2400 && f < 3600) lo += m * m; if (f > 9600 && f < 14400) hi += m * m; }
            return (10.0 * std::log10 (std::max (lo, 1e-26)) - 10.0 * std::log10 (std::max (hi, 1e-26))) / 2.0; };
        const double sd = slope (7);
        double steepOther = 0; std::string wn;
        for (int t = 0; t < CH::kNumTypes; ++t) if (t != 7)
        { const double v = slope (t); if (v > steepOther) { steepOther = v; wn = CH::typeNames()[t]; } }
        gate ("Dark: the STEEPEST reconstruction chain in the roster (3-pole BBD)",
              sd > 10.0 && sd > steepOther * 1.2,
              fmt2 ("%.1f dB/oct vs next steepest %.1f", sd, steepOther) + " (" + wn + ")");
        std::printf ("        note: the bible's Dark discriminator (cutoff tracks d(t) so brightness breathes)\n");
        std::printf ("              measured RED — June 7.48 dB vs Dark 7.33 dB at matched geometry. See FINDINGS.\n");
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("D. CROSS-TYPE DISTINCTNESS MATRIX  (1.00 = one JND unit; gate 1.00)");
    {
        std::printf ("        ");
        for (int b = 0; b < CH::kNumTypes; ++b) std::printf ("%8.8s", CH::typeNames()[b]);
        std::printf ("\n");
        double worst = 1e9; int wa = 0, wb = 0; const char* wax = "-";
        for (int a = 0; a < CH::kNumTypes; ++a)
        {
            std::printf ("  %-6.6s", CH::typeNames()[a]);
            for (int b = 0; b < CH::kNumTypes; ++b)
            {
                if (a == b) { std::printf ("       ."); continue; }
                const AxisRes d = distance (F[(size_t) a], F[(size_t) b]);
                std::printf ("%8.2f", d.val);
                if (b > a && d.val < worst) { worst = d.val; wa = a; wb = b; wax = d.name; }
            }
            std::printf ("\n");
        }
        gate ("every Type pair is distinguishable (worst pair >= 1.00)", worst >= 1.0,
              fmt ("closest pair %.2f", worst) + " = " + CH::typeNames()[wa] + "/" + CH::typeNames()[wb]
              + " (separated by " + wax + ")");
        std::printf ("        axes: mono spec /3 dB · side spec /4 dB · L/R corr /0.15 · corr drift /0.07\n");
        std::printf ("              centroid mod /0.10 · spec flux /0.60 dB · d(t) period /0.15 · d(t) 2-rate /0.15\n");
        std::printf ("              pitch offset /2 cents · d(t) L/R topology /0.20\n\n");
        std::printf ("        Type       corr   drift  centMod  flux  dPeriod  d2rate  dSkew  d(t)L/R   cents  mono/L\n");
        for (int t = 0; t < CH::kNumTypes; ++t)
            std::printf ("        %-9s %+6.2f  %.3f   x%.2f  %.2f    %.3f   %.3f  %+.2f   %+6.3f  %+6.2f  %+6.2f dB\n",
                         CH::typeNames()[t], F[(size_t) t].corrMean, F[(size_t) t].corrStd, F[(size_t) t].centMod,
                         F[(size_t) t].flux, F[(size_t) t].dAuto, F[(size_t) t].dDual, F[(size_t) t].dSk,
                         F[(size_t) t].dLR, F[(size_t) t].cents, F[(size_t) t].monoSurvive);
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("E. Every param evolves 0 -> 100, monotonic and dramatic (law 1)");
    {   // RATE — the d(t) modulation line must track the knob. A 60 s trace and a scan
        //        starting at 0.03 Hz, because the bottom of the taper is 0.04 Hz and a 10 s
        //        window cannot see half a cycle of it.
        double prev = -1; bool mono = true; std::string tr;
        for (float r : { 0.10f, 0.30f, 0.50f, 0.70f, 0.90f })
        {
            auto p = base (1, 3); p.rate = r;                 // June / Manual = free rate
            double f1 = 0, f2 = 0; auto t = dTrace (p, 60.0, 64);
            dLines (t, f1, f2, 0.03, 40.0);
            tr += fmt ("%.3f ", f1);
            if (f1 <= prev * 1.15) mono = false;
            prev = f1;
        }
        gate ("Rate 0->100 sweeps the modulation rate, monotonic", mono && prev > 8.0,
              "d(t) line Hz: " + tr);
    }
    {   // DEPTH — excursion in ms (Flutter off, so only Depth moves d(t)), plus flux
        bool mono = true; double prev = -1, p0 = 0, p100 = 0, fl0 = 0, fl100 = 0;
        std::string tr;
        auto sh = noise ((int) (FS * 10.0f));
        for (float d : { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f })
        {
            auto p = base (1, 0); p.depth = d; p.b4 = 0.0f;
            auto t = dTrace (p, 8.0);
            double mn = 1e9, mx = -1e9; for (double v : t.l) { mn = std::min (mn, v); mx = std::max (mx, v); }
            const double pp = mx - mn;
            tr += fmt ("%.2f ", pp);
            if (pp < prev - 1e-3) mono = false; prev = pp;
            if (d == 0.0f) { p0 = pp; fl0 = hfRatioMod (run (p, sh), sh, S); }
            if (d == 1.0f) { p100 = pp; fl100 = hfRatioMod (run (p, sh), sh, S); }
        }
        gate ("Depth 0->100 opens the excursion, monotonic", mono && p100 > 3.0 && p0 < 0.05,
              "d(t) peak-to-peak ms: " + tr);
        gate ("   ... and the comb MOVES with it (HF-ratio excursion)", fl100 > fl0 * 2.5,
              fmt2 ("HF-ratio excursion %.2f -> %.2f dB", fl0, fl100));
    }
    {   // FEEDBACK — sustained comb depth. NOT a decay tail: the loop is env-gated by law,
        //            so every setting's tail dies in ~150 ms and the tail says nothing.
        double prev = -1; bool mono = true; std::string tr;
        auto in = pink ((int) (FS * 3.0f));
        const double dms = 1.0 * std::pow (12.0, 0.5);        // June, Time knob 0.5 = 3.464 ms
        for (float f : { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f })
        {
            auto p = base (1, 0); p.feedback = f; p.depth = 0.0f; p.b4 = 0.0f;
            const double ptv = combDepthDb (run (p, in), S, dms, 4);
            tr += fmt ("%.1f ", ptv);
            if (ptv < prev - 0.8) mono = false; prev = ptv;
        }
        gate ("Feedback 0->100 deepens the comb, monotonic", mono && prev > 12.0,
              "wet comb peak-to-valley dB: " + tr);
    }
    {   // MIX
        std::string tr; bool mono = true; double prev = -1e9;
        auto in = chord ((int) (FS * 3.0f));
        for (float m : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto p = base (1); p.mix = m; p.depth = 0.8f;
            auto o = run (p, in);
            const double s = db (rmsOf (sideOf (o), S) / std::max (1e-12, rmsOf (o.l, S)));
            tr += fmt ("%.1f ", s);
            if (s < prev - 0.5) mono = false; prev = s;
        }
        gate ("Mix 0->100 brings the wet in, monotonic", mono, "side/L dB: " + tr);
    }
    {   // TIME — the base delay, by AUTOCORRELATION LAG of the output
        std::string tr; bool mono = true; double prev = -1;
        for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto p = base (1); p.b1 = t; p.depth = 0.0f; p.b4 = 0.0f; p.mix = 0.5f;
            const double ms = delayLagMs (p);
            tr += fmt ("%.2f ", ms);
            if (ms < prev - 0.05) mono = false; prev = ms;
        }
        gate ("Time 0->100 lengthens the delay, monotonic (1 -> 12 ms on June)",
              mono && prev > 10.0, "measured delay ms: " + tr);
    }
    {   // DETUNE
        std::string tr; bool mono = true; double prev = -1;
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto p = base (1); p.b2 = d; p.depth = 0.0f; p.b4 = 0.0f;
            const double c = centsOffset (p, 0);
            tr += fmt ("%.1f ", c);
            if (c < prev - 0.6) mono = false; prev = c;
        }
        gate ("Detune 0->100 splits the pitch, monotonic to ~50 cents", mono && prev > 35.0,
              "L offset cents: " + tr);
        auto p = base (1); p.b2 = 0.5f; p.depth = 0.0f; p.b4 = 0.0f;
        const double cl = centsOffset (p, 0), cr = centsOffset (p, 1);
        gate ("   ... and the split is symmetric (L up, R down)", cl > 5.0 && cr < -5.0,
              fmt2 ("L %+.2f c, R %+.2f c", cl, cr));
    }
    {   // WIDTH
        std::string tr; bool mono = true; double prev = -1e9;
        auto sh = chord ((int) (FS * 6.0f));
        for (float w : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto p = base (1); p.b3 = w;
            auto o = run (p, sh);
            const double s = db (rmsOf (sideOf (o), S) / std::max (1e-12, rmsOf (mid (o), S)));
            tr += fmt ("%.1f ", s);
            if (s < prev - 0.4) mono = false; prev = s;
        }
        gate ("Width 0->100 opens the stereo field, monotonic", mono && prev > -12.0,
              "side/mid dB: " + tr);
        auto p = base (1); p.b3 = 0.0f;
        auto o = run (p, sh);
        gate ("   ... and Width 0 is MONO, never SILENT (the pure-side trap)",
              rmsOf (mid (o), S) > 0.2 * rmsOf (sh, S),
              fmt2 ("mid RMS %.4f vs dry %.4f", rmsOf (mid (o), S), rmsOf (sh, S)));
    }
    {   // fb412 — THE WIDE PILL. Two claims, both measured: it goes PAST the Width knob's own
        //  ceiling (otherwise it is a duplicate control), and it never deletes the centre (a wet
        //  pair that is pure SIDE is silent at Width 0 and nulls in mono — the pre-audit trap).
        auto sh = chord ((int) (FS * 6.0f));
        auto pk = base (1); pk.b3 = 1.0f;                       // knob at its top
        auto pw = pk;      pw.wide = true;                      // ... and the pill on top of that
        auto ok = run (pk, sh), ow = run (pw, sh);
        const double sk = db (rmsOf (sideOf (ok), S) / std::max (1e-12, rmsOf (mid (ok), S)));
        const double sw = db (rmsOf (sideOf (ow), S) / std::max (1e-12, rmsOf (mid (ow), S)));
        gate ("Wide pill reaches PAST the Width knob's ceiling (>= 2 dB)", sw - sk > 2.0,
              fmt2 ("side/mid %.2f dB -> %.2f dB", sk, sw));
        gate ("   ... and the centre survives it (never a pure-side wet)",
              rmsOf (mid (ow), S) > 0.2 * rmsOf (sh, S),
              fmt2 ("mid RMS %.4f vs dry %.4f", rmsOf (mid (ow), S), rmsOf (sh, S)));
    }
    {   // FLUTTER — the fast bank. Depth 0 so only Flutter moves d(t).
        std::string tr; bool mono = true; double prev = -1;
        for (float f : { 0.0f, 0.33f, 0.66f, 1.0f })
        {
            auto p = base (1); p.b4 = f; p.depth = 0.0f;
            auto t = dTrace (p, 8.0);
            double mn = 1e9, mx = -1e9; for (double v : t.l) { mn = std::min (mn, v); mx = std::max (mx, v); }
            const double pp = mx - mn;
            tr += fmt ("%.3f ", pp);
            if (pp < prev - 0.002) mono = false; prev = pp;
        }
        gate ("Flutter 0->100 adds the fast bank, monotonic", mono && prev > 0.6,
              "d(t) peak-to-peak ms (LFO off): " + tr);
        auto pa = base (1); pa.b4 = 0.0f; pa.depth = 0.10f;
        auto pb = base (1); pb.b4 = 1.0f; pb.depth = 0.10f;
        gate ("   ... and it is HIGH-rate energy, not more of the same",
              dHiFraction (dTrace (pb, 8.0)) > dHiFraction (dTrace (pa, 8.0)) + 0.10,
              fmt2 ("d(t) energy above 3 Hz %.3f -> %.3f",
                    dHiFraction (dTrace (pa, 8.0)), dHiFraction (dTrace (pb, 8.0))));
    }
    {   // DRIFT
        std::string tr; bool mono = true; double prev = -1;
        for (float d : { 0.0f, 0.33f, 0.66f, 1.0f })
        {
            auto p = base (1); p.b5 = d; p.depth = 0.0f; p.b4 = 0.0f;
            auto t = dTrace (p, 10.0);
            double mn = 1e9, mx = -1e9; for (double v : t.l) { mn = std::min (mn, v); mx = std::max (mx, v); }
            const double pp = mx - mn;
            tr += fmt ("%.2f ", pp);
            if (pp < prev - 0.02) mono = false; prev = pp;
        }
        gate ("Drift 0->100 wanders further, monotonic", mono && prev > 2.0,
              "d(t) peak-to-peak ms (LFO off): " + tr);
    }
    {   // COLOUR — a three-regime journey: murk -> modelled -> studio clean
        std::string trH, trT; bool monoH = true; double prevH = -1e9;
        auto in = pink ((int) (FS * 3.0f));
        double thd0 = 0, thd100 = 0;
        for (float c : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto p = base (1); p.b6 = c; p.depth = 0.2f;
            const double h = hfRatio (run (p, in).l, S, 3000.0);
            trH += fmt ("%.1f ", h);
            if (h < prevH - 0.5) monoH = false; prevH = h;

            auto pt = base (1); pt.b6 = c; pt.depth = 0.0f; pt.b4 = 0.0f;
            auto ot = run (pt, tone ((int) (FS * 2.0f), 220.0f));
            const double f0 = binMag (ot.l, 220.0, S, 16384);
            double hh = 0; for (int k = 2; k <= 6; ++k) { const double m = binMag (ot.l, 220.0 * k, S, 16384); hh += m * m; }
            const double thd = db (std::sqrt (hh) / std::max (1e-13, f0));
            trT += fmt ("%.0f ", thd);
            if (c == 0.0f) thd0 = thd; if (c == 1.0f) thd100 = thd;
        }
        gate ("Colour 0->100 opens the top, monotonic", monoH && prevH > -30.0, "HF ratio dB: " + trH);
        gate ("   ... and it is PHYSICS not EQ (the BBD poly comes off too)", thd0 - thd100 > 12.0,
              "THD dB: " + trT);
    }
    {   // LOW KEEP — the wet's low band goes away AND the dry's low band survives, centred
        std::string tr; bool mono = true; double prev = 1e9;
        auto in = pink ((int) (FS * 3.0f));
        for (float k : { 0.0f, 0.33f, 0.66f, 1.0f })
        {
            auto p = base (1); p.b7 = k; p.mix = 1.0f; p.b3 = 1.0f;
            const double lo = db (binMag (sideOf (run (p, in)), 90.0, S, 16384));
            tr += fmt ("%.1f ", lo);
            if (lo > prev + 0.5) mono = false; prev = lo;
        }
        gate ("Low Keep 0->100 pulls the bass out of the WET, monotonic", mono,
              "wet side @90 Hz dB: " + tr);
        auto p0 = base (1); p0.b7 = 0.0f; p0.mix = 1.0f;
        auto p1 = base (1); p1.b7 = 1.0f; p1.mix = 1.0f;
        const double d0 = db (binMag (mid (run (p0, in)), 90.0, S, 16384));
        const double d1 = db (binMag (mid (run (p1, in)), 90.0, S, 16384));
        const double dry = db (binMag (in, 90.0, S, 16384));
        gate ("   ... and at Mix 100% the bass SURVIVES, dry and centred", std::fabs (d1 - dry) < 3.0,
              fmt2 ("mono @90 Hz: %.1f dB (Keep off) -> %.1f dB (Keep max), ", d0, d1) + fmt ("dry %.1f dB", dry));
    }
    {   // PHASE
        std::string tr; bool mono = true; double prev = 1e9;
        auto sh = chord ((int) (FS * 12.0f));
        for (float ph : { 0.0f, 0.33f, 0.66f, 1.0f })
        {
            auto p = base (1); p.b8 = ph;
            double m, s; meanStd (corrSeries (run (p, sh), S, 3.0), m, s);
            tr += fmt ("%+.2f ", m);
            if (m > prev + 0.06) mono = false; prev = m;
        }
        gate ("Phase 0->180 rotates mono-thick to antiphase-wide, monotonic", mono && prev < 0.45,
              "L/R correlation: " + tr);
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("F. Per-Type knob liveness — the 8 x 12 cell sweep (law 1, no dead knobs)");
    {
        struct K { const char* n; int idx; };
        const K knobs[] = { {"Rate",0},{"Depth",1},{"Feedbk",2},{"Mix",3},{"Time",4},{"Detune",5},
                            {"Width",6},{"Flutter",7},{"Drift",8},{"Colour",9},{"LowKeep",10},{"Phase",11} };
        auto setK = [] (CH::Params& p, int k, float v) {
            switch (k) { case 0: p.rate = v; break; case 1: p.depth = v; break; case 2: p.feedback = v; break;
                         case 3: p.mix = v; break; case 4: p.b1 = v; break; case 5: p.b2 = v; break;
                         case 6: p.b3 = v; break; case 7: p.b4 = v; break; case 8: p.b5 = v; break;
                         case 9: p.b6 = v; break; case 10: p.b7 = v; break; default: p.b8 = v; } };
        auto in = pink ((int) (FS * 4.0f));
        int dead = 0; std::string firstDead; double worstCell = 1e9; std::string worstName;
        std::printf ("        cell = max(mono-spec dB, side-spec dB, 40*|dCorr|, 25*|dCentMod|, 6*|dFlux|); gate 1.5\n");
        std::printf ("        %-9s", "");
        for (const K& k : knobs) std::printf ("%8.8s", k.n);
        std::printf ("\n");
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            std::printf ("        %-9s", CH::typeNames()[t]);
            for (const K& k : knobs)
            {
                auto pa = base (t); setK (pa, k.idx, 0.0f);
                auto pb = base (t); setK (pb, k.idx, 1.0f);
                if (k.idx != 3) { pa.mix = 1.0f; pb.mix = 1.0f; }
                auto oa = run (pa, in), ob = run (pb, in);
                const double dm = specDist (avgSpectrum (mid (oa), S, 4), avgSpectrum (mid (ob), S, 4));
                const double ds = specDist (avgSpectrum (sideOf (oa), S, 4), avgSpectrum (sideOf (ob), S, 4));
                double ma, sa, mb, sb;
                meanStd (corrSeries (oa, S, 1.0), ma, sa);
                meanStd (corrSeries (ob, S, 1.0), mb, sb);
                const double dc  = 40.0 * std::fabs (ma - mb);
                const double dcm = 25.0 * std::fabs (centroidMod (oa, S, 20) - centroidMod (ob, S, 20));
                const double dfl = 6.0 * std::fabs (specFlux (oa, S, 16) - specFlux (ob, S, 16));
                const double cell = std::max (std::max (dm, ds), std::max (std::max (dc, dcm), dfl));
                std::printf ("%8.2f", cell);
                if (cell < worstCell) { worstCell = cell; worstName = std::string (CH::typeNames()[t]) + "/" + k.n; }
                if (cell < 1.5) { ++dead; if (firstDead.empty()) firstDead = std::string (CH::typeNames()[t]) + "/" + k.n; }
            }
            std::printf ("\n");
        }
        gate ("no dead (Type x knob) cell in the 96-cell matrix", dead == 0,
              dead ? (std::to_string (dead) + " dead, first " + firstDead)
                   : ("weakest " + worstName + " = " + fmt ("%.2f", worstCell)));
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("G. Characters change PHYSICS, not EQ (R4 / the fb345 law)");
    {
        auto in = chord ((int) (FS * 6.0f));
        double globalWorst = 1e9; std::string gw;
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            std::vector<Feat> cf;
            for (int c = 0; c < CH::kNumChars; ++c)
            {
                auto p = base (t, c);
                Feat f;
                auto o = run (p, in);
                f.mono = avgSpectrum (mid (o), S, 5); f.sideS = avgSpectrum (sideOf (o), S, 5);
                meanStd (corrSeries (o, S, 1.5), f.corrMean, f.corrStd);
                f.centMod = centroidMod (o, S); f.flux = specFlux (o, S);
                cf.push_back (f);
            }
            double worst = 1e9; int wa = 0, wb = 0;
            for (int a = 0; a < CH::kNumChars; ++a) for (int b = a + 1; b < CH::kNumChars; ++b)
            {
                const double d = std::max (std::max (specDist (cf[(size_t) a].mono, cf[(size_t) b].mono),
                                                     specDist (cf[(size_t) a].sideS, cf[(size_t) b].sideS)),
                                           std::max (30.0 * std::fabs (cf[(size_t) a].corrMean - cf[(size_t) b].corrMean),
                                                     20.0 * std::fabs (cf[(size_t) a].centMod - cf[(size_t) b].centMod)));
                if (d < worst) { worst = d; wa = a; wb = b; }
            }
            gate ((std::string ("all 8 Characters differ: ") + CH::typeNames()[t]).c_str(), worst > 1.5,
                  fmt ("closest pair %.2f", worst) + " = " + CH::charNames (t)[wa] + "/" + CH::charNames (t)[wb]);
            if (worst < globalWorst) { globalWorst = worst; gw = std::string (CH::typeNames()[t]) + " "
                                       + CH::charNames (t)[wa] + "/" + CH::charNames (t)[wb]; }
        }
        std::printf ("        closest Character pair in the whole 64-row roster: %s (%.2f)\n", gw.c_str(), globalWorst);
    }
    {   // ⚠️ A CHARACTER CHANGES THE TONE, NEVER THE LEVEL (the fb343 preset-spread law).
        //    Measured before the CharSpec::lvl makeup existed: Dark spanned 7.52 dB
        //    (Pumped +7.07 over Cheap), Micro 5.21, Trio 4.47 — that is a volume knob wearing
        //    a character's name, and it is what made Dark Murk->Pumped BANG +5.9 dB mid-note.
        //    Every lvl in the table is 10^(-measured/20) from this exact probe.
        auto in = chord ((int) (FS * 4.0f));
        double worst = 0; std::string wn;
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            double lo = 1e9, hi = -1e9;
            for (int c = 0; c < CH::kNumChars; ++c)
            {
                auto p = base (t, c); p.mix = 1.0f; p.depth = 0.45f;
                auto o = run (p, in);
                const double d = db (std::sqrt (0.5 * (rmsOf (o.l, S) * rmsOf (o.l, S) + rmsOf (o.r, S) * rmsOf (o.r, S)))
                                     / std::max (1e-12, rmsOf (in, S)));
                lo = std::min (lo, d); hi = std::max (hi, d);
            }
            if (hi - lo > worst) { worst = hi - lo; wn = CH::typeNames()[t]; }
        }
        gate ("no Character is a secret volume knob (spread < 1.5 dB)", worst < 1.5,
              fmt ("worst spread %.2f dB", worst) + " (" + wn + ")");
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("H. MONO-SUM behaviour — per Type and per Character (law 5)");
    {
        auto in = pink ((int) (FS * 8.0f));
        std::printf ("        Type       mono/L dB   ripple vs dry dB   (Mix 50, Depth 60, Width 70)\n");
        double worstSurv = 1e9; std::string ws;
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            auto p = base (t); p.mix = 0.5f;
            auto o = run (p, in);
            const double surv = db (rmsOf (mid (o), S) / std::max (1e-12, rmsOf (o.l, S)));
            Run d; d.l = in; d.r = in;
            const double rip = specDist (avgSpectrum (mid (o), S), avgSpectrum (mid (d), S));
            std::printf ("        %-9s  %+7.2f       %6.2f\n", CH::typeNames()[t], surv, rip);
            if (surv < worstSurv) { worstSurv = surv; ws = CH::typeNames()[t]; }
        }
        gate ("no Type cancels on a mono fold-down (> -6 dB)", worstSurv > -6.0,
              fmt ("worst %.2f dB", worstSurv) + " (" + ws + ")");
    }
    {
        int hostile = 0; std::string names; double worst = 1e9; std::string wn;
        auto in = pink ((int) (FS * 3.0f));
        for (int t = 0; t < CH::kNumTypes; ++t)
            for (int c = 0; c < CH::kNumChars; ++c)
            {
                auto p = base (t, c); p.mix = 0.5f;
                auto o = run (p, in);
                const double surv = db (rmsOf (mid (o), S) / std::max (1e-12, rmsOf (o.l, S)));
                const bool flagged = CH::charIsMonoHostile (t, c);
                if (surv < -6.0 && ! flagged)
                { ++hostile; if (names.size() < 90) names += std::string (CH::typeNames()[t]) + "/" + CH::charNames (t)[c] + " "; }
                if (! flagged && surv < worst) { worst = surv; wn = std::string (CH::typeNames()[t]) + "/" + CH::charNames (t)[c]; }
            }
        gate ("no UNFLAGGED Character cancels in mono (all 64 rows)", hostile == 0,
              hostile ? names : ("worst unflagged " + wn + " = " + fmt ("%.2f dB", worst)));
    }
    {
        auto in = pink ((int) (FS * 3.0f));
        auto p = base (2, 5); p.mix = 1.0f; p.b8 = 1.0f;      // Pedal / Wet Flip, Phase 180
        const double s180 = db (rmsOf (mid (run (p, in)), S) / std::max (1e-12, rmsOf (run (p, in).l, S)));
        auto p0 = base (2, 5); p0.mix = 1.0f; p0.b8 = 0.0f;   // Phase 0 = the true null
        auto o0 = run (p0, in);
        const double s0 = db (rmsOf (mid (o0), S) / std::max (1e-12, rmsOf (o0.l, S)));
        gate ("Pedal/Wet Flip IS mono-hostile, and the badge says so",
              CH::charIsMonoHostile (2, 5) && s0 < -12.0,
              fmt2 ("mono/L: %.1f dB at Phase 180, %.1f dB at Phase 0", s180, s0));
        int n = 0; for (int t = 0; t < CH::kNumTypes; ++t) for (int c = 0; c < CH::kNumChars; ++c)
            if (CH::charIsMonoHostile (t, c)) ++n;
        gate ("   ... and it is the ONLY badged row", n == 1, std::to_string (n) + " badged row of 64");
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("I. No clicks — every param swept under a sustained tone (law 4)");
    {
        auto in = tone ((int) (FS * 3.0f), 220.0f);
        const double prog = rmsOf (in);
        struct K { const char* n; int idx; };
        const K knobs[] = { {"Rate",0},{"Depth",1},{"Feedback",2},{"Mix",3},{"Time",4},{"Detune",5},
                            {"Width",6},{"Flutter",7},{"Drift",8},{"Colour",9},{"Low Keep",10},{"Phase",11} };
        auto setK = [] (CH::Params& p, int k, float v) {
            switch (k) { case 0: p.rate = v; break; case 1: p.depth = v; break; case 2: p.feedback = v; break;
                         case 3: p.mix = v; break; case 4: p.b1 = v; break; case 5: p.b2 = v; break;
                         case 6: p.b3 = v; break; case 7: p.b4 = v; break; case 8: p.b5 = v; break;
                         case 9: p.b6 = v; break; case 10: p.b7 = v; break; default: p.b8 = v; } };
        double worst = 0; std::string wn;
        for (int t = 0; t < CH::kNumTypes; ++t)
            for (const K& k : knobs)
            {
                CH e; e.prepare ((double) FS, 512);
                auto p = base (t); setK (p, k.idx, 0.0f); e.setParams (p);
                std::vector<float> L (in), R (in);
                const int B = 64;
                for (size_t i = 0; i < L.size(); i += (size_t) B)
                {
                    const int n = (int) std::min ((size_t) B, L.size() - i);
                    setK (p, k.idx, (float) i / (float) L.size());
                    e.setParams (p);
                    e.processStereo (&L[i], &R[i], n);
                }
                const double w = worstStep (L, S) / prog;
                if (w > worst) { worst = w; wn = std::string (CH::typeNames()[t]) + "/" + k.n; }
            }
        gate ("no param sweep zippers on any Type (worst step < 6x program RMS)", worst < 6.0,
              fmt ("worst %.2fx", worst) + " (" + wn + ")");
    }
    {
        auto in = chord ((int) (FS * 3.0f));
        auto bang = [&] (int t0, int c0, int t1, int c1) {
            CH e; e.prepare ((double) FS, 512);
            auto p = base (t0, c0); e.setParams (p);
            std::vector<float> L (in), R (in);
            const size_t half = L.size() / 2;
            for (size_t i = 0; i < L.size(); i += 64)
            {
                const int n = (int) std::min ((size_t) 64, L.size() - i);
                if (i >= half) { p.type = t1; p.character = c1; }
                e.setParams (p);
                e.processStereo (&L[i], &R[i], n);
            }
            double ref = 0, pk = 0;
            for (size_t i = S; i < half - 2000; ++i) ref = std::max (ref, (double) std::fabs (L[i]));
            for (size_t i = half - 500; i < half + 12000 && i < L.size(); ++i) pk = std::max (pk, (double) std::fabs (L[i]));
            return db (pk / std::max (1e-12, ref)); };

        double worstT = 0; std::string wt;
        for (int t = 0; t + 1 < CH::kNumTypes; ++t)
        { const double o = bang (t, 0, t + 1, 0);
          if (o > worstT) { worstT = o; wt = std::string (CH::typeNames()[t]) + "->" + CH::typeNames()[t + 1]; } }
        gate ("Type swap mid-note never bangs (< +3 dB)", worstT < 3.0,
              fmt ("worst %+.2f dB", worstT) + " (" + wt + ")");

        double worstC = 0; std::string wc;
        for (int t = 0; t < CH::kNumTypes; ++t)
            for (int c = 0; c + 1 < CH::kNumChars; ++c)
            { const double o = bang (t, c, t, c + 1);
              if (o > worstC) { worstC = o; wc = std::string (CH::typeNames()[t]) + " "
                                + CH::charNames (t)[c] + "->" + CH::charNames (t)[c + 1]; } }
        gate ("Character swap mid-note never bangs (< +3 dB)", worstC < 3.0,
              fmt ("worst %+.2f dB", worstC) + " (" + wc + ")");
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("J. Nothing free-runs, nothing blows up");
    {
        double worstPk = 0; int bad = 0; std::string first;
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            CH e; e.prepare ((double) FS, 512);
            auto p = base (t); p.feedback = 1.0f; p.mix = 1.0f; p.depth = 1.0f; p.b6 = 0.0f; p.b5 = 1.0f;
            e.setParams (p);
            auto blk = noise (512, 0.05f, 4242u);
            double pk = 0;
            for (int b = 0; b < (int) (60.0f * FS / 512.0f); ++b)
            {
                std::vector<float> L (blk), R (blk);
                e.setParams (p);
                e.processStereo (L.data(), R.data(), 512);
                for (int i = 0; i < 512; ++i)
                {
                    if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i]))
                    { ++bad; if (first.empty()) first = CH::typeNames()[t]; break; }
                    pk = std::max (pk, (double) std::fabs (L[(size_t) i]));
                }
            }
            worstPk = std::max (worstPk, pk);
        }
        gate ("60 s at max Feedback stays finite on every Type", bad == 0,
              bad ? ("NaN/inf first on " + first) : fmt ("worst peak %.3f", worstPk));
        gate ("   ... and bounded (peak < 4.0)", worstPk < 4.0, fmt ("%.3f", worstPk));
    }
    {
        double worst = -200; std::string wn;
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            CH e; e.prepare ((double) FS, 512);
            auto p = base (t); p.feedback = 1.0f; p.mix = 1.0f;
            e.setParams (p);
            auto blk = noise (512, 0.05f, 99u);
            for (int b = 0; b < 200; ++b)
            { std::vector<float> L (blk), R (blk); e.setParams (p); e.processStereo (L.data(), R.data(), 512); }
            std::vector<float> tail;
            for (int b = 0; b < 400; ++b)
            {
                std::vector<float> L (512, 0.0f), R (512, 0.0f);
                e.setParams (p); e.processStereo (L.data(), R.data(), 512);
                if (b > 190) tail.insert (tail.end(), L.begin(), L.end());
            }
            const double lvl = db (rmsOf (tail));
            if (lvl > worst) { worst = lvl; wn = CH::typeNames()[t]; }
        }
        gate ("the feedback tail dies within 2 s of silence (< -60 dBFS)", worst < -60.0,
              fmt ("worst tail %.1f dBFS", worst) + " (" + wn + ")");
    }
    {
        double worst = -200; std::string wn;
        const int hissType[2] = { 0, 7 }, hissChar[2] = { 7, 4 };   // Vintage/Hiss, Dark/Hissy
        for (int k = 0; k < 2; ++k)
        {
            CH e; e.prepare ((double) FS, 512);
            auto p = base (hissType[k], hissChar[k]); p.mix = 1.0f;
            e.setParams (p);
            std::vector<float> tail;
            for (int b = 0; b < 300; ++b)
            {
                std::vector<float> L (512, 0.0f), R (512, 0.0f);
                e.setParams (p); e.processStereo (L.data(), R.data(), 512);
                if (b > 100) tail.insert (tail.end(), L.begin(), L.end());
            }
            const double lvl = db (rmsOf (tail));
            if (lvl > worst) { worst = lvl; wn = std::string (CH::typeNames()[hissType[k]]) + "/"
                               + CH::charNames (hissType[k])[hissChar[k]]; }
        }
        gate ("the hissiest Characters are SILENT on silence", worst < -90.0,
              fmt ("worst idle %.1f dBFS", worst) + " (" + wn + ")");
    }
    {
        double worst = 0; std::string wn;
        auto in = chord ((int) (FS * 3.0f));
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            auto p = base (t); p.feedback = 0.9f; p.b6 = 0.0f; p.mix = 1.0f;
            auto o = run (p, in);
            double m = 0; for (size_t i = S; i < o.l.size(); ++i) m += o.l[i];
            m /= (double) (o.l.size() - S);
            const double rel = std::fabs (m) / std::max (1e-12, rmsOf (o.l, S));
            if (rel > worst) { worst = rel; wn = CH::typeNames()[t]; }
        }
        gate ("no DC offset at grit 100% + Feedback 90%", worst < 0.01,
              fmt ("worst %.4f%% of program", worst * 100.0) + " (" + wn + ")");
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("K. Sample rates — 44.1 and 96 kHz");
    {
        const float saved = FS;
        for (float sr : { 44100.0f, 96000.0f })
        {
            FS = sr;
            int bad = 0; double worstUnity = 0; std::string wn;
            auto in = chord ((int) (FS * 2.0f));
            for (int t = 0; t < CH::kNumTypes; ++t)
            {
                auto p = base (t); p.mix = 1.0f; p.feedback = 0.6f; p.depth = 0.8f;
                auto o = run (p, in);
                for (float v : o.l) if (! std::isfinite (v) || std::fabs (v) > 4.0f) { ++bad; break; }
                const double d = db (rmsOf (o.l, (size_t) (0.5f * FS)) / std::max (1e-12, rmsOf (in, (size_t) (0.5f * FS))));
                if (std::fabs (d) > std::fabs (worstUnity)) { worstUnity = d; wn = CH::typeNames()[t]; }
            }
            gate ((fmt ("%.0f Hz: every Type finite and near unity", sr)).c_str(),
                  bad == 0 && std::fabs (worstUnity) < 2.5,
                  fmt ("worst %+.2f dB", worstUnity) + " (" + wn + "), " + std::to_string (bad) + " unstable");
            // fb397 — the free taper opened to 0.02 * 2000^r01 (top 20 -> 40 Hz), so knob 0.60
            // is now 0.02 * 2000^0.6 = 1.923 Hz. Expectation tracks the ENGINE, at EVERY fs.
            auto p = base (1, 3); p.rate = 0.6f;
            double f1 = 0, f2 = 0; auto tr = dTrace (p, 20.0); dLines (tr, f1, f2, 0.1, 20.0);
            gate ((fmt ("%.0f Hz: the LFO runs at the right rate", sr)).c_str(),
                  std::fabs (f1 - 1.923) < 0.12, fmt ("d(t) line at %.3f Hz (expect 1.923)", f1));
            // and the base delay must be in MILLISECONDS, not samples
            auto pd = base (1); pd.b1 = 0.5f; pd.depth = 0.0f; pd.b4 = 0.0f; pd.mix = 0.5f;
            const double ms = delayLagMs (pd);
            gate ((fmt ("%.0f Hz: the delay is in ms, not samples", sr)).c_str(),
                  std::fabs (ms - 3.464) < 0.15, fmt ("Time knob 0.5 = %.3f ms (expect 3.464)", ms));
        }
        FS = saved;
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("L. Tempo sync — identical divisions and labels in all three fx3 devices");
    {
        bool ok = true; std::string tr;
        const int idx[3] = { 10, 19, 1 };
        const double want[3] = { 4.0, 128.0, 0.125 };
        for (int k = 0; k < 3; ++k)
        {
            const float beats = CH::divBeats (idx[k]);
            const double hz = 120.0 / (60.0 * beats);
            tr += std::string (CH::divNames()[idx[k]]) + "=" + fmt ("%.3f ", hz);
            if (std::fabs (hz - want[k]) > 0.01 * want[k]) ok = false;
        }
        gate ("sync list resolves correctly (Free at index 0, 4 bar -> 1/256)", ok, tr + "Hz at 120 BPM");
        auto p = base (1, 3); p.tempoSync = true; p.rate = 10.0f / 19.0f; p.bpm = 120.0;
        double f1 = 0, f2 = 0; auto tr2 = dTrace (p, 12.0); dLines (tr2, f1, f2, 0.5, 20.0);
        gate ("   ... and the engine actually runs at the synced rate", std::fabs (f1 - 4.0) < 0.30,
              fmt ("1/8 at 120 BPM measured %.3f Hz", f1));
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("M. CPU — us per 128-sample block at 48 kHz, per Type");
    {
        auto blk = chord (128);
        auto bench = [&] (int t, bool detune) {
            CH e; e.prepare (48000.0, 512);
            auto p = base (t); p.mix = 0.5f; p.feedback = 0.4f; p.b2 = detune ? 0.6f : 0.0f;
            e.setParams (p);
            std::vector<float> L (128), R (128);
            for (int b = 0; b < 200; ++b) { L = blk; R = blk; e.processStereo (L.data(), R.data(), 128); }
            const auto t0 = std::chrono::high_resolution_clock::now();
            const int REPS = 4000;
            for (int b = 0; b < REPS; ++b)
            { L = blk; R = blk; e.setParams (p); e.processStereo (L.data(), R.data(), 128); }
            const auto t1 = std::chrono::high_resolution_clock::now();
            return std::chrono::duration<double, std::micro> (t1 - t0).count() / REPS; };

        double worst = 0; std::string wn;
        std::printf ("        Type        Detune 0    Detune 60      %% of one core (worst)\n");
        for (int t = 0; t < CH::kNumTypes; ++t)
        {
            const double a = bench (t, false), b = bench (t, true);
            const double pct = std::max (a, b) / (128.0 / 48000.0 * 1e6) * 100.0;
            std::printf ("        %-9s %8.2f us  %8.2f us     %5.2f %%\n", CH::typeNames()[t], a, b, pct);
            if (std::max (a, b) > worst) { worst = std::max (a, b); wn = CH::typeNames()[t]; }
        }
        const double pct6 = worst / (128.0 / 48000.0 * 1e6) * 100.0 * 6.0;
        gate ("worst Type under 45 us/block (6 instances stay under 10 % of a core)", worst < 45.0,
              fmt ("worst %.2f us", worst) + " (" + wn + "), 6 instances = " + fmt ("%.1f %% of a core", pct6));
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("N. Self-check — can these gates actually fail?");
    {
        auto in = chord (N2); auto p = base (1);
        auto a = spectrum (run (p, in).l, S);
        auto b = a; for (double& v : b) v += 9.0;
        gate ("(self-check) specDist sees an injected 9 dB", specDist (a, b) > 8.5,
              fmt ("%.1f dB", specDist (a, b)));
    }
    {
        auto in = tone (N2, 220.0f); auto p = base (1);
        auto o = run (p, in);
        std::vector<float> sp = o.l;
        for (size_t i = S + 1000; i < S + 1006 && i < sp.size(); ++i) sp[i] += 1.0f;
        gate ("(self-check) the click metric sees an injected click",
              worstStep (sp, S) / rmsOf (in) > 6.0, fmt ("%.1fx program", worstStep (sp, S) / rmsOf (in)));
    }
    {
        auto v = tone ((int) (FS * 5.0f), 440.0f * std::exp2 (9.0f / 1200.0f));
        double num = 0, den = 0;
        for (double f = 424.0; f <= 456.0; f += 0.10)
        { const double m = binMag (v, f, (size_t) (1.5f * FS), (size_t) (3.0f * FS)); num += f * m * m; den += m * m; }
        const double c = 1200.0 * std::log2 ((num / den) / 440.0);
        gate ("(self-check) the pitch probe reads an injected +9.00 cents", std::fabs (c - 9.0) < 0.5,
              fmt ("%+.2f cents", c));
    }
    {
        // the delay-lag probe must read a KNOWN 5 ms delay out of a synthetic dry+wet mix
        auto in = noise ((int) (FS * 3.0f));
        const int d = (int) (0.005f * FS);
        std::vector<float> y (in.size());
        for (size_t i = 0; i < y.size(); ++i) y[i] = in[i] + (i >= (size_t) d ? 0.7f * in[i - (size_t) d] : 0.0f);
        const size_t from = (size_t) (0.6f * FS), n = (size_t) (1.6f * FS);
        double best = -1e18; size_t bl = 0;
        for (size_t lag = 20; lag < (size_t) (0.055f * FS); ++lag)
        { double a = 0; for (size_t i = from; i < from + n; ++i) a += (double) y[i] * y[i + lag];
          if (a > best) { best = a; bl = lag; } }
        gate ("(self-check) the delay-lag probe reads an injected 5.00 ms",
              std::fabs ((double) bl * 1000.0 / FS - 5.0) < 0.05,
              fmt ("%.3f ms", (double) bl * 1000.0 / FS));
    }
    {
        const AxisRes d = distance (F[1], F[1]);
        gate ("(self-check) a Type is NOT distinguishable from itself", d.val < 0.01, fmt ("%.4f", d.val));
    }
    {
        // the d(t) periodicity probe must read ~1 on a pure sine and ~0 on a random walk
        DTrace a; a.hz = 1500.0; DTrace b; b.hz = 1500.0;
        uint32_t st = 5u; double w = 0;
        for (int i = 0; i < 24000; ++i)
        {
            a.l.push_back (std::sin (6.2831853 * 0.7 * i / a.hz));
            st = st * 1664525u + 1013904223u;
            w += 0.05 * (((double) (st >> 8) / 8388608.0) - 1.0) - 0.020 * w;
            b.l.push_back (w);
        }
        gate ("(self-check) the periodicity probe separates a sine from a walk",
              dPeriodicity (a) > 0.9 && dPeriodicity (b) < 0.7,
              fmt2 ("sine %.3f, random walk %.3f", dPeriodicity (a), dPeriodicity (b)));
    }

    // ═════════════════════════════════════════════════════════════════════════
    std::printf ("\n  %d passed, %d FAILED\n", gPass, gFail);
    if (! gFails.empty())
    {
        std::printf ("\n  RED:\n");
        for (const auto& s : gFails) std::printf ("    - %s\n", s.c_str());
    }
    std::printf ("\n");
    return gFail == 0 ? 0 : 1;
}
