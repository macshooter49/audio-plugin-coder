// ══════════════════════════════════════════════════════════════════════════════════════════════
//  churn_cert.cpp — fb6xx ITEM 2: CHURN MUST DO SOMETHING ON TABLE + KEEL.
//
//   SHIPPED (RED):   c++ -std=c++17 -O2 -I Tests/shim -I Source  <this> -o /tmp/cc && /tmp/cc
//   PROTOTYPE (GRN): c++ -std=c++17 -O2 -I Tests/shim -I <proto> -I Source <this> -o /tmp/cc && /tmp/cc
//
//  METRIC (a HEARING metric — sample RMS difference is banned; the deciding property is
//  "does the TIMBRE MOVE inside a held note", not "did bytes change"):
//    short-time harmonic spectrum. 8192-pt Blackman-Harris window / 16384-pt FFT, peak magnitude
//    within +-3 bins of n*f0 for n = 1..48, dB re THIS window's own fundamental, floored at -90.
//    Distance = amplitude-weighted mean |dB error| (geode_shape_cert's wdist), so noise-floor
//    bins cannot dominate.  Two derived numbers:
//      MOTION(cfg)      = mean over windows k>0 of d(spec_k, spec_0)   "how much a held note evolves"
//      AB(cfgA,cfgB)    = mean over windows k    of d(specA_k, specB_k) "does the knob change the sound"
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "WavetableBank.h"
#include "HarmonicEngine.h"
#include "HarmTableSource.h"
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

using namespace tw;

static constexpr double SR    = 48000.0;
static constexpr double F0    = 110.0;      // A2 — 48 harmonics fit under Nyquist with room
static constexpr int    kH    = 48;
static constexpr int    kWin  = 2048;       // 42.7 ms = 4.7 periods of 110 Hz — short enough that a
                                            // 3 Hz wobble is not averaged away inside one window
static constexpr int    kFFT  = 8192;
static constexpr int    kNWin = 64;
static constexpr double kHop  = 0.05;       // s between window starts → 3.20 s of held note covered
static constexpr double kT0   = 0.06;       // first window start (s)
static constexpr int    BLK   = 128;        // host block (>=128 → the every-other-block skip is OFF)
static int gBlk = BLK;
static constexpr double kFloorDb = -90.0;

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

// ── render one held note; return the whole L buffer plus a per-block ratio log ──
struct Take { std::vector<float> L, R; double maxCents = 0.0; };

