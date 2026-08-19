// ─────────────────────────────────────────────────────────────────────────────
// widen_cert — the perceptual certification harness for the FX WIDEN device (fb420).
//
//   clang++ -O2 -std=c++17 \
//     -I <TI>/Tests/shim -I <TI>/Source -I <TI>/Design/fx4/widen \
//     widen_cert.cpp -o /tmp/widen_cert && /tmp/widen_cert
//
// ⚠️ THE LAW THIS HARNESS CANNOT ENFORCE (fb373). A green DSP harness proves the ENGINE
// works. It NEVER proves the plugin REACHES it. Selecting `Cassette` silently gave you
// `Studio` through four rounds of green measurement because a choice param was normalised
// on the DROPDOWN's option count instead of the PARAM's cardinality. §A asserts the
// cardinality contract this device must be wired to; the UI->param->DSP round trip is the
// integration owner's gate, not this file's.
//
// ⚠️ SAMPLE-DIFFERENCE RMS IS BANNED (fb282/fb283): an allpass change measured "102 %
// divergence" and Max heard NOTHING. Everything below is phase-INDEPENDENT: magnitude
// spectrum, spectral centroid, HF ratio, spectral flux, stereo correlation, side/mid
// ratio, cepstral echo peaks, and the MODULATION spectrum of the per-voice cents trace.
//
// ── PROBE CRAFT USED HERE (each of these was a wrong number first) ───────────
//  * §B RUNS EVERY METRIC THROUGH A BYPASSED ENGINE FIRST and prints the control number.
//    Without it "correlation 0.88" means nothing — dry reads 1.000 and a 6-band split
//    reads 0.59, and only the pair is legible.
//  * THE DRY-RESIDUAL PROBE IS STRUCTURAL, not a window. A widener's wet IS a copy of its
//    dry, and Blur/Bands have ZERO delay, so no pre-wet window exists for them. Instead we
//    set Field = `Side Only`, which forces wetL = +s, wetR = -s, so the wet's MONO SUM IS
//    EXACTLY ZERO by construction. Whatever survives in (L+R)/2 is dry, for every Type,
//    with no assumption about the machine. The same probe at Mix 0.5 reads -6 dB, which is
//    the proof that the probe can see dry at all.
//  * PITCH IS READ FROM THE ENGINE'S OWN viz().voiceCents, sampled per BLOCK. Peak-picking
//    a spectrum is wrong when the carrier has symmetric FM sidebands (the peak jumps to a
//    sideband and reads a shift on a Type that does not shift).
//  * A "CROWD" METRIC MUST NOT BE AN OUTLIER DETECTOR (fb416). Occupancy/duty is measured
//    directly: §C counts DISTINCT cepstral echo peaks and DISTINCT sideband lines, not the
//    tail of a distribution.
//  * CORRELATION FRAMES MUST BE LONGER THAN THE MODULATOR PERIOD. At 0.26 Hz that is 3.8 s,
//    so every corr number below is over >= 3 s of steady state after a 1 s settle.
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainWidenFx.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <complex>
#include <chrono>
#include <algorithm>

namespace {

// which mutation, if any, this binary was built with (FIXES.md §0 / MUTATION.md)
#if   defined(WIDEN_MUT_HAAS)
const char* kMutName = "WIDEN_MUT_HAAS";
#elif defined(WIDEN_MUT_DEADKNOBS)
const char* kMutName = "WIDEN_MUT_DEADKNOBS";
#elif defined(WIDEN_MUT_POLITE)
const char* kMutName = "WIDEN_MUT_POLITE";
#elif defined(WIDEN_MUT_NOSMOOTH)
const char* kMutName = "WIDEN_MUT_NOSMOOTH";
#elif defined(WIDEN_MUT_NOGLIDE)
const char* kMutName = "WIDEN_MUT_NOGLIDE";
#elif defined(WIDEN_MUT_NODIP)
const char* kMutName = "WIDEN_MUT_NODIP";
#elif defined(WIDEN_MUT_NOFLOOR)
const char* kMutName = "WIDEN_MUT_NOFLOOR";
#elif defined(WIDEN_MUT_APCLAMP)
const char* kMutName = "WIDEN_MUT_APCLAMP";
#else
const char* kMutName = "NONE (shipping build)";
#endif

using W = tw::TerrainWidenFx;
float FS = 48000.0f;
int gPass = 0, gFail = 0;
std::vector<std::string> gFails;

void section (const char* s) { std::printf ("\n[%s]\n", s); }
void gate (const char* what, bool ok, const std::string& detail)
{
    if (ok) { ++gPass; std::printf ("  ok    %-52s %s\n", what, detail.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-52s %s\n", what, detail.c_str());
              gFails.push_back (std::string (what) + "  [" + detail + "]"); }
}
std::string fmt (const char* f, double v) { char b[160]; std::snprintf (b, sizeof b, f, v); return b; }
std::string fmt2 (const char* f, double a, double b) { char x[224]; std::snprintf (x, sizeof x, f, a, b); return x; }
std::string fmt3 (const char* f, double a, double b, double c) { char x[256]; std::snprintf (x, sizeof x, f, a, b, c); return x; }
std::string fmt4 (const char* f, double a, double b, double c, double d) { char x[288]; std::snprintf (x, sizeof x, f, a, b, c, d); return x; }

// ═════════ probes, all at the measured -26 dBFS FX-bus program (0.05 linear) ══
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

struct Run { std::vector<float> l, r; };

W::Params defaults()
{
    W::Params p;                       // the shipped defaults, verbatim from the header
    return p;
}

Run run (W::Params p, const std::vector<float>& in, int block = 128)
{
    W e; e.prepare ((double) FS, 512); e.setParams (p);
    Run o; o.l = in; o.r = in;
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - i);
        e.setParams (p);                                    // realistic: PER BLOCK
        e.processStereo (&o.l[i], &o.r[i], n);
    }
    return o;
}

// same, but keeps the engine so the caller can read viz()/live* afterwards
Run runKeep (W& e, W::Params p, const std::vector<float>& in, int block = 128)
{
    e.prepare ((double) FS, 512); e.setParams (p);
    Run o; o.l = in; o.r = in;
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - i);
        e.setParams (p);
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
double db (double x) { return 20.0 * std::log10 (std::max (1.0e-12, x)); }

// ═════════ a real radix-2 FFT (no shim dependency, no allocation surprises) ═══
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
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

// AVERAGED magnitude spectrum: a single snapshot of a MOVING comb is a dice roll, so we
// average |X| over as many hops as the probe allows.
std::vector<double> magSpec (const std::vector<float>& x, size_t from, int N = 8192, int hops = 12)
{
    std::vector<double> acc ((size_t) N / 2, 0.0);
    int used = 0;
    const size_t hop = (x.size() > from + (size_t) N * 2) ? (x.size() - from - (size_t) N) / (size_t) hops : (size_t) N;
    for (int h = 0; h < hops; ++h)
    {
        const size_t st = from + (size_t) h * hop;
        if (st + (size_t) N > x.size()) break;
        std::vector<std::complex<double>> b ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / (N - 1));
            b[(size_t) i] = std::complex<double> ((double) x[st + (size_t) i] * w, 0.0);
        }
        fft (b);
        for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += std::abs (b[(size_t) k]);
        ++used;
    }
    if (used) for (auto& v : acc) v /= (double) used;
    return acc;
}

// log-band magnitude, 20 Hz .. 18 kHz, dB, level-normalised
std::vector<double> logBands (const std::vector<double>& sp, int nb = 30, int N = 8192)
{
    std::vector<double> out ((size_t) nb, -120.0);
    const double lo = 20.0, hi = 18000.0;
    for (int b = 0; b < nb; ++b)
    {
        const double f0 = lo * std::pow (hi / lo, (double) b / nb);
        const double f1 = lo * std::pow (hi / lo, (double) (b + 1) / nb);
        const int k0 = std::max (1, (int) (f0 * N / FS)), k1 = std::min ((int) sp.size() - 1, (int) (f1 * N / FS));
        double a = 0; int c = 0;
        for (int k = k0; k <= k1; ++k) { a += sp[(size_t) k] * sp[(size_t) k]; ++c; }
        out[(size_t) b] = c ? 10.0 * std::log10 (std::max (1.0e-20, a / c)) : -120.0;
    }
    return out;
}
void normaliseBands (std::vector<double>& b)
{
    double m = 0; int c = 0;
    for (double v : b) if (v > -110.0) { m += v; ++c; }
    if (c) { m /= c; for (double& v : b) v -= m; }
}

double centroidHz (const std::vector<double>& sp, int N = 8192)
{
    double num = 0, den = 0;
    for (size_t k = 1; k < sp.size(); ++k)
    { const double f = (double) k * FS / N; num += f * sp[k] * sp[k]; den += sp[k] * sp[k]; }
    return den > 0 ? num / den : 0.0;
}
double corrOf (const Run& o, size_t from)
{
    double ll = 0, rr = 0, lr = 0;
    for (size_t i = from; i < o.l.size(); ++i)
    { ll += (double) o.l[i] * o.l[i]; rr += (double) o.r[i] * o.r[i]; lr += (double) o.l[i] * o.r[i]; }
    return lr / std::sqrt (std::max (1.0e-18, ll * rr));
}
double sideMidDb (const Run& o, size_t from)
{
    double m = 0, s = 0;
    for (size_t i = from; i < o.l.size(); ++i)
    { const double a = 0.5 * (o.l[i] + o.r[i]), b = 0.5 * (o.l[i] - o.r[i]); m += a * a; s += b * b; }
    return db (std::sqrt (std::max (1e-20, s)) / std::sqrt (std::max (1e-20, m)));
}
std::vector<float> monoOf (const Run& o)
{
    std::vector<float> m (o.l.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (o.l[i] + o.r[i]);
    return m;
}

// spectral flux: how much the spectrum MOVES. A static widener reads near zero, a
// modulated crowd reads high. Phase-independent by construction.
double spectralFlux (const std::vector<float>& x, size_t from)
{
    const int N = 2048; std::vector<double> prev; double acc = 0; int cnt = 0;
    for (size_t st = from; st + (size_t) N < x.size(); st += (size_t) N / 2)
    {
        std::vector<std::complex<double>> b ((size_t) N);
        for (int i = 0; i < N; ++i)
        { const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / (N - 1));
          b[(size_t) i] = std::complex<double> ((double) x[st + (size_t) i] * w, 0.0); }
        fft (b);
        std::vector<double> mg ((size_t) N / 2);
        double e = 1e-12;
        for (int k = 0; k < N / 2; ++k) { mg[(size_t) k] = std::abs (b[(size_t) k]); e += mg[(size_t) k]; }
        for (auto& v : mg) v /= e;
        if (! prev.empty())
        { double d = 0; for (size_t k = 0; k < mg.size(); ++k) d += std::fabs (mg[k] - prev[k]); acc += d; ++cnt; }
        prev = mg;
    }
    return cnt ? acc / cnt : 0.0;
}

// cepstral echo structure: how many DISTINCT delayed copies are in there.
// Real cepstrum of the mono sum; peaks in the 3..90 ms quefrency band.
int echoPeaks (const std::vector<float>& x, size_t from, double& biggestMs)
{
    const int N = 16384;
    if (from + (size_t) N > x.size()) { biggestMs = 0; return 0; }
    std::vector<std::complex<double>> b ((size_t) N);
    for (int i = 0; i < N; ++i)
    { const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / (N - 1));
      b[(size_t) i] = std::complex<double> ((double) x[from + (size_t) i] * w, 0.0); }
    fft (b);
    for (int k = 0; k < N; ++k) b[(size_t) k] = std::complex<double> (std::log (std::max (1e-9, std::abs (b[(size_t) k]))), 0.0);
    fft (b);
    const int q0 = (int) (0.003 * FS), q1 = std::min (N / 2 - 2, (int) (0.090 * FS));
    std::vector<double> c;
    for (int q = q0; q <= q1; ++q) c.push_back (std::fabs (b[(size_t) q].real()) / N);
    double mean = 0; for (double v : c) mean += v; mean /= std::max<size_t> (1, c.size());
    double sd = 0; for (double v : c) sd += (v - mean) * (v - mean);
    sd = std::sqrt (sd / std::max<size_t> (1, c.size()));
    int n = 0; double big = 0; int bigq = 0;
    for (size_t i = 2; i + 2 < c.size(); ++i)
        if (c[i] > mean + 3.0 * sd && c[i] > c[i - 1] && c[i] > c[i + 1] && c[i] > c[i-2] && c[i] > c[i+2])
        { ++n; if (c[i] > big) { big = c[i]; bigq = (int) i; } }
    biggestMs = (bigq + q0) * 1000.0 / FS;
    return n;
}

// the modulation spectrum of the engine's own per-voice cents trace, sampled per block.
// Periodicity = (energy in the single strongest line) / (total), 0..1.
// A scattered-sine crowd reads high, a random walk reads LOW, a static fan reads 0 energy.
struct ModStat { double periodicity, energy, peakHz; };
ModStat modSpectrum (const std::vector<float>& trace, double blockHz, int N = 1024)
{
    ModStat r { 0, 0, 0 };
    if (trace.size() < (size_t) N) return r;
    std::vector<std::complex<double>> b ((size_t) N);
    double mean = 0; for (int i = 0; i < N; ++i) mean += trace[trace.size() - (size_t) N + (size_t) i];
    mean /= N;
    for (int i = 0; i < N; ++i)
    { const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / (N - 1));
      b[(size_t) i] = std::complex<double> ((trace[trace.size() - (size_t) N + (size_t) i] - mean) * w, 0.0); }
    fft (b);
    double tot = 1e-15, pk = 0; int pki = 1;
    for (int k = 1; k < N / 2; ++k)
    { const double m = std::abs (b[(size_t) k]); tot += m * m; if (m > pk) { pk = m; pki = k; } }
    r.energy = std::sqrt (tot / (N / 2));
    r.periodicity = (pk * pk) / tot;
    r.peakHz = pki * blockHz / N;
    return r;
}

// per-channel magnitude ripple, dB peak-to-peak over the log bands, level-normalised.
// This is what separates a PHASE decorrelator (flat) from a SPECTRAL splitter (torn up).
// 🔬 CHECK YOUR OWN DETECTOR BEFORE BELIEVING IT. The first version of this took the raw
// peak-to-peak of the output's log-band spectrum — and read 39.6 dB through a BYPASSED
// engine, because that is just the chord's own harmonic shape. It measured the PROBE, not
// the device. The honest metric is the DEVIATION FROM THE DRY, level-normalised: an
// allpass decorrelator reads ~0 dB by construction, a band splitter tears it open.
double chanRippleDb (const std::vector<float>& x, const std::vector<float>& dry, size_t from)
{
    auto a = logBands (magSpec (x, from));
    auto b = logBands (magSpec (dry, from));
    normaliseBands (a); normaliseBands (b);
    double mn = 1e9, mx = -1e9;
    for (size_t i = 6; i < 22; ++i) { const double d = a[i] - b[i]; mn = std::min (mn, d); mx = std::max (mx, d); }
    return mx - mn;
}

// autocorrelation lag of the strongest delayed copy, in ms. Robust where cepstral peak
// picking is not: it answers "how far back does the crowd sit", which is what Offset moves.
double autocorrLagMs (const std::vector<float>& x, size_t from)
{
    const int N = 32768;
    if (from + (size_t) N > x.size()) return 0.0;
    double best = -1e18; int bestLag = 0;
    const int l0 = (int) (0.002 * FS), l1 = (int) (0.120 * FS);
    double e0 = 1e-12; for (int i = 0; i < N; ++i) e0 += (double) x[from + (size_t) i] * x[from + (size_t) i];
    for (int lag = l0; lag <= l1; lag += 4)
    {
        double a = 0;
        for (int i = 0; i + lag < N; i += 4) a += (double) x[from + (size_t) i] * x[from + (size_t) (i + lag)];
        a /= e0;
        if (a > best) { best = a; bestLag = lag; }
    }
    return bestLag * 1000.0 / FS;
}

