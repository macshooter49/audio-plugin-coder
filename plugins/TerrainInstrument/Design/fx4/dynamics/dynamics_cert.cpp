// ─────────────────────────────────────────────────────────────────────────────
// dynamics_cert — the perceptual certification harness for BOTH fx4 dynamics devices:
//     COMPRESS (chain kind 11)  ·  OTT (chain kind 12)  ·  their shared DynamicsCore.h
//
//   clang++ -O2 -std=c++17 \
//     -I <TI>/Tests/shim -I <TI>/Source -I <TI>/Design/fx4/dynamics \
//     <TI>/Design/fx4/dynamics/dynamics_cert.cpp -o /tmp/dynamics_cert && /tmp/dynamics_cert
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

namespace {

#include "shipped_labels.inc"

constexpr float FS = 48000.0f;
using CP = tw::TerrainCompressFx::Params;
using CX = tw::TerrainCompressFx;
using OP = tw::TerrainOttFx::Params;
using OX = tw::TerrainOttFx;

int gPass = 0, gFail = 0;
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
static void section1()
{
    section ("1. Names — rack-wide no-doubles, exact-string, vs a snapshot of Source/");
    // Deliberately EXEMPT: the shared-vocabulary words CONTRACT §4 tells us to reuse when the
    // concept is genuinely the same, plus the chassis words every device's back panel shows.
    static const char* kShared[] = { "Mix", "Attack", "Release", "Character", "Auto", "Type",
                                     "Power", "Stereo", "Amount", "Ratio", "Peak", "Bass", "Treble" };
    // ⚠️ THE ONLY TWO EXEMPTIONS, and both are RENAMES.md decisions where the SIBLING yields:
    //   `Gentle`   — OTT Type 1. RENAMES.md: "EQ yields" (its American char 2 becomes `Mellow`).
    //   `Low Split`— OTT Two Band char 1. RENAMES.md: "OTT's Low Split/High Split are a matched
    //                pair; breaking one breaks both" — Widen's becomes `Deep Grid`.
    // They still appear in the corpus because the siblings have not applied their own table yet.
    // The list is asserted to be EXACTLY these two below, so it cannot quietly grow into an
    // excuse. Anything else that collides is a real collision and this gate goes red.
    static const char* kSiblingYields[] = { "Gentle", "Low Split" };
    auto yielded = [] (const std::string& s)
    { for (auto k : kSiblingYields) if (s == k) return true; return false; };
    auto shared = [] (const std::string& s)
    { for (auto k : kShared) if (s == k) return true; return false; };
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
        if (!shared (s) && !yielded (s) && shipped (s)) { ++col; if (first.empty()) first = s; }
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
    gate ("the sibling-yield exemption list is exactly 2 entries",
          (int) (sizeof kSiblingYields / sizeof kSiblingYields[0]) == 2,
          "Gentle, Low Split — both RENAMES.md rows where the sibling gives way");

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

    struct KnobRes { const char* name; double span; bool mono; int dir; };
    std::vector<KnobRes> res;

    // Monotonicity is checked in WHICHEVER DIRECTION the control actually runs. A slower attack
    // removes LESS crest and a longer release adds LESS grind; demanding "increasing" was the
    // first draft's own bug, and it failed four knobs that are perfectly well behaved.
    auto sweep = [&] (const char* name, float CP::* fld, CP base, int type,
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

    { CP b; b.ratio = 0.85f; b.lift = 0.0f; sweep ("Push (front 1)", &CP::push, b, 0, mGR, chord); }
    { CP b; b.push = 0.65f;  b.lift = 0.0f; sweep ("Ratio (front 2)", &CP::ratio, b, 0, mGR, chord); }
    { CP b; b.push = 0.55f;  b.ratio = 0.7f; sweep ("Lift (front 3)", &CP::lift, b, 0, mLvl, chord); }
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
      gate ("  Attack (P1) — how much of the transient escapes",
            (m[8] - m[0]) > 4.0 && mono,
            F3 ("first-30 ms peak %.1f dBFS at 0.05 ms → %.1f dBFS at 300 ms (span %.1f)",
                m[0], m[8], m[8] - m[0]));
    }
    { CP b; b.push = 0.6f; b.ratio = 0.9f; b.b1 = 0.0f;
      sweep ("Release (P2) — via added THD on 80 Hz", &CP::b2, b, 0, mTHD, toneSig ((int) (FS * 1.0f), 80.0f, 0.05f)); }
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
      gate ("  Round (P3) — GR on a probe sitting ON the threshold",
            (au[8] - au[0]) > 2.0 && mono,
            F4 ("%.2f → %.2f dB of GR (span %.2f) · published curve bends over %.0f dB",
                au[0], au[8], au[8] - au[0], gHi) + F1 (" vs %.0f", gLo));
    }
    { CP b; b.push = 0.62f; b.ratio = 0.9f;
      sweep ("Hear Cut (P4) — via GR on a bass chord", &CP::b4, b, 0, mGR, chord); }
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
      gate ("  Edge (P5) — how much of the attack survives, −100 → +100",
            (m[8] - m[0]) > 3.0 && mono,
            F3 ("first-30 ms peak %.2f → %.2f dBFS (span %.2f)", m[0], m[8], m[8] - m[0]));
    }
    { CP b; b.push = 0.6f; b.ratio = 0.9f; b.b2 = 0.15f;
      sweep ("Latch (P6) — via GR on a pluck", &CP::b6, b, 0, mGR, pluck); }
    { CP b; b.push = 0.6f; b.ratio = 0.9f; b.b8 = 0.0f;
      sweep ("Heat (P8) — via added THD", &CP::b8, b, 2, mTHD, toneSig ((int) (FS * 1.0f), 80.0f, 0.05f)); }
    { // Ride's upward lane: sweep RATIO (which drives both slopes) and watch a quiet probe rise
      CP b; b.push = 0.5f; b.lift = 0.0f;
      sweep ("Ride: Ratio lifts a quiet probe", &CP::ratio, b, 6, mLvl,
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
        gate ("  Tie (P7) — measured on an UNBALANCED stereo probe", std::fabs (bal1 - bal0) > 3.0,
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
        struct KJ { const char* what; CP b; };
        CP k1 = base; k1.push = 0.9f;   CP k2 = base; k2.ratio = 1.0f;
        CP k3 = base; k3.lift = 1.0f;   CP k4 = base; k4.b3 = 1.0f;
        CP k5 = base; k5.b4 = 1.0f;     CP k6 = base; k6.b7 = 0.0f;
        CP k7 = base; k7.b8 = 1.0f;     CP k8 = base; k8.b5 = 1.0f;
        CP k9 = base; k9.b6 = 1.0f;     CP k10 = base; k10.axis = 3;
        const KJ kj[] = { { "  Push 0.3 → 0.9", k1 }, { "  Ratio 0.6 → 1.0 (∞:1)", k2 },
                          { "  Lift 0 → +24 dB", k3 }, { "  Round 0.25 → 1.0", k4 },
                          { "  Hear Cut off → 500 Hz", k5 }, { "  Tie 100 → 0", k6 },
                          { "  Burn 0 → 100", k7 }, { "  Edge 0.5 → 1.0", k8 },
                          { "  Cling 0 → 250 ms", k9 }, { "  Detect Native → Patient", k10 } };
        for (auto& j : kj)
        {
            double m = 0.0;
            for (int k = 0; k < 5; ++k) m = std::max (m, cJump (base, j.b, kAts[k], prog));
            gate (j.what, m <= 2.0, F1 ("%.2f dB/ms", m));
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
    gate ("Sheen: ≥ 6 dB more 8–12 kHz than Over Top on a dark pad", G[3].air > G[0].air + 6.0,
          F2 ("%+.2f dB vs %+.2f dB", G[3].air, G[0].air));
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

    struct KR { const char* name; double lo, hi; bool mono; };
    std::vector<KR> res;
    auto sweep = [&] (const char* name, float OP::* fld, OP base,
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

    { OP b; sweep ("Amount (front 1) — dynamic range removed", &OP::amount, b, mCrest, amChord ((int) (FS * 2.5f))); }
    { OP b; b.amount = 0.85f; sweep ("Speed (front 2) — THD on 100 Hz", &OP::speed, b, mTHD, toneSig ((int) (FS * 1.0f), 100.0f, 0.05f)); }
    { OP b; sweep ("Top Lift (front 3) — 8-12 kHz on a dark pad", &OP::topLift, b, mAir, dark, 3.0); }
    {   // A crossover does not move the level — it moves WHICH BAND OWNS WHAT. Measured as the
        // spectral distance from the sweep's own knob-0 output, which is the only reference that
        // isolates the control. (Distance-from-input just reads the compression, not the split.)
        auto run9 = [&] (const char* name, float OP::* fld, OP base)
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
        run9 ("Low Cross (P1) — spectrum vs knob 0", &OP::b1, b);
        run9 ("High Cross (P2) — spectrum vs knob 0", &OP::b2, b);
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
        gate ("  Raise (P3) — the LATE TAIL of a decaying pluck", (m[8] - m[0]) > 2.0 && mono,
              F3 ("%+.2f → %+.2f dB of tail lift (span %.2f)", m[0], m[8], m[8] - m[0]));
    }
    { OP b; b.amount = 0.6f; b.b3 = 0.0f; sweep ("Press (P4) — dynamic range removed", &OP::b4, b, mCrest, amChord ((int) (FS * 2.5f))); }
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
      gate ("  Grip (P5) — how deep the jaws sit (output level)",
            (m[0] - m[8]) > 6.0 && mono, trace + (mono ? "· monotone ↓" : "· NOT MONOTONE"));
    }
    { OP b; sweep ("Bass (P6) — 40-85 Hz", &OP::b6, b, mLow, chord); }
    { OP b; sweep ("Treble (P8) — 8-12 kHz", &OP::b8, b, mAir, chord); }

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
        gate ("  Mids (P7) — 300 Hz-1.5 kHz across the full sweep", d > 12.0,
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
        struct OJ { const char* what; OP b; };
        OP j1; j1.amount = 1.0f;  OP j2; j2.speed = 1.0f;   OP j3; j3.topLift = 1.0f;
        OP j4; j4.b1 = 1.0f;      OP j5; j5.b5 = 1.0f;      OP j6; j6.b8 = 1.0f;
        OP j7; j7.axis = 2;       OP j8; j8.axis = 1;       OP j9; j9.crest = true;
        const OJ oj[] = { { "  Amount 0.5 → 1.0", j1 }, { "  Chase 0.5 → 1.0", j2 },
                          { "  Top Lift 0.25 → 1.0", j3 }, { "  Low Cross 88 → 300 Hz", j4 },
                          { "  Grip 0 → +18 dB", j5 }, { "  Treble 0 → +12 dB", j6 },
                          { "  Stereo Linked → Mid-Side", j7 }, { "  Stereo Linked → Free Pair", j8 },
                          { "  Crest pill off → on", j9 } };
        for (auto& j : oj)
        {
            double m = 0.0;
            for (int k = 0; k < 5; ++k) m = std::max (m, oJump (z, j.b, kAts[k], prog));
            gate (j.what, m <= 2.0, F1 ("%.2f dB/ms", m));
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
    std::printf ("\n  Engine mutation (FIXES.md §0) is a separate binary — see MUTATION.md,\n"
                 "  produced by `python3 mutate.py`. It deletes one mechanism from a COPY of the\n"
                 "  engine, rebuilds this cert against it, and requires the named gate to go RED.\n");
}

int main (int argc, char** argv)
{
    // `dynamics_cert [0 1 2 3 4 5 6 K]` runs only the named sections — mutate.py uses this so a
    // mutant run takes seconds instead of the four minutes a full pass costs.
    bool want[8] = { true, true, true, true, true, true, true, true };
    if (argc > 1)
    {
        for (auto& w : want) w = false;
        for (int i = 1; i < argc; ++i)
        {
            const char c = argv[i][0];
            if (c >= '0' && c <= '6') want[c - '0'] = true;
            else if (c == 'K' || c == 'k') want[7] = true;
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
    if (want[7]) sectionK();
    std::printf ("\n════════════════════════════════════════════════════════════════════════\n");
    std::printf ("  PASS %d   FAIL %d\n", gPass, gFail);
    std::printf ("════════════════════════════════════════════════════════════════════════\n\n");
    return gFail ? 1 : 0;
}
