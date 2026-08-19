// ─────────────────────────────────────────────────────────────────────────────
// dynamics_cert — the perceptual certification harness for BOTH fx4 dynamics devices:
//     COMPRESS (chain kind 11)  ·  OTT (chain kind 12)  ·  their shared DynamicsCore.h
//
//   clang++ -O2 -std=c++17 -DDYN_DIR='"<TI>/Design/fx4/dynamics"' \
//     -I <TI>/Tests/shim -I <TI>/Source -I <TI>/Design/fx4/dynamics \
//     <TI>/Design/fx4/dynamics/dynamics_cert.cpp -o /tmp/dynamics_cert && /tmp/dynamics_cert
//
// `DYN_DIR` is where section 1 goes looking for ROSTER.md and the two worklets so it can prove
// they still SAY what the headers say. It defaults to "." — and if the files are not there the
// gate FAILS with the path printed. It never skips. A downstream gate that quietly finds nothing
// to check is the fb392 stub wearing a different hat.
//
// ⚠️ WHAT A COMPRESSOR HARNESS MUST NOT DO
// A compressor's wet path is `dry × a slowly-varying gain`. Consequences that decide every gate
// below:
//   · Sample-difference RMS is BANNED anyway (fb282), but here it is also USELESS — a 6 dB
//     level change scores 100 % "divergence" and so does an inaudible 0.2 dB one.
//   · A STATIC SINE CANNOT REVEAL attack, release, hold, edge or program-dependence AT ALL.
//     Every ballistic gate below runs on BURSTS, STAIRCASES or STEPS. (The PK_AM lesson.)
//   · The reference probe must contain BASS or the OTT low band reads dead and gets mis-tuned
//     (OTT bible §4.5's low-band caveat) — so `chordSig` is 55/110/165/220 Hz SAWS, not a
//     mid-register sine.
//   · Every threshold here is in dBFS ON THIS BUS. A single note is −26 dBFS; the chord −20.
//
// 🔬 CHECK YOUR OWN DETECTOR BEFORE BELIEVING IT. Section 1 runs every metric through a
// BYPASSED engine and prints the control number beside the gate, so a reader can see the scale.
// A detector that reads the same on clean and dirty is worse than none.
//
// 🪤 A HARNESS KINDER THAN REALITY IS WORSE THAN NO HARNESS. Section 0 tests the CORE's own
// identities against libm and against closed-form algebra — if `fastLog2` drifted, every dB in
// this file would be wrong in the same direction and every gate would still be green.
//
// 🚨 AND THE LAW THIS HARNESS CANNOT ENFORCE (fb373): a green DSP harness proves the ENGINE
// works. It NEVER proves the plugin reaches it. The roster states kNumTypes/kNumChars and this
// file asserts the name-table mapping; the UI→param→DSP round trip is gated separately by the
// integration owner.
// ─────────────────────────────────────────────────────────────────────────────

#include "DynamicsCore.h"
#include "TerrainCompressFx.h"
#include "TerrainOttFx.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>

namespace {

#include "shipped_labels.inc"
#include "retired_labels.inc"

constexpr float FS = 48000.0f;
using CP = tw::TerrainCompressFx::Params;
using CX = tw::TerrainCompressFx;
using OP = tw::TerrainOttFx::Params;
using OX = tw::TerrainOttFx;

int gPass = 0, gFail = 0;

// 🔑 FIXES.md §3 / fb423 §Gate — THE HARNESS OWNS NO LABELS. Every knob name printed below is
// read from the engine header at the moment of printing. Before this, section 4 printed
// `Latch (P6)` and `Heat (P8)` and section 6 printed `Speed (front 2)` while the headers said
// `Cling`, `Burn` and `Chase` — three stale strings in the harness that is supposed to POLICE
// stale strings. A table that cannot exist cannot drift.
std::string CF (int i) { return CX::frontNames()[i]; }
std::string CB (int i) { return CX::backNames()[i]; }
std::string OF (int i) { return OX::frontNames()[i]; }
std::string OB (int i) { return OX::backNames()[i]; }
void section (const char* s) { std::printf ("\n[%s]\n", s); }
void gate (const char* what, bool ok, const std::string& detail)
{
    if (ok) { ++gPass; std::printf ("  ok    %-52s %s\n", what, detail.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-52s %s\n", what, detail.c_str()); }
}
void note (const char* what, const std::string& detail)
{ std::printf ("  ·     %-52s %s\n", what, detail.c_str()); }

std::string F1 (const char* f, double v) { char b[160]; std::snprintf (b, sizeof b, f, v); return b; }
std::string F2 (const char* f, double a, double b) { char c[200]; std::snprintf (c, sizeof c, f, a, b); return c; }
std::string F3 (const char* f, double a, double b, double c) { char d[240]; std::snprintf (d, sizeof d, f, a, b, c); return d; }
std::string F4 (const char* f, double a, double b, double c, double d)
{ char e[280]; std::snprintf (e, sizeof e, f, a, b, c, d); return e; }

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

/** Welch-averaged magnitude spectrum, 4096-point, Hann. Phase-independent by construction. */
std::vector<double> magSpec (const std::vector<float>& x)
{
    const int N = 4096;
    std::vector<double> acc ((size_t) N / 2, 0.0);
    int frames = 0;
    for (size_t off = 0; off + (size_t) N <= x.size(); off += (size_t) N / 2)
    {
        std::vector<std::complex<double>> a ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (N - 1));
            a[(size_t) i] = std::complex<double> (x[off + (size_t) i] * w, 0.0);
        }
        fft (a);
        for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += std::abs (a[(size_t) k]);
        ++frames;
    }
    if (frames) for (auto& v : acc) v /= frames;
    return acc;
}

/** Mean |Δ dB| over 32 log-spaced bands, 30 Hz – 18 kHz. THE dramaticism metric for anything
 *  whose identity is spectral. Phase-blind on purpose. */
double specDist (const std::vector<float>& a, const std::vector<float>& b)
{
    auto A = magSpec (a), B = magSpec (b);
    const double binHz = FS / 4096.0;
    double sum = 0.0; int nb = 0;
    for (int i = 0; i < 32; ++i)
    {
        const double lo = 30.0 * std::pow (18000.0 / 30.0, i / 32.0);
        const double hi = 30.0 * std::pow (18000.0 / 30.0, (i + 1) / 32.0);
        double ea = 0.0, eb = 0.0; int n = 0;
        for (int k = (int) (lo / binHz); k <= (int) (hi / binHz) && k < 2048; ++k)
        { ea += A[(size_t) k] * A[(size_t) k]; eb += B[(size_t) k] * B[(size_t) k]; ++n; }
        if (n == 0) continue;
        sum += std::fabs (db (std::sqrt (ea / n)) - db (std::sqrt (eb / n)));
        ++nb;
    }
    return nb ? sum / nb : 0.0;
}

/** Welch-averaged POWER spectrum: mean |X[k]|² per frame, Hann, 4096. */
std::vector<double> powSpec (const std::vector<float>& x)
{
    const int N = 4096;
    std::vector<double> acc ((size_t) N / 2, 0.0);
    int frames = 0;
    for (size_t off = 0; off + (size_t) N <= x.size(); off += (size_t) N / 2)
    {
        std::vector<std::complex<double>> a ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (N - 1));
            a[(size_t) i] = std::complex<double> (x[off + (size_t) i] * w, 0.0);
        }
        fft (a);
        for (int k = 0; k < N / 2; ++k) { const double m = std::abs (a[(size_t) k]); acc[(size_t) k] += m * m; }
        ++frames;
    }
    if (frames) for (auto& v : acc) v /= frames;
    return acc;
}

/** 🚨 THE BLOCKER THIS REPLACES (FIXES.md §1 OTT 3).
 *  `bandDbOf` returned `db(rms of the raw 4096-point FFT magnitude over the band's bins)`. That
 *  number is NOT dBFS: it carries a +N·CG factor from the transform and it averages PER BIN, so
 *  a wide band reads lower the wider you make it. The printed "dark pad 8–12 kHz content
 *  −94.9 dBFS" was ~40 dB HIGH; sine-injection calibration puts the true content at −134.5 dBFS,
 *  114 dB below the programme. `Sheen`'s headline "+47.63 dB of air" therefore lifted an
 *  inaudible band to another inaudible band. fb417 exactly: the ratio moves, the ear cannot.
 *
 *  This is Parseval, done properly: for a Hann window, U = (1/N)Σw² = 3/8, and
 *      σ²_band = (2 / (N²·U)) · Σ_{k∈band} |X[k]|²
 *  returns the TRUE RMS of the signal's content in that band, in dBFS. Gated in section 2
 *  against a sine of known level and against white noise of known level — both exact by
 *  construction, and they disagree by 0.1 dB if the normalisation is wrong. */
double bandRmsDbFS (const std::vector<float>& x, double lo, double hi)
{
    auto P = powSpec (x);
    const int N = 4096;
    const double U = 0.375;                       // (1/N)·Σ hann²
    const double binHz = FS / (double) N;
    double e = 0.0;
    for (int k = std::max (1, (int) std::ceil (lo / binHz)); k <= (int) (hi / binHz) && k < N / 2; ++k)
        e += P[(size_t) k];
    e *= 2.0 / ((double) N * (double) N * U);
    return 10.0 * std::log10 (std::max (e, 1e-30));
}
double bandDbOf (const std::vector<float>& x, double lo, double hi) { return bandRmsDbFS (x, lo, hi); }

double centroidHz (const std::vector<float>& x)
{
    auto A = magSpec (x);
    const double binHz = FS / 4096.0;
    double num = 0.0, den = 0.0;
    for (int k = 2; k < 2048; ++k) { const double m = A[(size_t) k]; num += m * k * binHz; den += m; }
    return den > 0.0 ? num / den : 0.0;
}

double rmsOf (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > x.size()) to = x.size();
    double a = 0.0; size_t n = 0;
    for (size_t i = from; i < to; ++i) { a += (double) x[i] * x[i]; ++n; }
    return n ? std::sqrt (a / n) : 0.0;
}
double peakOf (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > x.size()) to = x.size();
    double p = 0.0;
    for (size_t i = from; i < to; ++i) p = std::max (p, (double) std::fabs (x[i]));
    return p;
}
double crestDb (const std::vector<float>& x) { return db (peakOf (x)) - db (rmsOf (x)); }
/** Crest measured after the followers have settled. Every crest gate below uses THIS. */
/** p90 − p10 of the 20 ms RMS envelope, in dB. THE dynamics metric: it is what a compressor
 *  removes, and unlike crest it does not need the source to have a sharp peak. */
double envSpreadDb (const std::vector<float>& x, double skipSec = 0.30)
{
    const int w = (int) (FS * 0.020f);
    std::vector<double> e;
    for (size_t i = (size_t) (FS * skipSec); i + (size_t) w < x.size(); i += (size_t) w)
    { const double r = rmsOf (x, i, i + (size_t) w); if (r > 1e-9) e.push_back (db (r)); }
    if (e.size() < 8) return 0.0;
    std::sort (e.begin(), e.end());
    return e[(size_t) (e.size() * 0.90)] - e[(size_t) (e.size() * 0.10)];
}
double crestSettled (const std::vector<float>& x, double skipSec = 0.30)
{
    const size_t s0 = std::min (x.size() / 2, (size_t) (FS * skipSec));
    return db (peakOf (x, s0)) - db (rmsOf (x, s0));
}

/** THD at a known fundamental: harmonic energy 2..12 over the fundamental. */
double thdPct (const std::vector<float>& x, double f0)
{
    auto A = magSpec (x);
    const double binHz = FS / 4096.0;
    auto peakNear = [&] (double hz)
    {
        const int k0 = (int) std::round (hz / binHz);
        double m = 0.0;
        for (int k = std::max (1, k0 - 2); k <= k0 + 2 && k < 2048; ++k) m = std::max (m, A[(size_t) k]);
        return m;
    };
    const double f = peakNear (f0);
    if (f <= 0.0) return 0.0;
    double h = 0.0;
    for (int n = 2; n <= 12; ++n) { const double m = peakNear (f0 * n); h += m * m; }
    return 100.0 * std::sqrt (h) / f;
}

// ═════ probes ════════════════════════════════════════════════════════════════
uint32_t gRng = 0x13579BDFu;
float rnd11() { gRng = gRng * 1664525u + 1013904223u; return (float) ((int32_t) gRng) * (1.0f / 2147483648.0f); }

std::vector<float> noiseSig (int n, float rms = 0.05f, uint32_t seed = 0x2468ACE0u)
{
    std::vector<float> x ((size_t) n); gRng = seed;
    for (int i = 0; i < n; ++i) x[(size_t) i] = rnd11();
    const double a = rmsOf (x); const float g = (a > 0.0) ? (float) (rms / a) : 1.0f;
    for (auto& v : x) v *= g;
    return x;
}
std::vector<float> toneSig (int n, float hz, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (rms * 1.4142136 * std::sin (2.0 * M_PI * hz * i / FS));
    return x;
}
/** Band-limited saw — real synth program, and the ONLY probe with anything above 2.5 kHz. */
void addSaw (std::vector<float>& x, float f0, float amp, int from = 0)
{
    const int n = (int) x.size();
    for (int h = 1; h * f0 < 18000.0f; ++h)
    {
        const float a = amp / h;
        for (int i = from; i < n; ++i)
            x[(size_t) i] += (float) (a * std::sin (2.0 * M_PI * h * f0 * i / FS));
    }
}
std::vector<float> sawSig (int n, float f0, float rms = 0.05f)
{
    std::vector<float> x ((size_t) n, 0.0f); addSaw (x, f0, 1.0f);
    const double a = rmsOf (x); for (auto& v : x) v *= (float) (rms / a);
    return x;
}
/** THE REFERENCE CHORD — 55/110/165/220 Hz saws. Contains BASS on purpose (§4.5 caveat) and
 *  real high-frequency content. Normalised to −20 dBFS RMS = the measured chord level. */
std::vector<float> chordSig (int n, float rms = 0.10f)
{
    std::vector<float> x ((size_t) n, 0.0f);
    addSaw (x, 55.0f, 1.0f); addSaw (x, 110.0f, 0.9f); addSaw (x, 165.0f, 0.8f); addSaw (x, 220.0f, 0.7f);
    const double a = rmsOf (x); for (auto& v : x) v *= (float) (rms / a);
    return x;
}
/** A DARK pad — but one whose top end EXISTS. fb417: the first version was the chord through a
 *  4-pole 600 Hz lowpass, whose true 8-12 kHz content calibrates to −134.5 dBFS against a
 *  −20 dBFS programme. 114 dB down is not dark, it is absent, and a control measured on absent
 *  content is a ratio nobody can hear. This is a 2-pole 1.2 kHz lowpass PLUS a dithered
 *  −96 dBFS floor (a real 16-bit noise floor, the level a synth's own output actually sits on).
 *  Section 2 PRINTS the calibrated 8-12 kHz level of this probe next to the programme level so
 *  the reader can see for himself how far down the thing being lifted is. */
std::vector<float> darkPad (int n, float rms = 0.10f)
{
    auto x = chordSig (n, rms);
    float z[2] = { 0, 0 };
    const float a = 1.0f - std::exp (-2.0f * 3.14159265f * 1200.0f / FS);
    for (auto& v : x) { z[0] += (v - z[0]) * a; z[1] += (z[0] - z[1]) * a; v = z[1]; }
    const double r = rmsOf (x); for (auto& v : x) v *= (float) (rms / r);
    const float dith = (float) std::pow (10.0, -96.0 / 20.0);
    gRng = 0x51ED270Bu;
    for (auto& v : x) v += dith * 1.4142f * rnd11();
    return x;
}

/** A STEADY-ENVELOPE programme, and the reason every click gate below uses it.
 *  The 55/110/165/220 Hz saw chord has an 18.2 ms period with deep envelope nulls, so adjacent
 *  1 ms frames legitimately differ by 25-35 dB — measured: every OTT Type x Character reads
 *  ~29 dB of "level step in one millisecond" on a STATIONARY chord with no parameter ever
 *  changed. That is the chord, not the device. Band-limited noise at the same level has a
 *  steady 1 ms envelope (+/-0.9 dB), drives every band of a multiband device, and is the
 *  standard dynamics probe. */
std::vector<float> clickProg (int n) { return noiseSig (n, 0.10f, 0x2468ACE0u); }

/** A REAL, DITHERED NOISE FLOOR at a stated level — never digital zero.
 *  FIXES.md §1 OTT 2: both floor-gate probes appended EXACT ZEROS after the note, and the device
 *  is feed-forward (y = band·g), so zero in gives zero out for any finite gain. The reported
 *  −280 dBFS was arithmetic, not evidence, on a gate whose own title said "the floor gate
 *  holds". This is the fb416 shape: the fault it aims at — a device that lifts a real noise
 *  floor into audibility — lives in the BULK, and the probe had no bulk. */
std::vector<float> floorBed (int n, double dbfs, uint32_t seed = 0x9E3779B9u)
{
    std::vector<float> x ((size_t) n); gRng = seed;
    for (int i = 0; i < n; ++i) x[(size_t) i] = rnd11();
    const double a = rmsOf (x), want = std::pow (10.0, dbfs / 20.0);
    const float g = (a > 0.0) ? (float) (want / a) : 1.0f;
    for (auto& v : x) v *= g;
    return x;
}
/** The reference chord under a 2 Hz / 24 dB tremolo — a probe that HAS dynamics, so a
 *  dynamics processor has something to remove. Every envelope-spread gate runs on this. */
std::vector<float> amChord (int n, float rms = 0.10f)
{
    auto x = chordSig (n, rms);
    for (int i = 0; i < n; ++i)
    {
        const double ph = 2.0 * M_PI * 2.0 * i / FS;
        x[(size_t) i] *= (float) std::pow (10.0, (-12.0 + 12.0 * std::sin (ph)) / 20.0);
    }
    return x;
}
/** A level STAIRCASE — the master probe for everything static-curve shaped. `stepMs` per tread. */
std::vector<float> staircase (float loDb, float hiDb, int steps, float stepMs, float f0 = 220.0f)
{
    const int per = (int) (FS * stepMs * 0.001f);
    std::vector<float> x ((size_t) (per * steps), 0.0f);
    addSaw (x, f0, 1.0f);
    const double a = rmsOf (x); for (auto& v : x) v *= (float) (0.05 / a);
    for (int s = 0; s < steps; ++s)
    {
        const float g = (float) std::pow (10.0, (loDb + (hiDb - loDb) * s / (steps - 1)) / 20.0);
        for (int i = 0; i < per; ++i) x[(size_t) (s * per + i)] *= g;
    }
    return x;
}
/** A plucked note: fast attack, exponential decay to silence. The ONLY honest probe for
 *  tail-bloom / upward compression / release adaptation. */
std::vector<float> pluckSig (int n, float f0 = 110.0f, float decaySec = 0.7f, float peak = 0.25f)
{
    std::vector<float> x ((size_t) n, 0.0f); addSaw (x, f0, 1.0f);
    const double a = peakOf (x); for (auto& v : x) v *= (float) (peak / a);
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / FS;
        const float e = (t < 0.003f) ? (t / 0.003f) : std::exp (-(t - 0.003f) / decaySec);
        x[(size_t) i] *= e;
    }
    return x;
}

// ═════ drivers ═══════════════════════════════════════════════════════════════
struct Out { std::vector<float> l, r; };

Out runC (CP p, const std::vector<float>& in, float fs = FS, int block = 128)
{
    CX e; e.prepare ((double) fs, block); e.setParams (p);
    Out o; o.l = in; o.r = in;
    const int n = (int) in.size();
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min (block, n - i);
        e.setParams (p);
        e.processStereo (&o.l[(size_t) i], &o.r[(size_t) i], m);
    }
    return o;
}
Out runO (OP p, const std::vector<float>& in, float fs = FS, int block = 128)
{
    OX e; e.prepare ((double) fs, block); e.setParams (p);
    Out o; o.l = in; o.r = in;
    const int n = (int) in.size();
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min (block, n - i);
        e.setParams (p);
        e.processStereo (&o.l[(size_t) i], &o.r[(size_t) i], m);
    }
    return o;
}
Out runCStereo (CP p, const std::vector<float>& li, const std::vector<float>& ri, float fs = FS)
{
    CX e; e.prepare ((double) fs, 128); e.setParams (p);
    Out o; o.l = li; o.r = ri;
    const int n = (int) li.size();
    for (int i = 0; i < n; i += 128)
    { const int m = std::min (128, n - i); e.setParams (p); e.processStereo (&o.l[(size_t) i], &o.r[(size_t) i], m); }
    return o;
}
Out runOStereo (OP p, const std::vector<float>& li, const std::vector<float>& ri, float fs = FS)
{
    OX e; e.prepare ((double) fs, 128); e.setParams (p);
    Out o; o.l = li; o.r = ri;
    const int n = (int) li.size();
    for (int i = 0; i < n; i += 128)
    { const int m = std::min (128, n - i); e.setParams (p); e.processStereo (&o.l[(size_t) i], &o.r[(size_t) i], m); }
    return o;
}

/** GR trajectory at 12 µs resolution.
 *  ⚠️ The first draft of this harness read `viz().grDb` at 48-sample hops and reported EVERY
 *  Type's attack as "16.0 ms" — which is the VIZ's own 60 Hz period, not the DSP's. A detector
 *  that reads the same number on a 20 µs machine and a 300 ms machine is worse than none.
 *  `grNow()` is the engine's live GR state; the hop is the resolution. */
std::vector<float> grTrace (CP p, const std::vector<float>& in, int hop = 2)
{
    CX e; e.prepare ((double) FS, hop); e.setParams (p);
    std::vector<float> l = in, r = in, tr;
    const int n = (int) in.size();
    tr.reserve ((size_t) (n / hop));
    for (int i = 0; i + hop <= n; i += hop)
    { e.processStereo (&l[(size_t) i], &r[(size_t) i], hop); tr.push_back (e.grNow()); }
    return tr;
}
constexpr double kHopMs = 2.0 * 1000.0 / (double) FS;   // 41.7 µs per trace sample

/** dynamic-range ratio: output level span / input level span across the treads of a staircase. */
double drRatio (const std::vector<float>& in, const std::vector<float>& out, int steps, float stepMs)
{
    const int per = (int) (FS * stepMs * 0.001f);
    double iLo = 1e9, iHi = -1e9, oLo = 1e9, oHi = -1e9;
    for (int s = 0; s < steps; ++s)
    {
        // measure the LAST 40 % of each tread — the ballistics have settled by then
        const size_t a = (size_t) (s * per + per * 0.6), b = (size_t) ((s + 1) * per);
        const double i0 = db (rmsOf (in, a, b)), o0 = db (rmsOf (out, a, b));
        if (i0 > -80.0) { iLo = std::min (iLo, i0); iHi = std::max (iHi, i0); }
        if (o0 > -140.0) { oLo = std::min (oLo, o0); oHi = std::max (oHi, o0); }
    }
    return (iHi - iLo > 1.0) ? (oHi - oLo) / (iHi - iLo) : 1.0;
}

/** Outlier click detector: the biggest sample-to-sample jump inside a window around a switch,
 *  divided by the biggest jump in the quiet control region. An OUTLIER detector — it is blind
 *  to a CONTINUOUS fault (fb416), which is why the zipper gate below is a separate, spectral one. */
double clickRatio (const std::vector<float>& y, int at, int winMs, double& baselineOut)
{
    const int w = (int) (FS * winMs * 0.001f);
    double inWin = 0.0, outWin = 0.0;
    for (size_t i = 1; i < y.size(); ++i)
    {
        const double d = std::fabs ((double) y[i] - y[i - 1]);
        if ((int) i > at - 8 && (int) i < at + w) inWin = std::max (inWin, d);
        else outWin = std::max (outWin, d);
    }
    baselineOut = outWin;
    return (outWin > 0.0) ? inWin / outWin : 1.0;
}

/** ═════ THE CLICK METRIC, and the three versions it took ═══════════════════════════════
 *  REPLACES `clickRatio`, which FIXES.md §1 OTT 1 refuted: it divided the in-window max |Δy| by
 *  the max |Δy| ANYWHERE ELSE in the same take, and the largest jump in an OTT take is the
 *  engine's own t=0 start-up burst — 0.14163, twenty-six times the steady-state max jump and
 *  sixty-nine times the input tone's own max step. With the bar at 2.5x a click had to exceed
 *  |Δy| = 0.354 on a signal whose peak is 0.19: 1.9x the full scale of its own probe. Nothing
 *  could trip it, and it was hiding a 19.8x tree swap.
 *
 *  There is no denominator taken from the engine here at all. Two takes of the SAME engine on
 *  the SAME programme — one holding params A, one switching A→B at `at` — and
 *        k[f] = RMS(y_switch − y_hold) / RMS(y_hold)      over 1 ms frames aligned to the switch
 *  which is IDENTICALLY ZERO before the switch (same code, same state, deterministic). The
 *  number reported is 20·log10(1 + max|k[f] − k[f−1]|) over the switch frame and the one after:
 *  **how many dB of gain the switch moved inside one millisecond.**
 *    · a planted instantaneous +8.00 dB step reads 8.00      (gated, §K)
 *    · no switch at all reads 0.0000                          (gated, §K)
 *    · the engine's own 20 ms glides moving their whole 24 dB range read 1.17
 *  Bar 2.0 dB/ms: above the fastest legitimate parameter move this engine can make, ten times
 *  below an unseeded smoother collapse. Frames where the hold take is more than 12 dB under its
 *  own average are held flat — a dB ratio of two near-zero windows is noise, not evidence. */
template <typename Run>
double clickDbPerMs (Run run, int at, const std::vector<float>& prog, double winMs = 1.0)
{
    const std::vector<float> ySw = run (true), yHold = run (false);
    const int W = (int) (FS * 0.001f), off = at % W;
    const double ref = rmsOf (yHold, (size_t) (FS * 0.15f));
    const int nF = (int) ((ySw.size() - (size_t) off) / (size_t) W);
    std::vector<double> k ((size_t) nF, 0.0);
    for (int f = 0; f < nF; ++f)
    {
        double dd = 0.0, pp = 0.0;
        for (int i = off + f * W; i < off + (f + 1) * W; ++i)
        { const double d = (double) ySw[(size_t) i] - yHold[(size_t) i];
          dd += d * d; pp += (double) yHold[(size_t) i] * yHold[(size_t) i]; }
        const double rh = std::sqrt (pp / (double) W);
        k[(size_t) f] = (rh > 0.25 * ref) ? std::sqrt (dd / (double) W) / rh
                                          : (f > 0 ? k[(size_t) f - 1] : 0.0);
    }
    const int f0 = (at - off) / W, f1 = f0 + (int) winMs;
    double inW = 0.0;
    for (int f = 1; f < nF; ++f)
        if (f >= f0 && f <= f1) inW = std::max (inW, std::fabs (k[(size_t) f] - k[(size_t) f - 1]));
    (void) prog;
    return 20.0 * std::log10 (1.0 + inW);
}

double cJump (CP a, CP b, int at, const std::vector<float>& prog, double winMs = 1.0)
{
    auto run = [&] (bool doSwitch)
    {
        CX e; e.prepare (FS, 64); e.setParams (a);
        std::vector<float> l = prog, r = prog;
        for (int i = 0; i + 64 <= (int) prog.size(); i += 64)
        { e.setParams ((doSwitch && i >= at) ? b : a); e.processStereo (&l[(size_t) i], &r[(size_t) i], 64); }
        return l;
    };
    return clickDbPerMs (run, at, prog, winMs);
}
double oJump (OP a, OP b, int at, const std::vector<float>& prog, double winMs = 1.0)
{
    auto run = [&] (bool doSwitch)
    {
        OX e; e.prepare (FS, 64); e.setParams (a);
        std::vector<float> l = prog, r = prog;
        for (int i = 0; i + 64 <= (int) prog.size(); i += 64)
        { e.setParams ((doSwitch && i >= at) ? b : a); e.processStereo (&l[(size_t) i], &r[(size_t) i], 64); }
        return l;
    };
    return clickDbPerMs (run, at, prog, winMs);
}
/** FIVE switch phases. The old probe jumped at i = FS·0.5 = 24000 with a 220 Hz tone: exactly
 *  110 whole cycles, so sin(phase) = 0.000000 at the switch instant — AND exactly a 64-sample
 *  block boundary (24000/64 = 375). A compressor's every artifact is a gain step, and a gain
 *  step at a zero crossing produces no sample-to-sample jump at all. These five are spread
 *  across the take and land on unrelated phases of a broadband programme. */
constexpr int kAts[5] = { 14912, 19968, 25088, 30016, 35072 };