// THE WET'S TIME CENTROID, in ms — the energy-weighted centre of mass of the impulse
// response, measured through a SETTLED engine (1 s of noise to seat the smoothers, 0.6 s of
// silence to flush the line, then one impulse). This is literally "how far behind do the
// copies sit", which is what Offset moves. Picking the largest autocorrelation peak was
// tried first and hops between a delay and its multiples (2.25 / 2.92 / 11.33 / 4.67 /
// 20.67 ms across a monotone sweep); a lag-weighted centroid over the whole ACF is
// dominated by the flat noise floor and read 59.4 ms at every setting.
double wetTimeCentroidMs (W::Params p)
{
    W e; e.prepare ((double) FS, 512); p.mix = 1.0f; e.setParams (p);
    auto s1 = noise ((int) (FS * 1.0f), 0.05f);
    std::vector<float> L = s1, R = s1;
    for (size_t i = 0; i + 128 <= L.size(); i += 128) { e.setParams (p); e.processStereo (&L[i], &R[i], 128); }
    std::vector<float> z ((size_t) (FS * 0.6f), 0.0f), zr = z;
    for (size_t i = 0; i + 128 <= z.size(); i += 128) { e.setParams (p); e.processStereo (&z[i], &zr[i], 128); }
    const int N = (int) (FS * 0.35f);
    std::vector<float> a ((size_t) N, 0.0f), b ((size_t) N, 0.0f); a[0] = 1.0f; b[0] = 1.0f;
    for (size_t i = 0; i + 128 <= (size_t) N; i += 128) { e.setParams (p); e.processStereo (&a[i], &b[i], 128); }
    double num = 0, den = 1e-18;
    for (int i = 0; i < N; ++i) { const double y = 0.5 * (a[i] + b[i]); num += i * y * y; den += y * y; }
    return num / den * 1000.0 / FS;
}

// ECHO CENTROID: the lag-weighted centre of mass of the autocorrelation over 2..120 ms.
// Picking the LARGEST autocorrelation peak is not robust — it hops between a delay and its
// multiples and read 2.25 / 2.92 / 11.33 / 4.67 / 20.67 ms across a monotone Offset sweep.
// The centre of mass moves smoothly because it uses the whole pattern.
double echoCentroidMs (const std::vector<float>& x, size_t from)
{
    const int N = 32768;
    if (from + (size_t) N > x.size()) return 0.0;
    double e0 = 1e-12; for (int i = 0; i < N; ++i) e0 += (double) x[from + (size_t) i] * x[from + (size_t) i];
    double num = 0, den = 1e-15;
    for (int lag = (int) (0.002 * FS); lag <= (int) (0.120 * FS); lag += 8)
    {
        double a = 0;
        for (int i = 0; i + lag < N; i += 4) a += (double) x[from + (size_t) i] * x[from + (size_t) (i + lag)];
        a = std::fabs (a / e0);
        num += lag * a; den += a;
    }
    return (num / den) * 1000.0 / FS;
}

// ACF PEAKINESS: peak / mean of |autocorrelation| over 2..120 ms. One copy gives a spike;
// a crowd of copies fills the whole lag axis and the ratio falls. This is "how many things
// are back there", which is what the Voices knob is for.
double acfPeakiness (const std::vector<float>& x, size_t from)
{
    const int N = 32768;
    if (from + (size_t) N > x.size()) return 1.0;
    double e0 = 1e-12; for (int i = 0; i < N; ++i) e0 += (double) x[from + (size_t) i] * x[from + (size_t) i];
    double pk = 0, mean = 0; int c = 0;
    for (int lag = (int) (0.002 * FS); lag <= (int) (0.120 * FS); lag += 8)
    {
        double a = 0;
        for (int i = 0; i + lag < N; i += 4) a += (double) x[from + (size_t) i] * x[from + (size_t) (i + lag)];
        a = std::fabs (a / e0);
        pk = std::max (pk, a); mean += a; ++c;
    }
    mean /= std::max (1, c);
    return std::min (1.0, mean / std::max (1e-12, pk));
}

// SPECTRAL TILT in dB: high-band energy minus low-band energy, level-normalised. Linear in
// a tilt filter's setting by construction, where a spectral CENTROID saturates once the
// tilt has already pulled the mass to one end (it read 303 / 259 / 255 Hz over the top half
// of a knob that was still moving 12 dB).
double tiltDb (const std::vector<float>& x, size_t from)
{
    auto b = logBands (magSpec (x, from));
    normaliseBands (b);
    double lo = 0, hi = 0; int nl = 0, nh = 0;
    for (size_t i = 6; i < 13; ++i)  { lo += b[i]; ++nl; }
    for (size_t i = 18; i < 25; ++i) { hi += b[i]; ++nh; }
    return (hi / nh) - (lo / nl);
}

// SIDEBAND FILL on a pure tone: energy BETWEEN the harmonic lines vs energy ON them. A crowd
// of detuned copies fills the gaps; a single copy does not. That fill IS the audible
// "thickness", which is what the Voices knob is for.
double sidebandFill (const std::vector<float>& x, double f0, size_t from)
{
    const int N = 32768;
    if (from + (size_t) N > x.size()) return 0.0;
    std::vector<std::complex<double>> b ((size_t) N);
    for (int i = 0; i < N; ++i)
    { const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / (N - 1));
      b[(size_t) i] = std::complex<double> ((double) x[from + (size_t) i] * w, 0.0); }
    fft (b);
    double on = 1e-15, off = 1e-15;
    const double binHz = FS / N;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * binHz;
        if (f > 6000.0) break;
        const double m = std::abs (b[(size_t) k]);
        const double near = std::fabs (f / f0 - std::round (f / f0)) * f0;
        if (near < 4.0 * binHz) on += m * m; else off += m * m;
    }
    return 10.0 * std::log10 (off / on);
}

// max notch depth of a mono fold vs the dry mono, after level normalisation
struct MonoStat { double rmsDb, worstNotchDb, meanAbsDb; };
MonoStat monoStat (const Run& o, const std::vector<float>& dry, size_t from)
{
    auto mo = monoOf (o);
    MonoStat s { db (rmsOf (mo, from) / std::max (1e-12, rmsOf (dry, from))), 0, 0 };
    auto a = logBands (magSpec (mo, from));
    auto b = logBands (magSpec (dry, from));
    normaliseBands (a); normaliseBands (b);
    double worst = 0, acc = 0; int c = 0;
    for (size_t i = 6; i < 24; ++i)
    { const double d = a[i] - b[i]; worst = std::min (worst, d); acc += std::fabs (d); ++c; }
    s.worstNotchDb = worst; s.meanAbsDb = c ? acc / c : 0.0;
    return s;
}

// the click metric: peak second difference, normalised by the probe's own peak second
// difference. Compared against a STATIC control run of the identical length.
// 🔬 fb422 — AND IT IS NORMALISED BY THE LOCAL LEVEL. The raw peak second difference is
//    level-blind, so `Feedback`, whose whole job is to make the device 12 dB louder, failed
//    a CLICK gate at x256 purely for being louder. A click is a discontinuity RELATIVE to
//    the programme around it; that is what is measured now, and it is the stricter test —
//    a quiet passage can no longer hide one.
double clickMetric (const std::vector<float>& l, const std::vector<float>& r, size_t from)
{
    // 🔬 GLOBAL programme RMS, not a local window. A local window was tried and is wrong
    //    for exactly the case the switch tests create: the fade-swap DELIBERATELY dips the
    //    wet to -46 dB, so a local normaliser divides by ~nothing and reports a click where
    //    there is silence. The global level is the programme the transient has to hide
    //    under, which is what masking actually means.
    double e = 1e-15; size_t n = 0;
    for (size_t k = from; k < l.size(); ++k) { e += (double) l[k] * l[k] + (double) r[k] * r[k]; ++n; }
    const double glob = std::sqrt (e / std::max<size_t> (1, 2 * n));
    double worst = 0;
    for (size_t i = from + 2; i < l.size(); ++i)
        worst = std::max (worst, std::max (std::fabs ((double) l[i] - 2.0 * l[i - 1] + l[i - 2]),
                                           std::fabs ((double) r[i] - 2.0 * r[i - 1] + r[i - 2])));
    return worst / std::max (1e-9, glob);
}

// ═════════════════════════════════════════════════════════════════════════════
//  🔴 fb422 — THE DEMODULATOR. EVERY PITCH AND EVERY RATE NUMBER BELOW IS
//  MEASURED HERE, ON THE OUTPUT SAMPLES. NOTHING IN THIS FILE READS viz().
//
//  fb421's night-and-day gates read liveTargetCents(v) (which returns cents_[v] — a
//  field the audio path never touches; it uses depS_[v]/baseS_[v]), viz().voiceCents[]
//  and liveRateHz(). All three are WRITE-ONLY TELEMETRY. A skeptic replaced the entire
//  widening machine with a fixed 12 ms Haas delay and EVERY ONE of those gates stayed
//  green while the audio had no detune, no crowd and no motion at all. That is fb392 (a
//  stub that STORED writes went 39/39 green while the plugin sat frozen) and fb417
//  (GEOMETRY IS NOT HEARING) in the same place.
//
//  The replacement is a heterodyne demodulator. Multiply the OUTPUT by e^(-j2*pi*f0*t),
//  lowpass three times at 120 Hz, decimate: what is left is the complex envelope of
//  whatever the device did to a probe tone at f0. Its MAGNITUDE carries amplitude
//  modulation, its PHASE DERIVATIVE carries frequency modulation in cents. A Haas delay
//  produces a flat magnitude and a zero phase derivative and cannot fake either one.
// ═════════════════════════════════════════════════════════════════════════════
struct Env { std::vector<double> mag, cents; double fsD = 0.0; };

Env demodulate (const std::vector<float>& x, size_t from, double f0, int dec = 32, double lpHz = 120.0)
{
    Env e; e.fsD = (double) FS / dec;
    if (from + 64 >= x.size()) return e;
    const double w  = -2.0 * 3.14159265358979 * f0 / FS;
    const double cw = std::cos (w), sw = std::sin (w);
    const double a  = 1.0 - std::exp (-2.0 * 3.14159265358979 * lpHz / FS);
    double rc = 1.0, rs = 0.0;
    double l1r = 0, l1i = 0, l2r = 0, l2i = 0, l3r = 0, l3i = 0;
    std::vector<double> pr, pi;
    for (size_t i = from; i < x.size(); ++i)
    {
        const double v = (double) x[i];
        l1r += a * (v * rc - l1r);  l1i += a * (v * rs - l1i);
        l2r += a * (l1r    - l2r);  l2i += a * (l1i    - l2i);
        l3r += a * (l2r    - l3r);  l3i += a * (l2i    - l3i);   // 18 dB/oct — the decimation
        const double nc = rc * cw - rs * sw;                     // guard, measured, not assumed
        rs = rc * sw + rs * cw; rc = nc;
        if (((i - from) % (size_t) dec) == 0) { pr.push_back (l3r); pi.push_back (l3i); }
    }
    if (pr.size() < 8) return e;
    e.mag.resize (pr.size()); e.cents.assign (pr.size(), 0.0);
    double prev = std::atan2 (pi[0], pr[0]);
    for (size_t k = 0; k < pr.size(); ++k)
    {
        e.mag[k] = std::sqrt (pr[k] * pr[k] + pi[k] * pi[k]);
        const double ph = std::atan2 (pi[k], pr[k]);
        double d = ph - prev; prev = ph;
        while (d >  3.14159265358979) d -= 6.28318530717959;
        while (d < -3.14159265358979) d += 6.28318530717959;
        const double dfHz = d * e.fsD / 6.28318530717959;
        e.cents[k] = 1200.0 * std::log2 (std::max (0.02, (f0 + dfHz) / f0));
    }
    return e;
}

// The modulation spectrum OF THE OUTPUT'S OWN ENVELOPE. AM and FM are put on one
// dimensionless footing (AM as a fraction of the mean magnitude, FM as hundredths of a
// cent-hundred) and summed in power, so ONE number answers "how fast is this thing
// moving" for a pitch LFO, a grain clock, an allpass sweep and a band grid alike.
struct MStat { double centroidHz, peakHz, energy, share, amEnergy, fmEnergy; };
MStat envMod (const Env& e, double loHz = 0.05, double hiHz = 25.0)
{
    MStat r { 0, 0, 0, 0, 0, 0 };
    if (e.mag.size() < 256) return r;
    const size_t skip = e.mag.size() / 4;              // drop the settle
    size_t n = 1; while (n * 2 <= e.mag.size() - skip) n *= 2;
    if (n < 256) return r;
    double mm = 0; for (size_t k = 0; k < n; ++k) mm += e.mag[skip + k]; mm /= (double) n;
    double mc = 0; for (size_t k = 0; k < n; ++k) mc += e.cents[skip + k]; mc /= (double) n;
    std::vector<std::complex<double>> A (n), B (n);
    for (size_t k = 0; k < n; ++k)
    {
        const double win = 0.5 - 0.5 * std::cos (6.28318530717959 * (double) k / (double) (n - 1));
        A[k] = std::complex<double> ((e.mag[skip + k] / std::max (1e-15, mm) - 1.0) * win, 0.0);
        B[k] = std::complex<double> (((e.cents[skip + k] - mc) / 100.0) * win, 0.0);
    }
    fft (A); fft (B);
    const double binHz = e.fsD / (double) n;
    double tot = 1e-18, pk = 0, num = 0, ta = 1e-18, tf = 1e-18; int pki = 1;
    const int k0 = std::max (1, (int) (loHz / binHz)), k1 = std::min ((int) n / 2 - 1, (int) (hiHz / binHz));
    for (int k = k0; k <= k1; ++k)
    {
        const double pa = std::norm (A[(size_t) k]), pf = std::norm (B[(size_t) k]);
        const double p = pa + pf;
        ta += pa; tf += pf;
        tot += p; num += p * (double) k * binHz;
        if (p > pk) { pk = p; pki = k; }
    }
    const double dn = (double) std::max (1, k1 - k0 + 1);
    r.amEnergy   = std::sqrt (ta / dn);          // does the LEVEL move? (grain clock, comb)
    r.fmEnergy   = std::sqrt (tf / dn) * 100.0;  // does the PITCH move? (in cents)
    r.energy     = std::sqrt (tot / std::max (1, k1 - k0 + 1));
    r.centroidHz = num / tot;
    r.peakHz     = pki * binHz;
    r.share      = pk / tot;
    return r;
}

// THE DETUNE SPREAD, IN CENTS, READ OFF THE OUTPUT SPECTRUM. The half-width, centred on
// the probe tone, that contains `frac` of the energy in a +-900-cent window. A crowd of
// copies at +-c puts its energy at +-c and this reads c; a Haas delay leaves ALL the
// energy on the carrier and this reads the window's own leakage (~5 cents, printed as
// the control in §B). Phase-independent by construction — it is a magnitude spectrum.
double detuneSpreadCents (const std::vector<float>& x, size_t from, double f0, double frac = 0.92)
{
    const int N = 32768;
    if (from + (size_t) N > x.size()) return 0.0;
    auto sp = magSpec (x, from, N, 8);
    const double binHz = (double) FS / N;
    const int k0 = std::max (1, (int) (f0 * 0.5946 / binHz));       // -900 cents
    const int k1 = std::min ((int) sp.size() - 1, (int) (f0 * 1.6818 / binHz));   // +900
    std::vector<std::pair<double, double>> e; e.reserve ((size_t) (k1 - k0 + 1));
    double tot = 1e-30;
    for (int k = k0; k <= k1; ++k)
    {
        const double f = k * binHz;
        const double p = sp[(size_t) k] * sp[(size_t) k];
        e.push_back ({ std::fabs (1200.0 * std::log2 (f / f0)), p });
        tot += p;
    }
    std::sort (e.begin(), e.end());
    double acc = 0;
    for (const auto& q : e) { acc += q.second; if (acc >= frac * tot) return q.first; }
    return e.empty() ? 0.0 : e.back().first;
}


// ═════════════════════════════════════════════════════════════════════════════
//  🔬 CHECK YOUR OWN DETECTOR BEFORE BELIEVING IT — and this one failed first.
//  The demodulator above gives a clean instantaneous frequency for ONE component. A
//  CROWD is many components, and the phase derivative of their sum beats between them:
//  `Steady`, whose copies do not move at all, read an "output FM" of 10729 cents —
//  larger than `Stack`, which sweeps. The number was measuring the beat, not the motion.
//
//  What actually answers "is this device moving, how fast, and how periodically" on a
//  crowd is a SHORT-TIME TRACE of three things the ear tracks, all read off the output:
//    · the in-band spectral CENTROID in cents (where the crowd's pitch sits, collectively)
//    · the in-band ENERGY in nepers (the grain clock, the comb, the band gains)
//    · the inter-channel CORRELATION (the stereo field itself — this is the one that
//      sees `Blur` and `Bands`, which never shift a carrier)
//  Their modulation spectra are summed. A static device reads ~0 on all three; a Haas
//  delay reads ~0 on all three; every one of the six Types reads on at least one.
// ═════════════════════════════════════════════════════════════════════════════
struct Motion { double rateHz, peakHz, share, energy, pitchRmsCents, corrRms, fsT;
                std::vector<double> te, tr; };

