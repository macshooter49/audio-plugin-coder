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
std::vector<float> toneN (int n, float hz, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = std::sin (6.2831853f * hz * (float) i / FS);
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
double specRmsLive (const Spec& a, const Spec& b)
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
double maxInRange (const Spec& a, double lo, double hi)
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
float tN (float db) { return 0.5f + 0.5f * db / EQ::kTiltDbSpan; }        // tilt dB -> 0..1
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

// impulse-response decay: T60 of the ring, in ms. The device is passive (no feedback
// loops), so this ALWAYS terminates - the number is how long a resonance sings for.
double t60ms (const EQ::Params& p)
{
    const int settle = 8192, N = (int) (FS * 1.5f);
    std::vector<float> L ((size_t)(settle + N), 0.0f), R ((size_t)(settle + N), 0.0f);
    L[(size_t) settle] = 0.25f; R[(size_t) settle] = 0.25f;
    EQ e; e.prepare ((double) FS, 128);
    for (int i = 0; i < settle + N; i += 128)
    { const int nn = std::min (128, settle + N - i); e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], nn); }
    double pk = 0; int pki = settle;
    std::vector<double> env; env.reserve ((size_t) N / 32);
    for (int i = settle; i + 32 < settle + N; i += 32)
    { double s = 0; for (int k = 0; k < 32; ++k) s += (double) L[(size_t)(i+k)] * L[(size_t)(i+k)];
      const double v = std::sqrt (s / 32.0); env.push_back (v);
      if (v > pk) { pk = v; pki = (int) env.size() - 1; } }
    if (pk <= 0) return 0.0;
    const double thr = pk * 0.001;
    for (size_t i = (size_t) pki; i < env.size(); ++i)
        if (env[i] < thr) return (double) (i - (size_t) pki) * 32.0 * 1000.0 / FS;
    return (double) N * 1000.0 / FS;
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
        EQ::Params p = refPatch (6, 7); p.f3 = 0.0f;      // Sculpt / Metal, everything lit
        auto x = chordN (8192); std::vector<float> L (x), R (x);
        EQ e; e.prepare ((double) FS, 128);
        for (int i = 0; i < 8192; i += 128) { e.setParams (p); e.processStereo (&L[(size_t)i], &R[(size_t)i], 128); }
        double w = 0; for (int i = 0; i < 8192; ++i) w = std::max (w, (double) std::fabs (L[(size_t)i] - x[(size_t)i]));
        gate ("Amount 0 % nulls BIT-EXACTLY at a full Sculpt/Metal patch", w == 0.0, fmt ("worst delta %.3e", w));
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
    // measured at each Type's DEFAULT Shape (0.5). At Shape 1.0 Surgical's `Width` is 40x
    // and its low SHELF resonates too, which muddied the Passive discriminator to within
    // 0.07 dB. At the default, Width is exactly 1.0 and Surgical has no resonance at all,
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

    gate ("Sculpt: a deep cut becomes a TRUE NOTCH, not a dip (sine probe)", F[6].notch <= -45.0,
          fmt2 ("notch floor %.1f dB (Surgical at the same knob: %.1f dB)", F[6].notch, F[0].notch));
    { const double a = t60ms (single (6, 0, 1, 24.0f, 1000.0f, 0.92f));
      const double b = t60ms (single (0, 0, 1, 24.0f, 1000.0f, 0.50f));
      gate ("   ... and it RINGS: T60 > 60 ms where Surgical does not", a > 60.0 && b < 25.0,
            fmt2 ("Sculpt/Ring T60 %.1f ms · Surgical T60 %.1f ms", a, b)); }
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
    rows.push_back ({ "Tilt   (spread 8 kHz - 80 Hz)",
        sweepParam ([] (float t) { EQ::Params p = base(); p.f1 = t; return p; },
                    [] (const Spec& s) { return atHz (s, 8000.0) - atHz (s, 80.0); }), 35.0 });
    rows.push_back ({ "Air    (level at 19 kHz, Reach 9 kHz)",
        sweepParam ([] (float t) { EQ::Params p = base(); p.b7 = fN (3, 9000.0f); p.f2 = t; return p; },
                    [] (const Spec& s) { return atHz (s, 19000.0); }), 45.0 });
    rows.push_back ({ "Amount (max spectral deviation)",
        sweepParam ([] (float t) { EQ::Params p = refPatch (0); p.f3 = t; return p; },
                    [] (const Spec& s) { return specMsd (s); }), 25.0 });
    rows.push_back ({ "Mix    (depth of a Sculpt notch)",
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

    // Shape (P8) is a different knob in every Type: seven sweeps, seven metrics.
    section ("F2. `Shape` — the P8 relabel, swept per Type on ITS OWN physics");
    struct SRow { int t; const char* nm; Sweep s; double need; };
    std::vector<SRow> sr;
    { Sweep sw; double lo = 1e9, hi = -1e9;
      for (int i = 0; i < 9; ++i)
      { sw.v[i] = std::log2 (std::max (0.004, bwSine (single (0, 0, 1, 20.0f, 550.0f, (float) i / 8.0f), 550.0)));
        lo = std::min (lo, sw.v[i]); hi = std::max (hi, sw.v[i]); }
      sw.span = hi - lo; sw.worstRev = 0;
      for (int i = 1; i < 9; ++i) sw.worstRev = std::max (sw.worstRev, sw.v[i] - sw.v[i-1]);
      sr.push_back ({ 0, "Surgical `Width`  (log2 bandwidth, sine-probed)", sw, 5.0 }); }
    sr.push_back ({ 1, "British  `Bump`   (shelf undershoot, dB)",
        sweepParam ([] (float t) { EQ::Params p = single (1, 0, 0, 24.0f, 90.0f, t); return p; },
                    [] (const Spec& s) { return minInRange (s, 110.0, 720.0); }), 4.0 });
    sr.push_back ({ 2, "American `Grip`   (log2 bandwidth at a SMALL +8 dB move)",
        sweepParam ([] (float t) { EQ::Params p = single (2, 0, 1, 8.0f, 550.0f, t); return p; },
                    [] (const Spec& s) { return std::log2 (std::max (0.05, bwOct (s))); }), 2.0 });
    sr.push_back ({ 3, "Passive  `Dip`    (Pultec scoop depth, dB)",
        sweepParam ([] (float t) { EQ::Params p = single (3, 0, 0, 18.0f, 60.0f, t); return p; },
                    [] (const Spec& s) { return minInRange (s, 150.0, 700.0); }), 4.0 });
    sr.push_back ({ 4, "Open     `Silk`   (level at 19 kHz, Air +20 @ Reach 9 kHz)",
        sweepParam ([] (float t) { EQ::Params p = single (4, 0, 3, 20.0f, 9000.0f, t); return p; },
                    [] (const Spec& s) { return atHz (s, 19000.0); }), 3.0 });
    sr.push_back ({ 5, "Dynamic  `Sense`  (applied cut at a -26 dBFS program, dB)",
        sweepParam ([] (float t) { EQ::Params p = single (5, 0, 1, -24.0f, 550.0f, t); return p; },
                    [] (const Spec& s) { return atHz (s, 550.0); }), 12.0 });
    { Sweep sw; double lo = 1e9, hi = -1e9;
      for (int i = 0; i < 9; ++i)
      { sw.v[i] = std::log2 (std::max (0.004, bwSine (single (6, 0, 1, 20.0f, 550.0f, (float) i / 8.0f), 550.0)));
        lo = std::min (lo, sw.v[i]); hi = std::max (hi, sw.v[i]); }
      sw.span = hi - lo; sw.worstRev = 0;
      for (int i = 1; i < 9; ++i) sw.worstRev = std::max (sw.worstRev, sw.v[i] - sw.v[i-1]);
      sr.push_back ({ 6, "Sculpt   `Ring`   (log2 bandwidth, sine-probed)", sw, 2.5 }); }
    for (auto& r : sr)
    {
        const bool ok = r.s.span >= r.need && r.s.worstRev <= 0.10 * r.s.span + 0.35;
        gate (r.nm, ok, fmt2 ("span %.2f (need %.2f)", r.s.span, r.need) + fmt (" · worst reversal %.2f", r.s.worstRev));
        note ("   0->100 %: " + sweepRow (r.s));
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
    {   // Inverted really is inverted, measured
        const Spec a = transferOf (single (5, 4, 1, -24.0f, 550.0f), 0.0056f);
        const Spec b = transferOf (single (5, 4, 1, -24.0f, 550.0f), 0.25f);
        const Spec c = transferOf (single (5, 0, 1, -24.0f, 550.0f), 0.0056f);
        const Spec d = transferOf (single (5, 0, 1, -24.0f, 550.0f), 0.25f);
        const double si = atHz (b, 550.0) - atHz (a, 550.0);
        const double sn = atHz (d, 550.0) - atHz (c, 550.0);
        gate ("Character `Inverted` reverses the sign of the ride", si * sn < 0.0,
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
    // has full energy and look in the hole. Sculpt's notch is designed to -90 dB, so a dry
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
    note ("-19.3 dB BY DESIGN in that Type. Sculpt's -90 dB hole above is the real dry gate.");
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
//      The first version of this gate reported an 11.03 dB "click" on Tilt at t = 0.251 s -
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

void sectionJ()
{
    section ("J. NO CLICKS (law 4) — every param swept under sustained white noise");
    static const char* nm[15] = { "Low Hz", "Low", "Body Hz", "Body", "Bite Hz", "Bite", "Reach",
                                  "Shape", "Tilt", "Air", "Amount", "Mix", "Type switch",
                                  "Character switch", "Focus switch" };
    double worst = -1e9; int wi = 0, ws = 12; double worstSw = -1e9, ctrlAt = 0, ctrlSw = 0;
    for (int i = 0; i < 15; ++i)
    { const double sweep = clickJump (i, 0);
      const double ctrl  = std::max (clickJump (i, 2), clickJump (i, 3));
      const double excess = sweep - ctrl;
      note (std::string ("   ") + nm[i] + fmt (" swept %.2f dB", sweep) + fmt (" · held %.2f dB", ctrl)
            + fmt (" · excess %+.2f dB", excess));
      if (i < 12) { if (excess > worst) { worst = excess; wi = i; ctrlAt = ctrl; } }
      else        { if (excess > worstSw) { worstSw = excess; ws = i; ctrlSw = ctrl; } } }
    gate ("all 12 CONTINUOUS params: MOVING adds <= 3 dB over HOLDING", worst <= 3.0,
          fmt2 ("worst excess %+.2f dB (held control %.2f) on ", worst, ctrlAt) + nm[wi]);
    // The three SWITCHES are not glides and must not be graded as if they were: a Type,
    // Character or Focus change snaps the whole filter bank, and the house grammar hides
    // that behind an 8 ms dip and a 30 ms recovery (fb345 fade-swap-recover). What is
    // gated here is that the dip is the ONLY event - smooth, bounded, and never a
    // sample-level discontinuity.
    gate ("the 3 SWITCHES ride the designed fade-swap and nothing else", worstSw <= 20.0,
          fmt2 ("worst excess %+.1f dB over held (control %.2f) — this IS the 8 ms dip, on ",
                worstSw, ctrlSw) + nm[ws]);
    // 🪤 NEGATIVE CONTROL, REQUIRED TO FAIL: the same gate on an INSTANTANEOUS param jump.
    { double w = -1e9; int k = 0;
      for (int i : { 1, 3, 5, 9, 10 })
      { const double e = clickJump (i, 1) - std::max (clickJump (i, 2), clickJump (i, 3));
        if (e > w) { w = e; k = i; } }
      gate ("   ... and the SAME gate FAILS on an instantaneous jump (control)", w > 3.0,
            fmt ("an instant jump adds %+.1f dB over holding, on ", w) + nm[k]); }
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
    { EQ::Params p = base(); p.f1 = 1.0f; p.f3 = 1.0f;
      const Spec s = transferOf (p);
      const double spread = specMax (s) - specMin (s);
      gate ("Tilt at 100 % with Amount 200 %: spectrum spread >= 28 dB AND +44 dB of lift",
            spread >= 28.0 && specMax (s) >= 44.0,
            fmt2 ("spread %.1f dB (%+.1f at the top", spread, specMax (s)) + fmt (", %+.1f at the bottom)", specMin (s)));
      note ("   a 6 dB/oct Baxandall seesaw's IN-BAND spread is capped by the PIVOT, not by");
      note ("   the gain: with a 700 Hz pivot the low side has 5.1 octaves to fall through, so");
      note ("   30.6 dB is the arithmetic maximum and the measurement lands on it. The knob's");
      note ("   ceiling is therefore expressed in absolute lift - +48 dB at 8 kHz - and the");
      note ("   `Deep Pivot` / `Bright Pivot` Characters move the pivot to buy more spread."); }
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
    { EQ::Params p = single (0, 0, 1, 24.0f, 550.0f, 1.0f);   // Width at maximum
      const double bw = bwSine (p, 550.0);
      gate ("Surgical `Width` at 100 % is a resonator, not an EQ curve", bw < 0.10,
            fmt2 ("half-height width %.4f octaves (%.0f Hz wide at 550 Hz)", bw,
                  550.0 * (std::pow (2.0, bw * 0.5) - std::pow (2.0, -bw * 0.5)))); }
    { const double t = t60ms (single (6, 1, 1, 28.0f, 550.0f, 1.0f));
      gate ("Sculpt `Ring` at 100 % SINGS: T60 > 250 ms", t > 250.0,
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
      gate ("a Q 90 Sculpt ring decays to TRUE zero (denormal flush)", tail == 0.0,
            fmt ("tail after 12 s of silence %.3e", tail)); }
    // 🔑 THE RING LAW, measured: no setting anywhere in the device may ring longer than 3 s.
    { double worst = 0; std::string wn;
      for (int t = 0; t < EQ::kNumTypes; ++t)
        for (float sh : { 0.5f, 0.95f, 1.0f })
          for (float hz : { 25.0f, 120.0f, 1000.0f })
          { const double v = t60ms (single (t, 1, hz < 100.0f ? 0 : 1, 30.0f, hz, sh));
            if (v > worst) { worst = v; wn = std::string (EQ::typeNames()[t]) + fmt (" @ %.0f Hz", hz)
                                            + fmt (", Shape %.2f", sh); } }
      gate ("NOTHING in the device rings longer than 3 s (the pole-radius cap)", worst <= 3100.0,
            fmt ("longest T60 %.0f ms — ", worst) + wn);
      note ("   without the cap a +72 dB 20 Hz band at Q 90 has Q_pole = 5670 and a TEN MINUTE");
      note ("   T60: passive, terminating, and still a drone. 'Nothing free-runs' is a number."); }
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
          p.b8 = 0.4f + 0.3f * (float) std::cos (i * 0.013);      // Ring too
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

} // namespace

int main()
{
    std::printf ("\n== TERRAIN EQUALIZER FX - certification ==   bus program -26 dBFS, fs %.0f Hz\n", (double) FS);
    std::printf ("   7 Types x 8 Characters x 5 Focus, 12 params (Tilt Air Amount Mix + 8 back)\n");
    std::printf ("   ceiling: +-30 dB per band x Amount 200 %% = +-60 dB   ·   Q law owned by the Type\n");

    sectionA();
    sectionB();
    Feat F[EQ::kNumTypes];
    for (int t = 0; t < EQ::kNumTypes; ++t) F[t] = featureOf (t);
    sectionC (F);
    sectionD (F);
    sectionE();
    sectionF();
    sectionG();
    sectionH();
    sectionI();
    sectionJ();
    sectionK();
    sectionL();
    sectionM();
    sectionN (F);

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n", gPass, gFail);
    for (auto& s : gFails) std::printf ("   FAILED: %s\n", s.c_str());
    return gFail == 0 ? 0 : 1;
}