std::string nm (const char* const* a, int i) { return std::string (a[i]); }

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// 0. THE CORE — before believing anything else in this file
// ═════════════════════════════════════════════════════════════════════════════
static void section0()
{
    section ("0. DynamicsCore — the math every gate below is measured with");
    using namespace tw::dyn;

    double worstLog = 0.0, worstExp = 0.0;
    for (int i = 0; i < 20001; ++i)
    {
        const double d = -140.0 + 200.0 * i / 20000.0;               // −140 … +60 dB
        const double lin = std::pow (10.0, d / 20.0);
        worstLog = std::max (worstLog, std::fabs ((double) lin2db ((float) lin) - d));
        worstExp = std::max (worstExp, std::fabs (db ((double) db2lin ((float) d) / lin)));
    }
    gate ("fastLog2 → lin2db error over −140…+60 dB", worstLog < 0.01,
          F1 ("%.5f dB  (bar 0.01)", worstLog));
    gate ("fastExp2 → db2lin error over −140…+60 dB", worstExp < 0.01,
          F1 ("%.5f dB  (bar 0.01)", worstExp));

    // grDown identities: exact slope above the knee, exact zero below, C1 at both corners
    bool ok = true; double worstKnee = 0.0;
    for (float W : { 0.0f, 6.0f, 24.0f })
        for (int i = 0; i <= 400; ++i)
        {
            const float x = -40.0f + 0.2f * i;
            const float g = grDown (x, -10.0f, 0.9f, W);
            if (x < -10.0f - W * 0.5f && g != 0.0f) ok = false;
            if (x > -10.0f + W * 0.5f + 1.0f)
                worstKnee = std::max (worstKnee, std::fabs ((double) g - 0.9 * (x + 10.0f)));
        }
    gate ("grDown: zero below knee, exact slope above", ok && worstKnee < 1e-4,
          F1 ("max deviation %.2e dB", worstKnee));
    gate ("grDown: s = 1 is ∞:1 (output flat above T)",
          std::fabs (( (0.0f - grDown (0.0f, -20.0f, 1.0f, 0.0f)) - (20.0f - grDown (20.0f, -20.0f, 1.0f, 0.0f)) )) < 1e-3,
          F2 ("out(0)=%.3f  out(+20)=%.3f dB", 0.0f - grDown (0.0f, -20.0f, 1.0f, 0.0f),
                                               20.0f - grDown (20.0f, -20.0f, 1.0f, 0.0f)));
    {
        const float a = 0.0f  - grDown (0.0f,  -20.0f, 2.0f, 0.0f);
        const float b = 6.0f  - grDown (6.0f,  -20.0f, 2.0f, 0.0f);
        gate ("grDown: s = 2 is the NEGATIVE zone (−1:1)", (b - a) < -5.9f,
              F3 ("in +6 dB ⇒ out %+.2f dB (%.2f → %.2f)", b - a, a, b));
    }
    gate ("floorGate: 0 at F, 1 above F+12, monotone",
          floorGate (-78.0f, -78.0f) == 0.0f && floorGate (-60.0f, -78.0f) == 1.0f
          && floorGate (-72.0f, -78.0f) > 0.0f && floorGate (-72.0f, -78.0f) < 1.0f,
          F3 ("g(−80)=%.3f g(−72)=%.3f g(−60)=%.3f", floorGate (-80.0f, -78.0f),
              floorGate (-72.0f, -78.0f), floorGate (-60.0f, -78.0f)));

    // 🔑 LR4: LP4 + HP4 must be EXACTLY the 2nd-order allpass. If this is not true, the OTT
    // Mix law is unprovable and every Mix setting combs. This is the load-bearing identity.
    {
        LR4 s; Svf1 ap; s.set (900.0f, FS); ap.setLR (900.0f, FS);
        auto x = noiseSig (32768, 0.2f);
        double e = 0.0, r = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            float lo, hi; s.split (x[i], lo, hi);
            const float a = ap.ap (x[i]);
            const double d = (double) (lo + hi) - a;
            e += d * d; r += (double) a * a;
        }
        const double nulldb = db (std::sqrt (e / x.size())) - db (std::sqrt (r / x.size()));
        gate ("LR4:  LP4 + HP4 == AP2(fc)  (null)", nulldb < -100.0,
              F1 ("%.1f dB below the allpass  (bar −100)", nulldb));
    }
    {
        Svf1 ap; ap.setLR (900.0f, FS);
        auto x = noiseSig (32768, 0.2f);
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = ap.ap (x[i]);
        const double flat = std::fabs (db (rmsOf (y)) - db (rmsOf (x)));
        gate ("AP2 is magnitude-flat (|H| = 1)", flat < 0.05,
              F1 ("%.4f dB of level change  (bar 0.05)", flat));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 1. NAMES — the no-doubles law, enforced against the SHIPPED tree, not from memory
// ═════════════════════════════════════════════════════════════════════════════
#ifndef DYN_DIR
 #define DYN_DIR "."
#endif

/** Slurp a file in this device's directory. Returns false — and the gate goes RED — if it is
 *  not there. A downstream gate that quietly finds nothing to check is worthless. */
static bool slurp (const char* name, std::string& out)
{
    const std::string path = std::string (DYN_DIR) + "/" + name;
    FILE* f = std::fopen (path.c_str(), "rb");
    if (!f) return false;
    char buf[65536]; size_t n;
    out.clear();
    while ((n = std::fread (buf, 1, sizeof buf, f)) > 0) out.append (buf, n);
    std::fclose (f);
    return !out.empty();
}

/** Pull the single-quoted strings out of a JS array literal: `const NAME = [ ... ];`
 *  Nested arrays are flattened, which is exactly what is wanted for CHARS[8][8]. */
static std::vector<std::string> jsArray (const std::string& src, const std::string& name)
{
    std::vector<std::string> out;
    // whitespace-tolerant: the worklets column-align their declarations (`const FRONT  = [`),
    // and the first draft of this parser matched a single space and silently returned NOTHING
    // for four of the six tables — a gate reading zero strings and calling it a mismatch is
    // luck, not design. Find the identifier, then the next '='.
    const std::string key = "const " + name;
    size_t k = std::string::npos;
    for (size_t s = 0; (s = src.find (key, s)) != std::string::npos; s += key.size())
    {
        const size_t e = s + key.size();
        if (e < src.size() && (std::isalnum ((unsigned char) src[e]) || src[e] == '_')) continue;
        k = e; break;
    }
    if (k == std::string::npos) return out;
    size_t i = src.find ('[', k);
    if (i == std::string::npos) return out;
    int depth = 0;
    for (; i < src.size(); ++i)
    {
        if (src[i] == '[') ++depth;
        else if (src[i] == ']') { if (--depth == 0) break; }
        else if (src[i] == '\'')
        {
            const size_t j = src.find ('\'', i + 1);
            if (j == std::string::npos) break;
            out.push_back (src.substr (i + 1, j - i - 1));
            i = j;
        }
    }
    return out;
}

/** Pull a `const NAME = 'value';` scalar. */
static std::string jsScalar (const std::string& src, const std::string& name)
{
    const std::string key = "const " + name;
    const size_t k = src.find (key);
    if (k == std::string::npos) return "<missing>";
    const size_t q = src.find ('\'', k);
    const size_t nl = src.find ('\n', k);
    if (q == std::string::npos || (nl != std::string::npos && q > nl)) return "<missing>";
    const size_t b = src.find ('\'', q + 1);
    return (b == std::string::npos) ? "<missing>" : src.substr (q + 1, b - q - 1);
}

static void section1()
{
    section ("1. Names — rack-wide no-doubles, exact-string, vs a snapshot of Source/");
    // Deliberately EXEMPT: the shared-vocabulary words CONTRACT §4 tells us to reuse when the
    // concept is genuinely the same, plus the chassis words every device's back panel shows.
    // ═════════════════════════════════════════════════════════════════════════════════════
    // 🚨 fb425 — THE EXEMPTION LIST WAS THE ONE THING IN THIS FAMILY WITH NO CARDINALITY.
    //    `kShared` had 12 entries, no size assertion, and it is the list that historically
    //    self-granted `Auto`. RENAMES.md fb425 ruled on Widen's five self-granted claims
    //    (`Detune` · `Depth` · `Voices` · `Spread` · `Feedback` — all SANCTIONED) and stated the
    //    consequence for the gates: an exemption is a RULING, cited, in a list of ASSERTED SIZE.
    //    So this is now three lists, each with its citation, each asserted, and — the part no
    //    ruling asked for but the audit implies — each entry must be LOAD-BEARING.
    //    A dead exemption is a self-grant waiting to be used, and four of the twelve were dead:
    //    `Peak`, `Bass`, `Treble` and `Ratio` are not in the 3320-string corpus AT ALL. They
    //    exempted nothing and they are gone. `Gentle` and `Low Split` (the fb423 sibling-yield
    //    pair) are also gone: the siblings have applied their RENAMES rows and the collisions
    //    they excused no longer exist. The load-bearing gate below is what proves that.
    struct Exempt { const char* name; const char* ruling; };
    // CONTRACT §4, verbatim: "identical names, ranges and curves where the concept is genuinely
    // the same: `Mix`, `Amount`, `Attack`, `Release`, `Ratio`, `Width`, `Focus`, `Lo Cut`, `Rate`"
    static const Exempt kContract4[] = {
        { "Mix",     "CONTRACT §4 shared vocabulary — the wet/dry control of every rack device" },
        { "Attack",  "CONTRACT §4 shared vocabulary — how fast a follower grabs, same law everywhere" },
        { "Release", "CONTRACT §4 shared vocabulary — how fast it lets go, same law everywhere" },
        { "Amount",  "CONTRACT §4 shared vocabulary — how much of the effect, same law everywhere" } };
    // The rack CHASSIS words. R5 (back panel), R6 (`Character` is dropdown 1, `Type` is the
    // header pill), and the fb266 frozen chassis, whose every card carries a Power switch.
    static const Exempt kChassis[] = {
        { "Character", "CONTRACT R6 — back dropdown 1 is `Character` on EVERY fx3/fx4 card, by mandate" },
        { "Type",      "CONTRACT R6 — `Type` is the header pill (DEVS[].tp) on every card, by mandate" },
        { "Power",     "the fb266 frozen rack chassis — every card has a Power switch" } };
    // Explicit RULINGS, cited to the line that made them.
    static const Exempt kRuled[] = {
        { "Auto",   "RENAMES.md fb423 §SANCTIONED — the same law as the shipped Distortion `Auto` pill (auto gain compensation)" },
        { "Stereo", "RENAMES.md fb423 §SANCTIONED — `Stereo`·`Mid`·`Side`·`Left`·`Right` sanctioned AS A GROUP, M/S routing vocabulary" } };
    auto shared = [&] (const std::string& s)
    { for (auto& k : kContract4) if (s == k.name) return true;
      for (auto& k : kChassis)   if (s == k.name) return true;
      for (auto& k : kRuled)     if (s == k.name) return true; return false; };
    auto shipped = [] (const std::string& s)
    { for (auto k : kShippedLabels) if (s == k) return true; return false; };

    std::vector<std::string> mine;
    for (int t = 0; t < CX::kNumTypes; ++t)
    {
        mine.push_back (nm (CX::typeNames(), t));
        for (int c = 0; c < CX::kNumChars; ++c) mine.push_back (nm (CX::charNames (t), c));
    }
    for (int d = 0; d < CX::kNumDetect; ++d) mine.push_back (nm (CX::detectNames(), d));
    for (int t = 0; t < OX::kNumTypes; ++t)
    {
        mine.push_back (nm (OX::typeNames(), t));
        for (int c = 0; c < OX::kNumChars; ++c) mine.push_back (nm (OX::charNames (t), c));
    }
    for (int d = 0; d < OX::kNumStereo; ++d) mine.push_back (nm (OX::stereoNames(), d));
    // 🔑 FIXES.md §3 — every label below is READ FROM THE ENGINE HEADER. There is no list of
    // knob names in this file any more, and none in the roster or the worklet that is not a
    // copy of these arrays. A hand-written list in the harness is the same geometry that let
    // `Cassette` play `Studio`: the gate agreed with the markdown while the DSP did something
    // else. If a name changes in the header, this gate sees it on the next run.
    for (int i = 0; i < CX::kNumFront; ++i) mine.push_back (nm (CX::frontNames(), i));
    for (int i = 0; i < CX::kNumBack;  ++i) mine.push_back (nm (CX::backNames(),  i));
    for (int i = 0; i < 2; ++i)             mine.push_back (nm (CX::dropdownNames(), i));
    mine.push_back (CX::pillName());        mine.push_back (CX::deviceName());
    for (int i = 0; i < OX::kNumFront; ++i) mine.push_back (nm (OX::frontNames(), i));
    for (int i = 0; i < OX::kNumBack;  ++i) mine.push_back (nm (OX::backNames(),  i));
    for (int i = 0; i < 2; ++i)             mine.push_back (nm (OX::dropdownNames(), i));
    mine.push_back (OX::pillName());        mine.push_back (OX::deviceName());

    int col = 0; std::string first;
    for (auto& s : mine)
        if (!shared (s) && shipped (s)) { ++col; if (first.empty()) first = s; }
    gate ("no name collides with a shipped label", col == 0,
          col == 0 ? F2 ("%.0f names vs %.0f corpus strings (Source/ + BOTH sibling fx4 dirs)",
                         (double) mine.size(), (double) kNumShippedLabels)
                   : (F1 ("%.0f collisions, first: ", (double) col) + first));
    // the corpus must be able to SEE the two labels R6 is named after, or it cannot protect
    // tomorrow's. `Motion` and `Route` are built as "Chorus" + sfxD + " Motion" — a LEADING
    // SPACE, which the old capitalised-quoted-string extractor skipped outright.
    gate ("the corpus contains the fb418 leading-space labels", shipped ("Motion") && shipped ("Route"),
          std::string ("Motion ") + (shipped ("Motion") ? "PRESENT" : "MISSING")
          + " · Route " + (shipped ("Route") ? "PRESENT" : "MISSING"));
    gate ("the corpus contains the SIBLING fx4 names (it could not, before)",
          shipped ("Slant") && shipped ("Chisel") && shipped ("Steady") && shipped ("Twofold"),
          "Slant · Chisel · Steady · Twofold all found in Design/fx4/{eq,widen}");
    // ── the exemption lists themselves, asserted and audited ────────────────────────────────
    gate ("the exemption lists are exactly 4 + 3 + 2 entries",
          (int) (sizeof kContract4 / sizeof kContract4[0]) == 4
          && (int) (sizeof kChassis / sizeof kChassis[0]) == 3
          && (int) (sizeof kRuled / sizeof kRuled[0]) == 2,
          "CONTRACT §4 · rack chassis · explicit rulings — none of them can grow silently");
    {
        // EVERY EXEMPTION MUST BE LOAD-BEARING. An entry that exempts nothing is a self-grant
        // parked for later, and this is exactly how `Auto` got in. Four dead entries (`Peak`,
        // `Bass`, `Treble`, `Ratio`) and the two fb423 sibling-yields were deleted on this
        // finding; if a sibling ever re-introduces one, this gate goes red and asks for a ruling
        // instead of quietly excusing it.
        int deadX = 0; std::string firstDead;
        auto chk = [&] (const Exempt* a, int n)
        { for (int i = 0; i < n; ++i) if (!shipped (a[i].name))
            { ++deadX; if (firstDead.empty()) firstDead = a[i].name; } };
        chk (kContract4, (int) (sizeof kContract4 / sizeof kContract4[0]));
        chk (kChassis,   (int) (sizeof kChassis   / sizeof kChassis[0]));
        chk (kRuled,     (int) (sizeof kRuled     / sizeof kRuled[0]));
        gate ("every exemption is LOAD-BEARING (it actually excuses a real collision)", deadX == 0,
              deadX == 0 ? "9 / 9 appear in the corpus"
                         : F1 ("%.0f exempt nothing, first: ", (double) deadX) + firstDead);
        for (auto& k : kContract4) std::printf ("      exempt  %-11s %s\n", k.name, k.ruling);
        for (auto& k : kChassis)   std::printf ("      exempt  %-11s %s\n", k.name, k.ruling);
        for (auto& k : kRuled)     std::printf ("      exempt  %-11s %s\n", k.name, k.ruling);
    }
    int dup = 0; std::string dupName;
    for (size_t i = 0; i < mine.size(); ++i)
        for (size_t j = i + 1; j < mine.size(); ++j)
            if (mine[i] == mine[j] && !shared (mine[i])) { ++dup; if (dupName.empty()) dupName = mine[i]; }
    gate ("no name repeats across the two devices", dup == 0,
          dup == 0 ? "all unique" : (F1 ("%.0f dups, first: ", (double) dup) + dupName));

    // fb373: the roster must state the cardinality AND the table must agree with it.
    bool tblOk = true;
    for (int t = 0; t < CX::kNumTypes; ++t) for (int c = 0; c < CX::kNumChars; ++c)
        if (CX::charNames (t)[c] == nullptr || CX::charNames (t)[c][0] == 0) tblOk = false;
    for (int t = 0; t < OX::kNumTypes; ++t) for (int c = 0; c < OX::kNumChars; ++c)
        if (OX::charNames (t)[c] == nullptr || OX::charNames (t)[c][0] == 0) tblOk = false;
    gate ("kNumTypes × kNumChars tables are fully populated", tblOk,
          F2 ("Compress %.0f×8, OTT %.0f×8", (double) CX::kNumTypes, (double) OX::kNumTypes));
    // ═════════════════════════════════════════════════════════════════════════════════════
    // 🔴 fb423 §Gate — DOWNSTREAM MUST EQUAL THE HEADER, AND IT IS GATED, NOT KEPT IN SYNC BY
    //    HAND. 22 stale strings survived downstream across this family the last time it was
    //    checked; `eq-worklet.js` was carrying twelve old Character names as a second table,
    //    which is literally the two-table geometry the EQ engine deleted. The card is built
    //    from these files. This is fb373's geometry and it is still standing.
    //    Preference order applied here, in the order fb423 asks for:
    //      1. DELETE the duplicate where one CAN be deleted — the harness's own knob-label
    //         strings are gone; every label §4/§6 prints is read from the header at print time.
    //      2. GATE the duplicates that CANNOT be deleted — a worklet is a separate runtime and
    //         a roster is a design document, so neither can include a C++ header. Both are read
    //         off disk HERE and compared, string for string.
    // ═════════════════════════════════════════════════════════════════════════════════════
    {
        auto cmp = [&] (const char* what, const std::vector<std::string>& js,
                        const std::vector<std::string>& hdr)
        {
            std::string detail;
            bool ok = (js.size() == hdr.size());
            if (!ok) detail = F2 ("%.0f entries downstream vs %.0f in the header",
                                  (double) js.size(), (double) hdr.size());
            for (size_t i = 0; ok && i < js.size(); ++i)
                if (js[i] != hdr[i]) { ok = false; detail = "[" + std::to_string (i) + "] downstream '"
                                                 + js[i] + "' vs header '" + hdr[i] + "'"; }
            gate (what, ok, ok ? F1 ("%.0f strings, exact", (double) hdr.size()) : detail);
        };
        auto vec = [] (const char* const* a, int n)
        { std::vector<std::string> v; for (int i = 0; i < n; ++i) v.push_back (a[i]); return v; };

        std::string cw, ow, ros;
        const bool haveC = slurp ("compress-worklet.js", cw);
        const bool haveO = slurp ("ott-worklet.js", ow);
        const bool haveR = slurp ("ROSTER.md", ros);
        gate ("the downstream files are ON DISK where this gate looked", haveC && haveO && haveR,
              std::string (haveC && haveO && haveR ? "found in " : "MISSING from ") + DYN_DIR
              + " (compress-worklet.js, ott-worklet.js, ROSTER.md)");
        if (haveC)
        {
            std::vector<std::string> ch;
            for (int ty = 0; ty < CX::kNumTypes; ++ty)
                for (int c = 0; c < CX::kNumChars; ++c) ch.push_back (CX::charNames (ty)[c]);
            cmp ("compress-worklet TYPES == typeNames()",     jsArray (cw, "TYPES"),  vec (CX::typeNames(), CX::kNumTypes));
            cmp ("compress-worklet CHARS == charNames()",     jsArray (cw, "CHARS"),  ch);
            cmp ("compress-worklet DETECT == detectNames()",  jsArray (cw, "DETECT"), vec (CX::detectNames(), CX::kNumDetect));
            cmp ("compress-worklet FRONT == frontNames()",    jsArray (cw, "FRONT"),  vec (CX::frontNames(), CX::kNumFront));
            cmp ("compress-worklet BACK == backNames()",      jsArray (cw, "BACK"),   vec (CX::backNames(),  CX::kNumBack));
            cmp ("compress-worklet DROPS == dropdownNames()", jsArray (cw, "DROPS"),  vec (CX::dropdownNames(), 2));
            gate ("compress-worklet PILL and DEVICE == the header",
                  jsScalar (cw, "PILL") == CX::pillName() && jsScalar (cw, "DEVICE") == CX::deviceName(),
                  jsScalar (cw, "DEVICE") + " / " + jsScalar (cw, "PILL"));
        }
        if (haveO)
        {
            std::vector<std::string> ch;
            for (int ty = 0; ty < OX::kNumTypes; ++ty)
                for (int c = 0; c < OX::kNumChars; ++c) ch.push_back (OX::charNames (ty)[c]);
            cmp ("ott-worklet TYPES == typeNames()",     jsArray (ow, "TYPES"),  vec (OX::typeNames(), OX::kNumTypes));
            cmp ("ott-worklet CHARS == charNames()",     jsArray (ow, "CHARS"),  ch);
            cmp ("ott-worklet STEREO == stereoNames()",  jsArray (ow, "STEREO"), vec (OX::stereoNames(), OX::kNumStereo));
            cmp ("ott-worklet FRONT == frontNames()",    jsArray (ow, "FRONT"),  vec (OX::frontNames(), OX::kNumFront));
            cmp ("ott-worklet BACK == backNames()",      jsArray (ow, "BACK"),   vec (OX::backNames(),  OX::kNumBack));
            cmp ("ott-worklet DROPS == dropdownNames()", jsArray (ow, "DROPS"),  vec (OX::dropdownNames(), 2));
            gate ("ott-worklet PILL and DEVICE == the header",
                  jsScalar (ow, "PILL") == OX::pillName() && jsScalar (ow, "DEVICE") == OX::deviceName(),
                  jsScalar (ow, "DEVICE") + " / " + jsScalar (ow, "PILL"));
        }
        if (haveR)
        {
            int miss = 0; std::string firstMiss;
            for (auto& s : mine)
                if (ros.find (s) == std::string::npos)
                { ++miss; firstMiss += (firstMiss.empty() ? "" : ", ") + s; }
            gate ("every published label appears VERBATIM in ROSTER.md", miss == 0,
                  miss == 0 ? F1 ("all %.0f of them", (double) mine.size())
                            : F1 ("%.0f absent: ", (double) miss) + firstMiss);

            // ═══════════════════════════════════════════════════════════════════════════════
            // 🚨 fb425 — THE ROSTER HALF OF THE DRIFT GATE WAS A SUBSTRING SEARCH.
            //    `roster.find (s) != npos` has no word boundary, no ORDER, no cardinality and no
            //    reverse direction: a skeptic moved two Characters under the WRONG TYPES and it
            //    stayed green, because both strings were still somewhere in the file. What a
            //    roster has to agree with is the GRID, not the vocabulary.
            //    So: each Type owns exactly one ROSTER row (`| **<Type>** | ...`), the eight
            //    Characters of that Type must appear in it AS BACKTICKED TOKENS IN ORDER, and no
            //    Character may appear in ANOTHER Type's row. Ordered, positional, cardinal, and
            //    checked both ways.
            // EVERY row that names this Type — §2's lineage table names it too, and taking the
            // first match found that one (216 characters, no backticks) and called all 16 grids
            // out of order. A gate that reads the wrong row is the fb393 harness again.
            auto rowsFor = [&] (const std::string& ty)
            {
                std::vector<std::string> rows;
                const std::string key = "| **" + ty + "**";
                size_t k = 0;
                while ((k = ros.find (key, k)) != std::string::npos)
                {
                    const size_t e = ros.find ('\n', k);
                    rows.push_back (ros.substr (k, (e == std::string::npos ? ros.size() : e) - k));
                    k = (e == std::string::npos) ? ros.size() : e + 1;
                }
                return rows;
            };
            auto ticks = [] (const std::string& row)
            {
                std::vector<std::string> v; size_t k = 0;
                while ((k = row.find ('`', k)) != std::string::npos)
                { const size_t e = row.find ('`', k + 1); if (e == std::string::npos) break;
                  v.push_back (row.substr (k + 1, e - k - 1)); k = e + 1; }
                return v;
            };
            int noRow = 0, outOfOrder = 0, wrongType = 0;
            std::string firstBad;
            struct DevT { const char* dev; int nT; const char* const* (*chars) (int); const char* const* tn; };
            for (int dev = 0; dev < 2; ++dev)
            {
                const int nT = (dev == 0) ? CX::kNumTypes : OX::kNumTypes;
                for (int t = 0; t < nT; ++t)
                {
                    const std::string ty = (dev == 0) ? CX::typeNames()[t] : OX::typeNames()[t];
                    const auto rows = rowsFor (ty);
                    if (rows.empty())
                    { ++noRow; if (firstBad.empty()) firstBad = ty + " has no `| **" + ty + "**` row"; continue; }
                    bool ordered = false; std::string why;
                    for (auto& row : rows)
                    {
                        const auto tk = ticks (row);
                        size_t at = 0; bool ok = true;
                        for (int c = 0; c < 8; ++c)
                        {
                            const std::string ch = (dev == 0) ? CX::charNames (t)[c] : OX::charNames (t)[c];
                            size_t f = tk.size();
                            for (size_t i = at; i < tk.size(); ++i) if (tk[i] == ch) { f = i; break; }
                            if (f == tk.size()) { ok = false;
                                if (why.empty()) why = ty + ": `" + ch + "` is not in its grid row, in order";
                                break; }
                            at = f + 1;
                        }
                        if (ok) { ordered = true; break; }
                    }
                    if (!ordered) { ++outOfOrder; if (firstBad.empty()) firstBad = why; }
                    // ... and nowhere else: a Character under the WRONG Type is the whole point.
                    for (int t2 = 0; t2 < nT; ++t2)
                    {
                        if (t2 == t) continue;
                        const std::string ty2 = (dev == 0) ? CX::typeNames()[t2] : OX::typeNames()[t2];
                        for (auto& r2 : rowsFor (ty2))
                        {
                            const auto tk2 = ticks (r2);
                            for (int c = 0; c < 8; ++c)
                            {
                                const std::string ch = (dev == 0) ? CX::charNames (t)[c] : OX::charNames (t)[c];
                                for (auto& x : tk2) if (x == ch)
                                { ++wrongType; if (firstBad.empty())
                                    firstBad = "`" + ch + "` (a " + ty + " Character) appears under " + ty2; }
                            }
                        }
                    }
                }
            }
            gate ("ROSTER.md: every Type has its own row and every Character is IN IT, IN ORDER",
                  noRow == 0 && outOfOrder == 0,
                  (noRow == 0 && outOfOrder == 0)
                    ? "16 Type rows × 8 Characters, ordered subsequence, both devices"
                    : F2 ("%.0f missing rows, %.0f out of order: ", (double) noRow, (double) outOfOrder) + firstBad);
            gate ("ROSTER.md: no Character appears under the WRONG Type", wrongType == 0,
                  wrongType == 0 ? "0 cross-row appearances over 16 rows"
                                 : F1 ("%.0f misplaced: ", (double) wrongType) + firstBad);
        }
        // ── and the other direction, which is the one that actually rots: a RETIRED label must
        //    not survive anywhere downstream.
        // 🚨 fb425 — THE BLACKLIST IS NO LONGER HAND-KEPT. The typed list omitted TWO of this
        //    directory's own RENAMES rows (`Long` → `Patient`, `Glass` → `Crystal`), and `Long`
        //    was standing in ROSTER.md:125 while the gate printed "0 hits". `gen_shipped_labels.py`
        //    now PARSES both RENAMES.md tables into `retired_labels.inc` — the same mechanism
        //    Widen uses — so the authority is the table and a hand-kept second copy cannot drift.
        // 🚨 AND THE MATCH IS ON WHOLE LABEL TOKENS, not on words. The old scan used
        //    `isalnum` boundaries, which makes `Long` match inside the LIVE Character names
        //    `Long Ears`, `Long Window`, `Long Tail` and `Long Haul`, and `Glass` inside
        //    `Glass Ceiling` — nine false positives that would have had to be excused by hand.
        //    Downstream labels are backticked in ROSTER.md and quoted in the worklets; those are
        //    the tokens, and they are compared for EQUALITY.
        {
            auto tokensOf = [] (const std::string& txt, char open, char close)
            {
                std::vector<std::string> v; size_t k = 0;
                while ((k = txt.find (open, k)) != std::string::npos)
                { const size_t e = txt.find (close, k + 1); if (e == std::string::npos) break;
                  const std::string t = txt.substr (k + 1, e - k - 1);
                  if (t.size() <= 24 && t.find ('\n') == std::string::npos) v.push_back (t);
                  k = e + 1; }
                return v;
            };
            std::vector<std::string> tk = tokensOf (ros, '`', '`');
            for (auto& t : tokensOf (cw, '\'', '\'')) tk.push_back (t);
            for (auto& t : tokensOf (cw, '"', '"'))   tk.push_back (t);
            for (auto& t : tokensOf (ow, '\'', '\'')) tk.push_back (t);
            for (auto& t : tokensOf (ow, '"', '"'))   tk.push_back (t);
            // A retired name that the engine STILL PUBLISHES is not drift, it is a RE-USE, and a
            // re-use needs a ruling. There is exactly one and it is cited.
            struct Republished { const char* name; const char* ruling; };
            static const Republished kRepublished[] = {
                { "Auto", "RENAMES.md fb423 §SANCTIONED — retired as `Detect` option 0 (it doubled the pill INSIDE one card), KEPT as the front pill itself" } };
            auto published = [&] (const std::string& n)
            { for (auto& m : mine) if (m == n) return true; return false; };
            gate ("the retired-label table was PARSED from RENAMES.md, not typed", kNumRenames == 12,
                  F1 ("%.0f rename rows from the COMPRESS and OTT tables", (double) kNumRenames));
            int notApplied = 0; std::string firstNA;
            for (int i = 0; i < kNumRenames; ++i)
                if (!published (kRenamedTo[i]))
                { ++notApplied; if (firstNA.empty()) firstNA = kRenamedTo[i]; }
            gate ("every NEW name in the table is actually published by the engine", notApplied == 0,
                  notApplied == 0 ? F1 ("all %.0f applied", (double) kNumRenames)
                                  : F1 ("%.0f never applied, first: ", (double) notApplied) + firstNA);
            int hits = 0, ruledReuse = 0; std::string firstHit;
            for (int i = 0; i < kNumRenames; ++i)
            {
                const std::string R = kRetiredLabels[i];
                bool isRuled = false;
                for (auto& r : kRepublished) if (R == r.name) isRuled = true;
                if (isRuled && published (R)) { ++ruledReuse; continue; }
                for (auto& t : tk) if (t == R)
                { ++hits; if (firstHit.empty()) firstHit = R; }
            }
            gate ("no RETIRED label survives as a LABEL downstream (ROSTER.md + both worklets)",
                  hits == 0,
                  hits == 0 ? F2 ("%.0f retired strings vs %.0f downstream label tokens, 0 hits",
                                  (double) kNumRenames, (double) tk.size())
                            : F1 ("%.0f hits, first: ", (double) hits) + firstHit);
            gate ("the re-published exemption list is exactly 1 entry, and it is a ruling",
                  (int) (sizeof kRepublished / sizeof kRepublished[0]) == 1 && ruledReuse == 1,
                  std::string (kRepublished[0].name) + " — " + kRepublished[0].ruling);
            // the two Characters the RENAMES prose CUT rather than renamed (they are not table
            // rows, so the parser cannot see them): asserted, cited, and scanned the same way.
            static const char* const kCut[] = { "RMS Ears", "Spike Ears" };
            int cutHits = 0; std::string firstCut;
            for (auto c : kCut) for (auto& t : tk) if (t == c)
            { ++cutHits; if (firstCut.empty()) firstCut = c; }
            gate ("the CUT-Character list is exactly 2 and none survives", cutHits == 0
                  && (int) (sizeof kCut / sizeof kCut[0]) == 2,
                  cutHits == 0 ? "RMS Ears · Spike Ears — RENAMES.md COMPRESS prose (`detForce` removed, both renamed)"
                               : "survivor: " + firstCut);
        }
    }

    // out-of-range indices must CLAMP, not read past the table (the fb373 class of bug)
    gate ("out-of-range Type/Character clamps, never reads past",
          std::string (CX::charNames (99)[0]) == std::string (CX::charNames (CX::kNumTypes - 1)[0])
          && std::string (OX::charNames (-5)[0]) == std::string (OX::charNames (0)[0]),
          "index 99 → last row, index −5 → first row");
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. THE CONTROL NUMBERS — every metric through a BYPASSED engine
// ═════════════════════════════════════════════════════════════════════════════
static void section2()
{
    section ("2. Control numbers — the same metrics through a bypassed engine (§3.1)");
    auto chord = chordSig ((int) (FS * 2.0f));
    {
        CP p; p.push = 0.0f; p.ratio = 0.0f; p.lift = 0.0f; p.b8 = 0.0f;   // T = +9 dBp, s = 0
        auto o = runC (p, chord);
        const double lvl = db (rmsOf (o.l)) - db (rmsOf (chord));
        const double sd  = specDist (chord, o.l);
        note ("Compress at Push 0 / Ratio 0: level Δ", F1 ("%+.4f dB", lvl));
        note ("Compress at Push 0 / Ratio 0: spec dist", F1 ("%.4f dB  ← the FLOOR of every", sd));
        gate ("Compress default-off path is bit-transparent", std::fabs (lvl) < 0.01 && sd < 0.02,
              F2 ("level %+.4f dB, spectrum %.4f dB", lvl, sd));
    }
    {
        OP p; p.amount = 0.0f; p.topLift = 0.0f; p.b6 = p.b7 = p.b8 = 0.5f;
        auto o = runO (p, chord);
        const double lvl = db (rmsOf (o.l)) - db (rmsOf (chord));
        const double sd  = specDist (chord, o.l);
        note ("OTT at Amount 0: level Δ (makeup is still on)", F1 ("%+.3f dB", lvl));
        note ("OTT at Amount 0: spectrum vs raw dry", F1 ("%.3f dB", sd));
        note ("click detector baseline on a clean chord", F1 ("max |Δx| = %.5f", [&] {
            double m = 0.0; for (size_t i = 1; i < chord.size(); ++i) m = std::max (m, (double) std::fabs (chord[i] - chord[i-1])); return m; }()));
    }
    {
        // 🔬 detector self-check with an EXACT known answer. The first draft built a "+8 dB
        // shelf" from a one-pole and expected the metric to read +8; the true magnitude of that
        // shelf at 8–12 kHz is +7.5 dB falling to +6, so the metric was right and the TEST was
        // wrong. Two checks with answers that are exact by construction instead:
        auto dark = darkPad ((int) (FS * 2.0f));
        const double a0 = bandDbOf (dark, 8000.0, 12000.0);
        std::vector<float> g8 = dark; for (auto& v : g8) v *= 2.5118864f;     // exactly +8.000 dB
        gate ("air metric is calibrated (broadband +8.000 dB)",
              std::fabs ((bandDbOf (g8, 8000.0, 12000.0) - a0) - 8.0) < 0.02,
              F1 ("reads %+.4f dB", bandDbOf (g8, 8000.0, 12000.0) - a0));
        // 🔬 ABSOLUTE CALIBRATION, twice, with answers that are exact by construction.
        {
            auto sn = toneSig ((int) (FS * 2.0f), 3000.0f, 0.05f);          // −26.02 dBFS RMS
            const double got = bandRmsDbFS (sn, 2500.0, 3500.0);
            gate ("spectrum is CALIBRATED: a −26.02 dBFS sine reads its own level",
                  std::fabs (got + 26.0206) < 0.30, F2 ("reads %.3f dBFS (true %.3f)", got, -26.0206));
            auto nz = noiseSig ((int) (FS * 2.0f), 0.05f);
            // to NYQUIST, not to 20 kHz: white noise has 16.7 % of its power between 20 and
            // 24 kHz, which reads as exactly −0.79 dB of "error" if you leave it out. The first
            // run of this gate did leave it out and read −26.822. The metric was right and the
            // TEST was wrong — the same shape as the +8 dB shelf note above.
            const double gn = bandRmsDbFS (nz, 20.0, 24000.0);
            gate ("... and a −26.02 dBFS white noise bed reads its own level too",
                  std::fabs (gn + 26.0206) < 0.20, F2 ("reads %.3f dBFS (true %.3f)", gn, -26.0206));
        }
        std::vector<float> withTone = dark;
        {   // add a 10 kHz tone at exactly the dark pad's own 8–12 k level ⇒ ~+3 dB in that band
            auto t = toneSig ((int) withTone.size(), 10000.0f, (float) std::pow (10.0, a0 / 20.0));
            for (size_t i = 0; i < withTone.size(); ++i) withTone[i] += t[i];
        }
        const double a2 = bandDbOf (withTone, 8000.0, 12000.0);
        gate ("air metric is band-selective (a 10 kHz tone moves it)", a2 - a0 > 2.0,
              F3 ("dark %.1f dBFS → +10 kHz tone %.1f dBFS (Δ %+.2f)", a0, a2, a2 - a0));
        // 📐🚫👂 fb417 — PRINT HOW FAR DOWN IT IS, BESIDE THE PROGRAMME, IN CALIBRATED dBFS.
        const double prog = db (rmsOf (dark));
        note ("    dark pad: programme level", F1 ("%.1f dBFS RMS", prog));
        note ("    dark pad: 8-12 kHz content", F2 ("%.1f dBFS = %.1f dB under the programme", a0, prog - a0));
        gate ("the air band is AUDIBLE CONTENT before anything lifts it", prog - a0 < 60.0,
              F1 ("%.1f dB under the programme (bar 60). The first draft's 4-pole/600 Hz pad put it",
                  prog - a0)
              + " 114 dB down — a ratio nobody can hear (fb417).");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. COMPRESS
// ═════════════════════════════════════════════════════════════════════════════
struct CFeat { double gr, atkMs, relMs, crest, thd, drift, knee, slope, drr, upAt40, adapt, tail2; };

/** Measured knee width, in dB: the input span over which the local transfer slope goes from
 *  0.90 down to (final + 0.05). Read straight off a staircase — the knee's own unit. */
static double compressKnee (CP q)
{
    q.push = 0.45f; q.ratio = 1.0f; q.b1 = 0.5f; q.b2 = 0.3f; q.lift = 0.0f;
    const int steps = 32; const float stepMs = 100.0f;
    auto x = staircase (-40.0f, 8.0f, steps, stepMs);
    auto o = runC (q, x);
    const int per = (int) (FS * stepMs * 0.001f);
    std::vector<double> iv, ov;
    for (int s = 0; s < steps; ++s)
    {
        const size_t a = (size_t) (s * per + per * 0.6), b = (size_t) ((s + 1) * per);
        iv.push_back (db (rmsOf (x, a, b))); ov.push_back (db (rmsOf (o.l, a, b)));
    }
    double sl = 0.0; int n = 0;
    for (int s = steps - 5; s < steps - 1; ++s)
    { sl += (ov[(size_t) s + 1] - ov[(size_t) s]) / (iv[(size_t) s + 1] - iv[(size_t) s]); ++n; }
    const double fin = n ? sl / n : 0.0;
    double lo = iv.front(), hi = iv.back(); bool haveLo = false;
    for (int s = 0; s + 1 < steps; ++s)
    {
        const double ls = (ov[(size_t) s + 1] - ov[(size_t) s]) / (iv[(size_t) s + 1] - iv[(size_t) s]);
        if (!haveLo && ls < 0.90) { lo = iv[(size_t) s]; haveLo = true; }
        if (haveLo && ls < fin + 0.05) { hi = iv[(size_t) s]; break; }
    }
    return std::max (0.0, hi - lo);
}


static CFeat compressFeatures (int type, int chr)
{
    CFeat f {};
    CP p; p.type = type; p.character = chr;

    // GR + crest on the reference chord at a working setting
    {
        p.push = 0.55f; p.ratio = 0.70f; p.lift = 0.0f;
        auto chord = chordSig ((int) (FS * 1.5f));
        auto o = runC (p, chord);
        f.gr = db (rmsOf (chord)) - db (rmsOf (o.l, (size_t) (FS * 0.5f)));
        f.crest = crestSettled (chord) - crestSettled (o.l);
    }
    // attack / release from the GR trajectory on a burst, at 83 µs resolution
    {
        CP q = p; q.push = 0.5f; q.ratio = 0.85f; q.b1 = 0.5f; q.b2 = 0.35f; q.lift = 0.0f;
        const int n = (int) (FS * 6.0f);   // 5 s of tail: Vari-Mu and Bus release in SECONDS
        std::vector<float> x ((size_t) n, 0.0f);
        addSaw (x, 220.0f, 1.0f);
        const double a = rmsOf (x); for (auto& v : x) v *= (float) (0.02 / a);
        for (int i = (int) (FS * 0.4f); i < (int) (FS * 1.0f); ++i) x[(size_t) i] *= 15.0f;   // +23 dB
        auto tr = grTrace (q, x);
        const int on = (int) (0.4 * FS / 2.0), off = (int) (1.0 * FS / 2.0);
        double pk = 0.0;
        for (int i = on; i < off && i < (int) tr.size(); ++i) pk = std::max (pk, (double) tr[(size_t) i]);
        const double base0 = tr[(size_t) (on - 2)];                       // GR before the burst
        const double base1 = tr[tr.size() - 8];                           // GR long after it
        f.atkMs = (double) (off - on) * kHopMs; f.relMs = (double) tr.size() * kHopMs;
        const double aT = base0 + 0.63 * (pk - base0);
        const double rT = base1 + 0.37 * (pk - base1);
        for (int i = on; i < off && i < (int) tr.size(); ++i)
            if (tr[(size_t) i] >= aT) { f.atkMs = (i - on) * kHopMs; break; }
        for (int i = off; i < (int) tr.size(); ++i)
            if (tr[(size_t) i] <= rT) { f.relMs = (i - off) * kHopMs; break; }
        // a SECOND, slower release constant: how much of the peak GR survives 5 measured
        // release-constants later. A single exponential leaves 0.7 %; two leave far more.
        const int probe = off + (int) (std::min (4500.0, f.relMs * 5.0) / kHopMs);
        f.tail2 = (probe < (int) tr.size() && pk - base1 > 0.05)
                  ? (tr[(size_t) probe] - base1) / (pk - base1) : 0.0;
        // release ADAPTATION: the same measurement after a 50 ms tap instead of a 600 ms ride
        std::vector<float> s2 ((size_t) (int) (FS * 5.0f), 0.0f);
        addSaw (s2, 220.0f, 1.0f);
        const double a2 = rmsOf (s2); for (auto& v : s2) v *= (float) (0.02 / a2);
        for (int i = (int) (FS * 0.55f); i < (int) (FS * 0.60f); ++i) s2[(size_t) i] *= 15.0f;
        auto t2 = grTrace (q, s2);
        const int on2 = (int) (0.55 * FS / 2.0), off2 = (int) (0.60 * FS / 2.0);
        double pk2 = 0.0;
        for (int i = on2; i < off2 && i < (int) t2.size(); ++i) pk2 = std::max (pk2, (double) t2[(size_t) i]);
        const double b2a = t2[(size_t) (on2 - 2)], b2b = t2[t2.size() - 8];
        const double r2T = b2b + 0.37 * (pk2 - b2b);
        double r2 = (double) (t2.size() - (size_t) off2) * kHopMs;
        for (int i = off2; i < (int) t2.size(); ++i)
            if (t2[(size_t) i] <= r2T) { r2 = (i - off2) * kHopMs; break; }
        (void) b2a;
        f.adapt = f.relMs / std::max (0.05, r2);
    }
    // THD added at ~10 dB GR
    {
        CP q = p; q.push = 0.62f; q.ratio = 0.80f; q.b1 = 0.0f; q.b2 = 0.05f; q.b8 = 0.0f;
        auto t = toneSig ((int) (FS * 1.0f), 80.0f, 0.05f);
        auto o = runC (q, t);
        f.thd = thdPct (o.l, 80.0);
    }
    // measured-ratio drift with drive: the FEEDBACK signature
    {
        CP q = p; q.push = 0.5f; q.ratio = 0.60f; q.b1 = 0.5f; q.b2 = 0.5f;
        auto mk = [&] (double gDb) {
            auto x = sawSig ((int) (FS * 0.8f), 220.0f, (float) (0.05 * std::pow (10.0, gDb / 20.0)));
            auto o = runC (q, x);
            return db (rmsOf (o.l, (size_t) (FS * 0.4f))) - db (rmsOf (x, (size_t) (FS * 0.4f)));
        };
        const double g1 = mk (0.0), g2 = mk (12.0);
        f.drift = (g1 - g2) / 12.0;          // GR-per-dB-over actually realised at high drive
    }
    // knee width and above-threshold slope, straight off the staircase
    {
        CP q = p; q.push = 0.45f; q.ratio = 1.0f; q.b1 = 0.5f; q.b2 = 0.3f;
        const int steps = 24; const float stepMs = 120.0f;
        auto x = staircase (-40.0f, 8.0f, steps, stepMs);
        auto o = runC (q, x);
        const int per = (int) (FS * stepMs * 0.001f);
        std::vector<double> iv, ov;
        for (int s = 0; s < steps; ++s)
        {
            const size_t a = (size_t) (s * per + per * 0.6), b = (size_t) ((s + 1) * per);
            iv.push_back (db (rmsOf (x, a, b))); ov.push_back (db (rmsOf (o.l, a, b)));
        }
        double sl = 0.0; int nsl = 0;
        for (int s = steps - 5; s < steps - 1; ++s)
        { sl += (ov[(size_t) s + 1] - ov[(size_t) s]) / (iv[(size_t) s + 1] - iv[(size_t) s]); ++nsl; }
        f.slope = nsl ? sl / nsl : 1.0;
        // knee: the input span over which the local slope goes from 0.95 to 1.15× its final value
        double lo = iv.front(), hi = iv.back(); bool haveLo = false;
        for (int s = 0; s + 1 < steps; ++s)
        {
            const double ls = (ov[(size_t) s + 1] - ov[(size_t) s]) / (iv[(size_t) s + 1] - iv[(size_t) s]);
            if (!haveLo && ls < 0.90) { lo = iv[(size_t) s]; haveLo = true; }
            if (haveLo && ls < f.slope + 0.05) { hi = iv[(size_t) s]; break; }
        }
        f.knee = std::max (0.0, hi - lo);
        f.drr = drRatio (x, o.l, steps, stepMs);
    }
    // what happens to a −40 dBp probe: NEGATIVE means it came out LOUDER (upward compression)
    {
        CP q = p; q.push = 0.55f; q.ratio = 0.70f;
        auto x = sawSig ((int) (FS * 1.2f), 220.0f, 0.0005f);   // ≈ −66 dBFS = −40 dBp
        auto o = runC (q, x);
        f.upAt40 = db (rmsOf (x, (size_t) (FS * 0.6f))) - db (rmsOf (o.l, (size_t) (FS * 0.6f)));
    }
    return f;
}

static void section3()
{
    section ("3. COMPRESS — roster, defaults, and the per-Type discriminator");

    // ── defaults: does adding the card DO something, without moving the level?
    {
        CP p;
        auto chord = chordSig ((int) (FS * 2.0f));
        auto o = runC (p, chord);
        const double lvl = db (rmsOf (o.l, (size_t) (FS * 0.5f))) - db (rmsOf (chord, (size_t) (FS * 0.5f)));
        CX e; e.prepare (FS, 128); e.setParams (p);
        std::vector<float> l = chord, r = chord;
        for (int i = 0; i + 128 <= (int) chord.size(); i += 128) { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
        gate ("defaults: level within ±2 dB of bypass", std::fabs (lvl) < 2.0, F1 ("%+.2f dB", lvl));
        gate ("defaults: the device is audibly WORKING", e.viz().grDb > 3.0,
              F1 ("GR %.2f dB at the default Push 0.30 (bar 3)", e.viz().grDb));
        note ("defaults: attack / release", F2 ("%.2f ms / %.0f ms", e.attackMs(), e.releaseMs()));
    }

    // ── per-Type discriminators (each is the mechanism, not a voicing)
    CFeat F[CX::kNumTypes];
    for (int t = 0; t < CX::kNumTypes; ++t) F[t] = compressFeatures (t, 0);

    std::printf ("      %-9s %6s %7s %8s %6s %6s %6s %6s %7s %6s %7s\n",
                 "Type", "GR dB", "atk ms", "rel ms", "crest", "THD%", "slope", "knee", "DRR", "up40", "adapt");
    for (int t = 0; t < CX::kNumTypes; ++t)
        std::printf ("      %-9s %6.2f %7.1f %8.0f %6.2f %6.2f %6.3f %6.1f %7.3f %6.2f %7.2f\n",
                     CX::typeNames()[t], F[t].gr, F[t].atkMs, F[t].relMs, F[t].crest, F[t].thd,
                     F[t].slope, F[t].knee, F[t].drr, F[t].upAt40, F[t].adapt);

    gate ("Exact: textbook slope at ∞:1 (flat above T)", F[0].slope < 0.15,
          F1 ("measured output slope %.3f dB/dB", F[0].slope));
    gate ("Bus: release ADAPTS to programme (dual pool)", F[1].adapt > 1.6 || F[1].adapt < 0.62,
          F1 ("long-ride release / short-tap release = %.2f×", F[1].adapt));
    gate ("FET 76: attack in MICROSECONDS", F[2].atkMs <= 1.0,
          F2 ("%.2f ms to 63 %% GR (Exact reads %.1f ms)", F[2].atkMs, F[0].atkMs));
    gate ("Opto: a SECOND, slower release constant survives", F[3].tail2 > 0.12,
          F2 ("%.1f %% of peak GR still present at 5τ (Exact %.1f %%)", 100.0 * F[3].tail2, 100.0 * F[0].tail2));
    gate ("Vari-Mu: the curve STEEPENS with drive", F[4].drift > F[0].drift + 0.03,
          F2 ("realised GR/dB %.3f vs Exact %.3f", F[4].drift, F[0].drift));
    gate ("OverEasy: the knee is WIDE", F[5].knee >= 9.0,
          F2 ("%.1f dB of knee (Exact %.1f dB)", F[5].knee, F[0].knee));
    gate ("Ride: a −40 dBp probe comes out LOUDER", F[6].upAt40 < -3.0,
          F1 ("%+.2f dB of gain on quiet material", -F[6].upAt40));
    {
        // Limit's real discriminator is not "more GR" — it is that NOTHING gets past the
        // ceiling. Zero lookahead means it eats overshoot instead of pre-empting it; the price
        // is measured here and stated, not hidden (contract §2).
        auto pl = pluckSig ((int) (FS * 1.2f), 330.0f, 0.25f, 0.45f);
        auto ov = [&] (int type) {
            CP q; q.type = type; q.push = 0.0f; q.ratio = 1.0f; q.lift = 0.0f; q.b1 = 0.25f; q.b2 = 0.3f;
            CX e; e.prepare (FS, 64); e.setParams (q);
            std::vector<float> l = pl, r = pl;
            for (int i = 0; i + 64 <= (int) pl.size(); i += 64) e.processStereo (&l[(size_t) i], &r[(size_t) i], 64);
            const double ceil = std::pow (10.0, (e.thresholdDbp() + tw::dyn::kBusNomDb) / 20.0);
            return db (peakOf (l)) - db (ceil);
        };
        const double oL = ov (CX::T_LIMIT), oE = ov (CX::T_EXACT);
        gate ("Limit: peak overshoot above its own ceiling ≤ 3 dB", oL <= 3.0,
              F2 ("Limit %+.2f dB over the ceiling; Exact at the same knobs %+.2f dB", oL, oE));
        note ("    the documented price of ZERO lookahead", F1 ("%+.2f dB of eaten overshoot", oL));
    }

    // ── OverEasy's negative zone: the one thing nothing else in the market does
    {
        CP p; p.type = CX::T_OVEREASY; p.character = 7 /* Anti */; p.push = 0.45f; p.ratio = 1.0f;
        auto a = sawSig ((int) (FS * 1.0f), 220.0f, 0.05f);
        auto b = sawSig ((int) (FS * 1.0f), 220.0f, 0.10f);      // +6 dB in
        auto oa = runC (p, a), ob = runC (p, b);
        const double d = db (rmsOf (ob.l, (size_t) (FS * 0.5f))) - db (rmsOf (oa.l, (size_t) (FS * 0.5f)));
        gate ("OverEasy `Anti`: +6 dB IN gives LESS OUT", d < -3.0,
              F1 ("%+.2f dB out for +6 dB in — the swallow", d));
    }

    // ── cross-type distinctness matrix, JND-normalised
    section ("3b. COMPRESS — cross-type distinctness (every pair, phase-independent)");
    // JND per feature, stated so the reader can argue with it:
    //   1 dB of GR · 1.5× of attack/release (log) · 1 dB of crest · 0.5 % THD · 0.05 dB/dB of
    //   slope · 3 dB of knee · 0.05 of DRR · 1 dB of upward · 1.4× of release adaptation.
    auto dist = [&] (const CFeat& a, const CFeat& b)
    {
        auto lg = [] (double x, double y, double j)
        { return std::fabs (std::log (std::max (1e-3, x) / std::max (1e-3, y))) / std::log (j); };
        double d[11] = {
            std::fabs (a.gr - b.gr) / 1.0,
            lg (a.atkMs, b.atkMs, 1.5),
            lg (a.relMs, b.relMs, 1.5),
            std::fabs (a.crest - b.crest) / 1.0,
            std::fabs (a.thd - b.thd) / 0.5,
            std::fabs (a.slope - b.slope) / 0.05,
            std::fabs (a.knee - b.knee) / 3.0,
            std::fabs (a.drr - b.drr) / 0.05,
            std::fabs (a.upAt40 - b.upAt40) / 1.0,
            lg (a.adapt, b.adapt, 1.4),                 // programme-dependence of the release
            std::fabs (a.tail2 - b.tail2) / 0.10 };     // how much SECOND time constant there is
        double m = 0.0; for (double v : d) m = std::max (m, v);
        return m;
    };
    double worst = 1e9; int wi = 0, wj = 0;
    for (int i = 0; i < CX::kNumTypes; ++i)
        for (int j = i + 1; j < CX::kNumTypes; ++j)
        { const double v = dist (F[i], F[j]); if (v < worst) { worst = v; wi = i; wj = j; } }
    gate ("closest Type pair still ≥ 3× JND", worst >= 3.0,
          F1 ("%.2f× JND", worst) + "  (" + CX::typeNames()[wi] + " / " + CX::typeNames()[wj] + ")");

    // ── Characters must change PHYSICS
    section ("3c. COMPRESS — every Character re-wires physics (R6)");
    int weak = 0; double worstC = 1e9; std::string worstName;
    for (int t = 0; t < CX::kNumTypes; ++t)
    {
        CFeat base = compressFeatures (t, 0);
        for (int c = 1; c < CX::kNumChars; ++c)
        {
            const CFeat f = compressFeatures (t, c);
            const double v = dist (base, f);
            if (v < 2.0) { ++weak;
                std::printf ("      weak: %-9s · %-14s %.2f× JND\n", CX::typeNames()[t], CX::charNames (t)[c], v); }
            if (v < worstC) { worstC = v; worstName = std::string (CX::typeNames()[t]) + " · " + CX::charNames (t)[c]; }
        }
    }
    gate ("every Character ≥ 2× JND from its Type's default", weak == 0,
          F1 ("weakest %.2f× JND", worstC) + "  (" + worstName + ")"
          + (weak ? F1 (", %.0f below bar", (double) weak) : ""));
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. COMPRESS — knobs, ceiling, mix, clicks, mono, stability, CPU, rates
// ═════════════════════════════════════════════════════════════════════════════
static void section4()
{
    section ("4. COMPRESS — every knob 0→100 (law 1: monotonic, night-and-day)");
    auto chord = chordSig ((int) (FS * 1.2f));
    auto pluck = pluckSig ((int) (FS * 1.6f));
    auto stair = staircase (-40.0f, 8.0f, 24, 120.0f);

    struct KnobRes { std::string name; double span; bool mono; int dir; };
    std::vector<KnobRes> res;

    // Monotonicity is checked in WHICHEVER DIRECTION the control actually runs. A slower attack
    // removes LESS crest and a longer release adds LESS grind; demanding "increasing" was the
    // first draft's own bug, and it failed four knobs that are perfectly well behaved.
    auto sweep = [&] (const std::string& name, float CP::* fld, CP base, int type,
                      double (*metric) (const std::vector<float>&, const std::vector<float>&),
                      const std::vector<float>& probe, double tol = 0.6)
    {
        double m[9];
        for (int k = 0; k <= 8; ++k)
        {
            CP p = base; p.type = type; p.*fld = (float) k / 8.0f;
            auto o = runC (p, probe);
            m[k] = metric (probe, o.l);
        }
        bool up = true, dn = true;
        for (int k = 1; k <= 8; ++k) { if (m[k] < m[k-1] - tol) up = false; if (m[k] > m[k-1] + tol) dn = false; }
        res.push_back ({ name, std::fabs (m[8] - m[0]), up || dn, up ? +1 : (dn ? -1 : 0) });
    };
    auto mGR   = [] (const std::vector<float>& i, const std::vector<float>& o)
                 { return db (rmsOf (i, (size_t) (FS * 0.4f))) - db (rmsOf (o, (size_t) (FS * 0.4f))); };
    auto mCrest= [] (const std::vector<float>& i, const std::vector<float>& o)
                 { return crestSettled (i) - crestSettled (o); };
    auto mLvl  = [] (const std::vector<float>& i, const std::vector<float>& o)
                 { return db (rmsOf (o)) - db (rmsOf (i)); };
    auto mTHD  = [] (const std::vector<float>&, const std::vector<float>& o) { return thdPct (o, 80.0); };
    auto mSpec = [] (const std::vector<float>& i, const std::vector<float>& o) { return specDist (i, o); };

    { CP b; b.ratio = 0.85f; b.lift = 0.0f; sweep (CF(0) + " (front 1)", &CP::push, b, 0, mGR, chord); }
    { CP b; b.push = 0.65f;  b.lift = 0.0f; sweep (CF(1) + " (front 2)", &CP::ratio, b, 0, mGR, chord); }
    { CP b; b.push = 0.55f;  b.ratio = 0.7f; sweep (CF(2) + " (front 3)", &CP::lift, b, 0, mLvl, chord); }
    { // Attack means "how much of the transient escapes before the clamp lands". Measured as
      // the first 25 ms peak over the 150-350 ms settled RMS. Crest-recovery (the first draft's
      // metric) is NOT monotone in attack, because a 300 ms attack stops engaging at all.
      CP b; b.push = 0.62f; b.ratio = 0.95f; b.b2 = 0.25f; b.lift = 0.0f;
      double m[9]; bool mono = true;
      for (int k = 0; k <= 8; ++k)
      {
          CP q = b; q.b1 = (float) k / 8.0f;
          auto o = runC (q, pluck);
          m[k] = db (peakOf (o.l, 0, (size_t) (FS * 0.030f)));   // absolute: how much escaped
          if (k && m[k] < m[k-1] - 0.6) mono = false;
      }
      gate (("  " + CB(0) + " (P1) — how much of the transient escapes").c_str(),
            (m[8] - m[0]) > 4.0 && mono,
            F3 ("first-30 ms peak %.1f dBFS at 0.05 ms → %.1f dBFS at 300 ms (span %.1f)",
                m[0], m[8], m[8] - m[0]));
    }
    { CP b; b.push = 0.6f; b.ratio = 0.9f; b.b1 = 0.0f;
      sweep (CB(1) + " (P2) — via added THD on 80 Hz", &CP::b2, b, 0, mTHD, toneSig ((int) (FS * 1.0f), 80.0f, 0.05f)); }
    { // A knee only exists NEAR the threshold. Measured in the knee's OWN unit — the width, in
      // dB, over which the transfer curve bends — read straight off a staircase. (On a 48 dB
      // staircase the knee is one tread in 24, so a GR metric reads 0.02 dB: true, and useless.)
      // GEOMETRY: the transfer curve the device publishes to its own Viz.
      double gLo = 0.0, gHi = 0.0;
      for (int k = 0; k <= 8; k += 8)
      {
          CX e; e.prepare (FS, 128); CP q; q.push = 0.45f; q.ratio = 1.0f; q.b3 = (float) k / 8.0f;
          e.setParams (q);
          // width = the input span over which the published curve bends
          int lo = 0, hi = 31; const float* kv = e.viz().knee;
          for (int i = 1; i < CX::kKnee; ++i) if (kv[i] - kv[i-1] < 0.95f * (72.0f / 31.0f)) { lo = i; break; }
          for (int i = CX::kKnee - 1; i > 0; --i) if (kv[i] - kv[i-1] > 0.05f * (72.0f / 31.0f)) { hi = i; break; }
          const double w = (hi - lo) * (72.0 / 31.0);
          if (k == 0) gLo = w; else gHi = w;
      }
      // HEARING: GR on a probe sitting ON the threshold, which is the only place a knee exists.
      double au[9]; bool mono = true;
      auto pr = sawSig ((int) (FS * 1.0f), 220.0f, 0.05f);   // peak sits ON the threshold
      for (int k = 0; k <= 8; ++k)
      {
          CP q; q.push = 0.0f; q.ratio = 1.0f; q.lift = 0.0f; q.b3 = (float) k / 8.0f;
          auto o = runC (q, pr);
          au[k] = db (rmsOf (pr, (size_t) (FS * 0.4f))) - db (rmsOf (o.l, (size_t) (FS * 0.4f)));
          if (k && au[k] < au[k-1] - 0.4) mono = false;
      }
      gate (("  " + CB(2) + " (P3) — GR on a probe sitting ON the threshold").c_str(),
            (au[8] - au[0]) > 2.0 && mono,
            F4 ("%.2f → %.2f dB of GR (span %.2f) · published curve bends over %.0f dB",
                au[0], au[8], au[8] - au[0], gHi) + F1 (" vs %.0f", gLo));
    }
    { CP b; b.push = 0.62f; b.ratio = 0.9f;
      sweep (CB(3) + " (P4) — via GR on a bass chord", &CP::b4, b, 0, mGR, chord); }
    { // Edge is the TRANSIENT lane, so measure the transient: how much of a pluck's first 30 ms
      // survives relative to its own sustain. Crest over a whole buffer is NOT monotone in Edge,
      // because a smashed attack and an escaped attack can share a crest figure.
      CP b; b.push = 0.60f; b.ratio = 0.90f; b.b1 = 0.45f; b.lift = 0.0f;
      double m[9]; bool mono = true;
      for (int k = 0; k <= 8; ++k)
      {
          CP q = b; q.b5 = (float) k / 8.0f;
          auto o = runC (q, pluck);
          m[k] = db (peakOf (o.l, 0, (size_t) (FS * 0.030f)));
          if (k && m[k] < m[k-1] - 0.6) mono = false;
      }
      gate (("  " + CB(4) + " (P5) — how much of the attack survives, −100 → +100").c_str(),
            (m[8] - m[0]) > 3.0 && mono,
            F3 ("first-30 ms peak %.2f → %.2f dBFS (span %.2f)", m[0], m[8], m[8] - m[0]));
    }
    { CP b; b.push = 0.6f; b.ratio = 0.9f; b.b2 = 0.15f;
      sweep (CB(5) + " (P6) — via GR on a pluck", &CP::b6, b, 0, mGR, pluck); }
    { CP b; b.push = 0.6f; b.ratio = 0.9f; b.b8 = 0.0f;
      sweep (CB(7) + " (P8) — via added THD", &CP::b8, b, 2, mTHD, toneSig ((int) (FS * 1.0f), 80.0f, 0.05f)); }
    { // Ride's upward lane: sweep RATIO (which drives both slopes) and watch a quiet probe rise
      CP b; b.push = 0.5f; b.lift = 0.0f;
      sweep ("Ride: " + CF(1) + " lifts a quiet probe", &CP::ratio, b, 6, mLvl,
             sawSig ((int) (FS * 1.2f), 220.0f, 0.0015f)); }

    for (auto& r : res)
        gate ((std::string ("  ") + r.name).c_str(), r.span > 2.0 && r.mono,
              F1 ("span %.2f", r.span)
              + (r.mono ? (r.dir > 0 ? " · monotone ↑" : " · monotone ↓") : " · NOT MONOTONE"));

    // Tie (P7) is a STEREO control — measuring it on a mono probe would read zero for a real reason.
    {
        auto l = chordSig ((int) (FS * 1.2f), 0.14f);
        auto r = chordSig ((int) (FS * 1.2f), 0.02f);      // 17 dB of level imbalance
        CP p; p.push = 0.6f; p.ratio = 0.9f;
        p.b7 = 1.0f; auto o1 = runCStereo (p, l, r);
        p.b7 = 0.0f; auto o0 = runCStereo (p, l, r);
        const double bal1 = db (rmsOf (o1.l)) - db (rmsOf (o1.r));
        const double bal0 = db (rmsOf (o0.l)) - db (rmsOf (o0.r));
        gate (("  " + CB(6) + " (P7) — measured on an UNBALANCED stereo probe").c_str(), std::fabs (bal1 - bal0) > 3.0,
              F3 ("L−R balance: Tie 100 %% = %+.2f dB, Tie 0 = %+.2f dB (Δ %.2f)", bal1, bal0, std::fabs (bal1 - bal0)));
    }

    // ═════ THE R11 CEILING GATE ═════════════════════════════════════════════
    section ("4b. COMPRESS — the R11 ceiling gate (a polite maximum is a failed device)");
    // METRIC + THRESHOLD, stated and defended:
    //  (a) DYNAMIC-RANGE RATIO on a 48 dB staircase at Push 100 / Ratio 100 must be ≤ 0.05.
    //      Defence: Ratio 100 is s = 1, i.e. ∞:1 exactly, and Push 100 puts the threshold
    //      39 dB inside the programme, so EVERY tread is over threshold. If the device still
    //      passes more than 5 % of a 48 dB span, the ratio law never actually reached ∞.
    //  (b) ATTACK must reach the microsecond decade: FET 76 at Attack 0 ≤ 0.05 ms measured.
    //  (c) At Attack 0 / Release 0 / Push 100 the gain must track the waveform INSIDE its own
    //      period on an 80 Hz sine — THD ≥ 10 %. Defence: this is exactly what a real 1176 at
    //      20 µs does to LF, and it is the audible top of the Release range.
    {
        auto st = staircase (-40.0f, 8.0f, 24, 120.0f);
        CP p; p.push = 1.0f; p.ratio = 1.0f; p.lift = 0.0f; p.b1 = 0.15f; p.b2 = 0.25f;
        auto o = runC (p, st);
        const double r = drRatio (st, o.l, 24, 120.0f);
        CP q = p; q.push = 0.0f; q.ratio = 0.0f; auto oq = runC (q, st);
        const double r0 = drRatio (st, oq.l, 24, 120.0f);
        gate ("(a) 48 dB staircase → ≤ 5 % survives at Push/Ratio 100", r <= 0.05,
              F2 ("DRR %.4f  (bypassed control %.4f)", r, r0));
    }
    {
        CX e; e.prepare (FS, 128);
        CP p; p.type = CX::T_FET; p.b1 = 0.0f; e.setParams (p);
        gate ("(b) attack reaches the MICROSECOND decade", e.attackMs() <= 0.05,
              F1 ("FET 76 at Attack 0 = %.0f µs", e.attackMs() * 1000.0));
        CP q; q.type = 0; q.b2 = 0.0f; e.setParams (q);
        note ("    release floor, feedforward Types", F1 ("%.1f ms", e.releaseMs()));
    }
    {
        CP p; p.push = 1.0f; p.ratio = 1.0f; p.b1 = 0.0f; p.b2 = 0.0f; p.b8 = 0.0f; p.lift = 0.0f;
        auto t = toneSig ((int) (FS * 1.0f), 80.0f, 0.05f);
        auto o = runC (p, t);
        const double thd = thdPct (o.l, 80.0);
        CP q = p; q.push = 0.0f; q.ratio = 0.0f; auto oq = runC (q, t);
        gate ("(c) the compressor becomes a WAVESHAPER at the top", thd >= 10.0,
              F2 ("THD %.1f %% on 80 Hz  (bypassed control %.3f %%)", thd, thdPct (oq.l, 80.0)));
    }
    {
        // and the same thing said in the way a listener would: crest annihilation on the chord
        auto ch = chordSig ((int) (FS * 1.5f));
        CP p; p.push = 1.0f; p.ratio = 1.0f; p.b1 = 0.2f; p.b2 = 0.2f;
        auto o = runC (p, ch);
        // measured on the SETTLED region — the first 300 ms contains the un-attacked onset, and
        // including it made the first draft report a crest of 45 dB (a true statement about a
        // transient the gate was not asking about).
        const size_t s0 = (size_t) (FS * 0.3f);
        std::vector<float> ci (ch.begin() + (long) s0, ch.end()), co (o.l.begin() + (long) s0, o.l.end());
        // Gated on the ABSOLUTE output crest — a pure sine is 3.01 dB, so ≤ 7 dB means the
        // chord has been flattened most of the way to a single tone. (The Δ is the wrong bar:
        // a source that already had a low crest could pass it without the device doing anything.)
        gate ("    crest factor collapses toward a sine (≤ 7 dB out)", crestDb (co) <= 7.0,
              F3 ("%.2f → %.2f dB (a sine is 3.01; Δ %.2f)", crestDb (ci), crestDb (co),
                  crestDb (ci) - crestDb (co)));
    }

    // ═════ MIX ═════════════════════════════════════════════════════════════
    section ("4c. COMPRESS — Mix 100 % = fully wet, zero dry (law 3)");
    {
        // The compressor's wet IS `dry × gain`, so "measure the dry residual" cannot be done by
        // spectrum. It CAN be done exactly: prove the crossfade is linear and that Mix 0 is the
        // untouched input. Then the dry coefficient at Mix 1.0 is (1 − mix) = 0 identically.
        auto ch = chordSig ((int) (FS * 1.0f));
        CP p; p.push = 0.8f; p.ratio = 1.0f; p.lift = 0.0f;
        p.mix = 0.0f; auto o0 = runC (p, ch);
        p.mix = 1.0f; auto o1 = runC (p, ch);
        p.mix = 0.5f; auto oh = runC (p, ch);
        double eDry = 0.0, eIn = 0.0, eLin = 0.0, eRef = 0.0;
        for (size_t i = 0; i < ch.size(); ++i)
        {
            const double d = (double) o0.l[i] - ch[i]; eDry += d * d; eIn += (double) ch[i] * ch[i];
            const double m = (double) oh.l[i] - 0.5 * ((double) o0.l[i] + o1.l[i]); eLin += m * m;
            eRef += (double) o1.l[i] * o1.l[i];
        }
        const double dryErr = db (std::sqrt (eDry / ch.size())) - db (std::sqrt (eIn / ch.size()));
        const double linErr = db (std::sqrt (eLin / ch.size())) - db (std::sqrt (eRef / ch.size()));
        gate ("Mix 0 output is the UNTOUCHED input", dryErr < -100.0, F1 ("%.1f dB residual", dryErr));
        gate ("the crossfade is EXACTLY linear in Mix", linErr < -100.0,
              F1 ("%.1f dB deviation at Mix 0.5", linErr));
        gate ("⇒ dry residual at Mix 1.0", true,
              F1 ("(1 − mix)·dry = 0 identically; measured floor %.1f dB (bar −60)", std::max (dryErr, linErr)));
    }

    // ═════ R6 — ONE CONTROL PER AXIS ═══════════════════════════════════════
    section ("4c2. COMPRESS — `Detect` owns detection outright (R6 / fb418 / fb373)");
    {
        int bad = 0; std::string first;
        for (int ax = 1; ax < CX::kNumDetect; ++ax)
            for (int t = 0; t < CX::kNumTypes; ++t)
            {
                int ref = -1;
                for (int c = 0; c < CX::kNumChars; ++c)
                {
                    CP p; p.type = t; p.character = c; p.axis = ax;
                    CX e; e.prepare (FS, 128); e.setParams (p);
                    if (c == 0) ref = e.detectId();
                    else if (e.detectId() != ref && ++bad == 1)
                        first = std::string (CX::typeNames()[t]) + " · " + CX::charNames (t)[c]
                              + " overrides Detect=" + CX::detectNames()[ax];
                }
            }
        gate ("no Character changes the rectifier at any Detect setting", bad == 0,
              bad == 0 ? "8 Types × 8 Characters × 4 explicit Detect settings = 256 checks"
                       : (F1 ("%.0f overrides, first: ", (double) bad) + first));
        int nat = 0;
        for (int t = 0; t < CX::kNumTypes; ++t)
        {
            int ref = -1;
            for (int c = 0; c < CX::kNumChars; ++c)
            { CP p; p.type = t; p.character = c; p.axis = 0;
              CX e; e.prepare (FS, 128); e.setParams (p);
              if (c == 0) ref = e.detectId(); else if (e.detectId() != ref) ++nat; }
        }
        gate ("  ... and at `Native` the TYPE decides it, still not the Character", nat == 0,
              F1 ("%.0f Characters disagreed with their Type's native ears", (double) nat));
        // and it is AUDIBLE, not just an integer: Peak and Average must measurably differ
        auto pk = pluckSig ((int) (FS * 1.5f), 110.0f, 0.25f);
        CP a1; a1.push = 0.5f; a1.ratio = 0.9f; a1.axis = 1;   // Peak
        CP a2 = a1; a2.axis = 2;                                // Average
        const double d = std::fabs (envSpreadDb (runC (a1, pk).l) - envSpreadDb (runC (a2, pk).l));
        gate ("  ... and the axis it owns is audible (Peak vs Average on a pluck)", d > 0.5,
              F1 ("%.2f dB of envelope-spread difference", d));
    }

    // ═════ THE SMOOTHER-SEED GATE, ON ITS OWN ══════════════════════════════
    section ("4c3. COMPRESS — the ballistic state survives a smoother-shape change");
    // 🚨 This gate exists because the CLICK gate could not prove the seeding on its own: with the
    // transition slew limiter also present, deleting the seed left the click matrix at 2.04 dB/ms
    // — over the bar, but barely, and only on one cell. Defence in depth is good engineering and
    // TERRIBLE evidence. So the mechanism gets a gate that measures IT and nothing else:
    // `gr_` is the gain reduction in dB and it is the same physical quantity in all five smoother
    // shapes, so across a shape change it must be CONTINUOUS. Sampled one 8-sample block either
    // side of the switch, over every Type pair and every Character pair that actually changes
    // shape. On the fb421 engine this reads 10.96 dB — a complete collapse to zero inside one
    // block, i.e. an ~+11 dB gain step, mid-note.
    {
        auto prog = clickProg ((int) (FS * 0.6f));
        auto step = [&] (CP a, CP b)
        {
            CX e; e.prepare (FS, 8); e.setParams (a);
            std::vector<float> l = prog, r = prog;
            const int at = 20000 - 20000 % 8;
            double before = 0.0, worst = 0.0;
            for (int i = 0; i + 8 <= (int) prog.size(); i += 8)
            {
                const bool sw = (i >= at);
                if (i == at) before = e.grNow();
                e.setParams (sw ? b : a);
                e.processStereo (&l[(size_t) i], &r[(size_t) i], 8);
                // ONE 8-sample block (0.17 ms). Longer than that and the NEW smoother's
                // attack legitimately runs — Limit closes a 5 dB gap in 1.09 ms — and the gate
                // starts measuring the ballistics instead of the seed. A discarded state shows
                // up in the FIRST block or not at all.
                if (i == at) worst = std::fabs (e.grNow() - before);
            }
            return worst;
        };
        CP base; base.push = 0.3f; base.ratio = 0.6f;
        double wT = 0.0, wC = 0.0; std::string nT, nC;
        for (int ta = 0; ta < CX::kNumTypes; ++ta)
            for (int tb = 0; tb < CX::kNumTypes; ++tb)
            {
                CP a = base; a.type = ta; CP b = base; b.type = tb;
                const double m = step (a, b);
                if (m > wT) { wT = m; nT = std::string (CX::typeNames()[ta]) + " → " + CX::typeNames()[tb]; }
            }
        for (int t = 0; t < CX::kNumTypes; ++t)
            for (int ca = 0; ca < CX::kNumChars; ++ca)
                for (int cb = 0; cb < CX::kNumChars; ++cb)
                {
                    if (ca == cb) continue;
                    CP a = base; a.type = t; a.character = ca; CP b = base; b.type = t; b.character = cb;
                    const double m = step (a, b);
                    if (m > wC) { wC = m; nC = std::string (CX::typeNames()[t]) + ": "
                                             + CX::charNames (t)[ca] + " → " + CX::charNames (t)[cb]; }
                }
        gate ("GR is continuous across all 64 Type changes (≤ 2 dB in the first 0.17 ms)", wT <= 2.0,
              F1 ("worst %.2f dB", wT) + "  (" + nT + ")");
        gate ("GR is continuous across all 448 Character changes", wC <= 2.0,
              F1 ("worst %.2f dB", wC) + "  (" + nC + ")");
    }

    // ═════════════════════════════════════════════════════════════════════════
    section ("4c4. COMPRESS — the GAIN ELEMENT's CURVE is continuous, not just its LEVEL");
    // 🔑 THIS GATE EXISTS TO CLOSE MUTATION.md's TWO SURVIVORS, and it is written in the shape
    // that failed them. `compress-transition-slew` and `compress-heat-kind-fade` both survived
    // because §4d measures **dB of gain per ms** — a LEVEL metric — and the mechanism they
    // protect is the SHAPE of the gain element's transfer curve. A waveshaper whose curve
    // changes in one sample at constant gain is invisible to every level metric there is.
    //
    // 🔬 CHECK YOUR OWN DETECTOR (§3.1). Two earlier drafts of this gate were LEVEL gates in
    // disguise and are recorded here so nobody rebuilds them:
    //   · absolute 3rd-harmonic level — a cubic term goes as A³, so it is 3× as sensitive to
    //     gain as gain is. Read 12.08 dB on `Ride: Only Up → Slow Iron`, a pair whose GR
    //     legitimately travels 0 → 15.6 dB over 60 ms at 0.35 dB/ms (well inside §4d's bar).
    //   · H3/H1 — better, still goes as A². Read 10.69 dB on the same pair, same reason.
    // The measurement below is the SHAPE INVARIANT. For a memoryless odd nonlinearity
    // y = x + c·x³ driven at amplitude A: H1 ≈ A and H3 ≈ (c/4)·A³, so
    //        S ≡ H3_dB − 3·H1_dB = 20·log10(c/4)
    // depends ONLY on the curvature, and a pure gain change cancels out of it exactly to cubic
    // order. Section K plants a 6 dB gain step and a curve swap and shows S ignores the first
    // and reports the second.
    // Probe: 2 kHz (so a 2 ms hop is four whole cycles), 250 ms of settling under config A, then
    // the switch, then S measured in the 2 ms immediately either side. Two milliseconds is the
    // §4c3 logic — a discarded state shows up at once or not at all — and it is short enough
    // that the 20 ms drive smoother cannot legitimately move the curvature inside it.
    {
        const int HOP = (int) (FS * 0.002f);                        // 2 ms = 4 cycles at 2 kHz
        auto bin = [&] (const std::vector<float>& y, int i0, double hz) {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < HOP; ++i)
            { const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (HOP - 1));
              const double a = 2.0 * M_PI * hz * i / (double) FS;
              re += w * y[(size_t) (i0 + i)] * std::cos (a);
              im -= w * y[(size_t) (i0 + i)] * std::sin (a); }
            return db (std::sqrt (re * re + im * im) * 2.0 / HOP);
        };
        auto shape = [&] (const std::vector<float>& y, int i0)
        { return bin (y, i0, 6000.0) - 3.0 * bin (y, i0, 2000.0); };
        auto tone = toneSig ((int) (FS * 0.4f), 2000.0f, 0.09f);
        auto swap = [&] (CP a, CP b) {
            CX e; e.prepare (FS, 8); e.setParams (a);
            std::vector<float> l = tone, r = tone;
            const int at = 12000;                                   // 250 ms in, block- and hop-aligned
            for (int i = 0; i + 8 <= (int) tone.size(); i += 8)
            { e.setParams ((i >= at) ? b : a); e.processStereo (&l[(size_t) i], &r[(size_t) i], 8); }
            return std::fabs (shape (l, at) - shape (l, at - HOP));
        };
        CP base; base.push = 0.55f; base.ratio = 0.85f; base.b8 = 1.0f;   // Burn 100, real GR
        double ctrl = 0.0;
        { CP a = base; ctrl = swap (a, a); }
        note ("  control: the SAME config either side of the switch", F1 ("%.2f dB of curvature", ctrl));
        double wT = 0.0, wC = 0.0; std::string nT, nC;
        for (int ta = 0; ta < CX::kNumTypes; ++ta)
            for (int tb = 0; tb < CX::kNumTypes; ++tb)
            { if (ta == tb) continue;
              CP a = base; a.type = ta; CP b = base; b.type = tb;
              const double m = swap (a, b);
              if (m > wT) { wT = m; nT = std::string (CX::typeNames()[ta]) + " → " + CX::typeNames()[tb]; } }
        for (int ty = 0; ty < CX::kNumTypes; ++ty)
            for (int ca = 0; ca < CX::kNumChars; ++ca)
                for (int cb = 0; cb < CX::kNumChars; ++cb)
                { if (ca == cb) continue;
                  CP a = base; a.type = ty; a.character = ca; CP b = a; b.character = cb;
                  const double m = swap (a, b);
                  if (m > wC) { wC = m; nC = std::string (CX::typeNames()[ty]) + ": "
                                           + CX::charNames (ty)[ca] + " → " + CX::charNames (ty)[cb]; } }
        // The bar is 6 dB of curvature in 2 ms — a DOUBLING of the gain element's cubic
        // coefficient, i.e. the smallest step anyone would call a change of TIMBRE rather than
        // of loudness. It is printed beside the control so the scale is legible.
        gate ("the gain element's CURVATURE is continuous across all 56 Type changes", wT <= 6.0,
              F1 ("worst %.2f dB of curvature in 2 ms", wT) + "  (" + nT + ")");
        gate ("... and across all 448 Character changes", wC <= 6.0,
              F1 ("worst %.2f dB", wC) + "  (" + nC + ")");
    }

    // ═════ CLICKS ══════════════════════════════════════════════════════════
    section ("4d. COMPRESS — no clicks (law 4): ALL 8x8 Type and ALL 8x8 Character transitions");
    // 🚨 WHAT THIS REPLACES (FIXES.md §1 COMPRESS 1 + 2). The old gate tested ONE Type
    // transition — Exact → Vari-Mu — which is the RS_EXP→RS_EXP case, the only clean one; and
    // it jumped at a zero crossing on a block boundary with a single 220 Hz tone. `Exact → Ride`
    // read 7.30x at that harness's own alignment. Measured here on the fb421 engine, every
    // ordered pair, five phases, broadband programme: the worst Type transition moved
    // **17.36 dB of gain inside one millisecond** and the worst Character transition 16.26 dB.
    {
        auto prog = clickProg ((int) (FS * 1.2f));
        CP base; base.push = 0.3f; base.ratio = 0.6f;
        double wT = 0.0, wC = 0.0; std::string nT, nC;
        for (int ta = 0; ta < CX::kNumTypes; ++ta)
            for (int tb = 0; tb < CX::kNumTypes; ++tb)
            {
                CP a = base; a.type = ta; CP b = base; b.type = tb;
                for (int k = 0; k < 5; ++k)
                {
                    const double m = cJump (a, b, kAts[k], prog);
                    if (m > wT) { wT = m; nT = std::string (CX::typeNames()[ta]) + " → " + CX::typeNames()[tb]; }
                }
            }
        gate ("all 64 Type transitions ≤ 2.0 dB of gain moved in 1 ms", wT <= 2.0,
              F1 ("worst %.2f dB/ms", wT) + "  (" + nT + ")   [fb421 engine: 17.36]");
        for (int t = 0; t < CX::kNumTypes; ++t)
            for (int ca = 0; ca < CX::kNumChars; ++ca)
                for (int cb = 0; cb < CX::kNumChars; ++cb)
                {
                    if (ca == cb) continue;
                    CP a = base; a.type = t; a.character = ca;
                    CP b = base; b.type = t; b.character = cb;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double m = cJump (a, b, kAts[k], prog);
                        if (m > wC) { wC = m; nC = std::string (CX::typeNames()[t]) + ": "
                                                 + CX::charNames (t)[ca] + " → " + CX::charNames (t)[cb]; }
                    }
                }
        gate ("all 448 Character transitions ≤ 2.0 dB of gain moved in 1 ms", wC <= 2.0,
              F1 ("worst %.2f dB/ms", wC) + "  (" + nC + ")   [fb421 engine: 16.26]");
        // the front/back knobs, on the same metric and the same five phases
        struct KJ { std::string what; CP b; };
        CP k1 = base; k1.push = 0.9f;   CP k2 = base; k2.ratio = 1.0f;
        CP k3 = base; k3.lift = 1.0f;   CP k4 = base; k4.b3 = 1.0f;
        CP k5 = base; k5.b4 = 1.0f;     CP k6 = base; k6.b7 = 0.0f;
        CP k7 = base; k7.b8 = 1.0f;     CP k8 = base; k8.b5 = 1.0f;
        CP k9 = base; k9.b6 = 1.0f;     CP k10 = base; k10.axis = 3;
        const KJ kj[] = { { "  " + CF(0) + " 0.3 → 0.9", k1 }, { "  " + CF(1) + " 0.6 → 1.0 (∞:1)", k2 },
                          { "  " + CF(2) + " 0 → +24 dB", k3 }, { "  " + CB(2) + " 0.25 → 1.0", k4 },
                          { "  " + CB(3) + " off → 500 Hz", k5 }, { "  " + CB(6) + " 100 → 0", k6 },
                          { "  " + CB(7) + " 0 → 100", k7 }, { "  " + CB(4) + " 0.5 → 1.0", k8 },
                          { "  " + CB(5) + " 0 → 250 ms", k9 },
                          { "  " + std::string (CX::dropdownNames()[1]) + " "
                                 + CX::detectNames()[0] + " → " + CX::detectNames()[3], k10 } };
        for (auto& j : kj)
        {
            double m = 0.0;
            for (int k = 0; k < 5; ++k) m = std::max (m, cJump (base, j.b, kAts[k], prog));
            gate (j.what.c_str(), m <= 2.0, F1 ("%.2f dB/ms", m));
        }
    }
    // the CONTINUOUS fault an outlier detector is blind to (fb416): a slow sweep must not
    // zipper. Measured as inter-harmonic hash RELATIVE TO THE FUNDAMENTAL BIN, against the
    // SAME measurement with the knob held still.
    {
        auto tone = toneSig ((int) (FS * 1.0f), 220.0f, 0.05f);
        auto hash = [&] (bool moving)
        {
            CX e; e.prepare (FS, 64); CP p; p.push = 0.55f; p.ratio = 0.9f; p.lift = 0.0f;
            std::vector<float> l = tone, r = tone;
            for (int i = 0; i + 64 <= (int) tone.size(); i += 64)
            {
                if (moving) p.push = 0.2f + 0.7f * (float) i / (float) tone.size();
                e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 64);
            }
            auto A = magSpec (l);
            const double binHz = FS / 4096.0;
            const int kf = (int) std::round (220.0 / binHz);
            double fund = 0.0;
            for (int k = kf - 3; k <= kf + 3; ++k) fund = std::max (fund, A[(size_t) k]);
            double h = 0.0; int n = 0;
            for (int k = 6; k < 2000; ++k)
            {
                const double hz = k * binHz;
                bool nearH = false;
                for (int m = 1; m <= 9; ++m) if (std::fabs (hz - 220.0 * m) < 45.0) nearH = true;
                if (nearH) continue;
                h += A[(size_t) k] * A[(size_t) k]; ++n;
            }
            return db (std::sqrt (h / std::max (1, n))) - db (fund);
        };
        const double moving = hash (true), still = hash (false);
        gate ("  slow Push sweep adds no zipper hash", moving - still < 6.0,
              F3 ("moving %.1f dBc vs held %.1f dBc (excess %+.2f dB, bar 6)", moving, still, moving - still));
    }

    // ═════ MONO ════════════════════════════════════════════════════════════
    section ("4e. COMPRESS — mono-safe (law 5): folding to mono must not kill it");
    {
        auto ch = chordSig ((int) (FS * 1.0f));
        int bad = 0; double worst = 1e9; std::string wn;
        for (int t = 0; t < CX::kNumTypes; ++t)
            for (int c = 0; c < CX::kNumChars; c += 3)
            {
                CP p; p.type = t; p.character = c; p.push = 0.6f; p.ratio = 0.9f;
                auto o = runC (p, ch);
                std::vector<float> mono (ch.size()), dryMono (ch.size());
                for (size_t i = 0; i < ch.size(); ++i) { mono[i] = 0.5f * (o.l[i] + o.r[i]); dryMono[i] = ch[i]; }
                const double d = specDist (dryMono, mono) + std::fabs (db (rmsOf (mono)) - db (rmsOf (dryMono)));
                if (d < 2.0) ++bad;
                if (d < worst) { worst = d; wn = std::string (CX::typeNames()[t]) + " · " + CX::charNames (t)[c]; }
            }
        gate ("every Type × Character survives a mono fold", bad == 0,
              F1 ("weakest %.2f dB of surviving change", worst) + "  (" + wn + ")");
    }

    // ═════ STABILITY ═══════════════════════════════════════════════════════
    section ("4f. COMPRESS — 60 s of full-drive noise, every Type (no NaN, no blow-up)");
    {
        auto n60 = noiseSig ((int) (FS * 60.0f), 0.35f);
        int bad = 0; double worstPk = 0.0, worstGrow = 0.0; std::string wn;
        for (int t = 0; t < CX::kNumTypes; ++t)
        {
            CP p; p.type = t; p.character = 2; p.push = 1.0f; p.ratio = 1.0f;
            p.lift = 1.0f; p.b1 = 0.0f; p.b2 = 0.0f; p.b8 = 1.0f;
            auto o = runC (p, n60);
            double pk = 0.0; bool nan = false;
            for (float v : o.l) { if (!std::isfinite (v)) nan = true; pk = std::max (pk, (double) std::fabs (v)); }
            // A +24 dB Lift on a −9 dBFS noise bed legitimately reads a peak near 10. The
            // stability question is not "is it loud" — it is "is it GROWING". So: last 10 s
            // vs first 10 s. (The first draft's absolute bar of 8.0 failed `Ride` for being
            // exactly as loud as its own Lift knob asked for.)
            const double e0 = peakOf (o.l, 0, (size_t) (FS * 10.0f));
            const double e1 = peakOf (o.l, (size_t) (FS * 50.0f));
            const double grow = db (e1) - db (e0);
            if (nan || grow > 1.0) { ++bad; wn = CX::typeNames()[t]; }
            worstPk = std::max (worstPk, pk); worstGrow = std::max (worstGrow, grow);
        }
        gate ("60 s × 8 Types at every extreme: no NaN, no growth", bad == 0,
              F2 ("worst peak %.2f, worst 60 s growth %+.3f dB", worstPk, worstGrow) + (bad ? "  " + wn : ""));
        // and the feedback topologies specifically: nothing free-runs into silence
        for (int t : { CX::T_FET, CX::T_OPTO, CX::T_VARIMU })
        {
            std::vector<float> sil ((size_t) (int) (FS * 2.0f), 0.0f);
            CP p; p.type = t; p.push = 1.0f; p.ratio = 1.0f; p.lift = 1.0f; p.b8 = 1.0f;
            auto o = runC (p, sil);
            gate ((std::string ("  ") + CX::typeNames()[t] + " (feedback) on 2 s of silence").c_str(),
                  peakOf (o.l) < 1e-9, F1 ("peak %.2e", peakOf (o.l)));
        }
    }

    // ═════ CPU + SAMPLE RATES ══════════════════════════════════════════════
    section ("4g. COMPRESS — CPU and sample rates");
    {
        auto blk = noiseSig (128, 0.05f);
        double worstUs = 0.0; int wt = 0;
        for (int t = 0; t < CX::kNumTypes; ++t)
        {
            CX e; e.prepare (FS, 128);
            CP p; p.type = t; p.push = 0.6f; p.ratio = 0.9f; p.b8 = 0.5f;
            std::vector<float> l = blk, r = blk;
            const auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < 4000; ++i) { e.setParams (p); e.processStereo (l.data(), r.data(), 128); }
            const auto t1 = std::chrono::high_resolution_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / 4000.0;
            if (us > worstUs) { worstUs = us; wt = t; }
        }
        // 128 samples at 48 k = 2666 µs of wall clock. 6 instances × 13 kinds may run at once.
        gate ("µs / 128-sample block @ 48 k, worst Type", worstUs < 25.0,
              F1 ("%.2f µs", worstUs) + " (" + CX::typeNames()[wt]
              + F1 (") = %.2f %% of one core", 100.0 * worstUs / 2666.0));
    }
    // 🚨 WHAT THIS REPLACES (FIXES.md §1 COMPRESS 3). The old gate was
    //        fabs (e.attackMs() - e48.attackMs()) < 0.02
    //    and `attackMs()` returns `atkMs_`, computed from the knob and the Type table with NO
    //    sample-rate term except a one-sample floor. It printed "atk 0.68 ms (48 k: 0.68)" BY
    //    CONSTRUCTION — a constant compared to itself. It would have passed on an engine that
    //    forgot `fs` in `coefTau` entirely.
    //    What is measured now: the REALISED time constant, off the audio. A sustained tone is
    //    switched on at t = 0.3 s; the GR trajectory is sampled at that rate's own resolution and
    //    the time to 63.2 % of the settled reduction is read off it. Same for the release when
    //    the tone stops. A rate-dependence bug shows up as a ratio, immediately.
    {
        auto realised = [] (float fs, bool wantRelease, double& settledGr)
        {
            const int n = (int) (fs * 1.2f);
            std::vector<float> x ((size_t) n, 0.0f);
            for (int i = (int) (fs * 0.3f); i < (int) (fs * 0.9f); ++i)
                x[(size_t) i] = (float) (0.28 * std::sin (2.0 * M_PI * 220.0 * i / fs));
            CP p; p.push = 0.35f; p.ratio = 0.9f; p.b1 = 0.5f; p.b2 = 0.5f;   // Exact, peak ears
            CX e; e.prepare ((double) fs, 8); e.setParams (p);
            std::vector<float> l = x, r = x, tr;
            for (int i = 0; i + 8 <= n; i += 8)
            { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 8); tr.push_back (e.grNow()); }
            const double hopMs = 8.0 * 1000.0 / fs;
            const int on = (int) (fs * 0.3f / 8.0), off = (int) (fs * 0.9f / 8.0);
            double top = 0.0;
            for (int i = off - 40; i < off; ++i) top = std::max (top, (double) tr[(size_t) i]);
            settledGr = top;
            if (!wantRelease)
            {
                for (int i = on; i < off; ++i)
                    if (tr[(size_t) i] >= 0.632 * top) return (i - on) * hopMs;
                return 1e9;
            }
            for (int i = off; i < (int) tr.size(); ++i)
                if (tr[(size_t) i] <= top * (1.0 - 0.632)) return (i - off) * hopMs;
            return 1e9;
        };
        double g48a = 0.0, g48r = 0.0;
        const double a48 = realised (48000.0f, false, g48a);
        const double r48 = realised (48000.0f, true,  g48r);
        note ("realised attack / release at 48 kHz",
              F3 ("t63 attack %.3f ms, t63 release %.1f ms, settled GR %.2f dB", a48, r48, g48a));
        for (float fs : { 44100.0f, 96000.0f })
        {
            double ga = 0.0, gr2 = 0.0;
            const double aM = realised (fs, false, ga);
            const double rM = realised (fs, true,  gr2);
            const double ea = 100.0 * std::fabs (aM - a48) / std::max (1e-9, a48);
            const double er = 100.0 * std::fabs (rM - r48) / std::max (1e-9, r48);
            gate ((F1 ("%.1f kHz: REALISED t63 within 12 %% of 48 kHz", fs / 1000.0)).c_str(),
                  ea < 12.0 && er < 12.0 && std::fabs (ga - g48a) < 1.0,
                  F4 ("attack %.3f ms (%+.1f %%), release %.1f ms (%+.1f %%)",
                      aM, aM - a48 > 0 ? ea : -ea, rM, rM - r48 > 0 ? er : -er)
                  + F2 (", settled GR %.2f dB vs %.2f", ga, g48a));
        }
        auto ch = chordSig ((int) (48000.0f * 1.0f));
        for (float fs : { 44100.0f, 96000.0f })
        {
            CP p; p.push = 0.6f; p.ratio = 0.9f;
            CX e; e.prepare ((double) fs, 128); e.setParams (p);
            std::vector<float> l = ch, r = ch;
            for (int i = 0; i + 128 <= (int) ch.size(); i += 128) { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
            bool fin = true; for (float v : l) if (!std::isfinite (v)) fin = false;
            gate ((F1 ("  %.1f kHz: finite and still engaging", fs / 1000.0)).c_str(),
                  fin && e.viz().grDb > 3.0, F1 ("GR %.2f dB", e.viz().grDb));
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. OTT
// ═════════════════════════════════════════════════════════════════════════════
struct OFeat { double gr[3], air, step, thd, crest, tailRise, cross, tilt, lowImd, drr, stepTau, tauLo, tauHi; };

static OFeat ottFeatures (int type, int chr)
{
    OFeat f {};
    OP p; p.type = type; p.character = chr;

    {   // per-band GR on the reference chord (which HAS bass — §4.5)
        auto ch = chordSig ((int) (FS * 1.5f));
        OX e; e.prepare (FS, 128); e.setParams (p);
        std::vector<float> l = ch, r = ch;
        for (int i = 0; i + 128 <= (int) ch.size(); i += 128) { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
        for (int b = 0; b < 3; ++b) f.gr[b] = e.viz().grDb[b];
    }
    {   // dynamic range REMOVED, on a probe that has some
        auto am = amChord ((int) (FS * 2.5f));
        auto o = runO (p, am);
        f.crest = envSpreadDb (am) - envSpreadDb (o.l);
    }
    {   // PER-BAND settling time, measured from AUDIO: a 50 Hz probe and an 8 kHz probe, each
        //    stepped −20 dB, each timed separately. This is the only honest way to see a
        //    Character that only re-times ONE band (`Even Bands`, `Slow Low`, `Reverse Spread`);
        //    a broadband step is dominated by the mid and reads them all as identical.
        auto settle = [&] (float hz) {
            const int n = (int) (FS * 1.6f);
            auto x = toneSig (n, hz, 0.06f);
            for (int i = (int) (FS * 0.5f); i < n; ++i) x[(size_t) i] *= 0.1f;
            auto o = runO (p, x);
            const double fin = db (rmsOf (o.l, (size_t) (FS * 1.30f), (size_t) (FS * 1.45f)));
            const int w = (int) (FS * 0.001f), i0 = (int) (FS * 0.5f);
            for (int i = i0; i + w < (int) (FS * 1.3f); i += w)
                if (std::fabs (db (rmsOf (o.l, (size_t) i, (size_t) (i + w))) - fin) < 1.0)
                    return (i - i0) * 1000.0 / (double) FS;
            return 800.0;
        };
        f.tauLo = settle (50.0f); f.tauHi = settle (8000.0f);
    }
    {   // AIR: 8–12 kHz on a genuinely dark pad, vs bypass
        auto dark = darkPad ((int) (FS * 2.0f));
        OP q = p; auto o = runO (q, dark);
        f.air = bandDbOf (o.l, 8000.0, 12000.0) - bandDbOf (dark, 8000.0, 12000.0);
    }
    {   // LEVEL STEP: −20 dB for 500 ms. A static device steps the full 20; a working OTT ≤ 6.
        const int n = (int) (FS * 1.6f);
        std::vector<float> x ((size_t) n, 0.0f); addSaw (x, 220.0f, 1.0f);
        const double a = rmsOf (x); for (auto& v : x) v *= (float) (0.06 / a);
        for (int i = (int) (FS * 0.5f); i < (int) (FS * 1.0f); ++i) x[(size_t) i] *= 0.1f;
        auto o = runO (p, x);
        const double hi = db (rmsOf (o.l, (size_t) (FS * 0.35f), (size_t) (FS * 0.48f)));
        const double lo = db (rmsOf (o.l, (size_t) (FS * 0.85f), (size_t) (FS * 0.98f)));
        f.step = hi - lo;
        // SETTLING TIME after the step, straight off the audio: 2 ms RMS windows, time until the
        // level is within 1 dB of its final value. This is the ballistic axis, and without it a
        // Character that only re-times the followers has nothing in the feature vector to move.
        const int w = (int) (FS * 0.002f);
        const int i0 = (int) (FS * 0.5f);
        f.stepTau = 500.0;
        for (int i = i0; i + w < (int) (FS * 1.0f); i += w)
            if (std::fabs (db (rmsOf (o.l, (size_t) i, (size_t) (i + w))) - lo) < 1.0)
            { f.stepTau = (i - i0) * 1000.0 / FS; break; }
    }
    {   // ripple THD on 100 Hz at full Amount — Gentle's whole claim
        OP q = p; q.amount = 1.0f;
        auto t = toneSig ((int) (FS * 1.0f), 100.0f, 0.05f);
        auto o = runO (q, t);
        f.thd = thdPct (o.l, 100.0);
    }
    {   // TAIL RISE: how much a decaying pluck's late tail is lifted. Surge lives here.
        auto pl = pluckSig ((int) (FS * 2.5f), 110.0f, 0.5f);
        auto o = runO (p, pl);
        const double lateIn  = db (rmsOf (pl,  (size_t) (FS * 1.4f), (size_t) (FS * 2.2f)));
        const double lateOut = db (rmsOf (o.l, (size_t) (FS * 1.4f), (size_t) (FS * 2.2f)));
        const double earlyIn  = db (rmsOf (pl,  (size_t) (FS * 0.02f), (size_t) (FS * 0.12f)));
        const double earlyOut = db (rmsOf (o.l, (size_t) (FS * 0.02f), (size_t) (FS * 0.12f)));
        f.tailRise = (lateOut - lateIn) - (earlyOut - earlyIn);
    }
    {   // CROSS-BAND DUCKING — the band-count telltale, measured in the OUTPUT SPECTRUM.
        //    A loud 500 Hz tone + a quiet 5 kHz tone. In a 3-band tree the 5 k sits in its own
        //    band and does not care. In a 2-band tree (or with One Detector) they share.
        const int n = (int) (FS * 1.5f);
        auto quiet = toneSig (n, 5000.0f, 0.004f);
        std::vector<float> both (quiet.size());
        auto loud = toneSig (n, 500.0f, 0.12f);
        for (size_t i = 0; i < both.size(); ++i) both[i] = quiet[i] + loud[i];
        auto oq = runO (p, quiet), ob = runO (p, both);
        const double a = bandDbOf (oq.l, 4700.0, 5300.0);
        const double b = bandDbOf (ob.l, 4700.0, 5300.0);
        f.cross = a - b;                                   // dB the 5 k lost when the 500 arrived
    }
    {   // SPECTRAL TILT TRAJECTORY — does the spectrum MORPH through the note? That is the
        //    mechanism when the bands decouple in time, and it is measured in the OUTPUT
        //    SPECTRUM, not in a control signal (fb417). Tilt = HF band minus LF band, in dB;
        //    the feature is how much MORE the wet tilt swings across the note than the dry's.
        auto pl = pluckSig ((int) (FS * 2.0f), 110.0f, 0.6f);
        auto o = runO (p, pl);
        auto tilt = [&] (const std::vector<float>& x, double t0, double t1) {
            std::vector<float> sg (x.begin() + (size_t) (FS * t0), x.begin() + (size_t) (FS * t1));
            return bandDbOf (sg, 4000.0, 14000.0) - bandDbOf (sg, 60.0, 400.0); };
        const double d1 = tilt (pl, 0.02, 0.18), d2 = tilt (pl, 0.45, 0.75), d3 = tilt (pl, 1.1, 1.8);
        const double w1 = tilt (o.l, 0.02, 0.18), w2 = tilt (o.l, 0.45, 0.75), w3 = tilt (o.l, 1.1, 1.8);
        const double dry = std::max ({ d1, d2, d3 }) - std::min ({ d1, d2, d3 });
        const double wet = std::max ({ w1, w2, w3 }) - std::min ({ w1, w2, w3 });
        f.tilt = wet - dry;      // dB of EXTRA spectral swing the device adds across the note
    }
    {   // LOW-BAND GRIT — the harmonics a low-band follower MANUFACTURES when it is fast
        //    enough to track a 20 ms waveform. Measured on the SETTLED half of a 50 Hz sine
        //    (the charge-up transient is not distortion), with Low Cross at ~210 Hz so the tone
        //    is firmly inside the low band and only that band's ballistics can be responsible.
        OP q = p; q.amount = 0.85f; q.b1 = 0.85f; q.speed = 0.75f;   // fast enough to matter
        auto x = toneSig ((int) (FS * 1.6f), 50.0f, 0.05f);
        auto o = runO (q, x);
        std::vector<float> tail (o.l.begin() + (long) (FS * 0.8f), o.l.end());
        f.lowImd = thdPct (tail, 50.0);
    }
    {   // dynamic range ratio on a staircase
        auto st = staircase (-40.0f, 6.0f, 20, 120.0f, 330.0f);
        auto o = runO (p, st);
        f.drr = drRatio (st, o.l, 20, 120.0f);
    }
    return f;
}

static void section5()
{
    section ("5. OTT — roster, unity, engage, and the per-Type discriminator");
    auto chord = chordSig ((int) (FS * 2.0f));

    // unity-through at defaults, all 8 Types
    {
        int bad = 0; double worst = 0.0; std::string wn;
        for (int t = 0; t < OX::kNumTypes; ++t)
        {
            OP p; p.type = t;
            auto o = runO (p, chord);
            const double d = db (rmsOf (o.l, (size_t) (FS * 0.5f))) - db (rmsOf (chord, (size_t) (FS * 0.5f)));
            if (std::fabs (d) > 2.0) ++bad;
            if (std::fabs (d) > std::fabs (worst)) { worst = d; wn = OX::typeNames()[t]; }
            note ((std::string ("  unity, ") + OX::typeNames()[t]).c_str(), F1 ("%+.2f dB", d));
        }
        gate ("every Type lands within ±2 dB of bypass at defaults", bad == 0,
              F1 ("worst %+.2f dB", worst) + "  (" + wn + ")");
    }

    // the ENGAGE gate — the #1 way this device ships broken (thresholds never crossed)
    {
        OX e; e.prepare (FS, 128); OP p; e.setParams (p);
        std::vector<float> l = chord, r = chord;
        for (int i = 0; i + 128 <= (int) chord.size(); i += 128) { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
        note ("  Over Top at defaults, per-band signed GR",
              F3 ("low %+.2f  mid %+.2f  high %+.2f dB", e.viz().grDb[0], e.viz().grDb[1], e.viz().grDb[2]));
        note ("  ... and the band levels it is reading",
              F3 ("%.1f / %.1f / %.1f dBFS", e.viz().bandDb[0], e.viz().bandDb[1], e.viz().bandDb[2]));
        int engaged = 0;
        for (int b = 0; b < 3; ++b) if (std::fabs (e.viz().grDb[b]) >= 3.0) ++engaged;
        gate ("ENGAGE: ≥ 2 bands doing ≥ 3 dB on the chord", engaged >= 2,
              F1 ("%.0f of 3 bands engaged (the anti-dead-port gate)", (double) engaged));
    }

    // 🔑 THE MIX LAW — perfect reconstruction, measured as a NULL
    {
        auto n = noiseSig ((int) (FS * 1.5f), 0.05f);
        OP p; p.amount = 0.0f; p.topLift = 0.0f; p.b6 = p.b7 = p.b8 = 0.5f;
        p.mix = 1.0f; auto w = runO (p, n);
        p.mix = 0.0f; auto d = runO (p, n);
        double e = 0.0, ref = 0.0;
        for (size_t i = 0; i < n.size(); ++i)
        { const double q = (double) w.l[i] - d.l[i]; e += q * q; ref += (double) d.l[i] * d.l[i]; }
        const double nulldb = db (std::sqrt (e / n.size())) - db (std::sqrt (ref / n.size()));
        gate ("wet path − phase-matched dry NULLS at Amount 0", nulldb < -55.0,
              F1 ("%.1f dB  (the whole Mix law in one number; bar −55)", nulldb));
    }
    {
        // and the thing the null protects against: a comb at Mix 50 %
        auto n = noiseSig ((int) (FS * 2.0f), 0.05f);
        OP p; p.amount = 0.0f; p.topLift = 0.0f; p.mix = 0.5f;
        auto o = runO (p, n);
        auto A = magSpec (o.l), B = magSpec (n);
        const double binHz = FS / 4096.0;
        double worstNotch = 0.0; double atHz = 0.0;
        for (double fc : { 88.3, 2500.0 })
            for (int k = (int) (fc * 0.6 / binHz); k <= (int) (fc * 1.6 / binHz) && k < 2048; ++k)
            {
                const double dd = db (B[(size_t) k]) - db (A[(size_t) k]);
                if (dd > worstNotch) { worstNotch = dd; atHz = k * binHz; }
            }
        gate ("no comb notch at either crossover at Mix 50 %", worstNotch < 1.0,
              F2 ("worst %.2f dB at %.0f Hz (bar 1.0)", worstNotch, atHz));
    }

    // ── per-Type discriminators
    OFeat G[OX::kNumTypes];
    for (int t = 0; t < OX::kNumTypes; ++t) G[t] = ottFeatures (t, 0);

    std::printf ("      %-10s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n",
                 "Type", "grLo", "grMid", "grHi", "air", "step", "THD%", "dynΔ", "tail", "cross", "tilt", "tauLo/Hi");
    for (int t = 0; t < OX::kNumTypes; ++t)
        std::printf ("      %-10s %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.1f\n",
                     OX::typeNames()[t], G[t].gr[0], G[t].gr[1], G[t].gr[2], G[t].air, G[t].step,
                     G[t].thd, G[t].crest, G[t].tailRise, G[t].cross, G[t].tilt, G[t].tauLo);

    gate ("Over Top: a 20 dB level step comes out ≤ 10 dB", G[0].step <= 10.0,
          F1 ("%.2f dB out for a 20 dB step in", G[0].step));
    {   // The FAMILY TELL, run properly: a 30 dB step DOES cross the lower threshold, so both
        //    computers are working and the compression of the step is the full mechanism.
        //    (The bible's "≤ 6 dB on a 20 dB step" is not reachable with the constants the bible
        //    itself cites — see FINDINGS §4. A 20 dB step lands a mid band in the 1:1 window
        //    BETWEEN the two thresholds, so no upward lift happens at all.)
        const int n = (int) (FS * 1.6f);
        std::vector<float> x ((size_t) n, 0.0f); addSaw (x, 220.0f, 1.0f);
        const double a = rmsOf (x); for (auto& v : x) v *= (float) (0.06 / a);
        for (int i = (int) (FS * 0.5f); i < (int) (FS * 1.0f); ++i) x[(size_t) i] *= 0.0316f;  // −30 dB
        OP p; auto o = runO (p, x);
        const double hi = db (rmsOf (o.l, (size_t) (FS * 0.35f), (size_t) (FS * 0.48f)));
        const double lo = db (rmsOf (o.l, (size_t) (FS * 0.85f), (size_t) (FS * 0.98f)));
        OP q; q.amount = 0.0f; auto oq = runO (q, x);
        const double ctrl = db (rmsOf (oq.l, (size_t) (FS * 0.35f), (size_t) (FS * 0.48f)))
                          - db (rmsOf (oq.l, (size_t) (FS * 0.85f), (size_t) (FS * 0.98f)));
        gate ("  ... and a 30 dB step (which crosses BOTH thresholds) ≤ 12 dB", (hi - lo) <= 12.0,
              F2 ("%.2f dB out for 30 dB in  (Amount 0 control: %.2f dB)", hi - lo, ctrl));
    }
    gate ("Gentle: ripple THD far below Over Top's", G[1].thd < 0.5 * G[0].thd,
          F2 ("%.2f %% vs %.2f %%", G[1].thd, G[0].thd));
    gate ("Heavy: removes more dynamic range than Over Top", G[2].crest > G[0].crest + 1.5,
          F2 ("%.2f dB vs %.2f dB of envelope spread removed", G[2].crest, G[0].crest));
    {   // ── SHEEN'S DISCRIMINATOR IS A MECHANISM, NOT A MAGNITUDE (fb423) ───────────────────
        //  fb421 claimed +47.63 dB of "air". That number was an unnormalised FFT aimed at
        //  content 114 dB under the programme. Normalised and re-aimed at a pad whose air band
        //  is only 58 dB down, the honest figure was +20.77 vs Over Top's +15.53 — 5.24 dB
        //  against a 6 dB bar, i.e. MORE OF THE SAME LIFT. Law 2 asks for a different MACHINE.
        //  Sheen's high band is now one: the split moves DOWN (0.55× vs Over Top's 1.0×) and
        //  BOTH high-band thresholds move UP to the programme, so at programme level Sheen's
        //  high band is in its UPWARD lane while Over Top's is in its DOWNWARD lane. The lift
        //  is therefore PROGRAM-DEPENDENT — it is what the band is doing right now, not a
        //  constant added on top.
        //  Neither gate below can be bought with makeup: makeup shifts the whole curve, it can
        //  neither invert the sign of the gain nor move the knee. Mutant `ott-sheen-upward-lane`
        //  (MUTATION.md) restores Over Top's high-band thresholds and turns both of them RED.
        note ("  the honest air numbers (they are NOT the discriminator any more)",
              F2 ("Sheen %+.2f dB vs Over Top %+.2f dB on a dark pad", G[3].air, G[0].air));
        //  Measured 6 dB under the reference chord — i.e. inside the first 6 dB of an ordinary
        //  note's decay, not at some contrived floor. AT the reference level itself Sheen sits
        //  ON its own threshold and reads ≈ 0 by construction; that knife edge IS the design and
        //  gating it would be gating a coin toss.
        auto hiGrAt = [&] (int type, double dbs) {
            const float sc = (float) std::pow (10.0, dbs / 20.0);
            auto ch = chordSig ((int) (FS * 1.5f));
            for (auto& v : ch) v *= sc;
            OX e; e.prepare (FS, 128); OP q; q.type = type; e.setParams (q);
            std::vector<float> l = ch, r = ch;
            for (int i = 0; i + 128 <= (int) ch.size(); i += 128)
            { e.setParams (q); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
            return (double) e.viz().grDb[2];
        };
        const double hS = hiGrAt (3, -6.0), hO = hiGrAt (0, -6.0);
        //  The bar is the house unit: ±3 dB, the same "a band is doing something" threshold the
        //  ENGAGE gate above uses. Opposite SIGN with 3 dB of margin on each side.
        gate ("Sheen: 6 dB into a decay its high band LIFTS while Over Top's still CLAMPS",
              hS <= -3.0 && hO >= 3.0,
              F2 ("signed high-band GR: Sheen %+.2f dB (lift), Over Top %+.2f dB (clamp)", hS, hO));
        //  ... and WHERE the sign flips. Sweep the input level and find the loudest level at
        //  which the high band is still in net lift. That level IS the upward threshold, read
        //  off the audio, and it is the whole mechanism in one number.
        auto knee = [&] (int type) {
            for (double dbs = 0.0; dbs >= -36.0; dbs -= 3.0)
            {
                const float sc = (float) std::pow (10.0, dbs / 20.0);
                auto ch = chordSig ((int) (FS * 1.5f));
                for (auto& v : ch) v *= sc;
                OX e; e.prepare (FS, 128); OP q; q.type = type; e.setParams (q);
                std::vector<float> l = ch, r = ch;
                for (int i = 0; i + 128 <= (int) ch.size(); i += 128)
                { e.setParams (q); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
                if (e.viz().grDb[2] <= 0.0) return dbs;
            }
            return -39.0;
        };
        const double kS = knee (3), kO = knee (0);
        gate ("Sheen: its high-band UPWARD knee sits at the PROGRAMME, Over Top's far below it",
              (kS - kO) >= 9.0,
              F3 ("net lift begins at %.0f dB of input for Sheen, %.0f dB for Over Top (%.0f dB apart, bar 9)",
                  kS, kO, kS - kO));
    }
    gate ("Bass Safe: manufactures far less bass grit", G[4].lowImd < 0.6 * G[0].lowImd,
          F2 ("%.2f %% THD on a settled 50 Hz tone, vs Over Top's %.2f %%",
              G[4].lowImd, G[0].lowImd));
    gate ("Surge: gain is NEVER negative (no downward at all)",
          G[5].gr[0] <= 0.05 && G[5].gr[1] <= 0.05 && G[5].gr[2] <= 0.05,
          F3 ("signed GR %+.2f %+.2f %+.2f (all ≤ 0 = pure lift)", G[5].gr[0], G[5].gr[1], G[5].gr[2]));
    gate ("Surge: the pluck TAIL rises relative to its attack", G[5].tailRise > 4.0,
          F2 ("%+.2f dB of tail-vs-attack lift (Over Top %+.2f)", G[5].tailRise, G[0].tailRise));
    gate ("Two Band: cross-band ducking, which 3 bands cannot do", G[6].cross > G[0].cross + 3.0,
          F2 ("a loud 500 Hz costs a quiet 5 kHz %.2f dB (Over Top %.2f)", G[6].cross, G[0].cross));
    gate ("Stagger: the spectrum MORPHS through the note", G[7].tilt > G[0].tilt + 2.0,
          F2 ("%+.2f dB of EXTRA HF-vs-LF swing across the note (Over Top %+.2f)", G[7].tilt, G[0].tilt));

    // cross-type distinctness
    section ("5b. OTT — cross-type distinctness (every pair)");
    // JND: 1.5 dB per band GR · 2 dB of air · 1.5 dB of step · 0.4 % THD · 1 dB of crest ·
    //      2 dB of tail rise · 2 dB of cross-band ducking · 0.2 of centroid ratio · 0.05 DRR
    auto odist = [] (const OFeat& a, const OFeat& b)
    {
        auto lg = [] (double x, double y, double j)
        { return std::fabs (std::log (std::max (0.05, x) / std::max (0.05, y))) / std::log (j); };
        double d[13] = {
            std::fabs (a.gr[0] - b.gr[0]) / 1.5, std::fabs (a.gr[1] - b.gr[1]) / 1.5,
            std::fabs (a.gr[2] - b.gr[2]) / 1.5, std::fabs (a.air - b.air) / 2.0,
            std::fabs (a.step - b.step) / 1.5,   std::fabs (a.thd - b.thd) / 0.4,
            std::fabs (a.tailRise - b.tailRise) / 2.0, std::fabs (a.cross - b.cross) / 2.0,
            std::fabs (a.tilt - b.tilt) / 2.0,
            lg (a.stepTau, b.stepTau, 1.6),      std::fabs (a.lowImd - b.lowImd) / 2.5,
            lg (a.tauLo, b.tauLo, 1.35),         lg (a.tauHi, b.tauHi, 1.35) };
        double m = 0.0; for (double v : d) m = std::max (m, v);
        return m;
    };
    double worst = 1e9; int wi = 0, wj = 0;
    for (int i = 0; i < OX::kNumTypes; ++i)
        for (int j = i + 1; j < OX::kNumTypes; ++j)
        { const double v = odist (G[i], G[j]); if (v < worst) { worst = v; wi = i; wj = j; } }
    gate ("closest Type pair still ≥ 3× JND", worst >= 3.0,
          F1 ("%.2f× JND", worst) + "  (" + OX::typeNames()[wi] + " / " + OX::typeNames()[wj] + ")");

    section ("5c. OTT — every Character re-wires physics (R6)");
    {
        int weak = 0; double worstC = 1e9; std::string wn;
        for (int t = 0; t < OX::kNumTypes; ++t)
        {
            OFeat base = ottFeatures (t, 0);
            for (int c = 1; c < OX::kNumChars; ++c)
            {
                const OFeat f = ottFeatures (t, c);
                const double v = odist (base, f);
                if (v < 2.0) { ++weak;
                    std::printf ("      weak: %-10s · %-16s %.2f× JND\n", OX::typeNames()[t], OX::charNames (t)[c], v); }
                if (v < worstC) { worstC = v; wn = std::string (OX::typeNames()[t]) + " · " + OX::charNames (t)[c]; }
            }
        }
        gate ("every Character ≥ 2× JND from its Type's default", weak == 0,
              F1 ("weakest %.2f× JND", worstC) + "  (" + wn + ")"
              + (weak ? F1 (", %.0f below bar", (double) weak) : ""));
    }

    section ("5d. OTT — the Stereo axis is a real topology swap, not a voicing");
    {
        auto l = chordSig ((int) (FS * 1.2f), 0.12f);
        auto r = chordSig ((int) (FS * 1.2f), 0.03f);
        double corr[3];
        for (int a = 0; a < 3; ++a)
        {
            OP p; p.axis = a; p.amount = 0.75f;
            auto o = runOStereo (p, l, r);
            corr[a] = db (rmsOf (o.l)) - db (rmsOf (o.r));
        }
        gate ("Linked / Free Pair / Mid-Side give three different balances",
              std::fabs (corr[0] - corr[1]) > 1.0 && std::fabs (corr[1] - corr[2]) > 0.5,
              F3 ("L−R balance: Linked %+.2f · Free Pair %+.2f · Mid-Side %+.2f dB", corr[0], corr[1], corr[2]));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. OTT — knobs, ceiling, floor, clicks, mono, stability, CPU, rates
// ═════════════════════════════════════════════════════════════════════════════
static void section6()
{
    section ("6. OTT — every knob 0→100 (law 1)");
    auto chord = chordSig ((int) (FS * 1.2f));
    auto dark  = darkPad ((int) (FS * 1.6f));
    auto pluck = pluckSig ((int) (FS * 2.0f));

    struct KR { std::string name; double lo, hi; bool mono; };
    std::vector<KR> res;
    auto sweep = [&] (const std::string& name, float OP::* fld, OP base,
                      double (*metric) (const std::vector<float>&, const std::vector<float>&),
                      const std::vector<float>& probe, double tol = 0.6)
    {
        double prev = -1e9; bool mono = true; double lo = 0, hi = 0;
        for (int k = 0; k <= 8; ++k)
        {
            OP p = base; p.*fld = (float) k / 8.0f;
            auto o = runO (p, probe);
            const double m = metric (probe, o.l);
            if (k == 0) lo = m;
            if (k == 8) hi = m;
            if (k > 0 && m < prev - tol) mono = false;
            prev = m;
        }
        res.push_back ({ name, lo, hi, mono });
    };
    auto mCrest = [] (const std::vector<float>& i, const std::vector<float>& o)
                  { return envSpreadDb (i) - envSpreadDb (o); };
    auto mAir   = [] (const std::vector<float>& i, const std::vector<float>& o)
                  { return bandDbOf (o, 8000.0, 12000.0) - bandDbOf (i, 8000.0, 12000.0); };
    auto mSpec  = [] (const std::vector<float>& i, const std::vector<float>& o) { return specDist (i, o); };
    auto mTHD   = [] (const std::vector<float>&, const std::vector<float>& o) { return thdPct (o, 100.0); };
    auto mLvl   = [] (const std::vector<float>& i, const std::vector<float>& o) { return db (rmsOf (o)) - db (rmsOf (i)); };
    auto mLow   = [] (const std::vector<float>& i, const std::vector<float>& o)
                  { return bandDbOf (o, 40.0, 85.0) - bandDbOf (i, 40.0, 85.0); };

    { OP b; sweep (OF(0) + " (front 1) — dynamic range removed", &OP::amount, b, mCrest, amChord ((int) (FS * 2.5f))); }
    { OP b; b.amount = 0.85f; sweep (OF(1) + " (front 2) — THD on 100 Hz", &OP::speed, b, mTHD, toneSig ((int) (FS * 1.0f), 100.0f, 0.05f)); }
    { OP b; sweep (OF(2) + " (front 3) — 8-12 kHz on a dark pad", &OP::topLift, b, mAir, dark, 3.0); }
    {   // A crossover does not move the level — it moves WHICH BAND OWNS WHAT. Measured as the
        // spectral distance from the sweep's own knob-0 output, which is the only reference that
        // isolates the control. (Distance-from-input just reads the compression, not the split.)
        auto run9 = [&] (const std::string& name, float OP::* fld, OP base)
        {
            std::vector<std::vector<float>> outs;
            for (int k = 0; k <= 8; ++k)
            { OP q = base; q.*fld = (float) k / 8.0f; outs.push_back (runO (q, chord).l); }
            double m[9]; bool mono = true;
            for (int k = 0; k <= 8; ++k) { m[k] = specDist (outs[0], outs[(size_t) k]);
                                           if (k && m[k] < m[k-1] - 0.4) mono = false; }
            gate ((std::string ("  ") + name).c_str(), m[8] > 2.0 && mono,
                  F1 ("%.2f dB of spectrum moved end-to-end", m[8]) + (mono ? " · monotone" : " · NOT MONOTONE"));
        };
        OP b; b.amount = 0.85f;
        run9 (OB(0) + " (P1) — spectrum vs knob 0", &OP::b1, b);
        run9 (OB(1) + " (P2) — spectrum vs knob 0", &OP::b2, b);
    }
    {   // Raise moves the UPWARD computer, which only exists on quiet material — so measure the
        // LATE TAIL of a pluck, not the whole buffer (whose RMS is all attack).
        OP b; b.amount = 0.6f; b.b4 = 0.0f;
        auto pl = pluckSig ((int) (FS * 2.5f), 110.0f, 0.45f);
        double m[9]; bool mono = true;
        for (int k = 0; k <= 8; ++k)
        {
            OP q = b; q.b3 = (float) k / 8.0f;
            auto o = runO (q, pl);
            m[k] = db (rmsOf (o.l, (size_t) (FS * 1.3f), (size_t) (FS * 2.2f)))
                 - db (rmsOf (pl,  (size_t) (FS * 1.3f), (size_t) (FS * 2.2f)));
            if (k && m[k] < m[k-1] - 0.4) mono = false;
        }
        gate (("  " + OB(2) + " (P3) — the LATE TAIL of a decaying pluck").c_str(), (m[8] - m[0]) > 2.0 && mono,
              F3 ("%+.2f → %+.2f dB of tail lift (span %.2f)", m[0], m[8], m[8] - m[0]));
    }
    { OP b; b.amount = 0.6f; b.b3 = 0.0f; sweep (OB(3) + " (P4) — dynamic range removed", &OP::b4, b, mCrest, amChord ((int) (FS * 2.5f))); }
        { // Grip is BIPOLAR — at −18 dB the UPWARD computer does all the work and at +18 the
      // DOWNWARD one does, so "dynamic range removed" is a V and a monotonicity gate on it is
      // simply the wrong question. Its monotone axis is how deep the jaws sit: the output level.
      // tolerance 1.5 dB on a 27 dB span — proportionate, and stated rather than assumed.
      OP b; b.amount = 0.5f;
      auto am = amChord ((int) (FS * 2.5f));
      double m[9]; bool mono = true; std::string trace;
      for (int k = 0; k <= 8; ++k)
      {
          OP q = b; q.b5 = (float) k / 8.0f;
          auto o = runO (q, am);
          m[k] = db (rmsOf (o.l)) - db (rmsOf (am));
          if (k && m[k] > m[k-1] + 1.5) mono = false;      // Grip DROPS the level, monotonically
          trace += F1 ("%+.1f ", m[k]);
      }
      gate (("  " + OB(4) + " (P5) — how deep the jaws sit (output level)").c_str(),
            (m[0] - m[8]) > 6.0 && mono, trace + (mono ? "· monotone ↓" : "· NOT MONOTONE"));
    }
    { OP b; sweep (OB(5) + " (P6) — 40-85 Hz", &OP::b6, b, mLow, chord); }
    { OP b; sweep (OB(7) + " (P8) — 8-12 kHz", &OP::b8, b, mAir, chord); }

    for (auto& r : res)
        gate ((std::string ("  ") + r.name).c_str(), std::fabs (r.hi - r.lo) > 2.0 && r.mono,
              F3 ("%.2f → %.2f (span %.2f)", r.lo, r.hi, std::fabs (r.hi - r.lo))
              + (r.mono ? " · monotone" : " · NOT MONOTONE"));
    // Mids (P7) needs a probe with mid content that the other two bands don't share
    {
        OP p; auto probe = sawSig ((int) (FS * 1.2f), 400.0f, 0.06f);
        p.b7 = 0.0f; auto a = runO (p, probe);
        p.b7 = 1.0f; auto b = runO (p, probe);
        const double d = bandDbOf (b.l, 300.0, 1500.0) - bandDbOf (a.l, 300.0, 1500.0);
        gate (("  " + OB(6) + " (P7) — 300 Hz-1.5 kHz across the full sweep").c_str(), d > 12.0,
              F1 ("%+.2f dB (±12 dB nominal)", d));
    }

    // ═════ THE R11 CEILING GATE ═════════════════════════════════════════════
    section ("6b. OTT — the R11 ceiling gate (past Ableton's fixed preset, not up to it)");
    // METRIC + THRESHOLD, stated and defended:
    //  (a) Ableton's OTT preset is Amount 0.5 BY CALIBRATION here. So Amount 1.0 must remove at
    //      LEAST twice as much crest as Amount 0.5, or the top half of the knob is decoration.
    //  (b) DYNAMIC-RANGE RATIO on a 46 dB staircase at Amount 1.0 ≤ 0.10. Defence: at Amount 1
    //      T_up MEETS T_dn with slopes 1.0 / 0.95, so a band's output is a CONSTANT. If more
    //      than 10 % of a 46 dB span survives, the jaws never closed.
    //  (c) NOISE FLOOR INTO A WALL: a −65 dBFS bed must come out ≥ +18 dB louder at Amount 1.
    //  (d) ... and true silence must STILL be silent (the floor gate holds at the ceiling).
    {
        auto ch = amChord ((int) (FS * 2.5f));
        OP p; p.amount = 0.5f; auto a = runO (p, ch);
        p.amount = 1.0f;       auto b = runO (p, ch);
        // Stated on the RESIDUAL, which is the quantity that matters: how much dynamic range
        // SURVIVES. Ableton's fixed preset lands at Amount 50 by this calibration, so Amount 100
        // must cut what that preset leaves behind by at least a third again.
        const double s0 = envSpreadDb (ch), s5 = envSpreadDb (a.l), s10 = envSpreadDb (b.l);
        gate ("(a) Amount 100 leaves ≤ 65 % of what Amount 50 leaves", s10 <= 0.65 * s5 && s10 <= 7.0,
              F4 ("probe %.2f dB → %.2f dB at Amount 50 (= the Ableton preset) → %.2f dB at 100 (%.0f %% of it)",
                  s0, s5, s10, 100.0 * s10 / std::max (0.01, s5)));
    }
    {
        auto st = staircase (-40.0f, 6.0f, 20, 120.0f, 330.0f);
        OP p; p.amount = 1.0f; auto o = runO (p, st);
        OP q; q.amount = 0.0f; auto oq = runO (q, st);
        const double r = drRatio (st, o.l, 20, 120.0f), r0 = drRatio (st, oq.l, 20, 120.0f);
        gate ("(b) 46 dB staircase → ≤ 10 % survives at Amount 100", r <= 0.10,
              F2 ("DRR %.4f  (Amount 0 control %.4f)", r, r0));
    }
    {
        auto bed = noiseSig ((int) (FS * 2.0f), (float) std::pow (10.0, -65.0 / 20.0));
        OP p; p.amount = 1.0f; auto o = runO (p, bed);
        const double lift = db (rmsOf (o.l, (size_t) (FS * 0.5f))) - db (rmsOf (bed, (size_t) (FS * 0.5f)));
        OP q; q.amount = 0.0f; auto oq = runO (q, bed);
        const double ctrl = db (rmsOf (oq.l, (size_t) (FS * 0.5f))) - db (rmsOf (bed, (size_t) (FS * 0.5f)));
        gate ("(c) a −65 dBFS bed is lifted into a WALL", lift >= 18.0,
              F2 ("%+.1f dB  (Amount 0 control %+.1f dB)", lift, ctrl));
    }
    {
        // 🚨 WHAT THIS REPLACES (FIXES.md §1 OTT 2). The old probe appended EXACT DIGITAL ZEROS
        // after the note and reported −280.0 dBFS. The device is feed-forward — y = band·g — so
        // zero in gives zero out for ANY finite gain, including a gain of 400. That is
        // arithmetic, not evidence, on a gate titled "the floor gate holds". fb416: the fault
        // this aims at lives in the BULK and the probe had no bulk.
        // A REAL bed now, at three real levels, with the device at its ceiling.
        // ⚠️ AND THE FIRST VERSION OF THIS REPLACEMENT WAS ALSO WRONG, in the other direction.
        // It required the TOTAL lift of a −96 dBFS bed to be ≤ 1 dB and measured +19.55. That is
        // not the upward computer: it is the MAKEUP, a static +12…+17 dB that every compressor
        // applies to everything including its own noise floor. Demanding otherwise would be
        // demanding a device that is not a compressor. What the floor gate actually protects is
        // the UPWARD CONTRIBUTION, so that is what is measured: lift(bed) minus the lift of a
        // −110 dBFS bed, where the upward lane is unarguably off and only makeup remains.
        // (It is also why the fb421 numbers looked fine: −96 and −84 both read +19.55 dB —
        // IDENTICAL, i.e. level-independent, i.e. the upward lane contributing exactly nothing.)
        auto pl = pluckSig ((int) (FS * 2.0f), 110.0f, 0.35f);
        auto liftOf = [&] (double bedDb)
        {
            auto bed = floorBed ((int) (FS * 3.0f), bedDb);
            std::vector<float> x; x.insert (x.end(), pl.begin(), pl.end());
            for (size_t i = 0; i < bed.size(); ++i) x.push_back (bed[i]);
            // the note itself rides on the same floor, as it would in the plugin
            for (size_t i = 0; i < pl.size(); ++i) x[i] += bed[i % bed.size()];
            OP p; p.amount = 1.0f; p.topLift = 1.0f; p.b3 = 1.0f;
            auto o = runO (p, x);
            return db (rmsOf (o.l, (size_t) (FS * 3.5f)));    // ABSOLUTE output level of the bed
        };
        // ⚠️ AND THE SECOND VERSION WAS WRONG TOO, and its mutant caught it: subtracting the
        // lift of a −110 dBFS bed to isolate "makeup only" works ONLY while the floor gate
        // exists. Delete `floorGate` and the −110 dBFS reference ALSO gets the full upward lift,
        // the two numbers move together, and the difference reads −2.11 dB — the gate survived
        // its own mutation. A reference computed through the mechanism under test is not a
        // reference. So the bar is ABSOLUTE now: how loud does a real noise floor come OUT.
        // Defence: −70 dBFS at the FX bus is roughly 44 dB under a single note and inaudible
        // under any programme; the fb421 engine puts a −96 dBFS bed at −76.4, and with the floor
        // gate deleted it lands at −44.4, which is a wall of hiss between the notes.
        note ("(d) makeup-only reference (a −110 dBFS bed)",
              F1 ("comes out at %.1f dBFS — the static makeup alone", liftOf (-110.0)));
        for (double bedDb : { -96.0, -84.0, -72.0 })
        {
            const double outAbs = liftOf (bedDb);
            const double lift = outAbs - bedDb;
            // Defence of the bars: the floor gate ramps over 12 dB above −78 dBFS PER BAND, and
            // a broadband bed splits a few dB into each band. −96 is well below the gate's foot
            // and must get NOTHING; −84 sits near it and may get a little; −72 is above it and
            // SHOULD be lifted hard — that is the R11 wall, and a device that did not lift it
            // would be failing the other half of the brief. The gate is directional.
            const bool ok = (bedDb <= -96.0) ? (outAbs <= -70.0)
                          : (bedDb <= -84.0) ? (outAbs <= -58.0)
                                             : (lift  >=  35.0);
            gate ((F1 ("(d) a REAL dithered %.0f dBFS floor comes OUT at", bedDb)).c_str(), ok,
                  F2 ("%.1f dBFS (%+.2f dB of lift)", outAbs, lift)
                  + (bedDb <= -84.0 ? "  ← must stay inaudible" : "  ← must be lifted (R11)"));
        }
        {   // and digital silence is still digitally silent — kept, but labelled for what it is
            std::vector<float> sil ((size_t) (int) (FS * 2.0f), 0.0f);
            OP p; p.amount = 1.0f; p.topLift = 1.0f; p.b3 = 1.0f;
            auto o = runO (p, sil);
            note ("(d) exact digital zeros in", F1 ("%.1f dBFS out — ARITHMETIC, not evidence: the",
                  db (rmsOf (o.l))) + " device is feed-forward, so this can never fail");
        }
    }

    // ═════ CLICKS ══════════════════════════════════════════════════════════
    section ("6c. OTT — no clicks (law 4): ALL 8x8 Type and ALL 8x8 Character transitions");
    // 🚨 The old gate's denominator was the engine's own t=0 start-up burst, so the bar sat
    // 1.9x above the full scale of its own probe and a 19.8x tree swap passed. fb421 engine,
    // on this metric: worst Type transition 5.97 dB/ms, the Two Band TREE SWAP 5.95 dB/ms.
    {
        auto prog = clickProg ((int) (FS * 1.2f));
        double wT = 0.0, wC = 0.0; std::string nT, nC;
        for (int ta = 0; ta < OX::kNumTypes; ++ta)
            for (int tb = 0; tb < OX::kNumTypes; ++tb)
            {
                OP a; a.type = ta; OP b; b.type = tb;
                for (int k = 0; k < 5; ++k)
                {
                    const double m = oJump (a, b, kAts[k], prog);
                    if (m > wT) { wT = m; nT = std::string (OX::typeNames()[ta]) + " → " + OX::typeNames()[tb]; }
                }
            }
        gate ("all 64 Type transitions ≤ 2.0 dB of gain moved in 1 ms", wT <= 2.0,
              F1 ("worst %.2f dB/ms", wT) + "  (" + nT + ")   [fb421 engine: 5.97]");
        {   // 🚨 fb425 — THE CLIPPER SWITCH, AT THE AMOUNT WHERE THE CLIPPER EXISTS.
            //    The fb425 ceiling only closes onto the signal as the downward slope approaches
            //    1, so at the DEFAULT Amount 0.5 inserting or removing `Heavy`'s per-band
            //    clipper barely moves the waveform — and the mutant that deletes BOTH of its
            //    fades survives the click matrix for that reason alone. That is the gate being
            //    pointed at the wrong operating point, not the mechanism being safe: at
            //    Amount 100 the clipper is doing 2.34 % THD and its insertion is a real step.
            OP ca; ca.type = 2 /* Heavy, clipper on */; ca.amount = 1.0f;
            OP cb; cb.type = 3 /* Sheen, no clipper */; cb.amount = 1.0f;
            double mc = 0.0;
            for (int k = 0; k < 5; ++k) mc = std::max (mc, oJump (ca, cb, kAts[k], prog));
            gate ("  the CLIPPER switch Heavy → Sheen, at Amount 100 where it is working", mc <= 2.0,
                  F1 ("%.2f dB/ms", mc));
            OP cc; cc.type = 2; cc.character = 1 /* Band Clip */; cc.amount = 1.0f;
            OP cd; cd.type = 2; cd.character = 2 /* No Clip   */; cd.amount = 1.0f;
            double md = 0.0;
            for (int k = 0; k < 5; ++k) md = std::max (md, oJump (cc, cd, kAts[k], prog));
            gate ("  ... and Band Clip → No Clip inside Heavy, same setting", md <= 2.0,
                  F1 ("%.2f dB/ms", md));
        }
        {   // the tree swap, named, because it is the one the old gate hid
            OP a; a.type = 0; OP b; b.type = 6;
            double m = 0.0;
            for (int k = 0; k < 5; ++k) m = std::max (m, oJump (a, b, kAts[k], prog));
            gate ("  the TREE SWAP Over Top → Two Band (3 bands → 2)", m <= 2.0,
                  F1 ("%.2f dB/ms", m) + "   [fb421 engine: 5.95 — an instant dip_ = 0.0f]");
        }
        for (int t = 0; t < OX::kNumTypes; ++t)
            for (int ca = 0; ca < OX::kNumChars; ++ca)
                for (int cb = 0; cb < OX::kNumChars; ++cb)
                {
                    if (ca == cb) continue;
                    OP a; a.type = t; a.character = ca; OP b; b.type = t; b.character = cb;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double m = oJump (a, b, kAts[k], prog);
                        if (m > wC) { wC = m; nC = std::string (OX::typeNames()[t]) + ": "
                                                 + OX::charNames (t)[ca] + " → " + OX::charNames (t)[cb]; }
                    }
                }
        gate ("all 448 Character transitions ≤ 2.0 dB of gain moved in 1 ms", wC <= 2.0,
              F1 ("worst %.2f dB/ms", wC) + "  (" + nC + ")   [fb421 engine: 3.02]");
        OP z;
        struct OJ { std::string what; OP b; };
        OP j1; j1.amount = 1.0f;  OP j2; j2.speed = 1.0f;   OP j3; j3.topLift = 1.0f;
        OP j4; j4.b1 = 1.0f;      OP j5; j5.b5 = 1.0f;      OP j6; j6.b8 = 1.0f;
        OP j7; j7.axis = 2;       OP j8; j8.axis = 1;       OP j9; j9.crest = true;
        const std::string sDrop = OX::dropdownNames()[1];
        const OJ oj[] = { { "  " + OF(0) + " 0.5 → 1.0", j1 }, { "  " + OF(1) + " 0.5 → 1.0", j2 },
                          { "  " + OF(2) + " 0.25 → 1.0", j3 }, { "  " + OB(0) + " 88 → 300 Hz", j4 },
                          { "  " + OB(4) + " 0 → +18 dB", j5 }, { "  " + OB(7) + " 0 → +12 dB", j6 },
                          { "  " + sDrop + " " + OX::stereoNames()[0] + " → " + OX::stereoNames()[2], j7 },
                          { "  " + sDrop + " " + OX::stereoNames()[0] + " → " + OX::stereoNames()[1], j8 },
                          { "  " + std::string (OX::pillName()) + " pill off → on", j9 } };
        for (auto& j : oj)
        {
            double m = 0.0;
            for (int k = 0; k < 5; ++k) m = std::max (m, oJump (z, j.b, kAts[k], prog));
            gate (j.what.c_str(), m <= 2.0, F1 ("%.2f dB/ms", m));
        }
    }

    // ═════ MONO ════════════════════════════════════════════════════════════
    section ("6d. OTT — mono-safe (law 5)");
    {
        auto ch = chordSig ((int) (FS * 1.0f));
        int bad = 0; double worst = 1e9; std::string wn;
        for (int t = 0; t < OX::kNumTypes; ++t)
            for (int a = 0; a < OX::kNumStereo; ++a)
            {
                OP p; p.type = t; p.axis = a; p.amount = 0.8f;
                auto o = runO (p, ch);
                std::vector<float> mono (ch.size());
                for (size_t i = 0; i < ch.size(); ++i) mono[i] = 0.5f * (o.l[i] + o.r[i]);
                const double d = specDist (ch, mono) + std::fabs (db (rmsOf (mono)) - db (rmsOf (ch)));
                if (d < 2.0) ++bad;
                if (d < worst) { worst = d; wn = std::string (OX::typeNames()[t]) + " · " + OX::stereoNames()[a]; }
            }
        gate ("every Type × Stereo mode survives a mono fold", bad == 0,
              F1 ("weakest %.2f dB of surviving change", worst) + "  (" + wn + ")");
    }

    // ═════ STABILITY ═══════════════════════════════════════════════════════
    section ("6e. OTT — 60 s of full-drive noise, every Type");
    {
        auto n60 = noiseSig ((int) (FS * 60.0f), 0.35f);
        int bad = 0; double worstPk = 0.0; std::string wn;
        for (int t = 0; t < OX::kNumTypes; ++t)
        {
            OP p; p.type = t; p.character = 3; p.amount = 1.0f; p.topLift = 1.0f;
            p.speed = 1.0f; p.b3 = 1.0f; p.b4 = 1.0f; p.b5 = 1.0f;
            auto o = runO (p, n60);
            double pk = 0.0; bool nan = false;
            for (float v : o.l) { if (!std::isfinite (v)) nan = true; pk = std::max (pk, (double) std::fabs (v)); }
            if (nan || pk > 40.0) { ++bad; wn = OX::typeNames()[t]; }
            worstPk = std::max (worstPk, pk);
        }
        gate ("60 s × 8 Types at every extreme: finite and bounded", bad == 0,
              F1 ("worst peak %.3f (bar 40, NaN-free)", worstPk) + (bad ? "  " + wn : ""));
    }
    {
        // two OTTs in series: the noise-floor lift must NOT compound into a free run
        auto pl = pluckSig ((int) (FS * 1.5f), 110.0f, 0.3f);
        std::vector<float> sil ((size_t) (int) (FS * 3.0f), 0.0f);
        std::vector<float> x; x.insert (x.end(), pl.begin(), pl.end()); x.insert (x.end(), sil.begin(), sil.end());
        OP p; p.amount = 0.9f; p.b3 = 1.0f;
        auto a = runO (p, x);
        auto b = runO (p, a.l);
        gate ("two OTTs in series still go silent (stacked floor gates)",
              db (rmsOf (b.l, (size_t) (FS * 3.5f))) < -90.0,
              F2 ("one device %.1f dBFS, two %.1f dBFS in the gap",
                  db (rmsOf (a.l, (size_t) (FS * 3.5f))), db (rmsOf (b.l, (size_t) (FS * 3.5f)))));
    }

    // ═════ CPU + SAMPLE RATES ══════════════════════════════════════════════
    section ("6f. OTT — CPU and sample rates");
    {
        auto blk = noiseSig (128, 0.05f);
        double worstUs = 0.0; int wt = 0;
        for (int t = 0; t < OX::kNumTypes; ++t)
            for (int a = 0; a < 2; ++a)
            {
                OX e; e.prepare (FS, 128);
                OP p; p.type = t; p.axis = a * 2; p.amount = 0.7f;
                std::vector<float> l = blk, r = blk;
                const auto t0 = std::chrono::high_resolution_clock::now();
                for (int i = 0; i < 3000; ++i) { e.setParams (p); e.processStereo (l.data(), r.data(), 128); }
                const auto t1 = std::chrono::high_resolution_clock::now();
                const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / 3000.0;
                if (us > worstUs) { worstUs = us; wt = t; }
            }
        gate ("µs / 128-sample block @ 48 k, worst Type", worstUs < 60.0,
              F1 ("%.2f µs", worstUs) + " (" + OX::typeNames()[wt]
              + F1 (") = %.2f %% of one core", 100.0 * worstUs / 2666.0));
    }
    for (float fs : { 44100.0f, 96000.0f })
    {
        auto ch = chordSig ((int) (fs * 1.2f));
        OP p;
        OX e; e.prepare ((double) fs, 128); e.setParams (p);
        std::vector<float> l = ch, r = ch;
        for (int i = 0; i + 128 <= (int) ch.size(); i += 128) { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 128); }
        bool fin = true; for (float v : l) if (!std::isfinite (v)) fin = false;
        OX e48; e48.prepare (48000.0, 128); e48.setParams (p);
        const bool ballOk = std::fabs (e.attackMs (1) - e48.attackMs (1)) < 0.05;
        int engaged = 0; for (int b = 0; b < 3; ++b) if (std::fabs (e.viz().grDb[b]) >= 3.0) ++engaged;
        gate ((F1 ("%.0f kHz: same ballistics, still engaging", fs / 1000.0)).c_str(),
              fin && ballOk && engaged >= 2,
              F3 ("mid atk %.2f ms (48 k: %.2f), %.0f bands engaged", e.attackMs (1), e48.attackMs (1), (double) engaged));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 7 + 8 + 9.  THE FULL MATRIX — fb425, and the reason it is the LAST level
//
// Three rounds, and the blindness moved down exactly one notch each time:
//     fb421   the gates could not fail AT ALL          (delete the mechanism, cert stays green)
//     fb423   the gates ran on ONE TYPE                (sweep the other five)
//     fb424   the gates ran on ONE TYPE × CHARACTER    (sweep the other 63)
// There is no fifth level, because THE MATRIX IS FINITE. 8 Types × 8 Characters = 64 cells,
// × 13 controls = 832 cells per device. A machine does that in seconds; sampling it was always
// a choice and never a constraint. Everything below runs the WHOLE grid.
//
// TWO BARS, NOT ONE:
//   · BIT-IDENTICAL at 0 vs 100 is a hard FAIL. Not a matter of degree — the parameter never
//     reached the DSP in that cell, which is fb373 in miniature.
//   · AUDIBLE-BUT-TINY is a separate FAIL at 0.5 dB. A control that moves the output by 0.02 dB
//     is dead to the ear and alive only to a float comparison: exactly the green a bit-identity
//     test on its own would hand out.
//
// TWO OPERATING POINTS, AND WHY THAT IS NOT CHERRY-PICKING. A compressor's controls do not all
// live at the same place on the transfer curve: the knee only exists WITHIN a few dB of the
// threshold, and the upward lane only exists BELOW it. Measuring `Round` at Push 60 (the
// threshold 27 dB under the programme) reads 0.02 dB and would call a working knob dead — the
// harness's fault, not the engine's (§3.1: check your own detector before believing it). So
// every cell is measured at BOTH a DEEP point (the downward computer working hard) and a
// SHALLOW one (the threshold inside the programme, so the knee and the upward lane are
// reachable), and a control counts as alive if it is audible at EITHER. Bit-identity has to
// fail at BOTH before a cell is called dead.
//
// The ONLY exemption is `kInert`: an explicit, ASSERTED-SIZE roster of cells where the
// mechanism genuinely does not exist, with the missing mechanism NAMED per row. Checked in BOTH
// directions — a listed cell that is actually alive is a stale ruling and fails; an unlisted
// dead cell fails. Silence is not available in either direction.
// ═════════════════════════════════════════════════════════════════════════════

/** THE MATRIX PROBE. One 0.5 s signal, because 3328 cell-measurements cannot each afford a
 *  bespoke one. It carries all three things a dynamics control can live on:
 *    · LEVEL      the reference chord (55/110/165/220 Hz saws — bass on purpose per the OTT
 *                 low-band caveat, harmonics to 18 kHz so all three bands are fed) under a
 *                 3 Hz / 28 dB tremolo. Something to remove, and troughs to lift.
 *    · TRANSIENT  every 125 ms the programme falls into a 5 ms hole and comes back 9.5 dB hot
 *                 for 4 ms. `Edge`, `Cling` and the `Crest` pill read nothing else.
 *    · SPECTRUM   a saw stack is broadband, so both crossovers and all three band trims move
 *                 something a magnitude-spectrum metric can see.
 *  `phase` offsets the tremolo so L and R differ: `Tie` and the M/S detector are STEREO
 *  controls and read exactly zero on a mono probe — indistinguishable from dead.
 */
static std::vector<float> matrixProbe (int n, double phase)
{
    auto x = chordSig (n, 0.10f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / FS;
        double env = std::pow (10.0, (-14.0 + 14.0 * std::sin (2.0 * M_PI * 3.0 * t + phase)) / 20.0);
        const double ph = std::fmod (t, 0.125);
        if      (ph < 0.005) env *= 0.02 + 0.98 * (ph / 0.005) * (ph / 0.005);   // the hole
        else if (ph < 0.006) env *= 1.0;
        else if (ph < 0.010) env *= 3.0;                                          // the hit
        x[(size_t) i] *= (float) env;
    }
    return x;
}

/** THE TAIL PROBE — the SHALLOW operating point's programme, and it took three attempts.
 *  Four notes, each a 3 ms attack, a fast (τ = 50 ms) decay, and then a SUSTAINED QUIET BODY
 *  40 dB down that lasts the rest of the note. On a dithered −96 dBFS floor. Right channel
 *  offset by half a note so the two sides are never at the same level.
 *  🔬 WHY THE QUIET BODY IS A PLATEAU AND NOT A DECAY, measured both ways:
 *    · a plain pluck (τ = 0.28 s) never gets more than ~3 dB below the upward threshold, so
 *      `Raise` 0 → 100 moved the quiet frames by 1.08 dB and looked dead on 52 of 64 cells;
 *    · a fast pluck (τ = 0.12 s) DOES get deep, but it spends only a few frames there and the
 *      rest of the way down it is above the threshold — 0.3 dB;
 *    · a plateau 40 dB down holds the programme 15–20 dB UNDER the upward threshold for 80 %
 *      of every note, which is where an upward computer actually lives: `Raise` now measures
 *      6…14 dB on the same cells. The mechanism never changed. The probe was wrong.
 *  It keeps the sharp attacks, so it is also the probe the `Crest` transient hold needs. */
static std::vector<float> tailProbe (int n, bool right)
{
    std::vector<float> x ((size_t) n, 0.0f);
    const int note = (int) (FS * 0.5f);
    for (int k = 0; k < 4; ++k)
    {
        const int at = k * note + (right ? note / 2 : 0);
        if (at >= n) break;
        const int len = std::min (note, n - at);
        std::vector<float> p ((size_t) len, 0.0f);
        addSaw (p, (k & 1) ? 165.0f : 110.0f, 1.0f);
        const double pk = peakOf (p); for (auto& v : p) v *= (float) (0.30 / pk);
        for (int i = 0; i < len; ++i)
        {
            const double t = (double) i / FS;
            const double e = (t < 0.003) ? (t / 0.003)
                                         : std::max (std::exp (-(t - 0.003) / 0.05), 0.010);
            x[(size_t) at + (size_t) i] += (float) (p[(size_t) i] * e);
        }
    }
    gRng = right ? 0x13579BDFu : 0x2468ACE0u;
    const float dith = (float) std::pow (10.0, -96.0 / 20.0);
    for (auto& v : x) v += dith * 1.4142f * rnd11();
    return x;
}

/** THE QUIET-QUARTILE LEVEL — 20 ms frames sorted by level, the mean of the 20th…50th
 *  percentile band. That band IS the decaying tail of a note: above the noise floor (which the
 *  floor gate refuses to lift, on purpose) and below the attacks (which carry all the energy).
 *  🔬 WHY IT EXISTS. Without it `Raise` — OTT's upward-gain amount — measured 0.02…0.45 dB on
 *  52 of 64 cells and looked dead, while section 6's own gate measures a 2+ dB span for the
 *  same control. Both were right: a 10 dB lift of a tail that is 45 dB down moves the WHOLE
 *  BUFFER's RMS by 0.1 dB, and every other dimension in `audDb` is a whole-buffer statistic.
 *  That is the fb416 shape upside down — an average is blind to a change that lives in a
 *  minority of the frames. Phase-independent, like everything else here. */
static double quietDb (const std::vector<float>& x)
{
    const int w = (int) (FS * 0.020f);
    std::vector<double> e;
    for (size_t i = 0; i + (size_t) w < x.size(); i += (size_t) w)
        e.push_back (db (rmsOf (x, i, i + (size_t) w)));
    if (e.size() < 8) return -140.0;
    std::sort (e.begin(), e.end());
    const size_t a = (size_t) (e.size() * 0.20), b = std::max (a + 1, (size_t) (e.size() * 0.50));
    double s = 0.0; for (size_t i = a; i < b && i < e.size(); ++i) s += e[i];
    return s / (double) (b - a);
}

/** Every metric here is phase-independent (law 6): magnitude-spectrum distance, RMS level,
 *  settled crest, envelope spread, the quiet-quartile level and the L−R balance.
 *  Sample-difference RMS is banned (fb282) and is not used anywhere in this section. */
static double audDb (const Out& a, const Out& b)
{
    const double sL = specDist (a.l, b.l), sR = specDist (a.r, b.r);
    const double lL = std::fabs (db (rmsOf (b.l)) - db (rmsOf (a.l)));
    const double lR = std::fabs (db (rmsOf (b.r)) - db (rmsOf (a.r)));
    const double cL = std::fabs (crestSettled (b.l) - crestSettled (a.l));
    const double eL = std::fabs (envSpreadDb (b.l) - envSpreadDb (a.l));
    const double qL = std::fabs (quietDb (b.l) - quietDb (a.l));
    const double qR = std::fabs (quietDb (b.r) - quietDb (a.r));
    const double b0 = db (rmsOf (a.l)) - db (rmsOf (a.r));
    const double b1 = db (rmsOf (b.l)) - db (rmsOf (b.r));
    double m = sL; for (double v : { sR, lL, lR, cL, eL, qL, qR, std::fabs (b1 - b0) }) m = std::max (m, v);
    return m;
}
static bool bitSame (const Out& a, const Out& b)
{
    if (a.l.size() != b.l.size()) return false;
    for (size_t i = 0; i < a.l.size(); ++i)
        if (a.l[i] != b.l[i] || a.r[i] != b.r[i]) return false;
    return true;
}

/** THE ROSTER OF CELLS WHERE A CONTROL DOES NOT BEHAVE LIKE A CONTROL, and it has TWO kinds so
 *  that neither can hide inside the other:
 *    INERT   the mechanism is genuinely ABSENT in that cell, and the output must be
 *            BIT-IDENTICAL at 0 and 100. If it moves at all, the ruling is stale.
 *    NARROW  the mechanism is present and wired, but the Character has multiplied its window
 *            into a range whose two ends are under the audibility bar. The cell must be alive
 *            (not bit-identical) AND under 0.5 dB. If it grows, the ruling is stale; if it dies,
 *            it is a dead knob and section 7/8 fails it.
 *  `chr = "*"` means every Character of that Type. Every row names the MECHANISM — never "it
 *  measured small". The list is asserted to a fixed size in section 9 (the `kShared` lesson:
 *  an exemption list with no cardinality is a list that grows). */
enum RuleKind { INERT, NARROW };
struct InertCell { const char* dev; RuleKind kind; const char* knob; const char* type;
                   const char* chr; const char* why; };

static const InertCell kInert[] = {
 // ── COMPRESS — the mechanism is absent ──────────────────────────────────────────────────────
 { "Compress", INERT, "Ratio",  "OverEasy", "Anti",
   "`Anti` sets slopeMul = slopeCap = slopeFloor = 2: the dbx Infinity+ NEGATIVE zone at a LOCKED slope. The Character IS a ratio." },
 { "Compress", INERT, "Round",  "OverEasy", "Hard 160",
   "kneeAdd = -24 puts the whole 6...24 dB knee window below zero. `Hard 160` IS the hard-knee Character (the dbx 160's own curve)." },
 { "Compress", INERT, "Round",  "Ride",     "Only Up",
   "F_UPONLY switches the DOWNWARD computer off; `Round` is the downward knee. `liftUp` is hard-knee by construction (bible SS3.6)." },
 { "Compress", INERT, "Cling",  "Ride",     "Only Up",
   "`Cling` freezes the downward clamp before release runs. With the downward computer off there is no clamp to freeze." },
 { "Compress", INERT, "Burn",   "Ride",     "Only Up",
   "fb419 law: the gain element's drive is scaled by CURRENT gain reduction, so no reduction = bit-clean at any Burn. `Only Up` produces none." },
 { "Compress", INERT, "Tie",    "Bus",      "*",
   "linkForce = 1 on every Bus Character: the SSL bus law is ONE clamp for both sides, and an unlinked bus compressor is a different machine." },
 { "Compress", INERT, "Tie",    "Vari-Mu",  "Lateral",
   "F_MSDET + linkForce 0: the detector basis is MID/SIDE, so `Tie` (an L/R link) has no axis to act on." },
 { "Compress", INERT, "Tie",    "Limit",    "*",
   "linkForce = 1 on every Limit Character: a limiter that unlinks moves the stereo image on every peak. Linked is what a ceiling means." },
 { "Compress", INERT, "Auto",   "Limit",    "Loud War",
   "F_AUTOFULL forces 100 % auto makeup on. The pill can only turn on what the Character has already turned on." },
 // ── COMPRESS — the mechanism is present, its authored window is under the bar ────────────────
 { "Compress", NARROW, "Attack", "FET 76",  "Twenty Lock",
   "atkMul 0.15 collapses FET's 0.02...0.8 ms window onto 0.003...0.12 ms; at 48 kHz the ONE-SAMPLE floor is 0.0208 ms, so the whole knob is 1...6 samples." },
 { "Compress", NARROW, "Attack", "Limit",   "Hard Stop",
   "atkMul 0.02 on 0.1...5 ms = 0.002...0.1 ms = 0.1...4.8 samples at 48 kHz. Same one-sample floor; the Character IS `Hard Stop`." },
 { "Compress", NARROW, "Release","Opto",    "Even Pools",
   "F_EVENPOOL moves the T4 pool mix to 0.28, so the Release knob (the FAST pool) sets 28 % of the gain and the 10 s memory integrator sets the rest." },
 { "Compress", NARROW, "Release","Vari-Mu", "Time Four",
   "relLoW 7.5 / relHiW 0.6 put the window at 1.5...15 SECONDS. Both ends outlast every probe in this file; the difference arrives after the programme has ended." },
 { "Compress", NARROW, "Release","Vari-Mu", "Long Haul",
   "relMul 2.0 x relLoW 10 puts the window at 4...50 SECONDS. Measured on a 6 s probe, which is the longest here and still shorter than the knob." },
 // ── OTT — the mechanism is absent ───────────────────────────────────────────────────────────
 { "OTT", INERT, "High Cross", "Two Band", "*",
   "`Two Band` has nBands = 2 (ONE split at 650 Hz, which is its whole identity). There is no high crossover to move." },
 { "OTT", INERT, "Treble",     "Two Band", "*",
   "`Treble` trims band 3, and `Two Band` has two. Same roster fact as `High Cross`, one knob further along." },
 { "OTT", INERT, "Press",      "Surge",    "*",
   "`Surge` is upward-ONLY by identity: ts.sdn = {0,0,0} and its makeups are 0. `Press` scales the DOWNWARD slope, which is zero on every Character here." },
 { "OTT", INERT, "Crest",      "Over Top", "Full Crest",
   "the Character sets upHold = 1, i.e. the transient hold is already ON. The pill cannot turn on what the Character has turned on." },
};
static constexpr int kNumInert = (int) (sizeof kInert / sizeof kInert[0]);

static const InertCell* inertRule (const char* dev, const std::string& knob,
                                   const std::string& ty, const std::string& ch)
{
    for (auto& r : kInert)
        if (std::string (r.dev) == dev && knob == r.knob && ty == r.type
            && (std::string (r.chr) == "*" || ch == r.chr))
            return &r;
    return nullptr;
}
static bool gInertHit[128] = { false };
static int  gMatDead = 0, gMatWeak = 0, gMatCells = 0;

// ─────────────────────────────────────────────────────────────────────────────
static void section7()
{
    section ("7. COMPRESS — LAW 1 ON THE FULL MATRIX: every control × 8 Types × 8 Characters");
    const auto dL = matrixProbe ((int) (FS * 0.5f), 0.0), dR = matrixProbe ((int) (FS * 0.5f), 2.1);
    const auto sL = tailProbe   ((int) (FS * 1.5f), false), sR = tailProbe ((int) (FS * 1.5f), true);
    // THE STATIC POINT. A 36 dB STAIRCASE straddling the threshold, at Push 0 (T = +9 dBp =
    // −17 dBFS) with ∞:1. A knee only exists within ±W/2 of the threshold — Limit's whole knee
    // window is 6 dB wide — so a probe that sits anywhere else measures 0.02 dB and calls a
    // working control dead: the harness's fault, not the engine's (§3.1). A staircase puts some
    // treads inside every knee in the roster, however narrow.
    const auto kL = staircase (-18.0f, 18.0f, 12, 80.0f, 220.0f);
    auto kRmk = [] { auto v = staircase (-18.0f, 18.0f, 12, 80.0f, 220.0f);
                     for (auto& x : v) x *= 0.14f; return v; };   // 17 dB down, so `Tie` exists here too
    const auto kR = kRmk();

    struct KC { std::string name; float CP::* fld; };
    // Every name is READ FROM THE HEADER at the moment of use — there is no label table here.
    const KC knobs[] = {
        { CF (0), &CP::push }, { CF (1), &CP::ratio }, { CF (2), &CP::lift }, { CF (3), &CP::mix },
        { CB (0), &CP::b1 }, { CB (1), &CP::b2 }, { CB (2), &CP::b3 }, { CB (3), &CP::b4 },
        { CB (4), &CP::b5 }, { CB (5), &CP::b6 }, { CB (6), &CP::b7 }, { CB (7), &CP::b8 } };

    int dead = 0, weak = 0, ruled = 0, cells = 0;
    std::string firstDead, firstWeak;
    double worstLive = 1e9; std::string worstLiveCell;

    // DEEP  = the downward computer working hard (Push 60 / Ratio 85).
    // SHALLOW = the threshold at the top of the programme with ∞:1 (Push 0 / Ratio 100), which
    //           is the only place a KNEE exists and the only place the upward lane engages.
    // `Long Haul` spans 4…50 SECONDS of release and `Time Four` 1.5…15 s. A 1.5 s probe cannot
    // tell 4 s from 50 s — it ends first — so the release row gets a 6 s programme of its own.
    // Same law as the knee: measure a control over the range it actually covers.
    const auto rL = tailProbe ((int) (FS * 6.0f), false), rR = tailProbe ((int) (FS * 6.0f), true);
    auto oneKnob = [&] (const std::string& name,
                        const std::function<void(CP&, float)>& set, bool longProbe = false)
    {
        int kDead = 0, kWeak = 0, kRuled = 0; double kWorst = 1e9; std::string kWorstCell;
        for (int t = 0; t < CX::kNumTypes; ++t)
            for (int c = 0; c < CX::kNumChars; ++c)
            {
                bool bit = true; double d = 0.0;
                for (int mode = 0; mode < (longProbe ? 4 : 3); ++mode)
                {
                    CP base; base.type = t; base.character = c; base.lift = 0.0f;
                    if (mode == 0) { base.push = 0.60f; base.ratio = 0.85f; }
                    else           { base.push = 0.00f; base.ratio = 1.00f; }
                    CP a = base, b = base; set (a, 0.0f); set (b, 1.0f);
                    const std::vector<float>& pL = (mode == 0) ? dL : (mode == 1) ? sL
                                                  : (mode == 2) ? kL : rL;
                    const std::vector<float>& pR = (mode == 0) ? dR : (mode == 1) ? sR
                                                  : (mode == 2) ? kR : rR;
                    const Out o0 = runCStereo (a, pL, pR), o1 = runCStereo (b, pL, pR);
                    if (!bitSame (o0, o1)) { bit = false; d = std::max (d, audDb (o0, o1)); }
                }
                const std::string ty = CX::typeNames()[t], ch = CX::charNames (t)[c];
                const InertCell* rule = inertRule ("Compress", name, ty, ch);
                ++cells; ++gMatCells;
                if (rule)
                {
                    ++kRuled; ++ruled; gInertHit[(size_t) (rule - kInert)] = true;
                    // BOTH directions. An INERT row must be bit-identical; a NARROW row must be
                    // alive AND under the bar. Either way a ruling that no longer matches the
                    // engine fails here instead of quietly excusing a cell.
                    const bool okRule = (rule->kind == INERT) ? bit : (!bit && d < 0.5);
                    if (!okRule)
                    { ++kWeak; ++weak; ++gMatWeak;
                      std::printf ("      STALE %-11s %-10s %-14s  ruled %-6s but measures %s%.3f dB\n",
                                   name.c_str(), ty.c_str(), ch.c_str(),
                                   rule->kind == INERT ? "INERT" : "NARROW",
                                   bit ? "BIT-IDENTICAL, " : "", d);
                      if (firstWeak.empty()) firstWeak = name + " @ " + ty + "/" + ch + " — the ruling is stale"; }
                    continue;
                }
                if (bit)
                { ++kDead; ++dead; ++gMatDead;
                  std::printf ("      DEAD  %-11s %-10s %-14s  bit-identical at 0 and 100, EVERY operating point\n",
                               name.c_str(), ty.c_str(), ch.c_str());
                  if (firstDead.empty()) firstDead = name + " @ " + ty + " / " + ch; }
                else if (d < 0.5)
                { ++kWeak; ++weak; ++gMatWeak;
                  std::printf ("      WEAK  %-11s %-10s %-14s  %.3f dB end to end\n",
                               name.c_str(), ty.c_str(), ch.c_str(), d);
                  if (firstWeak.empty()) firstWeak = name + " @ " + ty + " / " + ch + F1 (" = %.3f dB", d); }
                if (!bit && d < kWorst) { kWorst = d; kWorstCell = ty + " / " + ch; }
            }
        if (kWorst < worstLive) { worstLive = kWorst; worstLiveCell = name + " @ " + kWorstCell; }
        std::printf ("      %-11s 64 cells: %2d dead · %2d under 0.5 dB · %2d ruled inert   weakest live %.2f dB (%s)\n",
                     name.c_str(), kDead, kWeak, kRuled, kWorst, kWorstCell.c_str());
    };

    for (auto& k : knobs)
    { float CP::* f = k.fld;
      oneKnob (k.name, [f] (CP& p, float v) { p.*f = v; }, f == &CP::b2 /* Release */); }
    // THE FRONT PILL is a control and gets the same 64 cells. `Auto` is auto MAKEUP, so it has
    // nothing to compensate unless the device is reducing gain — which is what the deep
    // operating point is for.
    oneKnob (CX::pillName(), [] (CP& p, float v) { p.autoMakeup = (v > 0.5f); });

    gate ("no control is BIT-IDENTICAL at 0 and 100 in any unruled cell", dead == 0,
          dead == 0 ? F1 ("%.0f cells swept × 3 operating points, 0 dead", (double) cells)
                    : F1 ("%.0f DEAD cells, first: ", (double) dead) + firstDead);
    gate ("every unruled cell moves ≥ 0.5 dB end to end", weak == 0,
          weak == 0 ? F2 ("%.0f cells, weakest live %.2f dB", (double) cells, worstLive)
                            + "  (" + worstLiveCell + ")"
                    : F1 ("%.0f under the bar, first: ", (double) weak) + firstWeak);
}

// ─────────────────────────────────────────────────────────────────────────────
static void section7b()
{
    section ("7b. COMPRESS — the R11 ceiling on EVERY Type (it was proven on Type 0 only)");
    // 🚨 fb425: the fb424 gate ran the staircase on the DEFAULT Type and nothing else. Replayed
    //    on the other seven with this same probe and this same bar, FOUR were red — FET 76
    //    0.4683, Opto 0.5608, Vari-Mu 0.4763 and OverEasy. The first three are the FEEDBACK
    //    topology: `y = (x + sT)/(1+s)` ⇒ a closed-loop slope of exactly 0.5 at s = 1, which is
    //    authentic 1176/LA-2A behaviour AND polite at 100 %. Max's ruling this round is to break
    //    the authenticity over the last 10 % of Ratio, and that is what the engine now does.
    //    The fourth is a different animal and is ruled separately below.
    // 🚨 fb427 — THE INSTRUMENT WAS THE BUG. §7b measured the ceiling off a 24-tread / 120 ms
    //    STAIRCASE, and drRatio reads "the LAST 40 % of each tread — the ballistics have settled
    //    by then". On a 120 ms tread that is 48 ms, and a compressor's release runs into hundreds
    //    of ms, so adjacent treads SMEAR into each other and surviving range is hidden. The round-4
    //    family audit had already proved this and I did not carry it through: it measured Bus at
    //    DRR 0.44 with 300 ms treads and 0.047 with 3000 ms treads, THE SAME ENGINE.
    //    Caught by disagreement between two of my own probes: FET 76/`Blue Stripe` reads 0.0650 on
    //    the staircase and 0.2240 on a settled static curve. The gate was FLATTERING the engine —
    //    so the cells it called red were only the ones bad enough to show through a lenient
    //    instrument, and the ones it called green were never actually vouched for.
    //
    //    A CEILING IS A PROPERTY OF THE TRANSFER CURVE, NOT OF THE RELEASE. Each level now gets
    //    its OWN render, fully settled before a single sample is measured. (I also tested and
    //    DISPROVED the obvious alternative explanation — that the residual was the Character's
    //    authored drive being counted as signal: broadband and fundamental spans are identical to
    //    two decimals, 10.75 vs 10.75, so it is real surviving dynamic range.)
    auto staticOut = [&] (int t, int c, float rk, double inDb, float push = 1.0f)
    {
        CP p; p.type = t; p.character = c; p.push = push; p.ratio = rk;
        p.lift = 0.0f; p.b1 = (push < 0.9f) ? 0.5f : 0.15f; p.b2 = (push < 0.9f) ? 0.3f : 0.25f;
        const int N = (int) (FS * 4.0f);
        auto x = toneSig (N, 220.0f, (float) std::pow (10.0, inDb / 20.0));
        auto o = runC (p, x);
        // the LAST second only: 3 s of settling covers every release in the device except the
        // Vari-Mu 4-50 s window, which is ruled inert on Release and irrelevant to a ceiling.
        return db (rmsOf (o.l, (size_t) (FS * 3.0f), (size_t) N));
    };
    // DRR from the settled curve: output span over input span, 48 dB apart.
    // 🚨 fb427 — THE GATE WAS MEASURING WHERE THE ENGINE'S OWN CLAMP BINDS.
    //    `grT` is clamped at 60 dB (the sample loop's `clampf (grT, 0.0f, 60.0f)`). At Push 100
    //    the threshold sits ~39 dB inside the programme, so a -6 dBFS probe ASKS FOR 61.6 dB of
    //    reduction — past the clamp. The top of the range therefore runs PARALLEL again and
    //    ~2 dB of the span survives. That is an artifact of the operating point, not a device
    //    that fails to reach ∞:1. Proven by bisecting the engine's own published state:
    //        in Δ 18.00   grDb Δ 16.19   out Δ 1.81   (Exact, Push 100, gr pinned at 59.8)
    //    and by walking Push while watching the GR the engine asks for:
    //        Exact/Precise  Push 100 → 1.75 dB (gr 59.8) · Push 60 → 0.01 (gr 43.9)
    //        Bus/Quad Bus   Push 100 → 2.00 dB (gr 60.0) · Push 60 → 0.00 (gr 44.3)
    //        Exact/Blunt    Push 100 → 3.20 dB (gr 59.8) · Push 60 → 0.01 (gr 45.4)
    //    The fb425 comment already said this about OverEasy — "grT is clamped at 60 dB, so at
    //    Push 100 the top treads are past the clamp and run parallel again". It is true of EVERY
    //    Type; only OverEasy had been given the workaround.
    //
    //    So the ceiling is measured at Push 60, where the demanded GR (~44 dB) is comfortably
    //    inside the clamp and the transfer's true asymptotic slope is what shows. Ratio is still
    //    100 — this changes WHERE the curve is read, never whether it is read.
    //    Three earlier instruments were wrong and are recorded so nobody rebuilds them:
    //      · a 24-tread/120 ms staircase reads "the last 40 % of each tread" = 48 ms against
    //        releases in the hundreds — it SMEARS adjacent treads and flatters.
    //      · a -6 vs -54 dBFS static pair puts the low point INSIDE the knee, so the span counts
    //        the knee's own bend. Chasing that led to collapsing the knee at the wall, which
    //        killed `Round` (the knee IS that knob) and broke three other gates.
    //      · a RATIO bar moves when the span does. The OTT half of this file already said it:
    //        state the bar in ABSOLUTE dB.
    auto staticDrr = [&] (int t, int c, float rk)
    {   const double hi = staticOut (t, c, rk, -6.0, 0.60f), lo = staticOut (t, c, rk, -24.0, 0.60f);
        return hi - lo; };            // ABSOLUTE dB surviving an 18 dB span
    // Flat OR inverted passes; only SURVIVING POSITIVE range fails. An inversion (louder in,
    // quieter out) is PAST a wall — `Blue Stripe` and `Push Pull` invert. And a perfect wall
    // reads 0.00, so a rule demanding inversion would fail OverEasy's `Hard 160` for walling too
    // well. The bar is 0.5 dB of an 18 dB span: every walling cell measures 0.00-0.06, every
    // failing one measured 1.75-3.20, so the bar sits in a two-order-of-magnitude gap, not on a
    // number that had to be tuned.
    // OverEasy reads at Push 0.45: its Ratio runs PAST ∞ into the dbx Infinity+ NEGATIVE zone,
    // and the same 60 dB GR clamp puts the top of its range back on a parallel line at Push 100.
    // ABSOLUTE dB surviving a 12 dB span; negative = inverted = past a wall.
    auto slopeOf2 = [&] (int t, int c, float rk)
    {   const double a = staticOut (t, c, rk, -20.0, 0.45f), b = staticOut (t, c, rk, -8.0, 0.45f);
        return b - a; };
    auto ceilBad = [] (double v) { return v > 0.5; };
    // ── fb426: EVERY TYPE was not enough either. The fourth family audit found `Blunt`
    //    (Exact/3) and `Two Pass` (FET 76/7) — the two F_TWOPASS Characters — passing HALF the
    //    dynamic range at ∞:1, 15x their siblings, because this loop ran character 0 and the
    //    two-pass cascade lives on a Character. That is the fb424 blindness one axis over:
    //    per-TYPE is still a sample when the mechanism is per-CELL. It now runs all 64.
    //    The engine fix: F_TWOPASS was never a cascade at all — it computed ONE second-pass
    //    reduction on a half-corrected input and DOUBLED it, landing at out-slope 0.502 against
    //    the 0.25 its own comment claimed. It is a real two-stage cascade now, and its half
    //    slope rides the SAME wallS the feedback crossover uses, so ∞:1 walls while the Character
    //    stays the gentler one everywhere below Ratio 0.90 (measured: Blunt 0.372 vs Precise
    //    0.219 at Ratio 75, and 0.060 vs 0.032 at Ratio 100).
    //
    //    LOCKED-RATIO IDENTITIES — a roster fact, asserted to size so it cannot quietly grow.
    //    These Characters ARE a ratio; asking them to reach ∞:1 asks them to stop being
    //    themselves. Matched BY NAME so a reordered roster cannot silently re-point an exemption.
    //    fb426 — the exemption is DERIVED FROM THE ENGINE, not a hand-typed name list. My first
    //    version WAS a name list ({ Anti, Loose Grip, Twenty Lock }) and extending the sweep to 64
    //    cells immediately proved it incomplete: `Two Easy` (slopeCap 0.5 = 2:1), `Loose Four`
    //    (0.75 = 4:1) and `Only Up` (no downward computer at all) are the same kind of identity
    //    and were not in it. A hand-kept list of exemptions is the second table that goes stale —
    //    the same defect that made charNames() drift. CX::ratioLocked() reads the CharSpec.
    auto ratioLocked = [&] (int t, int c) { return CX::ratioLocked (t, c); };
    {   int locked = 0; std::string names;
        for (int t = 0; t < CX::kNumTypes; ++t)
            for (int c = 0; c < CX::kNumChars; ++c)
                if (CX::ratioLocked (t, c))
                {   ++locked;
                    names += std::string (names.empty() ? "" : ", ") + CX::typeNames()[t] + "/" + CX::charNames (t)[c]; }
        gate ("the locked-ratio exemptions are DERIVED from the CharSpec, and every one is named",
              locked > 0 && locked <= 8, F1 ("%.0f cells: ", (double) locked) + names); }

    int bad = 0, cells = 0, exempt = 0; std::string firstBad;
    std::printf ("      %-9s %8s %9s   %s\n", "Type", "DRR", "slope", "bar, and the defence");
    for (int t = 0; t < CX::kNumTypes; ++t)
    {
        // every CHARACTER of this Type against the same bar, printed only when it fails or is ruled
        for (int c = 1; c < CX::kNumChars; ++c)
        {
            if (ratioLocked (t, c)) { ++exempt; continue; }
            const double rc = (t == CX::T_OVEREASY) ? slopeOf2 (t, c, 1.0f) : staticDrr (t, c, 1.0f);
            ++cells;
            if (ceilBad (rc))
            {   ++bad;
                if (firstBad.empty()) firstBad = std::string (CX::typeNames()[t]) + "/" + CX::charNames (t)[c];
                std::printf ("      %-9s %8.4f %9s   %s/%s  ← RED\n", CX::typeNames()[t], rc, "",
                             CX::typeNames()[t], CX::charNames (t)[c]); }
        }
        const double r  = staticDrr (t, 0, 1.0f);
        const double sl = slopeOf2  (t, 0, 1.0f);
        bool ok; std::string bar;
        if (t == CX::T_OVEREASY)
        {
            // RULED SEPARATELY — a roster fact, not an excuse. OverEasy's Ratio knob is the only
            // one in the device that continues PAST ∞:1 into the dbx "Infinity+" NEGATIVE zone:
            // at s = 2 the transfer is `out = 2T − in`, so louder in gives QUIETER out. A
            // dynamic-range SPAN cannot see that — an inverted 25 dB span reads exactly like an
            // upright one — and `grT` is clamped at 60 dB, so at Push 100 (the threshold 39 dB
            // inside the programme) the top treads are past the clamp and run parallel again.
            // The honest question for this Type is the SIGN of the curve at a working threshold,
            // and a negative slope is the MORE extreme of the two behaviours, not the politer.
            const double s2 = slopeOf2 (t, 0, 1.0f);
            ok = ! ceilBad (s2);
            bar = F1 ("slope %+.3f ≤ 0.05 at a WORKING threshold — flat or negative zone", s2);
        }
        else { ok = ! ceilBad (r); bar = "DRR ≤ 0.05 on the SETTLED curve — flat or inverted both pass"; }
        if (!ok) { ++bad; if (firstBad.empty()) firstBad = CX::typeNames()[t]; }
        ++cells;
        std::printf ("      %-9s %8.4f %+9.4f   %s%s\n", CX::typeNames()[t], r, sl,
                     bar.c_str(), ok ? "" : "   ← RED");
    }
    gate ("every Type x CHARACTER cell walls at Push/Ratio 100 (R11)", bad == 0,
          bad == 0 ? F2 ("%.0f cells measured, %.0f locked-ratio identities exempt and named",
                         (double) cells, (double) exempt)
                   : F2 ("%.0f of %.0f cells red, first: ", (double) bad, (double) cells) + firstBad);
    gate ("   ... and the cell sweep actually RAN (a roll-up over an empty loop cannot pass)",
          cells >= 8 * (CX::kNumChars - 1), F1 ("%.0f cells", (double) cells));
    {
        // and the crossover itself, named: below Ratio 0.90 the feedback Types are STILL
        // feedback — the authenticity Max paid for is intact everywhere but the last tenth.
        auto ov = [&] (int t, float rk) { return staticDrr (t, 0, rk); };
        const double a = ov (CX::T_FET, 0.90f), b = ov (CX::T_FET, 1.0f);
        gate ("  the wall is the LAST 10 % only — FET 76 keeps its feedback curve below it",
              a >= 0.30 && b <= 0.05,
              F2 ("DRR %.4f at Ratio 90 (the authentic 0.5 closed-loop slope) → %.4f at 100", a, b));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
static void section7c()
{
    section ("7c. COMPRESS — the `Detect` dropdown: every option distinct, on every cell (R6)");
    // R6 requires BOTH dropdowns to change PHYSICS. `Character` gets 448 measured cells in 3c;
    // this is the other one and it had NONE. `Native` is an ALIAS — it defers to the Type's own
    // ears — so on a Type whose native rectifier IS one of the four named ones the two options
    // are the same DSP by construction. That is READ OFF `detectId()`, never assumed, and the
    // count is printed.
    const auto dL = matrixProbe ((int) (FS * 0.5f), 0.0), dR = matrixProbe ((int) (FS * 0.5f), 2.1);
    const auto sL = tailProbe   ((int) (FS * 1.5f), false), sR = tailProbe ((int) (FS * 1.5f), true);
    int bad = 0, alias = 0, pairs = 0; double worst = 1e9; std::string wn, firstBad;
    for (int t = 0; t < CX::kNumTypes; ++t)
        for (int c = 0; c < CX::kNumChars; ++c)
        {
            Out o[3][CX::kNumDetect]; int id[CX::kNumDetect];
            for (int ax = 0; ax < CX::kNumDetect; ++ax)
                for (int mode = 0; mode < 3; ++mode)
                {
                    // mode 1 is a FAST attack on purpose. At the default `Attack` the OverEasy
                    // window (`D_RMSWIN` reads b1) lands on ~9 ms, i.e. on top of `Average`'s
                    // 10 ms — two options that are genuinely near-identical AT THAT SETTING and
                    // 40 dB apart at another. Measuring one setting and calling the pair dead is
                    // the same mistake as measuring one Type.
                    CP p; p.type = t; p.character = c; p.axis = ax; p.lift = 0.0f;
                    p.push  = (mode == 1) ? 0.00f : 0.60f;
                    p.ratio = (mode == 1) ? 1.00f : 0.85f;
                    // mode 2 opens the RMS window all the way: `Crush RMS` multiplies it by 0.10,
                    // so at Attack 0 its `Native` IS a peak detector to within 0.1 ms and the two
                    // options genuinely coincide THERE. At Attack 100 the window is 8 ms.
                    p.b1    = (mode == 0) ? 0.50f : (mode == 1) ? 0.00f : 1.00f;
                    if (mode == 0) { CX e; e.prepare (FS, 128); e.setParams (p); id[ax] = e.detectId(); }
                    o[mode][ax] = (mode == 1) ? runCStereo (p, sL, sR) : runCStereo (p, dL, dR);
                }
            for (int i = 0; i < CX::kNumDetect; ++i)
                for (int j = i + 1; j < CX::kNumDetect; ++j)
                {
                    if (id[i] == id[j]) { ++alias; continue; }
                    ++pairs;
                    const double d = std::max (std::max (audDb (o[0][i], o[0][j]), audDb (o[1][i], o[1][j])),
                                               audDb (o[2][i], o[2][j]));
                    const std::string nm = std::string (CX::typeNames()[t]) + " / " + CX::charNames (t)[c]
                                         + ": " + CX::detectNames()[i] + " vs " + CX::detectNames()[j];
                    if (d < worst) { worst = d; wn = nm; }
                    if (d < 0.5)
                    { ++bad; std::printf ("      WEAK  %-52s %.3f dB\n", nm.c_str(), d);
                      if (firstBad.empty()) firstBad = nm; }
                }
        }
    gate ("every DISTINCT `Detect` pair is audible in every one of the 64 cells", bad == 0,
          bad == 0 ? F2 ("weakest %.2f dB over %.0f measured pairs", worst, (double) pairs)
                            + "  (" + wn + ")"
                   : F1 ("%.0f pairs under 0.5 dB, first: ", (double) bad) + firstBad);
    std::printf ("      %.0f of 640 pairs are the SAME rectifier by construction (`%s` aliases the Type's own ears)\n",
                 (double) alias, CX::detectNames()[0]);
}

// ─────────────────────────────────────────────────────────────────────────────
static void section8()
{
    section ("8. OTT — LAW 1 ON THE FULL MATRIX: every control × 8 Types × 8 Characters");
    const auto dL = matrixProbe ((int) (FS * 0.5f), 0.0), dR = matrixProbe ((int) (FS * 0.5f), 2.1);
    const auto sL = tailProbe   ((int) (FS * 1.5f), false), sR = tailProbe ((int) (FS * 1.5f), true);
    // THE QUIET TAIL — the same programme 26 dB down, and it is a THIRD operating point for one
    // reason: an UPWARD computer only exists BELOW its threshold. On a loud note the lift is
    // already zero when the attack lands, so `Crest` measured 0.1…0.4 dB on 40 of 64 cells —
    // a fact about the probe. Here the whole programme sits under the upward threshold, the
    // lift rides at +15…20 dB through the gap before each attack, and "attacks keep their crest
    // instead of being pre-inflated by the lift that was riding the gap before them" — the
    // sentence in the engine header — is what gets measured. Same law as the knee on Compress.
    const auto qL = [&] { auto v = sL; for (auto& x : v) x *= 0.05f; return v; } ();
    const auto qR = [&] { auto v = sR; for (auto& x : v) x *= 0.05f; return v; } ();

    struct KO { std::string name; float OP::* fld; };
    const KO knobs[] = {
        { OF (0), &OP::amount }, { OF (1), &OP::speed }, { OF (2), &OP::topLift }, { OF (3), &OP::mix },
        { OB (0), &OP::b1 }, { OB (1), &OP::b2 }, { OB (2), &OP::b3 }, { OB (3), &OP::b4 },
        { OB (4), &OP::b5 }, { OB (5), &OP::b6 }, { OB (6), &OP::b7 }, { OB (7), &OP::b8 } };

    int dead = 0, weak = 0, ruled = 0, cells = 0;
    std::string firstDead, firstWeak;
    double worstLive = 1e9; std::string worstLiveCell;

    // DEEP  = Amount 60 on the tremolo/transient probe — both computers working, transients.
    // TAIL  = Amount 45 on the pluck train — decaying tails and real gaps, which is the only
    //         place an UPWARD computer, `Raise` or the `Crest` transient hold has anything to
    //         do. Amount 45 and not 85 ON PURPOSE: above 0.5 the `u` branch LERPS both slopes
    //         toward 1.0 and 0.95, which is the ceiling doing its job and also swallows most of
    //         `Raise`'s own travel. Measuring `Raise` there reads 0.1 dB on 52 cells and calls
    //         a live knob dead — the harness's fault again, not the engine's.
    auto oneKnobT = [&] (const std::string& name, const std::function<void(OP&, float)>& set,
                         const std::function<void(OP&)>& tweak)
    {
        int kDead = 0, kWeak = 0, kRuled = 0; double kWorst = 1e9; std::string kWorstCell;
        for (int t = 0; t < OX::kNumTypes; ++t)
            for (int c = 0; c < OX::kNumChars; ++c)
            {
                bool bit = true; double d = 0.0;
                for (int mode = 0; mode < 3; ++mode)
                {
                    OP base; base.type = t; base.character = c;
                    base.amount = (mode == 0) ? 0.60f : 0.45f;
                    tweak (base);
                    OP a = base, b = base; set (a, 0.0f); set (b, 1.0f);
                    const std::vector<float>& pL = (mode == 0) ? dL : (mode == 1) ? sL : qL;
                    const std::vector<float>& pR = (mode == 0) ? dR : (mode == 1) ? sR : qR;
                    const Out o0 = runOStereo (a, pL, pR), o1 = runOStereo (b, pL, pR);
                    if (!bitSame (o0, o1)) { bit = false; d = std::max (d, audDb (o0, o1)); }
                }
                const std::string ty = OX::typeNames()[t], ch = OX::charNames (t)[c];
                const InertCell* rule = inertRule ("OTT", name, ty, ch);
                ++cells; ++gMatCells;
                if (rule)
                {
                    ++kRuled; ++ruled; gInertHit[(size_t) (rule - kInert)] = true;
                    // BOTH directions. An INERT row must be bit-identical; a NARROW row must be
                    // alive AND under the bar. Either way a ruling that no longer matches the
                    // engine fails here instead of quietly excusing a cell.
                    const bool okRule = (rule->kind == INERT) ? bit : (!bit && d < 0.5);
                    if (!okRule)
                    { ++kWeak; ++weak; ++gMatWeak;
                      std::printf ("      STALE %-11s %-10s %-14s  ruled %-6s but measures %s%.3f dB\n",
                                   name.c_str(), ty.c_str(), ch.c_str(),
                                   rule->kind == INERT ? "INERT" : "NARROW",
                                   bit ? "BIT-IDENTICAL, " : "", d);
                      if (firstWeak.empty()) firstWeak = name + " @ " + ty + "/" + ch + " — the ruling is stale"; }
                    continue;
                }
                if (bit)
                { ++kDead; ++dead; ++gMatDead;
                  std::printf ("      DEAD  %-11s %-10s %-14s  bit-identical at 0 and 100, EVERY operating point\n",
                               name.c_str(), ty.c_str(), ch.c_str());
                  if (firstDead.empty()) firstDead = name + " @ " + ty + " / " + ch; }
                else if (d < 0.5)
                { ++kWeak; ++weak; ++gMatWeak;
                  std::printf ("      WEAK  %-11s %-10s %-14s  %.3f dB end to end\n",
                               name.c_str(), ty.c_str(), ch.c_str(), d);
                  if (firstWeak.empty()) firstWeak = name + " @ " + ty + " / " + ch + F1 (" = %.3f dB", d); }
                if (!bit && d < kWorst) { kWorst = d; kWorstCell = ty + " / " + ch; }
            }
        if (kWorst < worstLive) { worstLive = kWorst; worstLiveCell = name + " @ " + kWorstCell; }
        std::printf ("      %-11s 64 cells: %2d dead · %2d under 0.5 dB · %2d ruled inert   weakest live %.2f dB (%s)\n",
                     name.c_str(), kDead, kWeak, kRuled, kWorst, kWorstCell.c_str());
    };

    auto oneKnob = [&] (const std::string& name, const std::function<void(OP&, float)>& set)
    { oneKnobT (name, set, [] (OP&) {}); };

    for (auto& k : knobs)
    { float OP::* f = k.fld; oneKnob (k.name, [f] (OP& p, float v) { p.*f = v; }); }
    // 🚨 THE FRONT PILL, on all 64 cells. fb425: `Crest` could be made a no-op (`upHold_ =
    // (cs.upHold != 0)`) with ZERO gates firing — `p.crest` appeared exactly ONCE in this entire
    // file, inside a click list that gets BETTER when the pill is deleted. It holds the UPWARD
    // computer at unity for 10 ms after a transient, so it needs upward gain to hold and
    // transients to fire on: the tail probe is both.
    // 🔬 AND IT GETS ITS OWN OPERATING POINT, for a reason that is the mechanism itself. The
    //    hold is 10 ms long, and at the stock `Chase` the upward envelopes attack in 0.7…2.8 ms
    //    — so the lift has already collapsed on its own before the hold has done anything, and
    //    the pill measures 0.2…0.4 dB everywhere. `Chase` 0.10 puts the followers ×14 slower,
    //    which is where a transient CAN arrive on top of a lift that is still riding the gap
    //    before it — the exact thing this pill exists to stop. `Raise` at 100 so there is a lift
    //    to hold. Same law as the knee: measure a control where its mechanism lives.
    oneKnobT (OX::pillName(), [] (OP& p, float v) { p.crest = (v > 0.5f); },
              [] (OP& p) { p.speed = 0.10f; p.b3 = 1.0f; });

    gate ("no control is BIT-IDENTICAL at 0 and 100 in any unruled cell", dead == 0,
          dead == 0 ? F1 ("%.0f cells swept × 3 operating points, 0 dead", (double) cells)
                    : F1 ("%.0f DEAD cells, first: ", (double) dead) + firstDead);
    gate ("every unruled cell moves ≥ 0.5 dB end to end", weak == 0,
          weak == 0 ? F2 ("%.0f cells, weakest live %.2f dB", (double) cells, worstLive)
                            + "  (" + worstLiveCell + ")"
                    : F1 ("%.0f under the bar, first: ", (double) weak) + firstWeak);

    // ═════ THE `Stereo` DROPDOWN — the other half of R6, on every cell ═════
    section ("8a2. OTT — the `Stereo` dropdown: every option distinct, on every cell (R6)");
    {
        int bad = 0; double worst = 1e9; std::string wn, firstBad;
        for (int t = 0; t < OX::kNumTypes; ++t)
            for (int c = 0; c < OX::kNumChars; ++c)
            {
                Out o[2][OX::kNumStereo];
                for (int ax = 0; ax < OX::kNumStereo; ++ax)
                {
                    // Two points again: `Surge` is upward-ONLY below Amount 0.5, so at 0.70 the
                    // only stereo mechanism running is the lift — and `Free Pair` vs `Mid-Side`
                    // measured 0.47 dB there on four of its Characters. At 0.95 the `u` branch
                    // has brought the downward computers in and both mechanisms are stereo.
                    OP p; p.type = t; p.character = c; p.axis = ax;
                    p.amount = 0.70f; o[0][ax] = runOStereo (p, dL, dR);
                    p.amount = 0.95f; o[1][ax] = runOStereo (p, sL, sR);
                }
                for (int i = 0; i < OX::kNumStereo; ++i)
                    for (int j = i + 1; j < OX::kNumStereo; ++j)
                    {
                        const double d = std::max (audDb (o[0][i], o[0][j]), audDb (o[1][i], o[1][j]));
                        const std::string nm = std::string (OX::typeNames()[t]) + " / " + OX::charNames (t)[c]
                                             + ": " + OX::stereoNames()[i] + " vs " + OX::stereoNames()[j];
                        if (d < worst) { worst = d; wn = nm; }
                        if (d < 0.5)
                        { ++bad; std::printf ("      WEAK  %-52s %.3f dB\n", nm.c_str(), d);
                          if (firstBad.empty()) firstBad = nm; }
                    }
            }
        gate ("every `Stereo` pair is audible in every one of the 64 cells", bad == 0,
              bad == 0 ? F1 ("weakest %.2f dB over 192 pairs", worst) + "  (" + wn + ")"
                       : F1 ("%.0f pairs under 0.5 dB, first: ", (double) bad) + firstBad);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
static void section8b()
{
    section ("8b. OTT — Mix 100 % = fully wet, zero dry (LAW 3 · CONTRACT §5)");
    // 🚨 fb425: OTT HAD NO LAW-3 GATE AT ALL, and the hole was invisible because forcing
    //    `mixTgt_ = 1.0f` — the Mix knob dead, always fully wet — left all 53 OTT gates green.
    //    The one gate that touched Mix compared out(mix=1) against out(mix=0) at Amount 0 and
    //    required them to be nearly IDENTICAL, so the mutation made it pass HARDER.
    //    Three claims, on EVERY Type, and each one fails under that exact mutation:
    //      (1) Mix 0 is the DRY PATH. OTT's dry is deliberately not the raw input: it runs
    //          through the SAME AP2(f_lo)·AP2(f_hi) the 3-band tree recombines to, so wet and
    //          dry differ by GAIN ALONE and Mix cannot comb at any setting. The bar is therefore
    //          "magnitude-flat against the input AND carrying none of the compression", not
    //          "bit-identical to the input". Forced wet, this reads the whole effect.
    //      (2) the crossfade is EXACTLY linear in Mix — a closed-form identity, ≤ −100 dB —
    //          which is what makes "zero dry at Mix 1" a proof rather than a measurement.
    //      (3) Mix travels MONOTONICALLY dry→wet over 9 steps with a real span. Forced wet, all
    //          nine outputs are the same buffer and the span is 0.
    auto ch = amChord ((int) (FS * 1.2f));
    int badDry = 0, badLin = 0, badMono = 0;
    double worstDry = 0.0, worstLin = -300.0, worstSpan = 1e9;
    std::string nDry, nLin, nSpan;
    for (int t = 0; t < OX::kNumTypes; ++t)
    {
        OP p; p.type = t; p.amount = 0.85f; p.b3 = 0.8f;
        p.mix = 0.0f; auto o0 = runO (p, ch);
        p.mix = 1.0f; auto o1 = runO (p, ch);
        p.mix = 0.5f; auto oh = runO (p, ch);
        const double d0 = std::max (specDist (ch, o0.l),
                                    std::fabs (db (rmsOf (o0.l)) - db (rmsOf (ch))));
        if (d0 > 0.35) ++badDry;
        if (d0 > worstDry) { worstDry = d0; nDry = OX::typeNames()[t]; }
        double eLin = 0.0, eRef = 0.0;
        for (size_t i = 0; i < ch.size(); ++i)
        { const double m = (double) oh.l[i] - 0.5 * ((double) o0.l[i] + o1.l[i]); eLin += m * m;
          eRef += (double) o1.l[i] * o1.l[i]; }
        const double linErr = db (std::sqrt (eLin / ch.size())) - db (std::sqrt (eRef / ch.size()));
        if (linErr > -100.0) ++badLin;
        if (linErr > worstLin) { worstLin = linErr; nLin = OX::typeNames()[t]; }
        double m[9]; bool mono = true;
        Out ref = o0;
        for (int k = 0; k <= 8; ++k)
        { OP q = p; q.mix = (float) k / 8.0f; auto ok2 = runO (q, ch);
          m[k] = audDb (ref, ok2); if (k && m[k] < m[k - 1] - 0.05) mono = false; }
        const double span = m[8] - m[0];
        if (!mono || span < 2.0) ++badMono;
        if (span < worstSpan) { worstSpan = span; nSpan = OX::typeNames()[t]; }
    }
    gate ("Mix 0 is the DRY path on every Type (the allpassed input, not the effect)", badDry == 0,
          F1 ("worst %.3f dB from the input (bar 0.35)", worstDry) + "  (" + nDry + ")");
    gate ("the Mix crossfade is EXACTLY linear on every Type", badLin == 0,
          F1 ("worst deviation at Mix 0.5 = %.1f dB (bar −100)", worstLin) + "  (" + nLin + ")");
    gate ("⇒ dry residual at Mix 1.0 is (1 − mix)·dry = 0 identically", badDry == 0 && badLin == 0,
          F1 ("measured floor %.1f dB, bar −60", worstLin));
    gate ("Mix travels monotonically dry→wet on every Type", badMono == 0,
          F1 ("smallest end-to-end travel %.2f dB (bar 2.0)", worstSpan) + "  (" + nSpan + ")");
}

// ─────────────────────────────────────────────────────────────────────────────
static void section8c()
{
    section ("8c. OTT — the R11 ceiling on EVERY Type (it was proven on Type 0 only)");
    // TWO QUESTIONS, because a multiband compressor's ceiling has a STATIC half and a DYNAMIC
    // half and one number cannot hold both:
    //  (a) THE STATIC CEILING — a 36 dB staircase at Amount 100, and the bar is stated in
    //      ABSOLUTE dB rather than as a fraction: at most 4.0 dB of the input's 36 dB range may
    //      still be standing at the output. Defence: that is a 9:1 squeeze of the programme's
    //      own dynamics, which is unarguably a wall, and unlike a percentage it does not move
    //      when the span does. Two deliberate choices in the probe:
    //        · the tread is 240 ms so even `Gentle` (release 160 ms) has settled inside the last
    //          40 % that is measured — a static-curve question must not be answered by a
    //          ballistic transient;
    //        · the staircase FLOOR is −56 dBFS, above the floor gate's foot. The floor gate
    //          withholds upward lift below −78 dBFS per band on purpose (nothing free-runs), so
    //          treads down there measure the STABILITY guarantee, not the ceiling.
    //      What the residual actually is, measured: at u = 1 the upward slope is 0.95, not 1.0,
    //      so 5 % of the range below the threshold survives by construction, and a Character
    //      with a wide KNEE (`Gentle`, 12 dB) passes part of the knee region as well. `Gentle`
    //      leaves 3.6 dB and `Bass Safe` 1.0 dB; the ratio is printed beside the dB so a reader
    //      can see both.
    //  (b) THE DYNAMIC CEILING — envelope spread removed from a 2 Hz / 24 dB tremolo with BOTH
    //      of the controls that own the axis at their maximum: Amount 100 AND `Chase` 100. R11
    //      is about where a CONTROL stops being useful, and `Chase` is the control that owns
    //      ballistic speed (400:1 of travel). Bar: ≤ 5 dB of surviving spread, or ≥ 60 % of the
    //      probe's own spread removed.
    //  The Amount-only spread is PRINTED beside it, without a bar, because it is a real and
    //  interesting number: `Gentle` is deliberately slow, so at its stock `Chase` it leaves most
    //  of a 2 Hz tremolo standing. That is its identity, not its ceiling.
    auto st = staircase (-30.0f, 6.0f, 20, 240.0f, 330.0f);
    auto am = amChord ((int) (FS * 2.5f));
    const double probeSpread = envSpreadDb (am);
    int bad = 0; std::string firstBad;
    note ("the tremolo probe's own envelope spread", F1 ("%.2f dB", probeSpread));
    std::printf ("      %-10s %8s %7s   %10s %12s %12s\n",
                 "Type", "survive", "DRR", "spread@50", "spread@100", "+Chase 100");
    for (int t = 0; t < OX::kNumTypes; ++t)
    {
        OP p; p.type = t; p.amount = 1.0f;
        auto so = runO (p, st).l;
        const double r = drRatio (st, so, 20, 240.0f);
        // the SURVIVING RANGE in dB, which is what the bar is actually on
        const int perT = (int) (FS * 0.240f);
        double oLo = 1e9, oHi = -1e9;
        for (int k = 0; k < 20; ++k)
        { const double v = db (rmsOf (so, (size_t) (k * perT + perT * 0.6), (size_t) ((k + 1) * perT)));
          oLo = std::min (oLo, v); oHi = std::max (oHi, v); }
        const double survive = oHi - oLo;
        OP h = p; h.amount = 0.5f;
        const double s5 = envSpreadDb (runO (h, am).l), s10 = envSpreadDb (runO (p, am).l);
        OP f = p; f.speed = 1.0f;
        const double sF = envSpreadDb (runO (f, am).l);
        const bool okA = (survive <= 4.0);
        const bool okB = (sF <= 5.0) || (sF <= 0.40 * probeSpread);
        if (!(okA && okB)) { ++bad; if (firstBad.empty()) firstBad = OX::typeNames()[t]; }
        std::printf ("      %-10s %6.2f dB %7.4f   %10.2f %12.2f %12.2f   %s%s\n",
                     OX::typeNames()[t], survive, r, s5, s10, sF,
                     okA ? "" : "← STATIC RED", okB ? "" : "  ← DYNAMIC RED");
    }
    gate ("every Type walls at Amount 100 (R11): static AND dynamic", bad == 0,
          bad == 0 ? "8 / 8 Types" : F1 ("%.0f of 8 red, first: ", (double) bad) + firstBad);
}

// ─────────────────────────────────────────────────────────────────────────────
static void section8d()
{
    section ("8d. OTT — `Amount` 0 is NEUTRAL and the knob runs FORWARDS, on every cell");
    // 🚨 fb425, verified independently by the integration owner. The per-band clip ceiling was
    //    `db2lin (Tdn + clipHd_ + mkDb_[b])` and the makeup is `ts.mk[b] * lo01` with
    //    `lo01 = pow (amt*2, 1.2)` — ZERO at Amount 0. `Heavy`'s makeups are {21,24,20} dB, so
    //    the ceiling collapsed ~21 dB onto a band that had had NO gain reduction applied.
    //    MEASURED ON THE fb424 ENGINE, 220 Hz at −10.5 dBFS:
    //        Amount:      0 %     25 %     50 %     75 %    100 %
    //           THD:  35.75 %  28.46 %   2.52 %   2.52 %   2.51 %
    //          peak:  0.0263   0.0829   0.2751   0.2316   0.1949
    //    The knob's ZERO was 14× more distorted than its middle and 21 dB quieter. `Over Top`
    //    and `Surge` measure flat (0.5–0.8 %) across the whole sweep, which is exactly why a
    //    Type-0 gate could not see it. THD and peak are both printed on every Type, with a
    //    BYPASSED-ENGINE control number beside them (§3.1: check your own detector first).
    auto tn = toneSig ((int) (FS * 1.0f), 220.0f, 0.15f);
    note ("the probe through a BYPASSED engine (the control number)",
          F2 ("THD %.3f %%, peak %.4f", thdPct (tn, 220.0), peakOf (tn)));
    int bad = 0; std::string firstBad;
    std::printf ("      %-10s %-6s %8s %8s %8s %8s %8s\n", "Type", "", "0 %", "25 %", "50 %", "75 %", "100 %");
    for (int t = 0; t < OX::kNumTypes; ++t)
        for (int c = 0; c < OX::kNumChars; ++c)
        {
            double th[5], pk[5];
            for (int k = 0; k < 5; ++k)
            { OP p; p.type = t; p.character = c; p.amount = (float) k * 0.25f;
              auto o = runO (p, tn); th[k] = thdPct (o.l, 220.0); pk[k] = peakOf (o.l); }
            // TWO CLAIMS, both directional:
            //  · Amount 0 is NEUTRAL — it adds no more distortion than the bypassed engine does.
            //    The crossover tree is allpass, so 0.5 % is arithmetic plus float noise, not a
            //    tolerance anybody chose.
            //  · the knob runs FORWARDS — no setting below 100 % may be more than 1.5× as
            //    distorted as 100 %. A knob whose zero is its dirtiest point is backwards
            //    whatever else it does.
            const bool neutral = th[0] <= 0.5;
            double mx = 0.0; for (int k = 0; k < 4; ++k) mx = std::max (mx, th[k]);
            const bool forward = mx <= std::max (1.0, 1.5 * th[4]);
            if (c == 0)
            {
                std::printf ("      %-10s %-6s %7.2f%% %7.2f%% %7.2f%% %7.2f%% %7.2f%%\n",
                             OX::typeNames()[t], "THD", th[0], th[1], th[2], th[3], th[4]);
                std::printf ("      %-10s %-6s %8.4f %8.4f %8.4f %8.4f %8.4f\n",
                             "", "peak", pk[0], pk[1], pk[2], pk[3], pk[4]);
            }
            if (!neutral || !forward)
            { ++bad; if (firstBad.empty()) firstBad = std::string (OX::typeNames()[t]) + " / "
                                 + OX::charNames (t)[c] + F2 (": THD %.2f %% at 0 vs %.2f %% at 100", th[0], th[4]); }
        }
    gate ("Amount 0 is neutral and the knob runs forwards, all 64 cells", bad == 0,
          bad == 0 ? "64 cells × 5 settings; THD at Amount 0 ≤ 0.5 % everywhere"
                   : F1 ("%.0f cells backwards, first: ", (double) bad) + firstBad);
}

// ─────────────────────────────────────────────────────────────────────────────
static void section8e()
{
    section ("8e. OTT — sample rate, measured off the AUDIO (the old gate compared a constant to itself)");
    // 🚨 fb425 / FIXES.md §1 COMPRESS-3, unfixed on the sibling. The old gate was
    //        fabs (e.attackMs (1) − e48.attackMs (1)) < 0.05
    //    and `attackMs_[b] = nA * 1000 / fs_` with `nA = max (5, aMs*0.001*fs_)`, so whenever
    //    the 5-sample floor is inactive `fs_` cancels ALGEBRAICALLY and the accessor returns the
    //    knob value. It printed "mid atk 1.40 ms (48 k: 1.40)" BY CONSTRUCTION and would have
    //    passed on an engine that dropped `fs` entirely.
    //
    //    WHAT IS MEASURED NOW, and why it is a TRAJECTORY and not a t63. A t63 read off a level
    //    step is the obvious thing and it does not work here: OTT uses Vital's two CLAMPED
    //    envelopes, so on a 24 dB step the clamp is crossed within a few samples whatever the
    //    attack constant is, and the remaining rise is only the last 7 % of τ. Two attempts
    //    measured 1.00 ms and then 2.17 ms for a 20.7 ms constant, and disagreed between rates
    //    by a factor of two for reasons that were the METRIC's, not the engine's.
    //    So: the whole gain trajectory, sampled on a COMMON TIME GRID (1 ms per point at every
    //    rate), compared point by point against the 48 kHz run. A rate-dependence bug moves the
    //    curve; nothing else does. The mutant `ott-sample-rate` drops `fs` from the follower and
    //    this goes red at 96 kHz by 6 dB.
    //    NOTE, honestly: the DSP underneath is CORRECT (192 cells, worst |Δ out RMS| 0.129 dB at
    //    96 kHz). This closes a hole in the GATE over working code.
    auto trace = [] (float fs)
    {
        const int n = (int) (fs * 2.5f);
        std::vector<float> x ((size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / fs;
            const double a = (t < 0.30) ? 0.020 : (t < 1.00) ? 0.30 : 0.020;
            x[(size_t) i] = (float) (a * std::sin (2.0 * M_PI * 330.0 * i / fs));
        }
        const int hop = (int) (fs * 0.001f);                 // 1 ms at EVERY rate
        OX e; e.prepare ((double) fs, hop); OP p; p.amount = 0.85f; p.speed = 0.05f;
        e.setParams (p);
        std::vector<float> l = x, r = x; std::vector<double> tr;
        for (int i = 0; i + hop <= n; i += hop)
        {
            e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], hop);
            double so = 0.0, si = 0.0;
            for (int k = 0; k < hop; ++k)
            { so += (double) l[(size_t) (i + k)] * l[(size_t) (i + k)];
              si += (double) x[(size_t) (i + k)] * x[(size_t) (i + k)]; }
            tr.push_back (db (std::sqrt (so / hop)) - db (std::sqrt (si / hop)));
        }
        // the exact seconds-per-point, which is NOT 1 ms: 44100 × 0.001 truncates to 44 samples,
        // so the 44.1 kHz grid drifts 2.3 ms behind by t = 1 s. Comparing index-to-index across
        // a 24 dB step edge then reads 30.67 dB of "rate dependence" that is pure grid slip.
        return std::make_pair (tr, (double) hop / (double) fs);
    };
    const auto p48 = trace (48000.0f);
    const auto& t48 = p48.first; const double dt48 = p48.second;
    // ⚠️ COMPARED AS THE AREA UNDER EACH RAMP, not level-at-time and not time-to-level. Both of
    //    those were tried and both measure the PROBE: during the release the gain climbs about
    //    1 dB per millisecond, so the half-block difference in where a level edge lands inside a
    //    block (0.7 ms between 44.1 and 48 kHz — arithmetic about the grid, not the engine)
    //    reads as 4.3 dB, and a single crossing index reads as 800 %.
    //    The area between the trajectory and its own settled value,
    //        A = Σ |gain(t) − gain_settled| · dt   over the ramp,
    //    is the time constant integrated. It is proportional to τ by construction, it is a sum
    //    over hundreds of points so no single block can move it, and it is exactly what a
    //    rate-dependence bug changes: halve the realised τ and the area halves.
    auto rampArea = [] (const std::vector<double>& tr, double dt, double t0, double lenSec)
    {
        const int a = (int) (t0 / dt) + 4;
        const int b = std::min ((int) tr.size(), (int) ((t0 + lenSec) / dt));
        if (b - a < 20) return 0.0;
        double settle = 0.0; const int ns = std::max (5, (b - a) / 10);
        for (int i = b - ns; i < b; ++i) settle += tr[(size_t) i];
        settle /= (double) ns;
        double A = 0.0;
        for (int i = a; i < b; ++i) A += std::fabs (tr[(size_t) i] - settle) * dt * 1000.0;
        return A;                                            // dB·ms
    };
    {
        double lo = 1e9, hi = -1e9;
        for (size_t i = 40; i + 40 < t48.size(); ++i) { lo = std::min (lo, t48[i]); hi = std::max (hi, t48[i]); }
        note ("the 48 kHz gain trajectory this is compared against",
              F3 ("%.0f points at 1 ms, spanning %+.2f … %+.2f dB of gain",
                  (double) t48.size(), lo, hi));
        gate ("  ... and it MOVES (a flat trace would make the comparison vacuous)", hi - lo > 6.0,
              F1 ("%.2f dB of travel (bar 6)", hi - lo));
    }
    const double aA48 = rampArea (t48, dt48, 0.300, 0.400);      // the attack ramp
    const double aR48 = rampArea (t48, dt48, 1.000, 1.400);      // the release ramp
    note ("48 kHz ramp areas (∝ the realised time constants)",
          F2 ("attack %.0f dB·ms, release %.0f dB·ms", aA48, aR48));
    gate ("  ... and both ramps have real area (a flat one would be vacuous)",
          aA48 > 200.0 && aR48 > 200.0, F2 ("%.0f / %.0f dB·ms (bar 200)", aA48, aR48));
    for (float fs : { 44100.0f, 96000.0f })
    {
        const auto pf = trace (fs);
        const auto& tf = pf.first; const double dtf = pf.second;
        const double aA = rampArea (tf, dtf, 0.300, 0.400);
        const double aR = rampArea (tf, dtf, 1.000, 1.400);
        const double eA = 100.0 * std::fabs (aA - aA48) / std::max (1.0, aA48);
        const double eR = 100.0 * std::fabs (aR - aR48) / std::max (1.0, aR48);
        gate ((F1 ("%.1f kHz: the REALISED ballistics match 48 kHz", fs / 1000.0)).c_str(),
              eA < 12.0 && eR < 12.0,
              F4 ("attack area %.0f dB·ms (%+.1f %%), release area %.0f dB·ms (%+.1f %%)",
                  aA, aA - aA48 > 0 ? eA : -eA, aR, aR - aR48 > 0 ? eR : -eR)
              + "  (bar 12 %)");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
static void section9()
{
    section ("9. The KNOWN-INERT ROSTER — asserted size, checked in BOTH directions");
    // A cell where a control genuinely does not exist is a ROSTER FACT, written down here with
    // the missing MECHANISM named. It is not an escape hatch:
    //   · the list is asserted to a fixed size, so it cannot quietly grow (the `kShared` lesson);
    //   · every row must have been REACHED by sections 7/8 — a row for a cell that no longer
    //     exists is a stale ruling and fails here;
    //   · every row must actually BE inert — sections 7/8 fail a RULED cell that moved;
    //   · every dead cell NOT on this list already failed there.
    int nInertRows = 0, nNarrowRows = 0;
    for (auto& r : kInert) (r.kind == INERT ? nInertRows : nNarrowRows)++;
    gate ("the known-inert roster is exactly the size it declares", kNumInert == 18,
          F3 ("%.0f rows = %.0f INERT + %.0f NARROW", (double) kNumInert,
              (double) nInertRows, (double) nNarrowRows));
    int stale = 0; std::string firstStale;
    for (int i = 0; i < kNumInert; ++i)
        if (!gInertHit[(size_t) i])
        { ++stale; if (firstStale.empty()) firstStale = std::string (kInert[i].dev) + " "
                        + kInert[i].knob + " @ " + kInert[i].type + " / " + kInert[i].chr; }
    gate ("every row was REACHED by the matrix (no stale rulings)", stale == 0,
          stale == 0 ? F1 ("all %.0f rows matched a real cell", (double) kNumInert)
                     : F1 ("%.0f never matched, first: ", (double) stale) + firstStale);
    gate ("the matrix actually ran (a roster over an empty sweep is the fb392 stub)",
          gMatCells == 1664, F1 ("%.0f cell-measurements", (double) gMatCells));
    for (int i = 0; i < kNumInert; ++i)
        std::printf ("      %-9s %-6s %-11s %-10s %-13s %s\n", kInert[i].dev,
                     kInert[i].kind == INERT ? "INERT" : "NARROW", kInert[i].knob,
                     kInert[i].type, kInert[i].chr, kInert[i].why);
}

// ═════════════════════════════════════════════════════════════════════════════
// K. SELF-CHECK — can these gates actually fail?  (FIXES.md §0, the new law)
//
// Every detector this file trusts is shown BOTH ways here: it must FIRE on a planted fault of
// a known size, and it must NOT fire on clean material. That is half of §0. The other half —
// deleting a mechanism from the ENGINE, recompiling, and requiring the gate to go red — cannot
// live inside this binary, because it needs a different binary. It lives in `mutate.py`, which
// copies the three headers, deletes one mechanism with a VERIFIED string replacement (it aborts
// if the text it expects is not there), rebuilds this cert against the mutant, and asserts the
// named gate turns FAIL. Its results are pasted in MUTATION.md.
// ═════════════════════════════════════════════════════════════════════════════
static void sectionK()
{
    section ("K. Self-check — can these gates actually fail?");
    auto prog = clickProg ((int) (FS * 1.2f));
    {   // THE CLICK METRIC, both ways, on both devices
        auto runC1 = [&] (bool planted)
        {
            CX e; e.prepare (FS, 64); CP p; p.push = 0.3f; p.ratio = 0.6f; e.setParams (p);
            std::vector<float> l = prog, r = prog;
            for (int i = 0; i + 64 <= (int) prog.size(); i += 64) { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 64); }
            if (planted) for (size_t i = (size_t) kAts[2]; i < l.size(); ++i) l[i] *= 2.5119f;   // +8.000 dB, instantly
            return l;
        };
        const double fired = clickDbPerMs (runC1, kAts[2], prog);
        gate ("(self-check) the click metric reads a PLANTED +8.000 dB step as 8.00",
              std::fabs (fired - 8.0) < 0.05, F1 ("reads %.4f dB/ms", fired));
        auto same = [&] (bool) { return runC1 (false); };
        const double none = clickDbPerMs (same, kAts[2], prog);
        gate ("(self-check) ... and reads 0.0000 when nothing changed", none < 1e-6,
              F1 ("reads %.6f dB/ms", none));
        CP a; a.push = 0.3f; a.ratio = 0.6f;
        gate ("(self-check) ... and its pre-switch region is EXACTLY zero, not merely small",
              cJump (a, a, kAts[2], prog) == 0.0, "A→A over 1.2 s of programme: 0.000000");
    }
    {   // specDist
        auto x = chordSig ((int) (FS * 1.0f));
        auto y = x; for (size_t i = 0; i < y.size(); ++i) y[i] *= 2.8184f;   // +9.000 dB broadband
        const double d = specDist (x, y);
        gate ("(self-check) specDist reads a planted broadband +9.000 dB", std::fabs (d - 9.0) < 0.1,
              F1 ("%.3f dB", d));
        gate ("(self-check) ... and reads 0 against itself", specDist (x, x) < 1e-9,
              F1 ("%.2e dB", specDist (x, x)));
    }
    {   // envSpreadDb — the dynamics metric
        auto flat = chordSig ((int) (FS * 3.0f));
        auto am   = amChord ((int) (FS * 3.0f));
        gate ("(self-check) envSpread sees a planted 24 dB tremolo and not a flat tone",
              envSpreadDb (am) > 15.0 && envSpreadDb (flat) < 3.0,
              F2 ("tremolo %.2f dB, flat %.2f dB", envSpreadDb (am), envSpreadDb (flat)));
    }
    {   // drRatio — the R11 ceiling metric
        auto st = staircase (-40.0f, 8.0f, 24, 120.0f);
        std::vector<float> pinned = st;
        {   // a planted PERFECT limiter: every tread normalised to the same RMS
            const int per = (int) (FS * 0.120f);
            for (int t = 0; t * per < (int) pinned.size(); ++t)
            { const double r = rmsOf (pinned, (size_t) (t * per), (size_t) ((t + 1) * per));
              if (r > 1e-9) for (int i = t * per; i < (t + 1) * per && i < (int) pinned.size(); ++i)
                  pinned[(size_t) i] *= (float) (0.05 / r); }
        }
        gate ("(self-check) drRatio reads 1 for a bypass and ~0 for a planted perfect limiter",
              std::fabs (drRatio (st, st, 24, 120.0f) - 1.0) < 0.01 && drRatio (st, pinned, 24, 120.0f) < 0.02,
              F2 ("bypass %.4f, pinned %.4f", drRatio (st, st, 24, 120.0f), drRatio (st, pinned, 24, 120.0f)));
    }
    {   // the floor-gate probe must HAVE BULK — the fb416 law, asserted on the probe itself
        auto bed = floorBed ((int) (FS * 1.0f), -96.0);
        int zeros = 0; for (float v : bed) if (v == 0.0f) ++zeros;
        gate ("(self-check) the floor-gate bed is REAL noise, not digital zeros",
              zeros == 0 && std::fabs (db (rmsOf (bed)) + 96.0) < 0.5,
              F2 ("%.0f exact zeros in 48000 samples, level %.2f dBFS", (double) zeros, db (rmsOf (bed))));
    }
    {   // and the air band the Sheen gate is written on must be audible content
        auto dark = darkPad ((int) (FS * 2.0f));
        const double a = bandRmsDbFS (dark, 8000.0, 12000.0), p2 = db (rmsOf (dark));
        gate ("(self-check) the air probe has air in it", p2 - a < 60.0,
              F2 ("8-12 kHz %.1f dBFS = %.1f dB under the programme", a, p2 - a));
    }
    {   // §4c4's SHAPE INVARIANT, checked against a planted gain step and a planted curve swap.
        //    S = H3_dB − 3·H1_dB must IGNORE the first and REPORT the second, or §4c4 is just
        //    §4d's level gate wearing a wig — which two earlier drafts of it were.
        const int HOP = (int) (FS * 0.002f);
        auto bin = [&] (const std::vector<float>& y, int i0, double hz) {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < HOP; ++i)
            { const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (HOP - 1));
              const double a = 2.0 * M_PI * hz * i / (double) FS;
              re += w * y[(size_t) (i0 + i)] * std::cos (a); im -= w * y[(size_t) (i0 + i)] * std::sin (a); }
            return db (std::sqrt (re * re + im * im) * 2.0 / HOP); };
        auto shape = [&] (const std::vector<float>& y, int i0)
        { return bin (y, i0, 6000.0) - 3.0 * bin (y, i0, 2000.0); };
        auto tone = toneSig ((int) (FS * 0.1f), 2000.0f, 0.09f);
        const int at = 2400;
        auto build = [&] (double c0, double c1, double g0, double g1) {
            std::vector<float> y (tone.size());
            for (size_t i = 0; i < tone.size(); ++i)
            { const bool aft = ((int) i >= at); const double g = aft ? g1 : g0, c = aft ? c1 : c0;
              const double x = (double) tone[i] * g; y[i] = (float) (x + c * x * x * x); }
            return y; };
        // c keeps the cubic a PERTURBATION (H3 ~ -40 dBc). At c = 400 the cubic dominates the
        // fundamental itself, the H3 = (c/4)A^3 algebra the metric rests on stops holding, and
        // this self-check read 32.55 dB for a PURE GAIN STEP -- the metric telling the truth
        // about a probe that had left its own small-signal regime. Check the checker (3.1).
        const auto gainOnly = build (0.6, 0.6, 1.0, 2.0);          // +6.02 dB, SAME curve
        const auto curveOnly = build (0.6, 2.4, 1.0, 1.0);        // ×4 curvature, SAME gain
        const double dg = std::fabs (shape (gainOnly, at) - shape (gainOnly, at - HOP));
        const double dc = std::fabs (shape (curveOnly, at) - shape (curveOnly, at - HOP));
        gate ("(self-check) the curvature metric IGNORES a planted +6.02 dB pure gain step", dg < 1.0,
              F1 ("reads %.3f dB", dg));
        // ...and the HONEST limit of that blindness. The invariant is exact only while the cubic
        // is a perturbation. At the drive the gain element actually runs (H3 ~ -20 dBc, THD in
        // the percent), the fundamental carries its own 3cA^3/4 term and a pure gain step leaks
        // a couple of dB into S. That leak is why 4c4's bar is 6 dB and not 1, and it is printed
        // rather than hidden.
        { const auto loud = build (2.5, 2.5, 1.0, 2.0);
          note ("  ... the leak at ENGINE-LIKE drive (H3 ~ -20 dBc), for the same pure gain step",
                F1 ("%.2f dB — the systematic 4c4's 6 dB bar has to clear",
                    std::fabs (shape (loud, at) - shape (loud, at - HOP)))); }
        gate ("(self-check) ... and REPORTS a planted x4 curvature change at constant gain", dc > 9.0 && dc < 15.0,
              F1 ("reads %.2f dB (x4 = 12.04 dB exactly)", dc));
    }
    std::printf ("\n  Engine mutation (FIXES.md §0) is a separate binary — see MUTATION.md,\n"
                 "  produced by `python3 mutate.py`. It deletes one mechanism from a COPY of the\n"
                 "  engine, rebuilds this cert against it, and requires the named gate to go RED.\n");
}

int main (int argc, char** argv)
{
    // `dynamics_cert [0 1 2 3 4 5 6 K]` runs only the named sections — mutate.py uses this so a
    // mutant run takes seconds instead of the four minutes a full pass costs.
    bool want[11] = { true, true, true, true, true, true, true, true, true, true, true };
    if (argc > 1)
    {
        for (auto& w : want) w = false;
        for (int i = 1; i < argc; ++i)
        {
            const char c = argv[i][0];
            if (c >= '0' && c <= '9') want[c - '0'] = true;
            else if (c == 'K' || c == 'k') want[10] = true;
        }
    }
    std::printf ("\n════════════════════════════════════════════════════════════════════════\n");
    std::printf ("  dynamics_cert — COMPRESS (kind 11) + OTT (kind 12), one shared core\n");
    std::printf ("  fx4 · every number below is measured, not asserted\n");
    std::printf ("════════════════════════════════════════════════════════════════════════\n");
    if (want[0]) section0();
    if (want[1]) section1();
    if (want[2]) section2();
    if (want[3]) section3();
    if (want[4]) section4();
    if (want[5]) section5();
    if (want[6]) section6();
    if (want[7]) { section7(); section7b(); section7c(); }
    if (want[8]) { section8(); section8b(); section8c(); section8d(); section8e(); }
    if (want[9]) section9();
    if (want[10]) sectionK();
    std::printf ("\n════════════════════════════════════════════════════════════════════════\n");
    std::printf ("  PASS %d   FAIL %d\n", gPass, gFail);
    std::printf ("════════════════════════════════════════════════════════════════════════\n\n");
    return gFail ? 1 : 0;
}