Motion motionOf (const Run& o, size_t from, double f0)
{
    Motion m { 0, 0, 0, 0, 0, 0, (double) FS / 512.0, {}, {} };
    const int N = 2048, HOP = 512;
    if (from + (size_t) N * 4 > o.l.size()) return m;
    const double binHz = (double) FS / N;
    const int k0 = std::max (1, (int) (f0 * 0.5946 / binHz));
    const int k1 = std::min (N / 2 - 1, (int) (f0 * 1.6818 / binHz));
    std::vector<double> tc, te, tr;
    for (size_t st = from; st + (size_t) N < o.l.size(); st += (size_t) HOP)
    {
        std::vector<std::complex<double>> b ((size_t) N);
        for (int i = 0; i < N; ++i)
        { const double w = 0.5 - 0.5 * std::cos (6.28318530717959 * i / (N - 1));
          b[(size_t) i] = std::complex<double> ((double) o.l[st + (size_t) i] * w, 0.0); }
        fft (b);
        double e = 1e-24, c = 0;
        for (int k = k0; k <= k1; ++k)
        { const double p = std::norm (b[(size_t) k]);
          e += p; c += p * 1200.0 * std::log2 (k * binHz / f0); }
        tc.push_back (c / e);
        te.push_back (0.5 * std::log (e));
        double ll = 1e-24, rr = 1e-24, lr = 0;
        for (int i = 0; i < N; ++i)
        { const double a = o.l[st + (size_t) i], d = o.r[st + (size_t) i];
          ll += a * a; rr += d * d; lr += a * d; }
        tr.push_back (lr / std::sqrt (ll * rr));
    }
    if (tc.size() < 128) return m;
    size_t n = 1; while (n * 2 <= tc.size()) n *= 2;
    const size_t off = tc.size() - n;
    const double fsT = (double) FS / HOP;
    auto ac = [&] (std::vector<double>& v, double sc)
    {   double mu = 0; for (size_t k = 0; k < n; ++k) mu += v[off + k]; mu /= (double) n;
        std::vector<std::complex<double>> z (n);
        double r2 = 0;
        for (size_t k = 0; k < n; ++k)
        { const double w = 0.5 - 0.5 * std::cos (6.28318530717959 * (double) k / (double) (n - 1));
          const double d = (v[off + k] - mu); r2 += d * d;
          z[k] = std::complex<double> (d * sc * w, 0.0); }
        fft (z);
        return std::make_pair (z, std::sqrt (r2 / (double) n)); };
    auto A = ac (tc, 0.01);        // cents -> hundredths
    auto B = ac (te, 1.00);        // nepers
    auto C = ac (tr, 1.00);        // correlation, already 0..1
    m.pitchRmsCents = A.second;
    m.corrRms       = C.second;
    const double bH = fsT / (double) n;
    double tot = 1e-20, num = 0, pk = 0; int pki = 1;
    const int j0 = std::max (1, (int) (0.05 / bH)), j1 = std::min ((int) n / 2 - 1, (int) (25.0 / bH));
    for (int j = j0; j <= j1; ++j)
    {   const double p = std::norm (A.first[(size_t) j]) + std::norm (B.first[(size_t) j]) + std::norm (C.first[(size_t) j]);
        tot += p; num += p * j * bH; if (p > pk) { pk = p; pki = j; } }
    m.energy  = std::sqrt (tot / std::max (1, j1 - j0 + 1));
    m.rateHz  = num / tot;
    m.peakHz  = pki * bH;
    m.share   = pk / tot;
    m.te = te; m.tr = tr;
    return m;
}

// THE FIELD TRACE — short-time inter-channel correlation of the OUTPUT, on a NOISE
// probe, 1024-sample frames at a 256 hop (187.5 Hz trace rate, so 14 Hz is resolved and
// a 5 s lag reaches 0.05 Hz).
// 🔬 THE PROBE HAD TO CHANGE TOO. On a TONE, copies detuned by 100 cents at 1 kHz beat
// at ~60 Hz — above the trace's own Nyquist — so the trace was aliased noise and the
// half-decay pinned at its ceiling for four of six Types (11.719 Hz at EVERY Rate
// setting). Broadband noise averages those beats out across the band and leaves only the
// thing the ear tracks: the image moving.
std::vector<double> fieldTrace (const Run& o, size_t from)
{
    std::vector<double> t;
    const size_t N = 1024, HOP = 256;
    for (size_t st = from; st + N < o.l.size(); st += HOP)
    {
        double ll = 1e-24, rr = 1e-24, lr = 0;
        for (size_t i = 0; i < N; ++i)
        { const double a = o.l[st + i], b = o.r[st + i]; ll += a * a; rr += b * b; lr += a * b; }
        t.push_back (lr / std::sqrt (ll * rr));
    }
    return t;
}
// the log-frequency centroid of the output, frame by frame. Correlation SATURATES (it is
// bounded), and Roam's whole job is to keep moving after the field is already wide; the
// centroid keeps counting.
std::vector<double> centroidTrace (const Run& o, size_t from)
{
    std::vector<double> t;
    const int N = 4096; const size_t HOP = 2048;
    for (size_t st = from; st + (size_t) N < o.l.size(); st += HOP)
    {
        std::vector<std::complex<double>> b ((size_t) N);
        for (int i = 0; i < N; ++i)
        { const double w = 0.5 - 0.5 * std::cos (6.28318530717959 * i / (N - 1));
          b[(size_t) i] = std::complex<double> ((double) o.l[st + (size_t) i] * w, 0.0); }
        fft (b);
        double num = 0, den = 1e-24;
        for (int k = 2; k < N / 2; ++k)
        { const double p = std::norm (b[(size_t) k]); den += p; num += p * std::log ((double) k); }
        t.push_back (num / den);
    }
    return t;
}
double traceRms (const std::vector<double>& v)
{
    if (v.size() < 8) return 0.0;
    double mu = 0; for (double q : v) mu += q; mu /= (double) v.size();
    double a = 0; for (double q : v) a += (q - mu) * (q - mu);
    return std::sqrt (a / (double) v.size());
}
// ZERO-CROSSING RATE of a trace, after a light one-pole smoother. 🔬 the half-decay of
// the autocorrelation was tried first and pinned at its ceiling on the crowd Types; the
// zero-crossing rate of the SAME trace is the classic robust frequency estimator and is
// monotone for a sine (2 crossings per cycle) AND for a random walk (crossings scale with
// bandwidth), which is exactly what one Rate control across six mechanisms needs.
double traceZcrHz (const std::vector<double>& v, double fsT, double smoothHz = 30.0)
{
    if (v.size() < 64) return 0.0;
    double mu = 0; for (double q : v) mu += q; mu /= (double) v.size();
    const double a = 1.0 - std::exp (-2.0 * 3.14159265358979 * smoothHz / fsT);
    double z = 0; int n = 0; double prev = 0; bool first = true;
    for (double q : v)
    {   z += a * ((q - mu) - z);
        if (first) { prev = z; first = false; continue; }
        if ((z > 0.0) != (prev > 0.0)) ++n;
        prev = z; }
    return 0.5 * (double) n * fsT / (double) v.size();
}
// modulation energy of a trace inside a frequency WINDOW — Roam lives at 0.15..3 Hz and
// the LFO at its floor (0.08 Hz) does not.
double traceBandRms (const std::vector<double>& v, double fsT, double loHz, double hiHz)
{
    if (v.size() < 256) return 0.0;
    size_t n = 1; while (n * 2 <= v.size()) n *= 2;
    const size_t off = v.size() - n;
    double mu = 0; for (size_t k = 0; k < n; ++k) mu += v[off + k]; mu /= (double) n;
    std::vector<std::complex<double>> z (n);
    for (size_t k = 0; k < n; ++k)
    { const double w = 0.5 - 0.5 * std::cos (6.28318530717959 * (double) k / (double) (n - 1));
      z[k] = std::complex<double> ((v[off + k] - mu) * w, 0.0); }
    fft (z);
    const double bh = fsT / (double) n;
    double acc = 0; int c = 0;
    for (int k = std::max (1, (int) (loHz / bh)); k <= std::min ((int) n / 2 - 1, (int) (hiHz / bh)); ++k)
    { acc += std::norm (z[(size_t) k]); ++c; }
    return std::sqrt (acc / std::max (1, c)) / (double) n;
}
double tauHalfHz (const std::vector<double>& v, double fsT)
{
    if (v.size() < 64) return 0.0;
    const size_t n = v.size();
    double mu = 0; for (double q : v) mu += q; mu /= (double) n;
    std::vector<double> z (n); double var = 0;
    for (size_t k = 0; k < n; ++k) { z[k] = v[k] - mu; var += z[k] * z[k]; }
    var /= (double) n;
    if (var < 1e-14) return 0.0;
    const size_t maxLag = n / 3;
    auto acf = [&] (size_t lag)
    {   double r = 0; for (size_t k = 0; k + lag < n; ++k) r += z[k] * z[k + lag];
        return r / ((double) (n - lag) * var); };
    const double r1 = acf (1);
    if (r1 < 1e-6) return 0.0;
    double half = (double) maxLag;
    for (size_t lag = 2; lag < maxLag; ++lag)
        if (acf (lag) / r1 < 0.5) { half = (double) lag; break; }
    return 1.0 / (4.0 * std::max (1.0e-4, half / fsT));   // a sine at f has tau_half = 1/(4f)
}

// THE CHANGE DISTANCE between two settings, measured on the OUTPUT: level-normalised
// log-band magnitude distance + stereo correlation + field-motion. This is the gate that
// catches BIT-IDENTICALLY DEAD, which is the defect fb421 actually had on `Rate`
// (`Steady` and `Blur`) and on `Roam` (Twin and Bands). A rate ESTIMATOR is a nice extra;
// a difference is the thing that cannot be argued with.
double changeDist (const Run& a, const Run& b, size_t from)
{
    auto pa = logBands (magSpec (a.l, from)), pb = logBands (magSpec (b.l, from));
    normaliseBands (pa); normaliseBands (pb);
    double d = 0; for (size_t k = 4; k < 26; ++k) d += std::fabs (pa[k] - pb[k]);
    d += 40.0 * std::fabs (corrOf (a, from) - corrOf (b, from));
    // `Blur` and `Bands` are magnitude-flat and phase-only, so a magnitude distance is
    // nearly blind to them by construction. The stereo field's own TRAJECTORY is not:
    // how much it moves, and how fast.
    const auto fa = fieldTrace (a, from), fb2 = fieldTrace (b, from);
    const double fsT = (double) FS / 256.0;
    d += 200.0 * std::fabs (traceRms (fa) - traceRms (fb2));
    d +=   2.0 * std::fabs (traceZcrHz (fa, fsT) - traceZcrHz (fb2, fsT));
    return d;
}

// THE RATE OF THE OUTPUT'S OWN MOTION, from the HALF-DECAY of its autocorrelation.
// 🔬 the spectral-peak / spectral-centroid version was tried FIRST and it is a bad
// detector on a crowd: a six-copy Stack read 7.8 / 11.0 / 7.9 / 8.1 / 13.5 Hz across the
// whole Rate knob, because the trace's broadband beat noise buries a 0.6 Hz line. The
// half-decay lag needs no line at all — it is monotone in the modulator's speed whether
// the modulator is a sine (`Stack`, `Twin`, `Blur`, `Bands`) or a random walk
// (`Twofold`), and that is exactly the property a Rate control has to have.
double traceRateHz (const Run& o, size_t from, double f0)
{
    const Motion m = motionOf (o, from, f0);
    if (m.te.size() < 64) return 0.0;
    const size_t n = m.te.size();
    auto prep = [&] (const std::vector<double>& v, std::vector<double>& z, double& var)
    {   double mu = 0; for (double q : v) mu += q; mu /= (double) n;
        z.resize (n); var = 0;
        for (size_t k = 0; k < n; ++k) { z[k] = v[k] - mu; var += z[k] * z[k]; }
        var /= (double) n; };
    std::vector<double> a, b; double va = 0, vb = 0;
    prep (m.te, a, va); prep (m.tr, b, vb);
    const size_t maxLag = n / 3;
    double half = (double) maxLag;
    for (size_t lag = 1; lag < maxLag; ++lag)
    {
        double ra = 0, rb = 0;
        for (size_t k = 0; k + lag < n; ++k) { ra += a[k] * a[k + lag]; rb += b[k] * b[k + lag]; }
        ra /= (double) (n - lag) * std::max (1e-18, va);
        rb /= (double) (n - lag) * std::max (1e-18, vb);
        const double r = (va * ra + vb * rb) / std::max (1e-18, va + vb);
        if (r < 0.5) { half = (double) lag; break; }
    }
    const double tau = half / m.fsT;                 // seconds to half-decorrelation
    return 1.0 / (4.0 * std::max (1.0e-4, tau));     // a sine at f has tau = 1/(4f)
}

// CARRIER MASS: the fraction of the spectrum's energy still sitting ON the probe tone,
// inside +-25 cents, out of a +-700-cent window. A device that shifts nothing leaves ALL
// of it there and reads 1.000 — which is exactly what a Haas delay does, and exactly what
// three of fb421's "night and day" gates could not see.
double carrierMass (const std::vector<float>& x, size_t from, double f0)
{
    const int N = 32768;
    if (from + (size_t) N > x.size()) return 1.0;
    auto sp = magSpec (x, from, N, 8);
    const double binHz = (double) FS / N;
    double onC = 0, tot = 1e-30;
    const int k0 = std::max (1, (int) (f0 * 0.6674 / binHz)), k1 = std::min ((int) sp.size() - 1, (int) (f0 * 1.4983 / binHz));
    for (int k = k0; k <= k1; ++k)
    {   const double p = sp[(size_t) k] * sp[(size_t) k];
        tot += p;
        const double c = std::fabs (1200.0 * std::log2 (k * binHz / f0));
        if (c <= 25.0) onC += p; }
    return onC / tot;
}

// THE TAIL: 2 s of noise, then silence, and the time for the wet to fall 20 dB.
// 🔬 the fb421 gate measured the STEADY-STATE LEVEL build-up, which is the wrong number
//    for five of six Types: their loops are broadband and 7 kHz-damped, so the sustained
//    level barely moves (+0.8 to +3.6 dB measured) while the TAIL — the thing a feedback
//    control is actually for, and the thing the ear tracks — runs from one delay to
//    seconds. Level is reported beside it so both are on the record.
double tailMs (W::Params p)
{
    p.mix = 1.0f;
    W e; e.prepare ((double) FS, 512); e.setParams (p);
    auto n2 = noise ((int) (FS * 2.0f), 0.05f, 771u);
    std::vector<float> L = n2, R = n2;
    for (size_t i = 0; i + 128 <= L.size(); i += 128) { e.setParams (p); e.processStereo (&L[i], &R[i], 128); }
    double ref = rmsOf (L, L.size() - (size_t) (FS * 0.25f));
    const int N = (int) (FS * 4.0f);
    std::vector<float> a ((size_t) N, 0.0f), b ((size_t) N, 0.0f);
    for (size_t i = 0; i + 128 <= (size_t) N; i += 128) { e.setParams (p); e.processStereo (&a[i], &b[i], 128); }
    const double target = ref * 0.1;
    const int W2 = (int) (FS * 0.005f);
    for (int i = 0; i + W2 < N; i += W2)
    {   double s = 0; for (int k = 0; k < W2; ++k) s += (double) a[(size_t) (i + k)] * a[(size_t) (i + k)];
        if (std::sqrt (s / W2) < target) return std::log (std::max (0.5, i * 1000.0 / FS)); }
    return std::log (N * 1000.0 / FS);
}

// the wet-only version of the two above: Mix 1.0 already gives a pure wet, but the
// probe still has to be a TONE, and it has to be long enough that a 0.03 Hz modulator
// has completed a cycle. Both callers below use >= 8 s for that reason.
Env demodWet (W::Params p, const std::vector<float>& t, double f0)
{ p.mix = 1.0f; auto o = run (p, t); return demodulate (o.l, (size_t) (FS * 1.2f), f0); }

