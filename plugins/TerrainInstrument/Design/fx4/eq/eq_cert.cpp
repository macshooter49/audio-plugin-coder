// ─────────────────────────────────────────────────────────────────────────────
// eq_cert — the perceptual certification harness for the FX EQUALIZER (fb420, kind 9).
//
//   clang++ -O2 -std=c++17 -I <TI>/Tests/shim -I <TI>/Source -I <this dir> \
//           eq_cert.cpp -o /tmp/eq_cert && /tmp/eq_cert
//
// ⚠️ THE LAW THIS HARNESS CANNOT ENFORCE (fb373). A green DSP harness proves the ENGINE
// works. It NEVER proves the plugin REACHES it. Selecting `Cassette` silently gave you
// `Studio` through four rounds of green measurement because a choice param was normalised
// on the DROPDOWN's option count instead of the PARAM's cardinality. This device declares
// kNumTypes = 7 and kNumTypeSlots = 12; §A gates the engine-side mapping (name <-> index
// <-> physics) but the UI -> APVTS -> DSP round trip is the integration owner's gate.
//
// ⚠️ SAMPLE-DIFFERENCE RMS IS BANNED (fb282/fb283). Everything below is measured on the
// OUTPUT MAGNITUDE SPECTRUM of real audio pushed through the real processStereo in real
// 128-sample blocks with setParams called per block. Nothing is read off the coefficient
// geometry except §B, which is explicitly a math gate and says so.
//
// ── PROBE CRAFT, learned in this file (each one started as a wrong number) ──
//  * 📐🚫👂 GEOMETRY IS NOT HEARING (fb417). An EQ is the one device where the geometry
//    IS the sound - but only if the geometry you measure is the OUTPUT spectrum. Every
//    headline number below is |Y(f)|/|X(f)| of white noise through the block path, and the
//    "obvious control" number (a plain +12 dB Low boost) is printed beside the extreme ones
//    so the scale is legible.
//  * 🔬 CHECK YOUR OWN DETECTOR. §B5 runs the identical metric through a BYPASSED engine
//    and prints it. §B6 gates the engine's own 96-bin viz curve against the independently
//    measured noise transfer - if the display and the audio ever disagreed, every other
//    number in this file that leans on the curve would be a lie.
//  * 🪤 A GATE THAT HAS NEVER FAILED HAS NEVER BEEN TESTED (fb393). Three negative controls
//    run here and are REQUIRED to fail: the RBJ design against the decramp gate (§B1),
//    an instantaneous param jump against the click gate (§J), and a zero-gain patch against
//    the ceiling gate (§K).
//  * 🌾 AN OUTLIER DETECTOR IS BLIND TO A CONTINUOUS FAULT (fb416). The click gate is a
//    per-64-sample-frame level-jump detector, not an outlier hunt: a zipper that lasts a
//    whole sweep lives in the BULK of the distribution, not its tail.
//  * A high-Q resonance is NARROWER THAN THE FFT BIN if you are careless. 16384-point
//    windows at 2.93 Hz, and every gate that leans on a Q > 20 peak says so.
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainEqualizerFx.h"
#include "shipped_labels.inc"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <complex>
#include <chrono>
#include <algorithm>

namespace {

using EQ = tw::TerrainEqualizerFx;
float FS = 48000.0f;
int gPass = 0, gFail = 0;
std::vector<std::string> gFails;

void section (const char* s) { std::printf ("\n[%s]\n", s); }
void gate (const char* what, bool ok, const std::string& d)
{
    if (ok) { ++gPass; std::printf ("  ok    %-58s %s\n", what, d.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-58s %s\n", what, d.c_str());
              gFails.push_back (std::string (what) + "  [" + d + "]"); }
}
void note (const std::string& s) { std::printf ("        %s\n", s.c_str()); }
std::string fmt (const char* f, double v) { char b[160]; std::snprintf (b, sizeof b, f, v); return b; }
std::string fmt2 (const char* f, double a, double b) { char x[220]; std::snprintf (x, sizeof x, f, a, b); return x; }
std::string fmt3 (const char* f, double a, double b, double c) { char x[260]; std::snprintf (x, sizeof x, f, a, b, c); return x; }

// ── probes, normalised to the measured -26 dBFS bus (0.05 linear RMS) ────────
std::vector<float> whiteN (int n, float rms = 0.05f, uint32_t seed = 12345u)
{
    std::vector<float> x ((size_t) n); uint32_t st = seed;
    for (int i = 0; i < n; ++i)
    { st = st * 1664525u + 1013904223u; x[(size_t) i] = ((float) (st >> 8) / 8388608.0f) - 1.0f; }
    double a = 0; for (float v : x) a += (double) v * v;
    const float g = rms / (float) std::sqrt (a / (double) n);
    for (float& v : x) v *= g;
    return x;
}
// 🔬 fb422 — CHECK YOUR OWN DETECTOR (§3.1), the third time in this file.
//  This used to be `std::sin (6.2831853f * hz * (float) i / FS)`. At hz = 5231 and
//  i = 191 000 the product is 6.3e9, where a float ULP is 512 — a phase quantisation of
//  0.0107 rad, i.e. a BROADBAND JITTER FLOOR at about -49 dB that GROWS with i. A Q 36
//  16 kHz shelf amplifies that to HALF THE SIGNAL. The first build of the §J click probe
//  read 24.6 dB/sample on a HELD knob and I very nearly filed it as an engine instability;
//  a 30 s run with a double-precision tone settles at a constant 0.6425 and decays to TRUE
//  ZERO in silence. The phase now accumulates in double and wraps.
std::vector<float> toneN (int n, float hz, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n);
    const double w = 6.283185307179586 * (double) hz / (double) FS;
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    { x[(size_t) i] = (float) std::sin (ph); ph += w; if (ph > 6.283185307179586) ph -= 6.283185307179586; }
    double a = 0; for (float v : x) a += (double) v * v;
    const float g = rms / (float) std::sqrt (a / (double) n);
    for (float& v : x) v *= g;
    return x;
}
std::vector<float> chordN (int n, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n, 0.0f);
    const float f[4] = { 110.0f, 130.81f, 164.81f, 220.0f };
    for (int i = 0; i < n; ++i)
    { float s = 0;
      for (float fr : f) for (int h = 1; h <= 14; ++h)
          s += std::sin (6.2831853f * fr * (float) h * (float) i / FS) / (float) h;
      x[(size_t) i] = s; }
    double a = 0; for (float v : x) a += (double) v * v;
    const float g = rms / (float) std::sqrt (a / (double) n);
    for (float& v : x) v *= g;
    return x;
}

// ── FFT ──────────────────────────────────────────────────────────────────────
void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
      if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    { const double ang = -2.0 * 3.14159265358979323846 / (double) len;
      const std::complex<double> wl (std::cos (ang), std::sin (ang));
      for (size_t i = 0; i < n; i += len)
      { std::complex<double> w (1.0, 0.0);
        for (size_t k = 0; k < len / 2; ++k)
        { const std::complex<double> u = a[i+k], v = a[i+k+len/2] * w;
          a[i+k] = u + v; a[i+k+len/2] = u - v; w *= wl; } } }
}

// ═════════════════════════════════════════════════════════════════════════════
//  THE MEASUREMENT: transfer magnitude in dB on the same 96 log bins the card draws.
//  White noise -> real processStereo in 128-sample blocks with setParams per block ->
//  Welch-averaged |Y|^2/|X|^2. This is an OUTPUT SPECTRUM measurement, not geometry.
// ═════════════════════════════════════════════════════════════════════════════
constexpr int NB = EQ::kCurveBins;
struct Spec { double db[NB]; };

Spec transferOf (const EQ::Params& p, float probeRms = 0.05f, int chan = 0, uint32_t seed = 12345u)
{
    const int NF = 16384, HOP = 8192, SEG = 4;
    const int settle = 8192;
    const int N = settle + NF + (SEG - 1) * HOP;
    auto x = whiteN (N, probeRms, seed);
    std::vector<float> L (x.begin(), x.end()), R (x.begin(), x.end());
    EQ e; e.prepare ((double) FS, 128);
    for (int i = 0; i < N; i += 128)
    { const int nn = std::min (128, N - i); e.setParams (p); e.processStereo (&L[(size_t) i], &R[(size_t) i], nn); }
    const std::vector<float>& Y = (chan == 0 ? L : R);

    std::vector<double> sxx ((size_t) NF / 2 + 1, 0.0), syy ((size_t) NF / 2 + 1, 0.0);
    std::vector<std::complex<double>> A ((size_t) NF), B ((size_t) NF);
    for (int s = 0; s < SEG; ++s)
    {
        const int off = settle + s * HOP;
        for (int i = 0; i < NF; ++i)
        { const double w = 0.5 - 0.5 * std::cos (6.283185307179586 * i / (NF - 1));
          A[(size_t) i] = x[(size_t) (off + i)] * w; B[(size_t) i] = Y[(size_t) (off + i)] * w; }
        fft (A); fft (B);
        for (int k = 0; k <= NF / 2; ++k)
        { sxx[(size_t) k] += std::norm (A[(size_t) k]); syy[(size_t) k] += std::norm (B[(size_t) k]); }
    }
    Spec out;
    for (int i = 0; i < NB; ++i)
    {
        const double fc = EQ::curveBinHz (i);
        const double r  = std::pow (10.0, 1.5 / 95.0);            // half a band, log
        double flo = fc / r, fhi = fc * r;
        int klo = (int) std::floor (flo * NF / FS), khi = (int) std::ceil (fhi * NF / FS);
        klo = std::max (1, klo); khi = std::min (NF / 2, std::max (khi, klo));
        double nx = 0, ny = 0;
        for (int k = klo; k <= khi; ++k) { nx += sxx[(size_t) k]; ny += syy[(size_t) k]; }
        out.db[i] = 10.0 * std::log10 (std::max (1e-300, ny) / std::max (1e-300, nx));
    }
    return out;
}

double specMax (const Spec& a) { double m = -1e9; for (int i = 0; i < NB; ++i) m = std::max (m, a.db[i]); return m; }
double specMin (const Spec& a) { double m =  1e9; for (int i = 0; i < NB; ++i) m = std::min (m, a.db[i]); return m; }
double specMsd (const Spec& a) { double m = 0;   for (int i = 0; i < NB; ++i) m = std::max (m, std::fabs (a.db[i])); return m; }
double specDelta (const Spec& a, const Spec& b)
{ double m = 0; for (int i = 0; i < NB; ++i) m = std::max (m, std::fabs (a.db[i] - b.db[i])); return m; }
double specRms (const Spec& a, const Spec& b)
{ double s = 0; for (int i = 0; i < NB; ++i) { const double d = a.db[i] - b.db[i]; s += d * d; } return std::sqrt (s / NB); }
// rms difference over the bins where BOTH curves are above -40 dB. Below that a noise
// transfer estimate is measuring its own leakage, not the device.
double specMedian (const Spec& a, const Spec& b)
{ std::vector<double> v; v.reserve (NB);
  for (int i = 0; i < NB; ++i) v.push_back (std::fabs (a.db[i] - b.db[i]));
  std::sort (v.begin(), v.end()); return v[v.size() / 2]; }
[[maybe_unused]] double specRmsLive (const Spec& a, const Spec& b)
{ double s = 0; int n = 0;
  for (int i = 0; i < NB; ++i) { if (a.db[i] < -40.0 || b.db[i] < -40.0) continue;
    const double d = a.db[i] - b.db[i]; s += d * d; ++n; }
  return n ? std::sqrt (s / n) : 0.0; }
double atHz (const Spec& a, double hz)
{ int best = 0; double bd = 1e18;
  for (int i = 0; i < NB; ++i) { const double d = std::fabs (std::log (EQ::curveBinHz (i)) - std::log (hz)); if (d < bd) { bd = d; best = i; } }
  return a.db[best]; }
double minInRange (const Spec& a, double lo, double hi)
{ double m = 1e9; for (int i = 0; i < NB; ++i) { const double f = EQ::curveBinHz (i); if (f >= lo && f <= hi) m = std::min (m, a.db[i]); } return m; }
[[maybe_unused]] double maxInRange (const Spec& a, double lo, double hi)
{ double m = -1e9; for (int i = 0; i < NB; ++i) { const double f = EQ::curveBinHz (i); if (f >= lo && f <= hi) m = std::max (m, a.db[i]); } return m; }
// the frequency where the curve crosses a level, scanned from the top. This is what a
// FREQUENCY knob actually moves; reading one fixed probe frequency instead makes the ends
// of the sweep look like plateaus when the corner has simply walked out of the probe.
double crossHz (const Spec& a, double level)
{ for (int i = NB - 1; i > 0; --i) if (a.db[i] >= level) return EQ::curveBinHz (i);
  return EQ::curveBinHz (0); }
double meanInRange (const Spec& a, double lo, double hi)
{ double s = 0; int n = 0;
  for (int i = 0; i < NB; ++i) { const double f = EQ::curveBinHz (i); if (f >= lo && f <= hi) { s += a.db[i]; ++n; } }
  return n ? s / n : 0.0; }
double argMaxHz (const Spec& a)
{ int b = 0; for (int i = 0; i < NB; ++i) if (a.db[i] > a.db[b]) b = i; return EQ::curveBinHz (b); }

// ── param builders. Every knob is 0..1; 0.5 is neutral for ALL of them. ──────
float gN (float db) { return 0.5f + 0.5f * db / EQ::kBandDbSpan; }        // gain dB -> 0..1
float tN (float db) { return 0.5f + 0.5f * db / EQ::kSlantDbSpan; }        // tilt dB -> 0..1
float fN (int band, float hz)
{ static const float lo[4] = { 20, 100, 700, 6000 }, hi[4] = { 500, 3000, 14000, 40000 };
  return std::log (hz / lo[band]) / std::log (hi[band] / lo[band]); }

EQ::Params base()
{ EQ::Params p; p.mix = 1.0f; return p; }

// The REFERENCE PATCH used by the cross-type matrix and most gates: a real, musical
// four-band move, big enough to separate the Types and small enough to stay measurable.
EQ::Params refPatch (int type, int chr = 0)
{
    EQ::Params p = base();
    p.type = type; p.character = chr;
    p.b1 = fN (0,   90.0f);  p.b2 = gN ( 18.0f);
    p.b3 = fN (1,  500.0f);  p.b4 = gN (-24.0f);
    p.b5 = fN (2, 3200.0f);  p.b6 = gN ( 18.0f);
    p.b7 = fN (3,16000.0f);  p.f2 = gN ( 18.0f);
    p.b8 = 0.5f; p.f1 = tN (8.0f); p.f3 = 0.5f;
    return p;
}

// stereo transfer with independent channels (needed for anything touching Focus: if L==R
// the SIDE signal is identically zero and a Side-focus test measures nothing at all).
// pick: 0 = L, 1 = R, 2 = mono fold (L+R)/2 measured against the mono fold of the input.
Spec transferStereo (const EQ::Params& p, int pick, bool decorr, float probeRms = 0.05f)
{
    const int NF = 16384, HOP = 8192, SEG = 4, settle = 8192;
    const int N = settle + NF + (SEG - 1) * HOP;
    auto xl = whiteN (N, probeRms, 12345u);
    auto xr = decorr ? whiteN (N, probeRms, 777u) : xl;
    std::vector<float> L (xl), R (xr);
    EQ e; e.prepare ((double) FS, 128);
    for (int i = 0; i < N; i += 128)
    { const int nn = std::min (128, N - i); e.setParams (p); e.processStereo (&L[(size_t) i], &R[(size_t) i], nn); }
    std::vector<float> in ((size_t) N), out ((size_t) N);
    for (int i = 0; i < N; ++i)
    { in [(size_t) i] = (pick == 0 ? xl[(size_t) i] : pick == 1 ? xr[(size_t) i] : 0.5f * (xl[(size_t) i] + xr[(size_t) i]));
      out[(size_t) i] = (pick == 0 ? L [(size_t) i] : pick == 1 ? R [(size_t) i] : 0.5f * (L [(size_t) i] + R [(size_t) i])); }

    std::vector<double> sxx ((size_t) NF / 2 + 1, 0.0), syy ((size_t) NF / 2 + 1, 0.0);
    std::vector<std::complex<double>> A ((size_t) NF), B ((size_t) NF);
    for (int s = 0; s < SEG; ++s)
    { const int off = settle + s * HOP;
      for (int i = 0; i < NF; ++i)
      { const double w = 0.5 - 0.5 * std::cos (6.283185307179586 * i / (NF - 1));
        A[(size_t) i] = in[(size_t)(off+i)] * w; B[(size_t) i] = out[(size_t)(off+i)] * w; }
      fft (A); fft (B);
      for (int k = 0; k <= NF / 2; ++k) { sxx[(size_t)k] += std::norm (A[(size_t)k]); syy[(size_t)k] += std::norm (B[(size_t)k]); } }
    Spec o;
    for (int i = 0; i < NB; ++i)
    { const double fc = EQ::curveBinHz (i), r = std::pow (10.0, 1.5 / 95.0);
      int klo = std::max (1, (int) std::floor (fc / r * NF / FS));
      int khi = std::min (NF / 2, std::max ((int) std::ceil (fc * r * NF / FS), klo));
      double nx = 0, ny = 0; for (int k = klo; k <= khi; ++k) { nx += sxx[(size_t)k]; ny += syy[(size_t)k]; }
      o.db[i] = 10.0 * std::log10 (std::max (1e-300, ny) / std::max (1e-300, nx)); }
    return o;
}

// the engine's OWN 96-bin push, read after the same probe. §B6 gates this against the
// independently measured transfer above.
Spec vizOf (const EQ::Params& p)
{
    const int N = 48000;
    auto x = whiteN (N, 0.05f, 12345u);
    std::vector<float> L (x), R (x);
    EQ e; e.prepare ((double) FS, 128);
    for (int i = 0; i < N; i += 128)
    { const int nn = std::min (128, N - i); e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], nn); }
    Spec s; for (int i = 0; i < NB; ++i) s.db[i] = e.viz().curve[i];
    return s;
}

// -3 dB (half-height, in dB) width of the dominant feature, in OCTAVES.
double bwOct (const Spec& s)
{
    int pk = 0; for (int i = 0; i < NB; ++i) if (s.db[i] > s.db[pk]) pk = i;
    const double half = s.db[pk] * 0.5;
    int lo = pk, hi = pk;
    while (lo > 0      && s.db[lo] > half) --lo;
    while (hi < NB - 1 && s.db[hi] > half) ++hi;
    return std::log2 (EQ::curveBinHz (hi) / EQ::curveBinHz (lo));
}

// impulse-response decay: T60 of the ring, in ms.
//
// 🚨 fb422 — THE RULER MUST BE LONGER THAN THE BAR, AND IT MUST SAY WHEN IT SATURATES.
//  The fb420 version ran a FIXED 1.5 s window and, when the tail never crossed -60 dB,
//  returned `N*1000/FS` = exactly 1500.0. The ring gate's bar is 3100 ms. 1500 < 3100 for
//  every possible input, so THE GATE COULD NOT FAIL: with limitRing()'s body deleted the
//  cert still printed 1500 ms — the saturation value of its own ruler — while the engine
//  actually rang for 11.8 seconds.
//  Now: the window is a parameter, defaults to 8 s (2.6x the bar), the saturation value is
//  RETURNED (so a non-terminating tail reads 8000 ms and fails loudly), and a saturation
//  flag is exposed so §L can print "did not decay within the window" instead of a number
//  that looks like a measurement. Early-exit keeps it cheap: only a genuinely long ring
//  pays for the long window.
double t60ms (const EQ::Params& p, double maxSec = 8.0, bool* saturated = nullptr)
{
    const int settle = 8192, N = (int) (FS * (float) maxSec);
    EQ e; e.prepare ((double) FS, 128);
    std::vector<float> L (128, 0.0f), R (128, 0.0f);
    // settle with silence, then one impulse
    for (int i = 0; i < settle; i += 128)
    { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
      e.setParams (p); e.processStereo (L.data(), R.data(), 128); }
    double pk = 0; int frame = 0, pki = 0; bool first = true;
    // the early exit must not out-run the envelope's own rise: a high-Q resonator peaks a
    // few frames AFTER the impulse. Require 8 frames past the running peak and THREE
    // consecutive frames under -60 dB before calling it, and report the first of the three
    // (which is what the fixed-window version reported).
    int belowRun = 0, belowAt = 0;
    for (int i = 0; i < N; i += 128)
    {
        std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        if (first) { L[0] = 0.25f; R[0] = 0.25f; first = false; }
        e.setParams (p); e.processStereo (L.data(), R.data(), 128);
        for (int k = 0; k + 32 <= 128; k += 32, ++frame)
        {
            double s2 = 0; for (int q = 0; q < 32; ++q) s2 += (double) L[(size_t)(k+q)] * L[(size_t)(k+q)];
            const double v = std::sqrt (s2 / 32.0);
            if (v > pk) { pk = v; pki = frame; belowRun = 0; }
            const bool below = (pk > 0.0 && frame > pki + 8 && v < pk * 0.001);
            if (below) { if (belowRun == 0) belowAt = frame; ++belowRun; } else belowRun = 0;
            if (belowRun >= 3)
            { if (saturated) *saturated = false;
              return (double) (belowAt - pki) * 32.0 * 1000.0 / FS; }
        }
    }
    if (saturated) *saturated = true;
    return maxSec * 1000.0;                                  // THE SATURATION VALUE
}