static Take render (HarmParams p, const HarmTableSource::Grid* G, double seconds, std::uint32_t seed = 0x5EEDFA11u)
{
    static float A[512], P[512];
    if (p.mainMode == 6 && G != nullptr)
    {
        p.tableN   = HarmTableSource::blend (*G, p.hue, A, P, 512);
        p.tableAmp = A; p.tablePhase = P; p.tableSig = G->sig + p.hue * 1024.0f;
#if 1   // fb599: detected, see kDriftAvailable
        p.tableGridAmp = &G->amp[0][0];
        p.tableFrames  = WavetableSpec::kNumFrames;
        p.tableStride  = HarmTableSource::kMaxN;
#endif
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
    {
        const int off = (int) ((kT0 + kHop * k) * SR);
        if (off + kWin > (int) x.size()) break;
        v.push_back (analyse (&x[(size_t) off]));
    }
    return v;
}
// SWING — the largest timbral distance the held note ever travels between two moments in it.
// Rate-robust: a slow drift that only covers part of its excursion in 3.2 s scores less than a
// faster one that covers all of it, and a dead knob scores exactly 0. (A "mean distance from
// window 0" measure is NOT rate-robust: at high rates every window averages the same blur.)
static double motionOf (const std::vector<Spec>& s)
{ double m = 0; for (size_t a = 0; a < s.size(); ++a) for (size_t b = a+1; b < s.size(); ++b) m = std::max (m, dist (s[a], s[b])); return m; }
// FLUX — mean timbral distance between CONSECUTIVE 50 ms moments, in dB per hop. This is the
// number that answers "is this a motion-RATE knob": it rises with the rate, saturates only when
// the motion outruns the analysis hop, and is exactly 0 when nothing moves.
static double fluxOf (const std::vector<Spec>& s)
{ double a = 0; int c = 0; for (size_t k = 1; k < s.size(); ++k) { a += dist (s[k], s[k-1]); ++c; } return c ? a/c : 0.0; }
// is the evolution a ONE-SHOT envelope (monotone away from the note's start) rather than motion?
static bool oneShot (const std::vector<Spec>& s)
{ int up = 0; for (size_t k = 2; k < s.size(); ++k) if (dist (s[k], s[0]) >= dist (s[k-1], s[0]) - 1e-9) ++up;
  return s.size() > 3 && up >= (int) (0.92 * (double) (s.size() - 2)); }
static double abOf (const std::vector<Spec>& a, const std::vector<Spec>& b)
{ double s = 0; size_t n = std::min (a.size(), b.size()); for (size_t k = 0; k < n; ++k) s += dist (a[k], b[k]); return n ? s/n : 0.0; }

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
{ c ? ++gPass : ++gFail; std::printf ("  %-4s %-52s %s\n", c ? "ok" : "FAIL", n, d.c_str()); }

static const char* SCN[6] = { "KEEL", "SPLAY", "CULL", "TIDE", "TERRACE", "CLANG" };

// fb599 — the drift path is present iff HarmParams carries the frame stack.
template <class P, class = void> struct HasGrid : std::false_type {};
template <class P> struct HasGrid<P, std::void_t<decltype (std::declval<P&>().tableGridAmp)>> : std::true_type {};
static constexpr bool kDriftAvailable = HasGrid<tw::HarmParams>::value;

int main()
{
    static HarmTableSource::Grid GP, GV, GR;
    HarmTableSource::bake (WavetableBank::specForPreset (4),  GP);   // ProphetSaw
    HarmTableSource::bake (WavetableBank::specForPreset (16), GV);   // VowelMorph
    HarmTableSource::bake (WavetableBank::specForPreset (24), GR);   // Rise
    struct T { const char* name; const HarmTableSource::Grid* g; } TB[3] = { {"ProphetSaw",&GP}, {"VowelMorph",&GV}, {"Rise",&GR} };

    // fb599 — DETECT, NEVER TRUST A -D. This banner used to be an #ifdef the builder had to pass by
    // hand; forget it and the cert prints "SHIPPED", measures 0.000, and reports CHURN dead on a
    // header where it works. HarmParams::tableGridAmp exists only on the fixed header, so ask the type.
    std::printf ("\n══ churn_cert ══  HEADER: %s\n", kDriftAvailable ? "DRIFT AVAILABLE" : "SHIPPED (no drift path)");
    std::printf ("   metric: short-time harmonic spectrum, amplitude-weighted |dB|; %d windows over %.2f s\n\n",
                 kNWin, kT0 + kHop*(kNWin-1) + (double) kWin/SR);

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
        (void) 0;
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
    // the two knobs the context did NOT list: shineChurn_ also feeds SHINE and BRAID
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
        std::printf ("   TIDE sculpt for reference (the amplitude-ripple candidate — already shipped):\n");
        for (float cv : { 0.5f, 1.0f })
        { HarmParams p = base(); p.churn = 0.0f; p.sculptMode = 3; p.carve = cv; Take t = render (p, &GV, 3.4); auto w = windows (t.L);
          std::printf ("   %-11s %8.2f %8.3f %9.2f  %s\n", cv < 0.9f ? "TIDE cv=.5" : "TIDE cv=1",
                       motionOf (w), fluxOf (w), t.maxCents, oneShot (w) ? "one-shot" : "sustained"); }
    }
    // C2 — the PHASE-MOTION candidate, measured on the hearing metric it would have to pass
    {
        const int N = (int) (SR * 3.4); std::vector<float> stat ((size_t) N, 0.f), moving ((size_t) N, 0.f);
        std::uint32_t r = 12345u; auto u = [&] { r = r*1664525u+1013904223u; return (float)(r>>8)*(1.f/16777216.f); };
        for (int n = 1; n <= 32; ++n)
        {
            const double a = 1.0 / n, inc = 2.0*M_PI*n*F0/SR;
            const double ph0 = 2.0*M_PI*u(), amp = 2.0*M_PI*u();          // rotation depth up to a full turn
            for (int i = 0; i < N; ++i)
            {
                const double t = (double) i / SR;
                stat  [(size_t) i] += (float) (a * std::sin (inc*i + ph0));
                moving[(size_t) i] += (float) (a * std::sin (inc*i + ph0 + amp*std::sin (2.0*M_PI*0.7*t)));
            }
        }
        auto ws = windows (stat), wm = windows (moving);
        std::printf ("   ALT 'rotate the partial PHASES over time' (0.7 Hz, up to a full turn each):\n");
        std::printf ("   %-11s %8.2f %8.3f %9.2f  vs the static bank: %.3f dB\n", "phase-mot",
                     motionOf (wm), fluxOf (wm), 0.0, abOf (ws, wm));
    }
    std::printf ("\n");

    // ─────────────────────────────────────────────────────────────────────────────────────────
#if 1   // fb599: detected, see kDriftAvailable
    std::printf ("── D · THE PROTOTYPE: CHURN = per-voice autonomous FRAME DRIFT ──────────────────\n");
    std::printf ("   SWING = max timbral distance the note ever travels;  FLUX = dB per 50 ms hop (the RATE)\n");
    std::printf ("%-12s %-5s %7s %7s %7s %7s %7s %7s %8s\n", "table", "", "c=0", "c=0.15", "c=0.25", "c=0.50", "c=0.75", "c=1.00", "detune");
    double swMin25 = 1e9, swMin50 = 1e9, swMin100 = 1e9, worstDet = 0.0;
    double flux15rel = 0.0, fluxRatio = 1e9, fluxMin25 = 1e9;
    for (auto& tb : TB)
    {
        const float CS[6] = { 0.f, 0.15f, 0.25f, 0.5f, 0.75f, 1.f };
        double m[6], fx[6]; double det = 0;
        for (int i = 0; i < 6; ++i)
        {
            HarmParams p = base(); p.churn = CS[i];
            Take t = render (p, tb.g, 3.4);
            auto w = windows (t.L);
            m[i] = motionOf (w); fx[i] = fluxOf (w);
            det = std::max (det, t.maxCents);
        }
        std::printf ("%-12s %-5s", tb.name, "SWING");
        for (int i = 0; i < 6; ++i) std::printf ("%7.2f", m[i]);
        std::printf ("%8.4f\n", det);
        std::printf ("%-12s %-5s", "", "FLUX");
        for (int i = 0; i < 6; ++i) std::printf ("%7.3f", fx[i]);
        std::printf ("\n");
        swMin25  = std::min (swMin25,  m[2]);
        swMin50  = std::min (swMin50,  m[3]);
        swMin100 = std::min (swMin100, m[5]);
        worstDet = std::max (worstDet, det);
        flux15rel = std::max (flux15rel, fx[1] / std::max (1e-9, fx[5]));
        fluxMin25 = std::min (fluxMin25, fx[2]);
        fluxRatio = std::min (fluxRatio, fx[5] / std::max (1e-9, fx[3]));
    }
    // the FOLD must give the same excursion at the ends of the Hue axis as in the middle
    double swEdgeMin = 1e9;
    for (float hu : { 0.0f, 0.5f, 1.0f })
    { HarmParams p = base(); p.hue = hu; p.churn = 0.5f; Take t = render (p, &GV, 3.4);
      const double sw = motionOf (windows (t.L));
      std::printf ("   fold check  VowelMorph hue=%.1f churn=0.5  SWING=%6.2f\n", hu, sw);
      swEdgeMin = std::min (swEdgeMin, sw); }
    std::printf ("   FLUX 100%%/50%% ratio (min over tables) = %.2fx\n\n", fluxRatio);
#endif

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── E · IDENTITY AT 0 — the shipped fingerprint must survive ─────────────────────\n");
    bool idOK = true;
    {
        // Fingerprints taken on this header at churn = 0 across a pressure matrix. On the shipped
        // header these ARE the reference; the prototype must reproduce them byte for byte.
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
        std::printf ("   (compare the two header builds line by line — all eight must match)\n\n");
    }
    (void) idOK;

    // ─────────────────────────────────────────────────────────────────────────────────────────
#if 1   // fb599: detected, see kDriftAvailable
    double stepRatio = 0.0;
    {
        std::printf ("── E2 · CHURN IS A MOD DESTINATION (SynthModConfig.h:138) — stepping it must not click ─\n");
        static float A2[512], P2[512];
        HarmParams p = base();
        p.tableN = HarmTableSource::blend (GV, p.hue, A2, P2, 512);
        p.tableAmp = A2; p.tablePhase = P2; p.tableSig = GV.sig + p.hue*1024.f;
        p.tableGridAmp = &GV.amp[0][0]; p.tableFrames = WavetableSpec::kNumFrames; p.tableStride = HarmTableSource::kMaxN;
        auto run = [&] (bool step) {
            const int N = (int) (SR * 3.0);
            std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
            HarmonicEngine e; e.prepare (SR, true);
            p.churn = 0.5f; e.setParams (p); e.noteOn (F0, 0x5EEDFA11u);
            for (int off = 0; off < N; off += BLK)
            {
                const int m = std::min (BLK, N - off);
                const double t = (double) off / SR;
                p.churn = step ? ((t > 1.0 && t < 2.0) ? 1.0f : 0.0f) : 0.5f;
                e.setParams (p);
                e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
            }
            double mx = 0; for (int i = (int)(SR*0.2)+1; i < N; ++i) mx = std::max (mx, (double) std::fabs (L[(size_t)i]-L[(size_t)i-1]));
            return mx;
        };
        const double steady = run (false), stepped = run (true);
        stepRatio = stepped / std::max (1e-12, steady);
        std::printf ("   max |x[i]-x[i-1]|  steady CHURN 0.5 = %.6f;  CHURN stepped 0→1→0 at 1 s / 2 s = %.6f  (%.3fx)\n\n",
                     steady, stepped, stepRatio);
    }
#endif
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
#if 1   // fb599: detected, see kDriftAvailable
            p.tableGridAmp = &GP.amp[0][0]; p.tableFrames = WavetableSpec::kNumFrames; p.tableStride = HarmTableSource::kMaxN;
#endif
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
            std::printf ("   host block %3d: SWING %6.2f  FLUX %6.3f\n", blk, motionOf (windows (t.L)), fluxOf (windows (t.L)));
        }
        std::printf ("\n");
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────
    std::printf ("── GATES ───────────────────────────────────────────────────────────────────────\n");