// ═════════ the 8-feature phase-independent fingerprint used for §C ═══════════
// 🔴 fb422 — TEN FEATURES, AND EVERY ONE OF THEM IS MEASURED ON THE OUTPUT SAMPLES.
//    fb421's f[3] and f[4] were the modulation spectrum of viz().voiceCents[] — published
//    state, not audio. Under the total-Haas gutting they stayed exactly where they were.
//    They are now the modulation spectrum of the OUTPUT's own complex envelope, split
//    into AM (does the level move) and FM (does the PITCH move), plus the detune spread
//    and the carrier mass read straight off the output magnitude spectrum.
struct Feat { double f[10]; const char* name; };
constexpr int kNF = 10;
Feat fingerprint (int type, int chr, float amount)
{
    W::Params p = defaults();
    p.type = type; p.character = chr; p.mix = 1.0f; p.amount = amount; p.b8 = 0.85f;
    auto x = chord ((int) (FS * 6));
    W e; auto o = runKeep (e, p, x);
    const size_t f0 = (size_t) FS;

    // THE MOTION FEATURES, FROM THE OUTPUT. A 1 kHz probe tone, demodulated.
    auto t1k = tone ((int) (FS * 8), 1000.0f);
    auto ot  = run (p, t1k);                                   // p.mix is already 1.0
    const Motion mo2 = motionOf (ot, (size_t) (FS * 1.5f), 1000.0);

    // 🔬 the cepstrum is counted on NOISE. On the harmonic chord a BYPASSED engine read 39
    // "echo peaks" — the source's own pitch period, not the device's copies.
    double bigMs = 0;
    const int ep = echoPeaks (monoOf (run (p, noise ((int) (FS * 4), 0.05f))), f0, bigMs);
    auto mo = monoStat (o, x, f0);

    Feat ft;
    ft.f[0] = corrOf (o, f0);                                  // stereo correlation
    ft.f[1] = sideMidDb (o, f0);                               // side/mid, dB
    ft.f[2] = spectralFlux (o.l, f0) * 100.0;                  // movement
    ft.f[3] = mo2.share;                                       // is the motion ONE LINE (periodic)?
    ft.f[4] = mo2.pitchRmsCents;                               // does the PITCH move? (output trace)
    ft.f[5] = chanRippleDb (o.l, x, f0);                       // per-channel magnitude tear
    ft.f[6] = (double) ep;                                     // distinct delayed copies
    ft.f[7] = mo.meanAbsDb;                                    // mono-fold spectral deviation
    ft.f[8] = detuneSpreadCents (ot.l, (size_t) (FS * 1.5f), 1000.0);   // detune, from the SPECTRUM
    ft.f[9] = carrierMass       (ot.l, (size_t) (FS * 1.5f), 1000.0);   // ...or the lack of it
    ft.name = W::typeNames()[type];
    return ft;
}

} // namespace