// ═════════════════════════════════════════════════════════════════════════════
void sectionA()
{
    section ("A. The NaN trap, the null law, Focus transparency, and the roster mapping");

    // 🚨 THE ParametricEQ.h:42-47 GATE. Prepare, then push audio with NO setParams call at
    // all. If any smoother defaulted to 0, Q = 0 => alpha = sin/(2Q) = NaN and the whole
    // instance is poisoned for life. This gate is why seedSmoothers() exists.
    {
        EQ e; e.prepare ((double) FS, 128);
        auto x = chordN (4096); std::vector<float> L (x), R (x);
        e.processStereo (L.data(), R.data(), 4096);
        bool fin = true; double worst = 0;
        for (int i = 0; i < 4096; ++i)
        { if (! std::isfinite (L[(size_t)i]) || ! std::isfinite (R[(size_t)i])) fin = false;
          worst = std::max (worst, (double) std::fabs (L[(size_t)i] - x[(size_t)i])); }
        gate ("prepare() with NO setParams: finite AND bit-transparent", fin && worst == 0.0,
              fmt ("worst sample delta %.3e", worst));
    }
    // the same, but with a hostile Params: every knob at 0 (which is where a naive Q would
    // be zero) and every knob at 1.
    for (int k = 0; k < 2; ++k)
    {
        const float v = (k == 0 ? 0.0f : 1.0f);
        bool fin = true; double peak = 0;
        for (int t = 0; t < EQ::kNumTypes; ++t)
        {
            EQ::Params p = base(); p.type = t;
            p.b1=p.b2=p.b3=p.b4=p.b5=p.b6=p.b7=p.b8=v; p.f1=p.f2=p.f3=v;
            auto x = chordN (8192); std::vector<float> L (x), R (x);
            EQ e; e.prepare ((double) FS, 128);
            for (int i = 0; i < 8192; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
            for (int i = 0; i < 8192; ++i)
            { if (! std::isfinite (L[(size_t)i])) fin = false; peak = std::max (peak, (double) std::fabs (L[(size_t)i])); }
        }
        gate (k == 0 ? "every knob at 0 %, all 7 Types: finite" : "every knob at 100 %, all 7 Types: finite",
              fin, fmt ("peak %.2f linear", peak));
    }

    // NULL. Defaults are neutral by construction (0.5 is the centre of every knob).
    {
        EQ::Params p = base();
        auto x = chordN (8192); std::vector<float> L (x), R (x);
        EQ e; e.prepare ((double) FS, 128);
        for (int i = 0; i < 8192; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
        double w = 0; for (int i = 0; i < 8192; ++i) w = std::max (w, (double) std::fabs (L[(size_t)i] - x[(size_t)i]));
        gate ("defaults null BIT-EXACTLY (fb303 default-sound guarantee)", w == 0.0, fmt ("worst delta %.3e", w));
    }
    // Amount 0 % nulls at ANY knob state — the A/B gesture, provable.
    {
        EQ::Params p = refPatch (6, 7); p.f3 = 0.0f;      // Chisel / `Tin`, everything lit
        auto x = chordN (8192); std::vector<float> L (x), R (x);
        EQ e; e.prepare ((double) FS, 128);
        for (int i = 0; i < 8192; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
        double w = 0; for (int i = 0; i < 8192; ++i) w = std::max (w, (double) std::fabs (L[(size_t)i] - x[(size_t)i]));
        gate ("Amount 0 % nulls BIT-EXACTLY at a full Chisel/`Tin` patch", w == 0.0, fmt ("worst delta %.3e", w));
    }
    // Mix 0 % is the dry, exactly.
    {
        EQ::Params p = refPatch (6, 1); p.mix = 0.0f;
        auto x = chordN (8192); std::vector<float> L (x), R (x);
        EQ e; e.prepare ((double) FS, 128);
        for (int i = 0; i < 8192; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
        double w = 0; for (int i = 0; i < 8192; ++i) w = std::max (w, (double) std::fabs (L[(size_t)i] - x[(size_t)i]));
        gate ("Mix 0 % is the dry signal, bit for bit", w == 0.0, fmt ("worst delta %.3e", w));
    }
    // Focus: the UNFOCUSED channel must be untouched. Left focus, extreme patch, R checked.
    for (int fx = 3; fx <= 4; ++fx)
    {
        EQ::Params p = refPatch (6, 1); p.axis = fx;
        auto xl = chordN (8192), xr = whiteN (8192, 0.05f, 999u);
        std::vector<float> L (xl), R (xr);
        EQ e; e.prepare ((double) FS, 128);
        for (int i = 0; i < 8192; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
        const std::vector<float>& un  = (fx == 3 ? R  : L);
        const std::vector<float>& ref = (fx == 3 ? xr : xl);
        double w = 0; for (int i = 0; i < 8192; ++i) w = std::max (w, (double) std::fabs (un[(size_t)i] - ref[(size_t)i]));
        gate (fx == 3 ? "Focus Left: the RIGHT channel is bit-exact" : "Focus Right: the LEFT channel is bit-exact",
              w == 0.0, fmt ("worst delta %.3e", w));
    }
    // Focus M/S round trip at zero gain: not bit-exact (two 0.7071 multiplies) but must be
    // inaudible. Measured, not assumed.
    for (int fx = 1; fx <= 2; ++fx)
    {
        EQ::Params p = base(); p.axis = fx;
        auto xl = chordN (8192), xr = whiteN (8192, 0.05f, 999u);
        std::vector<float> L (xl), R (xr);
        EQ e; e.prepare ((double) FS, 128);
        for (int i = 0; i < 8192; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
        double num = 0, den = 0;
        for (int i = 0; i < 8192; ++i)
        { const double d = (double) L[(size_t)i] - xl[(size_t)i]; num += d * d; den += (double) xl[(size_t)i] * xl[(size_t)i]; }
        const double dbr = 10.0 * std::log10 (std::max (1e-300, num / std::max (1e-300, den)));
        gate (fx == 1 ? "Focus Mid at zero gain: residual < -120 dB" : "Focus Side at zero gain: residual < -120 dB",
              dbr < -120.0, fmt ("M/S round-trip residual %.1f dB", dbr));
    }

    // the roster mapping (§3.1 fb373): the engine's own name<->index<->physics agreement.
    {
        bool ok = true; std::string bad;
        for (int t = 0; t < EQ::kNumTypes; ++t)
        { if (EQ::typeNames()[t] == nullptr || EQ::shapeName (t) == nullptr) ok = false;
          for (int c = 0; c < EQ::kNumChars; ++c) if (EQ::charNames (t)[c] == nullptr) ok = false; }
        // out-of-range indices must CLAMP, never read past the table
        EQ::Params p = refPatch (0); p.type = 99; p.character = 99; p.axis = 99;
        auto x = chordN (2048); std::vector<float> L (x), R (x);
        EQ e; e.prepare ((double) FS, 128);
        for (int i = 0; i < 2048; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
        for (int i = 0; i < 2048; ++i) if (! std::isfinite (L[(size_t)i])) ok = false;
        gate ("roster: 7 Types x 8 Characters x 5 Focus, all named, indices clamped", ok,
              fmt2 ("kNumTypes %.0f live of %.0f declared slots", (double) EQ::kNumTypes, (double) EQ::kNumTypeSlots));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
double designErrDb (int kind, double f0, double Q0, double gdb, double fs, bool matched, double* rbjOut = nullptr)
{
    // the engine limits shelf Q where its pole pair would leave the band (see the header):
    // the harness must therefore measure against the SAME prototype the engine designs to,
    // or it is grading the device against a filter that cannot exist.
    const double Q = EQ::usableQ (kind, f0, Q0, gdb, fs);
    const auto cm = matched ? EQ::designMatched (kind, f0, Q, gdb, fs) : EQ::designRbj (kind, f0, Q, gdb, fs);
    double worst = 0, worstRbj = 0;
    for (int i = 0; i <= 400; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, i / 400.0);
        if (f > 0.45 * fs) break;
        const double target = 10.0 * std::log10 (EQ::protoMag2 (kind, f, f0, Q, gdb));
        worst = std::max (worst, std::fabs (EQ::magDb (cm, f, fs) - target));
        if (rbjOut)
        { const auto cr = EQ::designRbj (kind, f0, Q, gdb, fs);
          worstRbj = std::max (worstRbj, std::fabs (EQ::magDb (cr, f, fs) - target)); }
    }
    if (rbjOut) *rbjOut = worstRbj;
    return worst;
}

void sectionB()
{
    section ("B. DECRAMPING — the math gate (this section reads coefficients, and says so)");

    // 🪤 NEGATIVE CONTROL, REQUIRED TO FAIL: the RBJ design is the OLD code. If the gate
    // cannot fail on RBJ it is not measuring anything.
    {
        double rbj = 0;
        const double m = designErrDb (0, 16000.0, 2.0, 10.0, 44100.0, true, &rbj);
        gate ("16 kHz Q2 +10 dB @ 44.1 k: matched error <= 1.0 dB", m <= 1.0,
              fmt ("max |err| vs analog prototype %.3f dB", m));
        gate ("   ... and the SAME gate FAILS on RBJ (the negative control)", rbj >= 3.0,
              fmt ("RBJ max |err| %.3f dB  <- this is the cramp, in dB", rbj));
    }
    {
        FS = 96000.0f;
        double rbj = 0;
        const double m = designErrDb (0, 16000.0, 2.0, 10.0, 96000.0, true, &rbj);
        gate ("16 kHz Q2 +10 dB @ 96 k: the cramp is an fs story — it mostly goes away",
              m <= 0.5 && rbj <= 1.3, fmt2 ("matched %.3f dB · RBJ %.3f dB (was 3.9 dB at 44.1 k)", m, rbj));
        FS = 48000.0f;
    }
    // ── the whole grid the device can actually reach, at BOTH sample rates, with the
    //    same number for RBJ so the comparison is like-for-like and not a slogan.
    {
        double wB = 0, wS = 0, wBr = 0, wSr = 0, sB = 0, sS = 0, sBr = 0, sSr = 0; int nB = 0, nS = 0;
        double wLo = 0; std::string wb, ws;
        const double fsv[2] = { 44100.0, 96000.0 };
        auto err = [] (const EQ::Coeffs& c, int kind, double f0, double q, double g, double fs)
        { double w = 0;
          for (int i = 0; i <= 300; ++i)
          { const double f = 20.0 * std::pow (1000.0, i / 300.0); if (f > 0.45 * fs) break;
            w = std::max (w, std::fabs (EQ::magDb (c, f, fs) - 10.0 * std::log10 (EQ::protoMag2 (kind, f, f0, q, g)))); }
          return w; };
        for (double fs : fsv)
          for (double f0 : { 40.0, 120.0, 450.0, 1200.0, 3200.0, 8000.0, 14000.0 })
            for (double q0 : { 0.35, 1.0, 4.0, 20.0 })
              for (double g : { -30.0, -12.0, 6.0, 24.0 })
              { const double q = EQ::usableQ (0, f0, q0, g, fs);
                const double e  = err (EQ::designBand (0, f0, q, g, fs), 0, f0, q, g, fs);
                const double er = err (EQ::designRbj  (0, f0, q, g, fs), 0, f0, q, g, fs);
                sB += e; sBr += er; ++nB; wBr = std::max (wBr, er);
                if (e > wB) { wB = e; wb = fmt3 ("f0 %.0f Q %.2f g %.0f dB", f0, q, g) + fmt (" @ %.1f k", fs / 1000.0); }
                if (f0 <= 0.25 * fs) wLo = std::max (wLo, e); }
        for (double fs : fsv)
          for (int kind = 1; kind <= 2; ++kind)
            for (double f0 : { 30.0, 90.0, 500.0, 6000.0, 16000.0, 28000.0, 40000.0 })
              for (double q0 : { 0.5, 0.9, 2.0, 4.5 })
                for (double g : { -30.0, -12.0, 12.0, 30.0 })
                { if ((kind == 1) != (f0 < 3000.0)) continue;
                  const double q = EQ::usableQ (kind, f0, q0, g, fs);
                  const double e = err (EQ::designBand (kind, f0, q, g, fs), kind, f0, q, g, fs);
                  const double er = (f0 < 0.45 * fs) ? err (EQ::designRbj (kind, f0, q, g, fs), kind, f0, q, g, fs) : 0.0;
                  sS += e; sSr += er; ++nS; wSr = std::max (wSr, er);
                  if (e > wS) { wS = e; ws = fmt3 ("kind %.0f f0 %.0f Q %.2f", (double) kind, f0, q) + fmt (" g %.0f dB", g); }
                  if (f0 <= 0.25 * fs) wLo = std::max (wLo, e); }
        note (fmt ("worst error for corners below 0.25 fs: %.2f dB (the cramp is a", wLo)
              + " top-octave story)");
        gate ("all 224 reachable BELL settings, 44.1 + 96 k: worst <= 5 dB", wB <= 5.0,
              fmt ("worst %.2f dB at ", wB) + wb + fmt (" · mean %.3f dB", sB / nB));
        gate ("   ... where RBJ is 2.5x worse ON AVERAGE over the same grid (control)",
              sBr / nB >= 2.5 * sB / nB,
              fmt2 ("RBJ mean %.3f dB vs matched mean %.3f dB", sBr / nB, sB / nB)
              + fmt2 (" · worst %.1f vs %.2f", wBr, wB));
        gate ("all 224 reachable SHELF settings: worst <= 4 dB", wS <= 4.0,
              fmt ("worst %.2f dB at ", wS) + ws + fmt (" · mean %.3f dB", sS / nS));
        gate ("   ... where RBJ is 3x worse ON AVERAGE over the same grid (control)",
              sSr / nS >= 3.0 * sS / nS,
              fmt2 ("RBJ mean %.3f dB vs matched mean %.3f dB", sSr / nS, sS / nS)
              + fmt2 (" · worst %.1f vs %.2f", wSr, wS));
        note ("the residual lives ONLY above 19 kHz on shelves whose corner is past 0.35 fs:");
        note ("a minimum-phase biquad cannot carry a pole pair that sits beyond Nyquist, and");
        note ("that is a fact about digital filters, not a tuning choice. Documented, not hidden.");
    }
    // the RBJ <-> matched blend seam: sweep f0 through the crossover and require continuity
    {
        double jump = 0; double prev[5] = { 0,0,0,0,0 }; bool first = true;
        const double probes[5] = { 200.0, 800.0, 2000.0, 6000.0, 15000.0 };
        for (int i = 0; i <= 600; ++i)
        {
            const double f0 = 300.0 * std::pow (14000.0 / 300.0, i / 600.0);
            const auto c = EQ::designBand (0, f0, 1.4, 18.0, 48000.0);
            double cur[5]; for (int k = 0; k < 5; ++k) cur[k] = EQ::magDb (c, probes[k], 48000.0);
            if (! first) for (int k = 0; k < 5; ++k) jump = std::max (jump, std::fabs (cur[k] - prev[k]));
            for (int k = 0; k < 5; ++k) prev[k] = cur[k];
            first = false;
        }
        gate ("the RBJ->matched blend has NO seam as f0 sweeps 300 Hz - 14 kHz", jump < 0.5,
              fmt ("worst step between adjacent designs %.4f dB (0.4 %% of an octave apart)", jump));
    }
    // 🔬 THE CONTROL NUMBER: the identical metric through a design that does nothing.
    {
        const double z = designErrDb (0, 16000.0, 2.0, 0.0, 44100.0, true);
        gate ("control number: the same metric on a 0 dB (bypassed) band", z < 1e-9,
              fmt ("max |err| %.3e dB — this is what 'no error' looks like", z));
    }
    // 🔑 THE MAAG LAW: Reach above Nyquist must stay a LIVE knob. At 44.1 k a 40 kHz corner
    // is 1.8x Nyquist; RBJ pins it and the top half of the knob dies.
    {
        std::string row; double span = 0; bool mono = true; double prev = 1e9;
        for (double reach : { 8000.0, 12000.0, 16000.0, 22000.0, 30000.0, 40000.0 })
        {
            const auto c = EQ::designBand (2, reach, 0.8, 20.0, 44100.0);
            const double v = EQ::magDb (c, 15000.0, 44100.0);
            row += fmt2 ("%.0fk:%.1f  ", reach / 1000.0, v);
            if (prev < 1e8) { if (v > prev + 0.05) mono = false; span = std::max (span, prev - v); }
            prev = v;
        }
        const auto lo = EQ::designBand (2,  8000.0, 0.8, 20.0, 44100.0);
        const auto hi = EQ::designBand (2, 40000.0, 0.8, 20.0, 44100.0);
        const double d = EQ::magDb (lo, 15000.0, 44100.0) - EQ::magDb (hi, 15000.0, 44100.0);
        gate ("Reach 8k->40k stays a LIVE knob at 44.1 k (monotonic, wide)", mono && d > 10.0,
              fmt ("response at 15 kHz moves %.1f dB across the knob", d));
        note ("15 kHz response vs Reach: " + row);
        // and the negative control: RBJ cannot even be designed above Nyquist - it clamps
        const auto r1 = EQ::designRbj (2, 22000.0, 0.8, 20.0, 44100.0);
        const auto r2 = EQ::designRbj (2, 40000.0, 0.8, 20.0, 44100.0);
        const double dead = std::fabs (EQ::magDb (r1, 15000.0, 44100.0) - EQ::magDb (r2, 15000.0, 44100.0));
        gate ("   ... where RBJ's top half is a DEAD knob (negative control)", dead < 0.01,
              fmt ("RBJ 22 kHz -> 40 kHz moves 15 kHz by %.4f dB", dead));
    }
}

// one band lit, everything else neutral. band 3's gain is the FRONT `Air` knob.
EQ::Params single (int type, int chr, int band, float db, float hz, float shape = 0.5f)
{
    EQ::Params p = base(); p.type = type; p.character = chr; p.b8 = shape;
    if (band == 0) { p.b1 = fN (0, hz); p.b2 = gN (db); }
    if (band == 1) { p.b3 = fN (1, hz); p.b4 = gN (db); }
    if (band == 2) { p.b5 = fN (2, hz); p.b6 = gN (db); }
    if (band == 3) { p.b7 = fN (3, hz); p.f2 = gN (db); }
    return p;
}


// ═════════════════════════════════════════════════════════════════════════════
//  🔬 THE SINE INSTRUMENT. A 96-log-band noise spectrum CANNOT see a deep narrow notch:
//  a Q 68 null at 550 Hz is 8 Hz wide and the analysis band around it is 21 Hz, so the
//  band average reports -29 dB for a -90 dB hole. That is a property of the RULER, not of
//  the device, and believing it would have hidden the real ceiling by 60 dB. A coherent
//  single-frequency probe has no such floor: it measures the true attenuation down to
//  float noise. Everything below that says "sine" uses this; everything that says
//  "spectrum" uses the noise transfer. Both are stated at every gate.
// snap a frequency to the sine probe's own FFT grid. 🔬 THIS WAS A REAL 33 dB ERROR: a
// Q 90 notch at 550 Hz is 6 Hz wide, the 16384-point grid steps 2.93 Hz, and probing
// 0.78 Hz off centre read -57 dB for a hole designed at -90. The ruler has to land ON the
// thing it is measuring.
double snapHz (double hz, int nf = 65536) { return std::llround (hz * nf / (double) FS) * (double) FS / nf; }

double sineDb (const EQ::Params& p, double hz, float rms = 0.05f, int nf = 16384)
{
    const int NF = nf, settle = nf * 3 / 4, N = settle + NF;
    const int kbin = std::max (1, (int) std::llround (hz * (double) NF / (double) FS));
    const double f = kbin * (double) FS / NF;                 // snap to a bin: no leakage
    std::vector<float> L ((size_t) N), R ((size_t) N);
    for (int i = 0; i < N; ++i)
    { const float v = (float) (rms * 1.41421356 * std::sin (6.283185307179586 * f * i / FS));
      L[(size_t)i] = v; R[(size_t)i] = v; }
    EQ e; e.prepare ((double) FS, 128);
    for (int i = 0; i < N; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
    double sr = 0, si = 0;
    for (int i = 0; i < NF; ++i)
    { const double a = 6.283185307179586 * kbin * i / NF;
      sr += L[(size_t)(settle + i)] * std::cos (a); si -= L[(size_t)(settle + i)] * std::sin (a); }
    const double amp = 2.0 * std::sqrt (sr * sr + si * si) / NF;
    return 20.0 * std::log10 (std::max (1e-30, amp / (rms * 1.41421356)));
}

// fb425 — the same coherent probe, PLUS the engine's own drawn curve averaged over the SAME
//  run at one bin. §Q2c compares a measured floor with the engine's prediction; on the one
//  level-dependent Type a prediction captured under a DIFFERENT excitation is not a
//  prediction at all (measured: a viz snapshot taken under noise disagrees with a sine
//  measurement by up to 40 dB on `Dynamic`, purely because a tone in the band drives the
//  detector differently from noise). Predict and measure in the same breath, or not at all.
double sineDbViz (const EQ::Params& p, double hz, float rms, int nf, int bin, double& vizAvg)
{
    const int NF = nf, settle = nf * 3 / 4, N = settle + NF;
    const int kbin = std::max (1, (int) std::llround (hz * (double) NF / (double) FS));
    const double f = kbin * (double) FS / NF;
    std::vector<float> L ((size_t) N), R ((size_t) N);
    for (int i = 0; i < N; ++i)
    { const float v = (float) (rms * 1.41421356 * std::sin (6.283185307179586 * f * i / FS));
      L[(size_t) i] = v; R[(size_t) i] = v; }
    EQ e; e.prepare ((double) FS, 128);
    double acc = 0; int nv = 0;
    for (int i = 0; i < N; i += 128)
    { e.setParams (p); e.processStereo (&L[(size_t) i], &R[(size_t) i], 128);
      if (i >= settle && bin >= 0 && bin < NB) { acc += e.viz().curve[bin]; ++nv; } }
    if (nv > 0) vizAvg = acc / nv;
    double sr = 0, si = 0;
    for (int i = 0; i < NF; ++i)
    { const double a = 6.283185307179586 * kbin * i / NF;
      sr += L[(size_t) (settle + i)] * std::cos (a); si -= L[(size_t) (settle + i)] * std::sin (a); }
    const double amp = 2.0 * std::sqrt (sr * sr + si * si) / NF;
    return 20.0 * std::log10 (std::max (1e-30, amp / (rms * 1.41421356)));
}

// -3 dB (half-height in dB) bandwidth in OCTAVES, bisected with sine probes so the answer
// is resolution-free even at Q 90.
double bwSine (const EQ::Params& p, double fc, float rms = 0.05f)
{
    const double pk = sineDb (p, fc, rms);
    if (std::fabs (pk) < 1.0) return 0.0;
    const double half = pk * 0.5;
    auto edge = [&] (double dir)
    {
        double lo = fc, hi = fc * std::pow (2.0, 5.0 * dir);
        if (std::fabs (sineDb (p, std::max (10.0, std::min (hi, 0.45 * FS)), rms)) > std::fabs (half)) return hi;
        for (int it = 0; it < 22; ++it)
        { const double mid = std::sqrt (lo * hi);
          if (std::fabs (sineDb (p, std::max (10.0, std::min (mid, 0.45 * FS)), rms)) > std::fabs (half)) lo = mid; else hi = mid; }
        return std::sqrt (lo * hi);
    };
    return std::log2 (edge (1.0) / edge (-1.0));
}

struct Feat
{
    Spec ref;             // the reference patch transfer
    double peak18;        // measured peak of a +18 dB Body boost (the GAIN law)
    double bwMid;         // its bandwidth in octaves (the Q law)
    double propRatio;     // bw(+6 dB) / bw(+30 dB)  (the PROPORTIONAL law)
    double undershoot;    // min dB just above a +24 dB Low shelf (the SHELF law)
    double scoop;         // min dB in 150-700 Hz for a +18 dB @ 60 Hz Low boost (PASSIVE)
    double levelDep;      // max |transfer(-12 dBFS) - transfer(-40 dBFS)|  (DYNAMIC)
    double notch;         // min dB of a -30 dB Body cut (SCULPT)
    double top;           // dB at 18 kHz for Air +20 with Reach at maximum (OPEN / decramp)
};

Feat featureOf (int t)
{
    Feat f;
    f.ref  = transferOf (refPatch (t));
    { const Spec s = transferOf (single (t, 0, 1, 18.0f, 550.0f)); f.peak18 = specMax (s); f.bwMid = bwOct (s); }
    { const Spec a = transferOf (single (t, 0, 1,  6.0f, 550.0f));
      const Spec b = transferOf (single (t, 0, 1, 30.0f, 550.0f));
      f.propRatio = bwOct (a) / std::max (0.02, bwOct (b)); }
    { const Spec s = transferOf (single (t, 0, 0, 24.0f, 90.0f)); f.undershoot = minInRange (s, 110.0, 720.0); }
    // measured at each Type's DEFAULT `Trait` (0.5). At `Trait` 1.0 Surgical's `Pinch` is 40x
    // and its low SHELF resonates too, which muddied the Passive discriminator to within
    // 0.07 dB. At the default, `Pinch` is exactly 1.0 and Surgical has no resonance at all,
    // so the scoop belongs to Passive alone - which is the claim being tested.
    { const Spec s = transferOf (single (t, 0, 0, 18.0f, 60.0f, 0.5f)); f.scoop = minInRange (s, 150.0, 700.0); }
    { const Spec a = transferOf (single (t, 0, 1, -18.0f, 550.0f), 0.01f);     // -40 dBFS
      const Spec b = transferOf (single (t, 0, 1, -18.0f, 550.0f), 0.25f);     // -12 dBFS
      f.levelDep = specDelta (a, b); }
    { const double fn = snapHz (550.0);
      f.notch = sineDb (single (t, 0, 1, -30.0f, (float) fn, 0.5f), fn, t == 5 ? 0.25f : 0.05f, 65536); }
    { EQ::Params p = single (t, 0, 3, 20.0f, 40000.0f); const Spec s = transferOf (p); f.top = atHz (s, 18000.0); }
    return f;
}

void sectionC (const Feat* F)
{
    section ("C. The display cannot lie, and every Type's own discriminator (law 2)");
    {
        const EQ::Params p = refPatch (0);
        const Spec meas = transferOf (p), drawn = vizOf (p);
        double w = 0; int nb = 0;
        for (int i = 0; i < NB; ++i)
        { const double f = EQ::curveBinHz (i);
          if (f < 25.0 || f > 18000.0) continue;
          if (drawn.db[i] < -30.0) continue;      // the noise ruler has no floor below this
          ++nb; w = std::max (w, std::fabs (meas.db[i] - drawn.db[i])); }
        gate ("the engine's 96-bin viz curve == the MEASURED output spectrum", w < 1.5,
              fmt2 ("worst |drawn - measured| %.2f dB over %.0f bins, 25 Hz - 18 kHz", w, (double) nb));
        note ("   bins the drawn curve puts below -30 dB are excluded: a 96-band noise");
        note ("   spectrum cannot measure a hole narrower than its own analysis band, and");
        note ("   grading the device with a ruler that bottoms out first proves nothing.");
    }

    gate ("Surgical: EXACT dB — a +18 dB knob measures +18 dB", std::fabs (F[0].peak18 - 18.0) < 1.2,
          fmt ("measured peak %.2f dB", F[0].peak18));
    { double worst = 0; int wi = 0;
      for (int t = 1; t < EQ::kNumTypes; ++t) if (std::fabs (F[t].peak18 - 18.0) > worst) { worst = std::fabs (F[t].peak18 - 18.0); wi = t; }
      note (std::string ("   the gain-law spread: the Type furthest from the knob is ")
            + EQ::typeNames()[wi] + fmt (" at %.2f dB", F[wi].peak18)); }

    gate ("British: the inductor shelf UNDERSHOOTS before it rises",
          F[1].undershoot <= -2.0, fmt2 ("dip %.2f dB just above fc (Surgical: %.2f dB)", F[1].undershoot, F[0].undershoot));
    gate ("   ... and no other Type digs that dip", F[1].undershoot < F[0].undershoot - 1.5,
          fmt ("British is %.2f dB deeper than the reference type", F[0].undershoot - F[1].undershoot));

    gate ("American: PROPORTIONAL Q — a small move is wide, a big move is a laser",
          F[2].propRatio >= 2.5, fmt2 ("bw(+6 dB)/bw(+30 dB) = %.2fx  (Surgical: %.2fx)", F[2].propRatio, F[0].propRatio));
    gate ("   ... where Surgical's Q is CONSTANT with gain", F[0].propRatio < 1.6,
          fmt ("Surgical ratio %.2fx", F[0].propRatio));

    gate ("Passive: one knob makes the Pultec HUMP AND the SCOOP",
          F[3].scoop <= -2.5, fmt2 ("trough %.2f dB at 150-700 Hz (Surgical: %.2f dB)", F[3].scoop, F[0].scoop));

    gate ("Open: the bells are more than 3 octaves wide", F[4].bwMid > 3.0,
          fmt2 ("-3 dB width %.2f octaves (Surgical: %.2f)", F[4].bwMid, F[0].bwMid));
    gate ("   ... and its AIR still LIFTS at Reach 40 kHz — gentle, not dead",
          F[4].top > 3.0 && F[4].top < 26.0, fmt ("18 kHz sits at %+.2f dB with Reach at 40 kHz", F[4].top));
    { double stiff = 0; for (int t = 0; t < EQ::kNumTypes; ++t) if (t != 4) stiff = std::max (stiff, F[t].top);
      note (fmt ("   the 2-pole Types deliver only %+.2f dB there — that is why Open's AIR is 6 dB/oct", stiff)); }

    gate ("Dynamic: the response MOVES with the program level (the family tell)",
          F[5].levelDep >= 6.0, fmt ("%.2f dB of response change from -40 to -12 dBFS", F[5].levelDep));
    { double worst = 0; for (int t = 0; t < EQ::kNumTypes; ++t) if (t != 5) worst = std::max (worst, F[t].levelDep);
      gate ("   ... and every other Type is level-INDEPENDENT by construction", worst < 1.0,
            fmt ("worst non-Dynamic level dependence %.3f dB", worst)); }

    gate ("Chisel: a deep cut becomes a TRUE NOTCH, not a dip (sine probe)", F[6].notch <= -45.0,
          fmt2 ("notch floor %.1f dB (Surgical at the same knob: %.1f dB)", F[6].notch, F[0].notch));
    { const double a = t60ms (single (6, 0, 1, 24.0f, 1000.0f, 0.92f));
      const double b = t60ms (single (0, 0, 1, 24.0f, 1000.0f, 0.50f));
      gate ("   ... and it RINGS: T60 > 60 ms where Surgical does not", a > 60.0 && b < 25.0,
            fmt2 ("Chisel/Sting T60 %.1f ms · Surgical T60 %.1f ms", a, b)); }
}

void sectionD (const Feat* F)
{
    section ("D. CROSS-TYPE DISTINCTNESS MATRIX  (1.00 = one JND unit; gate 1.00)");
    auto dist = [&] (int a, int b)
    {
        const double ax[10] = { specDelta (F[a].ref, F[b].ref) / 3.0,
                                specRms   (F[a].ref, F[b].ref) / 1.5,
                                (F[a].peak18     - F[b].peak18)     / 3.0,
                                (F[a].bwMid      - F[b].bwMid)      / 0.5,
                                (F[a].propRatio  - F[b].propRatio)  / 0.6,
                                (F[a].undershoot - F[b].undershoot) / 2.0,
                                (F[a].scoop      - F[b].scoop)      / 2.5,
                                (F[a].levelDep   - F[b].levelDep)   / 3.0,
                                (F[a].notch      - F[b].notch)      / 8.0,
                                (F[a].top        - F[b].top)        / 3.0 };
        double s = 0; for (double v : ax) s += v * v;
        return std::min (10.0, std::sqrt (s));
    };
    std::printf ("       ");
    for (int t = 0; t < EQ::kNumTypes; ++t) std::printf ("%8.8s", EQ::typeNames()[t]);
    std::printf ("\n");
    double worst = 1e9; int wa = 0, wb = 1;
    for (int a = 0; a < EQ::kNumTypes; ++a)
    {
        std::printf ("  %-6.6s", EQ::typeNames()[a]);
        for (int b = 0; b < EQ::kNumTypes; ++b)
        { if (a == b) { std::printf ("%8s", "."); continue; }
          const double d = dist (a, b); std::printf ("%8.2f", d);
          if (b > a && d < worst) { worst = d; wa = a; wb = b; } }
        std::printf ("\n");
    }
    gate ("every Type pair is distinguishable (worst pair >= 1.00)", worst >= 1.0,
          fmt ("closest pair %.2f = ", worst) + EQ::typeNames()[wa] + "/" + EQ::typeNames()[wb]);
    note ("axes: ref max /3 dB · ref rms /1.5 dB · +18 peak /3 dB · bandwidth /0.5 oct");
    note ("      prop ratio /0.6 · undershoot /2 dB · scoop /2.5 dB · level-dep /3 dB");
    note ("      notch floor /8 dB · 18 kHz top /3 dB");
    std::printf ("\n        %-11s %7s %7s %7s %8s %7s %8s %7s\n",
                 "Type", "pk+18", "bwOct", "prop", "undsht", "scoop", "notch", "top18k");
    for (int t = 0; t < EQ::kNumTypes; ++t)
        std::printf ("        %-11s %7.2f %7.2f %7.2f %8.2f %7.2f %8.1f %7.2f\n",
                     EQ::typeNames()[t], F[t].peak18, F[t].bwMid, F[t].propRatio,
                     F[t].undershoot, F[t].scoop, F[t].notch, F[t].top);
}

// ── the Dynamic gain-ride TRAJECTORY. A steady probe cannot tell Fast from Slow: their
//    steady-state ride is identical BY CONSTRUCTION. This is the AM/step probe the bible
//    demands, and it reads the engine's own applied-gain push (which §C already gated
//    against the measured output spectrum, so it is not a free pass).
struct Ride { double atk, rel, hi, lo, mid, near1, near2; };
Ride rideOf (int chr, float shape = 0.5f)
{
    EQ::Params p = single (5, chr, 1, -24.0f, 550.0f, shape);
    const int seg = (int) (FS * 0.5f), N = seg * 3;
    auto lo = whiteN (N, 0.0056f, 4242u);           // -45 dBFS
    auto hi = whiteN (N, 0.316f,  4242u);           // -10 dBFS
    std::vector<float> L ((size_t) N), R ((size_t) N);
    for (int i = 0; i < N; ++i)
    { const float v = (i >= seg && i < 2 * seg) ? hi[(size_t)i] : lo[(size_t)i]; L[(size_t)i] = v; R[(size_t)i] = v; }
    EQ e; e.prepare ((double) FS, 128);
    // 32-sample blocks: at 128 the resolution is 2.7 ms and `Fast` (0.8 ms) reads the same
    // as `Program Ride` (3.6 ms). The ruler has to be finer than the thing it measures.
    std::vector<double> g; g.reserve ((size_t) N / 32 + 2);
    for (int i = 0; i < N; i += 32)
    { const int nn = std::min (32, N - i); e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], nn);
      g.push_back (e.viz().nodeDb[1]); }
    const int bLo = seg / 32, bHi = 2 * seg / 32;
    Ride r; r.lo = g[(size_t)(bLo - 2)]; r.hi = g[(size_t)(bHi - 2)];
    const double d = r.hi - r.lo;
    r.atk = 0; r.rel = 0;
    if (std::fabs (d) > 0.5)
    { for (int i = bLo; i < bHi; ++i) if (std::fabs (g[(size_t)i] - r.lo) >= 0.63 * std::fabs (d))
      { r.atk = (i - bLo) * 32.0 * 1000.0 / FS; break; }
      for (int i = bHi; i < (int) g.size(); ++i) if (std::fabs (g[(size_t)i] - r.hi) >= 0.63 * std::fabs (d))
      { r.rel = (i - bHi) * 32.0 * 1000.0 / FS; break; } }
    // the ride at the NOMINAL program level: this is what separates a hard threshold
    // window from a soft one, and a step probe alone is blind to it.
    // 🔬 the window width is INVISIBLE at the endpoints - both a 1.5 dB and a 24 dB window
    //    are fully on at -10 dBFS and fully off at -45. It only shows within a few dB of the
    //    threshold, so that is where the probe has to sit.
    { r.mid   = atHz (transferOf (single (5, chr, 1, -24.0f, 550.0f, shape), 0.0501f),  550.0);
      r.near1 = atHz (transferOf (single (5, chr, 1, -24.0f, 550.0f, shape), 0.0355f),  550.0);   // -29 dBFS
      r.near2 = atHz (transferOf (single (5, chr, 1, -24.0f, 550.0f, shape), 0.0708f),  550.0); } // -23 dBFS
    return r;
}

void sectionE()
{
    section ("E. Every Character re-wires PHYSICS (R6) — within-Type distinctness");
    for (int t = 0; t < EQ::kNumTypes; ++t)
    {
        if (t == 5) continue;                                  // Dynamic: trajectory, below
        Spec s[EQ::kNumChars];
        for (int c = 0; c < EQ::kNumChars; ++c) s[c] = transferOf (refPatch (t, c));
        double worst = 1e9; int wa = 0, wb = 1; double vsZero = 1e9; int wz = 1;
        for (int a = 0; a < EQ::kNumChars; ++a)
            for (int b = a + 1; b < EQ::kNumChars; ++b)
            { const double d = specDelta (s[a], s[b]); if (d < worst) { worst = d; wa = a; wb = b; } }
        for (int c = 1; c < EQ::kNumChars; ++c)
        { const double d = specDelta (s[0], s[c]); if (d < vsZero) { vsZero = d; wz = c; } }
        gate ((std::string (EQ::typeNames()[t]) + ": 8 Characters, closest pair > 1.5 dB").c_str(),
              worst > 1.5, fmt ("closest %.2f dB = ", worst) + EQ::charNames (t)[wa] + "/" + EQ::charNames (t)[wb]
                          + fmt (" · weakest vs default %.2f dB", vsZero) + " (" + EQ::charNames (t)[wz] + ")");
    }
    // Dynamic — the detector IS the physics, so it is measured on the ride trajectory.
    {
        Ride r[EQ::kNumChars];
        for (int c = 0; c < EQ::kNumChars; ++c) r[c] = rideOf (c);
        double worst = 1e9; int wa = 0, wb = 1;
        for (int a = 0; a < EQ::kNumChars; ++a)
            for (int b = a + 1; b < EQ::kNumChars; ++b)
            { const double d = std::sqrt (std::pow ((r[a].atk - r[b].atk) /  2.5, 2.0)
                                        + std::pow ((r[a].rel - r[b].rel) / 25.0, 2.0)
                                        + std::pow ((r[a].hi  - r[b].hi)  /  3.0, 2.0)
                                        + std::pow ((r[a].lo  - r[b].lo)  /  3.0, 2.0)
                                        + std::pow ((r[a].mid   - r[b].mid)   / 3.0, 2.0)
                                        + std::pow ((r[a].near1 - r[b].near1) / 3.0, 2.0)
                                        + std::pow ((r[a].near2 - r[b].near2) / 3.0, 2.0));
              if (d < worst) { worst = d; wa = a; wb = b; } }
        gate ("Dynamic: 8 Characters separate on the RIDE TRAJECTORY (gate 1.00)", worst >= 1.0,
              fmt ("closest %.2f = ", worst) + EQ::charNames (5)[wa] + "/" + EQ::charNames (5)[wb]);
        std::printf ("        %-14s %8s %8s %8s %8s %8s %8s %8s\n", "Character",
                     "atk ms", "rel ms", "hot dB", "quiet", "-26 dB", "-29 dB", "-23 dB");
        for (int c = 0; c < EQ::kNumChars; ++c)
            std::printf ("        %-14s %8.2f %8.1f %8.2f %8.2f %8.2f %8.2f %8.2f\n", EQ::charNames (5)[c],
                         r[c].atk, r[c].rel, r[c].hi, r[c].lo, r[c].mid, r[c].near1, r[c].near2);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
struct Sweep { double v[9]; double span; double worstRev; };
template <typename MK, typename MET>
Sweep sweepParam (MK mk, MET met)
{
    Sweep s; double lo = 1e9, hi = -1e9;
    for (int i = 0; i < 9; ++i)
    { const float t = (float) i / 8.0f; s.v[i] = met (transferOf (mk (t)));
      lo = std::min (lo, s.v[i]); hi = std::max (hi, s.v[i]); }
    s.span = hi - lo;
    const bool up = s.v[8] >= s.v[0];
    s.worstRev = 0;
    for (int i = 1; i < 9; ++i)
    { const double d = up ? (s.v[i-1] - s.v[i]) : (s.v[i] - s.v[i-1]);
      s.worstRev = std::max (s.worstRev, d); }
    return s;
}
std::string sweepRow (const Sweep& s)
{ std::string r; for (int i = 0; i < 9; ++i) r += fmt ("%.1f ", s.v[i]); return r; }

void sectionF()
{
    section ("F. Every param 0->100 %: monotonic, night-and-day, measured span (law 1)");
    struct Row { const char* nm; Sweep s; double minSpan; };
    std::vector<Row> rows;

    // FRONT
    // 🚨 fb422 — TWO ROWS, NEVER THEIR DIFFERENCE. The fb420 row measured
    //  atHz(8 kHz) - atHz(80 Hz), and a DIFFERENCE stays monotone while BOTH ends reverse
    //  in common mode — which is exactly what the sliding pivot did. §F3 gates six probe
    //  frequencies end-by-end; these two rows put the headline ends in the main table.
    rows.push_back ({ "Slant  LOW  end (80 Hz alone — must FALL)",
        sweepParam ([] (float t) { EQ::Params p = base(); p.f1 = t; return p; },
                    [] (const Spec& s) { return atHz (s, 80.0); }), 30.0 });
    rows.push_back ({ "Slant  TOP  end (8 kHz alone — must RISE)",
        sweepParam ([] (float t) { EQ::Params p = base(); p.f1 = t; return p; },
                    [] (const Spec& s) { return atHz (s, 8000.0); }), 30.0 });
    rows.push_back ({ "Air    (level at 19 kHz, Reach 9 kHz)",
        sweepParam ([] (float t) { EQ::Params p = base(); p.b7 = fN (3, 9000.0f); p.f2 = t; return p; },
                    [] (const Spec& s) { return atHz (s, 19000.0); }), 45.0 });
    rows.push_back ({ "Amount (max spectral deviation)",
        sweepParam ([] (float t) { EQ::Params p = refPatch (0); p.f3 = t; return p; },
                    [] (const Spec& s) { return specMsd (s); }), 25.0 });
    rows.push_back ({ "Mix    (depth of a Chisel notch)",
        sweepParam ([] (float t) { EQ::Params p = single (6, 0, 1, -30.0f, 550.0f, 0.75f); p.mix = t; return p; },
                    [] (const Spec& s) { return -specMin (s); }), 25.0 });
    // BACK
    rows.push_back ({ "Low Hz (log2 of the shelf's half-gain corner)",
        sweepParam ([] (float t) { EQ::Params p = single (0, 0, 0, 20.0f, 100.0f); p.b1 = t; return p; },
                    [] (const Spec& s) { return std::log2 (crossHz (s, 10.0)); }), 4.0 });
    rows.push_back ({ "Low    (level at 30 Hz)",
        sweepParam ([] (float t) { EQ::Params p = single (0, 0, 0, 0.0f, 100.0f); p.b2 = t; return p; },
                    [] (const Spec& s) { return atHz (s, 30.0); }), 45.0 });
    rows.push_back ({ "Body Hz(log of the peak frequency)",
        sweepParam ([] (float t) { EQ::Params p = single (0, 0, 1, 20.0f, 500.0f); p.b3 = t; return p; },
                    [] (const Spec& s) { return std::log2 (argMaxHz (s)); }), 4.0 });
    rows.push_back ({ "Body   (level at 550 Hz)",
        sweepParam ([] (float t) { EQ::Params p = single (0, 0, 1, 0.0f, 550.0f); p.b4 = t; return p; },
                    [] (const Spec& s) { return atHz (s, 550.0); }), 45.0 });
    rows.push_back ({ "Bite Hz(log of the peak frequency)",
        sweepParam ([] (float t) { EQ::Params p = single (0, 0, 2, 20.0f, 3000.0f); p.b5 = t; return p; },
                    [] (const Spec& s) { return std::log2 (argMaxHz (s)); }), 3.0 });
    rows.push_back ({ "Bite   (level at 3.13 kHz)",
        sweepParam ([] (float t) { EQ::Params p = single (0, 0, 2, 0.0f, 3130.0f); p.b6 = t; return p; },
                    [] (const Spec& s) { return atHz (s, 3130.0); }), 45.0 });
    rows.push_back ({ "Reach  (mean lift over 10-20 kHz, Air +30)",
        sweepParam ([] (float t) { EQ::Params p = single (0, 0, 3, 30.0f, 8000.0f); p.b7 = t; return p; },
                    [] (const Spec& s) { return meanInRange (s, 10000.0, 20000.0); }), 14.0 });

    for (auto& r : rows)
    {
        const bool ok = r.s.span >= r.minSpan && r.s.worstRev <= 0.08 * r.s.span + 0.35;
        gate (r.nm, ok, fmt2 ("span %.1f (need %.0f)", r.s.span, r.minSpan)
                       + fmt (" · worst reversal %.2f", r.s.worstRev));
        note ("   0->100 %: " + sweepRow (r.s));
    }

    // `Trait` (P8) is a different knob in every Type: seven sweeps, seven metrics.
    section ("F2. `Trait` — the P8 relabel, swept per Type on ITS OWN physics");
    struct SRow { int t; const char* nm; Sweep s; double need; };
    std::vector<SRow> sr;
    { Sweep sw; double lo = 1e9, hi = -1e9;
      for (int i = 0; i < 9; ++i)
      { sw.v[i] = std::log2 (std::max (0.004, bwSine (single (0, 0, 1, 20.0f, 550.0f, (float) i / 8.0f), 550.0)));
        lo = std::min (lo, sw.v[i]); hi = std::max (hi, sw.v[i]); }
      sw.span = hi - lo; sw.worstRev = 0;
      for (int i = 1; i < 9; ++i) sw.worstRev = std::max (sw.worstRev, sw.v[i] - sw.v[i-1]);
      sr.push_back ({ 0, "Surgical `Pinch`  (log2 bandwidth, sine-probed)", sw, 5.0 }); }
    sr.push_back ({ 1, "British  `Slope`  (shelf undershoot, dB)",
        sweepParam ([] (float t) { EQ::Params p = single (1, 0, 0, 24.0f, 90.0f, t); return p; },
                    [] (const Spec& s) { return minInRange (s, 110.0, 720.0); }), 4.0 });
    sr.push_back ({ 2, "American `Taper`  (log2 bandwidth at a SMALL +8 dB move)",
        sweepParam ([] (float t) { EQ::Params p = single (2, 0, 1, 8.0f, 550.0f, t); return p; },
                    [] (const Spec& s) { return std::log2 (std::max (0.05, bwOct (s))); }), 2.0 });
    sr.push_back ({ 3, "Passive  `Dip`    (Pultec scoop depth, dB)",
        sweepParam ([] (float t) { EQ::Params p = single (3, 0, 0, 18.0f, 60.0f, t); return p; },
                    [] (const Spec& s) { return minInRange (s, 150.0, 700.0); }), 4.0 });
    sr.push_back ({ 4, "Open     `Silk`   (level at 19 kHz, Air +20 @ Reach 9 kHz)",
        sweepParam ([] (float t) { EQ::Params p = single (4, 0, 3, 20.0f, 9000.0f, t); return p; },
                    [] (const Spec& s) { return atHz (s, 19000.0); }), 3.0 });
    sr.push_back ({ 5, "Dynamic  `Pivot`  (applied cut at a -26 dBFS program, dB)",
        sweepParam ([] (float t) { EQ::Params p = single (5, 0, 1, -24.0f, 550.0f, t); return p; },
                    [] (const Spec& s) { return atHz (s, 550.0); }), 12.0 });
    { Sweep sw; double lo = 1e9, hi = -1e9;
      for (int i = 0; i < 9; ++i)
      { sw.v[i] = std::log2 (std::max (0.004, bwSine (single (6, 0, 1, 20.0f, 550.0f, (float) i / 8.0f), 550.0)));
        lo = std::min (lo, sw.v[i]); hi = std::max (hi, sw.v[i]); }
      sw.span = hi - lo; sw.worstRev = 0;
      for (int i = 1; i < 9; ++i) sw.worstRev = std::max (sw.worstRev, sw.v[i] - sw.v[i-1]);
      sr.push_back ({ 6, "Chisel   `Sting`  (log2 bandwidth, sine-probed)", sw, 2.5 }); }
    for (auto& r : sr)
    {
        const bool ok = r.s.span >= r.need && r.s.worstRev <= 0.10 * r.s.span + 0.35;
        gate (r.nm, ok, fmt2 ("span %.2f (need %.2f)", r.s.span, r.need) + fmt (" · worst reversal %.2f", r.s.worstRev));
        note ("   0->100 %: " + sweepRow (r.s));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  🚨 F3 — `Slant` (front hero 1), GATED END BY END. THE fb421 BLOCKER.
//
//  What was wrong with the DEVICE: designOnePole(kind 5) called designShelf1(f0,-g,+g),
//  a one-pole shelf whose 0 dB crossing sits at f0 / 10^(gDb/20). The pivot therefore
//  SLID from 700 Hz down to 2.8 Hz as the knob opened, and everything the crossing swept
//  past reversed direction: 120 Hz fell to -4.75 dB at 65 % and then rose to +8.55 dB at
//  100 % — 13.31 dB of WRONG-WAY travel at Amount default, 37.3 dB at Amount 200 %.
//
//  What was wrong with the GATE: it measured atHz(8 kHz) - atHz(80 Hz). A DIFFERENCE is
//  blind to a COMMON-MODE reversal — both ends can walk back up together and the
//  difference keeps climbing. The old row read "span 40.4, worst reversal 0.00" while the
//  bass was travelling backwards by 13 dB. That number was true and useless.
//
//  This section therefore measures SIX probe frequencies, each ALONE, at two Amounts, and
//  prints the old difference metric beside them so the blindness is visible rather than
//  argued. Every probe below the 700 Hz pivot must fall monotonically; every probe above
//  it must rise monotonically; and the pivot itself must not move.
// ═════════════════════════════════════════════════════════════════════════════
void sectionF1()
{
    section ("F3. `Slant` — EACH END SEPARATELY (a difference cannot see a common-mode reversal)");
    static const double HZ[6] = { 80.0, 120.0, 300.0, 2000.0, 8000.0, 16000.0 };
    const double PIVOT = 700.0;                       // Character `Plain`'s pivot
    double worstWrongAll = -1.0; std::string worstWhere;
    double worstPivotAll = 0;

    for (int a = 0; a < 2; ++a)
    {
        const float amt = (a == 0 ? 0.5f : 1.0f);
        Spec sp[9];
        for (int i = 0; i < 9; ++i)
        { EQ::Params p = base(); p.f1 = (float) i / 8.0f; p.f3 = amt; sp[i] = transferOf (p); }

        std::printf ("\n        Amount %s — Slant 0 -> 100 %% (9 steps), dB at each probe\n",
                     a == 0 ? "100 %" : "200 %");
        std::printf ("        %-9s %7s %7s %7s %7s %7s %7s %7s %7s %7s | %8s %9s\n", "probe",
                     "0%","12%","25%","37%","50%","62%","75%","87%","100%","travel","wrong-way");
        for (int k = 0; k < 6; ++k)
        {
            double v[9]; for (int i = 0; i < 9; ++i) v[i] = atHz (sp[i], HZ[k]);
            const bool up = (HZ[k] > PIVOT);
            double wrong = 0;
            for (int i = 1; i < 9; ++i)
            { const double d = up ? (v[i-1] - v[i]) : (v[i] - v[i-1]);
              wrong = std::max (wrong, d); }
            std::printf ("        %6.0f Hz", HZ[k]);
            for (int i = 0; i < 9; ++i) std::printf (" %7.2f", v[i]);
            std::printf (" | %8.2f %9.2f\n", v[8] - v[0], wrong);
            if (wrong > worstWrongAll)
            { worstWrongAll = wrong;
              worstWhere = fmt ("%.0f Hz", HZ[k]) + (a == 0 ? " @ Amount 100 %" : " @ Amount 200 %"); }
        }
        // the pivot must not move: 700 Hz stays 0 dB for every Slant setting.
        double wp = 0; for (int i = 0; i < 9; ++i) wp = std::max (wp, std::fabs (atHz (sp[i], PIVOT)));
        worstPivotAll = std::max (worstPivotAll, wp);
        // the OLD metric, printed so its blindness is legible rather than asserted.
        double dmin = 1e9, dmax = -1e9, drev = 0, prev = 0;
        for (int i = 0; i < 9; ++i)
        { const double d = atHz (sp[i], 8000.0) - atHz (sp[i], 80.0);
          dmin = std::min (dmin, d); dmax = std::max (dmax, d);
          if (i) drev = std::max (drev, prev - d); prev = d; }
        note (fmt ("the fb420 metric (8 kHz MINUS 80 Hz): span %.1f dB", dmax - dmin)
              + fmt (", worst reversal %.2f dB  <- this is what stayed green", drev));
    }

    gate ("every Slant probe moves ONE WAY ONLY (wrong-way travel <= 0.30 dB)",
          worstWrongAll <= 0.30, fmt ("worst wrong-way travel %.2f dB at ", worstWrongAll) + worstWhere);
    gate ("the 700 Hz PIVOT does not move across the whole Slant sweep, both Amounts",
          worstPivotAll <= 0.50, fmt ("worst |response at the pivot| %.3f dB", worstPivotAll));
    note ("   the fixed-pivot seesaw is |H|^2 = (Gp^2 + s^2 t^2)/(s^2 Gp^2 + t^2), t = tan(pi f/fs):");
    note ("   exactly 1 at t = Gp for every gain s and every sample rate, and d|H|^2/ds has the");
    note ("   sign of (t^4 - Gp^4) — strictly down below the pivot, strictly up above it.");
    // the two Characters that MOVE the pivot must move it, and must still pivot cleanly.
    for (int c : { 5, 6 })
    {
        const double want = (c == 5 ? 150.0 : 3000.0);
        Spec sp[5]; double wp = 0, wrong = 0, prevLo = 0;
        for (int i = 0; i < 5; ++i)
        { EQ::Params p = base(); p.character = c; p.f1 = 0.5f + 0.125f * (float) i; sp[i] = transferOf (p);
          wp = std::max (wp, std::fabs (atHz (sp[i], want)));
          const double lo = atHz (sp[i], want / 4.0);
          if (i) wrong = std::max (wrong, lo - prevLo); prevLo = lo; }
        gate ((std::string ("Character `") + EQ::charNames (0)[c] + "` pivots at "
               + fmt ("%.0f Hz", want) + ", and pivots CLEANLY").c_str(),
              wp <= 0.6 && wrong <= 0.30,
              fmt2 ("|H| at the pivot <= %.3f dB · wrong-way below it %.2f dB", wp, wrong));
    }
}

void sectionG()
{
    section ("G. The Dynamic detector — calibration first, THEN the claim (probe-craft law)");
    // 🔬 CHECK YOUR OWN DETECTOR. The threshold is anchored at -26 dBFS, but the detector
    // reads a BAND-PASS of the program, which sits far below the full-band level. Print the
    // raw band envelope for a -26 dBFS pink-ish program: the engine's kDetCal constant is
    // set FROM this number, not guessed from literature (bible pitfall 9: a literature
    // threshold lands 26 dB wrong; a band-pass detector adds a second offset on top).
    {
        for (float rms : { 0.0056f, 0.05f, 0.25f })
        {
            const Spec s = transferOf (single (5, 0, 1, -24.0f, 550.0f), rms);
            note (fmt2 ("program %6.1f dBFS -> applied Body cut %7.2f dB",
                        20.0 * std::log10 (rms), atHz (s, 550.0)));
        }
    }
    {
        const Spec q = transferOf (single (5, 0, 1, -24.0f, 550.0f), 0.0056f);
        const Spec h = transferOf (single (5, 0, 1, -24.0f, 550.0f), 0.25f);
        const double d = std::fabs (atHz (h, 550.0) - atHz (q, 550.0));
        gate ("Dynamic rides at least 12 dB across a -45 -> -12 dBFS program", d >= 12.0,
              fmt ("%.2f dB of ride", d));
    }
    {   // R11 for the Dynamic type: at full gain the band swings the whole +-30 dB
        const Spec q = transferOf (single (5, 0, 1, -30.0f, 550.0f, 0.5f), 0.0056f);
        const Spec h = transferOf (single (5, 0, 1, -30.0f, 550.0f, 0.5f), 0.5f);
        const double d = std::fabs (atHz (h, 550.0) - atHz (q, 550.0));
        gate ("   ... and at the knob's maximum the swing is >= 24 dB", d >= 24.0,
              fmt ("%.2f dB between a whisper and a wall", d));
    }
    {   // `Upward` really does ride the other way, measured
        const Spec a = transferOf (single (5, 4, 1, -24.0f, 550.0f), 0.0056f);
        const Spec b = transferOf (single (5, 4, 1, -24.0f, 550.0f), 0.25f);
        const Spec c = transferOf (single (5, 0, 1, -24.0f, 550.0f), 0.0056f);
        const Spec d = transferOf (single (5, 0, 1, -24.0f, 550.0f), 0.25f);
        const double si = atHz (b, 550.0) - atHz (a, 550.0);
        const double sn = atHz (d, 550.0) - atHz (c, 550.0);
        gate ("Character `Upward` reverses the sign of the ride", si * sn < 0.0,
              fmt2 ("normal %+.2f dB/level · inverted %+.2f dB/level", sn, si));
    }
}

void sectionH()
{
    section ("H. MONO-SAFE (law 5) — folded, not assumed");
    double worst = 0; int wt = 0;
    for (int t = 0; t < EQ::kNumTypes; ++t)
    {
        const Spec st = transferStereo (refPatch (t), 0, true);
        const Spec mo = transferStereo (refPatch (t), 2, true);
        const double d = specMedian (st, mo);
        if (d > worst) { worst = d; wt = t; }
    }
    gate ("Focus Stereo: the effect SURVIVES a mono fold on every Type", worst < 0.2,
          fmt ("worst median |stereo - mono| %.4f dB (", worst) + EQ::typeNames()[wt] + ")");

    // every Character of every Type, folded
    { double w = 0; std::string wn;
      for (int t = 0; t < EQ::kNumTypes; ++t)
        for (int c = 0; c < EQ::kNumChars; c += 3)
        { const double d = specMedian (transferStereo (refPatch (t, c), 0, true),
                                      transferStereo (refPatch (t, c), 2, true));
          if (d > w) { w = d; wn = std::string (EQ::typeNames()[t]) + "/" + EQ::charNames (t)[c]; } }
      gate ("   ... and on a Character sample across all 7 Types", w < 0.2, fmt ("worst median %.4f dB (", w) + wn + ")"); }

    // 🚨 THE TAGGED EXCEPTION. Focus = Side processes the DIFFERENCE only, so a mono fold
    // cancels the entire wet path. Every M/S EQ ever built has this; it is measured here
    // and it is a BADGE on the dropdown, not a bug and not a secret.
    {
        EQ::Params p = refPatch (0); p.axis = 2;
        const Spec sm = transferStereo (p, 2, true);     // Side focus, folded to mono
        const Spec st = transferStereo (refPatch (0), 2, true);
        gate ("Focus Side is mono-HOSTILE and is TAGGED (the wet vanishes on a fold)",
              EQ::focusIsMonoHostile (2) && specMsd (sm) < 0.5,
              fmt2 ("folded Side-focus leaves %.3f dB of EQ where Stereo focus leaves %.1f dB",
                    specMsd (sm), specMsd (st)));
        EQ::Params q = refPatch (0); q.axis = 1;
        const Spec qm = transferStereo (q, 2, true);
        gate ("   ... while Focus Mid is fully mono-SAFE", specMedian (qm, st) < 0.2,
              fmt ("Mid-focus mono fold matches Stereo focus within %.4f dB median", specMedian (qm, st)));
    }
}

void sectionI()
{
    section ("I. MIX 100 % = FULLY WET, ZERO DRY (law 3) — sine probes, not band averages");
    // The honest probe for a FILTER whose wet CONTAINS the dry: put a hole where the dry
    // has full energy and look in the hole. Chisel's notch is designed to -90 dB, so a dry
    // leak anywhere above -70 dB would fill it and be unmissable.
    { const double fn = snapHz (550.0);
      const double d = sineDb (single (6, 0, 1, -30.0f, (float) fn, 0.75f), fn, 0.05f, 65536);
      gate ("dry residual at Mix 100 %: a -90 dB notch stays a hole", d < -70.0,
            fmt2 ("measured %.1f dB in the notch => dry residual < %.0f dB", d, d)); }
    for (int t = 0; t < EQ::kNumTypes; ++t)
    { const double fn = snapHz (550.0);
      const double d = sineDb (single (t, 0, 1, -30.0f, (float) fn, t == 6 ? 0.75f : 0.5f), fn,
                               t == 5 ? 0.25f : 0.05f, 65536);
      gate ((std::string ("Mix 100 % leaks no dry: ") + EQ::typeNames()[t]).c_str(), d < -18.0,
            fmt ("deepest cut reaches %.1f dB", d)); }
    note ("Open's floor is its own tanh knee (22 dB), not a dry leak: a -30 dB knob becomes");
    note ("-19.3 dB BY DESIGN in that Type. Chisel's -90 dB hole above is the real dry gate.");
    { const double fn = snapHz (550.0);
      EQ::Params p = single (6, 0, 1, -30.0f, (float) fn, 0.75f); p.mix = 0.5f;
      const double d = sineDb (p, fn, 0.05f, 65536);
      gate ("at Mix 50 % the dry sits at exactly -6 dB (LINEAR law, not equal-power)",
            std::fabs (d + 6.0) < 0.6, fmt ("measured %.2f dB (linear crossfade predicts -6.02)", d)); }
    { const double fn = snapHz (550.0);
      EQ::Params p = single (0, 0, 1, 24.0f, (float) fn); p.mix = 0.5f;
      const double d = sineDb (p, fn, 0.05f, 65536);
      gate ("   ... and a +24 dB boost at Mix 50 % lands where linear says it must",
            std::fabs (d - 20.0 * std::log10 (0.5 + 0.5 * std::pow (10.0, 24.0 / 20.0))) < 0.6,
            fmt2 ("measured %.2f dB, linear predicts %.2f dB", d,
                  20.0 * std::log10 (0.5 + 0.5 * std::pow (10.0, 24.0 / 20.0)))); }
}

// ── THE CLICK GATE. Two things had to be fixed before this number meant anything.
//  (1) 🌾 fb416: it is NOT an outlier hunt. A zipper lasts the whole sweep and lives in the
//      BULK of the distribution, so the metric is the SECOND DIFFERENCE of the frame-level
//      curve: smooth motion has a small second difference no matter how large the motion is,
//      and a discontinuity spikes it.
//  (2) 🔬 the probe had to change. With a harmonic chord, a deep notch sweeping ACROSS a
//      harmonic legitimately swings a 64-sample frame by 28 dB - that is the effect working,
//      not a click, and the first version of this gate called it a failure. White noise has
//      no spectral features to sweep across, so every frame-level change is the filter's
//      broadband gain and nothing else.
//  (3) 🔬 AND THE CONTROL HAD TO BECOME PER-PARAM. A single global control number was
//      wrong: with the Low shelf at +18 dB the output is LF-dominated, a 256-sample frame
//      holds half a cycle of 90 Hz, and the frame level bounces +-4 dB with NOTHING moving.
//      The first version of this gate reported an 11.03 dB "click" on Slant at t = 0.251 s -
//      a quarter of a second BEFORE the sweep started. Every param is therefore compared
//      against itself HELD at the same extreme, so the question asked is the right one:
//      does MOVING this control add anything that HOLDING it does not.
//  mode: 0 sweep · 1 instant jump · 2 hold at 0 % · 3 hold at 100 %
double clickJump (int which, int mode, int type = 0)
{
    const int N = (int) (FS * 2.5f);
    auto x = whiteN (N, 0.05f, 31337u);
    std::vector<float> L (x), R (x);
    EQ e; e.prepare ((double) FS, 128);
    EQ::Params p = refPatch (type);
    const int t0 = (int) (FS * 0.5f), t1 = (int) (FS * 2.0f);
    for (int i = 0; i < N; i += 128)
    {
        const int nn = std::min (128, N - i);
        float u = (float) (i - t0) / (float) (t1 - t0); u = std::max (0.0f, std::min (1.0f, u));
        if (mode == 1) u = (i >= t0 ? 1.0f : 0.0f);
        if (mode == 2) u = 0.0f;
        if (mode == 3) u = 1.0f;
        switch (which)
        { case 0: p.b1 = u; break; case 1: p.b2 = u; break; case 2: p.b3 = u; break;
          case 3: p.b4 = u; break; case 4: p.b5 = u; break; case 5: p.b6 = u; break;
          case 6: p.b7 = u; break; case 7: p.b8 = u; break; case 8: p.f1 = u; break;
          case 9: p.f2 = u; break; case 10: p.f3 = u; break; case 11: p.mix = u; break;
          case 12: p.type = (u > 0.5f ? 6 : 0); break;
          case 13: p.character = (u > 0.5f ? 5 : 0); break;
          case 14: p.axis = (u > 0.5f ? 2 : 0); break; default: break; }
        e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], nn);
    }
    std::vector<double> db;
    for (int i = 0; i + 256 < N; i += 256)
    { double s2 = 0; for (int k = 0; k < 256; ++k) s2 += (double) L[(size_t)(i+k)] * L[(size_t)(i+k)];
      db.push_back (10.0 * std::log10 (std::max (1e-20, s2 / 256.0))); }
    double worst = 0;
    for (size_t i = 1; i + 1 < db.size(); ++i)
    { if (db[i] < -90.0) continue;
      worst = std::max (worst, std::fabs (db[i+1] - 2.0 * db[i] + db[i-1])); }
    return worst;
}

// ═════════════════════════════════════════════════════════════════════════════
//  🚨 fb422 — THE SAMPLE-LEVEL CLICK METRIC. WHY THE fb420 ONE COULD NOT FAIL.
//
//  The old gate measured the SECOND DIFFERENCE of a 256-sample frame-level curve, and
//  compared MOVING against HOLDING. Both choices made it an UPPER bound on a quantity that
//  a broken engine makes SMALLER, not larger:
//    * a 256-sample frame is 5.3 ms. Deleting all 11 smoothers does not make the sweep
//      jump — `setParams` is per block, so the param still walks in 128-sample steps of
//      1/562 of its range. That staircase is invisible to a 5.3 ms frame average.
//    * with the smoothers gone the frame curve gets FLATTER, not rougher, because the
//      coefficient glide is what smears each block's step across the frame boundary.
//    * and the SWITCH bar was 20 dB, while the gutted build's switch artefact was 18.6 dB.
//  Result: 104 pass / 0 FAIL on an engine with every smoother, the coefficient glide, the
//  Mix smoother and the whole fade-swap deleted.
//
//  THE REPLACEMENT measures the artefact itself, per sample, with a phase-independent
//  identity. A single sinusoid satisfies  y[n] - 2cos(w) y[n-1] + y[n-2] = 0  EXACTLY, and
//  an LTI filter's output of a sinusoid is still a sinusoid, so a SETTLED engine has a
//  residual of zero to float rounding no matter what the filter is doing. Writing
//  y[n] = g[n]·A·sin(wn+phi) and expanding gives
//        r[n] = A·( 2 g'[n] sin(w) cos(theta) + g''[n] sin(theta) ),
//  so  |r| / (2 sin(w) · envelope)  IS the per-sample fractional change of the wet gain.
//  Reported in dB as 20 log10(1 + that).
//
//  THE LAW, stated as a number: NO SINGLE SAMPLE MAY CHANGE THE WET GAIN BY MORE THAN
//  0.5 dB. A 0.5 dB/sample slew sustained is a 10 ms fade; a step bigger than that is the
//  textbook audible gain discontinuity. It is an ABSOLUTE bar, not a relative one, so
//  deleting a smoothing mechanism can only make it worse.
//
//  AND THE POLARITY IS FIXED. The fb420 file used an INSTANTANEOUS param jump as a
//  "negative control required to FAIL". That is backwards: an instantaneous param jump is
//  what a host automation write and a preset recall actually do, and absorbing it is
//  precisely what the 11 smoothers are FOR. It is now a gate the engine must PASS.
//
//  Three probe frequencies, deliberately incommensurate with the 128-sample block, so the
//  jump instant lands at a different phase of each (the fb421 Compress blocker: a gain step
//  at a zero crossing produces no sample-to-sample jump at all).
// ═════════════════════════════════════════════════════════════════════════════
// mode: 0 sweep 0.6 -> 1.4 s · 1 INSTANT jump at 0.6 s · 2 hold 0 % · 3 hold 100 %
//  🔬 CHECK YOUR OWN DETECTOR (§3.1). The first build of this probe read 15.58 dB/sample
//  on a HELD knob — the control that must read zero. It was not measuring the knob: at
//  `Trait` 100 % the Surgical Q law is x40, and a +18 dB band at Q 36 has a POLE Q of 101,
//  which rings for 2.5 s. The probe was measuring its own start-up transient. Fixed the
//  way the fault actually was: a 0.4 s raised-cosine fade-in on the tone (so the resonances
//  are never impulsed) and a 2.4 s settle before the first sample is read. The held control
//  is printed on every run and is the proof that this is fixed and stays fixed.
double slewDb (int which, int mode, double hz, int type = 0)
{
    const int N = (int) (FS * 4.0f);
    const int t0 = (int) (FS * 2.6f), t1 = (int) (FS * 3.4f);
    auto x = toneN (N, (float) hz, 0.25f);
    { const int fi = (int) (FS * 0.4f);
      for (int i = 0; i < fi; ++i) x[(size_t)i] *= (float) (0.5 - 0.5 * std::cos (3.14159265358979 * i / fi)); }
    std::vector<float> L (x), R (x);
    EQ e; e.prepare ((double) FS, 128);
    EQ::Params p = refPatch (type);
    for (int i = 0; i < N; i += 128)
    {
        const int nn = std::min (128, N - i);
        float u = (float) (i - t0) / (float) (t1 - t0); u = std::max (0.0f, std::min (1.0f, u));
        if (mode == 1) u = (i >= t0 ? 1.0f : 0.0f);
        if (mode == 2) u = 0.0f;
        if (mode == 3) u = 1.0f;
        switch (which)
        { case 0: p.b1 = u; break; case 1: p.b2 = u; break; case 2: p.b3 = u; break;
          case 3: p.b4 = u; break; case 4: p.b5 = u; break; case 5: p.b6 = u; break;
          case 6: p.b7 = u; break; case 7: p.b8 = u; break; case 8: p.f1 = u; break;
          case 9: p.f2 = u; break; case 10: p.f3 = u; break; case 11: p.mix = u; break;
          case 12: p.type      = (u > 0.5f ? 6 : 0); break;
          case 13: p.character = (u > 0.5f ? 5 : 0); break;
          case 14: p.axis      = (u > 0.5f ? 2 : 0); break; default: break; }
        e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], nn);
    }
    const double w = 6.283185307179586 * hz / (double) FS;
    const double c = std::cos (w), sw = std::sin (w);
    const int per = std::max (8, (int) std::lround ((double) FS / hz));
    const int nb = N / per + 2;
    std::vector<double> amp ((size_t) nb, 0.0);
    for (int b = 0; b * per < N; ++b)
    { double s2 = 0; int n = 0;
      for (int k = b * per; k < std::min (N, (b + 1) * per); ++k) { s2 += (double) L[(size_t)k] * L[(size_t)k]; ++n; }
      amp[(size_t) b] = n ? std::sqrt (2.0 * s2 / n) : 0.0; }
    const double A = 0.25 * 1.4142135623730951;
    const double floorA = A * 0.02;               // -34 dB: a step down there is masked anyway
    double worst = 0;
    for (int i = (int) (FS * 2.4f); i < N; ++i)
    {
        const double r = (double) L[(size_t)i] - 2.0 * c * (double) L[(size_t)(i-1)] + (double) L[(size_t)(i-2)];
        const int b = i / per;
        double env = amp[(size_t) b];
        if (b > 0)      env = std::max (env, amp[(size_t)(b-1)]);
        if (b + 1 < nb) env = std::max (env, amp[(size_t)(b+1)]);
        env = std::max (env, floorA);
        worst = std::max (worst, std::fabs (r) / (2.0 * sw * env));
    }
    return 20.0 * std::log10 (1.0 + worst);
}
double slewWorst (int which, int mode)
{
    double m = 0;
    for (double hz : { 137.0, 953.0, 5231.0 }) m = std::max (m, slewDb (which, mode, hz));
    return m;
}

// ── the fade-swap's own SIGNATURE. A switch must produce a DIP and never a BLAST. ──
//  The gutted build reads +18.62 dB where the real one reads a dip; a bar of "<= 20 dB"
//  could not tell those apart because it graded the MAGNITUDE of an excursion and not its
//  SIGN. This measures both signs against the settled levels on BOTH sides of the switch,
//  so a Type whose new curve is simply louder does not read as a blast.
struct SwProf { double pre, post, dip, rise; };
SwProf switchProfile (int which)
{
    const int N = (int) (FS * 2.6f), t0 = (int) (FS * 1.6f);
    auto x = whiteN (N, 0.05f, 31337u);
    std::vector<float> L (x), R (x);
    EQ e; e.prepare ((double) FS, 128);
    EQ::Params p = refPatch (0);
    for (int i = 0; i < N; i += 128)
    {
        const int nn = std::min (128, N - i); const bool on = (i >= t0);
        if (which == 12) p.type      = on ? 6 : 0;
        if (which == 13) p.character = on ? 5 : 0;
        if (which == 14) p.axis      = on ? 2 : 0;
        e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], nn);
    }
    std::vector<double> db; std::vector<int> at;
    for (int i = 0; i + 256 < N; i += 256)
    { double s2 = 0; for (int k = 0; k < 256; ++k) s2 += (double) L[(size_t)(i+k)] * L[(size_t)(i+k)];
      db.push_back (10.0 * std::log10 (std::max (1e-20, s2 / 256.0))); at.push_back (i); }
    auto med = [&] (double a, double b)
    { std::vector<double> v;
      for (size_t k = 0; k < db.size(); ++k) if (at[k] >= a * FS && at[k] < b * FS) v.push_back (db[k]);
      std::sort (v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size()/2]; };
    SwProf s2p;
    s2p.pre  = med (1.00, 1.55);
    s2p.post = med (1.90, 2.55);
    double lo = 1e9, hi = -1e9;
    for (size_t k = 0; k < db.size(); ++k)
        if (at[k] >= 1.58 * FS && at[k] < 1.86 * FS) { lo = std::min (lo, db[k]); hi = std::max (hi, db[k]); }
    s2p.dip  = lo - std::min (s2p.pre, s2p.post);
    s2p.rise = hi - std::max (s2p.pre, s2p.post);
    return s2p;
}

void sectionJ()
{
    section ("J. NO CLICKS (law 4) — sample-level, absolute-bar, and it fails under mutation");
    static const char* nm[15] = { "Low Hz", "Low", "Body Hz", "Body", "Bite Hz", "Bite", "Reach",
                                  "Trait", "Slant", "Air", "Amount", "Mix", "Type switch",
                                  "Character switch", "Focus switch" };

    // ── J1. the frame-level sweep metric, KEPT. It is a real (if weak) zipper detector and
    //    it is the one number that is comparable to the fb420 log. It is no longer the
    //    only thing standing between this device and a click.
    {
        double worst = -1e9; int wi = 0; double ctrlAt = 0;
        for (int i = 0; i < 12; ++i)
        { const double sweep = clickJump (i, 0);
          const double ctrl  = std::max (clickJump (i, 2), clickJump (i, 3));
          if (sweep - ctrl > worst) { worst = sweep - ctrl; wi = i; ctrlAt = ctrl; } }
        gate ("J1 frame-level: sweeping adds <= 3 dB over holding (all 12 continuous)",
              worst <= 3.0, fmt2 ("worst excess %+.2f dB (held control %.2f) on ", worst, ctrlAt) + nm[wi]);
    }

    // ── J2. THE REAL LAW-4 GATE: the per-sample wet-gain slew.
    //    Run four ways per param: a 0.8 s SWEEP, an INSTANT jump (host automation / preset
    //    recall — the case the 11 smoothers exist for), and both HELD extremes as control.
    {
        double sw[12], jp[12], hd[12];
        std::printf ("        %-10s %10s %10s %10s %10s   (dB of wet-gain change in ONE sample)\n",
                     "param", "swept", "JUMPED", "held", "jump-swp");
        for (int i = 0; i < 12; ++i)
        {
            sw[i] = slewWorst (i, 0); jp[i] = slewWorst (i, 1);
            hd[i] = std::max (slewWorst (i, 2), slewWorst (i, 3));
            std::printf ("        %-10s %10.4f %10.4f %10.4f %+10.4f\n", nm[i], sw[i], jp[i], hd[i], jp[i] - sw[i]);
        }
        { double w = 0; int k = 0; for (int i = 0; i < 12; ++i) if (hd[i] > w) { w = hd[i]; k = i; }
          gate ("J2 control: a HELD param changes the wet gain by ~0 per sample", w <= 0.20,
                fmt ("worst held slew %.4f dB/sample on ", w) + nm[k]); }
        // the 11 params that do not touch the Q law get an ABSOLUTE bar. 1 dB in one sample
        // is 12 % of amplitude; on the -26 dBFS bus that is a -44 dBFS impulse, ~26 dB under
        // the programme that carries it and inside its own masking skirt.
        { double w = 0; int k = 0; for (int i = 0; i < 12; ++i) if (i != 7)
          { const double v = std::max (sw[i], jp[i]); if (v > w) { w = v; k = i; } }
          gate ("J2 the 11 non-Q params: <= 1.0 dB of wet-gain change in ONE sample, swept OR jumped",
                w <= 1.0, fmt ("worst %.4f dB/sample on ", w) + nm[k]); }
        // 🔑 `Trait` is the Q LAW: 0.25x -> 40x. Retuning a Q 40 resonator releases its
        //   STORED ENERGY, and the energy is the same however slowly you glide — measured:
        //   at tau = 20 / 60 / 150 ms the number reads 7.20 / 7.97 / 7.50 dB. It is physics,
        //   not a missing smoother, and lengthening the smoother is exactly the constant-
        //   tuning FIXES.md §4 forbids. What the smoothers ARE responsible for is that an
        //   instantaneous jump costs no more than a deliberate sweep — which is the gate.
        { double w = -1e9; int k = 0; for (int i = 0; i < 12; ++i) if (jp[i] - sw[i] > w) { w = jp[i] - sw[i]; k = i; }
          gate ("J2 the SMOOTHERS' job: an INSTANT jump costs <= 3 dB more than a deliberate sweep",
                w <= 3.0, fmt ("worst jump-minus-sweep %+.4f dB/sample on ", w) + nm[k]); }
        note (fmt ("   `Trait` (the Q law) swept %.2f dB/sample — a Q 40 resonator releasing its", sw[7])
              + fmt (" stored energy; jumped %.2f dB/sample, i.e. the jump is free.", jp[7]));
    }

    // ── J3. the three SWITCHES: the fade-swap must leave its own signature.
    //    🚨 the fb420 bar was `|excursion| <= 20 dB` and the gutted build read 18.62 dB, so
    //    it passed. The bar graded the MAGNITUDE of an excursion and not its SIGN. A
    //    fade-swap DIPS. Deleting it makes the switch BLAST. Both signs are gated now,
    //    against the settled level on BOTH sides, so a Type whose new curve is simply louder
    //    (Chisel is +19.5 dB louder here) does not read as a blast.
    {
        double wRise = -1e9, shallow = -1e9, deepest = 1e9; int iR = 12, iSh = 12;
        std::printf ("        %-18s %8s %8s %8s %8s\n", "switch", "pre dB", "post dB", "dip dB", "rise dB");
        for (int i = 12; i < 15; ++i)
        {
            const SwProf q = switchProfile (i);
            std::printf ("        %-18s %8.2f %8.2f %8.2f %8.2f\n", nm[i], q.pre, q.post, q.dip, q.rise);
            if (q.rise > wRise)  { wRise = q.rise; iR = i; }
            if (q.dip  > shallow) { shallow = q.dip; iSh = i; }
            deepest = std::min (deepest, q.dip);
        }
        gate ("J3 every switch DIPS — the fade-swap actually ran: dip <= -8 dB",
              shallow <= -8.0, fmt ("shallowest dip %.2f dB on ", shallow) + nm[iSh]);
        gate ("J3 and no switch BLASTS: rise <= +2 dB over the settled level on EITHER side",
              wRise <= 2.0, fmt ("worst rise %+.2f dB on ", wRise) + nm[iR]);
        note (fmt ("   deepest dip %.1f dB — that is the 8 ms fade, doing its job.", deepest));
    }

    { EQ e; e.prepare ((double) FS, 128);
      auto x = chordN (8192); std::vector<float> L (x), R (x);
      EQ::Params p = base(); double w = 0;
      for (int i = 0; i < 8192; i += 128)
      { p.type = (i > 4096 ? 6 : 0); e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
      for (int i = 0; i < 8192; ++i) w = std::max (w, (double) std::fabs (L[(size_t)i] - x[(size_t)i]));
      gate ("a Type switch on a FLAT device stays bit-exact (no pointless dip)", w == 0.0,
            fmt ("worst delta %.3e", w)); }
}

void sectionK()
{
    section ("K. THE R11 CEILING GATE — 'if it sounds usable at 100 %, that is not what we want'");
    note ("METRIC (boosts): MSD = max |transfer| in dB over the 96 log bins, measured on the");
    note ("       OUTPUT spectrum of white noise through the real 128-sample block path.");
    note ("METRIC (cuts): a coherent SINE at the notch centre — a band-averaged spectrum");
    note ("       physically cannot see a hole narrower than its own analysis band.");
    note ("THRESHOLDS, defended: a mixing EQ tops out at 12-18 dB, and every console ever");
    note ("       built tops out at 16. 28 dB from ONE knob at Amount 100 % is already past");
    note ("       all of them; 55 dB at Amount 200 % is a factor of 560 on one band. A cut");
    note ("       must reach -60 dB: at that depth the band is GONE, not 'reduced'.");

    // 🔬 the legible-scale control: the move Max already agrees is obvious.
    { const Spec s = transferOf (single (0, 0, 0, 12.0f, 90.0f));
      note (fmt ("CONTROL (a plain, obvious +12 dB Low shelf): MSD = %.1f dB", specMsd (s))); }
    // 🪤 NEGATIVE CONTROL: the metric must read ~0 on a flat device or it is measuring itself.
    { const Spec s = transferOf (base());
      gate ("the ceiling metric reads ~0 on a FLAT device (control)", specMsd (s) < 0.5,
            fmt ("MSD %.3f dB", specMsd (s))); }

    { const Spec s = transferOf (single (0, 0, 0, 30.0f, 90.0f));
      gate ("ONE band at 100 %, Amount 100 %: MSD >= 28 dB", specMsd (s) >= 28.0, fmt ("MSD %.1f dB", specMsd (s))); }
    { EQ::Params p = single (0, 0, 0, 30.0f, 90.0f); p.f3 = 1.0f;
      const Spec s = transferOf (p);
      gate ("ONE band at 100 %, Amount 200 %: MSD >= 55 dB", specMsd (s) >= 55.0,
            fmt2 ("MSD %.1f dB = a factor of %.0f on one band", specMsd (s), std::pow (10.0, specMsd (s) / 20.0))); }
    { const double fn = snapHz (550.0);
      const double d = sineDb (single (6, 0, 1, -30.0f, (float) fn, 0.75f), fn, 0.05f, 65536);
      gate ("a cut at 100 % is a HOLE: <= -70 dB", d <= -70.0, fmt ("floor %.1f dB (sine probe)", d));
      EQ::Params p = single (6, 0, 1, -30.0f, (float) fn, 0.75f); p.f3 = 1.0f;
      const double d2 = sineDb (p, fn, 0.05f, 65536);
      gate ("   ... and Amount 200 % drives it past -85 dB", d2 <= -85.0, fmt ("floor %.1f dB", d2)); }
    // 🚨 fb422 — R11 FOR `Slant`, RE-DERIVED. The fb420 gate asked for "spread >= 28 dB AND
    //  +44 dB of lift" on the WHOLE curve. Both halves of that were wrong. `spread` is a
    //  DIFFERENCE (§F1); and `+44 dB of lift` was only ever reachable because the sliding
    //  pivot dragged the entire curve upward — it was measuring the BUG. A 6 dB/oct seesaw
    //  about a fixed pivot cannot lift 44 dB at 8 kHz and should never have been asked to:
    //  8 kHz is 3.5 octaves above 700 Hz, so 21 dB is the slope's arithmetic ceiling there.
    //  The R11 question for a tone-tilt is therefore asked PER END, in absolute dB, against
    //  the loudest thing the reference world offers: a Baxandall console tone control tops
    //  out at +-12 dB and a Pultec shelf at +-16. Each end alone must beat a whole console.
    {
        EQ::Params p1 = base(); p1.f1 = 1.0f;                       // Slant 100 %, Amount 100 %
        EQ::Params p2 = base(); p2.f1 = 1.0f; p2.f3 = 1.0f;         // Slant 100 %, Amount 200 %
        const Spec s1 = transferOf (p1), s2 = transferOf (p2);
        gate ("Slant 100 %: the TOP end ALONE beats a whole console (>= +15 dB at 8 kHz)",
              atHz (s1, 8000.0) >= 15.0,
              fmt2 ("%+.2f dB at 8 kHz, %+.2f dB at 16 kHz", atHz (s1, 8000.0), atHz (s1, 16000.0)));
        gate ("Slant 100 %: the LOW end ALONE beats a whole console (<= -15 dB at 80 Hz)",
              atHz (s1, 80.0) <= -15.0,
              fmt2 ("%+.2f dB at 80 Hz, %+.2f dB at 30 Hz", atHz (s1, 80.0), atHz (s1, 30.0)));
        gate ("   ... and Amount 200 % keeps BOTH ends past a console, top and bottom",
              atHz (s2, 8000.0) >= 15.0 && atHz (s2, 80.0) <= -15.0,
              fmt2 ("%+.2f dB at 8 kHz · %+.2f dB at 80 Hz", atHz (s2, 8000.0), atHz (s2, 80.0)));
        gate ("Slant 100 % / Amount 200 %: 20 Hz - 20 kHz spread >= 55 dB",
              specMax (s2) - specMin (s2) >= 55.0,
              fmt3 ("spread %.1f dB (%+.1f top, %+.1f bottom)",
                    specMax (s2) - specMin (s2), specMax (s2), specMin (s2)));
        note ("   HONEST LIMIT, stated rather than gated away: a 6 dB/oct seesaw is SLOPE-limited,");
        note ("   so Amount 200 % buys the ENDS very little (80 Hz: -17.6 -> -18.7 dB) and buys the");
        note ("   EXTREMES a lot (16 kHz: +23.3 -> +31.6 dB). Amount is not a plateau on this knob");
        note ("   because the pivot is what sets the ends — which is why `Deep Pivot` (150 Hz) and");
        note ("   `Bright Pivot` (3 kHz) exist and are gated in §F3.");
    }
    { EQ::Params p = base(); p.b7 = fN (3, 8000.0f); p.f2 = 1.0f; p.f3 = 1.0f;
      const Spec s = transferOf (p);
      gate ("Air at 100 % with Amount 200 % (Reach 8 kHz): >= 42 dB in band", specMax (s) >= 42.0,
            fmt ("+%.1f dB at 20 kHz; the shelf's asymptote is +60 dB, which a 8 kHz corner",
                 specMax (s)));
      note ("   reaches only above the audio band — 46.4 dB at 20 kHz is what the ANALOG"); 
      note ("   prototype itself delivers there, so this is the ceiling, not a shortfall."); }
    { const double fn = snapHz (550.0);
      EQ::Params p = base(); p.type = 6; p.character = 0; p.f3 = 1.0f;   // EVERY knob at 100 %
      p.b1 = fN (0, 90.0f);  p.b2 = 1.0f;
      p.b3 = fN (1, (float) fn); p.b4 = 0.0f;                            // the one deep cut
      p.b5 = fN (2, 3200.0f); p.b6 = 1.0f;
      p.b7 = fN (3, 12000.0f); p.f2 = 1.0f; p.b8 = 0.6f;
      const Spec s = transferOf (p);
      const double hole = sineDb (p, fn, 0.05f, 65536);
      gate ("all four bands at 100 % with Amount 200 %: boost >= 55 dB", specMax (s) >= 55.0,
            fmt2 ("peak %+.1f dB, deepest hole %+.1f dB", specMax (s), hole));
      gate ("   ... total dynamic range of ONE curve >= 120 dB", specMax (s) - hole >= 120.0,
            fmt ("%.0f dB from the top of the curve to the bottom", specMax (s) - hole)); }
    { EQ::Params p = single (0, 0, 1, 24.0f, 550.0f, 1.0f);   // `Pinch` at maximum
      const double bw = bwSine (p, 550.0);
      gate ("Surgical `Pinch` at 100 % is a resonator, not an EQ curve", bw < 0.10,
            fmt2 ("half-height width %.4f octaves (%.0f Hz wide at 550 Hz)", bw,
                  550.0 * (std::pow (2.0, bw * 0.5) - std::pow (2.0, -bw * 0.5)))); }
    { const double t = t60ms (single (6, 1, 1, 28.0f, 550.0f, 1.0f));
      gate ("Chisel `Sting` at 100 % SINGS: T60 > 250 ms", t > 250.0,
            fmt ("T60 %.0f ms — a struck resonator, not an equaliser", t)); }
}

void sectionL()
{
    section ("L. Stability — 60 s of full-drive white noise, every Type");
    const int N = (int) (FS * 60.0f);
    double worstPk = 0; int wt = 0; bool ok = true;
    for (int t = 0; t < EQ::kNumTypes; ++t)
    {
        EQ::Params p = refPatch (t, 7); p.f3 = 1.0f;               // Amount 200 %, worst Character
        EQ e; e.prepare ((double) FS, 128);
        std::vector<float> L (128), R (128);
        uint32_t st = 99991u; double pk = 0;
        for (int i = 0; i < N; i += 128)
        {
            for (int k = 0; k < 128; ++k)
            { st = st * 1664525u + 1013904223u;
              L[(size_t)k] = R[(size_t)k] = (((float) (st >> 8) / 8388608.0f) - 1.0f) * 0.5f; }
            e.setParams (p); e.processStereo (L.data(), R.data(), 128);
            for (int k = 0; k < 128; ++k)
            { const float v = L[(size_t)k];
              if (! std::isfinite (v)) ok = false;
              pk = std::max (pk, (double) std::fabs (v)); }
        }
        if (pk > worstPk) { worstPk = pk; wt = t; }
        if (pk > 1e7) ok = false;
    }
    gate ("60 s x 7 Types at -6 dBFS drive, Amount 200 %: finite, bounded", ok,
          fmt ("worst peak %.1f linear (", worstPk) + EQ::typeNames()[wt] + ")");
    note ("   a +60 dB band on a -6 dBFS drive IS supposed to reach ~500 linear. That is the");
    note ("   ceiling doing its job inside a float chain; downstream headroom is the mix's");
    note ("   problem and the manual's, not the EQ's (bible §7, no-playing-safe).");
    // a Q 90 ring is a LONG decay, not a denormal: it must reach EXACTLY zero, eventually.
    { EQ::Params p = single (6, 1, 1, 28.0f, 550.0f, 0.95f);
      EQ e; e.prepare ((double) FS, 128);
      std::vector<float> L (128), R (128);
      for (int b = 0; b < 40; ++b)
      { auto x = chordN (128, 0.2f); for (int k = 0; k < 128; ++k) L[(size_t)k] = R[(size_t)k] = x[(size_t)k];
        e.setParams (p); e.processStereo (L.data(), R.data(), 128); }
      double tail = 0; const int blocks = (int) (FS * 16.0f) / 128;
      for (int b = 0; b < blocks; ++b)
      { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        e.setParams (p); e.processStereo (L.data(), R.data(), 128);
        if (b > blocks * 3 / 4) for (int k = 0; k < 128; ++k) tail = std::max (tail, (double) std::fabs (L[(size_t)k])); }
      gate ("a Q 90 Chisel ring decays to TRUE zero (denormal flush)", tail == 0.0,
            fmt ("tail after 12 s of silence %.3e", tail)); }
    // 🔑 THE RING LAW, measured — and this is the gate that could not fail at fb420.
    //  Three things were wrong and all three are fixed here:
    //   1. the RULER. t60ms ran a 1.5 s window and saturated at 1500 ms against a 3100 ms
    //      bar, so 1500 < 3100 for every possible engine. Window is now 8 s and saturation
    //      is reported as a saturation.
    //   2. the COVERAGE. It only ever visited Amount's DEFAULT. The ring law is a statement
    //      about the pole radius, and the pole radius of a boost is set by A*Q — i.e. by the
    //      GAIN — so the ceiling is exactly where it has to be swept. Amount 200 % now runs.
    //   3. the STAGES. It only swept the four bands. The Slant is a one-pole whose corner is
    //      now placed by ARITHMETIC (f_corner = f_pivot * 10^(g/20)), which pushes its real
    //      pole toward z = 1 at extreme gain — a 0.6 Hz corner is a 1.7 s decay. That stage
    //      is in the sweep, and the one-pole G clamp (onePoleGmax) is what bounds it.
    { double worst = 0; std::string wn; int nSat = 0;
      for (int t = 0; t < EQ::kNumTypes; ++t)
        for (int ch : { 1, 7 })
          for (float amt : { 0.5f, 1.0f })
            for (float sh : { 0.5f, 0.95f, 1.0f })
              for (float hz : { 25.0f, 120.0f, 1000.0f })
              { EQ::Params p = single (t, ch, hz < 100.0f ? 0 : 1, 30.0f, hz, sh); p.f3 = amt;
                bool sat = false; const double v = t60ms (p, 8.0, &sat);
                if (sat) ++nSat;
                if (v > worst) { worst = v; wn = std::string (EQ::typeNames()[t]) + "/" + EQ::charNames (t)[ch]
                                                + fmt (" @ %.0f Hz", hz) + fmt (", Trait %.2f", sh)
                                                + (amt > 0.75f ? ", Amount 200 %" : ", Amount 100 %"); } }
      gate ("NOTHING in the device rings longer than 3 s (the pole-radius cap)", worst <= 3100.0 && nSat == 0,
            fmt ("longest T60 %.0f ms — ", worst) + wn + fmt (" · %.0f settings hit the window", (double) nSat));
      note ("   252 settings: 7 Types x 2 Characters x 2 Amounts x 3 Trait x 3 corner frequencies.");
      note ("   without the cap a +72 dB 20 Hz band at Q 90 has Q_pole = 5670 and a TEN MINUTE");
      note ("   T60: passive, terminating, and still a drone. 'Nothing free-runs' is a number."); }
    // and the SLANT's own pole, which the fb420 sweep never touched at all.
    { double worst = 0; std::string wn; bool anySat = false;
      for (int c = 0; c < EQ::kNumChars; ++c)
        for (float f1 : { 0.0f, 1.0f })
          for (float amt : { 0.5f, 1.0f })
          { EQ::Params p = base(); p.character = c; p.f1 = f1; p.f3 = amt;
            bool sat = false; const double v = t60ms (p, 8.0, &sat); anySat = anySat || sat;
            if (v > worst) { worst = v; wn = std::string (EQ::charNames (0)[c])
                                            + fmt (", Slant %.0f %%", f1 * 100.0)
                                            + (amt > 0.75f ? ", Amount 200 %" : ", Amount 100 %"); } }
      gate ("the SLANT's one-pole obeys the same ring law (its corner is now placed by gain)",
            worst <= 3100.0 && ! anySat, fmt ("longest T60 %.0f ms — ", worst) + wn); }
    // 🪤 THE RULER ITSELF, shown to be able to express a failure. This is the fb420 defect
    //    in one line: a saturating ruler shorter than the bar is a gate that cannot fail.
    { bool sat = false;
      const double v = t60ms (single (6, 1, 1, 30.0f, 550.0f, 1.0f), 0.020, &sat);
      gate ("(self-check) t60ms REPORTS saturation instead of a number it cannot measure",
            sat && v == 20.0, fmt2 ("a 20 ms window on a 700 ms ring returns %.0f ms, saturated=%.0f", v, (double) sat));
      note ("   the fb420 ruler was 1500 ms against a 3100 ms bar and returned 1500.0 silently."); }
}

void sectionM()
{
    section ("M. CPU — us per 128-sample block at 48 kHz (one block is 2666.7 us of audio)");
    const double blockUs = 128.0 / 48000.0 * 1e6;
    double worst = 0; int wt = 0; std::string rows;
    for (int t = 0; t < EQ::kNumTypes; ++t)
    {
        EQ::Params p = refPatch (t, 3);
        EQ e; e.prepare (48000.0, 128);
        std::vector<float> L (128, 0.01f), R (128, 0.01f);
        for (int i = 0; i < 200; ++i) { e.setParams (p); e.processStereo (L.data(), R.data(), 128); }
        const int NBLK = 12000;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < NBLK; ++i)
        { for (int k = 0; k < 128; ++k) { L[(size_t)k] = 0.01f * std::sin (0.03f * (k + i)); R[(size_t)k] = L[(size_t)k]; }
          e.setParams (p); e.processStereo (L.data(), R.data(), 128); }
        auto t1 = std::chrono::high_resolution_clock::now();
        const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / NBLK;
        rows += fmt ("%.2f ", us);
        if (us > worst) { worst = us; wt = t; }
    }
    gate ("worst Type, settled knobs, < 0.5 % of one core", worst / blockUs * 100.0 < 0.5,
          fmt2 ("%.2f us/block = %.3f %% of a core (", worst, worst / blockUs * 100.0) + EQ::typeNames()[wt] + ")");
    note ("   per Type (us/block, roster order): " + rows);
    // 🔑 the dirty flag means a SETTLED patch never redesigns. The honest worst case is a
    //    knob under the finger, where all 8 stages redesign every 32 samples AND both design
    //    candidates get scored against the analog prototype.
    {
        EQ::Params p = refPatch (6, 0);
        EQ e; e.prepare (48000.0, 128);
        std::vector<float> L (128, 0.01f), R (128, 0.01f);
        const int NBLK = 12000;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < NBLK; ++i)
        { p.b3 = 0.3f + 0.4f * (float) std::sin (i * 0.01);       // Body Hz under the finger
          p.b8 = 0.4f + 0.3f * (float) std::cos (i * 0.013);      // `Trait` too
          for (int k = 0; k < 128; ++k) { L[(size_t)k] = 0.01f * std::sin (0.03f * (k + i)); R[(size_t)k] = L[(size_t)k]; }
          e.setParams (p); e.processStereo (L.data(), R.data(), 128); }
        auto t1 = std::chrono::high_resolution_clock::now();
        const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / NBLK;
        gate ("worst case — TWO knobs moving every block — < 1.5 % of one core",
              us / blockUs * 100.0 < 1.5, fmt2 ("%.2f us/block = %.3f %% of a core", us, us / blockUs * 100.0));
        note (fmt ("   6 instances, all with knobs moving: %.2f %% of one core", us / blockUs * 600.0));
    }
}

void sectionN (const Feat* F48)
{
    section ("N. 44.1 kHz and 96 kHz — the device must be the SAME device");
    const Spec ref48 = F48[0].ref;
    for (float fs : { 44100.0f, 96000.0f })
    {
        FS = fs;
        // null first
        { EQ::Params p = base(); auto x = chordN (8192); std::vector<float> L (x), R (x);
          EQ e; e.prepare ((double) FS, 128);
          for (int i = 0; i < 8192; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
          double w = 0; for (int i = 0; i < 8192; ++i) w = std::max (w, (double) std::fabs (L[(size_t)i] - x[(size_t)i]));
          gate ((fmt ("%.1f kHz: defaults still null bit-exactly", fs / 1000.0)).c_str(), w == 0.0,
                fmt ("worst delta %.3e", w)); }
        const Spec s = transferOf (refPatch (0));
        double w = 0; for (int i = 0; i < NB; ++i)
        { const double f = EQ::curveBinHz (i); if (f < 25.0 || f > 18000.0) continue;
          w = std::max (w, std::fabs (s.db[i] - ref48.db[i])); }
        double wf = 0; for (int i = 0; i < NB; ++i)
        { const double f = EQ::curveBinHz (i); if (f < 25.0 || f > 18000.0) continue;
          if (std::fabs (s.db[i] - ref48.db[i]) >= w - 1e-9) { wf = f; break; } }
        gate ((fmt ("%.1f kHz: the reference patch matches 48 kHz within 2.0 dB", fs / 1000.0)).c_str(),
              w < 2.0, fmt2 ("worst |delta| %.2f dB at %.0f Hz", w, wf));
        // and the ceiling still lands
        { EQ::Params p = single (0, 0, 0, 30.0f, 90.0f); p.f3 = 1.0f;
          const Spec c = transferOf (p);
          gate ((fmt ("%.1f kHz: the R11 ceiling still reaches 55 dB", fs / 1000.0)).c_str(),
                specMsd (c) >= 55.0, fmt ("MSD %.1f dB", specMsd (c))); }
    }
    FS = 48000.0f;
}

// ═════════════════════════════════════════════════════════════════════════════
//  O. NAMES — the NO-DOUBLES gate the fb420 EQ never had.
//
//  fb420 shipped 23 collisions including `Tilt` and `Sculpt`, which are shipped TAPE FRONT
//  KNOBS. The dynamics agent had a gate; the EQ had none. This one is rebuilt per
//  RENAMES.md's own post-mortem: the extractor (eq/extract_labels.py) strips leading space
//  before its capitalisation test — so it sees the fb418 strings `" Motion"` / `" Route"`,
//  which the old pattern skipped — and it reads the two SIBLING fx4 directories as well as
//  Source/ and Design/fx3/. 3064 strings, against the dynamics agent's 1762.
//
//  The EQ's own labels come from EXACTLY ONE place: EQ::label(i), which walks
//  typeNames / frontNames / backNames / shapeName / focusNames / charNames, and charNames
//  is now DERIVED from charSpec().nm — the physics row itself. There is no second table to
//  drift from (fb420's charNames said `Fixed Top` while charSpec said `Iron Top`).
// ═════════════════════════════════════════════════════════════════════════════
struct Sanction { const char* name; const char* why; };
static const Sanction kSanctioned[] = {
    { "Low",    "RENAMES.md: band grammar, keep" },
    { "Body",   "RENAMES.md: band grammar, keep" },
    { "Bite",   "RENAMES.md: band grammar, keep" },
    { "Air",    "RENAMES.md: Max's mandate word, keep (precedence rule 3)" },
    { "Reach",  "RENAMES.md: sanctioned shared vocabulary, keep" },
    { "Amount", "RENAMES.md: sanctioned shared vocabulary, keep" },
    { "Mix",    "RENAMES.md: sanctioned shared vocabulary, keep" },
    { "Focus",  "RENAMES.md: sanctioned shared vocabulary, keep" },
    { "Tight",  "RENAMES.md EQUALIZER row: 'keep - Widen yields this one' (-> Tight Fan)" },
    { "Silk",   "RENAMES.md WIDEN row: Steady char 0 Silk -> Satin, 'EQ knob label outranks a Character'" },
    // fb423 SECOND RULING, §SANCTIONED. Ruled AS A GROUP, not one name at a time: "M/S routing
    // vocabulary, sanctioned wherever it appears (EQ Focus, OTT Stereo, any future device).
    // There is no synonym for these and inventing one would be worse than the duplication."
    // This is the ruling that resolves fb422's 121/1 - the gate was right to refuse to
    // self-rename them (FIXES.md §2) and right to fail loudly until one head ruled.
    { "Stereo", "RENAMES.md fb423 SANCTIONED: M/S routing vocabulary, ruled as a group" },
    { "Mid",    "RENAMES.md fb423 SANCTIONED: M/S routing vocabulary, ruled as a group" },
    { "Side",   "RENAMES.md fb423 SANCTIONED: M/S routing vocabulary, ruled as a group" },
    { "Left",   "RENAMES.md fb423 SANCTIONED: M/S routing vocabulary, ruled as a group" },
    { "Right",  "RENAMES.md fb423 SANCTIONED: M/S routing vocabulary, ruled as a group" },
    // fb425. `Character` is the CHASSIS word: back dropdown 1 is `Character` on all four fx4
    // devices and on all three fx3 devices before them (TerrainCompressFx.h:113 and
    // TerrainOttFx.h:114 publish it in their own dropdownNames()). It arrives in this gate
    // only because fb425 made this device publish its dropdown HEADINGS at all — it has been
    // printed on the card since fb420 and simply sat outside every gate. FIXES.md fb425, EQ
    // block: "Both words are already ruled (Focus is in CONTRACT §4 and in fb423 SANCTIONED;
    // Character is chassis), so this closes a coverage hole rather than opening a naming
    // question." Renaming it would give this one device a different word for the control
    // every other device calls `Character`, which is the opposite of the no-doubles law's aim.
    { "Character", "FIXES.md fb425 EQ block: chassis vocabulary, shared by all four fx4 devices" },
};
// every EQUALIZER row of RENAMES.md, as data: the old string must be GONE, the new one PRESENT.
//@RETIRED-OK-BEGIN kRenames: the RENAMES.md rows themselves — a rename table that may not
//  name the OLD string cannot state a rename. This is the one place in the file where a
//  retired name is the SUBJECT rather than a leak, and §P3 blanks exactly these lines.
struct Rename { const char* oldNm; const char* newNm; const char* slot; };
static const Rename kRenames[] = {
    { "Tilt",      "Slant",      "front hero 1"    }, { "Sculpt",   "Chisel",    "Type 6 pill"     },
    { "Shape",     "Trait",      "P8 slot name"    }, { "Width",    "Pinch",     "P8 @ Surgical"   },
    { "Bump",      "Slope",      "P8 @ British"    }, { "Grip",     "Taper",     "P8 @ American"   },
    { "Ring",      "Sting",      "P8 @ Chisel"     }, { "Sense",    "Pivot",     "P8 @ Dynamic"    },
    { "Clean",     "Plain",      "Surgical char 0" }, { "Carve",    "Scoop",     "Surgical char 4" },
    { "Console",   "Desk",       "British char 0"  }, { "Fixed Top","Iron Top",  "British char 3"  },
    { "Gentle",    "Mellow",     "American char 2" }, { "Modern",   "Revival",   "Passive char 7"  },
    { "Close Dip", "Close Cut",  "Passive char 1"  }, { "Far Dip",  "Far Cut",   "Passive char 2"  },
    { "Silky",     "Gloss",      "Open char 0"     }, { "Fast",     "Quick",     "Dynamic char 1"  },
    { "Slow",      "Lazy",       "Dynamic char 2"  }, { "Inverted", "Upward",    "Dynamic char 4"  },
    { "Gain Ring", "Gain Peak",  "Chisel char 3"   },
    // fb423 SECOND RULING - the 8 this device's own gate found and REFUSED to self-rename.
    { "Forward",   "Ahead",      "British char 2"  }, { "Runaway",  "Bolt",      "American char 7" },
    { "Program",   "Baseline",   "Passive char 0"  }, { "Stacked",  "Twin Shelf","Open char 3"     },
    { "Peak Hold", "Peak Keep",  "Dynamic char 7"  }, { "Razor",    "Scalpel",   "Chisel char 1"   },
    { "Telephone", "Handset",    "Chisel char 5"   }, { "Metal",    "Tin",       "Chisel char 7"   },
};
//@RETIRED-OK-END
bool hasLabel (const char* w)
{ for (int i = 0; i < EQ::kNumLabels; ++i) if (! std::strcmp (EQ::label (i), w)) return true; return false; }

void sectionO()
{
    const int nShipped = (int) (sizeof (kShippedLabels) / sizeof (kShippedLabels[0]));
    { char t[160]; std::snprintf (t, sizeof t,
        "O. NAMES — no doubles, inside the card and against %d shipped strings", nShipped);
      section (t); }
    note (fmt ("shipped_labels.inc holds %.0f strings from Source/ · Design/fx3/ · the two sibling", (double) nShipped)
          + fmt (" fx4 dirs; this device publishes %.0f.", (double) EQ::kNumLabels));

    // 0. the extractor must be able to see yesterday's labels, or it cannot protect tomorrow's.
    //@RETIRED-OK-BEGIN the extractor self-check: it PROVES the shipped-label list still contains
    //  the two Tape knobs fb420 collided with. Naming them is the whole assertion.
    { bool motion = false, route = false, tilt = false, sculpt = false;
      for (int k = 0; k < nShipped; ++k)
      { if (! std::strcmp (kShippedLabels[k], "Motion")) motion = true;
        if (! std::strcmp (kShippedLabels[k], "Route"))  route  = true;
        if (! std::strcmp (kShippedLabels[k], "Tilt"))   tilt   = true;
        if (! std::strcmp (kShippedLabels[k], "Sculpt")) sculpt = true; }
      gate ("(self-check) the extractor SEES the fb418 leading-space labels Motion / Route",
            motion && route, std::string ("Motion ") + (motion ? "yes" : "NO")
            + " · Route " + (route ? "yes" : "NO") + "  (the old pattern found neither)");
      gate ("(self-check) ... and the two shipped Tape knobs fb420 collided with",
            tilt && sculpt, std::string ("Tilt ") + (tilt ? "yes" : "NO")
            + " · Sculpt " + (sculpt ? "yes" : "NO") + "  — this is what the gate is FOR"); }
    //@RETIRED-OK-END

    // 1. every EQUALIZER row of RENAMES.md applied, verbatim.
    { int applied = 0; std::string bad;
      for (const Rename& r : kRenames)
      { const bool oldGone = ! hasLabel (r.oldNm), newHere = hasLabel (r.newNm);
        if (oldGone && newHere) ++applied;
        else bad += std::string (bad.empty() ? "" : ", ") + r.oldNm + "->" + r.newNm
                  + (oldGone ? "(new missing)" : "(old still present)"); }
      gate ("every EQUALIZER row of RENAMES.md is applied VERBATIM",
            applied == (int) (sizeof (kRenames) / sizeof (kRenames[0])),
            fmt2 ("%.0f of %.0f rows", (double) applied, (double) (sizeof (kRenames) / sizeof (kRenames[0])))
            + (bad.empty() ? "" : "  MISSING: " + bad)); }

    // 2. no doubles INSIDE the card.
    { std::string dup; int n = 0;
      for (int i = 0; i < EQ::kNumLabels; ++i)
        for (int j = i + 1; j < EQ::kNumLabels; ++j)
          if (! std::strcmp (EQ::label (i), EQ::label (j)))
          { ++n; dup += std::string (dup.empty() ? "" : ", ") + EQ::label (i)
                      + " (" + EQ::labelSlot (i) + " / " + EQ::labelSlot (j) + ")"; }
      gate ("no label appears twice INSIDE this card", n == 0,
            n ? dup : fmt ("0 doubles across %.0f published strings", (double) EQ::kNumLabels)); }

    // 3. no collision with anything shipped, except what RENAMES.md sanctions BY NAME.
    { std::string unresolved; int nColl = 0, nSanc = 0;
      for (int i = 0; i < EQ::kNumLabels; ++i)
      {
          bool coll = false;
          for (int k = 0; k < nShipped && ! coll; ++k) if (! std::strcmp (EQ::label (i), kShippedLabels[k])) coll = true;
          if (! coll) continue;
          ++nColl;
          bool ok = false;
          for (const Sanction& q : kSanctioned) if (! std::strcmp (q.name, EQ::label (i))) { ok = true; break; }
          if (ok) { ++nSanc; continue; }
          unresolved += std::string (unresolved.empty() ? "" : ", ") + EQ::label (i)
                      + " (" + EQ::labelSlot (i) + ")";
      }
      gate ("no collision with a shipped label outside RENAMES.md's sanctioned list",
            unresolved.empty(),
            fmt2 ("%.0f collisions, %.0f sanctioned by name; UNRESOLVED: ",
                  (double) nColl, (double) nSanc) + (unresolved.empty() ? "none" : unresolved));

      // 🔑 fb423: "a gate that can exempt itself is not a gate." The exemption list is pinned
      //  to the number it actually SPENDS, so a sanction can never be added ahead of a
      //  collision and quietly pre-authorise it. Any change to this number is a deliberate edit.
      //
      //  It is 9, and the 9 are: Low · Body · Air · Amount · Mix · Tight · Stereo · Mid ·
      //  Character. fb425 spent exactly ONE more, and only because this device started
      //  PUBLISHING its two dropdown headings: `Character` has been printed on the card since
      //  fb420 and was simply outside every gate until `dropdownNames()` existed. It is the
      //  chassis word — back dropdown 1 is `Character` on all four fx4 devices — so the
      //  ruling is a citation (FIXES.md fb425), not a new decision. Deliberate edit, stated.
      //  It was 10 until the corpus was re-extracted at fb423 and TWO exemptions came back
      //  UNSPENT — `Bite`, because OTT renamed its pill `Bite`->`Crest`, and `Silk`, because
      //  Widen renamed its Character `Silk`->`Satin`. Both are RENAMES.md rows landing in a
      //  sibling's engine, so the yield they bought is no longer needed. They stay listed
      //  (the rulings still stand and could be spent again) but the meter no longer counts
      //  them, which is the only way an exemption list can shrink honestly.
      const int nSanctions = (int) (sizeof (kSanctioned) / sizeof (kSanctioned[0]));
      std::string unused;
      for (const Sanction& q : kSanctioned)
      { bool used = false;
        for (int i = 0; i < EQ::kNumLabels && ! used; ++i)
          if (! std::strcmp (EQ::label (i), q.name))
            for (int k = 0; k < nShipped && ! used; ++k)
              if (! std::strcmp (kShippedLabels[k], q.name)) used = true;
        if (! used) unused += std::string (unused.empty() ? "" : ", ") + q.name; }
      gate ("the sanctioned list SPENDS exactly 9 exemptions — it cannot quietly grow",
            nSanc == 9,
            fmt2 ("%.0f of %.0f sanctions are actually spent on a live collision", (double) nSanc,
                  (double) nSanctions)
            + (unused.empty() ? "" : "; carried but unspent (ruled as a GROUP): " + unused));

      for (const Sanction& q : kSanctioned) note (std::string ("   sanctioned  ") + q.name + " — " + q.why); }

    // 4. the published table itself, printed, because a label that lives only in markdown is
    //    the exact geometry that let `Cassette` play `Studio` (FIXES.md §3).
    std::printf ("\n        PUBLISHED FROM THE ENGINE HEADER (EQ::label / EQ::labelSlot):\n");
    std::printf ("        Types : ");
    for (int t = 0; t < EQ::kNumTypes; ++t) std::printf ("%s%s", EQ::typeNames()[t], t + 1 < EQ::kNumTypes ? " · " : "\n");
    std::printf ("        Front : ");
    for (int i = 0; i < 4; ++i) std::printf ("%s%s", EQ::frontNames()[i], i < 3 ? " · " : "\n");
    std::printf ("        Back  : ");
    for (int i = 0; i < 8; ++i) std::printf ("%s%s", EQ::backNames()[i], i < 7 ? " · " : "\n");
    std::printf ("        Trait : ");
    for (int t = 0; t < EQ::kNumTypes; ++t) std::printf ("%s=%s%s", EQ::typeNames()[t], EQ::shapeName (t),
                                                          t + 1 < EQ::kNumTypes ? " · " : "\n");
    std::printf ("        Focus : ");
    for (int i = 0; i < EQ::kNumFocus; ++i) std::printf ("%s%s", EQ::focusNames()[i], i + 1 < EQ::kNumFocus ? " · " : "\n");
    for (int t = 0; t < EQ::kNumTypes; ++t)
    { std::printf ("        %-9s", EQ::typeNames()[t]);
      for (int c = 0; c < EQ::kNumChars; ++c) std::printf (" %-13s", EQ::charNames (t)[c]);
      std::printf ("\n"); }
}

// ═════════════════════════════════════════════════════════════════════════════
//  P. DOWNSTREAM — ROSTER.md and eq-worklet.js CANNOT DRIFT FROM THE HEADER.
//
//  fb422 made the header the single source of truth and deleted the duplicate
//  `charNames()` table. That fixed ONE of three name tables. The card is built from the
//  ROSTER and the worklet, so those two files publish labels too — and at fb423 the family
//  audit found 22 stale strings still alive downstream while every header was correct:
//  this device's worklet was carrying `CSPEC[].nm`, a SECOND 56-name table 12 renames
//  behind `CHARS` and never read by anything (line 535 publishes CHARS), and the ROSTER's
//  live §4 Trait table still printed the OLD P8 relabels while its own banner announced
//  the new ones.
//
//  🔑 THE SHAPE OF THIS GATE, and why it is not "keep the tables in sync":
//     P1 EQUALS, it does not spot-check — every one of the 87 strings, element by element.
//     P2 proves the DELETION held: each name is authored EXACTLY ONCE in the worklet and
//        the CSPEC physics block contains ZERO string literals. A table that cannot exist
//        cannot drift; gating a second table would only have made the drift noisy.
//     P3 hunts the 29 RETIRED strings across both files, so PROSE drift is caught too —
//        that is the class the audit actually found, and no equality check can see it.
//     P4/P5 pin the two places the ROSTER re-states a name in its own words.
//  A missing file FAILS. A gate that silently passes when it cannot find its subject is
//  the fb393 harness-kinder-than-reality trap.
// ═════════════════════════════════════════════════════════════════════════════
bool isWordCh (char c)
{ return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

std::string certDir()
{
    std::string f = __FILE__;
    const size_t p = f.find_last_of ('/');
    return p == std::string::npos ? std::string ("") : f.substr (0, p + 1);
}
bool slurp (const std::string& path, std::string& out)
{
    std::FILE* h = std::fopen (path.c_str(), "rb");
    if (h == nullptr) return false;
    out.clear(); char buf[65536]; size_t n;
    while ((n = std::fread (buf, 1, sizeof buf, h)) > 0) out.append (buf, n);
    std::fclose (h); return true;
}
std::string lineAt (const std::string& hay, size_t at)
{
    const size_t a = hay.rfind ('\n', at), b = hay.find ('\n', at);
    std::string s = hay.substr (a == std::string::npos ? 0 : a + 1,
                                (b == std::string::npos ? hay.size() : b) - (a == std::string::npos ? 0 : a + 1));
    if (s.size() > 96) s = s.substr (0, 96) + "...";
    return s;
}
// drop every line whose first non-blank characters are `pfx` (JS comment)
std::string dropLines (const std::string& src, const char* pfx, int& nDropped, size_t& lastAt)
{
    std::string out; nDropped = 0; lastAt = 0;
    size_t i = 0, ln = 0;
    while (i <= src.size())
    {
        size_t e = src.find ('\n', i); if (e == std::string::npos) e = src.size();
        const std::string line = src.substr (i, e - i);
        size_t k = 0; while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
        if (line.compare (k, std::strlen (pfx), pfx) == 0) { ++nDropped; lastAt = ln; }
        else out += line;
        out += '\n'; ++ln;
        if (e == src.size()) break;
        i = e + 1;
    }
    return out;
}
// 🪤 The FIRST version of this exemption dropped EVERY markdown blockquote line, and the
//  gate below immediately caught it doing so: §0 quotes MAX in a blockquote, and an
//  exemption wide enough to cover Max's own words is an exemption that would hide real
//  drift in the one section a reader trusts most. The exemption is now exactly ONE region:
//  the CONTIGUOUS blockquote that OPENS the file - the fb422/fb423 changelog banner, whose
//  whole job is to narrate the old names. Every other blockquote in the file is SCANNED.
std::string dropTopBanner (const std::string& src, int& nDropped, size_t& firstLn, size_t& lastLn,
                           int& nOtherQuoteLines)
{
    std::string out; nDropped = 0; firstLn = 0; lastLn = 0; nOtherQuoteLines = 0;
    bool started = false, ended = false;
    size_t i = 0, ln = 0;
    while (i <= src.size())
    {
        size_t e = src.find ('\n', i); if (e == std::string::npos) e = src.size();
        const std::string line = src.substr (i, e - i);
        const bool quote = ! line.empty() && line[0] == '>';
        if (quote && ! ended)
        {
            if (! started) { started = true; firstLn = ln; }
            ++nDropped; lastLn = ln;                       // dropped: the changelog banner
        }
        else
        {
            if (started && ! quote) ended = true;          // the banner closes at its first non-`>` line
            if (quote) ++nOtherQuoteLines;                 // every LATER blockquote is scanned
            out += line;
        }
        out += '\n'; ++ln;
        if (e == src.size()) break;
        i = e + 1;
    }
    return out;
}
// 🔑 fb425 — THE CERT SCANS ITSELF. §P3 used to read ROSTER.md and the worklet and stop
//  there, and the file doing the scanning was printing THREE retired names in its own gate
//  labels: `Chisel/Metal`, `Inverted` and Surgical `Width`, while the engine said `Tin`,
//  `Upward` and `Pinch`. A reader's only view of this device is the gate labels; a cert that
//  narrates the old roster is the same drift as a ROSTER that does, one file closer to the
//  reader. Three regions of this file must be allowed to spell the old names because the old
//  names ARE their subject — the RENAMES table, the extractor self-check and the blacklist —
//  and each is bracketed by an @RETIRED-OK sentinel with its reason. The count is ASSERTED,
//  so the exemption cannot quietly grow, and every sentinel's reason is printed on every run.
struct OkRegion { size_t first, last; std::string why; };
std::string dropOkRegions (const std::string& src, std::vector<OkRegion>& regs, int& unbalanced)
{
    std::string out; regs.clear(); unbalanced = 0;
    size_t i = 0, ln = 0; bool inside = false; OkRegion cur {};
    while (i <= src.size())
    {
        size_t e = src.find ('\n', i); if (e == std::string::npos) e = src.size();
        const std::string line = src.substr (i, e - i);
        size_t k = 0; while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
        const std::string t = line.substr (k);
        if (t.compare (0, 19, "//@RETIRED-OK-BEGIN") == 0)
        {
            if (inside) ++unbalanced;
            inside = true; cur = OkRegion { ln, ln, t.substr (19) };
        }
        else if (t.compare (0, 17, "//@RETIRED-OK-END") == 0)
        {
            if (! inside) ++unbalanced;
            else { cur.last = ln; regs.push_back (cur); }
            inside = false;
        }
        if (! inside && t.compare (0, 17, "//@RETIRED-OK-END") != 0) out += line;
        out += '\n'; ++ln;
        if (e == src.size()) break;
        i = e + 1;
    }
    if (inside) ++unbalanced;
    return out;
}

// a RETIRED name occurs as a whole token AND is not the head of a LONGER LIVE label
// (`Program` must never fire on `Program Ride`; `Slow` must never fire on `Slow Top`).
int countRetired (const std::string& hay, const std::string& needle, std::string& ctx)
{
    int n = 0; size_t at = 0;
    while ((at = hay.find (needle, at)) != std::string::npos)
    {
        const size_t end = at + needle.size();
        const bool lb = at == 0 || ! isWordCh (hay[at - 1]);
        const bool rb = end >= hay.size() || ! isWordCh (hay[end]);
        bool shadowed = false;
        if (lb && rb)
            for (int i = 0; i < EQ::kNumLabels && ! shadowed; ++i)
            { const std::string L = EQ::label (i);
              if (L.size() > needle.size() && hay.compare (at, L.size(), L) == 0) shadowed = true; }
        if (lb && rb && ! shadowed) { ++n; if (ctx.empty()) ctx = lineAt (hay, at); }
        at = end;
    }
    return n;
}
// every single-quoted string inside `const <name> = [ ... ];`, in source order
bool jsArray (const std::string& src, const char* name, std::vector<std::string>& out, std::string& err)
{
    const std::string key = std::string ("const ") + name;
    size_t a = src.find (key);
    if (a == std::string::npos) { err = "no `" + key + "`"; return false; }
    a = src.find ('[', a);
    if (a == std::string::npos) { err = std::string (name) + ": no `[`"; return false; }
    int depth = 0; size_t b = a;
    for (; b < src.size(); ++b)
    { if (src[b] == '[') ++depth; else if (src[b] == ']') { if (--depth == 0) break; } }
    if (b >= src.size()) { err = std::string (name) + ": unterminated"; return false; }
    out.clear();
    for (size_t i = a; i < b; ++i)
        if (src[i] == '\'')
        { const size_t e = src.find ('\'', i + 1); if (e == std::string::npos || e > b) break;
          out.push_back (src.substr (i + 1, e - i - 1)); i = e; }
    return true;
}
int countQuoted (const std::string& hay, const std::string& lab)
{ const std::string q = "'" + lab + "'"; int n = 0; size_t at = 0;
  while ((at = hay.find (q, at)) != std::string::npos) { ++n; at += q.size(); } return n; }

// the 29 strings this device has RETIRED across both rulings. Nothing downstream may say them.
//@RETIRED-OK-BEGIN kRetired: the hunted list itself. A blacklist that may not spell its own
//  entries cannot exist.
static const char* kRetired[] = {
    "Tilt", "Sculpt", "Shape", "Width", "Bump", "Grip", "Ring", "Sense", "Clean", "Carve",
    "Console", "Fixed Top", "Gentle", "Modern", "Close Dip", "Far Dip", "Silky", "Fast",
    "Slow", "Inverted", "Gain Ring",
    "Forward", "Runaway", "Program", "Stacked", "Peak Hold", "Razor", "Telephone", "Metal"
};
//@RETIRED-OK-END

void sectionP()
{
    section ("P. DOWNSTREAM — ROSTER.md, eq-worklet.js AND THIS CERT agree with the header");

    std::string roster, worklet, self;
    const std::string dir = certDir();
    const bool haveR = slurp (dir + "ROSTER.md",     roster);
    const bool haveW = slurp (dir + "eq-worklet.js", worklet);
    const bool haveS = slurp (dir + "eq_cert.cpp",   self);          // fb425: the cert scans itself
    gate ("all THREE published files are READABLE (a missing file FAILS, it does not skip)",
          haveR && haveW && haveS, std::string ("ROSTER.md ") + (haveR ? "yes" : "NO")
          + " · eq-worklet.js " + (haveW ? "yes" : "NO")
          + " · eq_cert.cpp " + (haveS ? "yes" : "NO")
          + "  at " + (dir.empty() ? std::string ("./") : dir));
    if (! haveR || ! haveW || ! haveS) return;

    // ── P1. the worklet's six name tables EQUAL the header's, element by element ──
    {
        struct A { const char* js; int n; };
        const A arrays[] = { { "TYPES", EQ::kNumTypes }, { "FOCUS", EQ::kNumFocus },
                             { "TRAIT", EQ::kNumTypes }, { "BACK", 8 }, { "FRONT", 4 },
                             { "DROPDOWN", EQ::kNumDropdowns },
                             { "CHARS", EQ::kNumTypes * EQ::kNumChars } };
        int checked = 0; std::string bad, err;
        for (const A& a : arrays)
        {
            std::vector<std::string> got;
            if (! jsArray (worklet, a.js, got, err))
            { bad += std::string (bad.empty() ? "" : ", ") + err; continue; }
            if ((int) got.size() != a.n)
            { bad += std::string (bad.empty() ? "" : ", ") + a.js + " has "
                   + std::to_string (got.size()) + ", header has " + std::to_string (a.n); continue; }
            for (int i = 0; i < a.n; ++i)
            {
                const char* want = ! std::strcmp (a.js, "TYPES") ? EQ::typeNames()[i]
                                 : ! std::strcmp (a.js, "FOCUS") ? EQ::focusNames()[i]
                                 : ! std::strcmp (a.js, "TRAIT") ? EQ::shapeName (i)
                                 : ! std::strcmp (a.js, "BACK")  ? EQ::backNames()[i]
                                 : ! std::strcmp (a.js, "FRONT") ? EQ::frontNames()[i]
                                 : ! std::strcmp (a.js, "DROPDOWN") ? EQ::dropdownNames()[i]
                                 : EQ::charNames (i / EQ::kNumChars)[i % EQ::kNumChars];
                if (got[(size_t) i] != want)
                    bad += std::string (bad.empty() ? "" : ", ") + a.js + "[" + std::to_string (i)
                         + "] worklet '" + got[(size_t) i] + "' vs header '" + want + "'";
                else ++checked;
            }
        }
        gate ("eq-worklet.js's name tables EQUAL the header, string for string",
              bad.empty() && checked == EQ::kNumLabels,
              fmt2 ("%.0f of %.0f labels identical", (double) checked, (double) EQ::kNumLabels)
              + (bad.empty() ? "" : "  DRIFT: " + bad));
    }

    // ── P2. the DELETION held: one authorship site per name, zero names in the physics table ──
    {
        std::string many; int once = 0;
        for (int i = 0; i < EQ::kNumLabels; ++i)
        { const int n = countQuoted (worklet, EQ::label (i));
          if (n == 1) ++once;
          else many += std::string (many.empty() ? "" : ", ") + EQ::label (i) + " x" + std::to_string (n); }
        gate ("every name is authored EXACTLY ONCE in eq-worklet.js (no second table)",
              once == EQ::kNumLabels,
              fmt2 ("%.0f of %.0f appear exactly once", (double) once, (double) EQ::kNumLabels)
              + (many.empty() ? "" : "  NOT ONCE: " + many));

        size_t a = worklet.find ("const CSPEC");
        int nq = -1;
        if (a != std::string::npos)
        { a = worklet.find ('[', a); int depth = 0; size_t b = a; nq = 0;
          for (; b < worklet.size(); ++b)
          { if (worklet[b] == '[') ++depth; else if (worklet[b] == ']') { if (--depth == 0) break; }
            else if (worklet[b] == '\'') ++nq; } }
        gate ("the CSPEC physics table carries ZERO strings — `nm` is DELETED, not gated",
              nq == 0, nq < 0 ? std::string ("no `const CSPEC` found")
                              : fmt ("%.0f quote characters inside the CSPEC block", (double) nq));
    }

    // ── P3. no RETIRED string survives anywhere downstream, PROSE INCLUDED ──
    {
        // the two exemptions, both structural, both asserted so they cannot quietly grow:
        //   ROSTER.md   — the `>` changelog blockquote NARRATES the old names on purpose.
        //   worklet     — `//` comments do the same.
        int nQuote = 0, nComment = 0, nLaterQuote = 0, nSelfCmt = 0; size_t firstQ = 0, lastQ = 0, lastComment = 0;
        const std::string rBody = dropTopBanner (roster, nQuote, firstQ, lastQ, nLaterQuote);
        const std::string wBody = dropLines (worklet, "//", nComment, lastComment);
        // fb425 — eq_cert.cpp joins the scan. Its `//` prose NARRATES the history exactly as
        // the worklet's does (same exemption, same reason); what is scanned is every line of
        // CODE, which is where the gate LABELS live — the three strings this round found.
        std::vector<OkRegion> okRegs; int unbalanced = 0;
        size_t lastSelfCmt = 0;
        const std::string sBody = dropLines (dropOkRegions (self, okRegs, unbalanced),
                                             "//", nSelfCmt, lastSelfCmt);
        gate ("the cert's own retired-name exemption is EXACTLY 3 bracketed regions, balanced",
              okRegs.size() == 3 && unbalanced == 0,
              fmt2 ("%.0f regions, %.0f unbalanced sentinels", (double) okRegs.size(), (double) unbalanced));
        for (const OkRegion& r : okRegs)
            note (fmt2 ("   exempt lines %.0f-%.0f:", (double) (r.first + 1), (double) (r.last + 1))
                  + r.why);

        // the exemption must be ONE contiguous region, opening the file, ending before §0.
        size_t firstH = 0, ln = 0, i = 0; bool found = false;
        while (i <= roster.size() && ! found)
        { size_t e = roster.find ('\n', i); if (e == std::string::npos) e = roster.size();
          if (roster.compare (i, 3, "## ") == 0) { firstH = ln; found = true; }
          ++ln; if (e == roster.size()) break; i = e + 1; }
        const bool contiguous = nQuote > 0 && (lastQ - firstQ + 1) == (size_t) nQuote;
        // ...and it must OPEN with a changelog heading, so the exemption stays what it claims to
        // be. A banner that may grow without saying what it is, is a blind spot that may grow.
        size_t bq = std::string::npos;
        for (size_t q = 0; q < roster.size(); )
        { if (roster[q] == '>') { bq = q; break; }
          const size_t e = roster.find ('\n', q); if (e == std::string::npos) break; q = e + 1; }
        const std::string kBannerHead = "> ## \xf0\x9f\x94\xb4 fb";     // "> ## <red circle> fb..."
        const bool isChangelog = bq != std::string::npos
                              && roster.compare (bq, kBannerHead.size(), kBannerHead) == 0;
        gate ("the changelog exemption is ONE contiguous banner at the top — Max's own §0 quote is SCANNED",
              contiguous && isChangelog && found && lastQ < firstH,
              fmt3 ("banner = lines %.0f-%.0f (%.0f lines, contiguous)",
                    (double) (firstQ + 1), (double) (lastQ + 1), (double) nQuote)
              + fmt (", first '## ' at line %.0f", (double) (firstH + 1))
              + std::string (isChangelog ? ", opens '> ## fb...'" : ", NOT a changelog heading")
              + fmt ("; %.0f LATER blockquote lines are scanned like any other prose", (double) nLaterQuote));

        std::string live; int nHits = 0;
        for (const char* r : kRetired)
        {
            std::string cR, cW, cS;
            const int hR = countRetired (rBody, r, cR), hW = countRetired (wBody, r, cW),
                      hS = countRetired (sBody, r, cS);
            if (hR + hW + hS == 0) continue;
            nHits += hR + hW + hS;
            live += std::string (live.empty() ? "" : "; ") + r
                  + (hR ? " ROSTER.md x" + std::to_string (hR) + " [" + cR + "]" : "")
                  + (hW ? " worklet x"   + std::to_string (hW) + " [" + cW + "]" : "")
                  + (hS ? " eq_cert.cpp x" + std::to_string (hS) + " [" + cS + "]" : "");
        }
        gate ("not one of the 29 RETIRED strings survives in ROSTER, worklet OR THE CERT",
              nHits == 0,
              fmt ("%.0f retired strings still live", (double) nHits)
              + (live.empty() ? "  (29 x 3 files checked, 0 hits)" : ":  " + live));
        note (fmt ("   exempt: %.0f ROSTER.md changelog blockquote lines, ", (double) nQuote)
              + fmt ("%.0f eq-worklet.js and ", (double) nComment)
              + fmt ("%.0f eq_cert.cpp comment lines — all NARRATE the old names.", (double) nSelfCmt));
    }

    // ── P4. the ROSTER's §4 Trait table re-states the P8 relabels in its own words ──
    {
        std::string bad; int ok = 0;
        for (int t = 0; t < EQ::kNumTypes; ++t)
        { const std::string row = std::string ("| ") + EQ::typeNames()[t] + " | **" + EQ::shapeName (t) + "** |";
          if (roster.find (row) != std::string::npos) ++ok;
          else bad += std::string (bad.empty() ? "" : ", ") + EQ::typeNames()[t] + "->" + EQ::shapeName (t); }
        gate ("ROSTER §4's Trait table prints the header's SEVEN P8 relabels",
              ok == EQ::kNumTypes,
              fmt2 ("%.0f of %.0f rows", (double) ok, (double) EQ::kNumTypes)
              + (bad.empty() ? "" : "  MISSING: " + bad));
    }

    // ── P5. and the ROSTER names every label the engine publishes ──
    {
        std::string miss; int ok = 0;
        for (int i = 0; i < EQ::kNumLabels; ++i)
        { if (roster.find (EQ::label (i)) != std::string::npos) ++ok;
          else miss += std::string (miss.empty() ? "" : ", ") + EQ::label (i)
                     + " (" + EQ::labelSlot (i) + ")"; }
        gate ("ROSTER.md names all 89 published labels (no label lives only in the header)",
              ok == EQ::kNumLabels,
              fmt2 ("%.0f of %.0f present", (double) ok, (double) EQ::kNumLabels)
              + (miss.empty() ? "" : "  ABSENT: " + miss));
        note ("   presence is COVERAGE and nothing more — P6 below is the gate that reads the");
        note ("   ROSTER positionally, because §3 is the one file that ASSIGNS Characters to Types.");
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  🔑 fb425 — P6. THE ROSTER HALF OF THE DRIFT GATE, MADE ORDERED AND POSITIONAL.
    //
    //  P1 diffs the worklet element by element. The ROSTER was covered only by P5's
    //  `roster.find (label) != npos` — a bare substring presence test with no position, no
    //  Type association, no ordering and no word boundary. A skeptic moved two Characters
    //  under the WRONG TYPES in `ROSTER.md §3` and P5 stayed green, because both strings
    //  were still SOMEWHERE in the file. §3 is the one downstream document that ASSIGNS
    //  Characters to Types; presence in it proves nothing about the assignment.
    //
    //  This gate reads §3 the way the card does:
    //    · the section is bounded by its own headings (`## 3.` .. the next `## `),
    //    · each Type owns the region from its own `**Name**` marker to the next one,
    //    · the FIRST 8 backticked tokens in that region must EQUAL `charNames(t)` IN ORDER,
    //    · and EVERY backticked token in the region must belong to that Type — the reverse
    //      direction, which is what catches a Character that has wandered in from elsewhere.
    //  A missing marker or a short region FAILS; it does not skip.
    // ═════════════════════════════════════════════════════════════════════════
    {
        const size_t s3 = roster.find ("## 3. ");
        size_t e3 = (s3 == std::string::npos) ? std::string::npos : roster.find ("\n## ", s3 + 4);
        if (e3 == std::string::npos) e3 = roster.size();
        gate ("ROSTER §3 (the Character-to-Type assignment) is FOUND and bounded",
              s3 != std::string::npos && e3 > s3,
              s3 == std::string::npos ? std::string ("no `## 3. ` heading")
                                      : fmt2 ("§3 spans %.0f bytes, ends at the next `## ` (offset %.0f)",
                                              (double) (e3 - s3), (double) e3));
        if (s3 != std::string::npos && e3 > s3)
        {
            const std::string sec = roster.substr (s3, e3 - s3);

            // where each Type's paragraph starts, in FILE order (which is not roster index order)
            struct Mark { size_t at; int t; };
            std::vector<Mark> marks; std::string noMark;
            for (int t = 0; t < EQ::kNumTypes; ++t)
            { const std::string m = std::string ("**") + EQ::typeNames()[t] + "**";
              const size_t at = sec.find (m);
              if (at == std::string::npos) noMark += std::string (noMark.empty() ? "" : ", ") + EQ::typeNames()[t];
              else marks.push_back ({ at, t }); }
            std::sort (marks.begin(), marks.end(), [] (const Mark& a, const Mark& b) { return a.at < b.at; });
            gate ("   ... and every Type owns a paragraph in it, exactly once",
                  (int) marks.size() == EQ::kNumTypes,
                  fmt2 ("%.0f of %.0f Type markers found", (double) marks.size(), (double) EQ::kNumTypes)
                  + (noMark.empty() ? "" : "  MISSING: " + noMark));

            std::string order, stray; int okT = 0;
            for (size_t m = 0; m < marks.size(); ++m)
            {
                const int t = marks[m].t;
                const size_t a = marks[m].at;
                const size_t b = (m + 1 < marks.size()) ? marks[m + 1].at : sec.size();
                // every `backticked` token in this Type's region, in source order
                std::vector<std::string> tok;
                for (size_t i = a; i < b; ++i)
                    if (sec[i] == '`')
                    { const size_t e = sec.find ('`', i + 1); if (e == std::string::npos || e > b) break;
                      tok.push_back (sec.substr (i + 1, e - i - 1)); i = e; }

                bool good = ((int) tok.size() >= EQ::kNumChars);
                for (int c = 0; good && c < EQ::kNumChars; ++c)
                    if (tok[(size_t) c] != EQ::charNames (t)[c])
                    { good = false;
                      order += std::string (order.empty() ? "" : ", ") + EQ::typeNames()[t]
                             + " slot " + std::to_string (c) + ": ROSTER '" + tok[(size_t) c]
                             + "' vs header '" + EQ::charNames (t)[c] + "'"; }
                // the reverse direction: nothing from ANOTHER Type may appear in this region
                for (const std::string& w : tok)
                {
                    bool mine = false;
                    for (int c = 0; c < EQ::kNumChars && ! mine; ++c) mine = (w == EQ::charNames (t)[c]);
                    if (mine) continue;
                    for (int u = 0; u < EQ::kNumTypes; ++u)
                        for (int c = 0; c < EQ::kNumChars; ++c)
                            if (w == EQ::charNames (u)[c])
                            { good = false;
                              stray += std::string (stray.empty() ? "" : ", ") + "`" + w + "` ("
                                     + EQ::typeNames()[u] + "'s) inside the " + EQ::typeNames()[t]
                                     + " paragraph"; }
                }
                if (good) ++okT;
            }
            gate ("ROSTER §3 assigns the 56 Characters to the RIGHT Types, IN ORDER",
                  okT == EQ::kNumTypes && order.empty() && stray.empty(),
                  fmt2 ("%.0f of %.0f Type paragraphs match charNames() element for element",
                        (double) okT, (double) EQ::kNumTypes)
                  + (order.empty() ? "" : "  ORDER: " + order)
                  + (stray.empty() ? "" : "  CROSS-TYPE: " + stray));
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  🔴 Q — THE FULL Type x Character MATRIX  (fb425, THE LAST LEVEL)
//
//  Three rounds, and the blindness moved down exactly one notch each time:
//      fb421  the gates could not fail AT ALL      (delete the mechanism, cert stays green)
//      fb423  the gates ran on ONE TYPE            (sweep the other six)
//      fb424  the gates ran on ONE TYPE x CHARACTER CELL   (sweep the other 55)
//  There is no deeper level, because THE MATRIX IS FINITE. 7 Types x 8 Characters = 56
//  cells; x 12 knobs = 672 knob-cells. The whole sweep below costs about a second. Sampling
//  it was always a choice, not a constraint — so this section takes the choice away:
//  §F/§F2/§F3 keep their hand-built, per-knob metrics (they are the ones that can tell a
//  bandwidth from a shelf undershoot), and §Q re-asks the ONE question that admits no
//  per-knob craft — "does this knob DO anything here?" — in every cell there is.
//
//  🔬 CHECK YOUR OWN DETECTOR, and this section needed it twice:
//
//  (1) PROBE PLACEMENT. The first build of §Q1 read the Open Type's `Trait` as 1.83 dB —
//      nearly dead — with `Reach` at the reference patch's 16 kHz. `Silk` is a SECOND shelf
//      one octave ABOVE Reach; at Reach 16 kHz that shelf sits at 32 kHz, past Nyquist, and
//      what was being measured was the PROBE's choice of corner, not the knob. The matrix
//      patch therefore parks `Reach` at 9 kHz, where every Type's top-band mechanism is in
//      band. §F2 already knew this — its Open row says "Reach 9 kHz" — and the matrix has to
//      know it too or it grades the probe.
//
//  (2) OPERATING POINT. At the -26 dBFS bus program, ELEVEN knob-cells on the `Dynamic`
//      Type are BIT-IDENTICAL at 0 % and 100 %. They are not dead knobs: `Dynamic` is the
//      one Type CONTRACT §4 allows to be level-dependent, and its ride has closed those
//      BOOST bands completely because the program is loud — which is the Type's whole
//      advertised mechanism (ROSTER §3 publishes the table: `Program Ride` hot -24.0 dB,
//      quiet 0.0 dB). A band the ride has switched off has no frequency to move. Every one
//      of those eleven wakes up at a -46 dBFS program, measured below. So the gate probes
//      each cell at the bus program and, only where that reads inert, RE-PROBES at -46 and
//      -6 dBFS — and the eleven are DECLARED in `kRideClosed` with the level that revealed
//      them, checked in BOTH directions so the list can neither hide a dead knob nor keep a
//      stale entry.
//
//  A cell that is bit-identical at ALL THREE program levels is a dead knob and fails with
//  its coordinates. There are none.
// ═════════════════════════════════════════════════════════════════════════════

static const char* const kKnobName[12] =
{ "Slant", "Air", "Amount", "Mix", "Low Hz", "Low", "Body Hz", "Body", "Bite Hz", "Bite",
  "Reach", "Trait" };

float* knobPtr (EQ::Params& p, int k)
{
    switch (k)
    { case 0: return &p.f1; case 1: return &p.f2; case 2: return &p.f3; case 3: return &p.mix;
      case 4: return &p.b1; case 5: return &p.b2; case 6: return &p.b3; case 7: return &p.b4;
      case 8: return &p.b5; case 9: return &p.b6; case 10: return &p.b7; default: return &p.b8; }
}

// the matrix cell patch: the reference four-band move, with `Reach` parked at 9 kHz so the
// top-band mechanism of EVERY Type is inside the audio band (see (1) above).
EQ::Params matPatch (int t, int c)
{ EQ::Params p = refPatch (t, c); p.b7 = fN (3, 9000.0f); return p; }

// one run of the matrix probe: the analysed tail AND its 96-bin log spectrum. The input is
// identical for both ends of a sweep, so the difference of the two OUTPUT spectra IS the
// transfer difference — a magnitude metric, never a sample difference (law 6).
struct MRun { std::vector<float> y; double db[NB]; };
MRun matRun (const EQ::Params& p, float rms)
{
    const int NF = 8192, settle = 4096, N = settle + NF;
    const std::vector<float> x = whiteN (N, rms, 12345u);
    std::vector<float> L (x), R (x);
    EQ e; e.prepare ((double) FS, 128);
    for (int i = 0; i < N; i += 128) { e.setParams (p); e.processStereo (&L[(size_t) i], &R[(size_t) i], 128); }
    MRun r; r.y.assign (L.begin() + settle, L.end());
    std::vector<std::complex<double>> A ((size_t) NF);
    for (int i = 0; i < NF; ++i)
    { const double w = 0.5 - 0.5 * std::cos (6.283185307179586 * i / (NF - 1));
      A[(size_t) i] = r.y[(size_t) i] * w; }
    fft (A);
    std::vector<double> s ((size_t) NF / 2 + 1);
    for (int k = 0; k <= NF / 2; ++k) s[(size_t) k] = std::norm (A[(size_t) k]);
    for (int i = 0; i < NB; ++i)
    { const double fc = EQ::curveBinHz (i), rr = std::pow (10.0, 1.5 / 95.0);
      int klo = std::max (1, (int) std::floor (fc / rr * NF / FS));
      int khi = std::min (NF / 2, std::max ((int) std::ceil (fc * rr * NF / FS), klo));
      double n = 0; for (int k = klo; k <= khi; ++k) n += s[(size_t) k];
      r.db[i] = 10.0 * std::log10 (std::max (1e-300, n)); }
    return r;
}

// the three program levels, in the order the gate tries them.
static const float       kLvlRms[3]  = { 0.05f, 0.005f, 0.5f };
static const char* const kLvlName[3] = { "-26 dBFS (the bus program)", "-46 dBFS (quiet)",
                                         "-6 dBFS (hot)" };

// ── THE DECLARED ROSTER FACTS. Both tables are ASSERTED-SIZE, and both are checked in BOTH
//    directions: an undeclared cell FAILS, and a declared cell that is NOT what it claims
//    also FAILS. A gate that can exempt itself is not a gate (RENAMES.md fb425).
struct MCell { int t, c, k; const char* why; };

//  (a) LEGITIMATELY INERT — the mechanism genuinely does not exist on that Type. There are
//      NONE in this device: every one of the 12 knobs is wired into every one of the 7 Types
//      (the Q law, the gain law and the four bands are re-voiced per Type, never removed),
//      and the 672-cell sweep below confirms it. The table exists at size 0 so that "no cell
//      is inert" is an ASSERTION with a size, not an absence nobody looked for.
[[maybe_unused]] static const MCell kKnownInert[] = {};
static constexpr int kNumKnownInert = (int) (sizeof (kKnownInert) / sizeof (MCell));
static_assert (kNumKnownInert == 0,
               "fb425: any legitimately-inert cell must be declared HERE, with a reason");

//  (b) RIDE-CLOSED AT THE BUS PROGRAM — alive, but only once the program leaves the level
//      that closes the band. Every entry is on `Dynamic`, the one Type allowed to be
//      level-dependent, and every one is a BOOST band whose ride is fully down at -26 dBFS.
static const MCell kRideClosed[] = {
    { 5, 3, 4,  "Dynamic x `Wideband`: ONE wideband envelope drives all four bands, so at a hot "
                "program every BOOST band is fully closed and `Low Hz` has no live band to move" },
    { 5, 3, 8,  "Dynamic x `Wideband`: same wideband envelope, the BITE boost is closed too" },
    { 5, 5, 8,  "Dynamic x `Hard Window`: a 2.5 dB window is fully ON at -26 dBFS, so the BITE "
                "boost is held at zero gain and its frequency knob has nothing to move" },
    { 5, 7, 8,  "Dynamic x `Peak Keep`: peak-hold release keeps the BITE boost closed for the "
                "whole analysis window at a hot program" },
    { 5, 0, 10, "Dynamic x `Program Ride`: the AIR boost is fully ridden down at -26 dBFS, so "
                "`Reach` (its corner) has no live shelf to move" },
    { 5, 1, 10, "Dynamic x `Quick`: same closed AIR boost, 0.67 ms attack" },
    { 5, 2, 10, "Dynamic x `Lazy`: same closed AIR boost, 307 ms release" },
    { 5, 3, 10, "Dynamic x `Wideband`: same closed AIR boost, wideband detector" },
    { 5, 5, 10, "Dynamic x `Hard Window`: same closed AIR boost, 2.5 dB window" },
    { 5, 6, 10, "Dynamic x `Soft Window`: same closed AIR boost, 34 dB window" },
    { 5, 7, 10, "Dynamic x `Peak Keep`: same closed AIR boost, peak-hold release" },
};
static constexpr int kNumRideClosed = (int) (sizeof (kRideClosed) / sizeof (MCell));
static_assert (kNumRideClosed == 11, "fb425: the ride-closed roster fact is exactly 11 cells");

//  (c) CEILING-CAPPED CELLS — R11 replayed on all 56 cells (§Q8). Three Characters cap the
//      device's own ceiling by a mechanism they publish in ROSTER §3. They are exempt from
//      the 55 dB bar and are held to a floor of 32 dB instead — twice, in decibels, the
//      loudest hardware EQ ever built (a Pultec/console tops out at 16 dB, which is the same
//      reference frame §K's thresholds are defended against).
struct RCell { int t, c; const char* why; };
static const RCell kCapped[] = {
    { 4, 5, "Open x `Soft Knee`: a 12 dB tanh knee IS the Character — `g = knee*tanh(g/knee)` "
            "caps a +60 dB request at ~24 dB by design (ROSTER §3: 'heavy compression of extremes')" },
    { 6, 5, "Chisel x `Handset`: LOW and AIR are pulled to 2.5x/0.35x and become BELLS, so the "
            "two SHELVES that carry the biggest boost are not in this Character at all" },
    { 6, 6, "Chisel x `Sub Kill`: LOW sits at 0.35x with Q x3 and a -10 dB knee — a deliberately "
            "narrow sub-band, not a full-range shelf" },
};
static constexpr int kNumCapped = (int) (sizeof (kCapped) / sizeof (RCell));
static_assert (kNumCapped == 3, "fb425: the R11 cap exemption is exactly 3 cells, each cited");

// a failure list that stays READABLE: the first six offenders by name, then a count. A gate
// whose evidence is 56 lines long is evidence nobody reads.
void addCapped (std::string& dst, int n, const std::string& item)
{
    if (n < 6)       dst += std::string (dst.empty() ? "" : "; ") + item;
    else if (n == 6) dst += "; ...";
}
std::string plusMore (int n)
{ return n > 6 ? fmt ("  (+%.0f more)", (double) (n - 6)) : std::string(); }

bool inList (const MCell* L, int n, int t, int c, int k)
{ for (int i = 0; i < n; ++i) if (L[i].t == t && L[i].c == c && L[i].k == k) return true; return false; }

// a DECORRELATED stereo run: both channel spectra from ONE pass. Focus routes which signal
// the filters see, so a correlated probe (L == R) has an identically-zero SIDE signal and
// would measure `Side` and `Stereo` as the same thing — the probe deciding the answer again.
struct StRun { double l[NB], r[NB]; };
StRun matRunStereo (const EQ::Params& p)
{
    const int NF = 8192, settle = 4096, N = settle + NF;
    const std::vector<float> xl = whiteN (N, 0.05f, 12345u), xr = whiteN (N, 0.05f, 777u);
    std::vector<float> L (xl), R (xr);
    EQ e; e.prepare ((double) FS, 128);
    for (int i = 0; i < N; i += 128) { e.setParams (p); e.processStereo (&L[(size_t) i], &R[(size_t) i], 128); }
    StRun o;
    for (int ch = 0; ch < 2; ++ch)
    {
        const std::vector<float>& Y = (ch == 0 ? L : R);
        std::vector<std::complex<double>> A ((size_t) NF);
        for (int i = 0; i < NF; ++i)
        { const double w = 0.5 - 0.5 * std::cos (6.283185307179586 * i / (NF - 1));
          A[(size_t) i] = Y[(size_t) (settle + i)] * w; }
        fft (A);
        std::vector<double> sp ((size_t) NF / 2 + 1);
        for (int k = 0; k <= NF / 2; ++k) sp[(size_t) k] = std::norm (A[(size_t) k]);
        for (int i = 0; i < NB; ++i)
        { const double fc = EQ::curveBinHz (i), rr = std::pow (10.0, 1.5 / 95.0);
          int klo = std::max (1, (int) std::floor (fc / rr * NF / FS));
          int khi = std::min (NF / 2, std::max ((int) std::ceil (fc * rr * NF / FS), klo));
          double n = 0; for (int k = klo; k <= khi; ++k) n += sp[(size_t) k];
          (ch == 0 ? o.l : o.r)[i] = 10.0 * std::log10 (std::max (1e-300, n)); }
    }
    return o;
}

void sectionQ()
{
    section ("Q1. LAW 1 ON EVERY CELL — 12 knobs x 7 Types x 8 Characters = 672 knob-cells");
    note ("BAR: knob 0 vs knob 100 must (a) never be BIT-IDENTICAL and (b) move the 96-bin");
    note ("     output spectrum by >= 3.0 dB. 3 dB is a doubling of power in a band: twice the");
    note ("     1.5 dB the roster demands between two DIFFERENT Characters, so a knob that");
    note ("     cannot clear it is doing less than the difference between its neighbours.");

    // 🪤 THE NEGATIVE CONTROL, first: the detector must read EXACTLY zero when nothing moved.
    { const MRun a = matRun (matPatch (0, 0), 0.05f), b = matRun (matPatch (0, 0), 0.05f);
      double m = 0; for (int i = 0; i < NB; ++i) m = std::max (m, std::fabs (a.db[i] - b.db[i]));
      const bool same = a.y.size() == b.y.size()
                     && std::memcmp (a.y.data(), b.y.data(), a.y.size() * sizeof (float)) == 0;
      gate ("(control) the matrix detector reads 0.000 dB and BIT-IDENTICAL on an unchanged patch",
            m < 1e-9 && same, fmt ("%.6f dB spectral delta, bit-identical ", m)
            + (same ? "yes" : "NO")); }

    struct Res { double best; int lvl; bool bitAll; bool bitBus; double busDelta; };
    static Res R[12][EQ::kNumTypes][EQ::kNumChars];
    int nBitAll = 0, nUnder = 0;
    std::string bitList, underList;

    for (int k = 0; k < 12; ++k)
        for (int t = 0; t < EQ::kNumTypes; ++t)
            for (int c = 0; c < EQ::kNumChars; ++c)
            {
                Res r { -1.0, 0, true, false, 0.0 };
                for (int L = 0; L < 3; ++L)
                {
                    EQ::Params p0 = matPatch (t, c), p1 = matPatch (t, c);
                    *knobPtr (p0, k) = 0.0f; *knobPtr (p1, k) = 1.0f;
                    const MRun a = matRun (p0, kLvlRms[L]), b = matRun (p1, kLvlRms[L]);
                    const bool bit = std::memcmp (a.y.data(), b.y.data(), a.y.size() * sizeof (float)) == 0;
                    double m = 0; for (int i = 0; i < NB; ++i) m = std::max (m, std::fabs (a.db[i] - b.db[i]));
                    if (! bit) r.bitAll = false;
                    if (L == 0) { r.bitBus = bit; r.busDelta = m; }
                    if (m > r.best) { r.best = m; r.lvl = L; }
                    if (L == 0 && ! bit && m >= 3.0) break;          // alive at the bus program: done
                }
                R[k][t][c] = r;
                const std::string coord = std::string (kKnobName[k]) + " @ " + EQ::typeNames()[t]
                                        + " x `" + EQ::charNames (t)[c] + "`";
                if (r.bitAll)     { addCapped (bitList,   nBitAll, coord); ++nBitAll; }
                if (r.best < 3.0) { addCapped (underList, nUnder, coord + fmt (" %.3f dB", r.best)); ++nUnder; }
            }

    gate ("no knob is BIT-IDENTICAL at 0 % and 100 % in ANY of the 672 knob-cells",
          nBitAll == 0, fmt ("%.0f dead knob-cells", (double) nBitAll)
          + (bitList.empty() ? "  (672 cells x 3 program levels)" : ":  " + bitList + plusMore (nBitAll)));
    gate ("every one of the 672 knob-cells moves the output spectrum by >= 3.0 dB",
          nUnder == 0, fmt ("%.0f cells under the bar", (double) nUnder)
          + (underList.empty() ? "" : ":  " + underList + plusMore (nUnder)));

    // the per-knob table: every knob's WORST cell, named. 12 rows, 672 cells behind them.
    note ("   per knob, the WORST of its 56 cells (this is the number a sampled gate never sees):");
    for (int k = 0; k < 12; ++k)
    {
        double w = 1e9; int wt = 0, wc = 0, wl = 0; double med = 0;
        for (int t = 0; t < EQ::kNumTypes; ++t)
            for (int c = 0; c < EQ::kNumChars; ++c)
            { med += R[k][t][c].best;
              if (R[k][t][c].best < w) { w = R[k][t][c].best; wt = t; wc = c; wl = R[k][t][c].lvl; } }
        std::printf ("        %-8s worst %7.2f dB at %-9s x %-13s (mean %6.2f dB%s)\n",
                     kKnobName[k], w, EQ::typeNames()[wt], EQ::charNames (wt)[wc],
                     med / (EQ::kNumTypes * EQ::kNumChars),
                     wl == 0 ? "" : ", at the quiet program");
    }

    // ── the DECLARED roster fact, checked in BOTH directions ──
    {
        std::string undeclared, stale;
        int nDecl = 0;
        for (int k = 0; k < 12; ++k)
            for (int t = 0; t < EQ::kNumTypes; ++t)
                for (int c = 0; c < EQ::kNumChars; ++c)
                    if (R[k][t][c].lvl != 0)
                    {
                        ++nDecl;
                        if (! inList (kRideClosed, kNumRideClosed, t, c, k))
                            undeclared += std::string (undeclared.empty() ? "" : "; ")
                                        + kKnobName[k] + " @ " + EQ::typeNames()[t]
                                        + " x `" + EQ::charNames (t)[c] + "`";
                    }
        for (int i = 0; i < kNumRideClosed; ++i)
        { const MCell& m = kRideClosed[i];
          if (R[m.k][m.t][m.c].lvl == 0)
              stale += std::string (stale.empty() ? "" : "; ") + kKnobName[m.k] + " @ "
                     + EQ::typeNames()[m.t] + " x `" + EQ::charNames (m.t)[m.c]
                     + "` is ALIVE at the bus program — the declaration is stale"; }
        gate ("the cells that need a quieter program are EXACTLY the 11 declared ride-closed cells",
              undeclared.empty() && stale.empty() && nDecl == kNumRideClosed,
              fmt2 ("%.0f measured, %.0f declared", (double) nDecl, (double) kNumRideClosed)
              + (undeclared.empty() ? "" : "  UNDECLARED: " + undeclared)
              + (stale.empty() ? "" : "  STALE: " + stale));
        for (int i = 0; i < kNumRideClosed; ++i)
        { const MCell& m = kRideClosed[i];
          std::printf ("        %-8s @ %-8s x %-13s  %6.3f dB at %-26s -> %6.2f dB at %s\n",
                       kKnobName[m.k], EQ::typeNames()[m.t], EQ::charNames (m.t)[m.c],
                       R[m.k][m.t][m.c].busDelta, kLvlName[0], R[m.k][m.t][m.c].best,
                       kLvlName[R[m.k][m.t][m.c].lvl]); }
        note ("   every one of the eleven is a BOOST band on the ONE level-dependent Type, closed");
        note ("   by its own ride at a hot program — the mechanism ROSTER §3's Dynamic table");
        note ("   publishes (`hot -24.0 dB / quiet 0.0 dB`). Not a dead knob: a working one.");
        gate ("the legitimately-INERT table is asserted at size 0 (no cell in this device is inert)",
              kNumKnownInert == 0, fmt ("kKnownInert holds %.0f cells", (double) kNumKnownInert));
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("Q2. LAW 3 ON EVERY CELL — Mix, in all 56 Type x Character cells");
    note ("Three questions, because no ONE of them is sufficient:");
    note ("  (a) Mix 0 must be the DRY signal, BIT-EXACTLY. This is what `mixTgt_ = 1.0f`");
    note ("      breaks, and it breaks it in every cell at once.");
    note ("  (b) Mix 0.5 must be the exact LINEAR midpoint of Mix 0 and Mix 1 — a null whose");
    note ("      correct answer is zero, so it needs no audibility bar. Catches equal-power");
    note ("      crossfades and any partial-wet clamp.");
    note ("  (c) at Mix 1.0 the measured floor must REACH the engine's own viz prediction: a");
    note ("      dry leak fills a hole and cannot fill it silently. (a)+(b) alone are blind to");
    note ("      a leak that scales with the knob; only an absolute reference sees that.");

    {
        int nDry = 0, nLin = 0, nFill = 0, nDryBad = 0, nLinBad = 0, nFillBad = 0;
        double worstLin = -300.0, worstFill = 0.0, deepestPerType[EQ::kNumTypes];
        std::string dryBad, linBad, fillBad;
        int wlT = 0, wlC = 0, wfT = 0, wfC = 0;
        for (int t = 0; t < EQ::kNumTypes; ++t) deepestPerType[t] = 0.0;

        const int NF = 16384, settle = 8192, N = settle + NF;
        int nHotPick = 0; std::string picked;

        for (int t = 0; t < EQ::kNumTypes; ++t)
            for (int c = 0; c < EQ::kNumChars; ++c)
            {
                // a DEEP, BROAD attenuation: a -30 dB low shelf at 200 Hz with Amount 200 %.
                EQ::Params p = single (t, c, 0, -30.0f, 200.0f); p.f3 = 1.0f;
                const std::string coord = std::string (EQ::typeNames()[t]) + " x `" + EQ::charNames (t)[c] + "`";

                // 🔬 THE OPERATING POINT AGAIN, and this one is not optional: a crossfade null
                //  divides by the difference between dry and wet, so a cell where the patch is
                //  doing NOTHING divides by zero and reports a meaningless 0.0 dB. That is not
                //  hypothetical — `Dynamic` x `Hard Window` has a 2.5 dB threshold window that
                //  is entirely SHUT for a -30 dB cut at the bus program, so the wet IS the dry
                //  there. Pick, per cell, the program level at which the patch is most active,
                //  and say which one was used. Every level-independent Type picks the bus.
                //  The pick is DELIBERATELY sticky on the bus program: a level-independent
                //  Type measures the same activity at every level, and a bare argmax would
                //  then choose between identical numbers on floating-point noise and print a
                //  level change that means nothing. Another level is chosen only when it is
                //  at least 20 % more active — which only a level-dependent cell can be.
                int PL = 0; double act0 = -1.0, bestAct = -1.0;
                std::vector<float> x, L1, R1;
                std::vector<double> viz ((size_t) NB, 0.0);
                for (int L = 0; L < 3; ++L)
                {
                    const std::vector<float> xx = whiteN (N, kLvlRms[L], 12345u);
                    std::vector<float> a (xx), b (xx); std::vector<double> vz ((size_t) NB, 0.0);
                    EQ e; e.prepare ((double) FS, 128); int nv = 0;
                    for (int i = 0; i < N; i += 128)
                    { e.setParams (p); e.processStereo (&a[(size_t) i], &b[(size_t) i], 128);
                      if (i >= settle) { for (int q = 0; q < NB; ++q) vz[(size_t) q] += e.viz().curve[q]; ++nv; } }
                    for (int q = 0; q < NB; ++q) vz[(size_t) q] /= std::max (1, nv);
                    // "active" = how far the wet has moved from the dry, RELATIVE to the probe
                    double sd = 0, sx = 0;
                    for (int i = settle; i < N; ++i)
                    { const double d = (double) xx[(size_t) i] - (double) a[(size_t) i]; sd += d * d;
                      sx += (double) xx[(size_t) i] * (double) xx[(size_t) i]; }
                    const double act = std::sqrt (sd / std::max (1e-30, sx));
                    if (L == 0) { act0 = act; bestAct = act; PL = 0; x = xx; L1 = a; R1 = b; viz = vz; }
                    else if (act > bestAct && act > 1.2 * act0)
                    { bestAct = act; PL = L; x = xx; L1 = a; R1 = b; viz = vz; }
                    if (L == 0 && act > 0.25) break;             // plainly active at the bus: done
                }
                if (PL != 0) { ++nHotPick; picked += std::string (picked.empty() ? "" : "; ") + coord
                                                   + " at " + kLvlName[PL]; }

                // (a) Mix 0 == dry, bit-exact
                std::vector<float> L0 (x), R0 (x);
                { EQ e; e.prepare ((double) FS, 128); EQ::Params q = p; q.mix = 0.0f;
                  for (int i = 0; i < N; i += 128) { e.setParams (q); e.processStereo (&L0[(size_t) i], &R0[(size_t) i], 128); } }
                const bool dryOk = std::memcmp (L0.data() + settle, x.data() + settle,
                                                (size_t) NF * sizeof (float)) == 0;
                if (dryOk) ++nDry; else { addCapped (dryBad, nDryBad, coord); ++nDryBad; }

                std::vector<float> Lh (x), Rh (x);
                { EQ e; e.prepare ((double) FS, 128); EQ::Params q = p; q.mix = 0.5f;
                  for (int i = 0; i < N; i += 128) { e.setParams (q); e.processStereo (&Lh[(size_t) i], &Rh[(size_t) i], 128); } }

                // (b) the linear-mix null, in dB below the thing being crossfaded
                double se = 0, sd = 0;
                for (int i = settle; i < N; ++i)
                { const double want = 0.5 * ((double) x[(size_t) i] + (double) L1[(size_t) i]);
                  const double e2 = (double) Lh[(size_t) i] - want; se += e2 * e2;
                  const double d = 0.5 * ((double) x[(size_t) i] - (double) L1[(size_t) i]); sd += d * d; }
                const double linDb = 20.0 * std::log10 (std::max (1e-30, std::sqrt (se))
                                                      / std::max (1e-30, std::sqrt (sd)));
                if (linDb <= -60.0) ++nLin;
                else { addCapped (linBad, nLinBad, coord + fmt (" %.1f dB", linDb)); ++nLinBad; }
                if (linDb > worstLin) { worstLin = linDb; wlT = t; wlC = c; }

                // (c) the dry-fill test at the deepest LOCALLY FLAT bin (a +-1-bin plateau, so a
                //     narrow notch's centring cannot be mistaken for a leak), sine-probed —
                //     resolution-free — with the viz averaged over THAT SAME sine run, which is
                //     what makes the comparison honest on the level-dependent Type.
                int bi = 1; double bv = 1e9;
                for (int i = 1; i < NB - 1; ++i)
                { const double m = std::max (viz[(size_t) (i - 1)], std::max (viz[(size_t) i], viz[(size_t) (i + 1)]));
                  if (m < bv) { bv = m; bi = i; } }
                double vizAt = viz[(size_t) bi];
                const double meas = sineDbViz (p, EQ::curveBinHz (bi), kLvlRms[PL], 65536, bi, vizAt);
                const double d = meas - vizAt;
                const double leakFloor = vizAt - 7.69;      // a leak this loud lifts the floor 3 dB
                if (std::fabs (d) <= 3.0) ++nFill;
                else { addCapped (fillBad, nFillBad, coord + fmt2 (" viz %.1f vs measured %.1f dB", vizAt, meas));
                       ++nFillBad; }
                if (std::fabs (d) > worstFill) { worstFill = std::fabs (d); wfT = t; wfC = c; }
                deepestPerType[t] = std::min (deepestPerType[t], leakFloor);
            }

        note (fmt ("   %.0f of the 56 cells needed a program level other than the bus to be ACTIVE",
                   (double) nHotPick) + (picked.empty() ? "." : ":"));
        if (! picked.empty()) note ("   " + picked);
        gate ("Mix 0 % is the DRY signal BIT-EXACTLY, in all 56 cells",
              nDry == EQ::kNumTypes * EQ::kNumChars,
              fmt2 ("%.0f of %.0f cells bit-exact", (double) nDry, (double) (EQ::kNumTypes * EQ::kNumChars))
              + (dryBad.empty() ? "" : "  NOT DRY: " + dryBad + plusMore (nDryBad)));
        gate ("Mix 50 % is the exact LINEAR midpoint (null <= -60 dB), in all 56 cells",
              nLin == EQ::kNumTypes * EQ::kNumChars,
              fmt3 ("%.0f of %.0f cells; worst %.1f dB at ", (double) nLin,
                    (double) (EQ::kNumTypes * EQ::kNumChars), worstLin)
              + EQ::typeNames()[wlT] + " x `" + EQ::charNames (wlT)[wlC] + "`"
              + (linBad.empty() ? "" : "  FAILED: " + linBad + plusMore (nLinBad)));
        gate ("at Mix 100 % the measured floor REACHES the engine's own prediction, all 56 cells",
              nFill == EQ::kNumTypes * EQ::kNumChars,
              fmt3 ("%.0f of %.0f cells within 3 dB; worst %.2f dB at ", (double) nFill,
                    (double) (EQ::kNumTypes * EQ::kNumChars), worstFill)
              + EQ::typeNames()[wfT] + " x `" + EQ::charNames (wfT)[wfC] + "`"
              + (fillBad.empty() ? "" : "  FAILED: " + fillBad + plusMore (nFillBad)));

        // and the SENSITIVITY of (c), stated per Type rather than assumed: the contract's bar
        // is a dry residual under -60 dB, so at least one cell in every Type must be deep
        // enough to SEE a -60 dB leak, or the gate is decoration in that Type.
        std::string weak; int strong = 0;
        for (int t = 0; t < EQ::kNumTypes; ++t)
        { if (deepestPerType[t] <= -60.0) ++strong;
          else weak += std::string (weak.empty() ? "" : ", ") + EQ::typeNames()[t]
                     + fmt (" (best %.1f dB)", deepestPerType[t]); }
        gate ("   ... and every Type has a cell deep enough to SEE a -60 dB leak (law 3's bar)",
              strong == EQ::kNumTypes,
              fmt2 ("%.0f of %.0f Types; deepest leak-detection floors: ", (double) strong,
                    (double) EQ::kNumTypes)
              + fmt ("%.0f dB", deepestPerType[0]) + fmt (" / %.0f", deepestPerType[1])
              + fmt (" / %.0f", deepestPerType[2]) + fmt (" / %.0f", deepestPerType[3])
              + fmt (" / %.0f", deepestPerType[4]) + fmt (" / %.0f", deepestPerType[5])
              + fmt (" / %.0f", deepestPerType[6])
              + (weak.empty() ? "" : "  TOO SHALLOW: " + weak));
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("Q3. R11 ON EVERY CELL — the ceiling is not a property of Character 0");
    note ("PROBE: §K's own 'all four bands at 100 % with Amount 200 %' patch, replayed on all");
    note ("       56 cells. BAR: §K's own, MSD >= 55 dB. Where a Character caps the ceiling by");
    note ("       a mechanism it publishes, it is exempt by the asserted table above and must");
    note ("       still clear 32 dB — twice, in decibels, the loudest hardware EQ ever built.");
    {
        double v[EQ::kNumTypes][EQ::kNumChars]; int lv[EQ::kNumTypes][EQ::kNumChars];
        int nOk = 0, nCap = 0, nBad = 0; std::string bad, capBad, undeclCap;
        for (int t = 0; t < EQ::kNumTypes; ++t)
            for (int c = 0; c < EQ::kNumChars; ++c)
            {
                EQ::Params p = base(); p.type = t; p.character = c; p.f3 = 1.0f;
                p.b1 = fN (0,   90.0f);  p.b2 = 1.0f;
                p.b3 = fN (1,  550.0f);  p.b4 = 1.0f;
                p.b5 = fN (2, 3200.0f);  p.b6 = 1.0f;
                p.b7 = fN (3,12000.0f);  p.f2 = 1.0f; p.b8 = 0.6f;
                double best = -1e9; int bl = 0;
                for (int L = 0; L < 2; ++L)
                { const double m = specMax (transferOf (p, kLvlRms[L]));
                  if (m > best) { best = m; bl = L; }
                  if (L == 0 && best >= 55.0) break; }
                v[t][c] = best; lv[t][c] = bl;
                bool capped = false;
                for (int i = 0; i < kNumCapped; ++i) if (kCapped[i].t == t && kCapped[i].c == c) capped = true;
                const std::string coord = std::string (EQ::typeNames()[t]) + " x `" + EQ::charNames (t)[c] + "`";
                if (capped)
                { ++nCap;
                  if (best >= 55.0) undeclCap += std::string (undeclCap.empty() ? "" : ", ") + coord
                                              + fmt (" reaches %.1f dB — the cap claim is stale", best);
                  if (best < 32.0) capBad += std::string (capBad.empty() ? "" : ", ") + coord
                                          + fmt (" only %.1f dB", best); }
                else if (best >= 55.0) ++nOk;
                else { addCapped (bad, nBad, coord + fmt (" %.1f dB", best)); ++nBad; }
            }
        gate ("every UNCAPPED cell reaches §K's own ceiling bar (MSD >= 55 dB)",
              bad.empty(),
              fmt2 ("%.0f of %.0f uncapped cells", (double) nOk,
                    (double) (EQ::kNumTypes * EQ::kNumChars - kNumCapped))
              + (bad.empty() ? "" : "  UNDER: " + bad + plusMore (nBad)));
        gate ("the 3 capped cells are still DESTRUCTIVE (>= 32 dB) and their caps are not stale",
              capBad.empty() && undeclCap.empty() && nCap == kNumCapped,
              fmt ("%.0f capped cells", (double) nCap)
              + (capBad.empty() ? "" : "  TOO POLITE: " + capBad)
              + (undeclCap.empty() ? "" : "  STALE: " + undeclCap));
        for (int i = 0; i < kNumCapped; ++i)
            std::printf ("        capped: %-8s x %-13s %6.1f dB — %s\n",
                         EQ::typeNames()[kCapped[i].t], EQ::charNames (kCapped[i].t)[kCapped[i].c],
                         v[kCapped[i].t][kCapped[i].c], kCapped[i].why);
        note ("   per Type, the WEAKEST and STRONGEST of its 8 Characters:");
        for (int t = 0; t < EQ::kNumTypes; ++t)
        {
            int lo = 0, hi = 0;
            for (int c = 0; c < EQ::kNumChars; ++c)
            { if (v[t][c] < v[t][lo]) lo = c; if (v[t][c] > v[t][hi]) hi = c; }
            std::printf ("        %-9s %6.1f dB at %-13s ... %6.1f dB at %-13s%s\n",
                         EQ::typeNames()[t], v[t][lo], EQ::charNames (t)[lo], v[t][hi],
                         EQ::charNames (t)[hi], lv[t][lo] ? "  (weakest read at the quiet program)" : "");
        }
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Q4 — THE SECOND DROPDOWN, ON EVERY TYPE. R6 requires BOTH dropdowns to change
    //  PHYSICS, and fb425's law requires every dropdown OPTION to have its own audibility
    //  gate on every Type. Dropdown 1 (`Character`) already had one — §E measures all 28
    //  pairs inside every Type. Dropdown 2 (`Focus`) had only STRUCTURAL gates (§A's
    //  bit-exact pass-through of the untouched channel, the M/S round-trip residual, §H's
    //  mono behaviour): every one of them proves Focus does the RIGHT thing, and not one of
    //  them proves the five options are DIFFERENT from each other on a given Type. That is
    //  the Widen `Field` finding in this device's own back panel, so it is gated here:
    //  5 options, all 10 pairs, on all 7 Types = 70 measured comparisons.
    // ═════════════════════════════════════════════════════════════════════════
    section ("Q4. `Focus` — all 5 options measurably different from each other, on EVERY Type");
    note ("PROBE: DECORRELATED stereo noise (L and R from different seeds). With L == R the SIDE");
    note ("       signal is identically zero and `Side` would measure as `Stereo` — the probe");
    note ("       answering the question. METRIC: max |dB| between two options over BOTH channels'");
    note ("       96 bins. BAR: 1.5 dB, the same bar §E holds two Characters to.");
    {
        int nOk = 0; std::string bad; double worst = 1e9; int wt = 0, wa = 0, wb = 0;
        for (int t = 0; t < EQ::kNumTypes; ++t)
        {
            StRun S[EQ::kNumFocus];
            for (int f = 0; f < EQ::kNumFocus; ++f)
            { EQ::Params p = matPatch (t, 0); p.axis = f; S[f] = matRunStereo (p); }
            double closest = 1e9; int ca = 0, cb = 1;
            for (int a = 0; a < EQ::kNumFocus; ++a)
                for (int b = a + 1; b < EQ::kNumFocus; ++b)
                {
                    double m = 0;
                    for (int i = 0; i < NB; ++i)
                    { m = std::max (m, std::fabs (S[a].l[i] - S[b].l[i]));
                      m = std::max (m, std::fabs (S[a].r[i] - S[b].r[i])); }
                    if (m < closest) { closest = m; ca = a; cb = b; }
                }
            if (closest >= 1.5) ++nOk;
            else bad += std::string (bad.empty() ? "" : ", ") + EQ::typeNames()[t]
                      + fmt (" %.2f dB", closest);
            if (closest < worst) { worst = closest; wt = t; wa = ca; wb = cb; }
            std::printf ("        %-9s closest pair %6.2f dB = %s / %s\n", EQ::typeNames()[t],
                         closest, EQ::focusNames()[ca], EQ::focusNames()[cb]);
        }
        gate ("every Focus pair separates by >= 1.5 dB on every Type (70 comparisons)",
              nOk == EQ::kNumTypes,
              fmt3 ("%.0f of %.0f Types; worst pair %.2f dB", (double) nOk, (double) EQ::kNumTypes, worst)
              + " = " + EQ::typeNames()[wt] + " " + EQ::focusNames()[wa] + "/" + EQ::focusNames()[wb]
              + (bad.empty() ? "" : "  UNDER: " + bad));
    }
}

} // namespace


// ═════════════════════════════════════════════════════════════════════════════
//  §R — fb441: PER-BAND Q (the wheel on a node) and THE SEEDED FREE BELL
//
//  Max: "when I scroll and I try to do that notch... band eight or nine gets affected...
//  it shouldn't affect any other bands." The wheel used to drive Trait (the Type's global
//  width); it now drives ONE node's own Q multiplier. R3 is that sentence as a gate.
//
//  R5 is the fb441 engine bug: the per-block smoother glided sm_[0..10] only, so a free
//  bell added to a RUNNING device (sm_[11..18]) never arrived — 632.5 Hz / 0.00 dB forever.
//  Every other gate in this file starts a FRESH engine, which snaps on its first block and
//  therefore could not see it. R5 seeds first. It is RED on the fb438 engine.
// ═════════════════════════════════════════════════════════════════════════════
static void sectionR()
{
    std::printf ("\n── §R  fb441 — per-band Q isolation + the seeded free bell ──\n");
    auto bw3 = [] (const Spec& s) { const double pk = specMax (s); int n = 0; for (int i = 0; i < NB; ++i) if (s.db[i] > pk - 3.0) ++n; return n; };
    auto binOf = [] (double f) { return (int) std::lround (95.0 * std::log10 (f / 20.0) / 3.0); };
    {   // R1 — the default is the law, bit-exact
        EQ::Params a; a.type = 0; a.b4 = 0.9f;
        EQ::Params b = a; b.q1 = b.q2 = b.q3 = b.q4 = b.q5 = b.q6 = b.q7 = b.q8 = 0.5f;
        const Spec sa = transferOf (a), sb = transferOf (b);
        gate ("R1 per-band Q at 0.5 (x1) is BIT-EXACT to the Type's Q law", specDelta (sa, sb) == 0.0, fmt ("max delta %.6f dB", specDelta (sa, sb)));
    }
    {   // R2/R3 — Body Q x8: Body narrows; the OTHER bands' own centre gain and Q, as the ENGINE reports them, do not move
        EQ::Params a; a.type = 0; a.b2 = 0.8f; a.b4 = 0.9f; a.b6 = 0.8f; a.f2 = 0.8f;   // Low/Body/Bite/Air all boosted
        EQ::Params b = a; b.q2 = 1.0f;
        const Spec sa = transferOf (a), sb = transferOf (b);
        // isolate Body's own contribution: Body-only patches at x1 and x8
        EQ::Params ba; ba.type = 0; ba.b4 = 0.9f; EQ::Params bb = ba; bb.q2 = 1.0f;
        const Spec sBa = transferOf (ba), sBb = transferOf (bb);
        const int wA = bw3 (sBa), wB = bw3 (sBb);
        gate ("R2 Body Q x8 NARROWS Body (-3 dB width in bins shrinks >= 2x)", wB * 2 <= wA && wB >= 1, fmt2 ("x1 %.0f bins -> x8 %.0f bins", wA, wB));
        // the engine's own per-node report
        EQ ea, eb; ea.prepare ((double) FS, 128); eb.prepare ((double) FS, 128);
        std::vector<float> L (128, 0.0f), R (128, 0.0f);
        for (int k = 0; k < 40; ++k) { ea.setParams (a); ea.processStereo (L.data(), R.data(), 128); eb.setParams (b); eb.processStereo (L.data(), R.data(), 128); }
        const auto& za = ea.viz(); const auto& zb = eb.viz();
        bool same = true; double wq = 0, wg = 0;
        for (int n : { 0, 2, 3 }) { wq = std::max (wq, (double) std::fabs (za.nodeQ[n] - zb.nodeQ[n])); wg = std::max (wg, (double) std::fabs (za.nodeDb[n] - zb.nodeDb[n])); }
        same = (wq == 0.0 && wg == 0.0);
        gate ("R3 Body Q x8 leaves Low/Bite/Air's OWN Q and gain untouched (engine report)", same, fmt2 ("max |dQ| %.4f  max |dG| %.4f dB", wq, wg));
        gate ("R3b ...and Body's reported Q rose ~8x", zb.nodeQ[1] > za.nodeQ[1] * 4.0f, fmt2 ("Body Q %.2f -> %.2f", za.nodeQ[1], zb.nodeQ[1]));
        note (fmt2 ("   (for the record: response at Low 100 Hz moved %.2f dB, at Air 15.5 kHz %.2f dB — Body's SKIRT leaving, not Low/Air moving)",
                    sa.db[binOf (100.0)] - sb.db[binOf (100.0)], sa.db[binOf (15500.0)] - sb.db[binOf (15500.0)]));
    }
    {   // R4 — a free bell's own Q: x8 narrower, x1/8 wider than x1
        EQ::Params a; a.type = 0; a.x1 = 0.5f; a.x2 = 0.95f; a.xOn1 = true;
        EQ::Params n = a; n.q5 = 1.0f; EQ::Params w = a; w.q5 = 0.0f;
        const int b1 = bw3 (transferOf (a)), bn = bw3 (transferOf (n)), bwid = bw3 (transferOf (w));
        gate ("R4 free bell Q: x8 narrower, x1/8 wider (-3 dB bins)", bn < b1 && bwid > b1, fmt3 ("x1/8 %.0f  x1 %.0f  x8 %.0f", bwid, b1, bn));
    }
    {   // R5 — THE SEEDED FREE BELL
        EQ e; e.prepare ((double) FS, 128); EQ::Params p; p.type = 0;
        std::vector<float> L (128), R (128);
        auto run = [&] (int blocks) { for (int b = 0; b < blocks; ++b) { e.setParams (p);
            for (int i = 0; i < 128; ++i) { L[(size_t) i] = ((i * 7919) % 1000) / 1000.0f - 0.5f; R[(size_t) i] = L[(size_t) i]; }
            e.processStereo (L.data(), R.data(), 128); } };
        run (20);                                                    // SEEDED — a device that has been playing
        p.x1 = 0.9f; p.x2 = 1.0f; p.xOn1 = true; run (60);            // add Band 5 at ~10 kHz, +max, ON; ~160 ms later
        const auto& z = e.viz(); const double tgt = 20.0 * std::pow (1000.0, 0.9);
        gate ("R5 a free bell added to a SEEDED engine ARRIVES (Hz within 2 %, gain > +20 dB)",
              std::fabs (z.nodeHz[4] - tgt) / tgt < 0.02 && z.nodeDb[4] > 20.0, fmt2 ("hz %.1f  db %.2f", z.nodeHz[4], z.nodeDb[4]));
        gate ("R5b ...and reports a finite Q for it", std::isfinite (z.nodeQ[4]) && z.nodeQ[4] > 0.0f, fmt ("q %.2f", z.nodeQ[4]));
    }
}

int main()
{
    std::printf ("\n== TERRAIN EQUALIZER FX - certification ==   bus program -26 dBFS, fs %.0f Hz\n", (double) FS);
    if (EQ::mutationTag() != nullptr)
        std::printf ("\n🔴🔴🔴 MUTATED BUILD — %s\n"
                     "        The gates below are EXPECTED TO FAIL. See MUTATION.md.\n\n", EQ::mutationTag());
    std::printf ("   7 Types x 8 Characters x 5 Focus, 12 params (Slant Air Amount Mix + 8 back)\n");
    std::printf ("   ceiling: +-30 dB per band x Amount 200 %% = +-60 dB   ·   Q law owned by the Type\n");

    sectionA();
    sectionB();
    Feat F[EQ::kNumTypes];
    for (int t = 0; t < EQ::kNumTypes; ++t) F[t] = featureOf (t);
    sectionC (F);
    sectionD (F);
    sectionE();
    sectionF();
    sectionF1();
    sectionG();
    sectionH();
    sectionI();
    sectionJ();
    sectionK();
    sectionL();
    sectionM();
    sectionN (F);
    sectionO();
    sectionP();
    sectionQ();
    sectionR();   // fb441

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n", gPass, gFail);
    for (auto& s : gFails) std::printf ("   FAILED: %s\n", s.c_str());
    return gFail == 0 ? 0 : 1;
}