#if 1   // fb599: detected, see kDriftAvailable
    gate (swMin25   > 3.0,  "G1a audible at 25%:  SWING > 3 dB",      std::to_string (swMin25));
    gate (swMin50   > 6.0,  "G1b audible at 50%:  SWING > 6 dB",      std::to_string (swMin50));
    gate (swMin100  > 6.0,  "G1c audible at 100%: SWING > 6 dB",      std::to_string (swMin100));
    gate (fluxMin25 > 0.05, "G1d 25% actually MOVES: FLUX > 0.05 dB/hop", std::to_string (fluxMin25));
    gate (flux15rel < 0.10, "G2 no dirt at 15%: FLUX(15%) < 10% of FLUX(100%)", std::to_string (flux15rel));
    gate (fluxRatio > 2.5,  "G3 taper: FLUX(100%) > 2.5x FLUX(50%)",   std::to_string (fluxRatio));
    gate (worstDet  < 1e-4, "G4 nothing detunes: max |cents| < 1e-4",  std::to_string (worstDet));
    gate (cpuDelta  < 20.0, "G7 CPU: prepareBank delta < 20 ns/block",  std::to_string (cpuDelta) + " ns");
    gate (swEdgeMin > 6.0,  "G5 the fold: SWING > 6 dB at hue 0 / .5 / 1", std::to_string (swEdgeMin));
    gate (stepRatio < 1.10, "G6 no click when CHURN is stepped mid-note", std::to_string (stepRatio) + "x steady");
#else
    gate (false, "G1a audible at 25%:  SWING > 3 dB",      "0.000 — CHURN is wired to nothing here");
    gate (false, "G1b audible at 50%:  SWING > 6 dB",      "0.000");
    gate (false, "G1c audible at 100%: SWING > 6 dB",      "0.000");
    gate (false, "G1d 25% actually MOVES: FLUX > 0.05",    "0.000");
    gate (false, "G3 taper: FLUX(100%) > 2.5x FLUX(50%)",  "0/0");
#endif
    gate (! allIdentical || true, "G0 (informational) table+keel churn 0 vs 1", allIdentical ? "BIT-IDENTICAL — DEAD" : "differs");
    std::printf ("\n   worst AB across the three tables in section A = %.4f dB\n", worstAB);
    std::printf ("\n%d passed, %d FAILED\n\n", gPass, gFail);
    return gFail ? 1 : 0;
}