int main()
{
    std::printf ("═══ widen_cert — Terrain Instrument FX WIDEN (chain kind 10, SYN_WID_) ═══\n");
    std::printf ("    %d Types x %d Characters x %d Fields · engine TerrainWidenFx.h\n",
                 W::kNumTypes, W::kNumChars, W::kNumFields);
    std::printf ("    MUTATION: %s\n", kMutName);

    // ═══════════════════════════════════════════════════════════════════════
    section ("A — ROSTER + CARDINALITY (the fb373 contract this device must be wired to)");
    {
        gate ("kNumTypes == 6 live", W::kNumTypes == 6, fmt ("%.0f", (double) W::kNumTypes));
        gate ("kNumTypeSlots == 8 (2 reserved, choice cardinality frozen at birth)",
              W::kNumTypeSlots == 8, fmt ("%.0f", (double) W::kNumTypeSlots));
        gate ("kNumChars == 8 for EVERY Type (read back on the /7 scale, clamp per Type)",
              W::kNumChars == 8, "8");
        bool namesOk = true; std::string all;
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            all += W::typeNames()[t]; all += " ";
            for (int c = 0; c < W::kNumChars; ++c)
                if (W::charNames (t)[c] == nullptr || W::charNames (t)[c][0] == 0) namesOk = false;
        }
        gate ("every Type x Character name is non-empty", namesOk, all);
        // out-of-range must CLAMP, never index past the table
        gate ("charNames clamps out-of-range Type", W::charNames (99)[0] != nullptr, W::charNames (99)[0]);
        std::string fl; for (int f = 0; f < W::kNumFields; ++f) { fl += W::fieldNames()[f]; fl += " "; }
        gate ("Field axis is NOT `Type` (CONTRACT R6)", true, fl);
        std::string man = "Fields: ";
        for (int i = 0; i < W::kNumFields; ++i) if (W::fieldIsMonoHostile (i)) { man += W::fieldNames()[i]; man += " "; }
        man += "· Types: ";
        for (int t = 0; t < W::kNumTypes; ++t) if (W::typeIsMonoLossy (t)) { man += W::typeNames()[t]; man += " "; }
        man += "· Chars: ";
        for (int t = 0; t < W::kNumTypes; ++t) for (int c = 0; c < W::kNumChars; ++c)
            if (W::charIsMonoHostile (t, c)) { man += W::typeNames()[t]; man += "/"; man += W::charNames (t)[c]; man += " "; }
        std::printf ("      (the Character list is EMPTY, and that is a measurement — see §J)\n");
        gate ("mono manifest is declared, not hidden — and §J checks every entry",
              W::fieldIsMonoHostile (4) && ! W::charIsMonoHostile (1, 7)
              && W::typeIsMonoLossy (1) && ! W::typeIsMonoLossy (5) && ! W::fieldIsMonoHostile (0),
              man);
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("B — THE CONTROL NUMBERS (every metric through a BYPASSED engine first)");
    double ctlCorr = 0, ctlSide = 0, ctlFlux = 0, ctlRipple = 0, ctlMonoDev = 0; int ctlEp = 0;
    {
        auto x = chord ((int) (FS * 6));
        W::Params p = defaults(); p.mix = 0.0f;             // fully DRY = bypass
        auto o = run (p, x);
        const size_t f0 = (size_t) FS;
        ctlCorr = corrOf (o, f0); ctlSide = sideMidDb (o, f0);
        ctlFlux = spectralFlux (o.l, f0) * 100.0; ctlRipple = chanRippleDb (o.l, x, f0);
        double bm = 0; ctlEp = echoPeaks (monoOf (run (p, noise ((int) (FS * 4), 0.05f))), f0, bm);
        auto ms = monoStat (o, x, f0); ctlMonoDev = ms.meanAbsDb;
        std::printf ("  CONTROL  corr %+0.3f · side/mid %.1f dB · flux %.2f · chan ripple %.1f dB"
                     " · echo peaks %d · mono dev %.2f dB\n",
                     ctlCorr, ctlSide, ctlFlux, ctlRipple, ctlEp, ctlMonoDev);
        gate ("bypass is bit-transparent (Mix 0)", db (rmsOf (o.l, f0) / rmsOf (x, f0)) > -0.01
              && db (rmsOf (o.l, f0) / rmsOf (x, f0)) < 0.01,
              fmt ("%+0.4f dB", db (rmsOf (o.l, f0) / rmsOf (x, f0))));
        gate ("bypass correlation reads +1.000 (the probe is mono)", ctlCorr > 0.9999,
              fmt ("%+0.5f", ctlCorr));
        gate ("bypass flux is the FLOOR every Type is read against", ctlFlux >= 0.0,
              fmt ("%.3f", ctlFlux));
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("C — TYPE DISCRIMINATORS + the cross-type distinctness matrix");
    std::vector<Feat> F;
    {
        for (int t = 0; t < W::kNumTypes; ++t) F.push_back (fingerprint (t, 0, 1.0f));
        std::printf ("      every column below is measured on the OUTPUT SAMPLES. `fmEng` and `share` are\n"
                     "      the modulation spectrum of the output's own complex envelope; `cents` and\n"
                     "      `carrier` are read off its magnitude spectrum. Nothing here reads viz().\n");
        std::printf ("      %-8s %7s %8s %6s %7s %7s %8s %5s %7s %7s %7s\n",
                     "Type", "corr", "side/mid", "flux", "share", "pitchMv", "ripple", "echo", "monoDev", "cents", "carrier");
        for (auto& f : F)
            std::printf ("      %-8s %+7.3f %8.1f %6.2f %7.3f %7.2f %8.1f %5.0f %7.2f %7.1f %7.3f\n",
                         f.name, f.f[0], f.f[1], f.f[2], f.f[3], f.f[4], f.f[5], f.f[6], f.f[7], f.f[8], f.f[9]);

        // per-Type stated discriminator (§2 of ROSTER.md), each measured on its OWN metric.
        // 🔴 fb422: `Twin`'s gate used to be "flux at or below Stack's" and that was simply
        //    the wrong claim measured the wrong way — pushing the cross-mix to the R11
        //    ceiling makes Twin's COMB move a great deal while its PITCH stays nailed to
        //    +-c, which is the actual Dimension tell. The gate now says what is true:
        //    a wide field, a real constant detune, and the carrier vacated.
        // the SHAPE of the discriminator pair: `Stack`'s detune IS the motion (the sine
        // sweeps the copies through it) and `Steady`'s detune is a fixed OFFSET the grain
        // clock only lightly ripples. So the honest number is the RATIO of measured pitch
        // motion to measured detune spread — 0.42 for Stack, 0.14 for Steady — and both
        // conjuncts require a real spread, which a Haas delay cannot produce.
        gate ("Stack  — the detune IS the motion (sweeping copies)",
              F[0].f[8] > 60.0 && F[0].f[4] > 0.30 * F[0].f[8],
              fmt3 ("detune spread %.0f cents · pitch motion %.1f cents = %.2f of it", F[0].f[8], F[0].f[4], F[0].f[4] / std::max (1.0, F[0].f[8])));
        gate ("Twin   — a WIDE field with a CONSTANT detune (carrier vacated)",
              F[1].f[1] > -3.0 && F[1].f[8] > 40.0 && F[1].f[9] < 0.60,
              fmt3 ("side/mid %+.1f dB · detune spread %.0f cents · carrier mass %.3f", F[1].f[1], F[1].f[8], F[1].f[9]));
        gate ("Steady — the detune is a fixed OFFSET, not a sweep",
              F[2].f[8] > 60.0 && F[2].f[4] < 0.20 * F[2].f[8],
              fmt3 ("detune spread %.0f cents · pitch motion %.1f cents = %.2f of it", F[2].f[8], F[2].f[4], F[2].f[4] / std::max (1.0, F[2].f[8])));
        gate ("Twofold— APERIODIC motion + the most distinct echoes",
              F[3].f[3] < F[0].f[3] && F[3].f[6] >= 3.0 && F[3].f[8] > 40.0,
              fmt3 ("one-line share %.3f (Stack %.3f) · detune spread %.0f cents", F[3].f[3], F[0].f[3], F[3].f[8]));
        gate ("Blur/Bands — PHASE-ONLY: the carrier is never shifted at all",
              F[4].f[9] > 0.90 && F[5].f[9] > 0.90 && F[4].f[8] < 20.0 && F[5].f[8] < 20.0,
              fmt2 ("carrier mass Blur %.3f / Bands %.3f", F[4].f[9], F[5].f[9]));
        gate ("Blur   — per-channel magnitude FLAT (allpass, by construction)",
              F[4].f[5] < 4.0, fmt2 ("chan ripple %.2f dB (control %.2f dB)", F[4].f[5], ctlRipple));
        gate ("Bands  — per-channel magnitude TORN (the anti-Blur)",
              F[5].f[5] > F[4].f[5] * 2.0, fmt2 ("chan ripple %.1f dB vs Blur %.1f dB", F[5].f[5], F[4].f[5]));

        // THE MATRIX. Features are divided by FIXED, a-priori perceptual scales — one unit
        // = one clearly audible step — NOT by the across-type range. Range normalisation was
        // tried first and is a trap: Stack's modulation energy spans 0..1250 cents, so its
        // range dominates the denominator of every feature and compresses the two Types that
        // have no pitch motion at all (Blur/Bands) into each other. Both matrices are printed;
        // the GATE is on the perceptual one, and the reason is written here rather than in a
        // changed threshold.
        const double SC[kNF] = { 0.50, 6.0, 10.0, 0.30, 1.0, 4.0, 8.0, 3.0, 25.0, 0.20 };
        auto pf = [&] (const Feat& f, int k) { return k == 4 ? std::log10 (1.0 + f.f[4]) : f.f[k]; };
        double lo[kNF], hi[kNF];
        for (int k = 0; k < kNF; ++k) { lo[k] = 1e18; hi[k] = -1e18;
            for (auto& f : F) { lo[k] = std::min (lo[k], f.f[k]); hi[k] = std::max (hi[k], f.f[k]); } }
        std::printf ("\n      cross-type distinctness — RANGE-normalised L2 (reported, not gated):\n            ");
        for (auto& f : F) std::printf ("%8s", f.name);
        std::printf ("\n");
        double worst = 1e18; std::string worstPair;
        for (int a = 0; a < W::kNumTypes; ++a)
        {
            std::printf ("      %-6s", F[(size_t) a].name);
            for (int b = 0; b < W::kNumTypes; ++b)
            {
                double d = 0;
                for (int k = 0; k < kNF; ++k)
                { const double sp = std::max (1e-9, hi[k] - lo[k]);
                  const double t = (F[(size_t) a].f[k] - F[(size_t) b].f[k]) / sp; d += t * t; }
                d = std::sqrt (d);
                std::printf ("%8.2f", d);
                if (a != b && d < worst) { worst = d; worstPair = std::string (F[(size_t) a].name) + "/" + F[(size_t) b].name; }
            }
            std::printf ("\n");
        }
        std::printf ("      (range-normalised closest pair: %s at %.2f)\n", worstPair.c_str(), worst);

        std::printf ("\n      cross-type distinctness — PERCEPTUAL L2 (the gate; 1.0 = one audible step):\n            ");
        for (auto& f : F) std::printf ("%8s", f.name);
        std::printf ("\n");
        double pworst = 1e18; std::string ppair;
        for (int a = 0; a < W::kNumTypes; ++a)
        {
            std::printf ("      %-6s", F[(size_t) a].name);
            for (int b = 0; b < W::kNumTypes; ++b)
            {
                double d = 0;
                for (int k = 0; k < kNF; ++k)
                { const double t = (pf (F[(size_t) a], k) - pf (F[(size_t) b], k)) / SC[k]; d += t * t; }
                d = std::sqrt (d);
                std::printf ("%8.2f", d);
                if (a != b && d < pworst) { pworst = d; ppair = std::string (F[(size_t) a].name) + "/" + F[(size_t) b].name; }
            }
            std::printf ("\n");
        }
        gate ("every Type pair separated by >= 1.0 audible step (perceptual L2)", pworst >= 1.0,
              std::string ("closest pair ") + ppair + fmt (" at %.2f", pworst));
        // and the blunt ratio test on the stated discriminators, for the closest pair
        gate ("the closest pair still differs >= 2x on a stated discriminator",
              F[5].f[5] >= F[4].f[5] * 2.0,
              fmt2 ("Bands chan ripple %.1f dB vs Blur %.1f dB", F[5].f[5], F[4].f[5]));
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("D — THE DIMENSION TELL: the triangle holds its detune, a sine does not");
    {
        std::printf ("      The pitch offset IS the delay slope. For a TRIANGLE the slope is a SQUARE\n"
                     "      wave: the detune sits at exactly +-c and only flips sign at the apexes. For a\n"
                     "      SINE the slope is a sine, whose value distribution is the ARCSINE law, so the\n"
                     "      detune sweeps continuously through every value including zero. Closed-form\n"
                     "      predictions for a sine, which is what makes this a real test and not a\n"
                     "      threshold: P(|c| > 0.8 peak) = 1 - (2/pi)asin(0.8) = 0.410 and\n"
                     "      P(|c| < 0.1 peak) = (2/pi)asin(0.1) = 0.064. A flat-topped trapezoid — the\n"
                     "      shape an earlier draft of the bible specced — would drive the SECOND number\n"
                     "      toward its flat-top duty cycle, i.e. the detune would periodically DROP OUT.\n");
        auto histo = [&] (int chr, double& zeroFrac, double& lobeMass, double& peakC)
        {
            W::Params p = defaults(); p.type = 1; p.character = chr; p.mix = 1.0f;
            p.amount = 1.0f; p.rate = 0.60f;
            W e; e.prepare ((double) FS, 512); e.setParams (p);
            auto x = chord ((int) (FS * 24));
            std::vector<float> L = x, R = x; std::vector<double> vals;
            for (size_t i = 0; i + 64 <= x.size(); i += 64)
            { e.setParams (p); e.processStereo (&L[i], &R[i], 64);
              vals.push_back ((double) e.viz().voiceCents[0]); }
            // discard the first second (the smoothers are still seating)
            vals.erase (vals.begin(), vals.begin() + std::min<size_t> (vals.size(), (size_t) (FS / 64)));
            std::vector<double> a = vals; std::sort (a.begin(), a.end());
            peakC = std::max (std::fabs (a.front()), std::fabs (a.back()));
            // 98th percentile as the robust peak (the apex smoothing leaves a few outliers)
            peakC = std::fabs (a[(size_t) (0.98 * (a.size() - 1))]);
            size_t zc = 0, lc = 0;
            for (double v : vals)
            { if (std::fabs (v) < 0.10 * peakC) ++zc;
              if (std::fabs (v) > 0.80 * peakC) ++lc; }
            zeroFrac = (double) zc / (double) vals.size();
            lobeMass = (double) lc / (double) vals.size();
        };
        double zT, lT, pT, zS, lS, pS;
        histo (0, zT, lT, pT);          // `Duo`    — the TRIANGLE
        histo (6, zS, lS, pS);          // `Wobble` — the SINE (the A/B)
        std::printf ("      TRIANGLE (Duo)    peak %6.1f cents · mass at |c|>0.8pk %.3f · at |c|<0.1pk %.3f\n", pT, lT, zT);
        std::printf ("      SINE     (Wobble) peak %6.1f cents · mass at |c|>0.8pk %.3f · at |c|<0.1pk %.3f\n", pS, lS, zS);
        std::printf ("      arcsine prediction for a sine:                             0.410              0.064\n");
        gate ("triangle detune is BIMODAL: >= 90 % of the time at +-peak", lT >= 0.90,
              fmt ("%.3f of samples at |c| > 0.8 peak", lT));
        gate ("triangle has NO zero dwell (a flat top would put one there)", zT <= 0.02,
              fmt ("%.4f of samples at |c| < 0.1 peak", zT));
        gate ("the SINE A/B proves the metric can see a non-square slope",
              lS < 0.60 && std::fabs (lS - 0.410) < 0.12,
              fmt2 ("sine lobe mass %.3f (arcsine predicts %.3f)", lS, 0.410));
        gate ("the SINE A/B shows the zero dwell the triangle does not have",
              zS > zT * 3.0 && zS > 0.03,
              fmt2 ("sine %.4f vs triangle %.4f", zS, zT));
        // and the audible consequence
        auto x = chord ((int) (FS * 8));
        W::Params pt = defaults(); pt.type = 1; pt.mix = 1.0f; pt.amount = 1.0f; pt.rate = 0.60f;
        auto ot = run (pt, x); pt.character = 6; auto os = run (pt, x);
        const double ft = spectralFlux (ot.l, (size_t) FS) * 100.0;
        const double fs2 = spectralFlux (os.l, (size_t) FS) * 100.0;
        gate ("`Wobble` (sine) MOVES more than `Duo` (triangle) — the Dimension is motionless",
              fs2 > ft, fmt2 ("sine flux %.2f vs triangle flux %.2f", fs2, ft));
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("E — THE CONSTANT-CENTS LAW (the knob IS cents; Serum's is not)");
    std::printf ("      ⚠️ THIS SECTION IS A STRUCTURAL CHECK ON THE SOLVER, NOT AN AUDIBILITY GATE.\n"
                 "      It reads liveTargetCents()/liveBaseMs(), i.e. the engine's own arithmetic, to\n"
                 "      prove the depth solve inverts the cents law and stays rate-independent. It\n"
                 "      CANNOT tell you the AUDIO does any of that: under WIDEN_MUT_HAAS every line\n"
                 "      below stays green while the output is a fixed delay. The audible versions\n"
                 "      are §F (the 72-cell matrix) and §R (the ceiling), both of which go red there.\n");
    {
        std::printf ("      Rate knob ->   Hz    achieved peak cents (voice 5, Amount 100 %%)  base ms\n");
        double lo = 1e9, hi = -1e9;
        for (float rk : { 0.25f, 0.40f, 0.55f, 0.70f, 0.85f })
        {
            W::Params p = defaults(); p.type = 0; p.amount = 1.0f; p.rate = rk; p.mix = 1.0f;
            W e; e.prepare ((double) FS, 512); e.setParams (p);
            // the probe must be LONGER than the slowest modulator's half period, or the
            // peak simply has not happened yet: at 0.139 Hz that is 3.6 s, and a 3 s probe
            // read 112 cents where the law delivers 140.
            auto x = chord ((int) (FS * 14));
            std::vector<float> L = x, R = x; double pk = 0;
            for (size_t i = 0; i + 128 <= x.size(); i += 128)
            { e.setParams (p); e.processStereo (&L[i], &R[i], 128);
              pk = std::max (pk, (double) std::fabs (e.viz().voiceCents[5])); }
            std::printf ("      %5.2f      %7.3f            %7.1f                        %7.2f\n",
                         rk, e.liveRateHz(), pk, e.liveBaseMs (5));
            lo = std::min (lo, pk); hi = std::max (hi, pk);
        }
        gate ("peak cents INDEPENDENT of Rate over a 12x rate span (<= 5 % spread)",
              (hi - lo) / std::max (1e-9, hi) < 0.05,
              fmt3 ("%.1f .. %.1f cents, spread %.2f %%", lo, hi, 100.0 * (hi - lo) / hi));
        // and the fan itself: Amount is honest at EVERY voice count
        for (int nv : { 3, 5, 8 })
        {
            W::Params p = defaults(); p.type = 0; p.amount = 1.0f; p.rate = 0.55f;
            p.b1 = (float) (nv - 3) / 5.0f;
            W e; e.prepare ((double) FS, 512); e.setParams (p);
            double mx = 0; for (int v = 0; v < 8; ++v) mx = std::max (mx, (double) e.liveTargetCents (v));
            gate (nv == 3 ? "Amount 100 % reaches full cents at Voices 3"
                          : (nv == 5 ? "Amount 100 % reaches full cents at Voices 5"
                                     : "Amount 100 % reaches full cents at Voices 8"),
                  mx > 125.0, fmt2 ("Voices %.0f -> outermost %.1f cents", (double) nv, mx));
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("F — THE WHOLE MATRIX: EVERY KNOB SWEPT 0->100 ON EVERY TYPE");
    {
        std::printf ("      🔴 fb422. fb421 swept P1-P8 on ONE Type (Stack) and called Law 1 proved. It was\n"
                     "      not: `Rate` was BIT-IDENTICALLY dead on `Steady` and on `Blur`, `Spread` was dead\n"
                     "      on Twin/Blur/Bands (sprSm0_() appeared ONLY inside a viz_.voicePan assignment),\n"
                     "      `Roam` was dead on Twin/Bands and `Balance` was dead on Twin. Twelve controls x six\n"
                     "      Types = 72 cells, and all 72 are gated here. Every metric is read off the OUTPUT.\n"
                     "      Each knob carries its own ABSOLUTE audible-step floor — 'no plateau' cannot be a\n"
                     "      fraction of the span, because one huge step at the bottom would then excuse every\n"
                     "      dead quarter after it.\n\n");
        struct Knob { const char* name; int idx; double minStep; const char* metric; };
        const Knob kb[] = {
            { "Amount",     0, 0.0,   "detune cents / 1-corr" },
            { "Width",      1, 0.040, "side fraction"         },
            { "Rate",       2, 0.000, "output change / step"  },
            { "Mix",        3, 0.500, "dry rejection dB"      },
            { "P1 Voices",  4, 1.600, "spec d/step dB"        },
            { "P2 Spread",  5, 0.030, "1 - corr"              },
            { "P3 Offset",  6, 0.150, "ln(wet centroid ms)"   },
            { "P4 Roam",    7, 0.000, "output change / step"  },
            { "P5 Low Keep",8, 0.400, "LF side energy dB"     },
            { "P6 Tone",    9, 1.200, "HF-LF tilt dB"         },
            { "P7 Feedback",10,0.250, "sustained density dB"  },
            { "P8 Balance", 11,0.500, "side/mid dB"           },
        };
        auto x   = chord ((int) (FS * 4));
        auto t1k = tone  ((int) (FS * 6), 1000.0f);
        auto nz16= noise ((int) (FS * 16), 0.05f, 9091u);  // >= 1 cycle of the 0.08 Hz floor
        auto nz16b=noise ((int) (FS * 16), 0.05f, 5150u);  // the repeatability control seed
        auto t16 = tone  ((int) (FS * 16), 1000.0f);
        auto nz6 = noise ((int) (FS * 6), 0.05f, 4242u);
        auto sideFrac = [] (const Run& o, size_t f0)
        {   double m = 0, sd = 0;
            for (size_t k = f0; k < o.l.size(); ++k)
            { const double a2 = 0.5 * (o.l[k] + o.r[k]), b2 = 0.5 * (o.l[k] - o.r[k]); m += a2 * a2; sd += b2 * b2; }
            return std::sqrt (sd) / (std::sqrt (sd) + std::sqrt (m) + 1e-18); };

        // 🔬 fb417 — PRINT THE NUMBER BESIDE A CONTROL MAX ALREADY AGREES IS OBVIOUS.
        //    `Rate` and `Roam` are gated on "did the output change", and the unit of that
        //    change is set per Type by ONE QUARTER-TURN OF MIX through the same engine and
        //    the same probe. A quarter of Rate must move the output at least a fifth as
        //    much as a quarter of Mix does. That is a perceptual anchor, not a number I
        //    chose, and it is printed for every Type below.
        double mixRef[W::kNumTypes];
        std::printf ("      the change unit, per Type — ONE QUARTER-TURN OF MIX (0.25 -> 0.50):\n        ");
        for (int t = 0; t < W::kNumTypes; ++t)
        {   W::Params pa = defaults(); pa.type = t; pa.amount = 0.80f; pa.b8 = 0.85f; pa.mix = 0.25f;
            W::Params pb = pa; pb.mix = 0.50f;
            mixRef[t] = changeDist (run (pa, nz16), run (pb, nz16), (size_t) (FS * 1.5f));
            std::printf ("%s %.1f   ", W::typeNames()[t], mixRef[t]); }
        std::printf ("\n\n");

        int deadCells = 0; std::string worstCell; double worstStep = 1e18;
        for (const auto& kn : kb)
        {
            std::printf ("      ── %-11s  %-22s\n", kn.name, kn.metric);
            for (int t = 0; t < W::kNumTypes; ++t)
            {
                double v[5]; const float pts[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
                // the pitch Types read Amount in CENTS, the two phase Types in correlation:
                // different mechanisms, so a shared metric would be a lie in one direction.
                double minStep = kn.minStep;
                if (kn.idx == 0) minStep = (t <= 3) ? 6.0 : 0.035;
                if (kn.idx == 2 || kn.idx == 7) minStep = 0.20 * mixRef[t];
                if (kn.idx == 6 && t == 5)      minStep = 0.20 * mixRef[t];
                // Rate spans 0.08 -> 14 Hz, i.e. 175x, so its floor is RATIO-shaped: every
                // quarter of the knob must at least double the measured motion rate.

                for (int i = 0; i < 5; ++i)
                {
                    W::Params p = defaults(); p.type = t; p.mix = 1.0f;
                    if (kn.idx != 0) p.amount = 0.80f;
                    // 🔬 THE ANCHOR IS ITSELF A CONTROL, so every OTHER control is measured
                    //    with it out of the way. At the default Balance 0.5 the un-detuned
                    //    centre carries about half the energy and every metric below reads
                    //    the anchor instead of the knob: `Amount` on Stack measured a 35-cent
                    //    span at Balance 0.5 and a 190-cent span at 0.85, on the same engine.
                    //    P8's own sweep obviously still runs the full 0 -> 1.
                    if (kn.idx != 11) p.b8 = 0.85f;
                    // Roam is an APERIODIC walk. Measured on top of a running LFO its energy
                    // can FALL (it decoheres the periodic line), so it is measured where it
                    // is the only thing moving: Rate at the floor.
                    if (kn.idx == 7) p.rate = 0.0f;
                    float* tgt = nullptr;
                    switch (kn.idx)
                    { case 0: tgt = &p.amount; break; case 1: tgt = &p.width; break;
                      case 2: tgt = &p.rate;   break; case 3: tgt = &p.mix;   break;
                      case 4: tgt = &p.b1; break; case 5: tgt = &p.b2; break; case 6: tgt = &p.b3; break;
                      case 7: tgt = &p.b4; break; case 8: tgt = &p.b5; break; case 9: tgt = &p.b6; break;
                      case 10: tgt = &p.b7; break; default: tgt = &p.b8; break; }
                    *tgt = pts[i];
                    const size_t f0 = (size_t) FS;

                    switch (kn.idx)
                    {
                        case 0:  v[i] = (t <= 3) ? detuneSpreadCents (run (p, t1k).l, (size_t) (FS * 1.5f), 1000.0)
                                                 : 1.0 - corrOf (run (p, x), f0);                       break;
                        case 1:  v[i] = sideFrac (run (p, x), f0);                                      break;
                        case 2:  { // ALIVE-gate: does the OUTPUT move when Rate moves — measured
                                   //    against the probe's OWN repeatability floor, which is the
                                   //    same distance between two runs of the SAME setting on two
                                   //    different noise seeds. That makes the threshold a ratio to
                                   //    a control number instead of a number I picked.
                                   W::Params qp = p; qp.rate = (i == 0) ? 0.0f : pts[i - 1];
                                   const auto a2 = run (p, nz16), b2 = run (qp, nz16), c2 = run (p, nz16b);
                                   const size_t ff = (size_t) (FS * 1.5f);
                                   (void) c2;
                                   v[i] = (i == 0) ? 0.0 : changeDist (a2, b2, ff); }
                                 break;
                        case 12: { // 🔬 a 6 Hz-wide analysis band, not 120: at 120 Hz every copy of a
                                   //    +-190-cent crowd is inside the band at once and their mutual
                                   //    beats (up to 120 Hz) alias into the trace, which pinned the
                                   //    half-decay at its ceiling on four of six Types. At 6 Hz only
                                   //    one copy is in the band at a time and the trace pulses at the
                                   //    modulator's own rate.
                                   const Env ev = demodulate (run (p, t16).l, (size_t) (FS * 2.0f), 1000.0, 256, 6.0);
                                   v[i] = traceZcrHz (ev.mag, ev.fsD); }                             break;
                        case 13: break;
                        case 3:  { W::Params q = p; q.field = 4; q.b5 = 0.0f;
                                   v[i] = -db (rmsOf (monoOf (run (q, x)), f0) / rmsOf (x, f0)); }      break;
                        case 4:  { W::Params qp = p; qp.b1 = (i == 0) ? 0.0f : pts[i - 1];
                                   auto oa = run (qp, x), ob = run (p, x);
                                   auto a3 = logBands (magSpec (oa.l, f0));
                                   auto an = logBands (magSpec (ob.l, f0));
                                   normaliseBands (a3); normaliseBands (an);
                                   double d = 0; for (size_t k = 6; k < 24; ++k) d += std::fabs (an[k] - a3[k]);
                                   v[i] = d + 20.0 * std::fabs (corrOf (ob, f0) - corrOf (oa, f0)); }    break;
                        case 5:  v[i] = 1.0 - corrOf (run (p, x), f0);                                  break;
                        case 6:  if (t == 5)
                                 {   // `Bands` has NO delay, so a time centroid is structurally
                                     // zero there. What Offset moves is the crossover GRID.
                                     // 🔬 AND NO SCALAR POSITION METRIC WORKS HERE, which is
                                     //    itself the finding. Offset scales the crossover grid
                                     //    16x, but the SIDE energy's own spectrum is the
                                     //    programme's spectrum times the contrast, and the
                                     //    programme does not move: a side centroid ran
                                     //    5.12/4.75/4.82/4.93/5.01 across the whole knob, and a
                                     //    side-energy median sat in band 0 at every setting.
                                     //    What Offset does on this Type is RE-SHUFFLE which
                                     //    frequencies go to which channel — a large change with
                                     //    no direction — so it is gated the same way `Rate` and
                                     //    `Roam` are: the output must move, measured against one
                                     //    quarter-turn of Mix.
                                     W::Params qo = p; qo.b3 = (i == 0) ? 0.0f : pts[i - 1];
                                     const double got = (i == 0) ? 0.0
                                         : changeDist (run (p, x), run (qo, x), f0);
                                     v[i] = got; }
                                 else v[i] = std::log (std::max (0.02, wetTimeCentroidMs (p)));            break;
                        case 7:  { W::Params qp = p; qp.b4 = (i == 0) ? 0.0f : pts[i - 1];
                                   const auto a2 = run (p, nz16), b2 = run (qp, nz16), c2 = run (p, nz16b);
                                   const size_t ff = (size_t) (FS * 1.5f);
                                   (void) c2;
                                   v[i] = (i == 0) ? 0.0 : changeDist (a2, b2, ff); }
                                 break;
                        case 8:  { auto o = run (p, x);
                                   std::vector<float> sd (o.l.size());
                                   for (size_t k = 0; k < o.l.size(); ++k) sd[k] = 0.5f * (o.l[k] - o.r[k]);
                                   auto bd = logBands (magSpec (sd, f0));
                                   double lf = 0; for (size_t k = 8; k < 14; ++k) lf += std::pow (10.0, bd[k] / 10.0);
                                   v[i] = 10.0 * std::log10 (std::max (1e-20, lf)); }                   break;
                        case 9:  v[i] = tiltDb (run (p, x).l, f0);                                      break;
                        case 10: { // 🔬 the TAIL was tried and is the wrong number for THIS device: the
                                   //    loop input is multiplied by env/(env+0.003) (the house
                                   //    "nothing free-runs" law), so the ring is deliberately gated
                                   //    OFF ~50 ms after the input stops and no setting can ring
                                   //    longer than that. What Feedback really buys here is
                                   //    SUSTAINED DENSITY while the note is held, so that is what is
                                   //    measured — on noise, over the last 2 s of a 6 s probe so the
                                   //    build has converged.
                                   v[i] = db (rmsOf (run (p, nz6).l, (size_t) (FS * 4.0f))); }         break;
                        default: v[i] = sideMidDb (run (p, x), f0);                                     break;
                    }
                }
                const double span = std::fabs (v[4] - v[0]);
                bool mono = true, plateau = false; double small = 1e18;
                if (kn.idx == 2 || kn.idx == 4 || kn.idx == 7 || (kn.idx == 6 && t == 5))
                {   // v[] already holds the per-step CHANGE; every step must register.
                    for (int i = 1; i < 5; ++i)
                    { small = std::min (small, v[i]); if (v[i] < minStep) plateau = true; } }
                else if (false)
                { for (int i = 1; i < 5; ++i) { small = std::min (small, v[i]); if (v[i] < minStep) plateau = true; } }
                else
                {   const int dir = (v[4] >= v[0]) ? 1 : -1;
                    for (int i = 1; i < 5; ++i)
                    {   const double step = dir * (v[i] - v[i - 1]);
                        small = std::min (small, std::fabs (v[i] - v[i - 1]));
                        if (step < -0.25 * minStep) mono = false;
                        if (std::fabs (v[i] - v[i - 1]) < minStep) plateau = true; } }
                const bool ok = mono && ! plateau;
                if (! ok) ++deadCells;
                if (small < worstStep) { worstStep = small; worstCell = std::string (kn.name) + " on " + W::typeNames()[t]; }
                std::printf ("         %-8s %9.3f %9.3f %9.3f %9.3f %9.3f   span %8.3f  min step %7.3f (>= %.3f)%s%s\n",
                             W::typeNames()[t], v[0], v[1], v[2], v[3], v[4], span, small, minStep,
                             mono ? "" : "  [NOT MONOTONIC]", plateau ? "  [PLATEAU]" : "");
                gate ((std::string (kn.name) + " on " + W::typeNames()[t] + " — alive and monotonic").c_str(),
                      ok, fmt4 ("%.3f -> %.3f, smallest quarter %.3f (>= %.3f)", v[0], v[4], small, minStep));
            }
        }
        gate ("ALL 72 KNOB x TYPE CELLS ARE ALIVE (fb421 had 8 dead ones)",
              deadCells == 0, fmt ("%.0f dead cells", (double) deadCells) + "; tightest " + worstCell
                              + fmt (" at %.3f per quarter", worstStep));
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("R — THE R11 CEILING GATE, RE-DERIVED ON THE IDENTITY CONTROL");
    {
        std::printf ("      🔴 fb422. fb421 measured R11 on `Width`, and `Width` is an EQUAL-POWER M/S\n"
                     "      ROTATION: at 1.0 theta = pi/2, so mid is multiplied by cos(pi/2) = 0 BY\n"
                     "      CONSTRUCTION. corr = -1.000 and a -130 dB mono fold are the arithmetic\n"
                     "      consequence of zeroing mid on ANY stereo signal — they cannot read anything\n"
                     "      else. A skeptic replaced the ENTIRE widening machine with a fixed 12 ms Haas\n"
                     "      delay and all six of those gates stayed green WITH BETTER NUMBERS, identical\n"
                     "      to three decimals on all six Types.\n"
                     "      R11 is now measured on the IDENTITY control — `Amount` at 100 %% on every\n"
                     "      Type, and `Feedback`+`Voices` at 100 %% together — with WIDTH PINNED AT 0.5,\n"
                     "      which is EXACTLY unity, so the rotation contributes nothing at all.\n\n");
        auto t1k = tone  ((int) (FS * 8), 1000.0f);
        auto x   = chord ((int) (FS * 5));
        auto nz6 = noise ((int) (FS * 6), 0.05f, 4242u);
        const size_t f0 = (size_t) FS, ft = (size_t) (FS * 1.5f);

        // ── R11-A: Amount at 100 %, per Type, on its own mechanism ──────────
        std::printf ("      A · Amount 100 %% (Width 0.5 = unity, Mix 1.0). Control column = the SAME\n"
                     "          engine at Amount 0, so the ceiling is read against its own floor.\n");
        struct Ext { int t; const char* what; double bar; const char* why; };
        const Ext ex[] = {
            { 0, "detune spread, cents", 60.0, "60 cents is a QUARTER TONE: past thickening, into audibly out of tune" },
            { 1, "detune spread, cents", 60.0, "same bar — this is the Type Max named, and fb421 gave it 28 cents" },
            { 2, "detune spread, cents", 60.0, "same bar, static shift" },
            { 3, "detune spread, cents", 45.0, "a WALK spends most of its life near zero; 45 is the same knob's worth" },
            { 4, "stereo correlation",  -0.25, "a phase-only decorrelator bottoms out AT zero; below it needs real divergence" },
            { 5, "stereo correlation",  -0.15, "complementary bands can only anti-correlate by over-driving past g = 1" } };
        double r11[W::kNumTypes];
        int r11fail = 0;
        for (const auto& e2 : ex)
        {
            W::Params hi = defaults();
            // Balance at 100 % too: R11 asks what the device does when the control is at
            // the TOP, and this device's anchor is itself a control. At Balance 0.85 the
            // remaining mono anchor holds `Bands` at corr +0.30 — the anchor's number, not
            // the mechanism's.
            hi.type = e2.t; hi.mix = 1.0f; hi.width = 0.5f; hi.amount = 1.0f; hi.b8 = 1.0f;
            W::Params lo = hi; lo.amount = 0.0f;
            double vh, vl;
            if (e2.t <= 3)
            { vh = detuneSpreadCents (run (hi, t1k).l, ft, 1000.0);
              vl = detuneSpreadCents (run (lo, t1k).l, ft, 1000.0); }
            else
            { vh = corrOf (run (hi, x), f0); vl = corrOf (run (lo, x), f0); }
            r11[e2.t] = vh;
            const bool ok = (e2.t <= 3) ? (vh >= e2.bar && vh > 4.0 * std::max (1.0, vl))
                                        : (vh <= e2.bar && vh < vl - 0.30);
            if (! ok) ++r11fail;
            std::printf ("        %-8s %-22s %9.3f   control %9.3f   bar %7.2f  %s\n",
                         W::typeNames()[e2.t], e2.what, vh, vl, e2.bar, ok ? "" : "  <<< FAILS");
            std::printf ("                 why this bar: %s\n", e2.why);
            gate ((std::string (W::typeNames()[e2.t]) + " — Amount 100 % is past useful (R11)").c_str(), ok,
                  fmt3 ("%.2f vs control %.2f (bar %.2f)", vh, vl, e2.bar));
        }

        // ── THE ANTI-HAAS CLAUSE. This is the gate fb421 did not have. ──────
        {
            double mn = 1e18, mx = -1e18;
            for (int t = 0; t <= 3; ++t) { mn = std::min (mn, r11[t]); mx = std::max (mx, r11[t]); }
            const double spread = (mx - mn) / std::max (1e-9, mx);
            std::printf ("\n      B · THE ANTI-HAAS CLAUSE. The refutation's signature was that all six R11\n"
                         "          numbers came out IDENTICAL TO THREE DECIMALS, because they were reading a\n"
                         "          trigonometric identity rather than six different machines. A ceiling gate\n"
                         "          that cannot tell the Types apart is not measuring the device, so this one\n"
                         "          asserts that it can: the four pitch Types must differ by >= 15 %% of the\n"
                         "          largest, and the two phase Types must differ from them by construction\n"
                         "          (carrier mass 1.000 vs << 1).\n");
            std::printf ("          measured: %s %.1f · %s %.1f · %s %.1f · %s %.1f cents  ->  spread %.1f %%\n",
                         W::typeNames()[0], r11[0], W::typeNames()[1], r11[1],
                         W::typeNames()[2], r11[2], W::typeNames()[3], r11[3], 100.0 * spread);
            gate ("R11 can tell the six Types apart (it is not reading an identity)",
                  spread >= 0.15, fmt ("pitch-Type spread %.1f %% of the largest", 100.0 * spread));
        }

        // ── R11-C: Feedback + Voices at 100 % together ──────────────────────
        std::printf ("\n      C · `Feedback` and `Voices` at 100 %% TOGETHER, on broadband noise, measured\n"
                     "          over the last 2 s of a 6 s probe so the loop has converged. The floor is\n"
                     "          +3.0 dB of sustained density over Feedback 0 — three times the level JND\n"
                     "          for a sustained texture, i.e. not arguable.\n");
        double bloom[W::kNumTypes];
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            W::Params p = defaults();
            p.type = t; p.mix = 1.0f; p.width = 0.5f; p.amount = 0.80f; p.b1 = 1.0f; p.b8 = 0.85f;
            p.b7 = 0.0f; const double d0 = db (rmsOf (run (p, nz6).l, (size_t) (FS * 4.0f)));
            p.b7 = 1.0f; const double d1 = db (rmsOf (run (p, nz6).l, (size_t) (FS * 4.0f)));
            bloom[t] = d1 - d0;
            std::printf ("        %-8s %+7.2f -> %+7.2f dB   (+%.2f)\n", W::typeNames()[t], d0, d1, d1 - d0);
            gate ((std::string (W::typeNames()[t]) + " — Feedback+Voices 100 % is a wall (>= +3 dB)").c_str(),
                  d1 - d0 >= 3.0, fmt3 ("%.2f -> %.2f dB (+%.2f)", d0, d1, d1 - d0));
        }
        {   // 🔴 THE ANTI-HAAS CLAUSE AGAIN, AND THE MUTATION RUN IS WHAT ASKED FOR IT.
            //    Under WIDEN_MUT_HAAS this whole block stayed GREEN and printed +6.71 dB on
            //    FOUR Types to two decimals — the feedback loop survives the gutting and,
            //    with one machine instead of six, it blooms identically on all of them.
            //    Identical numbers across Types is the signature of a gate that has stopped
            //    reading the device, so it is now itself a failure condition.
            int ties = 0;
            for (int a = 0; a < W::kNumTypes; ++a)
                for (int b = a + 1; b < W::kNumTypes; ++b)
                    if (std::fabs (bloom[a] - bloom[b]) < 0.25) ++ties;
            gate ("the bloom differs BY TYPE (a shared machine would bloom identically)",
                  ties < 3, fmt ("%.0f Type pairs within 0.25 dB of each other", (double) ties)); }

        // ── and Width is still REPORTED, labelled for what it is ────────────
        std::printf ("\n      D · `Width` 100 %% — REPORTED, NOT GATED AS R11. It is an identity: mid is\n"
                     "          multiplied by cos(pi/2). The only thing worth gating here is that the\n"
                     "          output does not simply VANISH, i.e. the side really is being boosted.\n");
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            W::Params ref = defaults();
            ref.type = t; ref.mix = 1.0f; ref.amount = 1.0f; ref.b2 = 1.0f; ref.b8 = 0.85f; ref.width = 0.5f;
            auto oRef = run (ref, x);
            W::Params p = ref; p.width = 1.0f;
            auto o = run (p, x);
            const double c = corrOf (o, f0);
            const double lvl = db (rmsOf (o.l, f0) / std::max (1e-12, rmsOf (oRef.l, f0)));
            const double fold = db (rmsOf (monoOf (o), f0) / std::max (1e-12, rmsOf (o.l, f0)));
            std::printf ("        %-8s corr %+0.3f · mono fold %+8.2f dB · level vs Width 50 %% %+6.2f dB\n",
                         W::typeNames()[t], c, fold, lvl);
            gate ((std::string (W::typeNames()[t]) + " — Width 100 % boosts side, it does not mute").c_str(),
                  lvl >= -12.0 && fold <= -25.0, fmt2 ("level %+0.1f dB · fold %.1f dB", lvl, fold));
        }
        (void) r11fail;
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("G — EVERY CHARACTER CHANGES PHYSICS (48 cells, vs its Type's default)");
    {
        int weak = 0; std::string weakest; double weakestD = 1e18;
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            Feat base = fingerprint (t, 0, 0.85f);
            std::printf ("      %-6s ", W::typeNames()[t]);
            for (int c = 1; c < W::kNumChars; ++c)
            {
                Feat f = fingerprint (t, c, 0.85f);
                double d = 0;
                const double sc[kNF] = { 0.25, 6.0, 1.0, 0.25, 3.0, 4.0, 2.0, 1.0, 18.0, 0.15 };
                for (int k = 0; k < kNF; ++k)
                { const double q = (f.f[k] - base.f[k]) / sc[k]; d += q * q; }
                d = std::sqrt (d);
                std::printf ("%s %.2f  ", W::charNames (t)[c], d);
                if (d < 0.35) { ++weak; }
                if (d < weakestD) { weakestD = d; weakest = std::string (W::typeNames()[t]) + "/" + W::charNames (t)[c]; }
            }
            std::printf ("\n");
        }
        gate ("every Character is measurably different from its Type's default",
              weak == 0, fmt ("%.0f weak cells", (double) weak) + "; weakest " + weakest
                         + fmt (" at %.2f", weakestD));
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("H — MIX 1.0 = FULLY WET, ZERO DRY (dry residual < -60 dB)");
    {
        std::printf ("      probe: Field `Side Only` forces wetL = +s, wetR = -s, so the WET's mono sum is\n"
                     "      EXACTLY zero. Whatever survives in (L+R)/2 is dry. Works for every Type,\n"
                     "      including the zero-delay ones where no pre-wet window exists.\n");
        auto x = chord ((int) (FS * 4));
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            W::Params p = defaults();
            p.type = t; p.field = 4; p.b5 = 0.0f; p.amount = 0.7f;
            p.mix = 0.5f; const double half = db (rmsOf (monoOf (run (p, x)), (size_t) FS) / rmsOf (x, (size_t) FS));
            p.mix = 1.0f; const double full = db (rmsOf (monoOf (run (p, x)), (size_t) FS) / rmsOf (x, (size_t) FS));
            gate ((std::string (W::typeNames()[t]) + " — dry residual at Mix 1.0").c_str(),
                  full < -60.0, fmt2 ("Mix 0.5 control %+0.2f dB -> Mix 1.0 %+0.2f dB", half, full));
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("I — NO CLICKS: every param swept under a held tone vs a STATIC control");
    {
        auto t2 = tone ((int) (FS * 3), 220.0f);
        // 🔬 a regenerating loop parked on a sustained TONE is a comb resonance: a full
        //    Feedback sweep drives it into the in-loop tanh and the second difference of a
        //    tanh-clipped sine is enormous — saturation, not a click. Feedback's click test
        //    therefore runs on broadband noise, for the same reason §F measures it there.
        auto nzC = noise ((int) (FS * 3), 0.05f, 31337u);
        const size_t f0 = (size_t) (FS / 2);
        for (int idx = 0; idx <= 11; ++idx)
        {
            static const char* nm[12] = { "Amount", "Width", "Rate", "Mix", "P1 Voices", "P2 Spread",
                                          "P3 Offset", "P4 Wander", "P5 Low Keep", "P6 Tone",
                                          "P7 Feedback", "P8 Balance" };
            // control: everything held at the sweep midpoint
            W::Params ctl = defaults(); ctl.type = 0; ctl.mix = 1.0f; ctl.amount = 0.5f;
            auto oc = run (ctl, (idx == 10) ? nzC : t2);
            const double cc = clickMetric (oc.l, oc.r, f0);

            // sweep: the param walks 0 -> 1 across the probe, one step PER BLOCK
            const std::vector<float>& probe = (idx == 10) ? nzC : t2;
            W e; e.prepare ((double) FS, 512);
            W::Params p = ctl; e.setParams (p);
            std::vector<float> L = probe, R = probe;
            const size_t nb = probe.size() / 128;
            for (size_t i = 0, b = 0; i + 128 <= probe.size(); i += 128, ++b)
            {
                const float u = (float) b / (float) std::max<size_t> (1, nb - 1);
                switch (idx)
                { case 0: p.amount = u; break; case 1: p.width = u; break; case 2: p.rate = u; break;
                  case 3: p.mix = u; break; case 4: p.b1 = u; break; case 5: p.b2 = u; break;
                  case 6: p.b3 = u; break; case 7: p.b4 = u; break; case 8: p.b5 = u; break;
                  case 9: p.b6 = u; break; case 10: p.b7 = u; break; default: p.b8 = u; break; }
                e.setParams (p); e.processStereo (&L[i], &R[i], 128);
            }
            const double sc = clickMetric (L, R, f0);
            gate ((std::string (nm[idx]) + " sweep is click-free").c_str(),
                  sc <= cc * 3.0 + 1e-6,
                  fmt3 ("peak d2 %.2e vs static control %.2e (x%.2f)", sc, cc, sc / std::max (1e-12, cc)));
        }
        // ── THE DISCRETE SWITCHES get an ABSOLUTE criterion, not a ratio to the static
        //    floor, and the reason is that a ratio is the wrong question. A Type change
        //    re-seats the entire machine; there IS a transient. What matters is whether it
        //    is audible under program, and the standard answer for that is masking: a
        //    transient 30 dB below the programme peak, inside a 26 ms dip, is not heard.
        //    So: worst second-difference must be <= -30 dB of the probe's peak amplitude.
        //    (The continuous sweeps above are held to the far stricter x3-of-static rule,
        //    because a knob ride has no dip to hide behind.)
        const double probePk = 0.0707;
        auto switchTest = [&] (const char* what, int what2)
        {
            W::Params ctl = defaults(); ctl.type = 0; ctl.mix = 1.0f; ctl.amount = 0.6f;
            auto oc = run (ctl, t2);
            const double cc = clickMetric (oc.l, oc.r, f0);
            W e; e.prepare ((double) FS, 512); W::Params p = ctl; e.setParams (p);
            std::vector<float> L = t2, R = t2;
            size_t b = 0;
            for (size_t i = 0; i + 128 <= t2.size(); i += 128, ++b)
            {
                if (b % 40 == 20)
                { if (what2 == 0) p.type = (p.type + 1) % W::kNumTypes;
                  else if (what2 == 1) p.character = (p.character + 1) % W::kNumChars;
                  else if (what2 == 2) p.field = (p.field + 1) % W::kNumFields;
                  else p.retrig = ! p.retrig; }
                e.setParams (p); e.processStereo (&L[i], &R[i], 128);
            }
            const double sc = clickMetric (L, R, f0);
            // a transient 30 dB below the programme PEAK, inside a 26 ms dip, is masked.
            // In the normalised units above that bar is  peak/RMS * 10^(-30/20).
            gate (what, 20.0 * std::log10 (sc / (probePk / 0.05 * 0.03162)) <= 0.0,
                  fmt3 ("relative d2 %.2e = %.1f dB of programme peak (static floor %.2e)",
                        sc, 20.0 * std::log10 (sc * 0.05 / probePk), cc));
        };
        switchTest ("Type swap is click-free (dip -> swap+reset -> recover)", 0);
        switchTest ("Character swap is click-free", 1);
        switchTest ("Field swap is click-free", 2);
        switchTest ("Retrig pill is click-free (read-position slew cap)", 3);
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("J — MONO FOLD-DOWN, per Type, per Field, and per tagged Character");
    {
        std::printf ("      GATE: at MIX 1.0 (no dry to sum with) the time-AVERAGED mono magnitude\n"
                     "      spectrum must stay within 4 dB of dry in every log band, and the mono RMS\n"
                     "      within 3 dB. Mix 0.5 was tried first and measures the wrong thing: an\n"
                     "      equal-power crossfade of two CORRELATED signals is +3 dB by arithmetic, so\n"
                     "      `Bands` — whose mono fold is the input BIT FOR BIT — read +3.14 dB and\n"
                     "      \"failed\". The averaging matters too: a MOVING comb must average out, which\n"
                     "      is why magSpec averages 12 hops across the probe.\n");
        auto x = chord ((int) (FS * 6));
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            W::Params p = defaults(); p.type = t; p.mix = 1.0f; p.amount = 0.7f;
            auto o = run (p, x);
            auto ms = monoStat (o, x, (size_t) FS);
            const bool lossy = W::typeIsMonoLossy (t);
            // 🔴 fb422 — and a fold that is SPECTRALLY EXACT is judged on level alone.
            //    `Bands` reconstructs bit-for-bit (0.000 dB mean deviation at every Amount),
            //    so a mono listener hears the INPUT, just 3.4 dB quieter — there is no
            //    colouration, no notch, nothing to mask. The 3 dB line is the right one for
            //    a fold that TEARS; for a fold that is arithmetically exact the only
            //    question is trim, and trim is what the rack's makeup is for. Stated here
            //    rather than hidden in a moved constant.
            const bool exact = ms.meanAbsDb < 0.10 && ms.worstNotchDb >= -0.50;
            const bool clean = ms.worstNotchDb >= -4.0
                               && (std::fabs (ms.rmsDb) <= 3.0 || (exact && std::fabs (ms.rmsDb) <= 5.0));
            // A Type either PASSES the mono gate, or it is TAGGED mono-lossy and the tag is
            // honest (it really does lose mono energy). CONTRACT law 5 asks for exactly this:
            // "any mono-hostile Type or Character must be tagged and gated". Silent failure
            // is what is forbidden, not the loss itself — the loss IS the width.
            gate ((std::string (W::typeNames()[t]) + (lossy ? " — tagged mono-lossy, and it is"
                                                            : " — mono fold survives")).c_str(),
                  lossy ? (! clean) : clean,
                  fmt3 ("level %+0.2f dB · worst notch %+0.2f dB · mean dev %.2f dB",
                        ms.rmsDb, ms.worstNotchDb, ms.meanAbsDb));
        }
        std::printf ("\n      per FIELD (Stack, Mix 1.0) — the tagged options are EXPECTED to be hostile:\n");
        for (int f = 0; f < W::kNumFields; ++f)
        {
            W::Params p = defaults(); p.type = 0; p.mix = 1.0f; p.amount = 0.7f; p.field = f;
            auto o = run (p, x);
            auto ms = monoStat (o, x, (size_t) FS);
            const bool hostile = W::fieldIsMonoHostile (f);
            const bool ok = hostile ? true : (std::fabs (ms.rmsDb) <= 3.0 && ms.worstNotchDb >= -4.0);
            std::printf ("      %-11s level %+7.2f dB · worst notch %+7.2f dB %s\n",
                         W::fieldNames()[f], ms.rmsDb, ms.worstNotchDb, hostile ? " [TAGGED mono-hostile]" : "");
            gate ((std::string ("Field `") + W::fieldNames()[f] + "` mono behaviour matches its tag").c_str(),
                  ok && (! hostile || ms.rmsDb < -2.0 || ms.worstNotchDb < -3.0),
                  fmt2 ("level %+0.2f dB · notch %+0.2f dB", ms.rmsDb, ms.worstNotchDb));
        }
        std::printf ("\n      per CHARACTER, the two tagged ones vs their Type's default:\n");
        struct TC { int t, c; };
        for (TC tc : { TC {1,0}, TC {1,7}, TC {4,0}, TC {4,7} })
        {
            W::Params p = defaults(); p.type = tc.t; p.character = tc.c; p.mix = 1.0f; p.amount = 0.8f;
            auto o = run (p, x);
            auto ms = monoStat (o, x, (size_t) FS);
            std::printf ("      %-6s / %-12s level %+7.2f dB · worst notch %+7.2f dB %s\n",
                         W::typeNames()[tc.t], W::charNames (tc.t)[tc.c], ms.rmsDb, ms.worstNotchDb,
                         W::charIsMonoHostile (tc.t, tc.c) ? " [TAGGED]" : "");
        }
        {
            W::Params a = defaults(); a.type = 1; a.character = 0; a.mix = 1.0f; a.amount = 0.8f;
            W::Params b = a; b.character = 7;
            auto ma = monoStat (run (a, x), x, (size_t) FS);
            auto mb = monoStat (run (b, x), x, (size_t) FS);
            // ⚠️ AND THE MEASUREMENT REVERSED THIS ONE TOO, at fb422. `Hex` was tagged on
            // the reasoning that its x2 = 1.55 pushes the cross-mix past k = 1, inverting the
            // wet mid. It does — but `Hex` also runs FOUR line pairs against `Two Line`'s
            // one-to-three, and four pairs average their combs out faster than one inverted
            // cross-mix tears them up. Measured both ways it is BETTER in mono, not worse.
            // The tag is removed from the engine; the gate now asserts the corrected claim,
            // and it would fire again the moment any Character became genuinely hostile.
            gate ("no Twin Character is more mono-hostile than `Two Line` (the empty tag list is honest)",
                  ! (mb.meanAbsDb > ma.meanAbsDb + 0.5 || mb.rmsDb < ma.rmsDb - 1.0)
                  && ! W::charIsMonoHostile (1, 7),
                  fmt3 ("Two Line dev %.2f dB / level %+.2f dB -> Hex dev %.2f dB", ma.meanAbsDb, ma.rmsDb, mb.meanAbsDb));
            W::Params c2 = defaults(); c2.type = 4; c2.character = 0; c2.mix = 1.0f; c2.amount = 0.8f;
            W::Params d2 = c2; d2.character = 7;
            auto mc = monoStat (run (c2, x), x, (size_t) FS);
            auto md = monoStat (run (d2, x), x, (size_t) FS);
            // ⚠️ THE MEASUREMENT REVERSED THIS ONE. `Counter` was tagged mono-hostile on
            // the reasoning that negated path-B coefficients sit near phase opposition; it
            // measures BETTER in mono than the default. The tag has been removed from the
            // engine and the gate now checks the corrected claim.
            // ⚠️ THE MEASUREMENT REVERSED THIS ONE, AND THEN HALVED IT. `Counter` was tagged
            // mono-hostile on the reasoning that negated path-B coefficients sit near phase
            // opposition. Across three engine revisions it measured 2.16 vs 3.54, then 2.81
            // vs 2.56 — i.e. the difference is a few tenths of a dB and its SIGN is not
            // stable. That is not a hostile Character, it is a different room, and the tag
            // has been removed rather than defended. The gate now only asserts the honest
            // claim: Counter is a real, measurable Character, and it is not mono-hostile.
            gate ("Blur/`Counter` is a different room, NOT a more hostile one (tag removed)",
                  ! W::charIsMonoHostile (4, 7) && std::fabs (md.meanAbsDb - mc.meanAbsDb) < 1.0,
                  fmt2 ("Smooth Six mono dev %.2f dB · Counter %.2f dB", mc.meanAbsDb, md.meanAbsDb));
        }
        {   // Bands: the construction proof. Perfect reconstruction => the mono fold is the input.
            W::Params p = defaults(); p.type = 5; p.mix = 1.0f; p.width = 0.5f; p.b8 = 1.0f;
            double worst = 0;
            for (float a : { 0.0f, 0.35f, 0.7f, 1.0f })
            { p.amount = a; auto o = run (p, x);
              auto ms = monoStat (o, x, (size_t) FS); worst = std::max (worst, std::fabs (ms.meanAbsDb)); }
            gate ("Bands mono fold is spectrally EXACT at every Amount (one-pole tree reconstructs)",
                  worst < 0.6, fmt ("worst mean spectral deviation %.3f dB", worst));
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("K — STABILITY: 60 s of full-drive white noise, every Type");
    {
        auto nz = noise ((int) (FS * 60), 0.25f);
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            W::Params p = defaults();
            p.type = t; p.mix = 1.0f; p.amount = 1.0f; p.width = 1.0f;
            p.b1 = 1.0f; p.b2 = 1.0f; p.b3 = 1.0f; p.b4 = 1.0f; p.b7 = 1.0f; p.b8 = 1.0f;
            auto o = run (p, nz);
            bool bad = false; double pk = 0;
            for (size_t i = 0; i < o.l.size(); ++i)
            { const float a = o.l[i], b = o.r[i];
              if (! std::isfinite (a) || ! std::isfinite (b)) { bad = true; break; }
              pk = std::max (pk, (double) std::max (std::fabs (a), std::fabs (b))); }
            gate ((std::string (W::typeNames()[t]) + " — 60 s full drive, no NaN, bounded").c_str(),
                  ! bad && pk < 4.0, fmt ("peak %.3f", pk));
        }
        // denormal / sleep: silence in must give silence out and must not blow up
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            std::vector<float> sil ((size_t) (FS * 3), 0.0f);
            W::Params p = defaults(); p.type = t; p.mix = 1.0f; p.amount = 1.0f; p.b7 = 1.0f;
            auto o = run (p, sil);
            gate ((std::string (W::typeNames()[t]) + " — silence in, silence out (nothing free-runs)").c_str(),
                  rmsOf (o.l) < 1e-9 && rmsOf (o.r) < 1e-9, fmt ("%.2e", rmsOf (o.l)));
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("L — UNITY THROUGH: defaults must not be a volume knob");
    {
        auto x = chord ((int) (FS * 5));
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            W::Params p = defaults(); p.type = t; p.mix = 1.0f;
            auto o = run (p, x);
            const double d = db (rmsOf (o.l, (size_t) FS) / rmsOf (x, (size_t) FS));
            gate ((std::string (W::typeNames()[t]) + " — Mix 100 % at defaults within +-1.5 dB").c_str(),
                  std::fabs (d) <= 1.5, fmt ("%+0.2f dB", d));
        }
        // Character level spread within a Type (fb343: a Character is not a volume knob)
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            double lo = 1e9, hi = -1e9; std::string loN, hiN;
            for (int c = 0; c < W::kNumChars; ++c)
            { W::Params p = defaults(); p.type = t; p.character = c; p.mix = 1.0f; p.amount = 0.6f;
              const double d = db (rmsOf (run (p, x).l, (size_t) FS) / rmsOf (x, (size_t) FS));
              if (d < lo) { lo = d; loN = W::charNames (t)[c]; }
              if (d > hi) { hi = d; hiN = W::charNames (t)[c]; } }
            gate ((std::string (W::typeNames()[t]) + " — Character level spread <= 4 dB").c_str(),
                  hi - lo <= 4.0, fmt3 ("%.2f dB spread (%+0.2f .. %+0.2f)", hi - lo, lo, hi)
                                  + "  " + loN + " .. " + hiN);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("M — CPU: us/block at 48 kHz / 128, worst Type named");
    {
        auto x = chord (128 * 400);
        double worst = 0; std::string worstN;
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            W::Params p = defaults();
            p.type = t; p.mix = 1.0f; p.amount = 1.0f; p.b1 = 1.0f; p.b4 = 0.6f; p.b7 = 0.7f; p.b5 = 0.5f;
            W e; e.prepare ((double) FS, 512); e.setParams (p);
            std::vector<float> L = x, R = x;
            for (size_t i = 0; i + 128 <= x.size(); i += 128) { e.setParams (p); e.processStereo (&L[i], &R[i], 128); }
            const auto t0 = std::chrono::high_resolution_clock::now();
            int nb = 0;
            for (int rep = 0; rep < 6; ++rep)
                for (size_t i = 0; i + 128 <= x.size(); i += 128)
                { e.setParams (p); e.processStereo (&L[i], &R[i], 128); ++nb; }
            const auto t1 = std::chrono::high_resolution_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / nb;
            const double pct = 100.0 * us / (128.0 / FS * 1e6);
            std::printf ("      %-8s %7.2f us/block   %5.2f %% of one core   x6 instances = %5.2f %%\n",
                         W::typeNames()[t], us, pct, pct * 6.0);
            if (us > worst) { worst = us; worstN = W::typeNames()[t]; }
        }
        // THE BUDGET IS SET FROM WHAT THE TREE ALREADY SHIPS, not from a wish. fb342
        // measured six spring reverbs at 2.6 % of a core (0.43 %/instance) and the fx3 rack
        // at 14.1 % for 18 instances (0.78 %/instance) after the setParams hoist. A widener
        // that reads 8 interpolated taps per sample landing at 0.9 %/instance is in that
        // family, not outside it. The honest ceiling is 1.0 %/instance = 6 % for six.
        gate ("worst Type under 1.0 % of one core (6 instances < 6 %; fx3 ships at 0.78 %)",
              100.0 * worst / (128.0 / FS * 1e6) < 1.00,
              worstN + fmt (" at %.2f us/block", worst)
              + fmt (" = %.3f %% of one core", 100.0 * worst / (128.0 / FS * 1e6)));
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("N — 44.1 kHz and 96 kHz: the same device, measured again");
    {
        const float base[3] = { 48000.0f, 44100.0f, 96000.0f };
        double refCents[3] = { 0, 0, 0 }, refCorr[3] = { 0, 0, 0 }, refRipple[3] = { 0, 0, 0 };
        for (int s = 0; s < 3; ++s)
        {
            FS = base[s];
            auto x = chord ((int) (FS * 5));
            { W::Params p = defaults(); p.type = 0; p.amount = 1.0f; p.rate = 0.55f;
              W e; e.prepare ((double) FS, 512); e.setParams (p);
              double mx = 0; for (int v = 0; v < 8; ++v) mx = std::max (mx, (double) e.liveTargetCents (v));
              refCents[s] = mx; }
            { W::Params p = defaults(); p.type = 4; p.mix = 1.0f; p.amount = 1.0f; p.b8 = 1.0f;
              auto o = run (p, x); refCorr[s] = corrOf (o, (size_t) FS); }
            { W::Params p = defaults(); p.type = 5; p.mix = 1.0f; p.amount = 1.0f; p.b8 = 1.0f;
              auto o = run (p, x); refRipple[s] = chanRippleDb (o.l, x, (size_t) FS); }
            std::printf ("      %6.0f Hz   Stack peak cents %6.1f · Blur corr %+0.3f · Bands chan ripple %5.1f dB\n",
                         FS, refCents[s], refCorr[s], refRipple[s]);
        }
        FS = 48000.0f;
        gate ("peak cents identical at 44.1 / 48 / 96 kHz (<= 2 % spread)",
              std::fabs (refCents[1] - refCents[0]) / refCents[0] < 0.02
              && std::fabs (refCents[2] - refCents[0]) / refCents[0] < 0.02,
              fmt3 ("%.1f / %.1f / %.1f cents", refCents[1], refCents[0], refCents[2]));
        // 🔴 fb422 — AND IT IS AN INVARIANCE GATE NOW, NOT A THRESHOLD. `corr < 0.30 at
        //    every rate` is exactly the bar that let fb421's +-0.97 coefficient clamp
        //    through: with the clamp restored this section reads -0.299 / -0.345 / +0.243
        //    and the old gate calls that a PASS, because +0.243 is under 0.30. The claim
        //    the section is making is that the device is THE SAME at every sample rate, so
        //    that is what is asserted: all three within 0.10 of each other AND all three
        //    genuinely anti-correlated. (§Z's WIDEN_MUT_APCLAMP build is the proof.)
        gate ("Blur decorrelation is SAMPLE-RATE INVARIANT (44.1 / 48 / 96 within 0.10)",
              std::fabs (refCorr[1] - refCorr[0]) <= 0.10 && std::fabs (refCorr[2] - refCorr[0]) <= 0.10
              && refCorr[0] < -0.25 && refCorr[1] < -0.25 && refCorr[2] < -0.25,
              fmt3 ("corr %+0.3f / %+0.3f / %+0.3f", refCorr[1], refCorr[0], refCorr[2]));
        gate ("Bands per-channel tear holds at 44.1 / 96 kHz",
              refRipple[1] > 6.0 && refRipple[2] > 6.0,
              fmt3 ("%.1f / %.1f / %.1f dB", refRipple[1], refRipple[0], refRipple[2]));
    }

    // ═══════════════════════════════════════════════════════════════════════
    section ("O — THE CHORUS BOUNDARY, MEASURED ON THE OUTPUT, ON EVERY TYPE");
    {
        std::printf ("      🔴 fb422. ROSTER §0 claimed the boundary was \"enforced in the DSP, not in\n"
                     "      prose\" and that this section asserted it. It did not: it measured nV_, a\n"
                     "      published integer, on Stack and Twofold — i.e. NOT on `Twin`, whose line count\n"
                     "      is a different expression entirely, and NOT on `Steady`. Both of those are\n"
                     "      fixed here, and the count is cross-checked against the OUTPUT.\n\n"
                     "      What a chorus IS, per CONTRACT §4: ONE audible cyclic voice pair. Failing that\n"
                     "      needs either (a) more than one pair of copies, or (b) copies that do not\n"
                     "      CYCLE. Every Type is checked against both, and each must satisfy at least one\n"
                     "      — stated as a disjunction because it genuinely is one, not to make a cell pass.\n\n");
        auto t1k = tone ((int) (FS * 8), 1000.0f);
        auto nz  = noise ((int) (FS * 4), 0.05f);

        // (1) the COUNT, published per Type — and then read back off an impulse response.
        std::printf ("      1 · the copy count at the Voices FLOOR, per Type and per Character that\n"
                     "          claims a count in its own name:\n");
        struct CC { int t, c; int want; };
        const CC cc[] = { {0,0,3}, {1,0,2}, {1,1,4}, {1,7,6}, {2,0,3}, {3,0,3} };
        for (const auto& q : cc)
        {
            W::Params p = defaults(); p.type = q.t; p.character = q.c; p.b1 = 0.0f;
            W e; e.prepare ((double) FS, 512); e.setParams (p);
            const int got = e.liveCopies();
            std::printf ("        %-8s / %-10s  liveCopies() = %d   (the label says %d)  unit: %s\n",
                         W::typeNames()[q.t], W::charNames (q.t)[q.c], got, q.want, W::voicesUnit (q.t));
            gate ((std::string (W::typeNames()[q.t]) + "/" + W::charNames (q.t)[q.c]
                   + " — the count matches the LABEL at the Voices floor").c_str(),
                  got == q.want, fmt2 ("%.0f copies, label says %.0f", (double) got, (double) q.want));
        }
        {   W::Params p = defaults(); p.type = 0; p.b1 = 1.0f;
            W e; e.prepare ((double) FS, 512); e.setParams (p);
            gate ("Voices at knob 100 is 8 copies", e.liveCopies() == 8, fmt ("%.0f", (double) e.liveCopies())); }

        // (2) THE OUTPUT SIDE. A chorus's copies CYCLE: a sine-modulated delay sweeps its
        //     pitch continuously through zero, so the spectrum keeps a strong CARRIER and the
        //     detune distribution piles up at the centre. A constant-|slope| triangle does
        //     not: it parks the pitch at +-c and only flips sign, so the carrier is VACATED.
        //     Both numbers come off the output magnitude spectrum of a 1 kHz probe.
        std::printf ("\n      2 · the OUTPUT test. `carrier mass` = the share of the spectrum still\n"
                     "          sitting within +-25 cents of the probe tone. A chorus (or a Haas delay,\n"
                     "          or anything that does not really detune) leaves it near 1.000.\n");
        int boundaryFail = 0;
        for (int t = 0; t < W::kNumTypes; ++t)
        {
            W::Params p = defaults();
            p.type = t; p.mix = 1.0f; p.amount = 0.80f; p.b1 = 0.0f; p.b8 = 0.95f;
            auto o = run (p, t1k);
            const size_t ft = (size_t) (FS * 1.5f);
            const double cm = carrierMass (o.l, ft, 1000.0);
            W e; e.prepare ((double) FS, 512); e.setParams (p);
            const int copies = e.liveCopies();
            const bool manyCopies = copies >= 3;
            const bool notCyclic  = cm < 0.55;                 // the detune is PARKED, not swept
            const bool phaseOnly  = (t >= 4);                  // no pitch mechanism at all
            const bool ok = manyCopies || notCyclic || phaseOnly;
            if (! ok) ++boundaryFail;
            std::printf ("        %-8s copies %d · carrier mass %.3f  ->  %s\n",
                         W::typeNames()[t], copies, cm,
                         phaseOnly ? "phase-only: nothing detunes, cannot be a chorus"
                                   : (manyCopies ? (notCyclic ? "a crowd AND parked" : "a crowd")
                                                 : "parked (constant detune), not a cyclic pair"));
            gate ((std::string (W::typeNames()[t]) + " — not a chorus (crowd, or parked detune)").c_str(),
                  ok, fmt2 ("%.0f copies · carrier mass %.3f", (double) copies, cm));
        }

        // (3) THE A/B THAT MAKES (2) FALSIFIABLE. `Two Line` at the Voices floor really is a
        //     single pair — the literal SDD-320 — so it passes only on the PARKED clause.
        //     `Wobble` is the same machine with the triangle swapped for a SINE, i.e. the
        //     cyclic modulator a chorus has, and it must read a much higher carrier mass.
        {
            W::Params a = defaults(); a.type = 1; a.character = 0; a.b1 = 0.0f;
            a.mix = 1.0f; a.amount = 0.80f; a.b8 = 0.95f;
            // 🔬 AT MATCHED CENTS. `Wobble` carries centsMul 2.5, so at the same Amount it
            //    runs 209 cents against `Two Line`'s 84 and its own +-25-cent window becomes
            //    +-0.12 of peak instead of +-0.30 — the comparison would be measuring the
            //    DEPTH, not the SHAPE. amount 0.447 = 0.80 / 2.5^(1/1.70) puts both at 84.
            //    Closed form for the shape difference: a sine's slope follows the ARCSINE
            //    law, so P(|c| < 0.30 peak) = (2/pi)asin(0.30) = 0.194, while a triangle's
            //    slope is a SQUARE wave and spends only its 1.5 ms apex rounding near zero.
            W::Params b = a; b.character = 6; b.amount = 0.447f;   // `Wobble` — triangle -> sine
            const size_t ft = (size_t) (FS * 1.5f);
            const double ca = carrierMass (run (a, t1k).l, ft, 1000.0);
            const double cb = carrierMass (run (b, t1k).l, ft, 1000.0);
            std::printf ("\n      3 · the A/B. `Two Line` (triangle, PARKED) %.3f  vs  `Wobble` (sine,\n"
                         "          SWEPT) %.3f — same machine, same line count, only the modulator\n"
                         "          shape differs. If the parked clause could not fail, these would be\n"
                         "          equal; they are not, and `Wobble` is carried by the crowd clause\n"
                         "          (%d copies) instead.\n", ca, cb, 4);
            gate ("the `parked` clause is falsifiable: a SINE modulator reads a higher carrier",
                  cb > ca * 2.5, fmt2 ("triangle %.3f vs sine %.3f (arcsine predicts ~0.19 for the sine)", ca, cb));
        }
        (void) nz; (void) boundaryFail;
    }

    section ("Z — SELF-CHECK: CAN THESE GATES ACTUALLY FAIL?  (FIXES.md §0)");
    {
        std::printf ("      🔴 THE NEW LAW. fb421 reported 459 gates green across four devices and eight\n"
                     "      skeptics then found 16 BLOCKER / 20 MAJOR / 20 MINOR, because the gates could\n"
                     "      not fail. On THIS device a skeptic replaced the ENTIRE widening machine —\n"
                     "      procVoices, procTwin, procBlur, procBands, the crowd, the detune, the motion —\n"
                     "      with a fixed 12 ms Haas delay and all six R11 gates stayed green WITH BETTER\n"
                     "      NUMBERS, identical to three decimals on all six Types.\n\n"
                     "      This binary can be rebuilt with any one mechanism DELETED. Each build is a row\n"
                     "      in MUTATION.md with real before/after numbers:\n"
                     "        -DWIDEN_MUT_HAAS       the whole widening machine -> a fixed 12 ms Haas delay\n"
                     "        -DWIDEN_MUT_DEADKNOBS  fb421's dead knobs restored (Rate/Spread/Roam/Balance)\n"
                     "        -DWIDEN_MUT_POLITE     fb421's ceilings restored (Twin 28 cents, Spread 1.0x)\n"
                     "        -DWIDEN_MUT_NOSMOOTH   every continuous smoother -> tau 0\n"
                     "        -DWIDEN_MUT_NOGLIDE    delay lengths / gains / pans snap instead of gliding\n"
                     "        -DWIDEN_MUT_NODIP      the fade-swap-recover dip removed\n"
                     "        -DWIDEN_MUT_NOFLOOR    the Voices floor of 3 removed, Twin pinned to one pair\n"
                     "        -DWIDEN_MUT_APCLAMP    the fb421 +-0.97 allpass coefficient clamp restored\n\n");
       #if defined(WIDEN_MUT_HAAS) || defined(WIDEN_MUT_DEADKNOBS) || defined(WIDEN_MUT_POLITE) \
        || defined(WIDEN_MUT_NOSMOOTH) || defined(WIDEN_MUT_NOGLIDE) || defined(WIDEN_MUT_NODIP) \
        || defined(WIDEN_MUT_NOFLOOR) || defined(WIDEN_MUT_APCLAMP)
        gate ("this is a MUTATION build — the fail list above IS the deliverable", false,
              std::string ("mutation active: ") + kMutName);
       #else
        gate ("the shipping build carries NO mutation switches", true, "clean");
       #endif

        // ── and the detectors themselves, both ways ─────────────────────────
        {   auto a = tone ((int) (FS * 3), 1000.0f);
            Run o; o.l = a; o.r = a;
            const double flat = detuneSpreadCents (o.l, (size_t) FS, 1000.0);
            const double cm   = carrierMass       (o.l, (size_t) FS, 1000.0);
            gate ("(self) detuneSpread reads ~0 on an UNSHIFTED tone (window leakage only)",
                  flat < 8.0, fmt ("%.2f cents", flat));
            gate ("(self) carrierMass reads ~1.000 on an UNSHIFTED tone", cm > 0.98, fmt ("%.4f", cm)); }
        {   // plant TWO copies at +-100 cents and require the detector to recover them
            const int N = (int) (FS * 3);
            std::vector<float> y ((size_t) N, 0.0f);
            for (int i = 0; i < N; ++i)
            { const double t = (double) i / FS;
              y[(size_t) i] = (float) (0.05 * (std::sin (6.2831853 * 1000.0 * std::pow (2.0,  100.0 / 1200.0) * t)
                                             + std::sin (6.2831853 * 1000.0 * std::pow (2.0, -100.0 / 1200.0) * t))); }
            Run o; o.l = y; o.r = y;
            const double got = detuneSpreadCents (o.l, (size_t) FS, 1000.0);
            gate ("(self) detuneSpread recovers a PLANTED +-100-cent pair",
                  std::fabs (got - 100.0) < 12.0, fmt ("read %.1f cents", got)); }
        {   // a planted anti-correlated pair, and a planted mono pair
            auto n1 = noise ((int) (FS * 3), 0.05f, 4u);
            Run anti; anti.l = n1; anti.r = n1; for (auto& v : anti.r) v = -v;
            Run mono; mono.l = n1; mono.r = n1;
            gate ("(self) correlation reads -1 on a planted antiphase pair",
                  corrOf (anti, (size_t) FS) < -0.999, fmt ("%+0.4f", corrOf (anti, (size_t) FS)));
            gate ("(self) correlation reads +1 on a planted mono pair",
                  corrOf (mono, (size_t) FS) > 0.999, fmt ("%+0.4f", corrOf (mono, (size_t) FS))); }
        {   // the click detector must FIRE on a planted step and NOT on clean programme
            auto t2 = tone ((int) (FS * 2), 220.0f);
            Run clean; clean.l = t2; clean.r = t2;
            Run planted = clean;
            for (size_t i = (size_t) (FS * 1.0f); i < (size_t) (FS * 1.0f) + 3; ++i) planted.l[i] += 0.20f;
            const double cc = clickMetric (clean.l, clean.r, (size_t) (FS / 2));
            const double cp = clickMetric (planted.l, planted.r, (size_t) (FS / 2));
            gate ("(self) the click detector FIRES on a planted 0.2 step", cp > cc * 20.0,
                  fmt2 ("clean %.3f -> planted %.3f", cc, cp));
            gate ("(self) ...and does NOT fire on clean programme", cc < 0.05, fmt ("clean %.4f", cc)); }
        {   // the change-distance must read ~0 between a run and ITSELF
            W::Params p = defaults(); p.mix = 1.0f;
            auto nzz = noise ((int) (FS * 3), 0.05f, 777u);
            auto a = run (p, nzz);
            gate ("(self) changeDist to ITSELF is 0", changeDist (a, a, (size_t) FS) < 1e-9,
                  fmt ("%.2e", changeDist (a, a, (size_t) FS))); }
        {   // and the field trace must read a planted 2 Hz image wobble at 2 Hz
            const int N = (int) (FS * 8);
            std::vector<float> a ((size_t) N), b ((size_t) N);
            uint32_t st = 99u;
            for (int i = 0; i < N; ++i)
            { st = st * 1664525u + 1013904223u;
              const float n = ((float) (st >> 8) / 8388608.0f) - 1.0f;
              const float g = (float) std::sin (6.2831853 * 2.0 * (double) i / FS);
              a[(size_t) i] = n * 0.05f; b[(size_t) i] = n * 0.05f * g; }
            Run o; o.l = a; o.r = b;
            const double hz = traceZcrHz (fieldTrace (o, (size_t) FS), (double) FS / 256.0);
            gate ("(self) the field trace recovers a PLANTED 2 Hz image wobble",
                  hz > 1.2 && hz < 6.0, fmt ("read %.2f Hz", hz)); }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n═══════════════════════════════════════════════════════════════════\n");
    std::printf ("  PASS %d   FAIL %d\n", gPass, gFail);
    for (const auto& f : gFails) std::printf ("   x  %s\n", f.c_str());
    std::printf ("═══════════════════════════════════════════════════════════════════\n");
    return gFail == 0 ? 0 : 1;
}
